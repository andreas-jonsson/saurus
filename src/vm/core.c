/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "intern.h"
#include "ref.h"
#include "seq.h"
#include "gc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>
#include <math.h>
#include <stdarg.h>

#define MAIN_STATE_ONLY_MSG "Can only do this from the main-state!"

static aint_t print_spin_lock = {0};

enum {
	OP_PUSH,
	OP_POP,
	OP_LOAD,
	OP_LUP,
	OP_LCL,

	OP_ADD,
	OP_SUB,
	OP_MUL,
	OP_DIV,
	OP_MOD,
	OP_POW,
	OP_UNM,

	OP_EQ,
	OP_LESS,
	OP_LEQUAL,

	OP_NOT,
	OP_AND,
	OP_OR,

	OP_TEST,
	OP_FOR,
	OP_JMP,

	OP_RETURN,
	OP_SHIFT,
	OP_CALL,
	OP_TCALL,
	OP_LAMBDA,

	OP_GETGLOBAL,
	OP_SETGLOBAL
};

struct instruction {
	unsigned char id;
	unsigned char a;
	short b;
};

struct upvalue {
	unsigned short lv;
	unsigned short idx;
};

struct reader_buffer{
	su_reader reader;
	void *data;
	char *buffer;
	size_t offset;
	size_t len;
	size_t size;
};

const char *su_version(int *major, int *minor, int *patch) {
	if (major) *major = VERSION_MAJOR;
	if (minor) *minor = VERSION_MINOR;
	if (patch) *patch = VERSION_PATCH;
	return VERSION_STRING;
}

su_alloc su_allocator(su_state *s) {
	return s->alloc;
}

void interrupt(su_state *s, int in) {
	int old;
	do {
		old = atomic_get(&s->msi->interrupt);
	} while (!atomic_cas(&s->msi->interrupt, old, old | in));
}

void thread_interrupt(su_state *s, int in) {
	s->interrupt |= in;
}

void unmask_interrupt(su_state *s, int in) {
	int old;
	do {
		old = atomic_get(&s->msi->interrupt);
	} while (!atomic_cas(&s->msi->interrupt, old, old & ~in));
}

void unmask_thread_interrupt(su_state *s, int in) {
	s->interrupt &= ~in;
}

void *su_allocate(su_state *s, void *p, size_t n) {
	void *np;
	if (n) {
		thread_interrupt(s, IGC);
		np = s->alloc(p, n);
		su_assert(s, np != NULL, "Out of memory!");
		return np;
	} else {
		return s->alloc(p, 0);
	}
}

void push_value(su_state *s, value_t *v) {
	su_assert(s, s->stack_top < STACK_SIZE, "Stack overflow!");
	s->stack[s->stack_top++] = *v;
}

int value_eq(value_t *a, value_t *b) {
	if (a->type != b->type)
		return 0;
	if (a->type == SU_BOOLEAN && a->obj.b == b->obj.b)
		return 1;
	if (a->type == SU_NUMBER && a->obj.num == b->obj.num)
		return 1;
	if (a->type == SU_STRING &&
		(a->obj.str == b->obj.str ||
		(a->obj.str->hash == b->obj.str->hash && a->obj.str->size == b->obj.str->size && !memcmp(a->obj.str->str, b->obj.str->str, a->obj.str->size)))
	)
		return 1;
	return a->obj.ptr == b->obj.ptr;
}

char *su_scratchpad(su_state *s) {
	return s->scratch_pad;
}

void su_swap(su_state *s, int a, int b) {
	value_t v = *STK(TOP(a));
	s->stack[s->stack_top + a] = *STK(TOP(b));
	s->stack[s->stack_top + b] = v;
}

void su_pop(su_state *s, int n) {
	s->stack_top -= n;
	assert(s->stack_top >= 0);
}

void su_copy(su_state *s, int idx) {
	push_value(s, STK(TOP(idx)));
}

int su_top(su_state *s) {
	return s->stack_top - 1;
}

void su_range(su_state *s, int idx) {
	value_t v = range_create(s, STK(TOP(idx))->obj.num, STK(-1)->obj.num);
	push_value(s, &v);
}

void su_copy_range(su_state *s, int idx, int num) {
	memcpy(&s->stack[s->stack_top], STK(TOP(idx)), sizeof(value_t) * num);
	s->stack_top += num;
}

static reader_buffer_t *buffer_open(su_state *s, su_reader reader, void *data) {
	reader_buffer_t *buffer = (reader_buffer_t*)su_allocate(s, NULL, sizeof(reader_buffer_t));
	buffer->reader = reader;
	buffer->data = data;
	buffer->offset = 0;
	buffer->size = 0;
	buffer->len = 0;
	buffer->buffer = reader ? NULL : data;
	return buffer;
}

static void buffer_close(su_state *s, reader_buffer_t *buffer) {
	if (buffer->reader) {
		buffer->reader(NULL, buffer->data);
		su_allocate(s, buffer->buffer, 0);
	}
	su_allocate(s, buffer, 0);
}

static int buffer_read(su_state *s, reader_buffer_t *buffer, void *dest, size_t num_bytes) {
	unsigned dest_offset = 0;
	do {
		const void *res;
		size_t size = buffer->reader ? (buffer->len - buffer->offset) : num_bytes;
		size_t num = num_bytes < size ? num_bytes : size;
		memcpy(((char*)dest) + dest_offset, &buffer->buffer[buffer->offset], num);

		buffer->offset += num;
		dest_offset += num;
		num_bytes -= num;

		if (!buffer->reader)
			break;

		if (num_bytes == 0) return 0;
		res = buffer->reader(&size, buffer->data);
		if (!res || size == 0) return -1;

		while (buffer->size < size) {
			buffer->size = buffer->size * 2 + 1;
			buffer->buffer = su_allocate(s, buffer->buffer, buffer->size);
		}

		buffer->offset = 0;
		buffer->len = size;
		memcpy(buffer->buffer, res, size);
	} while (num_bytes);
	return 0;
}

static int verify_header(su_state *s, reader_buffer_t *buffer) {
	char sign[4];
	unsigned char version[2];
	unsigned short flags;

	buffer_read(s, buffer, sign, sizeof(sign));
	if (memcmp(sign, "\x1bsuc", sizeof(sign)))
		return -1;

	buffer_read(s, buffer, version, sizeof(version));
	if (version[0] != VERSION_MAJOR && version[1] != VERSION_MINOR)
		return -1;

	buffer_read(s, buffer, &flags, sizeof(flags));
	if (flags != 0)
		return -1;

	return 0;
}

unsigned murmur(const void *key, int len, unsigned seed) {
	const unsigned m = 0x5bd1e995;
	const int r = 24;
	unsigned h = seed ^ len;
	const unsigned char * data = (const unsigned char*)key;
	while (len >= 4) {
		unsigned k = *(unsigned*)data;
		k *= m;
		k ^= k >> r;
		k *= m;
		h *= m;
		h ^= k;
		data += 4;
		len -= 4;
	}
	switch (len) {
		case 3: h ^= data[2] << 16;
		case 2: h ^= data[1] << 8;
		case 1: h ^= data[0];
	        h *= m;
	};
	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;
	return h;
}

