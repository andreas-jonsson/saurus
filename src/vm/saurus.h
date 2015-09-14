/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef _SAURUS_H_
#define _SAURUS_H_

/********** Options ***********/

/* #define SU_OPT_C_LINE */

/******************************/

#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

#define SU_SCRATCHPAD_SIZE 1024
#define SU_VALUE_DATA_SIZE 8
#define SU_VALUE_SIZE 16

typedef struct state su_state;

enum {
    SU_FALSE,
    SU_TRUE
};

enum {
	SU_NIL_INDEX,
	SU_GLOBAL_INDEX,
	SU_REGISTRY_INDEX,
	SU_INDEX_TOP
};

enum {
	SU_DEBUG_MASK_INSTRUCTION = 0x1,
	SU_DEBUG_MASK_LINE = 0x2
};

enum su_object_type {
    SU_INV, SU_NIL, SU_BOOLEAN, SU_STRING, SU_NUMBER,
    SU_SEQ, SU_FUNCTION, SU_NATIVEFUNC, SU_VECTOR, SU_MAP,
    SU_LOCAL, SU_GLOBAL, SU_NATIVEPTR, SU_NATIVEDATA, SU_NUM_OBJECT_TYPES
};

typedef struct {
	unsigned char _[SU_VALUE_SIZE];
} su_value_t;

typedef struct {
	const char *file;
	int line;
} su_debug_data;

typedef enum su_object_type su_object_type_t;

typedef int (*su_nativefunc)(su_state*,int);
typedef const void* (*su_reader)(size_t*,void*);
typedef void* (*su_alloc)(void*,size_t);
typedef void (*su_debugfunc)(su_state*,su_debug_data*,void*);

typedef void (*su_gc_trace_cb_t)(su_state*,su_value_t*);

typedef int (*su_native_data_call_cb_t)(su_state*,void*,int);
typedef void (*su_native_data_trace_cb_t)(su_state*,void*,su_gc_trace_cb_t);
typedef void (*su_native_data_gc_cb_t)(su_state*,void*);

typedef struct {
    const char *name;
    su_native_data_call_cb_t call;
    su_native_data_gc_cb_t gc_callback;
	su_native_data_trace_cb_t trace_callback;
} su_data_class_t;

su_state *su_init(su_alloc alloc);
void su_close(su_state *s);
void su_libinit(su_state *s);
const char *su_version(int *major, int *minor, int *patch);
void *su_allocate(su_state *s, void *p, size_t n);
su_alloc su_allocator(su_state *s);
char *su_scratchpad(su_state *s);
int su_compile(su_state *s, const char *code, const char *name, char **inline_c, char **result, size_t *size);
int su_clambda(su_state *s, su_nativefunc f);
void su_debug(su_state *s, int mask, su_debugfunc f, void *data);

void su_fork(su_state *s, int narg);
void su_transaction(su_state *s, int narg);
void su_thread_disposable(su_state *s);
void su_thread_indisposable(su_state *s);
int su_num_threads(su_state *s);
int su_num_cores(su_state *s);

void su_read_value(su_state *s, su_value_t *val, int idx);
void su_push_value(su_state *s, su_value_t *val);

void *su_reg_reference(su_state *s, int idx);
void su_unreg_reference(su_state *s, void *ref);

void su_seterror(su_state *s, jmp_buf jmp, int flag);
void su_error(su_state *s, const char *fmt, ...);
void su_assert(su_state *s, int cond, const char *fmt, ...);
void su_check_type(su_state *s, int idx, su_object_type_t t);
void su_check_arguments(su_state *s, int num, ...);
void su_check_num_arguments(su_state *s, int num);

const char *su_stringify(su_state *s, int idx);
const char *su_type_name(su_state *s, int idx);
su_object_type_t su_type(su_state *s, int idx);

void su_pushnil(su_state *s);
void su_pushfunction(su_state *s, su_nativefunc f);
su_nativefunc su_tofunction(su_state *s, int idx);
void su_pushnumber(su_state *s, double n);
double su_tonumber(su_state *s, int idx);
void su_pushboolean(su_state *s, int b);
int su_toboolean(su_state *s, int idx);
void su_pushinteger(su_state *s, int i);
int su_tointeger(su_state *s, int idx);
void su_pushbytes(su_state *s, const char *ptr, unsigned size);
void su_pushstring(su_state *s, const char *str);
const char *su_tostring(su_state *s, int idx, unsigned *size);
void su_pushpointer(su_state *s, void *ptr);
void *su_topointer(su_state *s, int idx);
void *su_newdata(su_state *s, unsigned size, const su_data_class_t *vt);
void *su_todata(su_state *s, const su_data_class_t **vt, int idx);

void su_string_begin(su_state *s, const char *str);
char *su_string_mem(su_state *s, unsigned size);
void su_string_cat(su_state *s, const char *str);
void su_string_catf(su_state *s, const char *fmt, ...);
void su_string_ch(su_state *s, char ch);
void su_string_push(su_state *s);

void su_ref_global(su_state *s, int idx);
void su_ref_local(su_state *s, int idx);
void su_unref(su_state *s, int idx);
void su_setref(su_state *s, int idx);

int su_unpack_seq(su_state *s, int idx);
void su_seq_reverse(su_state *s, int idx);
void su_seq(su_state *s, int idx, int reverse);
void su_cat_seq(su_state *s);
void su_list(su_state *s, int num);
void su_first(su_state *s, int idx);
void su_rest(su_state *s, int idx);
void su_range(su_state *s, int idx);
void su_cons(su_state *s);

void su_vector(su_state *s, int num);
int su_vector_length(su_state *s, int idx);
void su_vector_index(su_state *s, int idx);
void su_vector_set(su_state *s, int idx);
void su_vector_push(su_state *s, int idx, int num);
void su_vector_pop(su_state *s, int idx, int num);
void su_vector_cat(su_state *s);

void su_map(su_state *s, int num);
int su_map_length(su_state *s, int idx);
int su_map_get(su_state *s, int idx);
void su_map_insert(su_state *s, int idx);
void su_map_remove(su_state *s, int idx);
int su_map_has(su_state *s, int idx);
void su_map_cat(su_state *s);

int su_getglobal(su_state *s, const char *name);
void su_setglobal(su_state *s, const char *name);

int su_load(su_state *s, su_reader reader, void *data);
void su_call(su_state *s, int narg, int nret);
void su_pop(su_state *s, int n);
void su_copy(su_state *s, int idx);
void su_copy_range(su_state *s, int idx, int num);
void su_swap(su_state *s, int a, int b);
int su_top(su_state *s);

void su_gc(su_state *s);

FILE *su_stdout(su_state *s);
FILE *su_stdin(su_state *s);
FILE *su_stderr(su_state *s);
void su_set_stdout(su_state *s, FILE *fp);
void su_set_stdin(su_state *s, FILE *fp);
void su_set_stderr(su_state *s, FILE *fp);

#endif
