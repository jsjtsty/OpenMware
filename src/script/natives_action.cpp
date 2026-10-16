// SQAction1D: a timed 1-D value animation (src -> dst over a duration with a selectable tempo curve).
// Layout and update rules follow the original (fields: duration, timer, src, dst, value, power, tempo, active, time scale).
#include "script/bind.h"
#include "script/host.h"

namespace omw {

// Tempo curves; replaced by the exact port of the original table (docs/codex-tasks/06) when available.
float (*g_tempo_eval)(int tempo, float src, float dst, float duration, float timer) =
	[](int, float src, float dst, float duration, float timer) -> float {
	return duration > 0.f ? src + (dst - src) * (timer / duration) : dst;
};

namespace {

struct Action1D {
	float duration = 0, timer = 0, src = 0, dst = 0, now = 0, value = 0, power = 1, time_scale = 1;
	int tempo = 0;
	bool active = false;

	void recompute() {
		if (!active) return;
		value = g_tempo_eval(tempo, src, dst, duration, timer);
		now = power * value;
	}
	void set_timer(float t) {
		timer = t < 0 ? 0 : (t > duration ? duration : t);
		if (duration > 0) active = true;
		recompute();
	}
};

float as_uint_float(SQInteger n) { return static_cast<float>(static_cast<uint32_t>(n)); }

#define ACT sqb::self<Action1D>(v)

SQInteger ctor(HSQUIRRELVM v) {
	sqb::attach<Action1D>(v);
	return 0;
}
SQInteger set_action(HSQUIRRELVM v) { // (src, dst, time, tempo)
	Action1D* a = ACT;
	a->src = sqb::arg_float(v, 2);
	a->dst = sqb::arg_float(v, 3);
	a->duration = as_uint_float(sqb::arg_int(v, 4));
	a->tempo = static_cast<int>(sqb::arg_int(v, 5));
	a->timer = 0;
	a->active = a->duration > 0;
	if (a->active) a->recompute();
	else { a->value = a->dst; a->now = a->power * a->dst; }
	return 0;
}
SQInteger reset_now(HSQUIRRELVM v) { ACT->recompute(); return 0; }
SQInteger reset_timer(HSQUIRRELVM v) { ACT->timer = 0; ACT->recompute(); return 0; }
SQInteger set_timer(HSQUIRRELVM v) { ACT->set_timer(as_uint_float(sqb::arg_int(v, 2))); return 0; }
SQInteger get_timer(HSQUIRRELVM v) { sq_pushinteger(v, static_cast<SQInteger>(ACT->timer)); return 1; }
SQInteger set_action_time(HSQUIRRELVM v) { ACT->duration = as_uint_float(sqb::arg_int(v, 2)); ACT->recompute(); return 0; }
SQInteger get_action_time(HSQUIRRELVM v) { sq_pushinteger(v, static_cast<SQInteger>(ACT->duration)); return 1; }
SQInteger set_src(HSQUIRRELVM v) { ACT->src = sqb::arg_float(v, 2); ACT->recompute(); return 0; }
SQInteger set_dst(HSQUIRRELVM v) { ACT->dst = sqb::arg_float(v, 2); ACT->recompute(); return 0; }
SQInteger set_now(HSQUIRRELVM v) { ACT->value = sqb::arg_float(v, 2); ACT->now = ACT->power * ACT->value; return 0; }
SQInteger get_src(HSQUIRRELVM v) { sq_pushfloat(v, ACT->src); return 1; }
SQInteger get_dst(HSQUIRRELVM v) { sq_pushfloat(v, ACT->dst); return 1; }
SQInteger get_now(HSQUIRRELVM v) { sq_pushfloat(v, ACT->now); return 1; }
SQInteger get_now_no_power(HSQUIRRELVM v) { sq_pushfloat(v, ACT->value); return 1; }
SQInteger get_rate(HSQUIRRELVM v) { sq_pushfloat(v, ACT->duration > 0 ? ACT->timer / ACT->duration : 1.f); return 1; }
SQInteger set_power(HSQUIRRELVM v) {
	Action1D* a = ACT;
	a->power = sqb::arg_float(v, 2);
	if (a->active) a->recompute();
	else { a->value = a->src; a->now = a->src * a->power; }
	return 0;
}
SQInteger get_power(HSQUIRRELVM v) { sq_pushfloat(v, ACT->power); return 1; }
SQInteger set_tempo(HSQUIRRELVM v) { ACT->tempo = static_cast<int>(sqb::arg_int(v, 2)); ACT->recompute(); return 0; }
SQInteger set_time_scale(HSQUIRRELVM v) { ACT->time_scale = sqb::arg_float(v, 2); return 0; }
SQInteger is_active(HSQUIRRELVM v) { sq_pushbool(v, ACT->active && ACT->timer < ACT->duration); return 1; }
SQInteger get_time_scale(HSQUIRRELVM v) { sq_pushfloat(v, ACT->time_scale); return 1; }

} // namespace

void register_action1d(ScriptHost& h) {
	h.begin_class("SQAction1D");
	h.add_method("constructor", ctor);
	h.add_method("setAction", set_action);
	h.add_method("resetNow", reset_now);
	h.add_method("resetTimer", reset_timer);
	h.add_method("setTimer", set_timer);
	h.add_method("getTimer", get_timer);
	h.add_method("setActionTime", set_action_time);
	h.add_method("getActionTime", get_action_time);
	h.add_method("setSrc", set_src);
	h.add_method("setDst", set_dst);
	h.add_method("setNow", set_now);
	h.add_method("getSrc", get_src);
	h.add_method("getDst", get_dst);
	h.add_method("getNow", get_now);
	h.add_method("getNowNoPower", get_now_no_power);
	h.add_method("getRate", get_rate);
	h.add_method("setPower", set_power);
	h.add_method("getPower", get_power);
	h.add_method("setTempo", set_tempo);
	h.add_method("setTimeScale", set_time_scale);
	h.add_method("getTimeScale", get_time_scale);
	h.add_method("isActive", is_active);
	h.end_class();
}

} // namespace omw
