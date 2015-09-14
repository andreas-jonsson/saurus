/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "saurus.h"

static int seq(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_NIL);
	su_seq(s, -1, 0);
	return 1;
}

static int rseq(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_NIL);
	su_seq(s, -1, 1);
	return 1;
}

static int list(su_state *s, int narg) {
	if (narg > 0) {
		su_list(s, narg);
		return 1;
	} else {
		return 0;
	}
}

static int cons(su_state *s, int narg) {
	su_check_arguments(s, 2, SU_NIL, SU_NIL);
	su_cons(s);
	return 1;
}

static int range(su_state *s, int narg) {
	su_check_arguments(s, 2, SU_NUMBER, SU_NUMBER);
	su_range(s, -2);
	return 1;
}

static int first(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_SEQ);
	su_first(s, -1);
	return 1;
}

static int rest(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_SEQ);
	su_rest(s, -1);
	return 1;
}

static int vector(su_state *s, int narg) {
	su_vector(s, narg);
	return 1;
}

static int push(su_state *s, int narg) {
	su_check_arguments(s, -1, SU_VECTOR);
	su_vector_push(s, -narg, narg - 1);
	return 1;
}

static int pop(su_state *s, int narg) {
	su_check_arguments(s, 2, SU_VECTOR, SU_NUMBER);
	su_vector_pop(s, -2, -1);
	return 1;
}

static int map(su_state *s, int narg) {
	su_assert(s, narg % 2 == 0, "Expected key value pairs!");
	su_map(s, narg / 2);
	return 1;
}

static int dissoc(su_state *s, int narg) {
	su_check_arguments(s, 2, SU_MAP, SU_NIL);
	su_map_remove(s, -1);
	return 1;
}

static int assocq(su_state *s, int narg) {
	su_check_arguments(s, 2, SU_MAP, SU_NIL);
	su_pushboolean(s, su_map_has(s, -1));
	return 1;
}

static int length(su_state *s, int narg) {
	unsigned size;
	su_object_type_t type;
	su_check_num_arguments(s, 1);
	type = su_type(s, -1);
	switch (type) {
		case SU_VECTOR:
			su_pushinteger(s, su_vector_length(s, -1));
			break;
		case SU_MAP:
			su_pushinteger(s, su_map_length(s, -1));
			break;
		case SU_STRING:
			su_tostring(s, -1, &size);
			su_pushinteger(s, (int)size - 1);
			break;
		default:
			su_error(s, "%s has no length!", su_type_name(s, -1));
	}
	return 1;
}

static int assoc(su_state *s, int narg) {
	su_object_type_t type;
	su_check_num_arguments(s, 3);
	type = su_type(s, -3);
	switch (type) {
		case SU_VECTOR:
			su_check_type(s, -2, SU_NUMBER);
			su_copy(s, -2);
			su_copy(s, -2);
			su_vector_set(s, -5);
			break;
		case SU_MAP:
			su_copy(s, -2);
			su_copy(s, -2);
			su_map_insert(s, -5);
			break;
		default:
			su_error(s, "Can't assoc %s!", su_type_name(s, -1));
	}
	return 1;
}

void libseq(su_state *s) {
	int top;
	su_pushfunction(s, &seq);
	su_setglobal(s, "seq");
	su_pushfunction(s, &cons);
	su_setglobal(s, "cons");
	su_pushfunction(s, &range);
	su_setglobal(s, "range");
	
	su_pushfunction(s, &first);
	su_setglobal(s, "first");
	su_pushfunction(s, &rest);
	su_setglobal(s, "rest");
	
	su_pushfunction(s, &vector);
	su_setglobal(s, "vector");
	su_pushfunction(s, &map);
	su_setglobal(s, "hashmap");
	
	top = su_top(s);
	
	su_pushstring(s, "rseq");
	su_pushfunction(s, &rseq);
	su_pushstring(s, "list");
	su_pushfunction(s, &list);
	
	su_pushstring(s, "push");
	su_pushfunction(s, &push);
	su_pushstring(s, "pop");
	su_pushfunction(s, &pop);

	su_pushstring(s, "dissoc");
	su_pushfunction(s, &dissoc);
	su_pushstring(s, "assoc?");
	su_pushfunction(s, &assocq);
	
	su_pushstring(s, "assoc");
	su_pushfunction(s, &assoc);
	su_pushstring(s, "length");
	su_pushfunction(s, &length);
	
	su_map(s, (su_top(s) - top) / 2);
	su_setglobal(s, "sequence");
}
