/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* The vector and hashmap implementation is based on Rich Hickey's solution for Clojure. */

#include "seq.h"
#include "intern.h"

#include <string.h>
#include <assert.h>

#define TREE_MAX_DEPTH 7

static value_t cell_first(su_state *s, seq_t *q) {
	return ((cell_seq_t*)q)->first;
}

static value_t cell_rest(su_state *s, seq_t *q) {
	return ((cell_seq_t*)q)->rest;
}

const seq_class_t cell_vt = {&cell_first, &cell_rest};

value_t cell_create_array(su_state *s, value_t *array, int num) {
	int i;
	value_t tmp;
	cell_seq_t *cell = (cell_seq_t*)su_allocate(s, NULL, sizeof(cell_seq_t) * num);
	tmp.type = SU_NIL;
	
	for (i = num - 1; i >= 0; i--) {
		cell->first = array[i];
		cell->rest = tmp;
		cell->q.vt = &cell_vt;
		tmp.type = CELL_SEQ;
		tmp.obj.gc_object = gc_insert_object(s, &cell->q.gc, CELL_SEQ);
		cell++;
	}
	
	return tmp;
}

value_t cell_create(su_state *s, value_t *first, value_t *rest) {
	value_t v;
	cell_seq_t *cell = (cell_seq_t*)su_allocate(s, NULL, sizeof(cell_seq_t));
	cell->first = *first;
	cell->rest = *rest;
	cell->q.vt = &cell_vt;
	
	v.type = CELL_SEQ;
	v.obj.gc_object = gc_insert_object(s, &cell->q.gc, CELL_SEQ);
	return v;
}

static value_t it_next(su_state *s, it_seq_t *iq) {
	value_t v;
	it_seq_t *it = (it_seq_t*)su_allocate(s, NULL, sizeof(it_seq_t));
	it->idx = iq->idx + iq->step;
	it->step = iq->step;
	it->obj = iq->obj;
	it->q.vt = iq->q.vt;
	
	v.type = IT_SEQ;
	v.obj.gc_object = gc_insert_object(s, &it->q.gc, IT_SEQ);
	return v;
}

static value_t it_string_first(su_state *s, seq_t *q) {
	value_t v;
	char buffer[2] = {0, 0};
	it_seq_t *iq = (it_seq_t*)q;
	buffer[0] = ((string_t*)iq->obj)->str[iq->idx];
	
	v.type = SU_STRING;
	v.obj.gc_object = string_from_cache(s, buffer, 2);
	return v;
}

static value_t it_string_rest(su_state *s, seq_t *q) {
	value_t v;
	it_seq_t *iq = (it_seq_t*)q;
	
	if (iq->step > 0 ? (iq->idx + 2 == ((string_t*)iq->obj)->size) : !iq->idx) {
		v.type = SU_NIL;
		return v;
	} 
	
	return it_next(s, iq);
}

static value_t it_vector_first(su_state *s, seq_t *q) {
	it_seq_t *iq = (it_seq_t*)q;
	return vector_index(s, (vector_t*)iq->obj, iq->idx);
}

static value_t it_vector_rest(su_state *s, seq_t *q) {
	value_t v;
	it_seq_t *iq = (it_seq_t*)q;
	
	if (iq->step > 0 ? (iq->idx + 1 == ((vector_t*)iq->obj)->cnt) : !iq->idx) {
		v.type = SU_NIL;
		return v;
	}
	
	return it_next(s, iq);
}

const seq_class_t it_vt = {&it_vector_first, &it_vector_rest};

value_t it_create_vector(su_state *s, vector_t *vec, int reverse) {
	value_t v;
	it_seq_t *it;
	
	if (vec->cnt == 0) {
		v.type = SU_NIL;
		return v;
	}
	
	it = (it_seq_t*)su_allocate(s, NULL, sizeof(it_seq_t));
	if (reverse) {
		it->idx = vec->cnt - 1;
		it->step = -1;
	} else {
		it->idx = 0;
		it->step = 1;
	}
	
	it->obj = (gc_t*)vec;
	it->q.vt = &it_vt;
	
	v.type = IT_SEQ;
	v.obj.gc_object = gc_insert_object(s, &it->q.gc, IT_SEQ);
	return v;
}

const seq_class_t str_vt = {&it_string_first, &it_string_rest};

value_t it_create_string(su_state *s, string_t *str, int reverse) {
	value_t v;
	it_seq_t *it;
	
	if (str->size <= 1) {
		v.type = SU_NIL;
		return v;
	}
	
	it = (it_seq_t*)su_allocate(s, NULL, sizeof(it_seq_t));
	if (reverse) {
		it->idx = str->size - 2;
		it->step = -1;
	} else {
		it->idx = 0;
		it->step = 1;
	}
	
	it->obj = (gc_t*)str;
	it->q.vt = &str_vt;
	
	v.type = IT_SEQ;
	v.obj.gc_object = gc_insert_object(s, &it->q.gc, IT_SEQ);
	return v;
}

static value_t range_first(su_state *s, seq_t *q) {
	value_t v;
	range_seq_t *r = (range_seq_t*)q;
	v.type = SU_NUMBER;
	v.obj.num = (double)r->cnt;
	return v;
}