unsigned hash_value(value_t *v) {
	switch (v->type) {
		case SU_NIL:
			return (unsigned)SU_NIL;
		case SU_BOOLEAN:
			return (unsigned)v->obj.b + (unsigned)SU_BOOLEAN;
		case SU_NUMBER:
			return murmur(&v->obj.num, sizeof(double), (unsigned)SU_NUMBER);
		case SU_STRING:
			return v->obj.str->hash;
		default:
			return murmur(&v->obj.ptr, sizeof(void*), (unsigned)v->type);
	}
}

gc_t *gc_insert_object(su_state *s, gc_t *obj, su_object_type_t type) {
	obj->type = type;
	obj->flags = GC_FLAG_BLACK;
	obj->usr = 0;
	spin_lock(&s->msi->gc_list_lock);
	obj->next = s->msi->gc_root;
	s->msi->gc_root = obj;
	atomic_add(&s->msi->num_objects, 1);
	spin_unlock(&s->msi->gc_list_lock);
	return obj;
}

gc_t *string_from_cache(su_state *s, const char *str, unsigned size) {
	value_t v;
	string_cache_t entry;
	string_cache_t *entries;
	unsigned hash = murmur(str, size, 0);
	unsigned index = (unsigned)bit_count((int)hash) - 8;
	int i;
	
	if (index < 16) {
		entries = s->string_cache[index];
		for (i = 0; i < STRING_CACHE_SIZE; i++) {
			if (entries[i].hash == hash && !memcmp(entries[i].str->str, str, size))
				return &entries[i].str->gc;
		}
	}
	
	v.type = SU_STRING;
	v.obj.ptr = su_allocate(s, NULL, sizeof(string_t) + size);
	v.obj.str->size = size;
	memcpy(v.obj.str->str, str, size);
	v.obj.str->str[size] = '\0';
	v.obj.str->hash = hash;
	
	if (index < 16) {
		entry.hash = hash;
		entry.str = v.obj.str;
		entries[s->string_cache_head[index]] = entry;
		s->string_cache_head[index] = (s->string_cache_head[index] + 1) % STRING_CACHE_SIZE;
	}
	
	gc_insert_object(s, v.obj.gc_object, SU_STRING);
	return v.obj.gc_object;
}

gc_t *string_build_from_cache(su_state *s) {
	value_t v;
	v.type = SU_STRING;
	v.obj.str = s->string_builder;
	s->string_builder->str[s->string_builder->size] = '\0';
	gc_insert_object(s, v.obj.gc_object, SU_STRING);
	return v.obj.gc_object;
}

const char *stringify(su_state *s, value_t *v) {
	int tmp;
	switch (v->type) {
		case SU_NIL:
			return "nil";
		case SU_BOOLEAN:
			return v->obj.b ? "true" : "false";
		case SU_NUMBER:
			tmp = (int)v->obj.num;
			if (v->obj.num == (double)tmp)
				sprintf(s->scratch_pad, "%i", tmp);
			else
				sprintf(s->scratch_pad, "%f", v->obj.num);
			break;
		case SU_STRING:
			return ((string_t*)v->obj.gc_object)->str;
		case SU_FUNCTION:
			sprintf(s->scratch_pad, "<function %p>", (void*)v->obj.func);
			break;
		case SU_NATIVEFUNC:
			sprintf(s->scratch_pad, "<native-function %p>", (void*)v->obj.nfunc);
			break;
		case SU_NATIVEPTR:
			sprintf(s->scratch_pad, "<native-pointer %p>", v->obj.ptr);
			break;
		case SU_NATIVEDATA:
			if (v->obj.data->vt && v->obj.data->vt->name)
				sprintf(s->scratch_pad, "<%s>", v->obj.data->vt->name);
			else
				sprintf(s->scratch_pad, "<native-data %p>", v->obj.ptr);
			break;
		case SU_VECTOR:
			sprintf(s->scratch_pad, "<vector %p>", v->obj.ptr);
			break;
		case SU_MAP:
			sprintf(s->scratch_pad, "<hashmap %p>", v->obj.ptr);
			break;
		case SU_LOCAL:
			sprintf(s->scratch_pad, "<local-reference %p>", v->obj.ptr);
			break;
		case SU_GLOBAL:
			sprintf(s->scratch_pad, "<global-reference %p>", v->obj.ptr);
			break;
		case SU_INV:
			sprintf(s->scratch_pad, "<invalid>");
			break;
		case IT_SEQ:
		case CELL_SEQ:
		case TREE_SEQ:
		case RANGE_SEQ:
		case LAZY_SEQ:
			sprintf(s->scratch_pad, "<sequence %p>", v->obj.ptr);
			break;
		default:
			assert(0);
	}
	return s->scratch_pad;
}

const char *su_stringify(su_state *s, int idx) {
	return stringify(s, STK(TOP(idx)));
}

static value_t init_globals(su_state *s) {
	value_t key, m, tmp;
	key.type = SU_STRING;
	key.obj.gc_object = string_from_cache(s, "_G", 2);
	
	tmp.type = SU_NIL;
	
	m = map_create_empty(s);
	tmp = ref_local(s, &tmp);
	m = map_insert(s, m.obj.m, &key, hash_value(&key), &tmp);
	set_local(s, tmp.obj.loc, &m);
	return tmp;
}

static void set_global(su_state *s, const char *var, unsigned hash, int size, value_t *val) {
	value_t key, m;
	key.type = SU_STRING;
	key.obj.gc_object = string_from_cache(s, var, size);
	
	m = unref_local(s, s->stack[SU_GLOBAL_INDEX].obj.loc);
	m = map_insert(s, m.obj.m, &key, hash, val);
	set_local(s, s->stack[SU_GLOBAL_INDEX].obj.loc, &m);
}

static value_t get_global(su_state *s, const char *var, unsigned hash, int size) {
	value_t key, m;
	key.type = SU_STRING;
	key.obj.gc_object = string_from_cache(s, var, size);
	
	m = unref_local(s, s->stack[SU_GLOBAL_INDEX].obj.loc);
	return map_get(s, m.obj.m, &key, hash);
}

static int isseq(su_state *s, value_t *v) {
	switch (v->type) {
		case IT_SEQ:
		case CELL_SEQ:
		case TREE_SEQ:
		case RANGE_SEQ:
		case LAZY_SEQ:
			return 1;
		default:
			return 0;
	}
}

void su_pushnil(su_state *s) {
	value_t v;
	v.type = SU_NIL;
	push_value(s, &v);
}

void su_pushfunction(su_state *s, su_nativefunc f) {
	value_t v;
	v.type = SU_NATIVEFUNC;
	v.obj.nfunc = f;
	push_value(s, &v);
}

su_nativefunc su_tofunction(su_state *s, int idx) {
	value_t *v = STK(TOP(idx));
	if (v->type == SU_NATIVEFUNC)
		return v->obj.nfunc;
	return NULL;
}

int su_clambda(su_state *s, su_nativefunc f) {
	value_t v;
	int id = s->msi->num_c_lambdas;
	su_assert(s, s->main_state == s, MAIN_STATE_ONLY_MSG);
	s->msi->c_lambdas = (value_t*)su_allocate(s, s->msi->c_lambdas, sizeof(value_t) * (++s->msi->num_c_lambdas));
	if (f) {
		v.type = SU_NATIVEFUNC;
		v.obj.nfunc = f;
		s->msi->c_lambdas[id] = v;
	} else {
		s->msi->c_lambdas[id] = *STK(-1);
		s->stack_top--;
	}
	return id;
}

