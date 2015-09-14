/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "saurus.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

typedef struct {
	const char *buffer;
	unsigned size;
} buffer_and_size;

static aint_t print_spin_lock; /* From core.c */

static void print_rec(su_state *s, int idx);

#ifndef SU_OPT_NO_PATTERN

static void pattern_error(su_state *s, int err) {
	switch (err) {
		case SLRE_UNEXPECTED_QUANTIFIER:
			su_error(s, "Unexpected quantifier!");
		case SLRE_UNBALANCED_BRACKETS:
			su_error(s, "Unbalanced brackets!");
		case SLRE_INTERNAL_ERROR:
			su_error(s, "Internal error!");
		case SLRE_INVALID_CHARACTER_SET:
			su_error(s, "Invalid character set!");
		case SLRE_INVALID_METACHARACTER:
			su_error(s, "Invalid metacharacter!");
		case SLRE_CAPS_ARRAY_TOO_SMALL:
			su_error(s, "Caps array too small!");
		case SLRE_TOO_MANY_BRANCHES:
			su_error(s, "Too many branches!");
		case SLRE_TOO_MANY_BRACKETS:
			su_error(s, "Too many brackets!");
		default:
			su_error(s, "Unknown error!");
	}
}

#endif

static const void *reader(size_t *size, void *data) {
	buffer_and_size *bas = (buffer_and_size*)data;
	if (!size) return NULL;
	*size = bas->size;
	bas->size = 0;
	return *size ? bas->buffer : NULL;
}

static int load(su_state *s, int narg) {
	buffer_and_size bas;
	su_check_arguments(s, 1, SU_STRING);
	bas.buffer = su_tostring(s, -1, &bas.size);
	if (su_load(s, &reader, &bas))
		su_pushnil(s);
	return 1;
}

static int compile(su_state *s, int narg) {
	size_t size;
	const char *code, *name;
	char *res;
	
	if (narg == 1) {
		su_check_arguments(s, 1, SU_STRING);
		code = su_tostring(s, -1, NULL);
		name = NULL;
	} else {
		su_check_arguments(s, 2, SU_STRING, SU_STRING);
		code = su_tostring(s, -2, NULL);
		name = su_tostring(s, -1, NULL);
	}
	
	if (su_compile(s, code, name, NULL, &res, &size))
		su_pushstring(s, res);
	else if (su_load(s, NULL, (void*)res))
		su_pushstring(s, "Could not compile!");
	
	su_allocate(s, res, 0);
	return 1;
}

static int loadlib(su_state *s, int narg) {
	const char *libname;
	const char *symname;
	void *lib;
	su_nativefunc sym;
	
	if (narg > 1) {
		su_check_type(s, -narg, SU_STRING);
		su_check_type(s, -narg + 1, SU_STRING);
		libname = su_tostring(s, -narg, NULL);
		symname = su_tostring(s, -narg + 1, NULL);
		lib = lib_load(libname);
		su_assert(s, lib != NULL, "Could not load: %s", libname);
		sym = (su_nativefunc)lib_sym(lib, symname);
		if (!sym) {
			lib_unload(lib);
			su_error(s, "Could not load symbol '%s' in library: %s", symname, libname);
		}
		return sym(s, narg - 2);
	}
	
	su_check_arguments(s, 2, SU_STRING, SU_STRING);
	return 0;
}

static int execute(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_STRING);
	su_pushinteger(s, system(su_tostring(s, -1, NULL)));
	return 1;
}

