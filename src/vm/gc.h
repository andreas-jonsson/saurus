/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef _GC_H_
#define _GC_H_

#include "saurus.h"
#include "intern.h"

enum {
	GC_FLAG_WHITE,
	GC_FLAG_GRAY,
	GC_FLAG_BLACK
};

enum {
	GC_USR_GRAY = 0x1
};

enum {
	GC_STATE_MARK,
	GC_STATE_SWEEP
};

void gc_trace(su_state *s);
void gc_free_object(su_state *s, gc_t *obj);
void gc_gray_mutable(su_state *s, gc_t *obj);

#endif
