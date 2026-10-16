// VM snapshot (save / load). See vm_snapshot.h and docs/save-format.md.
#include "script/vm_snapshot.h"

#include "script/file_io.h"
#include "script/host.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Squirrel internals (after the standard headers: they define macros such as type()).
#include <squirrel.h>

#include "sqpcheader.h"
#include "sqopcodes.h"
#include "sqfuncproto.h"
#include "sqvm.h"
#include "sqclosure.h"
#include "sqstring.h"
#include "sqtable.h"
#include "squserdata.h"
#include "sqarray.h"
#include "sqclass.h"

namespace omw {
namespace {

constexpr uint32_t kMagic = 0x53574D4F; // "OMWS"
constexpr uint32_t kVersion = 1;
constexpr uint32_t kNoObject = 0xFFFFFFFFu;

enum Tag : uint8_t { T_NULL, T_INT, T_FLOAT, T_BOOL, T_STR, T_REF, T_IGNORED, T_WEAK, T_ROOT, T_CLASS };
enum Kind : uint8_t { K_TABLE, K_ARRAY, K_INSTANCE, K_CLOSURE, K_NATIVE, K_THREAD, K_GENERATOR };

// ---------------------------------------------------------------------------------------------------------------------
// Reference table (CreateReferenceTable / ClearReferenceTable): the order function and the ignore list of the next save.
struct RefTable {
	bool valid = false;
	HSQOBJECT order_fn;
	std::vector<HSQOBJECT> ignore;
};
RefTable g_ref;

void clear_ref(HSQUIRRELVM v) {
	if (g_ref.valid) {
		sq_release(v, &g_ref.order_fn);
		for (HSQOBJECT& o : g_ref.ignore) sq_release(v, &o);
	}
	g_ref.valid = false;
	g_ref.ignore.clear();
}

// ---------------------------------------------------------------------------------------------------------------------
// Names of the things that are never stored: classes (by root key) and native functions (by where they are registered).
struct Registry {
	std::unordered_map<SQClass*, std::string> class_path;
	std::unordered_map<std::string, SQClass*> class_by_path;
	std::unordered_map<SQFUNCTION, std::string> fn_name;
	std::unordered_map<std::string, SQFUNCTION> fn_by_name;

	void note_fn(SQNativeClosure* nc, const std::string& name) {
		if (!nc || !nc->_function) return;
		fn_name.emplace(nc->_function, name);
		fn_by_name.emplace(name, nc->_function);
	}
	void scan_class(SQClass* c, const std::string& path) {
		SQObjectPtr ref, k, val;
		SQInteger n;
		while ((n = c->_members->Next(false, ref, k, val)) != -1) {
			ref = n;
			if (type(k) != OT_STRING || type(val) != OT_INTEGER || !_ismethod(val)) continue;
			const SQObjectPtr& m = c->_methods[_member_idx(val)].val;
			if (type(m) == OT_NATIVECLOSURE) note_fn(_nativeclosure(m), "c:" + path + ":" + _stringval(k));
		}
		for (size_t i = 0; i < c->_metamethods.size(); i++)
			if (type(c->_metamethods[i]) == OT_NATIVECLOSURE) note_fn(_nativeclosure(c->_metamethods[i]), "m:" + path + ":" + std::to_string(i));
	}
	void scan_table_natives(SQTable* t, const std::string& prefix) {
		SQObjectPtr ref, k, val;
		SQInteger n;
		while ((n = t->Next(false, ref, k, val)) != -1) {
			ref = n;
			if (type(k) == OT_STRING && type(val) == OT_NATIVECLOSURE) note_fn(_nativeclosure(val), prefix + _stringval(k));
		}
	}
	void build(SQVM* vm) {
		class_path.clear();
		class_by_path.clear();
		fn_name.clear();
		fn_by_name.clear();
		SQTable* root = _table(vm->_roottable);
		SQObjectPtr ref, k, val;
		SQInteger n;
		while ((n = root->Next(false, ref, k, val)) != -1) {
			ref = n;
			if (type(k) != OT_STRING) continue;
			const std::string key = _stringval(k);
			if (type(val) == OT_CLASS) {
				class_path.emplace(_class(val), key);
				class_by_path.emplace(key, _class(val));
				scan_class(_class(val), key);
			} else if (type(val) == OT_NATIVECLOSURE) {
				note_fn(_nativeclosure(val), "r:" + key);
			}
		}
		SQSharedState* ss = _ss(vm);
		const struct { SQObjectPtr* t; const char* name; } dg[] = {
			{&ss->_table_default_delegate, "table"},   {&ss->_array_default_delegate, "array"},     {&ss->_string_default_delegate, "string"},
			{&ss->_number_default_delegate, "number"}, {&ss->_generator_default_delegate, "gen"},   {&ss->_closure_default_delegate, "closure"},
			{&ss->_thread_default_delegate, "thread"}, {&ss->_class_default_delegate, "class"},     {&ss->_instance_default_delegate, "instance"},
			{&ss->_weakref_default_delegate, "weakref"}};
		for (const auto& d : dg)
			if (type(*d.t) == OT_TABLE) scan_table_natives(_table(*d.t), std::string("d:") + d.name + ":");
	}
};

// ---------------------------------------------------------------------------------------------------------------------
// Stream helpers over the native file.
struct Out {
	FileObj* f;
	bool ok = true;
	void raw(const void* p, size_t n) { ok = ok && file_obj_write(f, p, n); }
	template <class T> void pod(T v) { raw(&v, sizeof v); }
	void u8(uint8_t v) { pod(v); }
	void u32(uint32_t v) { pod(v); }
	void i64(int64_t v) { pod(v); }
	void str(const char* s, size_t n) { u32(static_cast<uint32_t>(n)); raw(s, n); }
	void str(const std::string& s) { str(s.data(), s.size()); }
};
struct In {
	FileObj* f;
	bool ok = true;
	void raw(void* p, size_t n) { ok = ok && file_obj_read(f, p, n); }
	template <class T> T pod() { T v{}; raw(&v, sizeof v); return v; }
	uint8_t u8() { return pod<uint8_t>(); }
	uint32_t u32() { return pod<uint32_t>(); }
	int64_t i64() { return pod<int64_t>(); }
	std::string str() {
		const uint32_t n = u32();
		std::string s;
		if (!ok || n > (1u << 28)) { ok = false; return s; }
		s.resize(n);
		if (n) raw(s.data(), n);
		return s;
	}
};

SQInteger proto_write(SQUserPointer up, SQUserPointer data, SQInteger size) {
	return file_obj_write(static_cast<Out*>(up)->f, data, static_cast<size_t>(size)) ? size : -1;
}
SQInteger proto_read(SQUserPointer up, SQUserPointer data, SQInteger size) {
	return file_obj_read(static_cast<In*>(up)->f, data, static_cast<size_t>(size)) ? size : -1;
}

SQObjectPtr make_string(SQVM* vm, const std::string& s) { return SQObjectPtr(SQString::Create(_ss(vm), s.c_str(), static_cast<SQInteger>(s.size()))); }

// ---------------------------------------------------------------------------------------------------------------------
// Calls into the script callbacks (this = root table).
bool call_script(HSQUIRRELVM v, const HSQOBJECT& fn, const SQObjectPtr* arg, SQObjectPtr* result) {
	const SQInteger top = sq_gettop(v);
	sq_pushobject(v, fn);
	sq_pushroottable(v);
	if (arg) sq_pushobject(v, *arg);
	const bool ok = SQ_SUCCEEDED(sq_call(v, arg ? 2 : 1, result ? SQTrue : SQFalse, SQTrue));
	if (ok && result) {
		HSQOBJECT o;
		sq_getstackobj(v, -1, &o);
		*result = SQObjectPtr(o);
	}
	sq_settop(v, top);
	return ok;
}

// The native SQFile behind the CNutFile registered under `name` in g_NutManager.
FileObj* find_file(HSQUIRRELVM v, const std::string& name, SQObjectPtr* nut_out) {
	const SQInteger top = sq_gettop(v);
	FileObj* result = nullptr;
	sq_pushroottable(v);
	sq_pushstring(v, "g_NutManager", -1);
	if (SQ_SUCCEEDED(sq_get(v, -2))) {
		const SQInteger mgr = sq_gettop(v);
		sq_pushstring(v, "get", -1);
		if (SQ_SUCCEEDED(sq_get(v, mgr))) {
			sq_push(v, mgr);
			sq_pushstring(v, name.c_str(), -1);
			if (SQ_SUCCEEDED(sq_call(v, 2, SQTrue, SQTrue))) {
				const SQInteger nut = sq_gettop(v);
				if (sq_gettype(v, nut) == OT_INSTANCE) {
					if (nut_out) {
						HSQOBJECT o;
						sq_getstackobj(v, nut, &o);
						*nut_out = SQObjectPtr(o);
					}
					sq_pushstring(v, "m_File", -1);
					if (SQ_SUCCEEDED(sq_get(v, nut))) {
						SQUserPointer p = nullptr;
						if (SQ_SUCCEEDED(sq_getinstanceup(v, -1, &p, nullptr))) result = static_cast<FileObj*>(p);
					}
				}
			}
		}
	}
	sq_settop(v, top);
	return result;
}

// ---------------------------------------------------------------------------------------------------------------------
struct Writer {
	HSQUIRRELVM hv;
	SQVM* vm;
	Registry reg;
	Out out;
	HSQOBJECT recreate_cb, reset_cb;