static value_t range_rest(su_state *s, seq_t *q) {
	value_t v;
	range_seq_t *tmp;
	range_seq_t *r = (range_seq_t*)q;
	
	if (r->cnt + r->step == r->end) {
		v.type = SU_NIL;
		return v;
	} else {
		tmp = (range_seq_t*)su_allocate(s, NULL, sizeof(range_seq_t));
		memcpy(tmp, r, sizeof(range_seq_t));
		tmp->cnt += tmp->step;
		v.type = RANGE_SEQ;
		v.obj.gc_object = gc_insert_object(s, &tmp->q.gc, RANGE_SEQ);
		return v;
	}
}

const seq_class_t range_vt = {&range_first, &range_rest};

value_t range_create(su_state *s, int from, int to) {
	value_t v;
	range_seq_t *r;
	int range = to - from;
	
	r = (range_seq_t*)su_allocate(s, NULL, sizeof(range_seq_t));
	r->step = (range > 0) ? 1 : -1;
	r->end = to + r->step;
	r->cnt = from;
	
	r->q.vt = &range_vt;
	
	v.type = RANGE_SEQ;
	v.obj.gc_object = gc_insert_object(s, &r->q.gc, RANGE_SEQ);
	return v;
}

static value_t lazy_first(su_state *s, seq_t *q) {
	lazy_seq_t *r = (lazy_seq_t*)q;
	return r->d;
}

static value_t lazy_rest(su_state *s, seq_t *q) {
	value_t v;
	lazy_seq_t *tmp;
	lazy_seq_t *r = (lazy_seq_t*)q;
	push_value(s, &r->f);
	push_value(s, &r->d);
	su_call(s, 1, 1);
	
	if (STK(-1)->type == SU_NIL) {
		su_pop(s, 1);
		v.type = SU_NIL;
		return v;
	} else {
		tmp = (lazy_seq_t*)su_allocate(s, NULL, sizeof(lazy_seq_t));
		memcpy(tmp, r, sizeof(lazy_seq_t));
		
		tmp->d = *STK(-1);
		su_pop(s, 1);
		
		v.type = LAZY_SEQ;
		v.obj.gc_object = gc_insert_object(s, &tmp->q.gc, LAZY_SEQ);
		return v;
	}
}

const seq_class_t lazy_vt = {&lazy_first, &lazy_rest};

value_t lazy_create(su_state *s, value_t *f) {
	lazy_seq_t r;
	r.f = *f;
	r.d.type = SU_NIL;
	r.q.vt = &lazy_vt;
	return lazy_rest(s, (seq_t*)&r);
}

static value_t tree_first(su_state *s, seq_t *q) {
	tree_seq_t *ts = (tree_seq_t*)q;
	node_leaf_t *leaf = (node_leaf_t*)ts->links[ts->nlinks - 1].n;
	assert(leaf->n.gc.type == MAP_LEAF);
	return cell_create(s, &leaf->key, &leaf->val);
}

static node_leaf_t *tree_next(tree_link_t*,node_t*,int,int*,node_leaf_t**);
static node_leaf_t *tree_next(tree_link_t *path, node_t *n, int depth, int *out_depth, node_leaf_t **first_leaf) {
	int i;
	value_t *v;
	node_leaf_t *leaf;
	vector_node_t *vn;
	
	if (n->gc.type == MAP_LEAF) {
		if (*first_leaf) {
			*out_depth = depth + 1;
			path[depth].n = (node_t*)n;
			path[depth].idx = 0;
			return (node_leaf_t*)n;
		} else {
			*first_leaf = (node_leaf_t*)n;
			return NULL;
		}
	}
	
	vn = *(vector_node_t**)(n + 1);
	for (i = path[depth].idx; i < vn->len; i++) {
		v = &vn->data[i];
		leaf = tree_next(path, (node_t*)v->obj.map_node, depth + 1, out_depth, first_leaf);
		if (leaf) {
			path[depth].n = (node_t*)n;
			path[depth].idx = i;
			return leaf;
		}
	}
	return NULL;
}

static value_t build_tree_seq(su_state*,tree_link_t*,int);
static value_t tree_rest(su_state *s, seq_t *q) {
	int depth;
	value_t v;
	node_leaf_t *leaf = NULL;
	tree_link_t path[TREE_MAX_DEPTH];
	tree_seq_t *ts = (tree_seq_t*)q;
	
	assert(ts->nlinks <= TREE_MAX_DEPTH);
	memcpy(path, ts->links, sizeof(tree_link_t) * ts->nlinks);

	if (!tree_next(path, path[0].n, 0, &depth, &leaf)) {
		v.type = SU_NIL;
		return v;
	}
	
	return build_tree_seq(s, path, depth);
}

const seq_class_t tree_vt = {&tree_first, &tree_rest};

static value_t build_tree_seq(su_state *s, tree_link_t *path, int depth) {
	value_t v;
	tree_seq_t *ts = (tree_seq_t*)su_allocate(s, NULL, sizeof(tree_seq_t) + sizeof(tree_link_t) * (depth - 1));
	ts->nlinks = depth;
	ts->q.vt = &tree_vt;
	memcpy(ts->links, path, sizeof(tree_link_t) * depth);
	
	v.type = TREE_SEQ;
	v.obj.gc_object = gc_insert_object(s, &ts->q.gc, TREE_SEQ);
	return v;
}

