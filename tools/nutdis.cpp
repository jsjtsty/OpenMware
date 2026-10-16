// nutdis: disassembles a compiled game script (.nut) - every function with literals and line numbers. Dev tool.
//   nutdis FILE.nut
#include "sqpcheader.h"
#include "sqvm.h"
#include "sqstring.h"
#include "sqtable.h"
#include "sqarray.h"
#include "sqfuncproto.h"
#include "sqclosure.h"
#include "squserdata.h"
#include "sqclass.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char* kOps[] = {"LINE", "LOAD", "LOADINT", "LOADFLOAT", "DLOAD", "TAILCALL", "CALL", "PREPCALL", "PREPCALLK", "GETK", "MOVE", "NEWSLOT",
	"DELETE", "SET", "GET", "EQ", "NE", "ARITH", "BITW", "RETURN", "LOADNULLS", "LOADROOTTABLE", "LOADBOOL", "DMOVE", "JMP", "JNZ", "JZ",
	"LOADFREEVAR", "VARGC", "GETVARGV", "NEWTABLE", "NEWARRAY", "APPENDARRAY", "GETPARENT", "COMPARITH", "COMPARITHL", "INC", "INCL", "PINC",
	"PINCL", "CMP", "EXISTS", "INSTANCEOF", "AND", "OR", "NEG", "NOT", "BWNOT", "CLOSURE", "YIELD", "RESUME", "FOREACH", "POSTFOREACH",
	"DELEGATE", "CLONE", "TYPEOF", "PUSHTRAP", "POPTRAP", "THROW", "CLASS", "NEWSLOTA"};

struct R { const unsigned char* p; size_t n, o; };
static SQInteger rd(SQUserPointer up, SQUserPointer buf, SQInteger sz) {
	R* r = (R*)up;
	if (r->o + sz > r->n) return -1;
	memcpy(buf, r->p + r->o, sz);
	r->o += sz;
	return sz;
}

static std::string lit(const SQObjectPtr& o) {
	char b[64];
	switch (type(o)) {
	case OT_STRING: return std::string("\"") + _stringval(o) + "\"";
	case OT_INTEGER: snprintf(b, sizeof b, "%lld", (long long)_integer(o)); return b;
	case OT_FLOAT: snprintf(b, sizeof b, "%g", (double)_float(o)); return b;
	case OT_BOOL: return _integer(o) ? "true" : "false";
	case OT_NULL: return "null";
	default: return "?";
	}
}

static void dump(SQFunctionProto* p, int depth) {
	const std::string ind(depth * 2, ' ');
	printf("%sfunction %s  params=%d", ind.c_str(), type(p->_name) == OT_STRING ? _stringval(p->_name) : "(anon)", (int)p->_nparameters);
	for (int i = 0; i < p->_nparameters; i++) printf(" %s", lit(p->_parameters[i]).c_str());
	printf("  stack=%d%s%s\n", (int)p->_stacksize, p->_varparams ? " varargs" : "", p->_bgenerator ? " generator" : "");
	int li = 0, line = 0;
	for (int i = 0; i < p->_ninstructions; i++) {
		while (li < p->_nlineinfos && p->_lineinfos[li]._op <= i) line = (int)p->_lineinfos[li++]._line;
		const SQInstruction& x = p->_instructions[i];
		const char* name = x.op < sizeof kOps / sizeof *kOps ? kOps[x.op] : "?";
		printf("%s  %4d L%-4d %-12s a0=%-3d a1=%-6d a2=%-3d a3=%-3d", ind.c_str(), i, line, name, x._arg0, (int)x._arg1, x._arg2, x._arg3);
		switch (x.op) {
		case _OP_LOAD: case _OP_GETK: case _OP_PREPCALLK:
			if (x._arg1 >= 0 && x._arg1 < p->_nliterals) printf("  ; %s", lit(p->_literals[x._arg1]).c_str());
			break;
		case _OP_DLOAD:
			if (x._arg1 >= 0 && x._arg1 < p->_nliterals && x._arg3 < p->_nliterals)
				printf("  ; %s, %s", lit(p->_literals[x._arg1]).c_str(), lit(p->_literals[x._arg3]).c_str());
			break;
		case _OP_LOADFLOAT: { float f; memcpy(&f, &x._arg1, 4); printf("  ; %g", f); break; }
		default: break;
		}
		printf("\n");
	}
	for (int i = 0; i < p->_nfunctions; i++) dump(_funcproto(p->_functions[i]), depth + 1);
}

int main(int argc, char** argv) {
	if (argc < 2) return 2;
	FILE* f = fopen(argv[1], "rb");
	if (!f) return 1;
	std::vector<unsigned char> d(8 << 20);
	const size_t n = fread(d.data(), 1, d.size(), f);
	fclose(f);
	HSQUIRRELVM vm = sq_open_ex(1024, SQ_ABI_32);
	R r{d.data(), n, 16};
	if (SQ_FAILED(sq_readclosure_ex(vm, rd, &r, SQ_ABI_32))) { fputs("load failed\n", stderr); return 1; }
	dump(_funcproto(_closure(vm->GetUp(-1))->_function), 0);
	return 0;
}
