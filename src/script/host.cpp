#include "script/host.h"
#include "script/vm_snapshot.h"

#include <sqstdmath.h>
#include <sqstdstring.h>
#include <sqstdsystem.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>


namespace omw {
void register_real_classes(ScriptHost& host); // natives_*.cpp
void register_global_natives(ScriptHost& host);
namespace {

// Script threads (coroutines) do not inherit the foreign pointer, so use the single host instance.
ScriptHost* g_host = nullptr;
ScriptHost* host_of(HSQUIRRELVM) { return g_host; }

struct Reader {
	const uint8_t* p;
	size_t n, o;
};

SQInteger read_cb(SQUserPointer up, SQUserPointer buf, SQInteger size) {
	auto* r = static_cast<Reader*>(up);
	if (r->o + static_cast<size_t>(size) > r->n) return -1;
	std::memcpy(buf, r->p + r->o, static_cast<size_t>(size));
	r->o += static_cast<size_t>(size);
	return size;
}

void print_cb(HSQUIRRELVM, const SQChar* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	std::vfprintf(stdout, fmt, ap);
	va_end(ap);
}

// Registers the constants stored behind the closure stream (see docs/render-notes.md).
void register_tail_constants(HSQUIRRELVM v, const uint8_t* d, size_t n, size_t pos) {
	if (pos + 4 > n) return;
	pos += 4; // header after the closure stream's own tail tag
	// Scripts read these as free identifiers; inside class methods the VM only does a raw root-table lookup, so
	// they must live in the root table itself.
	sq_pushroottable(v);
	int registered = 0;
	while (pos + 2 <= n) {
		uint16_t len;
		std::memcpy(&len, d + pos, 2);
		pos += 2;
		if (len == 0 || pos + len + 4 > n) break;
		const std::string name(reinterpret_cast<const char*>(d + pos), len - 1);
		pos += len;
		uint32_t tag;
		std::memcpy(&tag, d + pos, 4);
		pos += 4;
		sq_pushstring(v, name.c_str(), -1);
		if (tag == 0x05000002 && pos + 4 <= n) {
			int32_t iv;
			std::memcpy(&iv, d + pos, 4);
			pos += 4;
			sq_pushinteger(v, iv);
		} else if (tag == 0x08000010 && pos + 4 <= n) {
			uint32_t sl;
			std::memcpy(&sl, d + pos, 4);
			pos += 4;
			if (pos + sl > n) { sq_pop(v, 1); break; }
			sq_pushstring(v, reinterpret_cast<const char*>(d + pos), sl ? static_cast<SQInteger>(sl) - 1 : 0);
			pos += sl;
		} else {
			sq_pop(v, 1);
			break;
		}
		sq_newslot(v, -3, SQFalse);
		registered++;
	}
	if (registered && std::getenv("OMW_DEBUG_CONSTS")) std::fprintf(stderr, "[consts] registered %d\n", registered);
	sq_pop(v, 1);
}

SQInteger native_import(HSQUIRRELVM v) {
	const SQChar* path = nullptr;
	sq_getstring(v, 2, &path);
	std::string err;
	if (!host_of(v)->import(path ? path : "", &err)) {
		if (err.rfind("script not found", 0) == 0) { // the original returns -1 for a missing file
			sq_pushinteger(v, -1);
			return 1;
		}
		return sq_throwerror(v, err.c_str());
	}
	sq_pushinteger(v, 0);
	return 1;
}

std::string describe_arg(HSQUIRRELVM v, SQInteger idx) {
	char buf[64];
	switch (sq_gettype(v, idx)) {
	case OT_NULL: return "null";
	case OT_INTEGER: { SQInteger n = 0; sq_getinteger(v, idx, &n); std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(n)); return buf; }
	case OT_FLOAT: { SQFloat f = 0; sq_getfloat(v, idx, &f); std::snprintf(buf, sizeof buf, "%g", static_cast<double>(f)); return buf; }
	case OT_BOOL: { SQBool b = 0; sq_getbool(v, idx, &b); return b ? "true" : "false"; }
	case OT_STRING: { const SQChar* s = ""; sq_getstring(v, idx, &s); return std::string("\"") + s + "\""; }
	case OT_TABLE: return "{table}";
	case OT_ARRAY: return "[array]";
	case OT_INSTANCE: return "<instance>";
	case OT_CLOSURE: case OT_NATIVECLOSURE: return "<function>";
	default: return "<?>";
	}
}

// Default behaviour of every native we have not implemented yet: log and return null.
SQInteger native_stub(HSQUIRRELVM v) {
	const SQChar* name = "?";
	sq_getstring(v, -1, &name); // free variable 0 (pushed last by the VM)
	ScriptHost* h = host_of(v);
	const int count = ++h->stub_calls[name];
	if (h->verbose) {
		std::string line = "[f" + std::to_string(h->frame) + "] [stub] " + name + "(";
		const SQInteger argc = sq_gettop(v) - 2;
		for (SQInteger i = 0; i < argc; i++) line += (i ? ", " : "") + describe_arg(v, 2 + i);
		line += ")";
		h->log.push_back(line);
		std::puts(line.c_str());
	} else if (count == 1) {
		h->log.push_back(std::string("[stub] ") + name);
	}
	// Native objects keep no state of their own in a save file (the scripts rebuild them), but the script checks that
	// serialize()/deserialize() report success.
	const std::string n = name;
	if (n.size() > 10 && (n.compare(n.size() - 10, 10, ".serialize") == 0 || (n.size() > 12 && n.compare(n.size() - 12, 12, ".deserialize") == 0))) {
		sq_pushbool(v, SQTrue);
		return 1;
	}
	// Saving reads every animation timer back; an unimplemented effect has none running.
	const size_t dot = n.find_last_of('.');
	const std::string method = dot == std::string::npos ? n : n.substr(dot + 1);
	auto ends = [&](const char* suffix) { const size_t l = std::strlen(suffix); return method.size() > l && method.compare(method.size() - l, l, suffix) == 0; };
	if (method.rfind("get", 0) == 0 && (ends("Timer") || ends("Phase"))) {
		sq_pushfloat(v, 0.f);
		return 1;
	}
	return 0;
}

SQInteger native_ctor_stub(HSQUIRRELVM v) { return native_stub(v); }

// _get metamethod of the root table's delegate: names missing from the root table fall back to the constant table
// (the tail records of the .nut files are registered there), then, in discovery mode, to logging stubs.
SQInteger native_root_get(HSQUIRRELVM v) {
	const SQChar* key = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &key))) return sq_throwerror(v, "index not found");
	ScriptHost* h = host_of(v);
	if (std::getenv("OMW_DEBUG_CONSTS")) std::fprintf(stderr, "[root_get] %s\n", key);
	sq_pushconsttable(v);
	sq_pushstring(v, key, -1);
	if (SQ_SUCCEEDED(sq_rawget(v, -2))) return 1;
	sq_pop(v, 1);
	if (!h->discovery) return sq_throwerror(v, (std::string("the index '") + key + "' does not exist").c_str());
	h->missing[key]++;
	sq_pushstring(v, key, -1);
	sq_newclosure(v, native_stub, 1);
	return 1;
}

