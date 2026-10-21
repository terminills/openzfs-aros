/* SPDX-License-Identifier: CDDL-1.0 */
/*
 * Upstream directory-entry locking from module/os/linux/zfs/zfs_dir.c.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 *
 * Native differences: validate input/directory type and reserve root .zfs
 * without claiming an implemented control-directory namespace. The caller
 * retains the parent znode and mount until unlocking. Shared locks retain
 * upstream copied-name lifetime and normalized matching semantics.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_dir.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/u8_textprep.h>
/* Upstream ZPL lookup result bit (sys/file.h on the kernel ports). */
#ifndef ED_CASE_CONFLICT
#define ED_CASE_CONFLICT 0x10
#endif

/*
 * zfs_match_find() is used by zfs_dirent_lock() to perform zap lookups
 * of names after deciding which is the appropriate lookup interface.
 */
static int
zfs_match_find(zfsvfs_t *zfsvfs, znode_t *dzp, const char *name,
    matchtype_t mt, boolean_t update, int *deflags, pathname_t *rpnp,
    uint64_t *zoid)
{
	boolean_t conflict = B_FALSE;
	int error;

	if (zfsvfs->z_norm) {
		size_t bufsz = 0;
		char *buf = NULL;

		if (rpnp) {
			buf = rpnp->pn_buf;
			bufsz = rpnp->pn_bufsize;
		}

		/*
		 * In the non-mixed case we only expect there would ever
		 * be one match, but we need to use the normalizing lookup.
		 */
		error = zap_lookup_norm(zfsvfs->z_os, dzp->z_id, name, 8, 1,
		    zoid, mt, buf, bufsz, &conflict);
	} else {
		error = zap_lookup(zfsvfs->z_os, dzp->z_id, name, 8, 1, zoid);
	}

	/*
	 * Allow multiple entries provided the first entry is
	 * the object id.  Non-zpl consumers may safely make
	 * use of the additional space.
	 *
	 * XXX: This should be a feature flag for compatibility
	 */
	if (error == EOVERFLOW)
		error = 0;

	if (zfsvfs->z_norm && !error && deflags)
		*deflags = conflict ? ED_CASE_CONFLICT : 0;

	*zoid = ZFS_DIRENT_OBJ(*zoid);

	return (error);
}

/*
 * Lock a directory entry.  A dirlock on <dzp, name> protects that name
 * in dzp's directory zap object.  As long as you hold a dirlock, you can
 * assume two things: (1) dzp cannot be reaped, and (2) no other thread
 * can change the zap entry for (i.e. link or unlink) this name.
 *
 * Input arguments:
 *	dzp	- znode for directory
 *	name	- name of entry to lock
 *	flag	- ZNEW: if the entry already exists, fail with EEXIST.
 *		  ZEXISTS: if the entry does not exist, fail with ENOENT.
 *		  ZSHARED: allow concurrent access with other ZSHARED callers.
 *		  ZXATTR: we want dzp's xattr directory
 *		  ZCILOOK: On a mixed sensitivity file system,
 *			   this lookup should be case-insensitive.
 *		  ZCIEXACT: On a purely case-insensitive file system,
 *			    this lookup should be case-sensitive.
 *		  ZRENAMING: we are locking for renaming, force narrow locks
 *		  ZHAVELOCK: Don't grab the z_name_lock for this call. The
 *			     current thread already holds it.
 *
 * Output arguments:
 *	zpp	- pointer to the znode for the entry (NULL if there isn't one)
 *	dlpp	- pointer to the dirlock for this entry (NULL on error)
 *      direntflags - (case-insensitive lookup only)
 *		flags if multiple case-sensitive matches exist in directory
 *      realpnp     - (case-insensitive lookup only)
 *		actual name matched within the directory
 *
 * Return value: 0 on success or errno on failure.
 *
 * NOTE: Always checks for, and rejects, '.' and '..'.
 * NOTE: For case-insensitive file systems we take wide locks (see below),
 *	 but return znode pointers to a single match.
 */
/* AROS pthread_cond_wait reacquires its private condition semaphore after
 * reacquiring the external mutex. Broadcast therefore does not make it safe
 * to immediately destroy/free the condition as the kernel CV path assumes.
 * Retire a removed lock until the last waiter has returned from cv_wait.
 * Counts and retirement are protected by the parent z_lock. */
struct native_dirlock {
	zfs_dirlock_t lock;
	unsigned waiters;
	boolean_t retired;
};

static void
free_dirlock(zfs_dirlock_t *dl)
{
	if (dl->dl_namesize != 0)
		kmem_free(dl->dl_name, dl->dl_namesize);
	cv_destroy(&dl->dl_cv);
	kmem_free(dl, sizeof (struct native_dirlock));
}