value_t tree_create_map(su_state *s, map_t *m) {
	int i;
	value_t v;
	tree_link_t path[TREE_MAX_DEPTH];
	node_t *n, *ln;
	
	if (!m->cnt) {
		v.type = SU_NIL;
		return v;
	}
	
	n = m->root;
	for (i = 0;; i++) {
		ln = *(node_t**)(n + 1);
		path[i].n = n;
		path[i].idx = 0;
		if (n->gc.type == MAP_LEAF)
			break;
		n = ((vector_node_t*)ln)->data[0].obj.map_node;
	}

	return build_tree_seq(s, path, i + 1);
}

value_t seq_first(su_state *s, seq_t *q) {
	return q->vt->first(s, q);
}

value_t seq_rest(su_state *s, seq_t *q) {
	return q->vt->rest(s, q);
}

/* --------------------------------- Vector implementation --------------------------------- */

#define tailoff(v) ((v)->cnt - (v)->tail->len)

static vector_node_t *push_tail(su_state *s, int level, vector_node_t *arr, vector_node_t *tail_node, vector_node_t **expansion);
static vector_node_t *insert(su_state *s, int level, vector_node_t *arr, int i, value_t *val);
static vector_node_t *pop_tail(su_state *s, int shift, vector_node_t *arr, vector_node_t **ptail);

static vector_node_t *node_create_only(su_state *s, int len) {
	vector_node_t *node = (vector_node_t*)su_allocate(s, NULL, (sizeof(vector_node_t) + sizeof(value_t) * len) - sizeof(value_t));
	node->len = (unsigned char)len;
	gc_insert_object(s, (gc_t*)node, VECTOR_NODE);
	return node;
}

static vector_node_t *node_create1(su_state *s, value_t *v) {
	vector_node_t *node = node_create_only(s, 1);
	node->data[0] = *v;
	return node;
}

static vector_node_t *node_create2(su_state *s, value_t *v1, value_t *v2) {
	vector_node_t *node = node_create_only(s, 2);
	node->data[0] = *v1;
	node->data[1] = *v2;
	return node;
}

static vector_node_t *node_clone(su_state *s, vector_node_t *src) {
	vector_node_t *node = node_create_only(s, src->len);
	memcpy(node->data, src->data, sizeof(value_t) * src->len);
	return node;
}

int vector_length(vector_t *v) {
	return v->cnt;
}

value_t vector_cat(su_state *s, vector_t *a, vector_t *b) {
	int i;
	value_t v, tmp;
	v.type = SU_VECTOR;
	v.obj.vec = a;
	for (i = 0; i < b->cnt; i++) {
		tmp = vector_index(s, b, i);
		v = vector_push(s, v.obj.vec, &tmp);
	}
	return v;
}

value_t vector_index(su_state *s, vector_t *v, int i) {
	int level;
	vector_node_t *arr;
	if (i >= 0 && i < v->cnt) {
		if (i >= tailoff(v))
			return v->tail->data[i & 0x01f];
		arr = v->root;
		for (level = v->shift; level > 0; level -= 5)
			arr = arr->data[(i >> level) & 0x01f].obj.vec_node;
		return arr->data[i & 0x01f];
	}
	su_error(s, "Index is out of bounds: %i", i);
	return *v->root->data;
}

value_t vector_create(su_state *s, unsigned cnt, int shift, vector_node_t *root, vector_node_t *tail) {
	vector_t *vec;
	value_t v;
	v.type = SU_VECTOR;
	v.obj.gc_object = (gc_t*)su_allocate(s, NULL, sizeof(vector_t));
	gc_insert_object(s, v.obj.gc_object, SU_VECTOR);
	
	assert(root);
	assert(tail);
	
	vec = (vector_t*)v.obj.gc_object;
	vec->cnt = cnt;
	vec->shift = shift;
	vec->root = root;
	vec->tail = tail;
	return v;
}

value_t vector_create_empty(su_state *s) {
	return vector_create(s, 0, 5, node_create_only(s, 0), node_create_only(s, 0));
}

value_t vector_push(su_state *s, vector_t *vec, value_t *val) {
	value_t expansion_value, tmp;
	vector_node_t *expansion = NULL, *new_root;
	int new_shift = vec->shift;
	
	if (vec->tail->len < 32) {
		vector_node_t *new_tail = node_create_only(s, vec->tail->len + 1);
		memcpy(new_tail->data, vec->tail->data, sizeof(value_t) * vec->tail->len);
		new_tail->data[vec->tail->len] = *val;
		return vector_create(s, vec->cnt + 1, vec->shift, vec->root, new_tail);
	}
	
	new_root = push_tail(s, vec->shift - 5, vec->root, vec->tail, &expansion);
	if (expansion) {
		expansion_value.type = tmp.type = VECTOR_NODE;
		expansion_value.obj.vec_node = expansion;
		tmp.obj.vec_node = new_root;
		new_root = node_create2(s, &tmp, &expansion_value);
		new_shift += 5;
	}
	
	return vector_create(s, vec->cnt + 1, new_shift, new_root, node_create1(s, val));
}