	struct Info {
		SQObjectPtr obj;
		Kind kind;
		bool native_inst = false;
		bool callback = false; // script instance: callbacks apply
		int64_t order = 0;
	};
	std::vector<Info> infos;
	std::unordered_map<void*, uint32_t> id_of;
	std::unordered_set<void*> ignored;
	std::unordered_map<SQFunctionProto*, uint32_t> proto_id;
	std::set<std::string> lost; // unsupported things met (for the log)
	std::vector<SQObjectPtr> work;
	SQTable* root = nullptr;

	Writer(HSQUIRRELVM v_, FileObj* f) : hv(v_), vm(v_), out{f} {}

	static void* raw_of(const SQObjectPtr& o) { return o._unVal.pRefCounted; }

	// --- collection -----------------------------------------------------------------------------------------------
	void add(const SQObjectPtr& o, Kind kind) {
		void* p = raw_of(o);
		if (ignored.count(p) || id_of.count(p)) return;
		Info inf;
		inf.obj = o;
		inf.kind = kind;
		id_of[p] = static_cast<uint32_t>(infos.size());
		infos.push_back(std::move(inf));
		work.push_back(o);
	}
	void visit(const SQObjectPtr& o) {
		switch (type(o)) {
		case OT_TABLE:
			if (_table(o) != root) add(o, K_TABLE);
			break;
		case OT_ARRAY: add(o, K_ARRAY); break;
		case OT_CLOSURE: add(o, K_CLOSURE); break;
		case OT_NATIVECLOSURE:
			if (reg.fn_name.count(_nativeclosure(o)->_function)) add(o, K_NATIVE);
			else lost.insert("native closure");
			break;
		case OT_THREAD: add(o, K_THREAD); break;
		case OT_GENERATOR:
			if (type(_generator(o)->_closure) == OT_CLOSURE) add(o, K_GENERATOR);
			break;
		case OT_INSTANCE:
			if (reg.class_path.count(_instance(o)->_class)) add(o, K_INSTANCE);
			else lost.insert("instance of an unnamed class");
			break;
		default: break; // primitives, strings, weak references (resolved at write time), classes (by path)
		}
	}

	void thread_extent(SQVM* t, SQInteger& n) {
		n = static_cast<SQInteger>(t->_stack.size());
		while (n > 0 && type(t->_stack._vals[n - 1]) == OT_NULL) n--;
	}