int
zfs_dirent_lock(zfs_dirlock_t **dlpp, znode_t *dzp, char *name,
    znode_t **zpp, int flag, int *direntflags, pathname_t *realpnp)
{
	if (!dlpp || !zpp) return (EINVAL);
	*dlpp = NULL; *zpp = NULL;
	if (!dzp || !name || strchr(name, '/')) return (EINVAL);
	/* The private xattr-directory slot uses an empty name on a base file;
	 * it is an SA reference, not a ZAP directory entry. */
	if (flag & ZXATTR) {
		if (*name) return (EINVAL);
	} else {
		if (!*name) return (EINVAL);
		if (!S_ISDIR(dzp->z_mode)) return (ENOTDIR);
	}
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	zfs_dirlock_t	*dl;
	boolean_t	update;
	matchtype_t	mt = 0;
	uint64_t	zoid = 0;
	int		error = 0;
	int		cmpflags;

	*zpp = NULL;
	*dlpp = NULL;

	/*
	 * Verify that we are not trying to lock '.', '..', or '.zfs'
	 */
	if ((name[0] == '.' &&
	    (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) ||
	    (dzp->z_id == zfsvfs->z_root && strcmp(name, ".zfs") == 0))
		return (SET_ERROR(EEXIST));

	/*
	 * Case sensitivity and normalization preferences are set when
	 * the file system is created.  These are stored in the
	 * zfsvfs->z_case and zfsvfs->z_norm fields.  These choices
	 * affect what vnodes can be cached in the DNLC, how we
	 * perform zap lookups, and the "width" of our dirlocks.
	 *
	 * A normal dirlock locks a single name.  Note that with
	 * normalization a name can be composed multiple ways, but
	 * when normalized, these names all compare equal.  A wide
	 * dirlock locks multiple names.  We need these when the file
	 * system is supporting mixed-mode access.  It is sometimes
	 * necessary to lock all case permutations of file name at
	 * once so that simultaneous case-insensitive/case-sensitive
	 * behaves as rationally as possible.
	 */

	/*
	 * When matching we may need to normalize & change case according to
	 * FS settings.
	 *
	 * Note that a normalized match is necessary for a case insensitive
	 * filesystem when the lookup request is not exact because normalization
	 * can fold case independent of normalizing code point sequences.
	 *
	 * See the table above zfs_dropname().
	 */
	if (zfsvfs->z_norm != 0) {
		mt = MT_NORMALIZE;

		/*
		 * Determine if the match needs to honor the case specified in
		 * lookup, and if so keep track of that so that during
		 * normalization we don't fold case.
		 */
		if ((zfsvfs->z_case == ZFS_CASE_INSENSITIVE &&
		    (flag & ZCIEXACT)) ||
		    (zfsvfs->z_case == ZFS_CASE_MIXED && !(flag & ZCILOOK))) {
			mt |= MT_MATCH_CASE;
		}
	}

	/*
	 * Only look in or update the DNLC if we are looking for the
	 * name on a file system that does not require normalization
	 * or case folding.  We can also look there if we happen to be
	 * on a non-normalizing, mixed sensitivity file system IF we
	 * are looking for the exact name.
	 *
	 * Maybe can add TO-UPPERed version of name to dnlc in ci-only
	 * case for performance improvement?
	 */
	update = !zfsvfs->z_norm ||
	    (zfsvfs->z_case == ZFS_CASE_MIXED &&
	    !(zfsvfs->z_norm & ~U8_TEXTPREP_TOUPPER) && !(flag & ZCILOOK));

	/*
	 * ZRENAMING indicates we are in a situation where we should
	 * take narrow locks regardless of the file system's
	 * preferences for normalizing and case folding.  This will
	 * prevent us deadlocking trying to grab the same wide lock
	 * twice if the two names happen to be case-insensitive
	 * matches.
	 */
	if (flag & ZRENAMING)
		cmpflags = 0;
	else
		cmpflags = zfsvfs->z_norm;

	/*
	 * Wait until there are no locks on this name.
	 *
	 * Don't grab the lock if it is already held. However, cannot
	 * have both ZSHARED and ZHAVELOCK together.
	 */
	ASSERT(!(flag & ZSHARED) || !(flag & ZHAVELOCK));
	if (!(flag & ZHAVELOCK))
		rw_enter(&dzp->z_name_lock, RW_READER);

	mutex_enter(&dzp->z_lock);
	for (;;) {
		if (dzp->z_unlinked && !(flag & ZXATTR)) {
			mutex_exit(&dzp->z_lock);
			if (!(flag & ZHAVELOCK))
				rw_exit(&dzp->z_name_lock);
			return (SET_ERROR(ENOENT));
		}
		for (dl = dzp->z_dirlocks; dl != NULL; dl = dl->dl_next) {
			if ((u8_strcmp(name, dl->dl_name, 0, cmpflags,
			    U8_UNICODE_LATEST, &error) == 0) || error != 0)
				break;
		}
		if (error != 0) {
			mutex_exit(&dzp->z_lock);
			if (!(flag & ZHAVELOCK))
				rw_exit(&dzp->z_name_lock);
			return (SET_ERROR(ENOENT));
		}
		if (dl == NULL)	{
			/*
			 * Allocate a new dirlock and add it to the list.
			 */
			dl = kmem_zalloc(sizeof (struct native_dirlock), KM_SLEEP);
			cv_init(&dl->dl_cv, NULL, CV_DEFAULT, NULL);
			dl->dl_name = name;
			dl->dl_sharecnt = 0;
			dl->dl_namelock = 0;
			dl->dl_namesize = 0;
			dl->dl_dzp = dzp;
			dl->dl_next = dzp->z_dirlocks;
			dzp->z_dirlocks = dl;
			break;
		}
		if ((flag & ZSHARED) && dl->dl_sharecnt != 0)
			break;
		struct native_dirlock *native = (struct native_dirlock *)dl;
		VERIFY3U(native->waiters, <, UINT_MAX);
		native->waiters++;
		cv_wait(&dl->dl_cv, &dzp->z_lock);
		VERIFY3U(native->waiters, >, 0);
		if (--native->waiters == 0 && native->retired)
			free_dirlock(dl);
	}

	/*
	 * If the z_name_lock was NOT held for this dirlock record it.
	 */
	if (flag & ZHAVELOCK)
		dl->dl_namelock = 1;

	if ((flag & ZSHARED) && ++dl->dl_sharecnt > 1 && dl->dl_namesize == 0) {
		/*
		 * We're the second shared reference to dl.  Make a copy of
		 * dl_name in case the first thread goes away before we do.
		 * Note that we initialize the new name before storing its
		 * pointer into dl_name, because the first thread may load
		 * dl->dl_name at any time.  It'll either see the old value,
		 * which belongs to it, or the new shared copy; either is OK.
		 */
		dl->dl_namesize = strlen(dl->dl_name) + 1;
		name = kmem_alloc(dl->dl_namesize, KM_SLEEP);
		memcpy(name, dl->dl_name, dl->dl_namesize);
		dl->dl_name = name;
	}

	mutex_exit(&dzp->z_lock);

	/*
	 * We have a dirlock on the name.  (Note that it is the dirlock,
	 * not the dzp's z_lock, that protects the name in the zap object.)
	 * See if there's an object by this name; if so, put a hold on it.
	 */
	if (flag & ZXATTR) {
		error = sa_lookup(dzp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs), &zoid,
		    sizeof (zoid));
		if (error == 0)
			error = (zoid == 0 ? SET_ERROR(ENOENT) : 0);
	} else {
		error = zfs_match_find(zfsvfs, dzp, name, mt,
		    update, direntflags, realpnp, &zoid);
	}
	if (error) {
		if (error != ENOENT || (flag & ZEXISTS)) {
			zfs_dirent_unlock(dl);
			return (error);
		}
	} else {
		if (flag & ZNEW) {
			zfs_dirent_unlock(dl);
			return (SET_ERROR(EEXIST));
		}
		error = zfs_zget(zfsvfs, zoid, zpp);
		if (error) {
			zfs_dirent_unlock(dl);
			return (error);
		}
	}

	*dlpp = dl;

	return (0);
}

/*
 * Unlock this directory entry and wake anyone who was waiting for it.
 */
void
zfs_dirent_unlock(zfs_dirlock_t *dl)
{
	znode_t *dzp = dl->dl_dzp;
	zfs_dirlock_t **prev_dl, *cur_dl;

	mutex_enter(&dzp->z_lock);

	if (!dl->dl_namelock)
		rw_exit(&dzp->z_name_lock);

	if (dl->dl_sharecnt > 1) {
		dl->dl_sharecnt--;
		mutex_exit(&dzp->z_lock);
		return;
	}
	prev_dl = &dzp->z_dirlocks;
	while ((cur_dl = *prev_dl) != dl)
		prev_dl = &cur_dl->dl_next;
	*prev_dl = dl->dl_next;
	struct native_dirlock *native = (struct native_dirlock *)dl;
	native->retired = B_TRUE;
	cv_broadcast(&dl->dl_cv);
	boolean_t dispose = native->waiters == 0;
	mutex_exit(&dzp->z_lock);

	if (dispose)
		free_dirlock(dl);
}
