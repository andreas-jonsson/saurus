/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef _SEQ_H_
#define _SEQ_H_

#include "saurus.h"
#include "intern.h"

struct vector_node {
	gc_t gc;
	unsigned char len;
	value_t data[1];
};

struct vector {
	gc_t gc;
	int cnt;
	int shift;
	vector_node_t *root;
	vector_node_t *tail;
};

int vector_length(vector_t *v);
value_t vector_cat(su_state *s, vector_t *a, vector_t *b);
value_t vector_index(su_state *s, vector_t *v, int i);
value_t vector_create_empty(su_state *s);
value_t vector_push(su_state *s, vector_t *vec, value_t *val);
value_t vector_pop(su_state *s, vector_t *vec);
value_t vector_set(su_state *s, vector_t *vec, int i, value_t *val);

/***********************************************************************************/

typedef node_t* (*node_set_func_t)(su_state *s, node_t *n, int shift, int hash, value_t *key, value_t *val, node_t **added_leaf);
typedef node_t* (*node_without_func_t)(su_state *s, node_t *n, int hash, value_t *key);
typedef node_leaf_t* (*node_find_func_t)(su_state *s, node_t *n, int hash, value_t *key);
typedef int (*node_get_hash_t)(su_state *s, node_t *n);

typedef struct {
	node_set_func_t set;
	node_without_func_t without;
	node_find_func_t find;
	node_get_hash_t get_hash;
} node_class_t;

struct node {
	gc_t gc;
	const node_class_t *vt;
};

struct node_leaf {
	node_t n;
	int hash;
	value_t key;
	value_t val;
};

struct node_collision {
	node_t n;
	vector_node_t *leaves;
	int hash;
};

struct node_idx {
	node_t n;
	vector_node_t *nodes;
	int bitmap;
	int shift;
	int hash;
};
	
struct node_full {
	node_t n;
	vector_node_t *nodes;
	int shift;
	int hash;
};

struct map {
	gc_t gc;
	int cnt;
	node_t *root;
};

value_t map_create_empty(su_state *s);
value_t map_cat(su_state *s, map_t *a, map_t *b);
value_t map_get(su_state *s, map_t *m, value_t *key, unsigned hash);
value_t map_remove(su_state *s, map_t *m, value_t *key, unsigned hash);
value_t map_insert(su_state *s, map_t *m, value_t *key, unsigned hash, value_t *val);
int map_length(map_t *m);

/***********************************************************************************/

typedef value_t (*seq_fr_func_t)(su_state *s, seq_t *q);

typedef struct {
	seq_fr_func_t first;
	seq_fr_func_t rest;
} seq_class_t;

struct seq {
	gc_t gc;
	const seq_class_t *vt;
};

typedef struct {
	seq_t q;
	value_t f;
	value_t d;
} lazy_seq_t;

typedef struct {
	seq_t q;
	int cnt;
	int end;
	int step;
} range_seq_t;

typedef struct {
	seq_t q;
	int idx;
	int step;
	gc_t *obj;
} it_seq_t;

typedef struct {
	seq_t q;
	value_t first, rest;
} cell_seq_t;

typedef struct {
	node_t *n;
	int idx;
} tree_link_t;

typedef struct {
	seq_t q;
	int nlinks;
	tree_link_t links[1];
} tree_seq_t;

value_t cell_create_array(su_state *s, value_t *array, int num);
value_t cell_create(su_state *s, value_t *first, value_t *rest);

value_t it_create_vector(su_state *s, vector_t *vec, int reverse);
value_t it_create_string(su_state *s, string_t *str, int reverse);

value_t range_create(su_state *s, int from, int to);
value_t lazy_create(su_state *s, value_t *f);

value_t tree_create_map(su_state *s, map_t *m);

value_t seq_first(su_state *s, seq_t *q);
value_t seq_rest(su_state *s, seq_t *q);

#endif