	void children(Info& inf) {
		const SQObjectPtr& o = inf.obj;
		switch (inf.kind) {
		case K_TABLE: {
			SQTable* t = _table(o);
			if (t->_delegate) visit(SQObjectPtr(t->_delegate));
			SQObjectPtr ref, k, val;
			SQInteger n;
			while ((n = t->Next(true, ref, k, val)) != -1) {
				ref = n;
				visit(k);
				visit(val);
			}
			break;
		}
		case K_ARRAY: {
			SQArray* a = _array(o);
			for (size_t i = 0; i < a->_values.size(); i++) visit(a->_values[i]);
			break;
		}
		case K_CLOSURE: {
			SQClosure* c = _closure(o);
			visit(c->_env);
			for (size_t i = 0; i < c->_outervalues.size(); i++) visit(c->_outervalues[i]);
			for (size_t i = 0; i < c->_defaultparams.size(); i++) visit(c->_defaultparams[i]);
			break;
		}
		case K_NATIVE: {
			SQNativeClosure* c = _nativeclosure(o);
			visit(c->_env);
			for (size_t i = 0; i < c->_outervalues.size(); i++) visit(c->_outervalues[i]);
			break;
		}
		case K_INSTANCE: {
			SQInstance* in = _instance(o);
			inf.native_inst = in->_hook != nullptr;
			const size_t n = in->_class->_defaultvalues.size();
			for (size_t i = 0; i < n; i++) visit(in->_values[i]);
			break;
		}
		case K_GENERATOR: {
			SQGenerator* g = _generator(o);
			visit(g->_closure);
			for (size_t i = 0; i < g->_stack.size(); i++) visit(g->_stack[i]);
			for (size_t i = 0; i < g->_vargsstack.size(); i++) visit(g->_vargsstack[i]);
			break;
		}
		case K_THREAD: {
			SQVM* t = _thread(o);
			SQInteger n;
			thread_extent(t, n);
			for (SQInteger i = 0; i < n; i++) visit(t->_stack._vals[i]);
			for (size_t i = 0; i < t->_vargsstack.size(); i++) visit(t->_vargsstack[i]);
			visit(t->_lasterror);
			for (SQInteger i = 0; i < t->_callsstacksize; i++) {
				visit(t->_callsstack[i]._closure);
				if (t->_callsstack[i]._generator) visit(SQObjectPtr(t->_callsstack[i]._generator));
			}
			break;
		}
		}
	}

	// --- encoding of values ---------------------------------------------------------------------------------------
	void put(const SQObjectPtr& o) {
		switch (type(o)) {
		case OT_NULL: out.u8(T_NULL); return;
		case OT_INTEGER: out.u8(T_INT); out.i64(_integer(o)); return;
		case OT_FLOAT: out.u8(T_FLOAT); out.pod<double>(_float(o)); return;
		case OT_BOOL: out.u8(T_BOOL); out.u8(_integer(o) ? 1 : 0); return;
		case OT_STRING: out.u8(T_STR); out.str(_stringval(o), static_cast<size_t>(_string(o)->_len)); return;
		case OT_WEAKREF: {
			const SQObject& target = _weakref(o)->_obj;
			out.u8(T_WEAK);
			auto it = type(target) == OT_NULL ? id_of.end() : id_of.find(target._unVal.pRefCounted);
			out.u32(it == id_of.end() ? kNoObject : it->second);
			return;
		}
		case OT_CLASS: {
			auto it = reg.class_path.find(_class(o));
			if (it == reg.class_path.end()) { out.u8(T_NULL); return; }
			out.u8(T_CLASS);
			out.str(it->second);
			return;
		}
		case OT_TABLE:
			if (_table(o) == root) { out.u8(T_ROOT); return; }
			[[fallthrough]];
		case OT_ARRAY: case OT_CLOSURE: case OT_NATIVECLOSURE: case OT_THREAD: case OT_GENERATOR: case OT_INSTANCE: {
			void* p = raw_of(o);
			auto it = id_of.find(p);
			if (it != id_of.end()) { out.u8(T_REF); out.u32(it->second); return; }
			out.u8(ignored.count(p) ? T_IGNORED : T_NULL);
			return;
		}
		default: out.u8(T_NULL); return;
		}
	}

	int64_t ip_offset(const SQObjectPtr& closure, const SQInstruction* ip) {
		if (type(closure) != OT_CLOSURE || !ip) return -1;
		SQFunctionProto* f = _funcproto(_closure(closure)->_function);
		return static_cast<int64_t>(ip - &f->_instructions[0]);
	}
	void put_ci(const SQVM::CallInfo& ci) {
		put(ci._closure);
		out.i64(ip_offset(ci._closure, ci._ip));
		out.pod<int32_t>(ci._etraps);
		out.pod<int32_t>(ci._prevstkbase);
		out.pod<int32_t>(ci._prevtop);
		out.pod<int32_t>(ci._target);
		out.pod<int32_t>(ci._ncalls);
		out.u8(ci._root ? 1 : 0);
		out.pod<uint16_t>(ci._vargs.size);
		out.pod<uint16_t>(ci._vargs.base);
		if (ci._generator) put(SQObjectPtr(ci._generator)); else out.u8(T_NULL);
	}

	// --- shells ---------------------------------------------------------------------------------------------------
	void write_shell(uint32_t id) {
		Info& inf = infos[id];
		out.u32(id);
		out.u8(inf.kind);
		switch (inf.kind) {
		case K_TABLE: case K_THREAD: break;
		case K_GENERATOR: put(_generator(inf.obj)->_closure); break;
		case K_ARRAY: out.u32(static_cast<uint32_t>(_array(inf.obj)->_values.size())); break;
		case K_CLOSURE: {
			SQFunctionProto* f = _funcproto(_closure(inf.obj)->_function);
			auto it = proto_id.find(f);
			if (it != proto_id.end()) {
				out.u8(0);
				out.u32(it->second);
			} else {
				const uint32_t pid = static_cast<uint32_t>(proto_id.size());
				proto_id[f] = pid;
				out.u8(1);
				out.u32(pid);
				if (!f->Save(vm, &out, proto_write, SQ_ABI_32)) out.ok = false;
			}
			break;
		}
		case K_NATIVE: {
			SQNativeClosure* c = _nativeclosure(inf.obj);
			out.str(reg.fn_name[c->_function]);
			out.i64(c->_nparamscheck);
			out.u32(static_cast<uint32_t>(c->_typecheck.size()));
			for (size_t i = 0; i < c->_typecheck.size(); i++) out.i64(c->_typecheck[i]);
			if (type(c->_name) == OT_STRING) { out.u8(1); out.str(_stringval(c->_name), static_cast<size_t>(_string(c->_name)->_len)); } else out.u8(0);
			break;
		}
		case K_INSTANCE: {
			SQInstance* in = _instance(inf.obj);
			out.str(reg.class_path[in->_class]);
			out.u8(inf.native_inst ? 1 : 0);
			if (!inf.native_inst) write_callback(recreate_cb, inf.obj);
			break;
		}
		}
	}

