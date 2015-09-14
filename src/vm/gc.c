/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "saurus.h"
#include "seq.h"
#include "ref.h"
#include "gc.h"

#include <assert.h>

static void free_prot(su_state *s, prototype_t *prot);

static void add_to_gray(su_state *s, gc_t *obj) {
	main_state_internal_t *msi = s->msi;
	if (obj->flags != GC_FLAG_WHITE)
		return;
	assert(msi->gc_gray_size <= GC_GRAY_SIZE);
	obj->flags = GC_FLAG_GRAY;
	msi->gc_gray[msi->gc_gray_size++] = obj;
}

static gc_t *get_gc_object(value_t *v) {
	switch (v->type) {
		case SU_INV:
		case SU_NIL:
		case SU_BOOLEAN:
		case SU_NUMBER:
		case SU_NATIVEFUNC:
		case SU_NATIVEPTR:
			return NULL;
	}
	assert((int)v->type == (int)v->obj.gc_object->type);
	return v->obj.gc_object;
}

static void gray_value(su_state *s, value_t *v) {
	gc_t *obj = get_gc_object(v);
	if (obj)
		add_to_gray(s, obj);
}

static void gray_vector(su_state *s, gc_t *obj) {
	vector_t *v = (vector_t*)obj;
	add_to_gray(s, (gc_t*)v->root);
	add_to_gray(s, (gc_t*)v->tail);
}

static void gray_vector_node(su_state *s, gc_t *obj) {
	int i;
	vector_node_t *node = (vector_node_t*)obj;
	for (i = 0; i < (int)node->len; i++)
		gray_value(s, &node->data[i]);
}

static void gray_tree_seq(su_state *s, gc_t *obj) {
	int i;
	tree_seq_t *ts = (tree_seq_t*)obj;
	for (i = 0; i < ts->nlinks; i++)
		add_to_gray(s, (gc_t*)ts->links[i].n);
}

static void gray_function(su_state *s, gc_t *obj) {
	int i;
	function_t *func = (function_t*)obj;
	if (func->prot->gc.type != SU_INV)
		add_to_gray(s, &func->prot->gc);
	for (i = 0; i < (int)func->num_const; i++)
		gray_value(s, &func->constants[i]);
	for (i = 0; i < (int)func->num_ups; i++)
		gray_value(s, &func->upvalues[i]);
}

static void free_prot(su_state *s, prototype_t *prot) {
	int i;
	su_allocate(s, prot->inst, 0);
	su_allocate(s, prot->lineinf, 0);
	su_allocate(s, prot->upvalues, 0);

	for (i = 0; i < prot->num_const; i++) {
		if (prot->constants[i].id == CSTRING)
			su_allocate(s, prot->constants[i].obj.str, 0);
	}
	su_allocate(s, prot->constants, 0);
	su_allocate(s, prot->name, 0);

	for (i = 0; i < prot->num_prot; i++)
		free_prot(s, &prot->prot[i]);
	su_allocate(s, prot->prot, 0);
}

static void trace_cb(su_state *s, su_value_t *v) {
	gc_t *tmp;
	value_t *val = (value_t*)v;
	tmp = get_gc_object(val);
	if (tmp)
		add_to_gray(s, tmp);
}

static void mark(su_state *s) {
	gc_t *obj;
	map_t *m;
	local_t *loc;
	native_data_t *nd;
	main_state_internal_t *msi = s->msi;
	assert(msi->gc_state == GC_STATE_MARK);
	
	mark_object: if (msi->gc_gray_size) {
		obj = msi->gc_gray[--msi->gc_gray_size];
		if (obj->flags == GC_FLAG_BLACK)
			goto mark_object;
		
		obj->flags = GC_FLAG_BLACK;
		switch (obj->type) {
			case SU_NATIVEDATA:
				nd = (native_data_t*)obj;
				if (nd->vt && nd->vt->trace_callback)
					nd->vt->trace_callback(s, (void*)nd->data, &trace_cb);
				break;
			case SU_LOCAL:
				loc = (local_t*)obj;
				if (loc->tid == s->tid)
					gray_value(s, &loc->v);
				else
					gc_gray_mutable(s, obj);
				break;
			case SU_GLOBAL:
				m = (map_t*)atomic_get_ptr(&((global_t*)obj)->value);
				if (m)
					add_to_gray(s, &m->gc);
				break;
			case SU_VECTOR:
				gray_vector(s, obj);
				break;
			case VECTOR_NODE:
				gray_vector_node(s, obj);
				break;
			case SU_FUNCTION:
				gray_function(s, obj);
				break;
			case SU_MAP:
				add_to_gray(s, &((map_t*)obj)->root->gc);
				break;
			case MAP_COLLISION:
				add_to_gray(s, &((node_collision_t*)obj)->leaves->gc);
				break;
			case MAP_FULL:
				add_to_gray(s, &((node_full_t*)obj)->nodes->gc);
				break;
			case MAP_IDX:
				add_to_gray(s, &((node_idx_t*)obj)->nodes->gc);
				break;
			case MAP_LEAF:
				gray_value(s, &((node_leaf_t*)obj)->key);
				gray_value(s, &((node_leaf_t*)obj)->val);
				break;
			case CELL_SEQ:
				gray_value(s, &((cell_seq_t*)obj)->first);
				gray_value(s, &((cell_seq_t*)obj)->rest);
				break;
			case TREE_SEQ:
				gray_tree_seq(s, obj);
				break;
			case IT_SEQ:
				add_to_gray(s, ((it_seq_t*)obj)->obj);
				break;
			case LAZY_SEQ:
				gray_value(s, &((lazy_seq_t*)obj)->f);
				gray_value(s, &((lazy_seq_t*)obj)->d);
				break;
		}
	} else {
		msi->gc_state = GC_STATE_SWEEP;
	}
}

