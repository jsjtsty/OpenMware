/*
	see copyright notice in squirrel.h
*/
#include "sqpcheader.h"
#include "sqvm.h"
#include "sqstring.h"
#include "sqarray.h"
#include "sqtable.h"
#include "squserdata.h"
#include "sqfuncproto.h"
#include "sqclass.h"
#include "sqclosure.h"

bool SafeWrite(HSQUIRRELVM v,SQWRITEFUNC write,SQUserPointer up,SQUserPointer dest,SQInteger size);
bool SafeRead(HSQUIRRELVM v,SQWRITEFUNC read,SQUserPointer up,SQUserPointer dest,SQInteger size);

static bool WriteFixedUInt32(HSQUIRRELVM v,SQWRITEFUNC write,SQUserPointer up,uint32_t value)
{
	return SafeWrite(v, write, up, &value, sizeof(value));
}

static bool ReadFixedUInt32(HSQUIRRELVM v,SQREADFUNC read,SQUserPointer up,uint32_t &value)
{
	return SafeRead(v, read, up, &value, sizeof(value));
}

static bool WriteAbiInteger(HSQUIRRELVM v,SQWRITEFUNC write,SQUserPointer up,SQInteger value,SQAbiMode abi)
{
	if(abi == SQ_ABI_32) {
		int32_t wire = (int32_t)value;
		return SafeWrite(v, write, up, &wire, sizeof(wire));
	}
	int64_t wire = (int64_t)value;
	return SafeWrite(v, write, up, &wire, sizeof(wire));
}

static bool ReadAbiInteger(HSQUIRRELVM v,SQREADFUNC read,SQUserPointer up,SQInteger &value,SQAbiMode abi)
{
	if(abi == SQ_ABI_32) {
		int32_t wire = 0;
		if(!SafeRead(v, read, up, &wire, sizeof(wire))) {
			return false;
		}
		value = (SQInteger)wire;
		return true;
	}
	int64_t wire = 0;
	if(!SafeRead(v, read, up, &wire, sizeof(wire))) {
		return false;
	}
	value = (SQInteger)wire;
	return true;
}

static bool WriteAbiUnsigned(HSQUIRRELVM v,SQWRITEFUNC write,SQUserPointer up,SQUnsignedInteger value,SQAbiMode abi)
{
	if(abi == SQ_ABI_32) {
		uint32_t wire = (uint32_t)value;
		return SafeWrite(v, write, up, &wire, sizeof(wire));
	}
	uint64_t wire = (uint64_t)value;
	return SafeWrite(v, write, up, &wire, sizeof(wire));
}

static bool ReadAbiUnsigned(HSQUIRRELVM v,SQREADFUNC read,SQUserPointer up,SQUnsignedInteger &value,SQAbiMode abi)
{
	if(abi == SQ_ABI_32) {
		uint32_t wire = 0;
		if(!SafeRead(v, read, up, &wire, sizeof(wire))) {
			return false;
		}
		value = (SQUnsignedInteger)wire;
		return true;
	}
	uint64_t wire = 0;
	if(!SafeRead(v, read, up, &wire, sizeof(wire))) {
		return false;
	}
	value = (SQUnsignedInteger)wire;
	return true;
}

static bool WriteAbiBool(HSQUIRRELVM v,SQWRITEFUNC write,SQUserPointer up,bool value)
{
	uint8_t wire = value ? 1u : 0u;
	return SafeWrite(v, write, up, &wire, sizeof(wire));
}

static bool ReadAbiBool(HSQUIRRELVM v,SQREADFUNC read,SQUserPointer up,bool &value)
{
	uint8_t wire = 0;
	if(!SafeRead(v, read, up, &wire, sizeof(wire))) {
		return false;
	}
	value = wire != 0;
	return true;
}