	// A script callback that writes into the same stream; its output is length prefixed so the reader can resynchronise.
	void write_callback(const HSQOBJECT& cb, const SQObjectPtr& obj) {
		const long len_pos = file_obj_tell(out.f);
		out.u32(0);
		const long start = file_obj_tell(out.f);
		if (!call_script(hv, cb, &obj, nullptr)) {
			out.ok = false;
			return;
		}
		file_obj_seek_end(out.f);
		const long end = file_obj_tell(out.f);
		file_obj_seek(out.f, len_pos);
		out.u32(static_cast<uint32_t>(end - start));
		file_obj_seek_end(out.f);
	}

	// --- data -----------------------------------------------------------------------------------------------------
	void write_data(uint32_t id) {
		Info& inf = infos[id];
		const SQObjectPtr& o = inf.obj;
		switch (inf.kind) {
		case K_TABLE: {
			SQTable* t = _table(o);
			if (t->_delegate) put(SQObjectPtr(t->_delegate)); else out.u8(T_NULL);
			std::vector<std::pair<SQObjectPtr, SQObjectPtr>> items;
			SQObjectPtr ref, k, val;
			SQInteger n;
			while ((n = t->Next(true, ref, k, val)) != -1) {
				ref = n;
				items.emplace_back(k, val);
			}
			out.u32(static_cast<uint32_t>(items.size()));
			for (auto& kv : items) { put(kv.first); put(kv.second); }
			break;
		}
		case K_ARRAY: {
			SQArray* a = _array(o);
			for (size_t i = 0; i < a->_values.size(); i++) put(a->_values[i]);
			break;
		}
		case K_CLOSURE: {
			SQClosure* c = _closure(o);
			put(c->_env);
			out.u32(static_cast<uint32_t>(c->_outervalues.size()));
			for (size_t i = 0; i < c->_outervalues.size(); i++) put(c->_outervalues[i]);
			out.u32(static_cast<uint32_t>(c->_defaultparams.size()));
			for (size_t i = 0; i < c->_defaultparams.size(); i++) put(c->_defaultparams[i]);
			break;
		}
		case K_NATIVE: {
			SQNativeClosure* c = _nativeclosure(o);
			put(c->_env);
			out.u32(static_cast<uint32_t>(c->_outervalues.size()));
			for (size_t i = 0; i < c->_outervalues.size(); i++) put(c->_outervalues[i]);
			break;
		}
		case K_INSTANCE: {
			SQInstance* in = _instance(o);
			SQClass* cls = in->_class;
			std::vector<std::pair<std::string, size_t>> fields;
			SQObjectPtr ref, k, val;
			SQInteger n;
			while ((n = cls->_members->Next(false, ref, k, val)) != -1) {
				ref = n;
				if (type(k) == OT_STRING && type(val) == OT_INTEGER && _isfield(val)) fields.emplace_back(_stringval(k), static_cast<size_t>(_member_idx(val)));
			}
			out.u32(static_cast<uint32_t>(fields.size()));
			for (auto& f : fields) {
				out.str(f.first);
				put(in->_values[f.second]);
			}
			break;
		}
		case K_GENERATOR: {
			SQGenerator* g = _generator(o);
			out.u8(static_cast<uint8_t>(g->_state));
			out.u32(static_cast<uint32_t>(g->_stack.size()));
			for (size_t i = 0; i < g->_stack.size(); i++) put(g->_stack[i]);
			out.u32(static_cast<uint32_t>(g->_vargsstack.size()));
			for (size_t i = 0; i < g->_vargsstack.size(); i++) put(g->_vargsstack[i]);
			put_ci(g->_ci);
			out.u32(static_cast<uint32_t>(g->_etraps.size()));
			for (size_t i = 0; i < g->_etraps.size(); i++) put_trap(g->_etraps[i], g->_closure);
			break;
		}
		case K_THREAD: {
			SQVM* t = _thread(o);
			out.u8(t->_suspended ? 1 : 0);
			out.u8(t->_suspended_root ? 1 : 0);
			out.i64(t->_suspended_target);
			out.i64(t->_suspended_traps);
			out.pod<uint16_t>(t->_suspend_varargs.size);
			out.pod<uint16_t>(t->_suspend_varargs.base);
			out.i64(t->_top);
			out.i64(t->_stackbase);
			SQInteger n;
			thread_extent(t, n);
			out.u32(static_cast<uint32_t>(n));
			for (SQInteger i = 0; i < n; i++) put(t->_stack._vals[i]);
			out.u32(static_cast<uint32_t>(t->_vargsstack.size()));
			for (size_t i = 0; i < t->_vargsstack.size(); i++) put(t->_vargsstack[i]);
			out.u32(static_cast<uint32_t>(t->_callsstacksize));
			for (SQInteger i = 0; i < t->_callsstacksize; i++) put_ci(t->_callsstack[i]);
			// exception traps belong to the frames in order
			out.u32(static_cast<uint32_t>(t->_etraps.size()));
			size_t trap = 0;
			for (SQInteger f = 0; f < t->_callsstacksize && trap < t->_etraps.size(); f++)
				for (SQInt32 j = 0; j < t->_callsstack[f]._etraps && trap < t->_etraps.size(); j++, trap++) put_trap(t->_etraps[trap], t->_callsstack[f]._closure);
			for (; trap < t->_etraps.size(); trap++) put_trap(t->_etraps[trap], SQObjectPtr());
			put(t->_lasterror);
			break;
		}
		}
	}
	void put_trap(const SQExceptionTrap& tr, const SQObjectPtr& closure) {
		out.i64(tr._stackbase);
		out.i64(tr._stacksize);
		out.i64(ip_offset(closure, tr._ip));
		out.i64(tr._extarget);
	}

	// --- whole snapshot -------------------------------------------------------------------------------------------
	// Root entries that a load must leave alone: classes, functions, user data, the ignore list, and anything that was
	// not collected (it would come back as null and clobber the live value).
	bool is_skipped_root_value(const SQObjectPtr& v) const {
		switch (type(v)) {
		case OT_CLASS: case OT_NATIVECLOSURE: case OT_CLOSURE: case OT_USERDATA: case OT_USERPOINTER: case OT_FUNCPROTO: return true;
		case OT_TABLE: case OT_ARRAY: case OT_INSTANCE: case OT_THREAD: case OT_GENERATOR:
			if (type(v) == OT_TABLE && _table(v) == root) return true;
			return id_of.count(raw_of(v)) == 0;
		default: return false;
		}
	}

