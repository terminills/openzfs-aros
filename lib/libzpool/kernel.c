// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 */

#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#ifndef __AROS__
#include <poll.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <libzutil.h>
#include <sys/crypto/icp.h>
#include <sys/processor.h>
#include <sys/rrwlock.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/zfs_context.h>
#ifdef OZ_NATIVE_DISPATCHER
#include "../../aros/engine/zvol-native.h"
extern void zfs_ereport_taskq_fini(void);
#endif
#include <sys/zfs_onexit.h>
#include <sys/zfs_vfsops.h>
#include <sys/zstd/zstd.h>
#include <sys/zvol.h>
#include <zfs_fletcher.h>
#include <zlib.h>
#ifdef __AROS__
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include "../../aros/engine/device-io.h"
#include "../../aros/engine/partition-io.h"
#include "../../aros/engine/file-scope.h"
/* AROS off_t is checked as 64-bit by the SPL type adapter. */
/* Some native SDK snapshots export these vectors without the declarations. */
extern ssize_t pread(int, void *, size_t, off_t);
extern ssize_t pwrite(int, const void *, size_t, off_t);
#define open64 open
#define pread64 pread
#define OZ_FILE_LOCK(fp) mutex_enter(&(fp)->f_io_lock)
#define OZ_FILE_UNLOCK(fp) do { int saved_errno = errno; \
	mutex_exit(&(fp)->f_io_lock); errno = saved_errno; } while (0)
#else
#define OZ_FILE_LOCK(fp) ((void)0)
#define OZ_FILE_UNLOCK(fp) ((void)0)
#endif
#ifdef __AROS__
#define pwrite64 pwrite
#endif

/*
 * Emulation of kernel services in userland.
 */

uint64_t physmem;
uint32_t hostid;
struct utsname hw_utsname;

/* If set, all blocks read will be copied to the specified directory. */
char *vn_dumpdir = NULL;

/* this only exists to have its address taken */
struct proc p0;

/*
 * =========================================================================
 * threads
 * =========================================================================
 *
 * TS_STACK_MIN is dictated by the minimum allowed pthread stack size.  While
 * TS_STACK_MAX is somewhat arbitrary, it was selected to be large enough for
 * the expected stack depth while small enough to avoid exhausting address
 * space with high thread counts.
 */
#define	TS_STACK_MIN	MAX(PTHREAD_STACK_MIN, 32768)
#define	TS_STACK_MAX	(256 * 1024)

struct zk_thread_wrapper {
	void (*func)(void *);
	void *arg;
};

static void *
zk_thread_wrapper(void *arg)
{
	struct zk_thread_wrapper ztw;
	memcpy(&ztw, arg, sizeof (ztw));
	free(arg);
	ztw.func(ztw.arg);
	return (NULL);
}

static kthread_t *
zk_thread_create_impl(const char *name, void (*func)(void *), void *arg,
    size_t stksize, int state, boolean_t optional)
{
	pthread_attr_t attr;
	pthread_t tid;
	char *stkstr;
	struct zk_thread_wrapper *ztw;
	int detachstate = PTHREAD_CREATE_DETACHED;

	VERIFY0(pthread_attr_init(&attr));

	if (state & TS_JOINABLE)
		detachstate = PTHREAD_CREATE_JOINABLE;

	VERIFY0(pthread_attr_setdetachstate(&attr, detachstate));

	/*
	 * We allow the default stack size in user space to be specified by
	 * setting the ZFS_STACK_SIZE environment variable.  This allows us
	 * the convenience of observing and debugging stack overruns in
	 * user space.  Explicitly specified stack sizes will be honored.
	 * The usage of ZFS_STACK_SIZE is discussed further in the
	 * ENVIRONMENT VARIABLES sections of the ztest(1) man page.
	 */
	if (stksize == 0) {
		stkstr = getenv("ZFS_STACK_SIZE");

		if (stkstr == NULL)
			stksize = TS_STACK_MAX;
		else
			stksize = MAX(atoi(stkstr), TS_STACK_MIN);
	}

	VERIFY3S(stksize, >, 0);
	stksize = P2ROUNDUP(MAX(stksize, TS_STACK_MIN), PAGESIZE);

	/*
	 * If this ever fails, it may be because the stack size is not a
	 * multiple of system page size.
	 */
	VERIFY0(pthread_attr_setstacksize(&attr, stksize));
#ifndef __AROS__
	VERIFY0(pthread_attr_setguardsize(&attr, PAGESIZE));
#endif
	/* AROS pthreads has no guard-page attribute. Stack bounds are checked by
	 * Exec; this is not equivalent to an MMU guard page. Keep explicit sizes. */

	ztw = malloc(sizeof (*ztw));
	if (!ztw && optional) {
		VERIFY0(pthread_attr_destroy(&attr));
		return (NULL);
	}
	VERIFY(ztw != NULL);
	ztw->func = func;
	ztw->arg = arg;
	int error = pthread_create(&tid, &attr, zk_thread_wrapper, ztw);
	if (error && optional) {
		free(ztw);
		VERIFY0(pthread_attr_destroy(&attr));
		return (NULL);
	}
	VERIFY0(error);
	VERIFY0(pthread_attr_destroy(&attr));

	pthread_setname_np(tid, name);

	return ((void *)(uintptr_t)ZFS_THREAD_ENCODE(tid));
}

kthread_t *
zk_thread_create(const char *name, void (*func)(void *), void *arg,
    size_t stksize, int state)
{
	return (zk_thread_create_impl(name, func, arg, stksize, state, B_FALSE));
}
#ifdef __AROS__
kthread_t *
zk_thread_create_optional(const char *name, void (*func)(void *), void *arg,
    size_t stksize, int state)
{
	return (zk_thread_create_impl(name, func, arg, stksize, state, B_TRUE));
}
#endif

/*
 * =========================================================================
 * kstats
 * =========================================================================
 */
kstat_t *
kstat_create(const char *module, int instance, const char *name,
    const char *class, uchar_t type, ulong_t ndata, uchar_t ks_flag)
{
	(void) module, (void) instance, (void) name, (void) class, (void) type,
	    (void) ndata, (void) ks_flag;
	return (NULL);
}

void
kstat_install(kstat_t *ksp)
{
	(void) ksp;
}

void
kstat_delete(kstat_t *ksp)
{
	(void) ksp;
}

void
kstat_set_raw_ops(kstat_t *ksp,
    int (*headers)(char *buf, size_t size),
    int (*data)(char *buf, size_t size, void *data),
    void *(*addr)(kstat_t *ksp, loff_t index))
{
	(void) ksp, (void) headers, (void) data, (void) addr;
}

/*
 * =========================================================================
 * mutexes
 * =========================================================================
 */

void
mutex_init(kmutex_t *mp, char *name, int type, void *cookie)
{
	(void) name, (void) type, (void) cookie;
	VERIFY0(pthread_mutex_init(&mp->m_lock, NULL));
	memset(&mp->m_owner, 0, sizeof (pthread_t));
}

void
mutex_destroy(kmutex_t *mp)
{
	VERIFY0(pthread_mutex_destroy(&mp->m_lock));
}

void
mutex_enter(kmutex_t *mp)
{
	VERIFY0(pthread_mutex_lock(&mp->m_lock));
	mp->m_owner = zfs_thread_self();
}

