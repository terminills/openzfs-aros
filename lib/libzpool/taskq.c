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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright 2012 Garrett D'Amore <garrett@damore.org>.  All rights reserved.
 * Copyright (c) 2014 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>

int taskq_now;
taskq_t *system_taskq;
taskq_t *system_delay_taskq;

static pthread_key_t taskq_tsd;

#define	TASKQ_ACTIVE	0x00010000

#ifdef __AROS__
/* Private extension: do not change the taskq_t layout seen by engine objects.
 * Grow on demand up to the caller's original concurrency, never a smaller cap.
 * Workers remain until destruction; idle retirement is not implemented here. */
struct aros_taskq { taskq_t base; int maximum; unsigned pending, spawning; };
static taskq_t *aros_spawn_taskq;
static __attribute__((noreturn)) void taskq_thread(void *);
static void
taskq_spawn(void *arg)
{
	taskq_t *tq = arg;
	struct aros_taskq *aq = (struct aros_taskq *)tq;
	mutex_enter(&tq->tq_lock);
	kthread_t *thread = zk_thread_create_optional(tq->tq_name, taskq_thread,
	    tq, 0, TS_RUN | TS_JOINABLE);
	ASSERT3U(aq->spawning, >, 0);
	aq->spawning--;
	if (thread)
		tq->tq_threadlist[tq->tq_nthreads++] = thread;
	else if (--tq->tq_active == 0)
		cv_broadcast(&tq->tq_wait_cv);
	mutex_exit(&tq->tq_lock);
}
static void
taskq_grow(taskq_t *tq, unsigned demand)
{
	struct aros_taskq *aq = (struct aros_taskq *)tq;
	if (!(tq->tq_flags & TASKQ_DYNAMIC) || !aros_spawn_taskq ||
	    tq->tq_nthreads + aq->spawning >= (unsigned)aq->maximum ||
	    aq->pending + demand <= tq->tq_nthreads - tq->tq_active + 2 * aq->spawning)
		return;
	/* The dedicated parent outlives every dynamic queue. Count outstanding
	 * spawn requests as active so wait/destroy cannot reclaim their target.
	 * Do not allocate an Exec process while holding a caller's ZFS locks. */
	aq->spawning++;
	tq->tq_active++;
	if (!taskq_dispatch(aros_spawn_taskq, taskq_spawn, tq, TQ_NOSLEEP)) {
		aq->spawning--;
		tq->tq_active--;
	}
}
#endif

static taskq_ent_t *
task_alloc(taskq_t *tq, int tqflags)
{
	taskq_ent_t *t;
	int rv;

again:	if ((t = tq->tq_freelist) != NULL && tq->tq_nalloc >= tq->tq_minalloc) {
		ASSERT(!(t->tqent_flags & TQENT_FLAG_PREALLOC));
		tq->tq_freelist = t->tqent_next;
	} else {
		if (tq->tq_nalloc >= tq->tq_maxalloc) {
			if (!(tqflags & KM_SLEEP))
				return (NULL);

			/*
			 * We don't want to exceed tq_maxalloc, but we can't
			 * wait for other tasks to complete (and thus free up
			 * task structures) without risking deadlock with
			 * the caller.  So, we just delay for one second
			 * to throttle the allocation rate. If we have tasks
			 * complete before one second timeout expires then
			 * taskq_ent_free will signal us and we will
			 * immediately retry the allocation.
			 */
			tq->tq_maxalloc_wait++;
			rv = cv_timedwait(&tq->tq_maxalloc_cv,
			    &tq->tq_lock, ddi_get_lbolt() + hz);
			tq->tq_maxalloc_wait--;
			if (rv > 0)
				goto again;		/* signaled */
		}
		mutex_exit(&tq->tq_lock);

		t = kmem_alloc(sizeof (taskq_ent_t), tqflags);

		mutex_enter(&tq->tq_lock);
		if (t != NULL) {
			/* Make sure we start without any flags */
			t->tqent_flags = 0;
			tq->tq_nalloc++;
		}
	}
	return (t);
}

static void
task_free(taskq_t *tq, taskq_ent_t *t)
{
	if (tq->tq_nalloc <= tq->tq_minalloc) {
		t->tqent_next = tq->tq_freelist;
		tq->tq_freelist = t;
	} else {
		tq->tq_nalloc--;
		mutex_exit(&tq->tq_lock);
		kmem_free(t, sizeof (taskq_ent_t));
		mutex_enter(&tq->tq_lock);
	}

	if (tq->tq_maxalloc_wait)
		cv_signal(&tq->tq_maxalloc_cv);
}