const SQChar *IdType2Name(SQObjectType type)
{
	switch(_RAW_TYPE(type))
	{
	case _RT_NULL:return _SC("null");
	case _RT_INTEGER:return _SC("integer");
	case _RT_FLOAT:return _SC("float");
	case _RT_BOOL:return _SC("bool");
	case _RT_STRING:return _SC("string");
	case _RT_TABLE:return _SC("table");
	case _RT_ARRAY:return _SC("array");
	case _RT_GENERATOR:return _SC("generator");
	case _RT_CLOSURE:
	case _RT_NATIVECLOSURE:
		return _SC("function");
	case _RT_USERDATA:
	case _RT_USERPOINTER:
		return _SC("userdata");
	case _RT_THREAD: return _SC("thread");
	case _RT_FUNCPROTO: return _SC("function");
	case _RT_CLASS: return _SC("class");
	case _RT_INSTANCE: return _SC("instance");
	case _RT_WEAKREF: return _SC("weakref");
	default:
		return NULL;
	}
}

const SQChar *GetTypeName(const SQObjectPtr &obj1)
{
	return IdType2Name(type(obj1));	
}

SQString *SQString::Create(SQSharedState *ss,const SQChar *s,SQInteger len)
{
	SQString *str=ADD_STRING(ss,s,len);
	str->_sharedstate=ss;
	return str;
}

void SQString::Release()
{
	REMOVE_STRING(_sharedstate,this);
}

SQInteger SQString::Next(const SQObjectPtr &refpos, SQObjectPtr &outkey, SQObjectPtr &outval)
{
	SQInteger idx = (SQInteger)TranslateIndex(refpos);
	while(idx < _len){
		outkey = SQAbiNormalizeInteger((SQInteger)idx, _sharedstate->GetAbiMode());
		outval = SQInteger(_val[idx]);
		//return idx for the next iteration
		return ++idx;
	}
	//nothing to iterate anymore
	return -1;
}

SQUnsignedInteger TranslateIndex(const SQObjectPtr &idx)
{
	switch(type(idx)){
		case OT_NULL:
			return 0;
		case OT_INTEGER:
			return (SQUnsignedInteger)_integer(idx);
		default: assert(0); break;
	}
	return 0;
}

SQWeakRef *SQRefCounted::GetWeakRef(SQObjectType type)
{
	if(!_weakref) {
		sq_new(_weakref,SQWeakRef);
		_weakref->_obj._type = type;
		_weakref->_obj._unVal.pRefCounted = this;
	}
	return _weakref;
}

SQRefCounted::~SQRefCounted()
{
	if(_weakref) {
		_weakref->_obj._type = OT_NULL;
		_weakref->_obj._unVal.pRefCounted = NULL;
	}
}

void SQWeakRef::Release() { 
	if(ISREFCOUNTED(_obj._type)) { 
		_obj._unVal.pRefCounted->_weakref = NULL;
	} 
	sq_delete(this,SQWeakRef);
}

bool SQDelegable::GetMetaMethod(SQVM *v,SQMetaMethod mm,SQObjectPtr &res) {
	if(_delegate) {
		return _delegate->Get((*_ss(v)->_metamethods)[mm],res);
	}
	return false;
}

bool SQDelegable::SetDelegate(SQTable *mt)
{
	SQTable *temp = mt;
	if(temp == this) return false;
	while (temp) {
		if (temp->_delegate == this) return false; //cycle detected
		temp = temp->_delegate;
	}
	if (mt)	__ObjAddRef(mt);
	__ObjRelease(_delegate);
	_delegate = mt;
	return true;
}

bool SQGenerator::Yield(SQVM *v)
{
	if(_state==eSuspended) { v->Raise_Error(_SC("internal vm error, yielding dead generator"));  return false;}
	if(_state==eDead) { v->Raise_Error(_SC("internal vm error, yielding a dead generator")); return false; }
	SQInteger size = v->_top-v->_stackbase;
	_ci=*v->ci;
	_stack.resize(size);
	for(SQInteger n =0; n<size; n++) {
		_stack._vals[n] = v->_stack[v->_stackbase+n];
		v->_stack[v->_stackbase+n] = _null_;
	}
	SQInteger nvargs = v->ci->_vargs.size;
	SQInteger vargsbase = v->ci->_vargs.base;
	for(SQInteger j = nvargs - 1; j >= 0; j--) {
		_vargsstack.push_back(v->_vargsstack[vargsbase+j]);
	}
	_ci._generator=NULL;
	for(SQInteger i=0;i<_ci._etraps;i++) {
		_etraps.push_back(v->_etraps.top());
		v->_etraps.pop_back();
	}
	_state=eSuspended;
	return true;
}