// `_get` metamethod of the stub classes: any member we have no binding for resolves to a logging stub (and is recorded).
SQInteger native_class_get(HSQUIRRELVM v) {
	const SQChar *cname = "?", *key = nullptr;
	sq_getstring(v, -1, &cname); // free variable: class name
	if (SQ_FAILED(sq_getstring(v, 2, &key))) return sq_throwerror(v, "index not found");
	const std::string full = std::string(cname) + "." + key;
	host_of(v)->missing[full]++;
	sq_pushstring(v, full.c_str(), -1);
	sq_newclosure(v, native_stub, 1);
	return 1;
}

void compiler_error_cb(HSQUIRRELVM, const SQChar* desc, const SQChar* source, SQInteger line, SQInteger column) {
	std::fprintf(stderr, "compile error %s:%d:%d %s\n", source, static_cast<int>(line), static_cast<int>(column), desc);
}

SQInteger runtime_error_cb(HSQUIRRELVM v) {
	const SQChar* msg = "unknown error";
	if (sq_gettop(v) >= 1 && SQ_SUCCEEDED(sq_getstring(v, 2, &msg))) {
	}
	std::fprintf(stderr, "script error: %s\n", msg);
	SQStackInfos si;
	for (SQInteger lvl = 1; SQ_SUCCEEDED(sq_stackinfos(v, lvl, &si)); lvl++)
		std::fprintf(stderr, "  at %s (%s:%d)\n", si.funcname ? si.funcname : "?", si.source ? si.source : "?", static_cast<int>(si.line));
	if (const char* code = std::getenv("OMW_EVAL")) { // debugging aid: run a snippet in the root table when a script fails
		if (SQ_SUCCEEDED(sq_compilebuffer(v, code, static_cast<SQInteger>(std::strlen(code)), "OMW_EVAL", SQTrue))) {
			sq_pushroottable(v);
			sq_call(v, 1, SQFalse, SQFalse);
			sq_pop(v, 1);
		}
	}
	if (std::getenv("OMW_LOCALS")) { // locals of the failing function: what the failing expression was working with
		SQUnsignedInteger idx = 0;
		while (const SQChar* name = sq_getlocal(v, 1, idx++)) {
			std::fprintf(stderr, "    local %s = %s\n", name, describe_arg(v, -1).c_str());
			sq_pop(v, 1);
		}
	}
	return 0;
}

} // namespace