static void collect_stack(su_state *s) {
	int i, j;
	for (i = 0; i < s->stack_top; i++)
		gray_value(s, &s->stack[i]);
	for (i = 0; i < 16; i++) {
		for (j = 0; j < STRING_CACHE_SIZE; j++) {
			if (s->string_cache[i][j].str)
				add_to_gray(s, &s->string_cache[i][j].str->gc);
		}
	}
}

static void scan_mutated(su_state *s) {
	int i;
	gc_t *obj;
	map_t *m;
	main_state_internal_t *msi = s->msi;
	
	for (i = 0; i < SU_OPT_MAX_THREADS; i++) {
		su_state *thread = &msi->threads[i];
		collect_stack(thread);
		while (thread->gray_size) {
			obj = thread->gray[--thread->gray_size];
			if (obj->type == SU_LOCAL) {
				gray_value(s, &((local_t*)obj)->v);
			} else {
				assert(obj->type == SU_GLOBAL);
				m = ((global_t*)obj)->value.value;
				if (m)
					add_to_gray(s, &m->gc);
			}
			obj->usr &= ~GC_USR_GRAY;
		}
	}
	
	msi->gc_state = GC_STATE_MARK;
	while (msi->gc_state != GC_STATE_SWEEP)
		mark(s);
	assert(!msi->gc_gray_size);
}

static void collect(su_state *s) {
	gc_t *tmp;
	gc_t *prev = NULL;
	main_state_internal_t *msi = s->msi;
	gc_t *obj = msi->gc_root;
	int num_freed = 0;
	int alive;
	
	scan_mutated(s);
	
	obj->flags = GC_FLAG_WHITE;
	while (obj) {
		if (obj->flags == GC_FLAG_WHITE && prev) {
			prev->next = obj->next;
			tmp = obj;
			obj = obj->next;
			num_freed++;
			gc_free_object(s, tmp);
		} else {
			obj->flags = GC_FLAG_WHITE;
			prev = obj;
			obj = obj->next;
		}
	}
	
	alive = msi->gc_num_objects - num_freed;
	atomic_set(&msi->gc_throttle, alive + (alive / SU_OPT_GC_OVERHEAD_DIVISOR) * atomic_get(&msi->thread_count));
	atomic_add(&msi->num_objects, -num_freed);
}

static void sweep(su_state *s) {
	int i;
	main_state_internal_t *msi = s->msi;
	assert(msi->gc_state == GC_STATE_SWEEP);
	
	spin_lock(&msi->thread_pool_lock);
	interrupt(s, ISCOLLECT);
	
	s->thread_indisposable.value = 1;
	for (i = 0; i < SU_OPT_MAX_THREADS; i++) {
		su_state *thread = &msi->threads[i];
		while (!atomic_get(&thread->thread_finished) && !atomic_get(&thread->thread_indisposable));
	}
	
	collect(s);
	
	for (i = 0; i < SU_OPT_MAX_THREADS; i++) {
		su_state *thread = &msi->threads[i];
		if (!atomic_get(&thread->thread_finished))
			collect_stack(thread);
	}
	
	msi->gc_num_objects = atomic_get(&msi->num_objects);
	s->thread_indisposable.value = 0;
	
	unmask_interrupt(s, ISCOLLECT);
	spin_unlock(&msi->thread_pool_lock);
	
	msi->gc_state = GC_STATE_MARK;
}

void gc_free_object(su_state *s, gc_t *obj) {
	native_data_t *nd;
	function_t *func;
	
	if (obj->type == SU_FUNCTION) {
		func = (function_t*)obj;
		su_allocate(s, func->constants, 0);
		su_allocate(s, func->upvalues, 0);
	} else if (obj->type == PROTOTYPE) {
		free_prot(s, (prototype_t*)obj);
	} else if (obj->type == SU_NATIVEDATA) {
		nd = (native_data_t*)obj;
		if (nd->vt && nd->vt->gc_callback)
			nd->vt->gc_callback(s, (void*)nd->data);
	}
	su_allocate(s, obj, 0);
}

void gc_gray_mutable(su_state *s, gc_t *obj) {
	assert(obj->type == SU_LOCAL || obj->type == SU_GLOBAL);
	if ((obj->usr & GC_USR_GRAY) == GC_USR_GRAY)
		return;
	
	assert(s->gray_size <= GC_GRAY_SIZE);
	obj->usr |= GC_USR_GRAY;
	s->gray[s->gray_size++] = obj;
}

void gc_trace(su_state *s) {
	main_state_internal_t *msi = s->msi;
	if (atomic_get(&msi->num_objects) > atomic_get(&msi->gc_throttle)) {
		if (spin_try_lock(&msi->gc_lock)) {
			if (msi->gc_state == GC_STATE_MARK)
				mark(s);
			else
				sweep(s);
			spin_unlock(&msi->gc_lock);
		}
	}
}

void su_gc(su_state *s) {
	main_state_internal_t *msi = s->msi;
	
	su_thread_indisposable(s);
	while (!spin_try_lock(&msi->gc_lock));
		thread_sleep(0);
	su_thread_disposable(s);
	
	if (msi->gc_state == GC_STATE_SWEEP) {
		sweep(s);
	} else {
		while (msi->gc_state == GC_STATE_MARK)
			mark(s);
		sweep(s);
	}
	
	assert(msi->gc_state == GC_STATE_MARK);
	spin_unlock(&msi->gc_lock);
}