void su_pushnumber(su_state *s, double n) {
	value_t v;
	v.type = SU_NUMBER;
	v.obj.num = n;
	push_value(s, &v);
}

double su_tonumber(su_state *s, int idx) {
	value_t *v = STK(TOP(idx));
	if (v->type == SU_NUMBER)
		return v->obj.num;
	return 0.0;
}

void su_pushpointer(su_state *s, void *ptr) {
	value_t v;
	v.type = SU_NATIVEPTR;
	v.obj.ptr = ptr;
	push_value(s, &v);
}

void *su_topointer(su_state *s, int idx) {
	value_t *v = STK(TOP(idx));
	if (v->type == SU_NATIVEPTR)
		return v->obj.ptr;
	return NULL;
}

void *su_newdata(su_state *s, unsigned size, const su_data_class_t *vt) {
	value_t v;
	v.type = SU_NATIVEDATA;
	v.obj.data = (native_data_t*)su_allocate(s, NULL, sizeof(native_data_t) + size - 1);
	v.obj.data->vt = (su_data_class_t*)vt;
	gc_insert_object(s, v.obj.gc_object, SU_NATIVEDATA);
	push_value(s, &v);
	return (void*)v.obj.data->data;
}

void *su_todata(su_state *s, const su_data_class_t **vt, int idx) {
	value_t *v = STK(TOP(idx));
	if (v->type == SU_NATIVEDATA) {
		if (vt) *vt = v->obj.data->vt;
		return (void*)v->obj.data->data;
	}
	return NULL;
}

void su_pushinteger(su_state *s, int i) {
	su_pushnumber(s, (double)i);
}

int su_tointeger(su_state *s, int idx) {
	return (int)su_tonumber(s, idx);
}

void su_pushboolean(su_state *s, int b) {
	value_t v;
	v.type = SU_BOOLEAN;
	v.obj.b = b == 0 ? 0 : 1;
	push_value(s, &v);
}

int su_toboolean(su_state *s, int idx) {
	value_t *v = STK(TOP(idx));
	if (v->type == SU_BOOLEAN)
		return v->obj.b;
	return 0;
}

void su_pushbytes(su_state *s, const char *ptr, unsigned size) {
	value_t v;
	v.type = SU_STRING;
	v.obj.gc_object = string_from_cache(s, ptr, size);
	push_value(s, &v);
}

void su_pushstring(su_state *s, const char *str) {
	su_pushbytes(s, str, (unsigned)strlen(str));
}

const char *su_tostring(su_state *s, int idx, unsigned *size) {
	string_t *str;
	value_t *v = STK(TOP(idx));
	if (v->type == SU_STRING) {
		str = (string_t*)v->obj.gc_object;
		if (size) *size = str->size;
		return str->str;
	}
	if (size) *size = 0;
	return NULL;
}

void su_string_begin(su_state *s, const char *str) {
	size_t len = str ? strlen(str) : 0;
	s->string_builder = (string_t*)su_allocate(s, s->string_builder, sizeof(string_t) + len);
	s->string_builder->size = (unsigned)len;
	s->string_builder->hash = 0;
	if (str)
		memcpy(s->string_builder->str, str, len);
}

char *su_string_mem(su_state *s, unsigned size) {
	unsigned offset;
	assert(s->string_builder);
	offset = s->string_builder->size;
	s->string_builder->size += size;
	s->string_builder = (string_t*)su_allocate(s, (void*)s->string_builder, sizeof(string_t) + s->string_builder->size);
	return s->string_builder->str + offset;
}

void su_string_cat(su_state *s, const char *str) {
	unsigned len = (unsigned)strlen(str);
	assert(s->string_builder);
	memcpy(su_string_mem(s, len), str, len);
}

void su_string_catf(su_state *s, const char *fmt, ...) {
	int len;
	char *str;
	va_list ls, ls2;
	va_start(ls, fmt);
	va_start(ls2, fmt);
	len = vsnprintf(NULL, 0, fmt, ls) + 1;
	va_end(ls);
	str = su_string_mem(s, (unsigned)len);
	vsnprintf(str, len, fmt, ls2);
	s->string_builder->size--;
	va_end(ls2);
}

void su_string_ch(su_state *s, char ch) {
	assert(s->string_builder);
	s->string_builder = (string_t*)su_allocate(s, (void*)s->string_builder, sizeof(string_t) + (++s->string_builder->size));
	s->string_builder->str[s->string_builder->size - 1] = ch;
}

void su_string_push(su_state *s) {
	value_t v;
	assert(s->string_builder);
	v.type = SU_STRING;
	s->string_builder->hash = murmur(s->string_builder->str, s->string_builder->size, 0);
	v.obj.gc_object = string_build_from_cache(s);
	push_value(s, &v);
	s->string_builder = NULL;
}

void su_read_value(su_state *s, su_value_t *val, int idx) {
	memcpy(val, STK(TOP(idx)), sizeof(value_t));
}

void su_push_value(su_state *s, su_value_t *val) {
	push_value(s, (value_t*)val);
}

void error(su_state *s, const char *fmt, va_list args) {
	int i, pc;
	const_string_t *str;
	prototype_t *prot;

	spin_lock(&print_spin_lock);
	if (fmt) {
		fputs("\n", s->fstderr);
		vfprintf(s->fstderr, fmt, args);
		fputs("\n", s->fstderr);
	}

	fprintf(s->fstderr, "\nThread: %x\n", s->tid);
	
	for (i = s->frame_top - 1; i >= 0; i--) {
		prot = (i == s->frame_top - 1) ? s->prot : s->frames[i].func->prot;
		pc = (i == s->frame_top - 1) ? s->pc : s->frames[i + 1].ret_addr;
		str = prot->name;

		if (pc < prot->num_lineinf)
			fprintf(s->fstderr, "%i <%s : %i>\n", i, str->str, prot->lineinf[pc]);
		else
			fprintf(s->fstderr, "%i <%s : -1>\n", i, str->str);
	}
	
	fputs("\n", s->fstderr);
	fflush(s->fstderr);
	spin_unlock(&print_spin_lock);
	
	if (s->errtop >= 0) {
		if (s->main_state == s) {
			va_end(args);
			longjmp(s->err, 1);
		} else {
			atomic_set(&s->thread_finished, 1);
			/* pthread_exit(NULL); */
			exit(-1);
		}
	} else {
		exit(-1);
	}
}

void su_error(su_state *s, const char *fmt, ...) {
	va_list args;
	if (fmt) {
		va_start(args, fmt);
		error(s, fmt, args);
	} else {
		error(s, fmt, args);
	}
}

void su_assert(su_state *s, int cond, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	if (!cond) error(s, fmt, args);
	va_end(args);
}

static const char *type_name(su_object_type_t type) {
	switch ((unsigned)type) {
		case SU_INV: return "<invalid>";
		case SU_NIL: return "nil";
		case SU_BOOLEAN: return "boolean";
		case SU_STRING: return "string";
		case SU_NUMBER: return "number";
		case SU_FUNCTION: return "function";
		case SU_NATIVEFUNC: return "native-function";
		case SU_NATIVEPTR: return "native-pointer";
		case SU_NATIVEDATA: return "native-data";
		case SU_VECTOR: return "vector";
		case SU_MAP: return "hashmap";
		case SU_LOCAL: return "local-reference";
		case SU_GLOBAL: return "global-reference";
		case SU_SEQ:
		case CELL_SEQ:
		case TREE_SEQ:
		case IT_SEQ:
		case RANGE_SEQ:
		case LAZY_SEQ:
			return "sequence";
		default: assert(0);
	}
	return NULL;
}