taskqid_t
taskq_dispatch(taskq_t *tq, task_func_t func, void *arg, uint_t tqflags)
{
	taskq_ent_t *t;

	if (taskq_now) {
		func(arg);
		return (1);
	}

	mutex_enter(&tq->tq_lock);
	ASSERT(tq->tq_flags & TASKQ_ACTIVE);
	if ((t = task_alloc(tq, tqflags)) == NULL) {
		mutex_exit(&tq->tq_lock);
		return (0);
	}
#ifdef __AROS__
	if (tqflags & TQ_NOQUEUE) {
		struct aros_taskq *aq = (struct aros_taskq *)tq;
		/* task_alloc may drop tq_lock: inspect availability only after it
		 * returns. Spawn reservations are active but not actual workers.
		 * Count already queued work against idle capacity; never promise
		 * immediate execution based on an optional future allocation. */
		unsigned idle = tq->tq_nthreads - tq->tq_active + aq->spawning;
		if (idle <= aq->pending) {
			/* Prepare capacity for a later caller without accepting this
			 * job on the promise of a successful asynchronous spawn. */
			taskq_grow(tq, 1);
			task_free(tq, t);
			mutex_exit(&tq->tq_lock);
			return (TASKQID_INVALID);
		}
		tqflags |= TQ_FRONT;
	}
#endif
	if (tqflags & TQ_FRONT) {
		t->tqent_next = tq->tq_task.tqent_next;
		t->tqent_prev = &tq->tq_task;
	} else {
		t->tqent_next = &tq->tq_task;
		t->tqent_prev = tq->tq_task.tqent_prev;
	}
	t->tqent_next->tqent_prev = t;
	t->tqent_prev->tqent_next = t;
	t->tqent_func = func;
	t->tqent_arg = arg;
	t->tqent_flags = 0;
#ifdef __AROS__
	((struct aros_taskq *)tq)->pending++;
	taskq_grow(tq, 0);
#endif
	cv_signal(&tq->tq_dispatch_cv);
	mutex_exit(&tq->tq_lock);
	return (1);
}

taskqid_t
taskq_dispatch_delay(taskq_t *tq, task_func_t func, void *arg, uint_t tqflags,
    clock_t expire_time)
{
	(void) tq, (void) func, (void) arg, (void) tqflags, (void) expire_time;
	return (0);
}

int
taskq_empty_ent(taskq_ent_t *t)
{
	return (t->tqent_next == NULL);
}

void
taskq_init_ent(taskq_ent_t *t)
{
	t->tqent_next = NULL;
	t->tqent_prev = NULL;
	t->tqent_func = NULL;
	t->tqent_arg = NULL;
	t->tqent_flags = 0;
}

void
taskq_dispatch_ent(taskq_t *tq, task_func_t func, void *arg, uint_t flags,
    taskq_ent_t *t)
{
	ASSERT(func != NULL);

	/*
	 * Mark it as a prealloc'd task.  This is important
	 * to ensure that we don't free it later.
	 */
	t->tqent_flags |= TQENT_FLAG_PREALLOC;
	/*
	 * Enqueue the task to the underlying queue.
	 */
	mutex_enter(&tq->tq_lock);

	if (flags & TQ_FRONT) {
		t->tqent_next = tq->tq_task.tqent_next;
		t->tqent_prev = &tq->tq_task;
	} else {
		t->tqent_next = &tq->tq_task;
		t->tqent_prev = tq->tq_task.tqent_prev;
	}
	t->tqent_next->tqent_prev = t;
	t->tqent_prev->tqent_next = t;
	t->tqent_func = func;
	t->tqent_arg = arg;
#ifdef __AROS__
	((struct aros_taskq *)tq)->pending++;
	taskq_grow(tq, 0);
#endif
	cv_signal(&tq->tq_dispatch_cv);
	mutex_exit(&tq->tq_lock);
}

void
taskq_wait(taskq_t *tq)
{
	mutex_enter(&tq->tq_lock);
	while (tq->tq_task.tqent_next != &tq->tq_task || tq->tq_active != 0)
		cv_wait(&tq->tq_wait_cv, &tq->tq_lock);
	mutex_exit(&tq->tq_lock);
}

void
taskq_wait_id(taskq_t *tq, taskqid_t id)
{
	(void) id;
	taskq_wait(tq);
}

void
taskq_wait_outstanding(taskq_t *tq, taskqid_t id)
{
	(void) id;
	taskq_wait(tq);
}