bool SQGenerator::Resume(SQVM *v,SQInteger target)
{
	SQInteger size=_stack.size();
	if(_state==eDead){ v->Raise_Error(_SC("resuming dead generator")); return false; }
	if(_state==eRunning){ v->Raise_Error(_SC("resuming active generator")); return false; }
	SQInteger prevtop=v->_top-v->_stackbase;
	PUSH_CALLINFO(v,_ci);
	SQInteger oldstackbase=v->_stackbase;
	v->_stackbase = v->_top;
	v->ci->_target = (SQInt32)target;
	v->ci->_generator = this;
	v->ci->_vargs.size = (unsigned short)_vargsstack.size();
	
	for(SQInteger i=0;i<_ci._etraps;i++) {
		v->_etraps.push_back(_etraps.top());
		_etraps.pop_back();
	}
	for(SQInteger n =0; n<size; n++) {
		v->_stack[v->_stackbase+n] = _stack._vals[n];
		_stack._vals[0] = _null_;
	}
	while(_vargsstack.size()) {
		v->_vargsstack.push_back(_vargsstack.back());
		_vargsstack.pop_back();
	}
	v->ci->_vargs.base = (unsigned short)(v->_vargsstack.size() - v->ci->_vargs.size);
	v->_top=v->_stackbase+size;
	v->ci->_prevtop = (SQInt32)prevtop;
	v->ci->_prevstkbase = (SQInt32)(v->_stackbase - oldstackbase);
	_state=eRunning;
	if (type(v->_debughook) != OT_NULL && _rawval(v->_debughook) != _rawval(v->ci->_closure))
		v->CallDebugHook(_SC('c'));

	return true;
}

void SQArray::Extend(const SQArray *a){
	SQInteger xlen;
	if((xlen=a->Size()))
		for(SQInteger i=0;i<xlen;i++)
			Append(a->_values[i]);
}

const SQChar* SQFunctionProto::GetLocal(SQVM *vm,SQUnsignedInteger stackbase,SQUnsignedInteger nseq,SQUnsignedInteger nop)
{
	SQUnsignedInteger nvars=_nlocalvarinfos;
	const SQChar *res=NULL; 
	if(nvars>=nseq){
 		for(SQUnsignedInteger i=0;i<nvars;i++){
			if(_localvarinfos[i]._start_op<=nop && _localvarinfos[i]._end_op>=nop)
			{
				if(nseq==0){
					vm->Push(vm->_stack[stackbase+_localvarinfos[i]._pos]);
					res=_stringval(_localvarinfos[i]._name);
					break;
				}
				nseq--;
			}
		}
	}
	return res;
}

SQInteger SQFunctionProto::GetLine(SQInstruction *curr)
{
	SQInteger op = (SQInteger)(curr-_instructions);
	SQInteger line=_lineinfos[0]._line;
	for(SQInteger i=1;i<_nlineinfos;i++){
		if(_lineinfos[i]._op>=op)
			return line;
		line=_lineinfos[i]._line;
	}
	return line;
}

#define _CHECK_IO(exp)  { if(!exp)return false; }
bool SafeWrite(HSQUIRRELVM v,SQWRITEFUNC write,SQUserPointer up,SQUserPointer dest,SQInteger size)
{
	if(write(up,dest,size) != size) {
		v->Raise_Error(_SC("io error (write function failure)"));
		return false;
	}
	return true;
}

bool SafeRead(HSQUIRRELVM v,SQWRITEFUNC read,SQUserPointer up,SQUserPointer dest,SQInteger size)
{
	if(size && read(up,dest,size) != size) {
		v->Raise_Error(_SC("io error, read function failure, the origin stream could be corrupted/trucated"));
		return false;
	}
	return true;
}

bool WriteTag(HSQUIRRELVM v,SQWRITEFUNC write,SQUserPointer up,uint32_t tag)
{
	return SafeWrite(v,write,up,&tag,sizeof(tag));
}

bool CheckTag(HSQUIRRELVM v,SQWRITEFUNC read,SQUserPointer up,uint32_t tag)
{
	uint32_t t;
	_CHECK_IO(SafeRead(v,read,up,&t,sizeof(t)));
	if(t != tag){
		v->Raise_Error(_SC("invalid or corrupted closure stream"));
		return false;
	}
	return true;
}