const char *su_type_name(su_state *s, int idx) {
	return type_name(STK(TOP(idx))->type);
}

void su_map(su_state *s, int num) {
	int i;
	value_t k, v;
	value_t m = map_create_empty(s);
	for (i = num * 2; i > 0; i -= 2) {
		k = *STK(-i);
		v = *STK(-i + 1);
		m = map_insert(s, m.obj.m, &k, hash_value(&k), &v);
	}
	s->stack_top -= num * 2;
	push_value(s, &m);
}

int su_map_length(su_state *s, int idx) {
	return map_length(STK(TOP(idx))->obj.m);
}

void su_map_cat(su_state *s) {
	s->stack[s->stack_top - 2] = map_cat(s, STK(-2)->obj.m, STK(-1)->obj.m);
	s->stack_top--;
}

int su_map_get(su_state *s, int idx) {
	value_t v = *STK(-1);
	unsigned hash = hash_value(&v);
	v = map_get(s, STK(TOP(idx))->obj.m, &v, hash);
	if (v.type == SU_INV) {
		s->stack_top--;
		return 0;
	}
	s->stack[s->stack_top - 1] = v;
	return 1;
}

void su_map_insert(su_state *s, int idx) {
	value_t key = *STK(-2);
	unsigned hash = hash_value(&key);
	s->stack[s->stack_top - 2] = map_insert(s, STK(TOP(idx))->obj.m, &key, hash, STK(-1));
	s->stack_top--;
}

void su_map_remove(su_state *s, int idx) {
	value_t key = *STK(-1);
	unsigned hash = hash_value(&key);
	s->stack[s->stack_top - 1] = map_remove(s, STK(TOP(idx))->obj.m, &key, hash);
}

int su_map_has(su_state *s, int idx) {
	value_t v = *STK(-1);
	unsigned hash = hash_value(&v);
	v = map_get(s, STK(TOP(idx))->obj.m, &v, hash);
	s->stack_top--;
	return v.type != SU_INV;
}

void su_list(su_state *s, int num) {
	value_t seq = cell_create_array(s, STK(-num), num);
	s->stack_top -= num;
	push_value(s, &seq);
}

void su_first(su_state *s, int idx) {
	value_t v = seq_first(s, STK(TOP(idx))->obj.q);
	push_value(s, &v);
}

void su_rest(su_state *s, int idx) {
	value_t v = seq_rest(s, STK(TOP(idx))->obj.q);
	push_value(s, &v);
}

void su_cons(su_state *s) {
	s->stack[s->stack_top - 2] = cell_create(s, STK(-2), STK(-1));
	s->stack_top--;
}

void su_seq_reverse(su_state *s, int idx) {
	su_pushnil(s);
	su_copy(s, idx - 1);
	while (su_type(s, -1) == SU_SEQ) {
		su_first(s, -1);
		su_copy(s, -3);
		su_cons(s);
		su_swap(s, -3, -1);
		su_pop(s, 1);
		su_rest(s, -1);
		su_swap(s, -2, -1);
		su_pop(s, 1);
	}
	su_pop(s, 1);
}

void su_seq(su_state *s, int idx, int reverse) {
	value_t v;
	value_t *seq = STK(TOP(idx));
	switch (su_type(s, idx)) {
		case SU_NIL:
			su_pushnil(s);
			return;
		case SU_VECTOR:
			v = it_create_vector(s, seq->obj.vec, reverse);
			break;
		case SU_MAP:
			v = tree_create_map(s, seq->obj.m);
			break;
		case SU_STRING:
			v = it_create_string(s, seq->obj.str, reverse);
			break;
		case SU_SEQ:
			if (reverse) {
				su_seq_reverse(s, idx);
				return;
			} else {
				v = *seq;
			}
			break;
		case SU_NUMBER:
			if (reverse)
				v = range_create(s, (int)seq->obj.num, 0);
			else
				v = range_create(s, 0, (int)seq->obj.num);
			break;
		case SU_FUNCTION:
		case SU_NATIVEFUNC:
			if (!reverse) {
				v = lazy_create(s, seq);
				break;
			}
		default:
			su_error(s, "Can't sequence object of type: %s", type_name((su_object_type_t)seq->type));
	}
	push_value(s, &v);
}

int su_unpack_seq(su_state *s, int idx) {
	int num = 0;
	value_t tmp;
	value_t v = *STK(TOP(idx));
	while (isseq(s, &v)) {
		tmp = v.obj.q->vt->first(s, v.obj.q);
		push_value(s, &tmp);
		v = v.obj.q->vt->rest(s, v.obj.q);
		num++;
	}
	return num;
}

void su_cat_seq(su_state *s) {
	value_t v, f, r;
	v = *STK(-1);
	su_seq_reverse(s, -2);
	r = *STK(-1);
	
	while (v.type != SU_NIL) {
		f = seq_first(s, v.obj.q);
		r = cell_create(s, &f, &r);
		v = seq_rest(s, v.obj.q);
	}
	
	s->stack[s->stack_top - 3] = r;
	s->stack_top -= 2;
	su_seq_reverse(s, -1);
	s->stack[s->stack_top - 2] = s->stack[s->stack_top - 1];
	s->stack_top--;
}

void su_vector(su_state *s, int num) {
	int i;
	value_t vec = vector_create_empty(s);
	for (i = 0; i < num; i++)
		vec = vector_push(s, vec.obj.vec, STK(-(num - i)));
	s->stack_top -= num;
	push_value(s, &vec);
}

void su_vector_cat(su_state *s) {
	s->stack[s->stack_top - 2] = vector_cat(s, STK(-2)->obj.vec, STK(-1)->obj.vec);
	s->stack_top--;
}

int su_vector_length(su_state *s, int idx) {
	return vector_length(STK(TOP(idx))->obj.vec);
}

void su_vector_index(su_state *s, int idx) {
	s->stack[s->stack_top - 1] = vector_index(s, STK(TOP(idx))->obj.vec, (int)STK(-1)->obj.num);
}

void su_vector_set(su_state *s, int idx) {
	s->stack[s->stack_top - 2] = vector_set(s, STK(TOP(idx))->obj.vec, (int)STK(-2)->obj.num, STK(-1));
	su_pop(s, 1);
}

void su_vector_push(su_state *s, int idx, int num) {
	int i;
	value_t vec = *STK(TOP(idx));
	for (i = 0; i < num; i++)
		vec = vector_push(s, vec.obj.vec, STK(-(num - i)));
	s->stack_top -= num;
	push_value(s, &vec);
}

void su_vector_pop(su_state *s, int idx, int num) {
	int i;
	int n = (int)STK(TOP(num))->obj.num;
	value_t vec = *STK(TOP(idx));
	for (i = 0; i < n; i++)
		vec = vector_pop(s, vec.obj.vec);
	push_value(s, &vec);
}

void su_check_type(su_state *s, int idx, su_object_type_t t) {
	value_t *v = STK(TOP(idx));
	su_assert(s, t == SU_SEQ ? isseq(s, v) : v->type == t, "Bad argument: Expected %s, but got %s.", type_name(t), type_name((su_object_type_t)v->type));
}