int
mutex_enter_check_return(kmutex_t *mp)
{
	int error = pthread_mutex_lock(&mp->m_lock);
	if (error == 0)
		mp->m_owner = zfs_thread_self();
	return (error);
}

int
mutex_tryenter(kmutex_t *mp)
{
	int error = pthread_mutex_trylock(&mp->m_lock);
	if (error == 0) {
		mp->m_owner = zfs_thread_self();
		return (1);
	} else {
		VERIFY3S(error, ==, EBUSY);
		return (0);
	}
}

void
mutex_exit(kmutex_t *mp)
{
	memset(&mp->m_owner, 0, sizeof (pthread_t));
	VERIFY0(pthread_mutex_unlock(&mp->m_lock));
}

/*
 * =========================================================================
 * rwlocks
 * =========================================================================
 */

void
rw_init(krwlock_t *rwlp, char *name, int type, void *arg)
{
	(void) name, (void) type, (void) arg;
	VERIFY0(pthread_rwlock_init(&rwlp->rw_lock, NULL));
	rwlp->rw_readers = 0;
	rwlp->rw_owner = 0;
}

void
rw_destroy(krwlock_t *rwlp)
{
	VERIFY0(pthread_rwlock_destroy(&rwlp->rw_lock));
}

void
rw_enter(krwlock_t *rwlp, krw_t rw)
{
	if (rw == RW_READER) {
		VERIFY0(pthread_rwlock_rdlock(&rwlp->rw_lock));
		atomic_inc_uint(&rwlp->rw_readers);
	} else {
		VERIFY0(pthread_rwlock_wrlock(&rwlp->rw_lock));
		rwlp->rw_owner = zfs_thread_self();
	}
}

void
rw_exit(krwlock_t *rwlp)
{
	if (RW_READ_HELD(rwlp))
		atomic_dec_uint(&rwlp->rw_readers);
	else
		rwlp->rw_owner = 0;

	VERIFY0(pthread_rwlock_unlock(&rwlp->rw_lock));
}

int
rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
	int error;

	if (rw == RW_READER)
		error = pthread_rwlock_tryrdlock(&rwlp->rw_lock);
	else
		error = pthread_rwlock_trywrlock(&rwlp->rw_lock);

	if (error == 0) {
		if (rw == RW_READER)
			atomic_inc_uint(&rwlp->rw_readers);
		else
			rwlp->rw_owner = zfs_thread_self();

		return (1);
	}

	VERIFY3S(error, ==, EBUSY);

	return (0);
}

uint32_t
zone_get_hostid(void *zonep)
{
	/*
	 * We're emulating the system's hostid in userland.
	 */
	(void) zonep;
	return (hostid);
}

int
rw_tryupgrade(krwlock_t *rwlp)
{
	(void) rwlp;
	return (0);
}

/*
 * =========================================================================
 * condition variables
 * =========================================================================
 */

void
cv_init(kcondvar_t *cv, char *name, int type, void *arg)
{
	(void) name, (void) type, (void) arg;
	VERIFY0(pthread_cond_init(cv, NULL));
}

void
cv_destroy(kcondvar_t *cv)
{
	VERIFY0(pthread_cond_destroy(cv));
}

void
cv_wait(kcondvar_t *cv, kmutex_t *mp)
{
	memset(&mp->m_owner, 0, sizeof (pthread_t));
	VERIFY0(pthread_cond_wait(cv, &mp->m_lock));
	mp->m_owner = zfs_thread_self();
}

int
cv_wait_sig(kcondvar_t *cv, kmutex_t *mp)
{
	cv_wait(cv, mp);
	return (1);
}

int
cv_timedwait(kcondvar_t *cv, kmutex_t *mp, clock_t abstime)
{
	int error;
	struct timeval tv;
	struct timespec ts;
	clock_t delta;

	delta = abstime - ddi_get_lbolt();
	if (delta <= 0)
		return (-1);

	VERIFY0(gettimeofday(&tv, NULL));

	ts.tv_sec = tv.tv_sec + delta / hz;
	ts.tv_nsec = tv.tv_usec * NSEC_PER_USEC + (delta % hz) * (NANOSEC / hz);
	if (ts.tv_nsec >= NANOSEC) {
		ts.tv_sec++;
		ts.tv_nsec -= NANOSEC;
	}

	memset(&mp->m_owner, 0, sizeof (pthread_t));
	error = pthread_cond_timedwait(cv, &mp->m_lock, &ts);
	mp->m_owner = zfs_thread_self();

	if (error == ETIMEDOUT)
		return (-1);

	VERIFY0(error);

	return (1);
}

int
cv_timedwait_hires(kcondvar_t *cv, kmutex_t *mp, hrtime_t tim, hrtime_t res,
    int flag)
{
	(void) res;
	int error;
	struct timeval tv;
	struct timespec ts;
	hrtime_t delta;

	ASSERT(flag == 0 || flag == CALLOUT_FLAG_ABSOLUTE);

	delta = tim;
	if (flag & CALLOUT_FLAG_ABSOLUTE)
		delta -= gethrtime();

	if (delta <= 0)
		return (-1);

	VERIFY0(gettimeofday(&tv, NULL));

	ts.tv_sec = tv.tv_sec + delta / NANOSEC;
	ts.tv_nsec = tv.tv_usec * NSEC_PER_USEC + (delta % NANOSEC);
	if (ts.tv_nsec >= NANOSEC) {
		ts.tv_sec++;
		ts.tv_nsec -= NANOSEC;
	}

	memset(&mp->m_owner, 0, sizeof (pthread_t));
	error = pthread_cond_timedwait(cv, &mp->m_lock, &ts);
	mp->m_owner = zfs_thread_self();

	if (error == ETIMEDOUT)
		return (-1);

	VERIFY0(error);

	return (1);
}

void
cv_signal(kcondvar_t *cv)
{
	VERIFY0(pthread_cond_signal(cv));
}

void
cv_broadcast(kcondvar_t *cv)
{
	VERIFY0(pthread_cond_broadcast(cv));
}

/*
 * =========================================================================
 * procfs list
 * =========================================================================
 */

void
seq_printf(struct seq_file *m, const char *fmt, ...)
{
	(void) m, (void) fmt;
}

void
procfs_list_install(const char *module,
    const char *submodule,
    const char *name,
    mode_t mode,
    procfs_list_t *procfs_list,
    int (*show)(struct seq_file *f, void *p),
    int (*show_header)(struct seq_file *f),
    int (*clear)(procfs_list_t *procfs_list),
    size_t procfs_list_node_off)
{
	(void) module, (void) submodule, (void) name, (void) mode, (void) show,
	    (void) show_header, (void) clear;
	mutex_init(&procfs_list->pl_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&procfs_list->pl_list,
	    procfs_list_node_off + sizeof (procfs_list_node_t),
	    procfs_list_node_off + offsetof(procfs_list_node_t, pln_link));
	procfs_list->pl_next_id = 1;
	procfs_list->pl_node_offset = procfs_list_node_off;
}

void
procfs_list_uninstall(procfs_list_t *procfs_list)
{
	(void) procfs_list;
}