static vector_node_t *push_tail(su_state *s, int level, vector_node_t *arr, vector_node_t *tail_node, vector_node_t **expansion) {
	value_t tmp;
	vector_node_t *new_child, *ret;
	
	if (level == 0) {
		new_child = tail_node;
	} else {
		new_child = push_tail(s, level - 5, arr->data[arr->len - 1].obj.vec_node, tail_node, expansion);
		if (*expansion == NULL) {
			ret = node_clone(s, arr);
			tmp.type = VECTOR_NODE;
			tmp.obj.vec_node = new_child;
			ret->data[arr->len - 1] = tmp;
			return ret;
		} else {
			new_child = *expansion;
		}
	}
	
	/* Do expansion */
	
	tmp.type = VECTOR_NODE;
	tmp.obj.vec_node = new_child;
	
	if (arr->len == 32) {
		*expansion = node_create1(s, &tmp);
		return arr;
	}
	
	ret = node_create_only(s, arr->len + 1);
	memcpy(ret->data, arr->data, sizeof(value_t) * arr->len);
	ret->data[arr->len] = tmp;
	*expansion = NULL;
	return ret;
}

value_t vector_set(su_state *s, vector_t *vec, int i, value_t *val) {
	vector_node_t *new_tail;
	if (i >= 0 && i < vec->cnt) {
		if (i >= tailoff(vec)) {
			new_tail = node_create_only(s, vec->tail->len);
			memcpy(new_tail->data, vec->tail->data, sizeof(value_t) * vec->tail->len);
			new_tail->data[i & 0x01f] = *val;
			return vector_create(s, vec->cnt, vec->shift, vec->root, new_tail);
		}
		return vector_create(s, vec->cnt, vec->shift, insert(s, vec->shift, vec->root, i, val), vec->tail);
	}
	su_error(s, "Index is out of bounds: %i", i);
	return *val;
}

static vector_node_t *insert(su_state *s, int level, vector_node_t *arr, int i, value_t *val) {
	int subidx;
	value_t tmp;
	vector_node_t *ret = node_clone(s, arr);
	if (level == 0) {
		ret->data[i & 0x01f] = *val;
	} else {
		subidx = (i >> level) & 0x01f;
		tmp.type = VECTOR_NODE;
		tmp.obj.vec_node = insert(s, level - 5, arr->data[subidx].obj.vec_node, i, val);
		ret->data[subidx] = tmp;
	}
	return ret;
}

value_t vector_pop(su_state *s, vector_t *vec) {
	vector_node_t *new_tail, *new_root;
	vector_node_t *ptail = NULL;
	int new_shift = vec->shift;
	
	if (vec->cnt == 0)
		su_error(s, "Can't pop empty vector!");
	if (vec->cnt == 1)
		return vector_create_empty(s);
	
	if (vec->tail->len > 1) {
		new_tail = node_create_only(s, vec->tail->len - 1);
		memcpy(new_tail->data, vec->tail->data, sizeof(value_t) * new_tail->len);
		return vector_create(s, vec->cnt - 1, vec->shift, vec->root, new_tail);
	}

	new_root = pop_tail(s, vec->shift - 5, vec->root, &ptail);
	if (!new_root)
		new_root = node_create_only(s, 0);

	if (vec->shift > 5 && new_root->len == 1) {
		new_root = new_root->data[0].obj.vec_node;
		new_shift -= 5;
	}

	return vector_create(s, vec->cnt - 1, new_shift, new_root, ptail);
}

static vector_node_t *pop_tail(su_state *s, int shift, vector_node_t *arr, vector_node_t **ptail) {
	vector_node_t *new_child, *ret;
	value_t tmp;
	
	if (shift > 0) {
		new_child = pop_tail(s, shift - 5, arr->data[arr->len - 1].obj.vec_node, ptail);
		if (new_child != NULL) {
			tmp.type = VECTOR_NODE;
			tmp.obj.vec_node = new_child;
			ret = node_clone(s, arr);
			ret->data[arr->len - 1] = tmp;
			return ret;
		}
	}
	
	if (shift == 0)
		*ptail = arr->data[arr->len - 1].obj.vec_node;
	
	/* Contraction */
	
	if (arr->len == 1)
		return NULL;
	
	ret = node_create_only(s, arr->len - 1);
	memcpy(ret->data, arr->data, sizeof(value_t) * ret->len);
	return ret;
}

/* --------------------------------- HashMap implementation --------------------------------- */

#define CAST_AND_TEST(t, f) t *thiz = (t*)n; assert(n->gc.type == (f))
#define xmemcpy(dest, doff, src, soff, num) memcpy((void*)(((char*)(dest))+(doff)),(void*)(((char*)(src))+(soff)),(num))

#define MASK(h, s) (((h) >> (s)) & 0x01f)
#define BITPOS(h, s) (1 << MASK((h), (s)))

static node_t *create_leaf_node(su_state *s, int hash, value_t *key, value_t *val);
static node_t *create_collision_node(su_state *s, int hash, vector_node_t *leaves);
static node_t *create_full_node(su_state *s, vector_node_t *nodes, int shift);
static node_t *create_idx_node(su_state *s, int bitmap, vector_node_t *nodes, int shift);
static node_t *create_idx_node2(su_state *s, int bitmap, vector_node_t *nodes, int shift);
static node_t *create_idx_node3(su_state *s, int shift, node_t *branch, int hash, value_t *key, value_t *val, node_t **added_leaf);

/* Full node */