void su_seterror(su_state *s, jmp_buf jmp, int flag) {
	memcpy(s->err, jmp, sizeof(jmp_buf));
	if (flag && s->errtop >= 0) {
		s->stack_top = s->errtop;
		s->frame_top = s->ferrtop;
	} else if (flag < 0) {
		s->errtop = -1;
		s->ferrtop = -1;
	} else {
		s->errtop = s->stack_top;
		s->ferrtop = s->frame_top;
	}
}

su_object_type_t su_type(su_state *s, int idx) {
	return isseq(s, STK(TOP(idx))) ? SU_SEQ : (su_object_type_t)STK(idx)->type;
}

int su_getglobal(su_state *s, const char *name) {
	int size = strlen(name);
	value_t v = get_global(s, name, murmur(name, size, 0), size);
	if (v.type == SU_INV)
		return 0;
	push_value(s, &v);
	return 1;
}

void su_setglobal(su_state *s, const char *name) {
	value_t v;
	unsigned size = strlen(name);
	v.type = SU_STRING;
	v.obj.gc_object = string_from_cache(s, name, size);
	set_global(s, name, hash_value(&v), size, STK(-1));
	su_pop(s, 1);
}

void *su_reg_reference(su_state *s, int idx) {
	value_t key;
	unsigned hash;
	su_assert(s, s->main_state == s, MAIN_STATE_ONLY_MSG);
	key.type = SU_NATIVEPTR;
	key.obj.ptr = (void*)s->msi->ref_counter;
	hash = hash_value(&key);
	s->stack[SU_REGISTRY_INDEX] = map_insert(s, s->stack[SU_REGISTRY_INDEX].obj.m, &key, hash, STK(TOP(idx)));
	s->msi->ref_counter++;
	assert(s->msi->ref_counter);
	return key.obj.ptr;
}

void su_unreg_reference(su_state *s, void *ref) {
	value_t key;
	unsigned hash;
	su_assert(s, s->main_state == s, MAIN_STATE_ONLY_MSG);
	key.type = SU_NATIVEPTR;
	key.obj.ptr = ref;
	hash = hash_value(&key);
	s->stack[SU_REGISTRY_INDEX] = map_remove(s, s->stack[SU_REGISTRY_INDEX].obj.m, &key, hash);
	s->stack_top--;
}

void su_debug(su_state *s, int mask, su_debugfunc f, void *data) {
	s->debug_mask = mask;
	s->debug_cb = f;
	s->debug_cb_data = data;
	thread_interrupt(s, IBREAK);
}

value_t create_value(su_state *s, const_t *constant) {
	value_t v;
	switch (constant->id) {
		case CSTRING:
			v.type = SU_STRING;
			v.obj.gc_object = string_from_cache(s, constant->obj.str->str, constant->obj.str->size);
			break;
		case CNUMBER:
			v.type = SU_NUMBER;
			v.obj.num = constant->obj.num;
			break;
		case CTRUE:
			v.type = SU_BOOLEAN;
			v.obj.b = SU_TRUE;
			break;
		case CFALSE:
			v.type = SU_BOOLEAN;
			v.obj.b = SU_FALSE;
			break;
		case CNIL:
			v.type = SU_NIL;
			break;
		default:
			assert(0);
	}
	return v;
}

#define READ(p, n) { if (buffer_read(s, buffer, (p), (n))) goto error; }

static const_string_t *read_string(su_state *s, reader_buffer_t *buffer) {
	const_string_t *str;
	unsigned size;

	if (buffer_read(s, buffer, &size, sizeof(unsigned)))
		return NULL;

	str = su_allocate(s, NULL, sizeof(unsigned) + size + 1);
	if (buffer_read(s, buffer, str->str, size))
		return NULL;

	str->size = size;
	str->str[size] = '\0';
	return str;
}

int read_prototype(su_state *s, reader_buffer_t *buffer, prototype_t *prot) {
	unsigned i;
	memset(prot, 0, sizeof(prototype_t));

	assert(sizeof(unsigned) == 4);
	assert(sizeof(instruction_t) == 4);

	READ(&prot->num_inst, sizeof(unsigned));
	prot->inst = su_allocate(s, NULL, sizeof(instruction_t) * prot->num_inst);
	for (i = 0; i < prot->num_inst; i++)
		READ(&prot->inst[i], sizeof(instruction_t));

	READ(&prot->num_const, sizeof(unsigned));
	prot->constants = su_allocate(s, NULL, sizeof(const_t) * prot->num_const);
	for (i = 0; i < prot->num_const; i++) {
		READ(&prot->constants[i].id, sizeof(char));
		switch (prot->constants[i].id) {
			case CSTRING:
				prot->constants[i].obj.str = read_string(s, buffer);
				if (!prot->constants[i].obj.str)
					goto error;
				break;
			case CNUMBER:
				READ(&prot->constants[i].obj.num, sizeof(double));
				break;
			case CTRUE:
			case CFALSE:
			case CNIL:
				break;
			default:
				assert(0);
		}
	}

	READ(&prot->num_ups, sizeof(unsigned));
	prot->upvalues = su_allocate(s, NULL, sizeof(upvalue_t) * prot->num_ups);
	for (i = 0; i < prot->num_ups; i++)
		READ(&prot->upvalues[i], sizeof(upvalue_t));

	READ(&prot->num_prot, sizeof(unsigned));
	prot->prot = su_allocate(s, NULL, sizeof(prototype_t) * prot->num_prot);
	for (i = 0; i < prot->num_prot; i++) {
		if (read_prototype(s, buffer, &prot->prot[i]))
			goto error;
	}

	prot->name = read_string(s, buffer);
	if (!prot->name)
		goto error;

	READ(&prot->num_lineinf, sizeof(unsigned));
	prot->lineinf = su_allocate(s, NULL, sizeof(int) * prot->num_lineinf);
	for (i = 0; i < prot->num_lineinf; i++)
		READ(&prot->lineinf[i], sizeof(unsigned));

	return 0;

error:
	/* TODO: Deleate the rest of the objects. */
	return -1;
}

#undef READ

void lambda(su_state *s, prototype_t *prot, int narg) {
	unsigned i, tmp;
	value_t v;
	function_t *func = su_allocate(s, NULL, sizeof(function_t));

	func->narg = narg;
	func->prot = prot;
	func->num_const = prot->num_const;
	func->num_ups = prot->num_ups;
	func->constants = su_allocate(s, NULL, sizeof(value_t) * prot->num_const);
	func->upvalues = su_allocate(s, NULL, sizeof(value_t) * prot->num_ups);

	for (i = 0; i < func->num_const; i++)
		func->constants[i] = create_value(s, &prot->constants[i]);

	for (i = 0; i < func->num_ups; i++) {
		tmp = s->frame_top - prot->upvalues[i].lv;
		tmp = s->frames[tmp].stack_top + prot->upvalues[i].idx + 1;
		func->upvalues[i] = s->stack[tmp];
	}

	gc_insert_object(s, (gc_t*)func, SU_FUNCTION);
	v.type = SU_FUNCTION;
	v.obj.func = func;
	push_value(s, &v);
}