bool WriteObject(HSQUIRRELVM v,SQUserPointer up,SQWRITEFUNC write,SQObjectPtr &o,SQAbiMode abi)
{
	_CHECK_IO(SafeWrite(v,write,up,&type(o),sizeof(SQObjectType)));
	switch(type(o)){
	case OT_STRING:
		_CHECK_IO(WriteAbiInteger(v,write,up,_string(o)->_len,abi));
		_CHECK_IO(SafeWrite(v,write,up,_stringval(o),rsl(_string(o)->_len)));
		break;
	case OT_INTEGER:
		_CHECK_IO(WriteAbiInteger(v,write,up,_integer(o),abi));break;
	case OT_FLOAT:
		_CHECK_IO(SafeWrite(v,write,up,&_float(o),sizeof(SQFloat)));break;
	case OT_NULL:
		break;
	default:
		v->Raise_Error(_SC("cannot serialize a %s"),GetTypeName(o));
		return false;
	}
	return true;
}

bool ReadObject(HSQUIRRELVM v,SQUserPointer up,SQREADFUNC read,SQObjectPtr &o,SQAbiMode abi)
{
	SQObjectType t;
	_CHECK_IO(SafeRead(v,read,up,&t,sizeof(SQObjectType)));
	switch(t){
	case OT_STRING:{
		SQInteger len;
		_CHECK_IO(ReadAbiInteger(v,read,up,len,abi));
		_CHECK_IO(SafeRead(v,read,up,_ss(v)->GetScratchPad(rsl(len)),rsl(len)));
		o=SQString::Create(_ss(v),_ss(v)->GetScratchPad(-1),len);
				   }
		break;
	case OT_INTEGER:{
		SQInteger i;
		_CHECK_IO(ReadAbiInteger(v,read,up,i,abi)); o = i; break;
					}
	case OT_FLOAT:{
		SQFloat f;
		_CHECK_IO(SafeRead(v,read,up,&f,sizeof(SQFloat))); o = f; break;
				  }
	case OT_NULL:
		o=_null_;
		break;
	default:
		v->Raise_Error(_SC("cannot serialize a %s"),IdType2Name(t));
		return false;
	}
	return true;
}

bool SQClosure::Save(SQVM *v,SQUserPointer up,SQWRITEFUNC write,SQAbiMode abi)
{
	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_HEAD));
	_CHECK_IO(WriteTag(v,write,up,(uint32_t)sizeof(SQChar)));
	_CHECK_IO(_funcproto(_function)->Save(v,up,write,abi));
	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_TAIL));
	return true;
}

bool SQClosure::Load(SQVM *v,SQUserPointer up,SQREADFUNC read,SQObjectPtr &ret,SQAbiMode abi)
{
	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_HEAD));
	_CHECK_IO(CheckTag(v,read,up,(uint32_t)sizeof(SQChar)));
	SQObjectPtr func;
	_CHECK_IO(SQFunctionProto::Load(v,up,read,func,abi));
	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_TAIL));
	ret = SQClosure::Create(_ss(v),_funcproto(func));
	return true;
}

