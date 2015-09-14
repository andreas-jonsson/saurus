/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "saurus.h"

#include <math.h>

const double pi = 3.141592653589793;

#define FUNC1D_DEF(f) \
	static int _##f(su_state *s, int narg) { \
		su_check_arguments(s, 1, SU_NUMBER); \
		su_pushnumber(s, f(su_tonumber(s, -1))); \
		return 1; }

#define FUNC2D_DEF(f) \
	static int _##f(su_state *s, int narg) { \
		su_check_arguments(s, 2, SU_NUMBER, SU_NUMBER); \
		su_pushnumber(s, f(su_tonumber(s, -2), su_tonumber(s, -1))); \
		return 1; }
		
#define FUNC1I_DEF(f) \
	static int _##f(su_state *s, int narg) { \
		su_check_arguments(s, 1, SU_NUMBER); \
		su_pushinteger(s, f(su_tointeger(s, -1))); \
		return 1; }

#define FUNC_DEC(f) \
	su_pushstring(s, #f); \
	su_pushfunction(s, (su_nativefunc)&_##f);

FUNC1I_DEF(abs)
FUNC1D_DEF(acos)
FUNC1D_DEF(asin)
FUNC1D_DEF(cos)
FUNC1D_DEF(cosh)
FUNC1D_DEF(ceil)
FUNC1D_DEF(floor)
FUNC2D_DEF(fmod)
FUNC1D_DEF(log)
FUNC1D_DEF(log10)
FUNC1D_DEF(sin)
FUNC1D_DEF(sinh)
FUNC1D_DEF(sqrt)
FUNC1D_DEF(tan)
FUNC1D_DEF(tanh)
FUNC1D_DEF(exp)

static int atan_(su_state *s, int narg) {
	if (narg == 1) {
		su_check_arguments(s, 1, SU_NUMBER);
		su_pushnumber(s, atan(su_tonumber(s, -1)));
	} else {
		su_check_arguments(s, 2, SU_NUMBER, SU_NUMBER);
		su_pushnumber(s, atan2(su_tonumber(s, -2), su_tonumber(s, -1)));
	}
	return 1;
}

static int modf_(su_state *s, int narg) {
	double f, r;
	su_check_arguments(s, 1, SU_NUMBER);
	f = modf(su_tonumber(s, -1), &r);
	su_pushnumber(s, f);
	su_pushnumber(s, r);
	su_cons(s);
	return 1;
}

static int frexp_(su_state *s, int narg) {
	double f;
	int r;
	su_check_arguments(s, 1, SU_NUMBER);
	f = frexp(su_tonumber(s, -1), &r);
	su_pushnumber(s, f);
	su_pushinteger(s, r);
	su_cons(s);
	return 1;
}

static int ldexp_(su_state *s, int narg) {
	su_check_arguments(s, 2, SU_NUMBER, SU_NUMBER);
	su_pushnumber(s, ldexp(su_tonumber(s, -2), su_tointeger(s, -1)));
	return 1;
}

static int min_(su_state *s, int narg) {
	double n, tmp;
	if (narg > 0) {
		n = su_tonumber(s, -narg);
		while (--narg) {
			tmp = su_tonumber(s, -narg);
			n = (tmp < n) ? tmp : n;
		}
		su_pushnumber(s, n);
		return 1;
	}
	return 0;
}

static int max_(su_state *s, int narg) {
	double n, tmp;
	if (narg > 0) {
		n = su_tonumber(s, -narg);
		while (--narg) {
			tmp = su_tonumber(s, -narg);
			n = (tmp > n) ? tmp : n;
		}
		su_pushnumber(s, n);
		return 1;
	}
	return 0;
}

static int deg_(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_NUMBER);
	su_pushnumber(s, su_tointeger(s, -1) * (180.0 / pi));
	return 1;
}

static int rad_(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_NUMBER);
	su_pushnumber(s, su_tointeger(s, -1) * (pi / 180.0));
	return 1;
}

static int random_(su_state *s, int narg) {
	su_check_arguments(s, 2, SU_NUMBER, SU_NUMBER);
	su_pushinteger(s, (rand() % (su_tointeger(s, -2) + 1)) + su_tointeger(s, -1));
	return 1;
}

static int seed(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_NUMBER);
	srand((unsigned)su_tonumber(s, -1));
	return 0;
}

extern void libseq(su_state *s);

void libmath(su_state *s) {
	int top = su_top(s);
	
	su_pushstring(s, "pi");
	su_pushnumber(s, pi);
	su_pushstring(s, "huge");
	su_pushnumber(s, HUGE_VAL);
	
	FUNC_DEC(abs)
	FUNC_DEC(acos)
	FUNC_DEC(asin)
	FUNC_DEC(cos)
	FUNC_DEC(cosh)
	FUNC_DEC(ceil)
	FUNC_DEC(floor)
	FUNC_DEC(fmod)
	FUNC_DEC(log)
	FUNC_DEC(log10)
	FUNC_DEC(sin)
	FUNC_DEC(sinh)
	FUNC_DEC(sqrt)
	FUNC_DEC(tan)
	FUNC_DEC(tanh)
	
	su_pushstring(s, "atan");
	su_pushfunction(s, &atan_);
	su_pushstring(s, "modf");
	su_pushfunction(s, &modf_);
	su_pushstring(s, "frexp");
	su_pushfunction(s, &frexp_);
	su_pushstring(s, "ldexp");
	su_pushfunction(s, &ldexp_);
	su_pushstring(s, "min");
	su_pushfunction(s, &min_);
	su_pushstring(s, "max");
	su_pushfunction(s, &max_);
	su_pushstring(s, "deg");
	su_pushfunction(s, &deg_);
	su_pushstring(s, "rad");
	su_pushfunction(s, &rad_);
	
	su_pushstring(s, "random");
	su_pushfunction(s, &random_);
	su_pushstring(s, "seed");
	su_pushfunction(s, &seed);

	su_map(s, (su_top(s) - top) / 2);
	su_setglobal(s, "math");
}