int su_load(su_state *s, su_reader reader, void *data) {
	prototype_t *prot = su_allocate(s, NULL, sizeof(prototype_t));
	reader_buffer_t *buffer = buffer_open(s, reader, data);

	if (verify_header(s, buffer)) {
		buffer_close(s, buffer);
		return -1;
	}

	if (read_prototype(s, buffer, prot)) {
		buffer_close(s, buffer);
		return -1;
	}

	buffer_close(s, buffer);
	gc_insert_object(s, &prot->gc, PROTOTYPE);
	lambda(s, prot, -1);
	return 0;
}

static void check_args(su_state *s, va_list arg, int start, int num) {
	int i;
	su_object_type_t a, b;
	for (i = start; num; i--, num--) {
		a = su_type(s, -i);
		b = va_arg(arg, su_object_type_t);
		if (b != SU_NIL)
			su_assert(s, a == b, "Expected argument %i to be of type '%s', but it is of type '%s'.", start - i, type_name(b), type_name(a));
	}
}

void su_check_num_arguments(su_state *s, int num) {
	su_assert(s, num == s->narg, "Bad number of arguments to function. Expected %i but got %i.", num, s->narg);
}

void su_check_arguments(su_state *s, int num, ...) {
	va_list arg;
	va_start(arg, num);

	if (s->narg != num) {
		if (num < 0) {
			num *= -1;
			if (num > s->narg)
				su_error(s, "To few arguments passed to function. Expected at least %i but got %i.", num, s->narg);
			check_args(s, arg, s->narg, num);
		} else {
			su_check_num_arguments(s, num);
		}
	} else {
		check_args(s, arg, num, num);
	}

	va_end(arg);
}

static void global_error(su_state *s, const char *msg, value_t *constant) {
	assert(constant->type == SU_STRING);
	fprintf(s->fstderr, "%s: %s\n", msg, ((string_t*)constant->obj.gc_object)->str);
	su_error(s, NULL);
}