ScriptHost* ScriptHost::current() { return g_host; }

ScriptHost::ScriptHost(NpkArchive& scripts) : scripts_(scripts) {
	vm_ = sq_open_ex(1024, SQ_ABI_32); // the game's bytecode uses the 32-bit ABI
	g_host = this;
	sq_setforeignptr(vm_, this);
	sq_newtable(vm_);
	sq_setconsttable(vm_);
	sq_setprintfunc(vm_, print_cb);
	sq_setcompilererrorhandler(vm_, compiler_error_cb);
	sq_newclosure(vm_, runtime_error_cb, 0);
	sq_seterrorhandler(vm_);
	register_natives();
	register_global_natives(*this);
	register_real_classes(*this);
	register_snapshot_natives(*this);
	for (const char* name : {"PreCompile", "Suspend", "TokenizeText", "ConvertToUTF16", "CompareSqObject",
							 "ResetNGCode", "AddNGCode_HeadOfLine", "AddNGCode_EndOfLine"}) {
		sq_pushroottable(vm_);
		sq_pushstring(vm_, name, -1);
		sq_pushstring(vm_, name, -1);
		sq_newclosure(vm_, native_stub, 1);
		sq_newslot(vm_, -3, SQFalse);
		sq_pop(vm_, 1);
	}
	register_stubs();
	sq_pushroottable(vm_);
	sq_newtable(vm_);
	sq_pushstring(vm_, "_get", -1);
	sq_newclosure(vm_, native_root_get, 0);
	sq_newslot(vm_, -3, SQFalse);
	sq_setdelegate(vm_, -2);
	sq_pop(vm_, 1);
}

ScriptHost::~ScriptHost() {
	if (g_host == this) g_host = nullptr;
	if (vm_) sq_close(vm_);
}

bool ScriptHost::load(const std::string& path, std::string* error) {
	const NpkEntry* e = scripts_.find_ci(path);
	if (!e) e = scripts_.find_ci("media/script/" + path); // short names such as lang/ch/x.nut are relative to the script root
	if (!e) {
		if (error) *error = "script not found: " + path;
		return false;
	}
	std::vector<uint8_t> data;
	std::string err;
	if (!scripts_.read(*e, data, &err) || data.size() < 16 || std::memcmp(data.data(), "SCRP", 4) != 0) {
		if (error) *error = "cannot read script " + path + (err.empty() ? "" : ": " + err);
		return false;
	}
	Reader r{data.data(), data.size(), 16};
	if (SQ_FAILED(sq_readclosure_ex(vm_, read_cb, &r, SQ_ABI_32))) {
		if (error) *error = "bad bytecode: " + path;
		return false;
	}
	register_tail_constants(vm_, data.data(), data.size(), r.o);
	return true;
}

bool ScriptHost::import(const std::string& path, std::string* error) {
	std::string key = path;
	for (char& c : key)
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	loaded_.insert(key); // the original always re-runs the file (scripts rely on this to redefine ::main)
	if (verbose) std::fprintf(stderr, "[import] %s\n", path.c_str());
	if (!load(path, error)) return false;
	sq_pushroottable(vm_);
	if (SQ_FAILED(sq_call(vm_, 1, SQFalse, SQTrue))) {
		sq_pop(vm_, 1);
		if (error) *error = "error while running " + path;
		return false;
	}
	sq_pop(vm_, 1);
	return true;
}

bool ScriptHost::resume_frame(std::string* error) {
	if (sq_getvmstate(vm_) != SQ_VMSTATE_SUSPENDED) {
		if (error) *error = "main script is not suspended";
		return false;
	}
	frame++;
	if (SQ_FAILED(sq_wakeupvm(vm_, SQFalse, SQFalse, SQTrue, SQFalse))) {
		if (error) *error = "error while running a frame";
		return false;
	}
	return true;
}