void
procfs_list_destroy(procfs_list_t *procfs_list)
{
	ASSERT(list_is_empty(&procfs_list->pl_list));
	list_destroy(&procfs_list->pl_list);
	mutex_destroy(&procfs_list->pl_lock);
}

#define	NODE_ID(procfs_list, obj) \
		(((procfs_list_node_t *)(((char *)obj) + \
		(procfs_list)->pl_node_offset))->pln_id)

void
procfs_list_add(procfs_list_t *procfs_list, void *p)
{
	ASSERT(MUTEX_HELD(&procfs_list->pl_lock));
	NODE_ID(procfs_list, p) = procfs_list->pl_next_id++;
	list_insert_tail(&procfs_list->pl_list, p);
}

/*
 * =========================================================================
 * vnode operations
 * =========================================================================
 */

/*
 * =========================================================================
 * Figure out which debugging statements to print
 * =========================================================================
 */

static char *dprintf_string;
static int dprintf_print_all;

int
dprintf_find_string(const char *string)
{
	char *tmp_str = dprintf_string;
	int len = strlen(string);

	/*
	 * Find out if this is a string we want to print.
	 * String format: file1.c,function_name1,file2.c,file3.c
	 */

	while (tmp_str != NULL) {
		if (strncmp(tmp_str, string, len) == 0 &&
		    (tmp_str[len] == ',' || tmp_str[len] == '\0'))
			return (1);
		tmp_str = strchr(tmp_str, ',');
		if (tmp_str != NULL)
			tmp_str++; /* Get rid of , */
	}
	return (0);
}

void
dprintf_setup(int *argc, char **argv)
{
	int i, j;

	/*
	 * Debugging can be specified two ways: by setting the
	 * environment variable ZFS_DEBUG, or by including a
	 * "debug=..."  argument on the command line.  The command
	 * line setting overrides the environment variable.
	 */

	for (i = 1; i < *argc; i++) {
		int len = strlen("debug=");
		/* First look for a command line argument */
		if (strncmp("debug=", argv[i], len) == 0) {
			dprintf_string = argv[i] + len;
			/* Remove from args */
			for (j = i; j < *argc; j++)
				argv[j] = argv[j+1];
			argv[j] = NULL;
			(*argc)--;
		}
	}

	if (dprintf_string == NULL) {
		/* Look for ZFS_DEBUG environment variable */
		dprintf_string = getenv("ZFS_DEBUG");
	}

	/*
	 * Are we just turning on all debugging?
	 */
	if (dprintf_find_string("on"))
		dprintf_print_all = 1;

	if (dprintf_string != NULL)
		zfs_flags |= ZFS_DEBUG_DPRINTF;
}

/*
 * =========================================================================
 * debug printfs
 * =========================================================================
 */
void
__dprintf(boolean_t dprint, const char *file, const char *func,
    int line, const char *fmt, ...)
{
	/* Get rid of annoying "../common/" prefix to filename. */
	const char *newfile = zfs_basename(file);

	va_list adx;
	if (dprint) {
		/* dprintf messages are printed immediately */

		if (!dprintf_print_all &&
		    !dprintf_find_string(newfile) &&
		    !dprintf_find_string(func))
			return;

		/* Print out just the function name if requested */
		flockfile(stdout);
		if (dprintf_find_string("pid"))
			(void) printf("%ld ", (long)getpid());
		if (dprintf_find_string("tid"))
			(void) printf("%llu ",
			    (unsigned long long)(uintptr_t)pthread_self());
		if (dprintf_find_string("cpu"))
			(void) printf("%u ", getcpuid());
		if (dprintf_find_string("time"))
			(void) printf("%llu ", gethrtime());
		if (dprintf_find_string("long"))
			(void) printf("%s, line %d: ", newfile, line);
		(void) printf("dprintf: %s: ", func);
		va_start(adx, fmt);
		(void) vprintf(fmt, adx);
		va_end(adx);
		funlockfile(stdout);
	} else {
		/* zfs_dbgmsg is logged for dumping later */
		size_t size;
		char *buf;
		int i;

		size = 1024;
		buf = umem_alloc(size, UMEM_NOFAIL);
		i = snprintf(buf, size, "%s:%d:%s(): ", newfile, line, func);

		if (i < size) {
			va_start(adx, fmt);
			(void) vsnprintf(buf + i, size - i, fmt, adx);
			va_end(adx);
		}

		__zfs_dbgmsg(buf);

		umem_free(buf, size);
	}
}

/*
 * =========================================================================
 * cmn_err() and panic()
 * =========================================================================
 */

static __attribute__((noreturn)) void
panic_stop_or_abort(void)
{
	const char *stopenv = getenv("LIBZPOOL_PANIC_STOP");
	if (stopenv != NULL && atoi(stopenv)) {
		fputs("libzpool: LIBZPOOL_PANIC_STOP is set, sending "
		    "SIGSTOP to process group\n", stderr);
		fflush(stderr);

		kill(0, SIGSTOP);

		fputs("libzpool: continued after panic stop, "
		    "aborting\n", stderr);
	}

	abort();	/* think of it as a "user-level crash dump" */
}

static void
vcmn_msg(int ce, const char *fmt, va_list adx)
{
	switch (ce) {
	case CE_IGNORE:
		return;
	case CE_CONT:
		break;
	case CE_NOTE:
		fputs("libzpool: NOTICE: ", stderr);
		break;
	case CE_WARN:
		fputs("libzpool: WARNING: ", stderr);
		break;
	case CE_PANIC:
		fputs("libzpool: PANIC: ", stderr);
		break;
	default:
		fputs("libzpool: [unknown severity %d]: ", stderr);
		break;
	}

	vfprintf(stderr, fmt, adx);
	if (ce != CE_CONT)
		fputc('\n', stderr);
	fflush(stderr);
}

void
vcmn_err(int ce, const char *fmt, va_list adx)
{
	vcmn_msg(ce, fmt, adx);

	if (ce == CE_PANIC)
		panic_stop_or_abort();
}

void
cmn_err(int ce, const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_err(ce, fmt, adx);
	va_end(adx);
}

__attribute__((noreturn)) void
panic(const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_msg(CE_PANIC, fmt, adx);
	va_end(adx);

	panic_stop_or_abort();
}

__attribute__((noreturn)) void
vpanic(const char *fmt, va_list adx)
{
	vcmn_msg(CE_PANIC, fmt, adx);
	panic_stop_or_abort();
}

/*
 * =========================================================================
 * misc routines
 * =========================================================================
 */

void
delay(clock_t ticks)
{
#ifdef __AROS__
	if (ticks <= 0)
		return;
	struct timespec req = { ticks / hz,
	    (ticks % hz) * (1000000000L / hz) };
	while (nanosleep(&req, &req) == -1 && errno == EINTR) {}
#else
	(void) poll(0, 0, ticks * (1000 / hz));
#endif
}

/*
 * Find highest one bit set.
 * Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 * The __builtin_clzll() function is supported by both GCC and Clang.
 */
int
highbit64(uint64_t i)
{
	if (i == 0)
	return (0);

	return (NBBY * sizeof (uint64_t) - __builtin_clzll(i));
}

/*
 * Find lowest one bit set.
 * Returns bit number + 1 of lowest bit that is set, otherwise returns 0.
 * The __builtin_ffsll() function is supported by both GCC and Clang.
 */