static void vm_loop(su_state *s, function_t *func) {
	value_t tmpv, tmpv2;
	instruction_t inst;
	int tmp, narg, i, j, k;
	const char *tmpcs;
	su_debug_data dbg;

	s->frame = FRAME();
	s->prot = func->prot;

	#define ARITH_OP(op) \
		su_check_type(s, -2, SU_NUMBER); \
		su_check_type(s, -1, SU_NUMBER); \
		STK(-2)->obj.num = STK(-2)->obj.num op STK(-1)->obj.num; \
		su_pop(s, 1); \
		break;

	#define LOG_OP(op) \
		su_check_type(s, -2, SU_NUMBER); \
		su_check_type(s, -1, SU_NUMBER); \
		STK(-2)->type = SU_BOOLEAN; \
		STK(-2)->obj.b = STK(-2)->obj.num op STK(-1)->obj.num; \
		su_pop(s, 1); \
		break;

	for (s->pc = 0; s->pc < s->prot->num_inst; s->pc++) {
		tmp = s->interrupt | atomic_get(&s->msi->interrupt);
		if (tmp) {
			if ((tmp & ISCOLLECT) == ISCOLLECT) {
				su_thread_indisposable(s);
				su_thread_disposable(s);
			}
			if ((tmp & IGC) == IGC) {
				unmask_thread_interrupt(s, IGC);
				gc_trace(s);
			}
			if ((tmp & IBREAK) == IBREAK) {
				unmask_thread_interrupt(s, IBREAK);
				dbg.file = s->prot->name->str;
				dbg.line = s->prot->lineinf[s->pc];
				s->debug_cb(s, &dbg, s->debug_cb_data);
			}
		}
		inst = s->prot->inst[s->pc];
		switch (inst.id) {
			case OP_PUSH:
				push_value(s, &func->constants[inst.a]);
				break;
			case OP_POP:
				su_pop(s, inst.a);
				break;
			case OP_ADD: ARITH_OP(+)
			case OP_SUB: ARITH_OP(-)
			case OP_MUL: ARITH_OP(*)
			case OP_DIV:
				su_check_type(s, -2, SU_NUMBER);
				su_check_type(s, -1, SU_NUMBER);
				su_assert(s, STK(-1)->obj.num != 0.0, "Division by zero!");
				STK(-2)->obj.num = STK(-2)->obj.num / STK(-1)->obj.num;
				su_pop(s, 1);
				break;
			case OP_MOD:
				su_check_type(s, -2, SU_NUMBER);
				su_check_type(s, -1, SU_NUMBER);
				STK(-2)->obj.num = (double)((int)STK(-2)->obj.num % (int)STK(-1)->obj.num);
				su_pop(s, 1);
				break;
			case OP_POW:
				su_check_type(s, -2, SU_NUMBER);
				su_check_type(s, -1, SU_NUMBER);
				STK(-2)->obj.num = pow(STK(-2)->obj.num, STK(-1)->obj.num);
				su_pop(s, 1);
				break;
			case OP_UNM:
				su_check_type(s, -1, SU_NUMBER);
				STK(-1)->obj.num = -STK(-1)->obj.num;
				break;
			case OP_EQ:
				STK(-2)->obj.b = value_eq(STK(-2), STK(-1));
				STK(-2)->type = SU_BOOLEAN;
				su_pop(s, 1);
				break;
			case OP_LESS: LOG_OP(<);
			case OP_LEQUAL: LOG_OP(<=);
			case OP_NOT:
				if (STK(-1)->type == SU_BOOLEAN) {
					STK(-1)->obj.b = !STK(-1)->obj.b;
				} else {
					STK(-1)->obj.b = (STK(-1)->type == SU_NIL) ? 1 : 0;
					STK(-1)->type = SU_BOOLEAN;
				}
				break;
			case OP_AND:
				tmp = STK(-2)->type != SU_NIL && (STK(-2)->type != SU_BOOLEAN || STK(-2)->obj.b);
				if (tmp && STK(-1)->type != SU_NIL && (STK(-1)->type != SU_BOOLEAN || STK(-1)->obj.b)) {
					s->stack[s->stack_top - 2] = *STK(-1);
				} else {
					STK(-2)->obj.b = 0;
					STK(-2)->type = SU_BOOLEAN;
				}
				su_pop(s, 1);
				break;
			case OP_OR:
				if (STK(-2)->type != SU_NIL && (STK(-2)->type != SU_BOOLEAN || STK(-2)->obj.b)) {
					/* return -2 */
				} else if (STK(-1)->type != SU_NIL && (STK(-1)->type != SU_BOOLEAN || STK(-1)->obj.b)) {
					s->stack[s->stack_top - 2] = *STK(-1);
				} else {
					STK(-2)->obj.b = 0;
					STK(-2)->type = SU_BOOLEAN;
				}
				su_pop(s, 1);
				break;
			case OP_TEST:
				if (STK(-1)->type != SU_NIL && (STK(-1)->type != SU_BOOLEAN || STK(-1)->obj.b))
					s->pc = inst.b - 1;
				su_pop(s, 1);
				break;
			case OP_FOR:
				if (STK(-2)->type == SU_NIL) {
					su_swap(s, -2, -1);
					s->stack_top--;
					s->pc = inst.b - 1;
				} else {
					s->stack_top--;
					su_check_type(s, -1, SU_SEQ);
					su_rest(s, -1);
					su_swap(s, -2, -1);
					su_first(s, -1);
					su_swap(s, -2, -1);
					s->stack_top--;
				}
				break;
			case OP_JMP:
				s->pc = inst.b - 1;
				break;
			case OP_RETURN:
				s->pc = s->frame->ret_addr - 1;
				s->prot = s->frame->func->prot;
				func = s->frame->func;

				s->stack[s->frame->stack_top] = *STK(-1);
				s->stack_top = s->frame->stack_top + 1;
				s->frame_top--;
				s->frame = FRAME();
				break;
			case OP_TCALL:
				s->pc = s->frame->ret_addr - 1;
				s->prot = s->frame->func->prot;
				func = s->frame->func;

				memmove(&s->stack[s->frame->stack_top], &s->stack[s->stack_top - (inst.a + 1)], sizeof(value_t) * (inst.a + 1));
				s->stack_top = s->frame->stack_top + inst.a + 1;
				s->frame_top--;
				s->frame = FRAME();

				/* Do a normal call. */
			case OP_CALL:
				tmp = s->stack_top - inst.a - 1;
				switch (s->stack[tmp].type) {
					case SU_FUNCTION:
						s->frame = &s->frames[s->frame_top++];
						assert(s->frame_top <= MAX_CALLS);
						s->frame->ret_addr = s->pc + 1;
						s->frame->func = func;
						s->frame->stack_top = tmp;

						func = s->stack[tmp].obj.func;
						if (func->narg < 0)
							su_vector(s, inst.a);
						else if (func->narg != inst.a)
							su_error(s, "Bad number of arguments to function! Expected %i, but got %i.", (int)func->narg, (int)inst.a);

						s->prot = func->prot;
						s->pc = -1;
						break;
					case SU_NATIVEFUNC:
						narg = s->narg;
						s->narg = inst.a;
						if (s->stack[tmp].obj.nfunc(s, inst.a)) {
							s->stack[tmp] = *STK(-1);
						} else {
							s->stack[tmp].type = SU_NIL;
						}
						s->stack_top = tmp + 1;
						s->narg = narg;
						break;
					case SU_VECTOR:
						if (inst.a == 1) {
							su_check_type(s, -1, SU_NUMBER);
							tmpv = vector_index(s, s->stack[tmp].obj.vec, su_tointeger(s, -1));
							su_pop(s, 2);
							push_value(s, &tmpv);
						} else {
							for (i = -inst.a, j = 0; i; i++, j++) {
								su_check_type(s, i - j, SU_NUMBER);
								tmpv = vector_index(s, s->stack[tmp].obj.vec, su_tointeger(s, i - j));
								push_value(s, &tmpv);
							}
							su_vector(s, inst.a);
							s->stack[tmp] = s->stack[s->stack_top - 1];
							s->stack_top -= inst.a + 1;
						}
						break;
					case SU_MAP:
						if (inst.a == 1) {
							tmpv2 = *STK(-1);
							tmpv = map_get(s, s->stack[tmp].obj.m, &tmpv2, hash_value(&tmpv2));
							su_assert(s, tmpv.type != SU_INV, "No value with key: %s", stringify(s, &tmpv2));
							su_pop(s, 2);
							push_value(s, &tmpv);
						} else {
							for (i = -inst.a, j = 0; i; i++, j += 2) {
								tmpv2 = *STK(i - j);
								push_value(s, &tmpv2);
								tmpv = map_get(s, s->stack[tmp].obj.m, &tmpv2, hash_value(&tmpv2));
								su_assert(s, tmpv.type != SU_INV, "No value with key: %s", stringify(s, &tmpv2));
								push_value(s, &tmpv);
							}
							su_map(s, inst.a);
							s->stack[tmp] = s->stack[s->stack_top - 1];
							s->stack_top -= inst.a + 1;
						}
						break;
					case SU_STRING:
						if (inst.a == 1) {
							su_check_type(s, -1, SU_NUMBER);
							j = su_tointeger(s, -1);
							su_assert(s, j < s->stack[tmp].obj.str->size, "Out of range!");
							s->scratch_pad[0] = s->stack[tmp].obj.str->str[j];
							su_pop(s, 2);
							su_pushbytes(s, s->scratch_pad, 1);
						} else {
							k = 0;
							for (i = -inst.a; i; i++) {
								su_check_type(s, i, SU_NUMBER);
								j = su_tointeger(s, i);
								su_assert(s, j < s->stack[tmp].obj.str->size, "Out of range!");
								s->scratch_pad[k++] = s->stack[tmp].obj.str->str[j];
								assert(k < SU_SCRATCHPAD_SIZE);
							}
							su_pushbytes(s, s->scratch_pad, k);
							s->stack[tmp] = s->stack[s->stack_top - 1];
							s->stack_top -= inst.a + 1;
						}
						break;
					case SU_NATIVEDATA:
						tmpv = s->stack[tmp];
						if (tmpv.obj.data->vt && tmpv.obj.data->vt->call) {
							narg = s->narg;
							s->narg = inst.a;
							if (tmpv.obj.data->vt->call(s, (void*)tmpv.obj.data->data, inst.a))
								s->stack[tmp] = *STK(-1);
							else
								s->stack[tmp].type = SU_NIL;
							s->stack_top = tmp + 1;
							s->narg = narg;
							break;
						}
					default:
						if (inst.a == 1 && isseq(s, &s->stack[tmp])) {
							su_check_type(s, -1, SU_STRING);
							tmpcs = su_tostring(s, -1, NULL);
							if (!strcmp(tmpcs, "first")) {
								s->stack[(--s->stack_top) - 1] = seq_first(s, STK(-1)->obj.q);
								break;
							} else if (!strcmp(tmpcs, "rest")) {
								s->stack[(--s->stack_top) - 1] = seq_rest(s, STK(-1)->obj.q);
								break;
							}
						}
						su_error(s, "Can't apply '%s'.", type_name(s->stack[tmp].type));
				}
				break;
			case OP_LAMBDA:
				assert(inst.a < s->prot->num_prot);
				lambda(s, &s->prot->prot[inst.a], inst.b);
				break;
			case OP_GETGLOBAL:
				tmpv = func->constants[inst.a];
				su_assert(s, tmpv.type == SU_STRING, "Global key must be a string!");
				tmpv = map_get(s, unref_local(s, s->stack[SU_GLOBAL_INDEX].obj.loc).obj.m, &tmpv, hash_value(&tmpv));
				if (tmpv.type == SU_INV)
					global_error(s, "Undefined global variable", &func->constants[inst.a]);
				push_value(s, &tmpv);
				break;
			case OP_SETGLOBAL:
				tmpv = func->constants[inst.a];
				su_assert(s, tmpv.type == SU_STRING, "Global key must be a string!");
				i = hash_value(&tmpv);
				tmpv2 = unref_local(s, s->stack[SU_GLOBAL_INDEX].obj.loc);
				tmpv = map_insert(s, tmpv2.obj.m, &tmpv, i, STK(-1));
				set_local(s, s->stack[SU_GLOBAL_INDEX].obj.loc, &tmpv);
				break;
			case OP_SHIFT:
				s->stack[s->stack_top - (inst.a + 1)] = *STK(-1);
				s->stack_top -= inst.a;
				break;
			case OP_LOAD:
				assert(FRAME()->stack_top + inst.a < s->stack_top);
				push_value(s, &s->stack[FRAME()->stack_top + inst.a]);
				break;
			case OP_LUP:
				assert(inst.a < func->num_ups);
				push_value(s, &func->upvalues[inst.a]);
				break;
			case OP_LCL:
				assert(inst.b < s->msi->num_c_lambdas);
				push_value(s, &s->msi->c_lambdas[inst.b]);
				break;
			default:
				assert(0);
		}

		#undef ARITH_OP
		#undef LOG_OP
	}
}