bool SQFunctionProto::Save(SQVM *v,SQUserPointer up,SQWRITEFUNC write,SQAbiMode abi)
{
	SQInteger i,nliterals = _nliterals,nparameters = _nparameters;
	SQInteger noutervalues = _noutervalues,nlocalvarinfos = _nlocalvarinfos;
	SQInteger nlineinfos=_nlineinfos,ninstructions = _ninstructions,nfunctions=_nfunctions;
	SQInteger ndefaultparams = _ndefaultparams;
	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_PART));
	_CHECK_IO(WriteObject(v,up,write,_sourcename,abi));
	_CHECK_IO(WriteObject(v,up,write,_name,abi));
	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_PART));
	_CHECK_IO(WriteAbiInteger(v,write,up,nliterals,abi));
	_CHECK_IO(WriteAbiInteger(v,write,up,nparameters,abi));
	_CHECK_IO(WriteAbiInteger(v,write,up,noutervalues,abi));
	_CHECK_IO(WriteAbiInteger(v,write,up,nlocalvarinfos,abi));
	_CHECK_IO(WriteAbiInteger(v,write,up,nlineinfos,abi));
	_CHECK_IO(WriteAbiInteger(v,write,up,ndefaultparams,abi));
	_CHECK_IO(WriteAbiInteger(v,write,up,ninstructions,abi));
	_CHECK_IO(WriteAbiInteger(v,write,up,nfunctions,abi));
	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_PART));
	for(i=0;i<nliterals;i++){
		_CHECK_IO(WriteObject(v,up,write,_literals[i],abi));
	}

	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_PART));
	for(i=0;i<nparameters;i++){
		_CHECK_IO(WriteObject(v,up,write,_parameters[i],abi));
	}

	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_PART));
	for(i=0;i<noutervalues;i++){
		_CHECK_IO(WriteFixedUInt32(v,write,up,(uint32_t)_outervalues[i]._type));
		_CHECK_IO(WriteObject(v,up,write,_outervalues[i]._src,abi));
		_CHECK_IO(WriteObject(v,up,write,_outervalues[i]._name,abi));
	}

	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_PART));
	for(i=0;i<nlocalvarinfos;i++){
		SQLocalVarInfo &lvi=_localvarinfos[i];
		_CHECK_IO(WriteObject(v,up,write,lvi._name,abi));
		_CHECK_IO(WriteAbiUnsigned(v,write,up,lvi._pos,abi));
		_CHECK_IO(WriteAbiUnsigned(v,write,up,lvi._start_op,abi));
		_CHECK_IO(WriteAbiUnsigned(v,write,up,lvi._end_op,abi));
	}

	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_PART));
	for(i = 0; i < nlineinfos; i++) {
		_CHECK_IO(WriteAbiInteger(v,write,up,_lineinfos[i]._line,abi));
		_CHECK_IO(WriteAbiInteger(v,write,up,_lineinfos[i]._op,abi));
	}

	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_PART));
	for(i = 0; i < ndefaultparams; i++) {
		_CHECK_IO(WriteAbiInteger(v,write,up,_defaultparams[i],abi));
	}

	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_PART));
	for(i = 0; i < ninstructions; i++) {
		// On-disk layout matches the original 32-bit struct: arg1 first, then op, arg0, arg2, arg3.
		_CHECK_IO(SafeWrite(v,write,up,&_instructions[i]._arg1,sizeof(_instructions[i]._arg1)));
		_CHECK_IO(SafeWrite(v,write,up,&_instructions[i].op,sizeof(_instructions[i].op)));
		_CHECK_IO(SafeWrite(v,write,up,&_instructions[i]._arg0,sizeof(_instructions[i]._arg0)));
		_CHECK_IO(SafeWrite(v,write,up,&_instructions[i]._arg2,sizeof(_instructions[i]._arg2)));
		_CHECK_IO(SafeWrite(v,write,up,&_instructions[i]._arg3,sizeof(_instructions[i]._arg3)));
	}

	_CHECK_IO(WriteTag(v,write,up,SQ_CLOSURESTREAM_PART));
	for(i=0;i<nfunctions;i++){
		_CHECK_IO(_funcproto(_functions[i])->Save(v,up,write,abi));
	}
	_CHECK_IO(WriteAbiInteger(v,write,up,_stacksize,abi));
	_CHECK_IO(WriteAbiBool(v,write,up,_bgenerator));
	_CHECK_IO(WriteAbiBool(v,write,up,_varparams));
	return true;
}