int
lowbit64(uint64_t i)
{
	if (i == 0)
		return (0);

	return (__builtin_ffsll(i));
}

#ifdef __AROS__
/* getentropy uses AROS entropy.resource; no fake /dev nodes or weak PRNG. */
void random_init(void) {}
void random_fini(void) {}
int random_get_bytes(uint8_t *ptr, size_t len)
{
	while (len) {
		size_t n = MIN(len, 256);
		if (getentropy(ptr, n) != 0)
			return (errno);
		ptr += n;
		len -= n;
	}
	return (0);
}
int random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes(ptr, len));
}
#else
const char *random_path = "/dev/random";
const char *urandom_path = "/dev/urandom";
static int random_fd = -1, urandom_fd = -1;

void
random_init(void)
{
	VERIFY((random_fd = open(random_path, O_RDONLY | O_CLOEXEC)) != -1);
	VERIFY((urandom_fd = open(urandom_path, O_RDONLY | O_CLOEXEC)) != -1);
}

void
random_fini(void)
{
	close(random_fd);
	close(urandom_fd);

	random_fd = -1;
	urandom_fd = -1;
}

static int
random_get_bytes_common(uint8_t *ptr, size_t len, int fd)
{
	size_t resid = len;
	ssize_t bytes;

	ASSERT(fd != -1);

	while (resid != 0) {
		bytes = read(fd, ptr, resid);
		ASSERT3S(bytes, >=, 0);
		ptr += bytes;
		resid -= bytes;
	}

	return (0);
}

int
random_get_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes_common(ptr, len, random_fd));
}

int
random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes_common(ptr, len, urandom_fd));
}
#endif

int
ddi_strtoull(const char *str, char **nptr, int base, u_longlong_t *result)
{
	errno = 0;
	*result = strtoull(str, nptr, base);
	if (*result == 0)
		return (errno);
	return (0);
}

utsname_t *
utsname(void)
{
	return (&hw_utsname);
}

/*
 * =========================================================================
 * kernel emulation setup & teardown
 * =========================================================================
 */
static int
umem_out_of_memory(void)
{
	char errmsg[] = "out of memory -- generating core dump\n";

	(void) fprintf(stderr, "%s", errmsg);
	abort();
	return (0);
}

static void
spa_config_load(void)
{
	void *buf = NULL;
	nvlist_t *nvlist, *child;
	nvpair_t *nvpair;
	char *pathname;
	zfs_file_t *fp;
	zfs_file_attr_t zfa;
	uint64_t fsize;
	int err;

	/*
	 * Open the configuration file.
	 */
	pathname = kmem_alloc(MAXPATHLEN, KM_SLEEP);

	(void) snprintf(pathname, MAXPATHLEN, "%s", spa_config_path);

	err = zfs_file_open(pathname, O_RDONLY, 0, kcred, &fp);
	if (err)
		err = zfs_file_open(ZPOOL_CACHE_BOOT, O_RDONLY, 0, kcred, &fp);

	kmem_free(pathname, MAXPATHLEN);

	if (err)
		return;

	if (zfs_file_getattr(fp, &zfa))
		goto out;

	fsize = zfa.zfa_size;
	buf = kmem_alloc(fsize, KM_SLEEP);

	/*
	 * Read the nvlist from the file.
	 */
	if (zfs_file_read(fp, buf, fsize, NULL) < 0)
		goto out;

	/*
	 * Unpack the nvlist.
	 */
	if (nvlist_unpack(buf, fsize, &nvlist, KM_SLEEP) != 0)
		goto out;

	/*
	 * Iterate over all elements in the nvlist, creating a new spa_t for
	 * each one with the specified configuration.
	 */
	spa_namespace_enter(FTAG);
	nvpair = NULL;
	while ((nvpair = nvlist_next_nvpair(nvlist, nvpair)) != NULL) {
		if (nvpair_type(nvpair) != DATA_TYPE_NVLIST)
			continue;

		child = fnvpair_value_nvlist(nvpair);

		if (spa_lookup(nvpair_name(nvpair)) != NULL)
			continue;
		(void) spa_add(nvpair_name(nvpair), child, NULL);
	}
	spa_namespace_exit(FTAG);

	nvlist_free(nvlist);

out:
	if (buf != NULL)
		kmem_free(buf, fsize);

	zfs_file_close(fp);
}

void
kernel_init(int mode)
{
	extern uint_t rrw_tsd_key;

	umem_nofail_callback(umem_out_of_memory);

#ifdef __AROS__
	physmem = AvailMem(MEMF_TOTAL) / PAGESIZE;
#else
	physmem = sysconf(_SC_PHYS_PAGES);
#endif

	dprintf("physmem = %llu pages (%.2f GB)\n", (u_longlong_t)physmem,
	    (double)physmem * sysconf(_SC_PAGE_SIZE) / (1ULL << 30));

	hostid = (mode & SPA_MODE_WRITE) ? get_system_hostid() : 0;

	random_init();

	VERIFY0(uname(&hw_utsname));

	system_taskq_init();
	icp_init();

	zstd_init();

#ifdef OZ_NATIVE_DISPATCHER
	VERIFY0(zvol_init());
#endif
	spa_init((spa_mode_t)mode);
	spa_config_load();

	fletcher_4_init();

	tsd_create(&rrw_tsd_key, rrw_tsd_destroy);
}

void
kernel_fini(void)
{
#ifdef __AROS__
	const boolean_t trace = getenv("OPENZFS_BOOT_TRACE") != NULL;
#define AROS_FINI_TRACE(stage) do { if (trace) { \
	(void) printf("OpenZFS shutdown: %s\n", stage); \
	(void) fflush(stdout); } } while (0)
#else
#define AROS_FINI_TRACE(stage) ((void)0)
#endif
	AROS_FINI_TRACE("fletcher");
	fletcher_4_fini();
	AROS_FINI_TRACE("spa");
#ifdef OZ_NATIVE_DISPATCHER
	zfs_ereport_taskq_fini();
#endif
	spa_fini();
#ifdef OZ_NATIVE_DISPATCHER
	zvol_fini();
#endif

	AROS_FINI_TRACE("zstd");
	zstd_fini();

	AROS_FINI_TRACE("icp");
	icp_fini();
	AROS_FINI_TRACE("system taskq");
	system_taskq_fini();

	AROS_FINI_TRACE("random");
	random_fini();
#undef AROS_FINI_TRACE
}

#ifndef __AROS__
uid_t
crgetuid(cred_t *cr)
{
	(void) cr;
	return (0);
}

uid_t
crgetruid(cred_t *cr)
{
	(void) cr;
	return (0);
}

gid_t
crgetgid(cred_t *cr)
{
	(void) cr;
	return (0);
}

int
crgetngroups(cred_t *cr)
{
	(void) cr;
	return (0);
}

gid_t *
crgetgroups(cred_t *cr)
{
	(void) cr;
	return (NULL);
}
#endif

#ifndef __AROS__
int
zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr)
{
	(void) name, (void) cr;
	return (0);
}

int
zfs_secpolicy_rename_perms(const char *from, const char *to, cred_t *cr)
{
	(void) from, (void) to, (void) cr;
	return (0);
}