static __attribute__((noreturn)) void
taskq_thread(void *arg)
{
	taskq_t *tq = arg;
	taskq_ent_t *t;
	boolean_t prealloc;

	VERIFY0(pthread_setspecific(taskq_tsd, tq));

	mutex_enter(&tq->tq_lock);
	while (tq->tq_flags & TASKQ_ACTIVE) {
		if ((t = tq->tq_task.tqent_next) == &tq->tq_task) {
			if (--tq->tq_active == 0)
				cv_broadcast(&tq->tq_wait_cv);
			cv_wait(&tq->tq_dispatch_cv, &tq->tq_lock);
			tq->tq_active++;
			continue;
		}
		t->tqent_prev->tqent_next = t->tqent_next;
		t->tqent_next->tqent_prev = t->tqent_prev;
		t->tqent_next = NULL;
		t->tqent_prev = NULL;
		prealloc = t->tqent_flags & TQENT_FLAG_PREALLOC;
#ifdef __AROS__
		ASSERT3U(((struct aros_taskq *)tq)->pending, >, 0);
		((struct aros_taskq *)tq)->pending--;
#endif
		mutex_exit(&tq->tq_lock);

		rw_enter(&tq->tq_threadlock, RW_READER);
		t->tqent_func(t->tqent_arg);
		rw_exit(&tq->tq_threadlock);

		mutex_enter(&tq->tq_lock);
		if (!prealloc)
			task_free(tq, t);
	}
	tq->tq_nthreads--;
	cv_broadcast(&tq->tq_wait_cv);
	mutex_exit(&tq->tq_lock);
	thread_exit();
}

taskq_t *
taskq_create(const char *name, int nthreads, pri_t pri,
    int minalloc, int maxalloc, uint_t flags)
{
	(void) pri;
#ifdef __AROS__
	taskq_t *tq = kmem_zalloc(sizeof (struct aros_taskq), KM_SLEEP);
#else
	taskq_t *tq = kmem_zalloc(sizeof (taskq_t), KM_SLEEP);
#endif
	int t;

	if (flags & TASKQ_THREADS_CPU_PCT) {
		int pct;
		ASSERT3S(nthreads, >=, 0);
		ASSERT3S(nthreads, <=, 100);
		pct = MIN(nthreads, 100);
		pct = MAX(pct, 0);

		nthreads = (boot_ncpus * pct) / 100;
		nthreads = MAX(nthreads, 1);	/* need at least 1 thread */
	} else {
		ASSERT3S(nthreads, >=, 1);
	}

	rw_init(&tq->tq_threadlock, NULL, RW_DEFAULT, NULL);
	mutex_init(&tq->tq_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&tq->tq_dispatch_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&tq->tq_wait_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&tq->tq_maxalloc_cv, NULL, CV_DEFAULT, NULL);
	(void) strlcpy(tq->tq_name, name, sizeof (tq->tq_name));
	tq->tq_flags = flags | TASKQ_ACTIVE;
	tq->tq_active = nthreads;
	tq->tq_nthreads = nthreads;
	tq->tq_minalloc = minalloc;
	tq->tq_maxalloc = maxalloc;
	tq->tq_task.tqent_next = &tq->tq_task;
	tq->tq_task.tqent_prev = &tq->tq_task;
	tq->tq_threadlist = kmem_alloc(nthreads * sizeof (kthread_t *),
	    KM_SLEEP);
#ifdef __AROS__
	((struct aros_taskq *)tq)->maximum = nthreads;
	if (flags & TASKQ_DYNAMIC) {
		nthreads = 1;
		tq->tq_active = tq->tq_nthreads = nthreads;
	}
#endif

	if (flags & TASKQ_PREPOPULATE) {
		mutex_enter(&tq->tq_lock);
		while (minalloc-- > 0)
			task_free(tq, task_alloc(tq, KM_SLEEP));
		mutex_exit(&tq->tq_lock);
	}

	int state = TS_RUN;
#ifdef __AROS__
	state |= TS_JOINABLE;
#endif
	for (t = 0; t < nthreads; t++)
		VERIFY((tq->tq_threadlist[t] = thread_create_named(tq->tq_name,
		    NULL, 0, taskq_thread, tq, 0, &p0, state, pri)) != NULL);

	return (tq);
}