bool SQFunctionProto::Load(SQVM *v,SQUserPointer up,SQREADFUNC read,SQObjectPtr &ret,SQAbiMode abi)
{
	SQInteger i, nliterals,nparameters;
	SQInteger noutervalues ,nlocalvarinfos ;
	SQInteger nlineinfos,ninstructions ,nfunctions,ndefaultparams ;
	SQObjectPtr sourcename, name;
	SQObjectPtr o;
	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_PART));
	_CHECK_IO(ReadObject(v, up, read, sourcename, abi));
	_CHECK_IO(ReadObject(v, up, read, name, abi));
	
	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_PART));
	_CHECK_IO(ReadAbiInteger(v,read,up,nliterals,abi));
	_CHECK_IO(ReadAbiInteger(v,read,up,nparameters,abi));
	_CHECK_IO(ReadAbiInteger(v,read,up,noutervalues,abi));
	_CHECK_IO(ReadAbiInteger(v,read,up,nlocalvarinfos,abi));
	_CHECK_IO(ReadAbiInteger(v,read,up,nlineinfos,abi));
	_CHECK_IO(ReadAbiInteger(v,read,up,ndefaultparams,abi));
	_CHECK_IO(ReadAbiInteger(v,read,up,ninstructions,abi));
	_CHECK_IO(ReadAbiInteger(v,read,up,nfunctions,abi));
	

	SQFunctionProto *f = SQFunctionProto::Create(ninstructions,nliterals,nparameters,
			nfunctions,noutervalues,nlineinfos,nlocalvarinfos,ndefaultparams);
	SQObjectPtr proto = f; //gets a ref in case of failure
	f->_sourcename = sourcename;
	f->_name = name;

	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_PART));

	for(i = 0;i < nliterals; i++){
		_CHECK_IO(ReadObject(v, up, read, o, abi));
		f->_literals[i] = o;
	}
	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_PART));

	for(i = 0; i < nparameters; i++){
		_CHECK_IO(ReadObject(v, up, read, o, abi));
		f->_parameters[i] = o;
	}
	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_PART));

	for(i = 0; i < noutervalues; i++){
		uint32_t type;
		SQObjectPtr name;
		_CHECK_IO(ReadFixedUInt32(v,read,up,type));
		_CHECK_IO(ReadObject(v, up, read, o, abi));
		_CHECK_IO(ReadObject(v, up, read, name, abi));
		f->_outervalues[i] = SQOuterVar(name,o, (SQOuterType)type);
	}
	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_PART));

	for(i = 0; i < nlocalvarinfos; i++){
		SQLocalVarInfo lvi;
		_CHECK_IO(ReadObject(v, up, read, lvi._name, abi));
		_CHECK_IO(ReadAbiUnsigned(v,read,up,lvi._pos,abi));
		_CHECK_IO(ReadAbiUnsigned(v,read,up,lvi._start_op,abi));
		_CHECK_IO(ReadAbiUnsigned(v,read,up,lvi._end_op,abi));
		f->_localvarinfos[i] = lvi;
	}
	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_PART));
	for(i = 0; i < nlineinfos; i++) {
		_CHECK_IO(ReadAbiInteger(v,read,up,f->_lineinfos[i]._line,abi));
		_CHECK_IO(ReadAbiInteger(v,read,up,f->_lineinfos[i]._op,abi));
	}

	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_PART));
	for(i = 0; i < ndefaultparams; i++) {
		_CHECK_IO(ReadAbiInteger(v,read,up,f->_defaultparams[i],abi));
	}

	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_PART));
	for(i = 0; i < ninstructions; i++) {
		// On-disk layout matches the original 32-bit struct: arg1 first, then op, arg0, arg2, arg3.
		_CHECK_IO(SafeRead(v,read,up, &f->_instructions[i]._arg1, sizeof(f->_instructions[i]._arg1)));
		_CHECK_IO(SafeRead(v,read,up, &f->_instructions[i].op, sizeof(f->_instructions[i].op)));
		_CHECK_IO(SafeRead(v,read,up, &f->_instructions[i]._arg0, sizeof(f->_instructions[i]._arg0)));
		_CHECK_IO(SafeRead(v,read,up, &f->_instructions[i]._arg2, sizeof(f->_instructions[i]._arg2)));
		_CHECK_IO(SafeRead(v,read,up, &f->_instructions[i]._arg3, sizeof(f->_instructions[i]._arg3)));
	}

	_CHECK_IO(CheckTag(v,read,up,SQ_CLOSURESTREAM_PART));
	for(i = 0; i < nfunctions; i++){
		_CHECK_IO(_funcproto(o)->Load(v, up, read, o, abi));
		f->_functions[i] = o;
	}
	_CHECK_IO(ReadAbiInteger(v,read,up,f->_stacksize,abi));
	_CHECK_IO(ReadAbiBool(v,read,up,f->_bgenerator));
	_CHECK_IO(ReadAbiBool(v,read,up,f->_varparams));
	
	ret = f;
	return true;
}

#ifndef NO_GARBAGE_COLLECTOR