static node_t *full_node_set(su_state *s, node_t *n, int shift, int hash, value_t *key, value_t *val, node_t **added_leaf) {
	int idx;
	node_t *tmp;
	value_t v;
	vector_node_t *new_nodes;
	CAST_AND_TEST(node_full_t, MAP_FULL);
	
	idx = MASK(hash, shift);
	tmp = thiz->nodes->data[idx].obj.map_node;
	tmp = tmp->vt->set(s, tmp, shift + 5, hash, key, val, added_leaf);
	if (tmp == thiz->nodes->data[idx].obj.map_node) {
		return n;
	} else {
		new_nodes = node_clone(s, thiz->nodes);
		v.type = tmp->gc.type;
		v.obj.map_node = tmp;
		new_nodes->data[idx] = v;
		return create_full_node(s, new_nodes, shift);
	}
}

static node_t *full_node_without(su_state *s, node_t *n, int hash, value_t *key) {
	int idx;
	value_t v;
	node_t *tmp;
	vector_node_t *new_nodes;
	CAST_AND_TEST(node_full_t, MAP_FULL);
	
	idx = MASK(hash, thiz->shift);
	tmp = thiz->nodes->data[idx].obj.map_node;
	tmp = tmp->vt->without(s, tmp, hash, key);
	if (tmp != thiz->nodes->data[idx].obj.map_node) {
		if (!tmp) {
			new_nodes = node_create_only(s, thiz->nodes->len - 1);
			memcpy(new_nodes->data, thiz->nodes->data, sizeof(value_t) * idx);
			xmemcpy(new_nodes->data, sizeof(value_t) * idx, thiz->nodes->data, sizeof(value_t) * (idx + 1), sizeof(value_t) * (thiz->nodes->len - (idx + 1)));
			return create_idx_node(s, ~BITPOS(hash, thiz->shift), new_nodes, thiz->shift);
		}
		
		new_nodes = node_clone(s, thiz->nodes);
		v.type = tmp->gc.type;
		v.obj.map_node = tmp;
		new_nodes->data[idx] = v;
		return create_full_node(s, new_nodes, thiz->shift);
	}
	return n;
}

static node_leaf_t *full_node_find(su_state *s, node_t *n, int hash, value_t *key) {
	node_t *tmp;
	CAST_AND_TEST(node_full_t, MAP_FULL);
	tmp = thiz->nodes->data[MASK(hash, thiz->shift)].obj.map_node;
	return tmp->vt->find(s, tmp, hash, key);
}

static int full_node_get_hash(su_state *s, node_t *n) {
	CAST_AND_TEST(node_full_t, MAP_FULL);
	return thiz->hash;
}

const node_class_t full_vt = {
	&full_node_set,
	&full_node_without,
	&full_node_find,
	&full_node_get_hash
};

static node_t *create_full_node(su_state *s, vector_node_t *nodes, int shift) {
	node_t *tmp, *n;
	node_full_t *fn = (node_full_t*)su_allocate(s, NULL, sizeof(node_full_t));
	fn->nodes = nodes;
	fn->shift = shift;
	tmp = nodes->data[0].obj.map_node;
	fn->hash = tmp->vt->get_hash(s, tmp);
	
	n = (node_t*)fn;
	n->vt = &full_vt;
	return (node_t*)gc_insert_object(s, (gc_t*)n, MAP_FULL);
}

/* Index node */

static int idx_node_index(node_t *n, int bit) {
	CAST_AND_TEST(node_idx_t, MAP_IDX);
	return bit_count(thiz->bitmap & (bit - 1));
}

static node_t *idx_node_set(su_state *s, node_t *n, int shift, int hash, value_t *key, value_t *val, node_t **added_leaf) {
	int bit, idx;
	node_t *tmp;
	value_t v;
	vector_node_t *new_nodes;
	CAST_AND_TEST(node_idx_t, MAP_IDX);
	bit = BITPOS(hash, shift);
	idx = idx_node_index(n, bit);
	
	if ((thiz->bitmap & bit) != 0) {
		tmp = thiz->nodes->data[idx].obj.map_node;
		tmp = tmp->vt->set(s, tmp, shift + 5, hash, key, val, added_leaf);
		if (tmp == thiz->nodes->data[idx].obj.map_node) {
			return n;
		} else {
			new_nodes = node_clone(s, thiz->nodes);
			v.type = tmp->gc.type;
			v.obj.map_node = tmp;
			new_nodes->data[idx] = v;
			return create_idx_node(s, thiz->bitmap, new_nodes, shift);
		}
	} else {
		new_nodes = node_create_only(s, thiz->nodes->len + 1);
		memcpy(new_nodes->data, thiz->nodes->data, sizeof(value_t) * idx);
		*added_leaf = new_nodes->data[idx].obj.map_node = create_leaf_node(s, hash, key, val);
		new_nodes->data[idx].type = (*added_leaf)->gc.type;
		xmemcpy(new_nodes->data, sizeof(value_t) * (idx + 1), thiz->nodes->data, sizeof(value_t) * idx, sizeof(value_t) * (thiz->nodes->len - idx));
		return create_idx_node2(s, thiz->bitmap | bit, new_nodes, shift);
	}
}