void ScriptHost::enable_discovery() { discovery = true; }

void ScriptHost::debug_eval(const char* code) {
	if (SQ_SUCCEEDED(sq_compilebuffer(vm_, code, static_cast<SQInteger>(std::strlen(code)), "OMW_EVAL", SQTrue))) {
		sq_pushroottable(vm_);
		sq_call(vm_, 1, SQFalse, SQFalse);
		sq_pop(vm_, 1);
	}
}

void ScriptHost::begin_class(const char* script_name) {
	building_ = script_name;
	real_classes_.insert(building_);
	sq_pushroottable(vm_);
	sq_pushstring(vm_, script_name, -1);
	sq_newclass(vm_, SQFalse);
}

void ScriptHost::add_method(const char* name, SQFUNCTION fn) {
	sq_pushstring(vm_, name, -1);
	sq_newclosure(vm_, fn, 0);
	sq_newslot(vm_, -3, SQFalse);
}

void ScriptHost::add_global(const char* name, SQFUNCTION fn) {
	sq_pushroottable(vm_);
	sq_pushstring(vm_, name, -1);
	sq_newclosure(vm_, fn, 0);
	sq_newslot(vm_, -3, SQFalse);
	sq_pop(vm_, 1);
}

void ScriptHost::add_method_s(const char* name, SQFUNCTION fn, const char* free_string) {
	sq_pushstring(vm_, name, -1);
	sq_pushstring(vm_, free_string, -1);
	sq_newclosure(vm_, fn, 1);
	sq_newslot(vm_, -3, SQFalse);
}

void ScriptHost::end_class() {
	sq_pushstring(vm_, "_get", -1);
	sq_pushstring(vm_, building_.c_str(), -1);
	sq_newclosure(vm_, native_class_get, 1);
	sq_newslot(vm_, -3, SQFalse);
	sq_newslot(vm_, -3, SQFalse); // root[name] = class
	sq_pop(vm_, 1);
	building_.clear();
}

void ScriptHost::register_natives() {
	auto reg = [&](const char* name, SQFUNCTION fn) {
		sq_pushroottable(vm_);
		sq_pushstring(vm_, name, -1);
		sq_newclosure(vm_, fn, 0);
		sq_newslot(vm_, -3, SQFalse);
		sq_pop(vm_, 1);
	};
	sq_pushroottable(vm_);
	sqstd_register_mathlib(vm_);
	sqstd_register_stringlib(vm_);
	sqstd_register_systemlib(vm_);
	sq_pop(vm_, 1);
	reg("Import", native_import);
	reg("IncludeFile", native_import);
}

void ScriptHost::register_stubs() {
	auto push_stub = [&](const std::string& full_name) {
		sq_pushstring(vm_, full_name.c_str(), -1);
		sq_newclosure(vm_, native_stub, 1);
	};
	auto class_stub = [&](const char* script_name, std::initializer_list<const char*> methods) {
		if (real_classes_.count(script_name)) return;
		sq_pushroottable(vm_);
		sq_pushstring(vm_, script_name, -1);
		sq_newclass(vm_, SQFalse);
		sq_pushstring(vm_, "constructor", -1);
		push_stub(std::string(script_name) + ".constructor");
		sq_newslot(vm_, -3, SQFalse);
		sq_pushstring(vm_, "_get", -1);
		sq_pushstring(vm_, script_name, -1);
		sq_newclosure(vm_, native_class_get, 1);
		sq_newslot(vm_, -3, SQFalse);
		for (const char* m : methods) {
			sq_pushstring(vm_, m, -1);
			push_stub(std::string(script_name) + "." + m);
			sq_newslot(vm_, -3, SQFalse);
		}
		sq_newslot(vm_, -3, SQFalse);
		sq_pop(vm_, 1);
	};
	auto global_stub = [&](const char* name) {
		sq_pushroottable(vm_);
		sq_pushstring(vm_, name, -1);
		push_stub(name);
		sq_newslot(vm_, -3, SQFalse);
		sq_pop(vm_, 1);
	};
#define SQ_CLASS(script_name, native_name, ...) class_stub(script_name, std::initializer_list<const char*> __VA_ARGS__);
#define SQ_GLOBAL(name) global_stub(name);
#include "script/generated/sq_binding_names.inc"
#undef SQ_CLASS
#undef SQ_GLOBAL
}

} // namespace omw
