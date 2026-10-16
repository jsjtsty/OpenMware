// Small helpers for writing native Squirrel bindings.
#pragma once

#include <squirrel.h>

#include <string>

namespace omw::sqb {

inline float arg_float(HSQUIRRELVM v, SQInteger idx, float def = 0.f) {
	SQFloat f;
	if (SQ_SUCCEEDED(sq_getfloat(v, idx, &f))) return static_cast<float>(f);
	SQInteger n;
	if (SQ_SUCCEEDED(sq_getinteger(v, idx, &n))) return static_cast<float>(n);
	return def;
}

inline SQInteger arg_int(HSQUIRRELVM v, SQInteger idx, SQInteger def = 0) {
	SQInteger n;
	if (SQ_SUCCEEDED(sq_getinteger(v, idx, &n))) return n;
	SQFloat f;
	if (SQ_SUCCEEDED(sq_getfloat(v, idx, &f))) return static_cast<SQInteger>(f);
	SQBool b;
	if (SQ_SUCCEEDED(sq_getbool(v, idx, &b))) return b ? 1 : 0;
	return def;
}

inline std::string arg_string(HSQUIRRELVM v, SQInteger idx) {
	const SQChar* s = nullptr;
	if (SQ_SUCCEEDED(sq_getstring(v, idx, &s)) && s) return s;
	return {};
}

// Native object attached to the instance at stack index 1 (the `this` of a method / constructor).
template <class T>
T* self(HSQUIRRELVM v) {
	SQUserPointer p = nullptr;
	sq_getinstanceup(v, 1, &p, nullptr);
	return static_cast<T*>(p);
}

template <class T>
SQInteger release_hook(SQUserPointer p, SQInteger) {
	delete static_cast<T*>(p);
	return 1;
}

// Creates a T for the instance being constructed and ties its lifetime to the instance.
template <class T, class... Args>
T* attach(HSQUIRRELVM v, Args&&... args) {
	T* obj = new T(std::forward<Args>(args)...);
	sq_setinstanceup(v, 1, obj);
	sq_setreleasehook(v, 1, release_hook<T>);
	return obj;
}

} // namespace omw::sqb