static node_t *idx_node_without(su_state *s, node_t *n, int hash, value_t *key) {
	int bit, idx;
	node_t *tmp;
	value_t v;
	vector_node_t *new_nodes;
	CAST_AND_TEST(node_idx_t, MAP_IDX);
	bit = BITPOS(hash, thiz->shift);
	
	if ((thiz->bitmap & bit) != 0) {
		idx = idx_node_index(n, bit);
		tmp = thiz->nodes->data[idx].obj.map_node;
		tmp = tmp->vt->without(s, tmp, hash, key);
		if (tmp != thiz->nodes->data[idx].obj.map_node) {
			if (!tmp) {
				if (thiz->bitmap == bit)
					return NULL;

				new_nodes = node_create_only(s, thiz->nodes->len - 1);
				memcpy(new_nodes->data, thiz->nodes->data, sizeof(value_t) * idx);
				xmemcpy(new_nodes->data, sizeof(value_t) * idx, thiz->nodes->data, sizeof(value_t) * (idx + 1), sizeof(value_t) * (thiz->nodes->len - (idx + 1)));
				return create_idx_node(s, thiz->bitmap & ~bit, new_nodes, thiz->shift);
			}
			new_nodes = node_clone(s, thiz->nodes);
			v.type = tmp->gc.type;
			v.obj.map_node = tmp;
			new_nodes->data[idx] = v;
			return create_idx_node(s, thiz->bitmap, new_nodes, thiz->shift);
		}
	}
	return n;
}

static node_leaf_t *idx_node_find(su_state *s, node_t *n, int hash, value_t *key) {
	int bit;
	node_t *tmp;
	CAST_AND_TEST(node_idx_t, MAP_IDX);
	bit = BITPOS(hash, thiz->shift);
	if ((thiz->bitmap & bit) != 0) {
		tmp = thiz->nodes->data[idx_node_index(n, bit)].obj.map_node;
		return tmp->vt->find(s, tmp, hash, key);
	} else {
		return NULL;
	}
}

static int idx_node_get_hash(su_state *s, node_t *n) {
	CAST_AND_TEST(node_idx_t, MAP_IDX);
	return thiz->hash;
}

const node_class_t idx_vt = {
	&idx_node_set,
	&idx_node_without,
	&idx_node_find,
	&idx_node_get_hash
};

static node_t *create_idx_node(su_state *s, int bitmap, vector_node_t *nodes, int shift) {
	node_t *n;
	node_t *tmp;
	node_idx_t *in = (node_idx_t*)su_allocate(s, NULL, sizeof(node_idx_t));
	in->bitmap = bitmap;
	in->shift = shift;
	in->nodes = nodes;
	tmp = nodes->data[0].obj.map_node;
	in->hash = tmp->vt->get_hash(s, tmp);
	
	n = (node_t*)in;
	n->vt = &idx_vt;
	return (node_t*)gc_insert_object(s, (gc_t*)n, MAP_IDX);
}

static node_t *create_idx_node2(su_state *s, int bitmap, vector_node_t *nodes, int shift) {
	if (bitmap == -1)
		return create_full_node(s, nodes, shift);
	return create_idx_node(s, bitmap, nodes, shift);
}

static node_t *create_idx_node3(su_state *s, int shift, node_t *branch, int hash, value_t *key, value_t *val, node_t **added_leaf) {
	value_t v;
	vector_node_t *vec;
	node_t *n;
	
	v.type = branch->gc.type;
	v.obj.map_node = branch;
	vec = node_create1(s, &v);
	
	n = create_idx_node(s, BITPOS(branch->vt->get_hash(s, branch), shift), vec, shift);
	return n->vt->set(s, n, shift, hash, key, val, added_leaf);
}

/* Collision node */

static int collision_node_find_index(su_state *s, node_t *n, int hash, value_t *key) {
	int i;
	node_t *tmp;
	CAST_AND_TEST(node_collision_t, MAP_COLLISION);
	for (i = 0; i < thiz->leaves->len; i++) {
		tmp = thiz->leaves->data[i].obj.map_node;
		if (tmp->vt->find(s, tmp, hash, key))
			return i;
	}
	return -1;
}

static node_t *collision_node_set(su_state *s, node_t *n, int shift, int hash, value_t *key, value_t *val, node_t **added_leaf) {
	int idx;
	value_t v;
	vector_node_t *new_leaves;
	CAST_AND_TEST(node_collision_t, MAP_COLLISION);
	
	if (hash == thiz->hash) {
		idx = collision_node_find_index(s, n, hash, key);
		if (idx != -1) {
			if (value_eq(&thiz->leaves->data[idx], val))
				return n;
			
			new_leaves = node_clone(s, thiz->leaves);
			v.type = MAP_LEAF;
			v.obj.map_node = (node_t*)create_leaf_node(s, hash, key, val);
			new_leaves->data[idx] = v;
			return create_collision_node(s, hash, new_leaves);
		}
		
		new_leaves = node_create_only(s, thiz->leaves->len + 1);
		memcpy(new_leaves->data, thiz->leaves->data, sizeof(value_t) * thiz->leaves->len);
		
		*added_leaf = (node_t*)create_leaf_node(s, hash, key, val);
		v.obj.map_node = *added_leaf;
		v.type = v.obj.map_node->gc.type;
		new_leaves->data[thiz->leaves->len] = v;
		return create_collision_node(s, hash, new_leaves);
	}
	return create_idx_node3(s, shift, n, hash, key, val, added_leaf);
}