#define START_MARK() 	if(!(_uiRef&MARK_FLAG)){ \
		_uiRef|=MARK_FLAG;

#define END_MARK() RemoveFromChain(&_sharedstate->_gc_chain, this); \
		AddToChain(chain, this); }

void SQVM::Mark(SQCollectable **chain)
{
	START_MARK()
		SQSharedState::MarkObject(_lasterror,chain);
		SQSharedState::MarkObject(_errorhandler,chain);
		SQSharedState::MarkObject(_debughook,chain);
		SQSharedState::MarkObject(_roottable, chain);
		SQSharedState::MarkObject(temp_reg, chain);
		for(SQUnsignedInteger i = 0; i < _stack.size(); i++) SQSharedState::MarkObject(_stack[i], chain);
		for(SQUnsignedInteger j = 0; j < _vargsstack.size(); j++) SQSharedState::MarkObject(_vargsstack[j], chain);
		for(SQInteger k = 0; k < _callsstacksize; k++) SQSharedState::MarkObject(_callsstack[k]._closure, chain);
	END_MARK()
}

void SQArray::Mark(SQCollectable **chain)
{
	START_MARK()
		SQInteger len = _values.size();
		for(SQInteger i = 0;i < len; i++) SQSharedState::MarkObject(_values[i], chain);
	END_MARK()
}
void SQTable::Mark(SQCollectable **chain)
{
	START_MARK()
		if(_delegate) _delegate->Mark(chain);
		SQInteger len = _numofnodes;
		for(SQInteger i = 0; i < len; i++){
			SQSharedState::MarkObject(_nodes[i].key, chain);
			SQSharedState::MarkObject(_nodes[i].val, chain);
		}
	END_MARK()
}

void SQClass::Mark(SQCollectable **chain)
{
	START_MARK()
		_members->Mark(chain);
		if(_base) _base->Mark(chain);
		SQSharedState::MarkObject(_attributes, chain);
		for(SQUnsignedInteger i =0; i< _defaultvalues.size(); i++) {
			SQSharedState::MarkObject(_defaultvalues[i].val, chain);
			SQSharedState::MarkObject(_defaultvalues[i].attrs, chain);
		}
		for(SQUnsignedInteger j =0; j< _methods.size(); j++) {
			SQSharedState::MarkObject(_methods[j].val, chain);
			SQSharedState::MarkObject(_methods[j].attrs, chain);
		}
		for(SQUnsignedInteger k =0; k< _metamethods.size(); k++) {
			SQSharedState::MarkObject(_metamethods[k], chain);
		}
	END_MARK()
}

void SQInstance::Mark(SQCollectable **chain)
{
	START_MARK()
		_class->Mark(chain);
		SQUnsignedInteger nvalues = _class->_defaultvalues.size();
		for(SQUnsignedInteger i =0; i< nvalues; i++) {
			SQSharedState::MarkObject(_values[i], chain);
		}
	END_MARK()
}

void SQGenerator::Mark(SQCollectable **chain)
{
	START_MARK()
		for(SQUnsignedInteger i = 0; i < _stack.size(); i++) SQSharedState::MarkObject(_stack[i], chain);
		for(SQUnsignedInteger j = 0; j < _vargsstack.size(); j++) SQSharedState::MarkObject(_vargsstack[j], chain);
		SQSharedState::MarkObject(_closure, chain);
	END_MARK()
}

void SQClosure::Mark(SQCollectable **chain)
{
	START_MARK()
		for(SQUnsignedInteger i = 0; i < _outervalues.size(); i++) SQSharedState::MarkObject(_outervalues[i], chain);
		for(SQUnsignedInteger i = 0; i < _defaultparams.size(); i++) SQSharedState::MarkObject(_defaultparams[i], chain);
	END_MARK()
}

void SQNativeClosure::Mark(SQCollectable **chain)
{
	START_MARK()
		for(SQUnsignedInteger i = 0; i < _outervalues.size(); i++) SQSharedState::MarkObject(_outervalues[i], chain);
	END_MARK()
}

void SQUserData::Mark(SQCollectable **chain){
	START_MARK()
		if(_delegate) _delegate->Mark(chain);
	END_MARK()
}

void SQCollectable::UnMark() { _uiRef&=~MARK_FLAG; }

#endif