	int run(const std::vector<HSQOBJECT>& ignore_list, const HSQOBJECT* order_fn) {
		reg.build(vm);
		root = _table(vm->_roottable);
		for (const HSQOBJECT& o : ignore_list)
			if (ISREFCOUNTED(o._type)) ignored.insert(o._unVal.pRefCounted);
		ignored.insert(static_cast<void*>(vm)); // the saving thread itself

		// 1. collect the graph reachable from the root entries
		std::vector<std::pair<SQObjectPtr, SQObjectPtr>> roots;
		{
			SQObjectPtr ref, k, val;
			SQInteger n;
			while ((n = root->Next(true, ref, k, val)) != -1) {
				ref = n;
				if (type(k) != OT_STRING) continue;
				switch (type(val)) {
				case OT_CLASS: case OT_NATIVECLOSURE: case OT_CLOSURE: case OT_USERDATA: case OT_USERPOINTER: case OT_FUNCPROTO: continue;
				default: break;
				}
				if (ISREFCOUNTED(type(val)) && ignored.count(raw_of(val))) continue;
				roots.emplace_back(k, val);
			}
		}
		for (auto& kv : roots) visit(kv.second);
		for (size_t w = 0; w < work.size(); w++) {
			SQObjectPtr cur = work[w];
			children(infos[id_of[raw_of(cur)]]);
		}
		roots.erase(std::remove_if(roots.begin(), roots.end(), [&](const std::pair<SQObjectPtr, SQObjectPtr>& kv) { return is_skipped_root_value(kv.second); }), roots.end());

		// 2. creation order of the script instances
		for (Info& inf : infos) {
			if (inf.kind != K_INSTANCE || inf.native_inst) continue;
			inf.callback = true;
			inf.order = 100000;
			if (order_fn) {
				SQObjectPtr r;
				if (call_script(hv, *order_fn, &inf.obj, &r) && (type(r) == OT_INTEGER || type(r) == OT_FLOAT)) inf.order = type(r) == OT_INTEGER ? _integer(r) : static_cast<int64_t>(_float(r));
			}
		}
		std::vector<uint32_t> shell_order;
		for (Kind pass : {K_CLOSURE, K_GENERATOR, K_TABLE}) // closures and generators first: generator shells refer to their closure
			for (uint32_t i = 0; i < infos.size(); i++) {
				const bool first = pass == K_TABLE;
				const Kind k = infos[i].kind;
				if (infos[i].callback) continue;
				if (first ? (k != K_CLOSURE && k != K_GENERATOR) : k == pass) shell_order.push_back(i);
			}
		std::vector<uint32_t> inst_order;
		for (uint32_t i = 0; i < infos.size(); i++)
			if (infos[i].callback) inst_order.push_back(i);
		std::stable_sort(inst_order.begin(), inst_order.end(), [&](uint32_t a, uint32_t b) { return infos[a].order < infos[b].order; });
		shell_order.insert(shell_order.end(), inst_order.begin(), inst_order.end());

		// 3. write
		out.u32(kMagic);
		out.u32(kVersion);
		out.u32(static_cast<uint32_t>(infos.size()));
		for (uint32_t id : shell_order) write_shell(id);
		for (uint32_t id = 0; id < infos.size(); id++) write_data(id);
		out.u32(static_cast<uint32_t>(roots.size()));
		for (auto& kv : roots) {
			out.str(_stringval(kv.first), static_cast<size_t>(_string(kv.first)->_len));
			put(kv.second);
		}
		out.u32(static_cast<uint32_t>(inst_order.size()));
		for (uint32_t id : inst_order) {
			out.u32(id);
			write_callback(reset_cb, infos[id].obj);
		}
		for (const std::string& l : lost) std::fprintf(stderr, "[save] not stored: %s\n", l.c_str());
		return out.ok ? 0 : -1;
	}
};

// ---------------------------------------------------------------------------------------------------------------------
struct Reader {
	HSQUIRRELVM hv;
	SQVM* vm;
	Registry reg;
	In in;
	HSQOBJECT recreate_cb, reset_cb, error_cb;
	SQTable* root = nullptr;
	std::vector<SQObjectPtr> objs;
	std::vector<uint8_t> kind;
	std::vector<std::string> native_class; // pending native instances (objs[id] stays null until referenced)
	std::vector<uint8_t> pending;
	std::vector<long> native_pos; // file offset of the field block of a native instance (applied once it exists)
	std::vector<SQObjectPtr> protos;
	int soft_error = 0;
	std::set<std::string> unresolved;

	Reader(HSQUIRRELVM v_, FileObj* f) : hv(v_), vm(v_), in{f} {}

	SQObjectPtr instantiate(const std::string& path, bool call_ctor) {
		auto it = reg.class_by_path.find(path);
		if (it == reg.class_by_path.end()) return SQObjectPtr();
		if (!call_ctor) return SQObjectPtr(it->second->CreateInstance());
		const SQInteger top = sq_gettop(hv);
		sq_pushobject(hv, SQObjectPtr(it->second));
		sq_pushroottable(hv);
		SQObjectPtr res;
		if (SQ_SUCCEEDED(sq_call(hv, 1, SQTrue, SQTrue))) {
			HSQOBJECT o;
			sq_getstackobj(hv, -1, &o);
			res = SQObjectPtr(o);
		}
		sq_settop(hv, top);
		return res;
	}

	// Native instances are created on first reference, or adopted from the owner's field that the constructor filled.
	SQObjectPtr native_ref(uint32_t id, const SQObjectPtr* existing) {
		if (!pending[id]) return objs[id];
		pending[id] = 0;
		const std::string& path = native_class[id];
		auto cit = reg.class_by_path.find(path);
		if (existing && type(*existing) == OT_INSTANCE && cit != reg.class_by_path.end() && _instance(*existing)->_class == cit->second && _instance(*existing)->_hook) objs[id] = *existing;
		else objs[id] = instantiate(path, true);
		return objs[id];
	}