static node_t *collision_node_without(su_state *s, node_t *n, int hash, value_t *key) {
	int idx;
	vector_node_t *new_leaves;
	CAST_AND_TEST(node_collision_t, MAP_COLLISION);
	idx = collision_node_find_index(s, n, hash, key);
	if (idx == -1)
		return n;
	if (thiz->leaves->len == 2)
		return idx == 0 ? thiz->leaves->data[1].obj.map_node : thiz->leaves->data[0].obj.map_node;
	
	new_leaves = node_create_only(s, thiz->leaves->len - 1);
	memcpy(new_leaves->data, thiz->leaves->data, sizeof(value_t) * idx);
	xmemcpy(new_leaves->data, sizeof(value_t) * idx, thiz->leaves->data, sizeof(value_t) * (idx + 1), sizeof(value_t) * (thiz->leaves->len - (idx + 1)));
	return create_collision_node(s, hash, new_leaves);
}

static node_leaf_t *collision_node_find(su_state *s, node_t *n, int hash, value_t *key) {
	int idx;
	node_leaf_t *tmp;
	CAST_AND_TEST(node_collision_t, MAP_COLLISION);
	idx = collision_node_find_index(s, n, hash, key);
	if (idx != -1) {
		tmp = (node_leaf_t*)thiz->leaves->data[idx].obj.map_node;
		assert(tmp->n.gc.type == MAP_LEAF);
		return tmp;
	}
	return NULL;
}

static int collision_node_get_hash(su_state *s, node_t *n) {
	CAST_AND_TEST(node_collision_t, MAP_COLLISION);
	return thiz->hash;
}

const node_class_t collision_vt = {
	&collision_node_set,
	&collision_node_without,
	&collision_node_find,
	&collision_node_get_hash
};

static node_t *create_collision_node(su_state *s, int hash, vector_node_t *leaves) {
	node_t *n;
	node_collision_t *cn = (node_collision_t*)su_allocate(s, NULL, sizeof(node_collision_t));
	cn->hash = hash;
	cn->leaves = leaves;
	
	n = (node_t*)cn;
	n->vt = &collision_vt;
	return (node_t*)gc_insert_object(s, (gc_t*)n, MAP_COLLISION);
}

/* Leaf node */

static node_t *leaf_node_set(su_state *s, node_t *n, int shift, int hash, value_t *key, value_t *val, node_t **added_leaf) {
	value_t v, t;
	CAST_AND_TEST(node_leaf_t, MAP_LEAF);
	
	if (hash == thiz->hash) {
		if (value_eq(key, &thiz->key)) {
			if (value_eq(val, &thiz->val))
				return n;
			return create_leaf_node(s, hash, key, val);
		}
		
		*added_leaf = create_leaf_node(s, hash, key, val);
		v.type = (*added_leaf)->gc.type;
		v.obj.map_node = *added_leaf;
		t.type = n->gc.type;
		t.obj.map_node = n;
		
		return create_collision_node(s, hash, node_create2(s, &t, &v));
	}
	return create_idx_node3(s, shift, n, hash, key, val, added_leaf);
}

static node_t *leaf_node_without(su_state *s, node_t *n, int hash, value_t *key) {
	CAST_AND_TEST(node_leaf_t, MAP_LEAF);
	if (hash == thiz->hash && value_eq(key, &thiz->key))
		return NULL;
	return n;
}

static node_leaf_t *leaf_node_find(su_state *s, node_t *n, int hash, value_t *key) {
	CAST_AND_TEST(node_leaf_t, MAP_LEAF);
	if(hash == thiz->hash && value_eq(key, &thiz->key))
		return thiz;
	return NULL;
}

static int leaf_node_get_hash(su_state *s, node_t *n) {
	CAST_AND_TEST(node_leaf_t, MAP_LEAF);
	return thiz->hash;
}

const node_class_t leaf_vt = {
	&leaf_node_set,
	&leaf_node_without,
	&leaf_node_find,
	&leaf_node_get_hash
};

static node_t *create_leaf_node(su_state *s, int hash, value_t *key, value_t *val) {
	node_t *n;
	node_leaf_t *ln = (node_leaf_t*)su_allocate(s, NULL, sizeof(node_leaf_t));
	ln->hash = hash;
	ln->key = *key;
	ln->val = *val;
	
	n = (node_t*)ln;
	n->vt = &leaf_vt;
	return (node_t*)gc_insert_object(s, (gc_t*)n, MAP_LEAF);
}

/* Empty node */

static node_t *empty_node_set(su_state *s, node_t *n, int shift, int hash, value_t *key, value_t *val, node_t **added_leaf) {
	node_t *ret = create_leaf_node(s, hash, key, val);
	CAST_AND_TEST(node_t, MAP_EMPTY);
	(void)*thiz;
	*added_leaf = ret;
	return ret;
}

static node_t *empty_node_without(su_state *s, node_t *n, int hash, value_t *key) {
	CAST_AND_TEST(node_t, MAP_EMPTY);
	(void)*thiz;
	return n;
}

static node_leaf_t *empty_node_find(su_state *s, node_t *n, int hash, value_t *key) {
	CAST_AND_TEST(node_t, MAP_EMPTY);
	(void)*thiz;
	return NULL;
}

