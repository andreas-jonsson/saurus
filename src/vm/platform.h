/*
 * S A U R U S
 * Copyright (c) 2009-2015 Andreas T Jonsson <andreas@saurus.org>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef _PLATFORM_H_
#define _PLATFORM_H_

/********** Options ***********/

/* #define SU_OPT_NO_DYNLIB */
/* #define SU_OPT_NO_FILE_IO */
/* #define SU_OPT_NO_PATTERN */
/* #define SU_OPT_NO_SOCKET */

#define SU_OPT_MAX_THREADS 128
#define SU_OPT_GC_OVERHEAD_DIVISOR 4 /* Allow for 25% memory overhead per thread. */

/******************************/

#ifndef false
	#define false 0
#endif

#ifndef true
	#define true 1
#endif

#ifndef bool
	#define bool int
#endif

#if __STDC_VERSION__ >= 199901L
	#define INLINE inline
#else
	#define INLINE
#endif

#if _MSC_VER >= 1500 && _MSC_VER <= 1800 /* VS2008-VS2013 */
	#define snprintf sprintf_s
#endif

#ifndef SU_OPT_NO_PATTERN
	#include "slre.h"
#endif

#if defined(SU_OPT_NO_DYNLIB)
	static INLINE void lib_unload(void *lib) {}

	static INLINE void *lib_load(const char *path) {
		return NULL;
	}

	static INLINE void *lib_sym(void *lib, const char *sym) {
		return NULL;
	}
#elif defined(_WIN32)
	#include <windows.h>

	static INLINE void lib_unload(void *lib) {
		FreeLibrary((HINSTANCE)lib);
	}

	static INLINE void *lib_load(const char *path) {
		return (void*)LoadLibraryA(path);
	}

	static INLINE void *lib_sym(void *lib, const char *sym) {
		return (void*)GetProcAddress((HINSTANCE)lib, sym);
	}
#else
	#include <dlfcn.h>

	static INLINE void lib_unload(void *lib) {
		dlclose(lib);
	}

	static INLINE void *lib_load(const char *path) {
		return dlopen(path, RTLD_NOW);
	}

	static INLINE void *lib_sym(void *lib, const char *sym) {
		return (void*)dlsym(lib, sym);
	}
#endif

static INLINE int bit_count(int x)
{
	#ifdef __GNUC__
		return __builtin_popcount(x);
	#else
		x = (x & 0x55555555) + ((x >> 1) & 0x55555555);
		x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
		x = (x & 0x0f0f0f0f) + ((x >> 4) & 0x0f0f0f0f);
		x = (x & 0x00ff00ff) + ((x >> 8) & 0x00ff00ff);
		x = (x & 0x0000ffff) + ((x >> 16)& 0x0000ffff);
		return x;
	#endif
}

#ifdef __APPLE__
	#include "TargetConditionals.h"
#endif

static INLINE const char *platform_name() {
	#if defined(_WIN32)
		#define OS_NAME "windows";
	#elif defined(__APPLE__)
		#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
			#define OS_NAME "ios"
		#elif TARGET_OS_MAC
			#define OS_NAME "osx"
		#endif
	#elif defined(__ANDROID__)
		#define OS_NAME "android"
	#elif defined(__linux)
		#define OS_NAME "linux"
	#elif defined(__unix)
		#define OS_NAME "unix"
	#elif defined(__posix)
		#define OS_NAME "posix"
	#endif

	#ifdef OS_NAME
		#if defined(__x86_64__) || defined(_M_X64)
			return OS_NAME "_x64";
		#elif defined(__i386__) || defined(_M_IX86)
			return OS_NAME "_x86";
		#elif defined(__ia64__) || defined(_M_IA64)
			return OS_NAME "_ia64";
		#elif defined(__ppc64__)
			return OS_NAME "_ppc64";
		#elif defined(__ppc__) || defined(_M_PPC)
			return OS_NAME "_ppc";
		#elif defined(__aarch64__)
			return OS_NAME "_arm64";
		#elif defined(__arm__) || defined(_M_ARM)
			return OS_NAME "_arm";
		#elif defined(__alpha__) || defined(_M_ALPHA)
			return OS_NAME "_alpha";
		#elif defined(__mips__)
			return OS_NAME "_mips";
		#elif defined(__hppa__)
			return OS_NAME "_hppa";
		#endif
		#undef OS_NAME
	#endif

	return NULL;
}

/* Atomics */

typedef struct {
	int value;
} aint_t;