int
zfs_secpolicy_destroy_perms(const char *name, cred_t *cr)
{
	(void) name, (void) cr;
	return (0);
}

int
secpolicy_zfs(const cred_t *cr)
{
	(void) cr;
	return (0);
}
#endif

ksiddomain_t *
ksid_lookupdomain(const char *dom)
{
	ksiddomain_t *kd;

	kd = umem_zalloc(sizeof (ksiddomain_t), UMEM_NOFAIL);
	kd->kd_name = spa_strdup(dom);
	return (kd);
}

void
ksiddomain_rele(ksiddomain_t *ksid)
{
	spa_strfree(ksid->kd_name);
	umem_free(ksid, sizeof (ksiddomain_t));
}

char *
kmem_vasprintf(const char *fmt, va_list adx)
{
	char *buf = NULL;
	va_list adx_copy;

	va_copy(adx_copy, adx);
	VERIFY(vasprintf(&buf, fmt, adx_copy) != -1);
	va_end(adx_copy);

	return (buf);
}

char *
kmem_asprintf(const char *fmt, ...)
{
	char *buf = NULL;
	va_list adx;

	va_start(adx, fmt);
	VERIFY(vasprintf(&buf, fmt, adx) != -1);
	va_end(adx);

	return (buf);
}

/*
 * kmem_scnprintf() will return the number of characters that it would have
 * printed whenever it is limited by value of the size variable, rather than
 * the number of characters that it did print. This can cause misbehavior on
 * subsequent uses of the return value, so we define a safe version that will
 * return the number of characters actually printed, minus the NULL format
 * character.  Subsequent use of this by the safe string functions is safe
 * whether it is snprintf(), strlcat() or strlcpy().
 */
int
kmem_scnprintf(char *restrict str, size_t size, const char *restrict fmt, ...)
{
	int n;
	va_list ap;

	/* Make the 0 case a no-op so that we do not return -1 */
	if (size == 0)
		return (0);

	va_start(ap, fmt);
	n = vsnprintf(str, size, fmt, ap);
	va_end(ap);

	if (n >= size)
		n = size - 1;

	return (n);
}

#ifndef OZ_NATIVE_ONEXIT
zfs_file_t *
zfs_onexit_fd_hold(int fd, minor_t *minorp)
{
	(void) fd;
	*minorp = 0;
	return (NULL);
}

void
zfs_onexit_fd_rele(zfs_file_t *fp)
{
	(void) fp;
}

int
zfs_onexit_add_cb(minor_t minor, void (*func)(void *), void *data,
    uintptr_t *action_handle)
{
	(void) minor, (void) func, (void) data, (void) action_handle;
	return (0);
}
#endif /* diagnostic on-exit substitutes */

fstrans_cookie_t
spl_fstrans_mark(void)
{
	return ((fstrans_cookie_t)0);
}

void
spl_fstrans_unmark(fstrans_cookie_t cookie)
{
	(void) cookie;
}

int
kmem_cache_reap_active(void)
{
	return (0);
}

#ifndef OZ_NATIVE_DISPATCHER
void
zvol_create_minors(const char *name)
{
	(void) name;
}

void
zvol_remove_minors(spa_t *spa, const char *name, boolean_t async)
{
	(void) spa, (void) name, (void) async;
}

void
zvol_rename_minors(spa_t *spa, const char *oldname, const char *newname,
    boolean_t async)
{
	(void) spa, (void) oldname, (void) newname, (void) async;
}
#endif /* diagnostic zvol substitutes; real dispatcher uses upstream zvol.c */

/*
 * Open file
 *
 * path - fully qualified path to file
 * flags - file attributes O_READ / O_WRITE / O_EXCL
 * fpp - pointer to return file pointer
 *
 * Returns 0 on success underlying error on failure.
 */
int
zfs_file_open(const char *path, int flags, int mode, cred_t *cr,
    zfs_file_t **fpp)
{
	(void) cr;
	int fd;
	int dump_fd;
	int err;
	int old_umask = 0;
	zfs_file_t *fp;
	struct stat64 st;

	*fpp = NULL;
#ifdef OZ_NATIVE_DISPATCHER
	/* Resident-engine volume namespace, not a DOS assign or /dev node. */
	if (strncmp(path, "ZVOL:", 5) == 0) {
		if (flags & (O_CREAT | O_TRUNC)) return (EINVAL);
		struct oz_zvol_handle *volume = NULL;
		err = oz_zvol_open(path + 5, (flags & O_ACCMODE) != O_RDONLY, cr, &volume);
		if (err) return (err);
		fp = umem_zalloc(sizeof (*fp), UMEM_NOFAIL);
		fp->f_fd = fp->f_dump_fd = -1;
		fp->f_zvol = volume; fp->f_refs = 1;
		mutex_init(&fp->f_io_lock, NULL, MUTEX_DEFAULT, NULL);
		*fpp = fp;
		return (0);
	}
#endif
#ifdef __AROS__
	/* Prefer the exact retained extent admitted by the engine. The fallback
	 * below remains restricted to explicitly provisioned QEMU fixtures. */
	if (strncmp(path, "/AROS/", 6) == 0) {
		struct oz_device *bound = NULL;
		if (flags & (O_CREAT | O_TRUNC)) return (EINVAL);
		err = oz_device_bound_open(path,
		    (flags & O_ACCMODE) != O_RDONLY, &bound);
		if (err == 0) {
			err = oz_file_from_device(bound, fpp);
			if (err) oz_device_close(bound);
			return (err);
		}
		if (err != ENOENT) return (err);
		const char *permit = getenv("OPENZFS_QEMU_DEVICE_IO");
		unsigned unit;
		int partition = 0;
		struct oz_device *device;
		if (permit == NULL || strcmp(permit, "1") != 0)
			return (EACCES);
		if (strcmp(path, "/AROS/ata.device/0") == 0)
			unit = 0;
		else if (strcmp(path, "/AROS/ata.device/1") == 0)
			unit = 1;
		else if (strcmp(path, "/AROS/ata.device/0/partition/0") == 0)
			unit = 0, partition = 1;
		else if (strcmp(path, "/AROS/ata.device/1/partition/0") == 0)
			unit = 1, partition = 1;
		else
			return (ENOTSUP);
		if (flags & (O_CREAT | O_TRUNC))
			return (EINVAL);
		err = oz_device_open("ata.device", unit,
		    (flags & O_ACCMODE) != O_RDONLY, &device);
		if (err != 0)
			return (err);
		if (partition) {
			/* Additional disposable fixture only: exact enclosing capacity,
			 * then real native-table resolution. No general raw admission. */
			uint64_t physical_size = oz_device_size(device);
			oz_device_close(device);
			if (physical_size != 512ULL * 1024 * 1024)
				return (EINVAL);
			err = oz_device_open_partition("ata.device", unit, 0,
			    (flags & O_ACCMODE) != O_RDONLY, &device);
			if (err != 0)
				return (err);
		}
		/* Only the two explicitly provisioned disposable fixture sizes.
		 * The larger fixture exercises native offsets above 4 GiB. */
		if (oz_device_size(device) != 256ULL * 1024 * 1024 &&
		    oz_device_size(device) != 8ULL * 1024 * 1024 * 1024) {
			oz_device_close(device);
			return (EINVAL);
		}
		fp = umem_zalloc(sizeof (*fp), UMEM_NOFAIL);
		fp->f_fd = fp->f_dump_fd = -1;
		fp->f_device = device;
		fp->f_refs = 1;
		mutex_init(&fp->f_io_lock, NULL, MUTEX_DEFAULT, NULL);
		*fpp = fp;
		return (0);
	}
#endif
	if (!(flags & O_CREAT) && stat64(path, &st) == -1)
		return (errno != 0 ? errno : EIO);

#ifdef __AROS__
	if (!(flags & O_CREAT) && !S_ISREG(st.st_mode))
		return (ENOTSUP);
#else
	if (!(flags & O_CREAT) && S_ISBLK(st.st_mode))
		flags |= O_DIRECT;
#endif

	if (flags & O_CREAT)
		old_umask = umask(0);

	fd = open64(path, flags, mode);
	if (flags & O_CREAT)
		(void) umask(old_umask);
	if (fd == -1)
		return (errno != 0 ? errno : EIO);

#ifdef __AROS__
	/* Validate the opened object too, including O_CREAT and path races. */
	if (fstat64(fd, &st) == -1 || !S_ISREG(st.st_mode)) {
		err = errno != 0 ? errno : ENOTSUP;
		close(fd);
		return (err);
	}
#endif

	if (vn_dumpdir != NULL) {
		char *dumppath = umem_zalloc(MAXPATHLEN, UMEM_NOFAIL);
		const char *inpath = zfs_basename(path);

		(void) snprintf(dumppath, MAXPATHLEN,
		    "%s/%s", vn_dumpdir, inpath);
		dump_fd = open64(dumppath, O_CREAT | O_WRONLY, 0666);
		umem_free(dumppath, MAXPATHLEN);
		if (dump_fd == -1) {
			err = errno != 0 ? errno : EIO;
			close(fd);
			return (err);
		}
	} else {
		dump_fd = -1;
	}

	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);

	fp = umem_zalloc(sizeof (zfs_file_t), UMEM_NOFAIL);
	fp->f_fd = fd;
	fp->f_dump_fd = dump_fd;