static void print_rec(su_state *s, int idx) {
	int i, tmp;
	FILE *fp = stdout;
	int type = su_type(s, idx);

	if (type == SU_SEQ) {
		fprintf(fp, "(");
		for (i = 0; su_type(s, -1) == SU_SEQ; i++) {
			if (i > 0) fprintf(fp, " ");
			su_first(s, -1);
			print_rec(s, -1);
			su_pop(s, 1);
			su_rest(s, -1);
			su_swap(s, -2, -1);
			su_pop(s, 1);
		}
		if (su_type(s, -1) != SU_NIL) {
			fprintf(fp, ":");
			print_rec(s, -1);
		}
		fprintf(fp, ")");
	} else {
		switch (type) {
			case SU_VECTOR:
				tmp = su_vector_length(s, idx);
				fprintf(fp, "[");
				for (i = 0; i < tmp; i++) {
					if (i > 0) fprintf(fp, " ");
					su_pushinteger(s, i);
					su_vector_index(s, idx - 1);
					print_rec(s, -1);
					su_pop(s, 1);
				}
				fprintf(fp, "]");
				break;
			case SU_MAP:
				fprintf(fp, "{");
				su_seq(s, idx, 0);
				for (i = 0; su_type(s, -1) == SU_SEQ; i++) {
					if (i > 0) fprintf(fp, " ");
					su_first(s, -1);
					print_rec(s, -1);
					su_pop(s, 1);
					su_rest(s, -1);
					su_swap(s, -2, -1);
					su_pop(s, 1);
				}
				su_pop(s, 1);
				fprintf(fp, "}");
				break;
			case SU_STRING:
				fprintf(fp, "%s", su_tostring(s, idx, NULL));
				break;
			default:
				fprintf(fp, "%s", su_stringify(s, idx));
		}
	}
}

static int print(su_state *s, int narg) {
	FILE *fp = stdout;
	spin_lock(&print_spin_lock);
	for (; narg; narg--) {
		print_rec(s, -narg);
		fputs("\t", fp);
	}
	fputs("\n", fp);
	fflush(fp);
	spin_unlock(&print_spin_lock);
	return 0;
}

static int apply(su_state *s, int narg) {
	su_object_type_t t;
	su_assert(s, narg == 2, "apply expected 2 arguments!");
	t = su_type(s, -2);
	su_assert(s, t == SU_FUNCTION || t == SU_NATIVEFUNC, "apply expected function to call!");
	su_check_type(s, -1, SU_SEQ);
	su_copy(s, -2);
	su_call(s, su_unpack_seq(s, -2), 1);
	return 1;
}

static int type(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_NIL);
	su_pushstring(s, su_type_name(s, -1));
	return 1;
}

static int string(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_NIL);
	if (su_type(s, -1) != SU_STRING)
		su_pushstring(s, su_stringify(s, -1));
	return 1;
}

static int number(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_STRING);
	su_pushnumber(s, atof(su_tostring(s, -1, NULL)));
	return 1;
}

static int unref(su_state *s, int narg) {
	su_object_type_t t;
	su_check_arguments(s, 1, SU_NIL);
	t = su_type(s, -1);
	su_assert(s, t == SU_LOCAL || t == SU_GLOBAL, "Expected mutable object!");
	su_unref(s, -1);
	return 1;
}

static int local_ref(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_NIL);
	su_ref_local(s, -1);
	return 1;
}

static int global_ref(su_state *s, int narg) {
	su_object_type_t t;
	su_check_num_arguments(s, 1);
	t = su_type(s, -1);
	su_assert(s, t == SU_NIL || t == SU_MAP, "Expected hashmap or nil!");
	su_ref_global(s, -1);
	return 1;
}

static int set(su_state *s, int narg) {
	su_check_arguments(s, 2, SU_LOCAL, SU_NIL);
	su_copy(s, -1);
	su_setref(s, -3);
	return 1;
}

static int read_(su_state *s, int narg) {
	int size, tmp;
	FILE *fp;
	void *mem;
	char buffer[4096];

	if (narg == 0) {
		if (!fgets(buffer, sizeof(buffer), su_stdin(s)))
			return 0;
		su_pushstring(s, buffer);
		return 1;
	}

	su_check_arguments(s, 1, SU_STRING);
	#ifdef SU_OPT_NO_FILE_IO
		return 0;
	#else
		fp = fopen(su_tostring(s, -1, NULL), "rb");
		if (!fp)
			return 0;
		fseek(fp, 0, SEEK_END);
		size = (int)ftell(fp);
		fseek(fp, 0, SEEK_SET);
		mem = su_allocate(s, NULL, size);
	
		su_thread_indisposable(s);
		tmp = (int)fread(mem, 1, size, fp);
		su_thread_disposable(s);
	
		su_assert(s, size == tmp, "IO error: %d (%s)", errno, strerror(errno));
		su_pushbytes(s, mem, size);
		su_allocate(s, mem, 0);
		fclose(fp);
		return 1;
	#endif
}