int su_num_threads(su_state *s) {
	return atomic_get(&s->msi->thread_count);
}

int su_num_cores(su_state *s) {
	return num_cores();
}

void su_thread_disposable(su_state *s) {
	if (atomic_get(&s->thread_indisposable)) {
		while ((atomic_get(&s->msi->interrupt) & ISCOLLECT) == ISCOLLECT)
			thread_sleep(0);
		atomic_set(&s->thread_indisposable, 0);
	}
}

void su_thread_indisposable(su_state *s) {
	atomic_set(&s->thread_indisposable, 1);
}

static void *thread_boot(su_state *s) {
	su_call(s, s->narg, 1);
	s->stack_top = 0;
	su_thread_indisposable(s);
	
	spin_lock(&s->msi->thread_pool_lock);
	atomic_set(&s->thread_finished, 1);
	atomic_add(&s->msi->thread_count, -1);
	spin_unlock(&s->msi->thread_pool_lock);
	return NULL;
}

static su_state *new_state(su_state *s) {
	int i;
	su_state *ns;
	for (i = 1; i < SU_OPT_MAX_THREADS; i++) {
		ns = &s->msi->threads[i];
		if (atomic_cas(&ns->thread_finished, 1, 0)) {
			memcpy(ns, s, sizeof(su_state));
			atomic_add(&s->msi->thread_count, 1);
			ns->tid = atomic_add(&s->msi->tid_count, 1);
			assert(ns->tid > 0);
			return ns;
		}
	}
	return NULL;
}

void su_fork(su_state *s, int narg) {
	value_t v;
	su_state *ns;
 
	narg++;
	v.type = SU_BOOLEAN;
	su_thread_indisposable(s);
	spin_lock(&s->msi->thread_pool_lock);
	su_thread_disposable(s);
	ns = new_state(s);
	
	if (!ns) {
		s->stack_top -= narg;
		v.obj.b = 0;
		push_value(s, &v);
		spin_unlock(&s->msi->thread_pool_lock);
		return;
	}
	
	ns->interrupt = 0x0;
	ns->narg = narg - 1;
	ns->pc = 0xffff;
	
	ns->string_builder = NULL;
	ns->errtop = ns->ferrtop = -1;
	
	ns->stack[SU_GLOBAL_INDEX] = s->stack[SU_GLOBAL_INDEX];
	
	su_assert(s, !thread_init(&thread_boot, (void*)ns), "Could not create thread!");
	s->stack_top -= narg;
	
	v.obj.b = 1;
	push_value(s, &v);
	
	spin_unlock(&s->msi->thread_pool_lock);
}

void su_call(su_state *s, int narg, int nret) {
	int pc, tmp, fret;
	prototype_t *prot;
	int top = s->stack_top - narg - 1;
	value_t *f = &s->stack[top];
	frame_t *frame = &s->frames[s->frame_top++];
	assert(s->frame_top <= MAX_CALLS);

	frame->ret_addr = 0xffff;
	frame->func = f->obj.func;
	frame->stack_top = top;

	pc = s->pc;
	prot = s->prot;
	tmp = s->narg;
	s->narg = narg;

	if (f->type == SU_FUNCTION) {
		if (f->obj.func->narg < 0) {
			su_vector(s, narg);
			s->narg = narg = 1;
		} else {
			su_assert(s, f->obj.func->narg == narg, "Bad number of argument to function!");
		}
		vm_loop(s, f->obj.func);
		if (nret == 0)
			su_pop(s, 1);
	} else if (f->type == SU_NATIVEFUNC) {
		fret = f->obj.nfunc(s, narg);
		if (nret > 0 && fret > 0) {
			s->stack[top] = *STK(-1);
			su_pop(s, narg);
		} else {
			s->stack_top = top;
			if (nret > 0)
				su_pushnil(s);
		}
		s->frame_top--;
	} else {
		assert(0);
	}

	s->narg = tmp;
	s->prot = prot;
	s->pc = pc;
}

FILE *su_stdout(su_state *s) { return s->fstdout; }
FILE *su_stdin(su_state *s) { return s->fstdin; }
FILE *su_stderr(su_state *s) { return s->fstderr; }
void su_set_stdout(su_state *s, FILE *fp) { s->fstdout = fp; }
void su_set_stdin(su_state *s, FILE *fp) { s->fstdin = fp; }
void su_set_stderr(su_state *s, FILE *fp) { s->fstderr = fp; }

static void *default_alloc(void *ptr, size_t size) {
	if (size) return realloc(ptr, size);
	free(ptr);
	return NULL;
}

su_state *su_init(su_alloc alloc) {
	int i;
	value_t v;
	su_state *s;
	su_alloc mf;
	main_state_internal_t *msi;
	
	assert(sizeof(value_t) <= SU_VALUE_SIZE);
	assert(sizeof(value_t) > SU_VALUE_DATA_SIZE);
	
	mf = alloc ? alloc : default_alloc;
	msi = (main_state_internal_t*)mf(NULL, sizeof(main_state_internal_t));
	s = &msi->threads[0];
	memset(msi, 0, sizeof(main_state_internal_t));
	
	for (i = 0; i < SU_OPT_MAX_THREADS; i++)
		msi->threads[i].thread_finished.value = 1;
	
	s->alloc = mf;
	s->msi = msi;
	s->main_state = s;

	msi->gc_state = GC_STATE_SWEEP;

	s->fstdin = stdin;
	s->fstdout = stdout;
	s->fstderr = stderr;
	s->errtop = s->ferrtop = -1;

	s->pc = 0xffff;
	s->msi->ref_counter = 0x1;
	s->msi->tid_count.value = 1;
	s->msi->thread_count.value = 1;
	
	v = init_globals(s);
	
	su_pushnil(s);					/* SU_NIL_INDEX */
	push_value(s, &v);				/* SU_GLOBAL_INDEX */
	su_map(s, 0);					/* SU_REGISTRY_INDEX */
	
	rw_barrier();
	return s;
}

void su_close(su_state *s) {
	int i;
	s->stack_top = 0;
	su_thread_indisposable(s);
	
	while (atomic_get(&s->msi->thread_count) > 1)
		thread_sleep(0);
	
	for (i = 0; i < SU_OPT_MAX_THREADS; i++) {
		su_state *thread = &s->msi->threads[i];
		memset(thread->string_cache, 0, sizeof(thread->string_cache));
		if (thread->string_builder) thread->alloc(s->string_builder, 0);
		
		if (thread->fstdin != stdin) fclose(thread->fstdin);
		if (thread->fstdout != stdout) fclose(thread->fstdout);
		if (thread->fstderr != stderr) fclose(thread->fstderr);
	}
	
	while (atomic_get(&s->msi->num_objects) > 1)
		su_gc(s);
	gc_free_object(s, s->msi->gc_root);

	if (s->msi->c_lambdas)
		s->alloc(s->msi->c_lambdas, 0);
	s->alloc(s->msi, 0);
}