static int empty_node_get_hash(su_state *s, node_t *n) {
	CAST_AND_TEST(node_t, MAP_EMPTY);
	(void)*thiz;
	return 0;
}

const node_class_t empty_vt = {
	&empty_node_set,
	&empty_node_without,
	&empty_node_find,
	&empty_node_get_hash
};

static node_t *create_empty_node(su_state *s) {
	node_t *n = (node_t*)su_allocate(s, NULL, sizeof(node_t));
	n->vt = &empty_vt;
	return (node_t*)gc_insert_object(s, &n->gc, MAP_EMPTY);
}

/* Map functions */

static value_t map_create(su_state *s, int cnt, node_t *root) {
	value_t v;
	map_t *m = (map_t*)su_allocate(s, NULL, sizeof(map_t));
	m->root = root;
	m->cnt = cnt;
	v.type = SU_MAP;
	v.obj.gc_object = gc_insert_object(s, (gc_t*)m, SU_MAP);
	return v;
}

value_t map_create_empty(su_state *s) {
	return map_create(s, 0, create_empty_node(s));
}

value_t map_get(su_state *s, map_t *m, value_t *key, unsigned hash) {
	value_t v;
	node_leaf_t *n = m->root->vt->find(s, m->root, (int)hash, key);
	if (!n) {
		v.type = SU_INV;
		return v;
	}
	return n->val;
}

value_t map_remove(su_state *s, map_t *m, value_t *key, unsigned hash) {
	value_t v;
	node_t *new_root = m->root->vt->without(s, m->root, (int)hash, key);
	v.type = SU_MAP;
	if (new_root == m->root) {
		v.obj.m = m;
		return v;
	}
	if (!new_root)
		return map_create_empty(s);
	return map_create(s, m->cnt - 1, new_root);
}

value_t map_insert(su_state *s, map_t *m, value_t *key, unsigned hash, value_t *val) {
	value_t v;
	node_t *added_leaf = NULL;
	node_t *new_root = m->root->vt->set(s, m->root, 0, (int)hash, key, val, &added_leaf);
	if (new_root == m->root) {
		v.type = SU_MAP;
		v.obj.m = m;
		return v;
	}
	return map_create(s, added_leaf ? m->cnt + 1 : m->cnt, new_root);
}

value_t map_cat(su_state *s, map_t *a, map_t *b) {
	value_t first, rest, kv, ret;
	value_t seq = tree_create_map(s, b);
	
	ret.type = SU_MAP;
	ret.obj.m = a;
	
	while (seq.type != SU_NIL) {
		kv = seq_first(s, seq.obj.q);
		first = seq_first(s, kv.obj.q);
		rest = seq_rest(s, kv.obj.q);
		map_insert(s, a, &first, hash_value(&first), &rest);
		seq = seq_rest(s, seq.obj.q);
	}
	
	return ret;
}

int map_length(map_t *m) {
	return m->cnt;
}

/* --------------------------------- Seq implementation --------------------------------- */

static value_t it_seq_create_with_index(su_state *s, gc_t *obj, int idx);

static value_t it_seq_string_first(su_state *s, seq_t *q) {
	value_t v;
	char buffer[2] = {0, 0};
	it_seq_t *itq = (it_seq_t*)q;
	string_t *str = (string_t*)itq->obj;
	v.type = SU_STRING;
	buffer[0] = str->str[itq->idx];
	v.obj.gc_object = string_from_cache(s, buffer, 2);
	return v;
}

static value_t it_seq_string_rest(su_state *s, seq_t *q) {
	value_t v;
	it_seq_t *itq = (it_seq_t*)q;
	v.type = SU_NIL;
	if (itq->idx + 1 >= ((string_t*)itq->obj)->size)
		return v;
	return it_seq_create_with_index(s, itq->obj, itq->idx + 1);
}

static value_t it_seq_vector_first(su_state *s, seq_t *q) {
	it_seq_t *itq = (it_seq_t*)q;
	return vector_index(s, (vector_t*)itq->obj, itq->idx);
}

static value_t it_seq_vector_rest(su_state *s, seq_t *q) {
	value_t v;
	it_seq_t *itq = (it_seq_t*)q;
	v.type = SU_NIL;
	if (itq->idx + 1 >= ((vector_t*)itq->obj)->cnt)
		return v;
	return it_seq_create_with_index(s, itq->obj, itq->idx + 1);
}

const seq_class_t seq_string_vt = {&it_seq_string_first, &it_seq_string_rest};
const seq_class_t seq_vector_vt = {&it_seq_vector_first, &it_seq_vector_rest};

static value_t it_seq_create_with_index(su_state *s, gc_t *obj, int idx) {
	value_t v;
	it_seq_t *q = (it_seq_t*)su_allocate(s, NULL, sizeof(it_seq_t));
	v.type = IT_SEQ;
	q->obj = obj;
	q->idx = idx;
	
	q->q.vt = v.type == SU_STRING ? &seq_string_vt : &seq_vector_vt;
	v.obj.gc_object = gc_insert_object(s, &q->q.gc, IT_SEQ);
	return v;
}

value_t it_seq_create(su_state *s, value_t *obj) {
	return it_seq_create_with_index(s, obj->obj.gc_object, 0);
}