#ifdef __AROS__
	fp->f_refs = 1;
	mutex_init(&fp->f_io_lock, NULL, MUTEX_DEFAULT, NULL);
#endif
	*fpp = fp;

	return (0);
}

#ifdef __AROS__
int
oz_file_from_device(struct oz_device *device, zfs_file_t **out)
{
	if (out == NULL) return (EINVAL);
	*out = NULL;
	if (device == NULL) return (EINVAL);
	zfs_file_t *fp = umem_zalloc(sizeof (*fp), UMEM_NOFAIL);
	fp->f_fd = fp->f_dump_fd = -1;
	fp->f_device = device;
	fp->f_refs = 1;
	mutex_init(&fp->f_io_lock, NULL, MUTEX_DEFAULT, NULL);
	*out = fp;
	return (0);
}

int
zfs_file_from_reader(int (*reader)(void *, void *, size_t, size_t *),
    void *arg, zfs_file_t **out)
{
	if (out == NULL)
		return (EINVAL);
	*out = NULL;
	if (reader == NULL || arg == NULL)
		return (EINVAL);
	zfs_file_t *fp = umem_zalloc(sizeof (*fp), UMEM_NOFAIL);
	fp->f_fd = fp->f_dump_fd = -1;
	fp->f_reader = reader;
	fp->f_reader_arg = arg;
	fp->f_refs = 1;
	mutex_init(&fp->f_io_lock, NULL, MUTEX_DEFAULT, NULL);
	*out = fp;
	return (0);
}
int
zfs_file_from_writer(int (*writer)(void *, const void *, size_t, size_t *),
    void *arg, zfs_file_t **out)
{
	if (out == NULL)
		return (EINVAL);
	*out = NULL;
	if (writer == NULL || arg == NULL)
		return (EINVAL);
	zfs_file_t *fp = umem_zalloc(sizeof (*fp), UMEM_NOFAIL);
	fp->f_fd = fp->f_dump_fd = -1;
	fp->f_writer = writer;
	fp->f_writer_arg = arg;
	fp->f_refs = 1;
	mutex_init(&fp->f_io_lock, NULL, MUTEX_DEFAULT, NULL);
	*out = fp;
	return (0);
}
int oz_file_hold(zfs_file_t *fp)
{
	if (!fp) return EINVAL;
	mutex_enter(&fp->f_io_lock);
	ASSERT3U(fp->f_refs, >, 0);
	int error = fp->f_refs == SIZE_MAX ? EOVERFLOW : 0;
	if (!error) ++fp->f_refs;
	mutex_exit(&fp->f_io_lock);
	return error;
}
int oz_file_set_private(zfs_file_t *fp, void *state, void (*release)(void *))
{
	if (!fp || !state || !release) return EINVAL;
	mutex_enter(&fp->f_io_lock);
	int error = fp->f_refs != 1 || fp->f_private ? EBUSY : 0;
	if (!error) { fp->f_private = state; fp->f_private_release = release; }
	mutex_exit(&fp->f_io_lock);
	return error;
}
void *zfs_file_private(zfs_file_t *fp)
{ return fp ? fp->f_private : NULL; }
#endif

void
zfs_file_close(zfs_file_t *fp)
{
#ifdef __AROS__
	mutex_enter(&fp->f_io_lock);
	ASSERT3U(fp->f_refs, >, 0);
	boolean_t last = --fp->f_refs == 0;
	mutex_exit(&fp->f_io_lock);
	if (!last) return;
	if (fp->f_private_release) fp->f_private_release(fp->f_private);
#ifdef OZ_NATIVE_DISPATCHER
	if (fp->f_zvol != NULL) {
		oz_zvol_close(fp->f_zvol);
		mutex_destroy(&fp->f_io_lock);
		umem_free(fp, sizeof (*fp));
		return;
	}
#endif
	if (fp->f_reader != NULL || fp->f_writer != NULL) {
		mutex_destroy(&fp->f_io_lock);
		umem_free(fp, sizeof (*fp));
		return;
	}
	if (fp->f_device != NULL) {
		oz_device_close(fp->f_device);
		mutex_destroy(&fp->f_io_lock);
		umem_free(fp, sizeof (*fp));
		return;
	}
#endif
	close(fp->f_fd);
	if (fp->f_dump_fd != -1)
		close(fp->f_dump_fd);
#ifdef __AROS__
	mutex_destroy(&fp->f_io_lock);
#endif

	umem_free(fp, sizeof (zfs_file_t));
}

