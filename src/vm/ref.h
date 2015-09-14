/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef _REF_H_
#define _REF_H_

#include "saurus.h"
#include "intern.h"

struct local {
	gc_t gc;
	int tid;
	value_t v;
};

value_t ref_local(su_state *s, value_t *val);
value_t unref_local(su_state *s, local_t *loc);
void set_local(su_state *s, local_t *loc, value_t *val);

#endif