typedef struct {
	void *value;
} aptr_t;

#define spin_try_lock(a) atomic_cas((a), 0, 1)
#define spin_lock(a) while (!spin_try_lock((a)))
#define spin_unlock(a) atomic_set((a), 0)

#ifdef __GNUC__
	#define rw_barrier() __asm__ __volatile__ ("" : : : "memory")

	#define atomic_set(a, v) __sync_lock_test_and_set(&(a)->value, v)
	#define atomic_add(a, v) __sync_fetch_and_add(&(a)->value, v)
	#define atomic_set_ptr(a, v) __sync_lock_test_and_set(&(a)->value, v)
	#define atomic_cas(a, oldval, newval) __sync_bool_compare_and_swap(&(a)->value, oldval, newval)
	#define atomic_cas_ptr(a, oldval, newval) __sync_bool_compare_and_swap(&(a)->value, oldval, newval)
#elif _MSC_VER
	void _ReadWriteBarrier();
	#pragma intrinsic(_ReadWriteBarrier)
	#define rw_barrier() _ReadWriteBarrier()

	#define atomic_set(a, v)     _InterlockedExchange((long*)&(a)->value, (v))
	#define atomic_add(a, v)     _InterlockedExchangeAdd((long*)&(a)->value, (v))
	#define atomic_set_ptr(a, v)  _InterlockedExchangePointer(&(a)->value, (v))
	#define atomic_cas(a, oldval, newval) (_InterlockedCompareExchange((long*)&(a)->value, (newval), (oldval)) == (oldval))

	#if _M_IX86
		#define atomic_cas_ptr(a, oldval, newval) (_InterlockedCompareExchange((long*)(a), (long)(newval), (long)(oldval)) == (long)(oldval))
	#else
		#define atomic_cas_ptr(a, oldval, newval) (_InterlockedCompareExchangePointer(&(a)->value, (newval), (oldval)) == (oldval))
	#endif
#else
	#error Unknown atomic system!
#endif

static INLINE int atomic_get(aint_t *a)
{
	int value = a->value;
	rw_barrier();
	return value;
}

static INLINE void *atomic_get_ptr(aptr_t *a)
{
	void *value = a->value;
	rw_barrier();
	return value;
}

/* Threading */

#ifdef __GNUC__

	#include <pthread.h>
	#include <time.h>

	static INLINE unsigned thread_sleep(unsigned ms) {
		struct timespec req, rem;
		if (!ms) {
			sched_yield();
		} else {
			req.tv_sec = ms / 1000;
			req.tv_nsec = (ms - req.tv_sec * 1000) * 1000000;
			if (!nanosleep(&req, &rem))
				return 0;
		}
		return (req.tv_sec - rem.tv_sec) * 1000 + (req.tv_nsec - rem.tv_nsec) / 1000000;
	}

	static INLINE int thread_init(void *(*func)(su_state*), void *data) {
		pthread_t handle;
		pthread_attr_t type;
		if (pthread_attr_init(&type) != 0)
			return -1;
		pthread_attr_setdetachstate(&type, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&handle, &type, (void*(*)(void*))func, data) != 0)
			return -1;
		return 0;
	}

#elif _WIN32

	#include <windows.h>

	static INLINE unsigned thread_sleep(unsigned ms) {
		Sleep((DWORD)ms);
		return 0;
	}

	static INLINE int thread_init(void *(*func)(su_state*), void *data) {
		CloseHandle((HANDLE)_beginthread(func, 0, data));
		return 0;
	}

#else
	#error Unknown threading system!
#endif

#ifdef _WIN32
	#include <windows.h>
#elif __APPLE__
	#include <sys/param.h>
	#include <sys/sysctl.h>
#elif __unix
	#include <unistd.h>
#endif

static INLINE int num_cores() {
	#ifdef _WIN32
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		return sysinfo.dwNumberOfProcessors;
	#elif __APPLE__
		int nm[2];
		size_t len = 4;
		uint32_t count;
	
		nm[0] = CTL_HW; nm[1] = HW_AVAILCPU;
		sysctl(nm, 2, &count, &len, NULL, 0);
	
		if(count < 1) {
			nm[1] = HW_NCPU;
			sysctl(nm, 2, &count, &len, NULL, 0);
			if(count < 1)
				count = 1;
		}
		return count;
	#elif __unix
		return sysconf(_SC_NPROCESSORS_ONLN);
	#else
		return -1;
	#endif
}

#endif