	SQObjectPtr get(const SQObjectPtr* existing = nullptr) {
		const uint8_t tag = in.u8();
		switch (tag) {
		case T_NULL: case T_IGNORED: return SQObjectPtr();
		case T_INT: return SQObjectPtr(static_cast<SQInteger>(in.i64()));
		case T_FLOAT: return SQObjectPtr(static_cast<SQFloat>(in.pod<double>()));
		case T_BOOL: return SQObjectPtr(in.u8() != 0);
		case T_STR: return make_string(vm, in.str());
		case T_ROOT: return SQObjectPtr(root);
		case T_CLASS: {
			auto it = reg.class_by_path.find(in.str());
			return it == reg.class_by_path.end() ? SQObjectPtr() : SQObjectPtr(it->second);
		}
		case T_REF: {
			const uint32_t id = in.u32();
			if (id >= objs.size()) { in.ok = false; return SQObjectPtr(); }
			return native_ref(id, existing);
		}
		case T_WEAK: {
			const uint32_t id = in.u32();
			if (id == kNoObject || id >= objs.size()) return SQObjectPtr();
			SQObjectPtr target = native_ref(id, nullptr);
			if (!ISREFCOUNTED(type(target))) return SQObjectPtr();
			return SQObjectPtr(_refcounted(target)->GetWeakRef(type(target)));
		}
		default: in.ok = false; return SQObjectPtr();
		}
	}

	bool read_callback_payload(uint32_t& len, long& start) {
		len = in.u32();
		start = file_obj_tell(in.f);
		return in.ok;
	}

	SQObjectPtr read_shell(uint32_t& id_out) {
		const uint32_t id = in.u32();
		const uint8_t k = in.u8();
		id_out = id;
		if (!in.ok || id >= objs.size()) { in.ok = false; return SQObjectPtr(); }
		kind[id] = k;
		SQSharedState* ss = _ss(vm);
		switch (k) {
		case K_TABLE: return SQObjectPtr(SQTable::Create(ss, 0));
		case K_ARRAY: return SQObjectPtr(SQArray::Create(ss, in.u32()));
		case K_THREAD: {
			HSQUIRRELVM nt = sq_newthread(hv, 4);
			HSQOBJECT o;
			sq_getstackobj(hv, -1, &o);
			SQObjectPtr res(o);
			sq_pop(hv, 1);
			(void)nt;
			return res;
		}
		case K_GENERATOR: {
			SQObjectPtr clo = get();
			if (type(clo) != OT_CLOSURE) { in.ok = false; return SQObjectPtr(); }
			return SQObjectPtr(SQGenerator::Create(ss, _closure(clo)));
		}
		case K_CLOSURE: {
			const uint8_t fresh = in.u8();
			const uint32_t pid = in.u32();
			if (fresh) {
				SQObjectPtr proto;
				if (!SQFunctionProto::Load(vm, &in, proto_read, proto, SQ_ABI_32)) { in.ok = false; return SQObjectPtr(); }
				if (pid >= protos.size()) protos.resize(pid + 1);
				protos[pid] = proto;
			}
			if (pid >= protos.size() || type(protos[pid]) != OT_FUNCPROTO) { in.ok = false; return SQObjectPtr(); }
			return SQObjectPtr(SQClosure::Create(ss, _funcproto(protos[pid])));
		}
		case K_NATIVE: {
			const std::string name = in.str();
			const SQInteger check = in.i64();
			const uint32_t ntc = in.u32();
			std::vector<SQInteger> tc;
			for (uint32_t i = 0; i < ntc && in.ok; i++) tc.push_back(in.i64());
			std::string nm;
			const bool has_name = in.u8() != 0;
			if (has_name) nm = in.str();
			auto it = reg.fn_by_name.find(name);
			if (it == reg.fn_by_name.end()) { unresolved.insert(name); return SQObjectPtr(); }
			SQNativeClosure* nc = SQNativeClosure::Create(ss, it->second);
			nc->_nparamscheck = check;
			for (SQInteger t : tc) nc->_typecheck.push_back(t);
			if (has_name) nc->_name = make_string(vm, nm);
			return SQObjectPtr(nc);
		}
		case K_INSTANCE: {
			const std::string path = in.str();
			const bool native = in.u8() != 0;
			if (native) {
				native_class[id] = path;
				pending[id] = 1;
				return SQObjectPtr();
			}
			uint32_t len;
			long start;
			if (!read_callback_payload(len, start)) return SQObjectPtr();
			SQObjectPtr inst;
			if (len > 0) {
				SQObjectPtr res;
				if (!call_script(hv, recreate_cb, nullptr, &res)) {
					in.ok = false;
					return SQObjectPtr();
				}
				if (type(res) == OT_INSTANCE) inst = res;
				file_obj_seek(in.f, start + static_cast<long>(len));
			}
			if (type(inst) != OT_INSTANCE) {
				const bool unknown = len == 8 && (file_obj_seek(in.f, start), in.u8() == 'U'); // the game's "Unknown" marker: a plain native-class instance
				file_obj_seek(in.f, start + static_cast<long>(len));
				inst = instantiate(path, false);
				if (!unknown && type(inst) == OT_INSTANCE && error_cb._type != OT_NULL) {
					SQObjectPtr r;
					if (call_script(hv, error_cb, &inst, &r) && type(r) == OT_INTEGER) {
						if (_integer(r) == -2) soft_error = -2;
						else if (soft_error == 0) soft_error = static_cast<int>(_integer(r));
					}
				}
			}
			return inst;
		}
		default: in.ok = false; return SQObjectPtr();
		}
	}

	void get_ci(SQVM::CallInfo& ci, bool want_generator_ref) {
		SQObjectPtr clo = get();
		const int64_t ip = in.i64();
		ci._closure = clo;
		ci._etraps = in.pod<int32_t>();
		ci._prevstkbase = in.pod<int32_t>();
		ci._prevtop = in.pod<int32_t>();
		ci._target = in.pod<int32_t>();
		ci._ncalls = in.pod<int32_t>();
		ci._root = in.u8() ? SQTrue : SQFalse;
		ci._vargs.size = in.pod<uint16_t>();
		ci._vargs.base = in.pod<uint16_t>();
		SQObjectPtr gen = get();
		ci._generator = type(gen) == OT_GENERATOR ? _generator(gen) : nullptr;
		(void)want_generator_ref;
		if (type(clo) == OT_CLOSURE) {
			SQFunctionProto* f = _funcproto(_closure(clo)->_function);
			ci._literals = f->_literals;
			ci._ip = ip >= 0 ? &f->_instructions[0] + ip : &f->_instructions[0];
		} else {
			ci._literals = nullptr;
			ci._ip = nullptr;
		}
	}
	SQExceptionTrap get_trap(const SQObjectPtr& closure) {
		SQExceptionTrap t;
		t._stackbase = in.i64();
		t._stacksize = in.i64();
		const int64_t ip = in.i64();
		t._extarget = in.i64();
		t._ip = (type(closure) == OT_CLOSURE && ip >= 0) ? &_funcproto(_closure(closure)->_function)->_instructions[0] + ip : nullptr;
		return t;
	}

