/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef _INTERN_H_
#define _INTERN_H_

#include "saurus.h"
#include "platform.h"

#include <stdio.h>
#include <setjmp.h>

#define MAX_CALLS 128
#define STACK_SIZE 512
#define GC_GRAY_SIZE 512
#define STRING_CACHE_SIZE 128

#define STK(n) (&s->stack[s->stack_top + (n)])
#define TOP(n) ((n) < 0 ? (n) : (n) - s->stack_top)
#define FRAME() (&s->frames[s->frame_top - 1])

#define xstr(s) str(s)
#define str(s) #s

#define VERSION_MAJOR 0
#define VERSION_MINOR 0
#define VERSION_PATCH 1
#define VERSION_STRING xstr(VERSION_MAJOR) "." xstr(VERSION_MINOR) "." xstr(VERSION_PATCH)

typedef struct gc gc_t;
typedef struct value value_t;
typedef struct function function_t;
typedef struct prototype prototype_t;
typedef struct instruction instruction_t;
typedef struct upvalue upvalue_t;

typedef struct local local_t;
typedef struct seq seq_t;

typedef struct vector vector_t;
typedef struct vector_node vector_node_t;
typedef struct reader_buffer reader_buffer_t;

typedef struct map map_t;
typedef struct node node_t;
typedef struct node_leaf node_leaf_t;
typedef struct node_full node_full_t;
typedef struct node_idx node_idx_t;
typedef struct node_collision node_collision_t;

typedef struct main_state_internal main_state_internal_t;

typedef void (*thread_entry_t)(su_state*);

enum {
	PROTOTYPE = SU_NUM_OBJECT_TYPES, /* 14 */
	VECTOR_NODE,
	MAP_LEAF,
	MAP_EMPTY,
	MAP_FULL,
	MAP_IDX,
	MAP_COLLISION,
	RANGE_SEQ,
	LAZY_SEQ,
	CELL_SEQ,
	TREE_SEQ,
	IT_SEQ
};

enum {
	CNIL,
	CFALSE,
	CTRUE,
	CNUMBER,
	CSTRING
};

enum {
	IGC		= 0x1,
	ISCOLLECT	= 0x2,
	IBREAK		= 0x4
};

struct gc {
	gc_t *next;
	unsigned char type;
	unsigned char flags;
	unsigned char usr;
};

typedef struct {
	unsigned size;
	char str[1];
} const_string_t;

typedef struct {
	gc_t gc;
	unsigned hash;
	unsigned size;
	char str[1];
} string_t;

typedef struct {
	unsigned char id;
	union {
		const_string_t *str;
		double num;
	} obj;
} const_t;

typedef struct {
	gc_t gc;
	su_data_class_t *vt;
	unsigned char data[1];
} native_data_t;

typedef struct {
	gc_t gc;
	aptr_t value;
} global_t;

struct value {
	union {
		int b;
		double num;
		function_t *func;
		su_nativefunc nfunc;
		gc_t *gc_object;
		string_t *str;
		vector_t *vec;
		vector_node_t *vec_node;
		seq_t *q;
		map_t *m;
		node_t *map_node;
		local_t *loc;
		global_t *glob;
		native_data_t *data;
		void *ptr;
		unsigned char value_data[SU_VALUE_DATA_SIZE];
	} obj;
	unsigned char type;
};

typedef struct {
	function_t *func;
	int stack_top;
	int ret_addr;
} frame_t;

struct prototype {
	gc_t gc;
	
	unsigned num_inst;
	instruction_t *inst;
	unsigned num_const;
	const_t *constants;
	unsigned num_ups;
	upvalue_t *upvalues;
	unsigned num_prot;
	prototype_t *prot;
	
	const_string_t *name;
	unsigned num_lineinf;
	unsigned *lineinf;
};

struct function {
	gc_t gc;
	int narg;
	prototype_t *prot;
	unsigned num_const;
	value_t *constants;
	unsigned num_ups;
	value_t *upvalues;
};

typedef struct {
	unsigned hash;
	string_t *str;
} string_cache_t;

struct state {
	gc_t gc;
	
	su_alloc alloc;
	su_state *main_state;

	string_t *string_builder;
	string_cache_t string_cache[16][STRING_CACHE_SIZE];
	int string_cache_head[16];
	
	gc_t *gray[GC_GRAY_SIZE];
	unsigned gray_size;
	
	int debug_mask;
	void *debug_cb_data;
	su_debugfunc debug_cb;
	
	char scratch_pad[SU_SCRATCHPAD_SIZE];
	FILE *fstdin, *fstdout, *fstderr;
	
	jmp_buf err;
	int errtop;
	int ferrtop;
	
	frame_t *frame;
	prototype_t *prot;
	int pc, narg;
	int interrupt;
	
	aint_t thread_indisposable;
	aint_t thread_finished;
	int tid;
	
	int frame_top;
	frame_t frames[MAX_CALLS];
	
	int stack_top;
	value_t stack[STACK_SIZE];
	
	main_state_internal_t *msi;
};

struct main_state_internal {
	aint_t gc_lock;
	aint_t gc_list_lock;
	gc_t *gc_root;
	gc_t *gc_gray[GC_GRAY_SIZE];
	int gc_state;
	unsigned gc_gray_size;
	aint_t gc_throttle;
	int gc_num_objects;
	aint_t num_objects;
	
	int num_c_lambdas;
	value_t *c_lambdas;
	
	aint_t interrupt;
	char *ref_counter;
	
	aint_t tid_count;
	aint_t thread_count;
	aint_t thread_pool_lock;
	
	su_state threads[SU_OPT_MAX_THREADS];
};

unsigned hash_value(value_t *v);
void push_value(su_state *s, value_t *v);
int value_eq(value_t *a, value_t *b);
int read_prototype(su_state *s, reader_buffer_t *buffer, prototype_t *prot);
gc_t *gc_insert_object(su_state *s, gc_t *obj, su_object_type_t type);
gc_t *string_from_cache(su_state *s, const char *str, unsigned size);
unsigned murmur(const void *key, int len, unsigned seed);
void interrupt(su_state *s, int in);
void thread_interrupt(su_state *s, int in);
void unmask_interrupt(su_state *s, int in);
void unmask_thread_interrupt(su_state *s, int in);

#endif