void
taskq_destroy(taskq_t *tq)
{
#ifdef __AROS__
	int nthreads;
#else
	int nthreads = tq->tq_nthreads;
#endif

	taskq_wait(tq);

	mutex_enter(&tq->tq_lock);

#ifdef __AROS__
	nthreads = tq->tq_nthreads;
#endif
	tq->tq_flags &= ~TASKQ_ACTIVE;
	cv_broadcast(&tq->tq_dispatch_cv);

	while (tq->tq_nthreads != 0)
		cv_wait(&tq->tq_wait_cv, &tq->tq_lock);

	tq->tq_minalloc = 0;
	while (tq->tq_nalloc != 0) {
		ASSERT(tq->tq_freelist != NULL);
		taskq_ent_t *tqent_nexttq = tq->tq_freelist->tqent_next;
		task_free(tq, tq->tq_freelist);
		tq->tq_freelist = tqent_nexttq;
	}

	mutex_exit(&tq->tq_lock);

#ifdef __AROS__
	/* nthreads reaching zero precedes pthread teardown and slot release.
	 * Join before reclaiming queue synchronization or returning to callers. */
	for (int i = 0; i < nthreads; i++)
		VERIFY0(thread_join(tq->tq_threadlist[i]));
	kmem_free(tq->tq_threadlist,
	    ((struct aros_taskq *)tq)->maximum * sizeof (kthread_t *));
#else
	kmem_free(tq->tq_threadlist, nthreads * sizeof (kthread_t *));
#endif

	rw_destroy(&tq->tq_threadlock);
	mutex_destroy(&tq->tq_lock);
	cv_destroy(&tq->tq_dispatch_cv);
	cv_destroy(&tq->tq_wait_cv);
	cv_destroy(&tq->tq_maxalloc_cv);

#ifdef __AROS__
	kmem_free(tq, sizeof (struct aros_taskq));
#else
	kmem_free(tq, sizeof (taskq_t));
#endif
}

/*
 * Create a taskq with a specified number of pool threads. Allocate
 * and return an array of nthreads kthread_t pointers, one for each
 * thread in the pool. The array is not ordered and must be freed
 * by the caller.
 */
taskq_t *
taskq_create_synced(const char *name, int nthreads, pri_t pri,
    int minalloc, int maxalloc, uint_t flags, kthread_t ***ktpp)
{
	taskq_t *tq;
	kthread_t **kthreads = kmem_zalloc(sizeof (*kthreads) * nthreads,
	    KM_SLEEP);

	(void) pri; (void) minalloc; (void) maxalloc;

	flags &= ~(TASKQ_DYNAMIC | TASKQ_THREADS_CPU_PCT | TASKQ_DC_BATCH);

	tq = taskq_create(name, nthreads, minclsyspri, nthreads, INT_MAX,
	    flags | TASKQ_PREPOPULATE);
	VERIFY(tq != NULL);
	VERIFY(tq->tq_nthreads == nthreads);

	for (int i = 0; i < nthreads; i++) {
		kthreads[i] = tq->tq_threadlist[i];
	}
	*ktpp = kthreads;
	return (tq);
}

int
taskq_member(taskq_t *tq, kthread_t *t)
{
	int i;

	if (taskq_now)
		return (1);

#ifdef __AROS__
	mutex_enter(&tq->tq_lock);
#endif
	for (i = 0; i < tq->tq_nthreads; i++)
		if (tq->tq_threadlist[i] == t) {
#ifdef __AROS__
			mutex_exit(&tq->tq_lock);
#endif
			return (1);
		}

#ifdef __AROS__
	mutex_exit(&tq->tq_lock);
#endif
	return (0);
}

taskq_t *
taskq_of_curthread(void)
{
	return (pthread_getspecific(taskq_tsd));
}

int
taskq_cancel_id(taskq_t *tq, taskqid_t id, boolean_t wait)
{
	(void) tq, (void) id, (void) wait;
	return (ENOENT);
}

void
system_taskq_init(void)
{
	VERIFY0(pthread_key_create(&taskq_tsd, NULL));
#ifdef __AROS__
	aros_spawn_taskq = taskq_create("aros_taskq_spawn", 1, maxclsyspri,
	    4, INT_MAX, TASKQ_PREPOPULATE);
#endif
	system_taskq = taskq_create("system_taskq", 64, maxclsyspri, 4, 512,
	    TASKQ_DYNAMIC | TASKQ_PREPOPULATE);
	system_delay_taskq = taskq_create("delay_taskq", 4, maxclsyspri, 4,
	    512, TASKQ_DYNAMIC | TASKQ_PREPOPULATE);
}

void
system_taskq_fini(void)
{
	taskq_destroy(system_taskq);
	system_taskq = NULL; /* defensive */
	taskq_destroy(system_delay_taskq);
	system_delay_taskq = NULL;
#ifdef __AROS__
	taskq_destroy(aros_spawn_taskq);
	aros_spawn_taskq = NULL;
#endif
	VERIFY0(pthread_key_delete(taskq_tsd));
}