	void read_data(uint32_t id) {
		SQObjectPtr o = objs[id];
		const uint8_t k = kind[id];
		if (type(o) == OT_NULL) {
			// shell failed to build or native instance: nothing to fill, but the data must be consumed
			if (k == K_INSTANCE) {
				native_pos[id] = file_obj_tell(in.f);
				const uint32_t nf = in.u32();
				for (uint32_t i = 0; i < nf && in.ok; i++) { in.str(); skip_value(); }
				return;
			}
			skip_data(k);
			return;
		}
		switch (k) {
		case K_TABLE: {
			SQTable* t = _table(o);
			SQObjectPtr del = get();
			if (type(del) == OT_TABLE) t->SetDelegate(_table(del));
			const uint32_t n = in.u32();
			for (uint32_t i = 0; i < n && in.ok; i++) {
				SQObjectPtr key = get();
				SQObjectPtr val = get();
				if (type(key) != OT_NULL) t->NewSlot(key, val);
			}
			break;
		}
		case K_ARRAY: {
			SQArray* a = _array(o);
			for (size_t i = 0; i < a->_values.size() && in.ok; i++) a->_values[i] = get();
			break;
		}
		case K_CLOSURE: {
			SQClosure* c = _closure(o);
			c->_env = get();
			const uint32_t no = in.u32();
			c->_outervalues.resize(0);
			for (uint32_t i = 0; i < no && in.ok; i++) c->_outervalues.push_back(get());
			const uint32_t nd = in.u32();
			c->_defaultparams.resize(0);
			for (uint32_t i = 0; i < nd && in.ok; i++) c->_defaultparams.push_back(get());
			break;
		}
		case K_NATIVE: {
			SQNativeClosure* c = _nativeclosure(o);
			c->_env = get();
			const uint32_t no = in.u32();
			c->_outervalues.resize(0);
			for (uint32_t i = 0; i < no && in.ok; i++) c->_outervalues.push_back(get());
			break;
		}
		case K_INSTANCE: apply_fields(o); break;
		case K_GENERATOR: {
			// Build the generator now that its closure is known.
			SQGenerator* g = _generator(o);
			SQObjectPtr clo = g->_closure;
			const uint8_t state = in.u8();
			g->_state = static_cast<SQGenerator::SQGeneratorState>(state);
			const uint32_t ns = in.u32();
			g->_stack.resize(ns);
			for (uint32_t i = 0; i < ns && in.ok; i++) g->_stack._vals[i] = get();
			const uint32_t nv = in.u32();
			for (uint32_t i = 0; i < nv && in.ok; i++) g->_vargsstack.push_back(get());
			get_ci(g->_ci, false);
			const uint32_t nt = in.u32();
			for (uint32_t i = 0; i < nt && in.ok; i++) g->_etraps.push_back(get_trap(clo));
			break;
		}
		case K_THREAD: {
			SQVM* t = _thread(o);
			t->_suspended = in.u8() ? SQTrue : SQFalse;
			t->_suspended_root = in.u8() ? SQTrue : SQFalse;
			t->_suspended_target = in.i64();
			t->_suspended_traps = in.i64();
			t->_suspend_varargs.size = in.pod<uint16_t>();
			t->_suspend_varargs.base = in.pod<uint16_t>();
			t->_top = in.i64();
			t->_stackbase = in.i64();
			const uint32_t ns = in.u32();
			t->_stack.resize(std::max<size_t>(ns + 16, 64));
			for (uint32_t i = 0; i < ns && in.ok; i++) t->_stack._vals[i] = get();
			const uint32_t nv = in.u32();
			t->_vargsstack.resize(0);
			for (uint32_t i = 0; i < nv && in.ok; i++) t->_vargsstack.push_back(get());
			const uint32_t nf = in.u32();
			t->_alloccallsstacksize = std::max<uint32_t>(4, nf);
			t->_callstackdata.resize(t->_alloccallsstacksize);
			t->_callsstack = &t->_callstackdata[0];
			for (uint32_t i = 0; i < nf && in.ok; i++) get_ci(t->_callsstack[i], true);
			t->_callsstacksize = nf;
			t->ci = nf ? &t->_callsstack[nf - 1] : nullptr;
			const uint32_t nt = in.u32();
			t->_etraps.resize(0);
			uint32_t trap = 0;
			for (uint32_t f = 0; f < nf && trap < nt; f++)
				for (int j = 0; j < t->_callsstack[f]._etraps && trap < nt; j++, trap++) t->_etraps.push_back(get_trap(t->_callsstack[f]._closure));
			for (; trap < nt && in.ok; trap++) t->_etraps.push_back(get_trap(SQObjectPtr()));
			t->_lasterror = get();
			break;
		}
		}
	}

	void apply_fields(const SQObjectPtr& o) {
		SQInstance* inst = _instance(o);
		const uint32_t nf = in.u32();
		for (uint32_t i = 0; i < nf && in.ok; i++) {
			const std::string name = in.str();
			SQObjectPtr key = make_string(vm, name), slot;
			SQObjectPtr existing;
			const bool has = inst->_class->_members->Get(key, slot) && _isfield(slot);
			if (has) existing = inst->_values[_member_idx(slot)];
			SQObjectPtr val = get(has ? &existing : nullptr);
			if (!has) continue;
			const bool adopted = type(val) == OT_INSTANCE && type(existing) == OT_INSTANCE && _instance(val) == _instance(existing);
			if (!adopted) inst->_values[_member_idx(slot)] = val;
		}
	}