/*
 * Stateful write - use os internal file pointer to determine where to
 * write and update on successful completion.
 *
 * fp -  pointer to file (pipe, socket, etc) to write to
 * buf - buffer to write
 * count - # of bytes to write
 * resid -  pointer to count of unwritten bytes  (if short write)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_write(zfs_file_t *fp, const void *buf, size_t count, ssize_t *resid)
{
#ifdef OZ_NATIVE_DISPATCHER
	if (fp->f_zvol != NULL) return (ESPIPE);
#endif
#ifdef __AROS__
	if (fp->f_writer != NULL) {
		size_t actual = 0;
		if (count > SSIZE_MAX)
			return (EINVAL);
		if (resid != NULL)
			*resid = count;
		OZ_FILE_LOCK(fp);
		if (count > INT64_MAX - fp->f_stream_offset) {
			OZ_FILE_UNLOCK(fp);
			return (EOVERFLOW);
		}
		int error = fp->f_writer(fp->f_writer_arg, buf, count, &actual);
		if (actual <= count)
			fp->f_stream_offset += actual;
		OZ_FILE_UNLOCK(fp);
		if (actual > count)
			return (EIO);
		if (resid != NULL)
			*resid = count - actual;
		return (error != 0 ? error :
		    (resid == NULL && actual != count ? EIO : 0));
	}
	if (fp->f_reader != NULL)
		return (EBADF);
	if (fp->f_device != NULL)
		return (ENOTSUP);
#endif
	ssize_t rc;

	OZ_FILE_LOCK(fp);
	rc = write(fp->f_fd, buf, count);
	OZ_FILE_UNLOCK(fp);
	if (rc < 0)
		return (errno != 0 ? errno : EIO);

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * Stateless write - os internal file pointer is not updated.
 *
 * fp -  pointer to file (pipe, socket, etc) to write to
 * buf - buffer to write
 * count - # of bytes to write
 * off - file offset to write to (only valid for seekable types)
 * resid -  pointer to count of unwritten bytes
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_pwrite(zfs_file_t *fp, const void *buf,
    size_t count, loff_t pos, uint8_t ashift, ssize_t *resid)
{
#ifdef OZ_NATIVE_DISPATCHER
	if (fp->f_zvol != NULL) {
		size_t done = 0;
		if (count > SSIZE_MAX) return (EINVAL);
		int error = pos < 0 ? EINVAL : oz_zvol_io(fp->f_zvol, pos,
		    (void *)buf, count, B_TRUE, &done);
		if (resid) *resid = count - done;
		return (error ? error : !resid && done != count ? EIO : 0);
	}
#endif
#ifdef __AROS__
	if (fp->f_writer != NULL)
		return (ESPIPE);
	if (fp->f_reader != NULL)
		return (EBADF);
	if (fp->f_device != NULL) {
		int error = pos < 0 ? EINVAL : oz_device_transfer(fp->f_device,
		    (void *)buf, count, pos, 1);
		if (resid != NULL)
			*resid = error ? count : 0;
		return (error);
	}
#endif
	ssize_t rc, split, done;
	int sectors;

	/*
	 * To simulate partial disk writes, we split writes into two
	 * system calls so that the process can be killed in between.
	 * This is used by ztest to simulate realistic failure modes.
	 */
	sectors = count >> ashift;
	split = (sectors > 0 ? rand() % sectors : 0) << ashift;
	OZ_FILE_LOCK(fp);
	rc = pwrite64(fp->f_fd, buf, split, pos);
	if (rc != -1) {
		done = rc;
		rc = pwrite64(fp->f_fd, (char *)buf + split,
		    count - split, pos + split);
	}
	OZ_FILE_UNLOCK(fp);
#ifdef __linux__
	if (rc == -1 && errno == EINVAL) {
		/*
		 * Under Linux, this most likely means an alignment issue
		 * (memory or disk) due to O_DIRECT, so we abort() in order
		 * to catch the offender.
		 */
		abort();
	}
#endif

	if (rc < 0)
		/* Some AROS POSIX failures currently leave errno unset. */
		return (errno != 0 ? errno : EIO);

	done += rc;

	if (resid) {
		*resid = count - done;
	} else if (done != count) {
		return (EIO);
	}

	return (0);
}

/*
 * Stateful read - use os internal file pointer to determine where to
 * read and update on successful completion.
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * buf - buffer to write
 * count - # of bytes to read
 * resid -  pointer to count of unread bytes (if short read)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_read(zfs_file_t *fp, void *buf, size_t count, ssize_t *resid)
{
#ifdef OZ_NATIVE_DISPATCHER
	if (fp->f_zvol != NULL) return (ESPIPE);
#endif
#ifdef __AROS__
	if (fp->f_writer != NULL)
		return (EBADF);
	if (fp->f_reader != NULL) {
		size_t actual = 0;
		if (count > SSIZE_MAX)
			return (EINVAL);
		if (resid != NULL)
			*resid = count;
		OZ_FILE_LOCK(fp);
		if (count > INT64_MAX - fp->f_stream_offset) {
			OZ_FILE_UNLOCK(fp);
			return (EOVERFLOW);
		}
		int error = fp->f_reader(fp->f_reader_arg, buf, count, &actual);
		if (actual <= count)
			fp->f_stream_offset += actual;
		OZ_FILE_UNLOCK(fp);
		if (actual > count)
			return (EIO);
		if (resid != NULL)
			*resid = count - actual;
		return (error != 0 ? error :
		    (resid == NULL && actual != count ? EIO : 0));
	}
	if (fp->f_device != NULL)
		return (ENOTSUP);
#endif
	int rc;

	OZ_FILE_LOCK(fp);
	rc = read(fp->f_fd, buf, count);
	OZ_FILE_UNLOCK(fp);
	if (rc < 0)
		return (errno != 0 ? errno : EIO);

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * Stateless read - os internal file pointer is not updated.
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * buf - buffer to write
 * count - # of bytes to write
 * off - file offset to read from (only valid for seekable types)
 * resid -  pointer to count of unwritten bytes (if short write)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_pread(zfs_file_t *fp, void *buf, size_t count, loff_t off,
    ssize_t *resid)
{
#ifdef OZ_NATIVE_DISPATCHER
	if (fp->f_zvol != NULL) {
		size_t done = 0;
		if (count > SSIZE_MAX) return (EINVAL);
		int error = off < 0 ? EINVAL : oz_zvol_io(fp->f_zvol, off,
		    buf, count, B_FALSE, &done);
		if (resid) *resid = count - done;
		return (error ? error : !resid && done != count ? EIO : 0);
	}
#endif
#ifdef __AROS__
	if (fp->f_writer != NULL)
		return (EBADF);
	if (fp->f_reader != NULL)
		return (ESPIPE);
	if (fp->f_device != NULL) {
		int error = off < 0 ? EINVAL : oz_device_transfer(fp->f_device,
		    buf, count, off, 0);
		if (resid != NULL)
			*resid = error ? count : 0;
		return (error);
	}
#endif
	ssize_t rc;

	OZ_FILE_LOCK(fp);
	rc = pread64(fp->f_fd, buf, count, off);
	OZ_FILE_UNLOCK(fp);
	if (rc < 0) {
#ifdef __linux__
		/*
		 * Under Linux, this most likely means an alignment issue
		 * (memory or disk) due to O_DIRECT, so we abort() in order to
		 * catch the offender.
		 */
		if (errno == EINVAL)
			abort();
#endif
		/* A failed read must never become a successful ZFS I/O. */
		return (errno != 0 ? errno : EIO);
	}

	if (fp->f_dump_fd != -1) {
		int status;

		OZ_FILE_LOCK(fp);
		status = pwrite64(fp->f_dump_fd, buf, rc, off);
		OZ_FILE_UNLOCK(fp);
		ASSERT(status != -1);
	}

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * lseek - set / get file pointer
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * offp - value to seek to, returns current value plus passed offset
 * whence - see man pages for standard lseek whence values
 *
 * Returns 0 on success errno on failure (ESPIPE for non seekable types)
 */