static int write_(su_state *s, int narg) {
	FILE *fp;
	unsigned size, tmp;
	const char *str;
	su_check_arguments(s, 2, SU_STRING, SU_STRING);
	#ifdef SU_OPT_NO_FILE_IO
		return 0;
	#else
		fp = fopen(su_tostring(s, -2, NULL), "wb");
		if (!fp)
			return 0;
		str = su_tostring(s, -1, &size);
	
		su_thread_indisposable(s);
		tmp = (unsigned)fwrite(str, 1, size, fp);
		su_thread_disposable(s);
	
		su_assert(s, size == tmp, "IO error: %d (%s)", errno, strerror(errno));
		su_pushinteger(s, (int)size);
		fclose(fp);
		return 1;
	#endif
}

static int size_(su_state *s, int narg) {
	FILE *fp;
	double size;
	su_check_arguments(s, 1, SU_STRING);
#ifdef SU_OPT_NO_FILE_IO
	return 0;
#else
	fp = fopen(su_tostring(s, -1, NULL), "rb");
	if (!fp)
		return 0;
	fseek(fp, 0, SEEK_END);
	size = (double)ftell(fp);
	fclose(fp);
	su_pushnumber(s, size);
	return 1;
#endif
}

static int delete_(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_STRING);
#ifdef SU_OPT_NO_FILE_IO
	su_pushboolean(s, 0);
#else
	su_pushboolean(s, remove(su_tostring(s, -1, NULL)));
#endif
	return 1;
}

static int error(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_STRING);
	su_error(s, su_tostring(s, -1, NULL));
	return 0;
}

static int assert_(su_state *s, int narg) {
	su_object_type_t t;
	su_check_arguments(s, 2, SU_NIL, SU_STRING);
	t = su_type(s, -2);
	if (t == SU_NIL || (t == SU_BOOLEAN && !su_toboolean(s, -2)))
		su_error(s, su_tostring(s, -1, NULL));
	su_copy(s, -2);
	return 1;
}

static int format(su_state *s, int narg) {
	const char *fmt, *tmp;
	int i = narg - 1;
	char buffer[64];
	
	if (narg) {
		su_check_type(s, -narg, SU_STRING);
		fmt = su_tostring(s, -narg, NULL);
		su_string_begin(s, NULL);
		
		while (*fmt) {
			if (*fmt == '%') {
				fmt++;
				tmp = buffer;
				if (*fmt == '%') {
					su_string_ch(s, *fmt);
				} else if (*fmt == 's') {
					su_check_type(s, -i, SU_STRING);
					tmp = su_tostring(s, -i, NULL);
				} else if (*fmt == 'f') {
					su_check_type(s, -i, SU_NUMBER);
					sprintf(buffer, "%f", su_tonumber(s, -i));
				} else if (*fmt == 'i') {
					su_check_type(s, -i, SU_NUMBER);
					sprintf(buffer, "%i", su_tointeger(s, -i));
				} else if (*fmt == 'x') {
					su_check_type(s, -i, SU_NUMBER);
					sprintf(buffer, "%x", su_tointeger(s, -i));
				} else if (*fmt == 't') {
					tmp = su_type_name(s, -i);
				} else if (*fmt == 'v') {
					tmp = su_stringify(s, -i);
				} else if (*fmt == 'p') {
					sprintf(buffer, "%p", su_topointer(s, -i));
				} else {
					su_error(s, "Invalid character: %c", *fmt);
				}
				su_string_cat(s, tmp);
				i--;
			} else {
				su_string_ch(s, *fmt);
			}
			fmt++;
		}
		
		su_string_ch(s, '\0');
		su_string_push(s);
		return 1;
	}
	return 0;
}

