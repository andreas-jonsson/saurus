/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "saurus.h"
#include "intern.h"
#include "gc.h"
#include "ref.h"
#include "seq.h"

#define NUM_QUICK_TRIES 5
#define ERROR_MSG "Locals can only be mutated and accessed by owner thread!"

value_t ref_global(su_state *s, value_t *val) {
	value_t v;
	v.type = SU_GLOBAL;
	v.obj.glob = (global_t*)su_allocate(s, NULL, sizeof(global_t));
	v.obj.glob->value.value = val->type == SU_NIL ? NULL : val->obj.ptr;
	gc_insert_object(s, v.obj.gc_object, SU_GLOBAL);
	return v;
}

value_t unref_global(su_state *s, global_t *glob) {
	value_t v;
	void *ptr = atomic_get_ptr(&glob->value);
	if (!ptr) {
		v.type = SU_NIL;
		return v;
	}
	v.type = SU_MAP;
	v.obj.ptr = ptr;
	return v;
}

value_t ref_local(su_state *s, value_t *val) {
	value_t v;
	v.type = SU_LOCAL;
	v.obj.loc = (local_t*)su_allocate(s, NULL, sizeof(local_t));
	v.obj.loc->v = *val;
	v.obj.loc->tid = s->tid;
	gc_insert_object(s, &v.obj.loc->gc, SU_LOCAL);
	return v;
}

value_t unref_local(su_state *s, local_t *loc) {
	su_assert(s, s->tid == loc->tid, ERROR_MSG);
	return loc->v;
}

void set_local(su_state *s, local_t *loc, value_t *val) {
	su_assert(s, s->tid == loc->tid, ERROR_MSG);
	loc->v = *val;
	gc_gray_mutable(s, &loc->gc);
}

void su_ref_global(su_state *s, int idx) {
	value_t v = ref_global(s, STK(TOP(idx)));
	push_value(s, &v);
}

void su_ref_local(su_state *s, int idx) {
	value_t v = ref_local(s, STK(TOP(idx)));
	push_value(s, &v);
}

void su_unref(su_state *s, int idx) {
	value_t v = *STK(TOP(idx));
	if (v.type == SU_LOCAL)
		v = unref_local(s, v.obj.loc);
	else if (v.type == SU_GLOBAL)
		v = unref_global(s, v.obj.glob);
	push_value(s, &v);
}

void su_setref(su_state *s, int idx) {
	value_t *v = STK(TOP(idx));
	set_local(s, v->obj.loc, STK(-1));
	su_pop(s, 1);
}

void su_transaction(su_state *s, int narg) {
	map_t *ptr, *nptr;
	value_t v;
	su_object_type_t t;
	unsigned tries = 0;
	global_t *glob = STK(-narg - 2)->obj.glob;
	
	do {
		/*
		if (tries >= NUM_QUICK_TRIES) {
			su_thread_indisposable(s);
			thread_sleep(tries);
			su_thread_disposable(s);
		}
		*/
		
		if (tries++)
			su_pop(s, 1);
		
		ptr = (map_t*)atomic_get_ptr(&glob->value);
		if (ptr) {
			v.type = SU_MAP;
			v.obj.ptr = ptr;
		} else {
			v.type = SU_NIL;
		}
		
		su_copy(s, -narg - 1);
		push_value(s, &v);
		su_copy_range(s, -narg - 2, narg);
		su_call(s, narg + 1, 1);
		t = STK(-1)->type;
		su_assert(s, t == SU_NIL || t == SU_MAP, "Expected hashmap or nil!");
		nptr = t == SU_MAP ? STK(-1)->obj.ptr : NULL;
	} while(!atomic_cas_ptr(&glob->value, ptr, nptr));
	
	gc_gray_mutable(s, &glob->gc);
	s->stack[s->stack_top - narg + 1] = s->stack[s->stack_top - 1];
	s->stack_top -= narg;
}