	void skip_value() {
		const uint8_t tag = in.u8();
		switch (tag) {
		case T_INT: in.i64(); break;
		case T_FLOAT: in.pod<double>(); break;
		case T_BOOL: in.u8(); break;
		case T_STR: case T_CLASS: in.str(); break;
		case T_REF: case T_WEAK: in.u32(); break;
		default: break;
		}
	}
	void skip_data(uint8_t k) {
		switch (k) {
		case K_TABLE: { skip_value(); const uint32_t n = in.u32(); for (uint32_t i = 0; i < n && in.ok; i++) { skip_value(); skip_value(); } break; }
		case K_CLOSURE: case K_NATIVE: {
			skip_value();
			uint32_t n = in.u32();
			for (uint32_t i = 0; i < n && in.ok; i++) skip_value();
			if (k == K_CLOSURE) { n = in.u32(); for (uint32_t i = 0; i < n && in.ok; i++) skip_value(); }
			break;
		}
		default: in.ok = false; break; // arrays/threads/generators always have a shell
		}
	}

	int run(const std::vector<std::pair<std::string, SQObjectPtr>>* /*unused*/) {
		reg.build(vm);
		root = _table(vm->_roottable);
		if (in.u32() != kMagic || in.u32() != kVersion || !in.ok) return -1;
		const uint32_t n = in.u32();
		if (!in.ok || n > (1u << 24)) return -1;
		objs.assign(n, SQObjectPtr());
		kind.assign(n, 0xFF);
		native_class.assign(n, std::string());
		pending.assign(n, 0);
		native_pos.assign(n, -1);
		for (uint32_t i = 0; i < n; i++) {
			uint32_t id;
			SQObjectPtr o = read_shell(id);
			if (!in.ok) return -1;
			if (!pending[id]) objs[id] = o;
		}
		// Threads need the generators built first when frames refer to them: build generators before everything that points at them.
		for (uint32_t id = 0; id < n && in.ok; id++) read_data(id);
		if (!in.ok) return -1;
		const uint32_t nroots = in.u32();
		for (uint32_t i = 0; i < nroots && in.ok; i++) {
			const std::string key = in.str();
			SQObjectPtr existing;
			SQObjectPtr ks = make_string(vm, key);
			root->Get(ks, existing);
			SQObjectPtr val = get(&existing);
			if (in.ok) root->NewSlot(ks, val);
		}
		if (!in.ok) return -1;
		{
			const long resume = file_obj_tell(in.f);
			for (uint32_t id = 0; id < n && in.ok; id++) {
				if (native_pos[id] < 0 || pending[id] || type(objs[id]) != OT_INSTANCE) continue;
				file_obj_seek(in.f, native_pos[id]);
				apply_fields(objs[id]);
			}
			file_obj_seek(in.f, resume);
		}
		const uint32_t nreset = in.u32();
		int result = soft_error;
		for (uint32_t i = 0; i < nreset && in.ok; i++) {
			const uint32_t id = in.u32();
			uint32_t len;
			long start;
			if (!read_callback_payload(len, start) || id >= n) return -1;
			if (len > 0 && type(objs[id]) == OT_INSTANCE) {
				if (!call_script(hv, reset_cb, &objs[id], nullptr)) return -2;
				file_obj_seek(in.f, start + static_cast<long>(len));
			} else {
				file_obj_seek(in.f, start + static_cast<long>(len));
			}
		}
		for (const std::string& u : unresolved) std::fprintf(stderr, "[load] unknown native function: %s\n", u.c_str());
		return in.ok ? result : -1;
	}
};

// ---------------------------------------------------------------------------------------------------------------------
SQInteger create_reference_table(HSQUIRRELVM v) { // (order function, array of instances to leave out)
	clear_ref(v);
	g_ref.valid = true;
	sq_getstackobj(v, 2, &g_ref.order_fn);
	sq_addref(v, &g_ref.order_fn);
	if (sq_gettype(v, 3) == OT_ARRAY) {
		SQObjectPtr arr(_array(stack_get(v, 3)));
		for (size_t i = 0; i < _array(arr)->_values.size(); i++) {
			const SQObjectPtr& e = _array(arr)->_values[i];
			if (!ISREFCOUNTED(type(e))) continue;
			HSQOBJECT h = e;
			sq_addref(v, &h);
			g_ref.ignore.push_back(h);
		}
	}
	return 0;
}
SQInteger clear_reference_table(HSQUIRRELVM v) {
	clear_ref(v);
	return 0;
}

SQInteger serialize_root_table(HSQUIRRELVM v) { // (nut name, recreate callback, reset callback) -> 0 / negative
	const SQChar* name = nullptr;
	sq_getstring(v, 2, &name);
	SQObjectPtr nut;
	FileObj* f = name ? find_file(v, name, &nut) : nullptr;
	if (!f) {
		sq_pushinteger(v, -1);
		return 1;
	}
	Writer w(v, f);
	sq_getstackobj(v, 3, &w.recreate_cb);
	sq_getstackobj(v, 4, &w.reset_cb);
	std::vector<HSQOBJECT> ignore = g_ref.ignore;
	HSQOBJECT nuth = nut;
	ignore.push_back(nuth);
	const int r = w.run(ignore, g_ref.valid ? &g_ref.order_fn : nullptr);
	sq_pushinteger(v, r);
	return 1;
}

SQInteger deserialize_root_table(HSQUIRRELVM v) { // (nut name, recreate, reset, error) -> 0 ok, -1 bad data, -2 fatal
	const SQChar* name = nullptr;
	sq_getstring(v, 2, &name);
	FileObj* f = name ? find_file(v, name, nullptr) : nullptr;
	if (!f) {
		sq_pushinteger(v, -1);
		return 1;
	}
	Reader r(v, f);
	sq_getstackobj(v, 3, &r.recreate_cb);
	sq_getstackobj(v, 4, &r.reset_cb);
	r.error_cb = HSQOBJECT();
	sq_resetobject(&r.error_cb);
	if (sq_gettop(v) >= 5) sq_getstackobj(v, 5, &r.error_cb);
	const int res = r.run(nullptr);
	sq_pushinteger(v, res);
	return 1;
}

} // namespace

void register_snapshot_natives(ScriptHost& host) {
	host.add_global("CreateReferenceTable", create_reference_table);
	host.add_global("ClearReferenceTable", clear_reference_table);
	host.add_global("SerializeRootTable", serialize_root_table);
	host.add_global("DeserializeRootTable", deserialize_root_table);
}

} // namespace omw