static int char_(su_state *s, int narg) {
	su_check_arguments(s, 1, SU_NUMBER);
	su_string_begin(s, NULL);
	su_string_ch(s, (char)su_tointeger(s, -1));
	su_string_push(s);
	return 1;
}

static int byte_(su_state *s, int narg) {
	const char *str;
	su_check_arguments(s, 1, SU_STRING);
	str = su_tostring(s, -1, NULL);
	su_pushinteger(s, (int)str[0]);
	return 1;
}

static void cat_string(su_state *s) {
	char *str;
	unsigned size_a, size_b;
	const char *str_a = su_tostring(s, -2, &size_a);
	const char *str_b = su_tostring(s, -1, &size_b);
	su_string_begin(s, NULL);
	str = su_string_mem(s, size_a + size_b);
	memcpy(str, str_a, size_a);
	memcpy(str + size_a, str_b, size_b);
	su_pop(s, 2);
	su_string_push(s);
}

static void cat_number(su_state *s) {
	double n = su_tonumber(s, -2) + su_tonumber(s, -1);
	su_pop(s, 2);
	su_pushnumber(s, n);
}

static int cat(su_state *s, int narg) {
	su_object_type_t t;
	if (narg == 1) {
		return 1;
	} else if (narg > 1) {
		t = su_type(s, -narg);
		for (; narg > 1; narg--) {
			su_check_type(s, -1, t);
			su_check_type(s, -2, t);
			switch (t) {
				case SU_STRING:
					cat_string(s);
					break;
				case SU_NUMBER:
					cat_number(s);
					break;
				case SU_SEQ:
					su_cat_seq(s);
					break;
				case SU_VECTOR:
					su_vector_cat(s);
					break;
				case SU_MAP:
					su_map_cat(s);
					break;
				default:
					su_error(s, "Can't concatinate: %s", su_type_name(s, -1));
			}
		}
		return 1;
	}
	return 0;
}

static int find(su_state *s, int narg) {
	#ifndef SU_OPT_NO_PATTERN
		int offset;
		unsigned size;
		const char *buffer, *pattern;
		struct slre_cap cap;
		cap.len = 0;

		su_check_arguments(s, 2, SU_STRING, SU_STRING);

		buffer = su_tostring(s, -2, &size);
		pattern = su_tostring(s, -1, NULL);
		offset = slre_match(pattern, buffer, (int)size - 1, &cap, 1, 0);

		if (offset >= 0) {
			su_pushinteger(s, (int)((buffer + offset) - buffer));
			if (cap.len)
				su_pushinteger(s, (int)((cap.ptr + cap.len) - buffer));
			else
				su_pushinteger(s, (int)size - 2);
			su_cons(s);
			return 1;
		} else if (offset < -1) {
			pattern_error(s, offset);
		}
	#endif
	return 0;
}

static int time_(su_state *s, int narg) {
	su_check_arguments(s, 0);
	su_pushnumber(s, (double)time(NULL));
	return 1;
}

static int num_threads(su_state *s, int narg) {
	su_check_num_arguments(s, 0);
	su_pushinteger(s, su_num_threads(s));
	return 1;
}

static int num_cores_(su_state *s, int narg) {
	su_check_num_arguments(s, 0);
	su_pushinteger(s, su_num_cores(s));
	return 1;
}

static int sleep_(su_state *s, int narg) {
	unsigned tmp;
	su_check_arguments(s, 1, SU_NUMBER);
	tmp = (unsigned)su_tonumber(s, -1);
	su_thread_indisposable(s);
	tmp = thread_sleep(tmp);
	su_thread_disposable(s);
	su_pushnumber(s, (double)tmp);
	return 1;
}