int
zfs_file_seek(zfs_file_t *fp, loff_t *offp, int whence)
{
#ifdef OZ_NATIVE_DISPATCHER
	if (fp->f_zvol != NULL) return (ESPIPE);
#endif
#ifdef __AROS__
	if (fp->f_reader != NULL || fp->f_writer != NULL)
		return (ESPIPE);
	if (fp->f_device != NULL)
		return (ESPIPE);
#endif
	loff_t rc;

	OZ_FILE_LOCK(fp);
	rc = lseek(fp->f_fd, *offp, whence);
	OZ_FILE_UNLOCK(fp);
	if (rc < 0)
		return (errno != 0 ? errno : EIO);

	*offp = rc;

	return (0);
}

/*
 * Get file attributes
 *
 * filp - file pointer
 * zfattr - pointer to file attr structure
 *
 * Currently only used for fetching size and file mode
 *
 * Returns 0 on success or error code of underlying getattr call on failure.
 */
int
zfs_file_getattr(zfs_file_t *fp, zfs_file_attr_t *zfattr)
{
#ifdef OZ_NATIVE_DISPATCHER
	if (fp->f_zvol != NULL) {
		uint64_t block; boolean_t readonly;
		int error = oz_zvol_info(fp->f_zvol, &zfattr->zfa_size, &block, &readonly);
		if (!error) zfattr->zfa_mode = S_IFBLK;
		return (error);
	}
#endif
#ifdef __AROS__
	if (fp->f_reader != NULL || fp->f_writer != NULL)
		return (ENOTSUP);
	if (fp->f_device != NULL) {
		zfattr->zfa_size = oz_device_size(fp->f_device);
		zfattr->zfa_mode = S_IFBLK;
		return (0);
	}
#endif
	struct stat64 st;

	OZ_FILE_LOCK(fp);
	int status = fstat64_blk(fp->f_fd, &st);
	OZ_FILE_UNLOCK(fp);
	if (status == -1)
		return (errno != 0 ? errno : EIO);

	zfattr->zfa_size = st.st_size;
	zfattr->zfa_mode = st.st_mode;

	return (0);
}

/*
 * Sync file to disk
 *
 * filp - file pointer
 * flags - O_SYNC and or O_DSYNC
 *
 * Returns 0 on success or error code of underlying sync call on failure.
 */
int
zfs_file_fsync(zfs_file_t *fp, int flags)
{
	(void) flags;
#ifdef OZ_NATIVE_DISPATCHER
	if (fp->f_zvol != NULL) return (oz_zvol_flush(fp->f_zvol));
#endif
#ifdef __AROS__
	if (fp->f_reader != NULL || fp->f_writer != NULL)
		return (ENOTSUP);
	if (fp->f_device != NULL)
		return (oz_device_flush(fp->f_device));
#endif

	OZ_FILE_LOCK(fp);
	int status = fsync(fp->f_fd);
	int error = status < 0 ? (errno != 0 ? errno : EIO) : 0;
	OZ_FILE_UNLOCK(fp);
#ifdef __AROS__
	/* Buffered file backing cannot use zio's ENOTSUP/no-write-cache
	 * fallback: it may still hold acknowledged writes in handler memory. */
	if (error == ENOTSUP || error == ENOSYS)
		error = EIO;
#endif

	return (error);
}

/*
 * deallocate - zero and/or deallocate file storage
 *
 * fp - file pointer
 * offset - offset to start zeroing or deallocating
 * len - length to zero or deallocate
 */
int
zfs_file_deallocate(zfs_file_t *fp, loff_t offset, loff_t len)
{
	int rc;
#if defined(__linux__)
	rc = fallocate(fp->f_fd,
	    FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, len);
#elif defined(__FreeBSD__) && (__FreeBSD_version >= 1400029)
	struct spacectl_range rqsr = {
		.r_offset = offset,
		.r_len = len,
	};
	rc = fspacectl(fp->f_fd, SPACECTL_DEALLOC, &rqsr, 0, &rqsr);
#else
	(void) fp, (void) offset, (void) len;
	rc = EOPNOTSUPP;
#endif
	if (rc)
		return (SET_ERROR(rc));
	return (0);
}

/*
 * Request current file pointer offset
 *
 * fp - pointer to file
 *
 * Returns current file offset.
 */
loff_t
zfs_file_off(zfs_file_t *fp)
{
#ifdef OZ_NATIVE_DISPATCHER
	if (fp->f_zvol != NULL) { errno = ESPIPE; return (-1); }
#endif
#ifdef __AROS__
	if (fp->f_reader != NULL || fp->f_writer != NULL) {
		OZ_FILE_LOCK(fp);
		loff_t offset = fp->f_stream_offset;
		OZ_FILE_UNLOCK(fp);
		return (offset);
	}
	if (fp->f_device != NULL) {
		errno = ESPIPE;
		return (-1);
	}
#endif
	OZ_FILE_LOCK(fp);
	loff_t offset = lseek(fp->f_fd, 0, SEEK_CUR);
	OZ_FILE_UNLOCK(fp);
	return (offset);
}

/*
 * unlink file
 *
 * path - fully qualified file path
 *
 * Returns 0 on success.
 *
 * OPTIONAL
 */
int
zfs_file_unlink(const char *path)
{
#ifdef __AROS__
	/* Native zfs_file APIs return errno values, not libc's -1 sentinel.
	 * spa_config_remove relies on ENOENT when an old cache is absent.
	 * Some stdcio/posixc combinations leave the caller's errno untouched;
	 * preserve DOS's actual failure instead of manufacturing EIO. Clear
	 * both channels so an earlier unrelated failure cannot leak through. */
	errno = 0;
	SetIoErr(0);
	if (remove(path) == 0)
		return (0);
	LONG dos_error = IoErr();
	if (dos_error != 0)
		return (__stdc_ioerr2errno(dos_error));
	return (errno != 0 ? errno : EIO);
#else
	return (remove(path));
#endif
}

/*
 * Get reference to file pointer
 *
 * fd - input file descriptor
 *
 * Returns pointer to file struct or NULL.
 * Native AROS resolves only explicitly scoped service-owned bindings.
 * Other libzpool user-space builds do not support this operation.
 */
zfs_file_t *
zfs_file_get(int fd)
{
#ifdef __AROS__
	return oz_file_scope_get(fd);
#else
	(void) fd;
	abort();
	return (NULL);
#endif
}
/*
 * Drop reference to file pointer
 *
 * fp - pointer to file struct
 *
 * Native AROS drops a retained file reference; other user-space builds
 * do not support this operation.
 */
void
zfs_file_put(zfs_file_t *fp)
{
#ifdef __AROS__
	zfs_file_close(fp);
#else
	abort();
	(void) fp;
#endif
}

void
zfsvfs_update_fromname(const char *oldname, const char *newname)
{
	(void) oldname, (void) newname;
}

void
spa_import_os(spa_t *spa)
{
	(void) spa;
}

void
spa_export_os(spa_t *spa)
{
	(void) spa;
}

void
spa_activate_os(spa_t *spa)
{
	(void) spa;
}

void
spa_deactivate_os(spa_t *spa)
{
	(void) spa;
}