static int async(su_state *s, int narg) {
	su_object_type_t t;
	su_assert(s, narg, "Expected at least a function!");
	t = su_type(s, -narg);
	su_assert(s, t == SU_FUNCTION || t == SU_NATIVEFUNC, "Expected function!");
	su_fork(s, narg - 1);
	return 1;
}

static int sync_(su_state *s, int narg) {
	su_object_type_t t;
	su_assert(s, narg > 1, "Expected at least a global-reference and swap function!");
	su_check_type(s, -narg, SU_GLOBAL);
	t = su_type(s, -(--narg));
	su_assert(s, t == SU_FUNCTION || t == SU_NATIVEFUNC, "Expected function!");
	su_transaction(s, --narg);
	return 1;
}

extern void libmath(su_state *s);
extern void libseq(su_state *s);
extern void libhttp(su_state *s);

void su_libinit(su_state *s) {
	int top;
	const char *name = platform_name();
	su_pushstring(s, su_version(NULL, NULL, NULL));
	su_setglobal(s, "_VERSION");

	su_pushfunction(s, &apply);
	su_setglobal(s, "apply");
	su_pushfunction(s, &type);
	su_setglobal(s, "type?");
	su_pushfunction(s, &load);
	su_setglobal(s, "load");
	su_pushfunction(s, &compile);
	su_setglobal(s, "compile");
	su_pushfunction(s, &cat);
	su_setglobal(s, "cat");

	su_pushfunction(s, &error);
	su_setglobal(s, "error");
	su_pushfunction(s, &assert_);
	su_setglobal(s, "assert");

	su_pushfunction(s, &unref);
	su_setglobal(s, "unref");
	su_pushfunction(s, &local_ref);
	su_setglobal(s, "local");
	su_pushfunction(s, &global_ref);
	su_setglobal(s, "global");
	su_pushfunction(s, &set);
	su_setglobal(s, "set");
	
	top = su_top(s);
	
	su_pushstring(s, "async");
	su_pushfunction(s, &async);
	su_pushstring(s, "sync");
	su_pushfunction(s, &sync_);
	su_pushstring(s, "sleep");
	su_pushfunction(s, &sleep_);
	su_pushstring(s, "num_threads");
	su_pushfunction(s, &num_threads);
	su_pushstring(s, "num_cores");
	su_pushfunction(s, &num_cores_);
	
	su_map(s, (su_top(s) - top) / 2);
	su_setglobal(s, "process");
	
	top = su_top(s);
	
	su_pushstring(s, "string!");
	su_pushfunction(s, &string);
	su_pushstring(s, "number!");
	su_pushfunction(s, &number);
	su_pushstring(s, "format");
	su_pushfunction(s, &format);
	su_pushstring(s, "find");
	su_pushfunction(s, &find);
	su_pushstring(s, "byte");
	su_pushfunction(s, &byte_);
	su_pushstring(s, "char");
	su_pushfunction(s, &char_);

	su_map(s, (su_top(s) - top) / 2);
	su_setglobal(s, "string");

	su_pushstring(s, "print");
	su_pushfunction(s, &print);
	su_pushstring(s, "read");
	su_pushfunction(s, &read_);
	su_pushstring(s, "write");
	su_pushfunction(s, &write_);
	su_pushstring(s, "size");
	su_pushfunction(s, &size_);
	su_pushstring(s, "delete");
	su_pushfunction(s, &delete_);
	
	su_map(s, (su_top(s) - top) / 2);
	su_setglobal(s, "io");
	
	su_pushstring(s, "name");
	if (name)
		su_pushstring(s, name);
	else
		su_pushnil(s);
	
	su_pushstring(s, "time");
	su_pushfunction(s, &time_);
	su_pushstring(s, "loadlib");
	su_pushfunction(s, &loadlib);
	su_pushstring(s, "execute");
	su_pushfunction(s, &execute);
	
	su_map(s, (su_top(s) - top) / 2);
	su_setglobal(s, "os");

	libmath(s);
	libseq(s);
	libhttp(s);
}
