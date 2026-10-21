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
 * Copyright 2015 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright 2015 RackTop Systems.
 * Copyright (c) 2016, Intel Corporation.
 * Copyright (c) 2021, Colm Buckley <colm@tuatha.org>
 */

/*
 * Pool import support functions.
 *
 * Used by zpool, ztest, zdb, and zhack to locate importable configs. Since
 * these commands are expected to run in the global zone, we can assume
 * that the devices are all readable when called.
 *
 * To import a pool, we rely on reading the configuration information from the
 * ZFS label of each device.  If we successfully read the label, then we
 * organize the configuration information in the following hierarchy:
 *
 *	pool guid -> toplevel vdev guid -> label txg
 *
 * Duplicate entries matching this same tuple will be discarded.  Once we have
 * examined every device, we pick the best label txg config for each toplevel
 * vdev.  We then arrange these toplevel vdevs into a complete pool config, and
 * update any paths that have changed.  Finally, we attempt to import the pool
 * using our derived config, and record the results.
 */

#ifdef HAVE_AIO_H
#include <aio.h>
#endif
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <libintl.h>
#include <libgen.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/dktp/fdisk.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>

#include <thread_pool.h>
#include <libzutil.h>
#include <libnvpair.h>

#include "zutil_import.h"
#if defined(__AROS__) && !defined(OZ_NATIVE_LABEL_CLIENT)
#include "../../aros/engine/device-io.h"
#endif
#ifdef OZ_NATIVE_LABEL_CLIENT
#define OZ_IMPORT_ADMISSION "org.aros:import-admission"
#define OZ_IMPORT_PATHS "paths"
#include "../../aros/cli/device-path.h"
#include "../../aros/cli/native-device.h"
#include "../../aros/ui/admission-api.h"
#endif

const char *
libpc_error_description(libpc_handle_t *hdl)
{
	if (hdl->lpc_desc[0] != '\0')
		return (hdl->lpc_desc);

	switch (hdl->lpc_error) {
	case LPC_BADCACHE:
		return (dgettext(TEXT_DOMAIN, "invalid or missing cache file"));
	case LPC_BADPATH:
		return (dgettext(TEXT_DOMAIN, "must be an absolute path"));
	case LPC_NOMEM:
		return (dgettext(TEXT_DOMAIN, "out of memory"));
	case LPC_EACCESS:
		return (dgettext(TEXT_DOMAIN, "some devices require root "
		    "privileges"));
	case LPC_UNKNOWN:
		return (dgettext(TEXT_DOMAIN, "unknown error"));
	default:
		assert(hdl->lpc_error == 0);
		return (dgettext(TEXT_DOMAIN, "no error"));
	}
}

static __attribute__((format(printf, 2, 3))) void
zutil_error_aux(libpc_handle_t *hdl, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	(void) vsnprintf(hdl->lpc_desc, sizeof (hdl->lpc_desc), fmt, ap);
	hdl->lpc_desc_active = B_TRUE;

	va_end(ap);
}

static void
zutil_verror(libpc_handle_t *hdl, lpc_error_t error, const char *fmt,
    va_list ap)
{
	char action[1024];

	(void) vsnprintf(action, sizeof (action), fmt, ap);
	hdl->lpc_error = error;

	if (hdl->lpc_desc_active)
		hdl->lpc_desc_active = B_FALSE;
	else
		hdl->lpc_desc[0] = '\0';

	if (hdl->lpc_printerr)
		(void) fprintf(stderr, "%s: %s\n", action,
		    libpc_error_description(hdl));
}

static __attribute__((format(printf, 3, 4))) int
zutil_error_fmt(libpc_handle_t *hdl, lpc_error_t error,
    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	zutil_verror(hdl, error, fmt, ap);

	va_end(ap);

	return (-1);
}

static int
zutil_error(libpc_handle_t *hdl, lpc_error_t error, const char *msg)
{
	return (zutil_error_fmt(hdl, error, "%s", msg));
}

static int
zutil_no_memory(libpc_handle_t *hdl)
{
	zutil_error(hdl, LPC_NOMEM, "internal error");
	exit(1);
}

void *
zutil_alloc(libpc_handle_t *hdl, size_t size)
{
	void *data;

	if ((data = calloc(1, size)) == NULL)
		(void) zutil_no_memory(hdl);

	return (data);
}

char *
zutil_strdup(libpc_handle_t *hdl, const char *str)
{
	char *ret;

	if ((ret = strdup(str)) == NULL)
		(void) zutil_no_memory(hdl);

	return (ret);
}

static char *
zutil_strndup(libpc_handle_t *hdl, const char *str, size_t n)
{
	char *ret;

	if ((ret = strndup(str, n)) == NULL)
		(void) zutil_no_memory(hdl);

	return (ret);
}

/*
 * Intermediate structures used to gather configuration information.
 */
typedef struct config_entry {
	uint64_t		ce_txg;
	nvlist_t		*ce_config;
	struct config_entry	*ce_next;
} config_entry_t;

typedef struct vdev_entry {
	uint64_t		ve_guid;
	config_entry_t		*ve_configs;
	struct vdev_entry	*ve_next;
} vdev_entry_t;

typedef struct pool_entry {
	uint64_t		pe_guid;
	vdev_entry_t		*pe_vdevs;
	struct pool_entry	*pe_next;
} pool_entry_t;

typedef struct name_entry {
	char			*ne_name;
	uint64_t		ne_guid;
	uint64_t		ne_order;
	uint64_t		ne_num_labels;
	struct name_entry	*ne_next;
} name_entry_t;

typedef struct pool_list {
	pool_entry_t		*pools;
	name_entry_t		*names;
} pool_list_t;

/*
 * Go through and fix up any path and/or devid information for the given vdev
 * configuration.
 */
static int
fix_paths(libpc_handle_t *hdl, nvlist_t *nv, name_entry_t *names)
{
	nvlist_t **child;
	uint_t c, children;
	uint64_t guid;
	name_entry_t *ne, *best;
	const char *path;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if (fix_paths(hdl, child[c], names) != 0)
				return (-1);
		return (0);
	}

	/*
	 * This is a leaf (file or disk) vdev.  In either case, go through
	 * the name list and see if we find a matching guid.  If so, replace
	 * the path and see if we can calculate a new devid.
	 *
	 * There may be multiple names associated with a particular guid, in
	 * which case we have overlapping partitions or multiple paths to the
	 * same disk.  In this case we prefer to use the path name which
	 * matches the ZPOOL_CONFIG_PATH.  If no matching entry is found we
	 * use the lowest order device which corresponds to the first match
	 * while traversing the ZPOOL_IMPORT_PATH search path.
	 */
	verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) == 0);
	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) != 0)
		path = NULL;

	best = NULL;
	for (ne = names; ne != NULL; ne = ne->ne_next) {
		if (ne->ne_guid == guid) {
			if (path == NULL) {
				best = ne;
				break;
			}

			if ((strlen(path) == strlen(ne->ne_name)) &&
			    strncmp(path, ne->ne_name, strlen(path)) == 0) {
				best = ne;
				break;
			}

			if (best == NULL) {
				best = ne;
				continue;
			}

			/* Prefer paths with move vdev labels. */
			if (ne->ne_num_labels > best->ne_num_labels) {
				best = ne;
				continue;
			}

			/* Prefer paths earlier in the search order. */
			if (ne->ne_num_labels == best->ne_num_labels &&
			    ne->ne_order < best->ne_order) {
				best = ne;
				continue;
			}
		}
	}

	if (best == NULL)
		return (0);

	if (nvlist_add_string(nv, ZPOOL_CONFIG_PATH, best->ne_name) != 0)
		return (-1);

	update_vdev_config_dev_strs(nv);

	return (0);
}

/*
 * Add the given configuration to the list of known devices.
 */
static int
add_config(libpc_handle_t *hdl, pool_list_t *pl, const char *path,
    int order, int num_labels, nvlist_t *config)
{
	uint64_t pool_guid, vdev_guid, top_guid, txg, state;
	pool_entry_t *pe;
	vdev_entry_t *ve;
	config_entry_t *ce;
	name_entry_t *ne;

	/*
	 * If this is a hot spare not currently in use or level 2 cache
	 * device, add it to the list of names to translate, but don't do
	 * anything else.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &state) == 0 &&
	    (state == POOL_STATE_SPARE || state == POOL_STATE_L2CACHE) &&
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &vdev_guid) == 0) {
		if ((ne = zutil_alloc(hdl, sizeof (name_entry_t))) == NULL)
			return (-1);

		if ((ne->ne_name = zutil_strdup(hdl, path)) == NULL) {
			free(ne);
			return (-1);
		}
		ne->ne_guid = vdev_guid;
		ne->ne_order = order;
		ne->ne_num_labels = num_labels;
		ne->ne_next = pl->names;
		pl->names = ne;

		return (0);
	}

	/*
	 * If we have a valid config but cannot read any of these fields, then
	 * it means we have a half-initialized label.  In vdev_label_init()
	 * we write a label with txg == 0 so that we can identify the device
	 * in case the user refers to the same disk later on.  If we fail to
	 * create the pool, we'll be left with a label in this state
	 * which should not be considered part of a valid pool.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &pool_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID,
	    &vdev_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_TOP_GUID,
	    &top_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG,
	    &txg) != 0 || txg == 0) {
		return (0);
	}

	/*
	 * First, see if we know about this pool.  If not, then add it to the
	 * list of known pools.
	 */
	for (pe = pl->pools; pe != NULL; pe = pe->pe_next) {
		if (pe->pe_guid == pool_guid)
			break;
	}

	if (pe == NULL) {
		if ((pe = zutil_alloc(hdl, sizeof (pool_entry_t))) == NULL) {
			return (-1);
		}
		pe->pe_guid = pool_guid;
		pe->pe_next = pl->pools;
		pl->pools = pe;
	}

	/*
	 * Second, see if we know about this toplevel vdev.  Add it if its
	 * missing.
	 */
	for (ve = pe->pe_vdevs; ve != NULL; ve = ve->ve_next) {
		if (ve->ve_guid == top_guid)
			break;
	}

	if (ve == NULL) {
		if ((ve = zutil_alloc(hdl, sizeof (vdev_entry_t))) == NULL) {
			return (-1);
		}
		ve->ve_guid = top_guid;
		ve->ve_next = pe->pe_vdevs;
		pe->pe_vdevs = ve;
	}

	/*
	 * Third, see if we have a config with a matching transaction group.  If
	 * so, then we do nothing.  Otherwise, add it to the list of known
	 * configs.
	 */
	for (ce = ve->ve_configs; ce != NULL; ce = ce->ce_next) {
		if (ce->ce_txg == txg)
			break;
	}

	if (ce == NULL) {
		if ((ce = zutil_alloc(hdl, sizeof (config_entry_t))) == NULL) {
			return (-1);
		}
		ce->ce_txg = txg;
		ce->ce_config = fnvlist_dup(config);
		ce->ce_next = ve->ve_configs;
		ve->ve_configs = ce;
	}

	/*
	 * At this point we've successfully added our config to the list of
	 * known configs.  The last thing to do is add the vdev guid -> path
	 * mappings so that we can fix up the configuration as necessary before
	 * doing the import.
	 */
	if ((ne = zutil_alloc(hdl, sizeof (name_entry_t))) == NULL)
		return (-1);

	if ((ne->ne_name = zutil_strdup(hdl, path)) == NULL) {
		free(ne);
		return (-1);
	}

	ne->ne_guid = vdev_guid;
	ne->ne_order = order;
	ne->ne_num_labels = num_labels;
	ne->ne_next = pl->names;
	pl->names = ne;

	return (0);
}

static int
zutil_pool_active(libpc_handle_t *hdl, const char *name, uint64_t guid,
    boolean_t *isactive)
{
	ASSERT(hdl->lpc_ops->pco_pool_active != NULL);

	int error = hdl->lpc_ops->pco_pool_active(hdl->lpc_lib_handle, name,
	    guid, isactive);

	return (error);
}

static nvlist_t *
zutil_refresh_config(libpc_handle_t *hdl, nvlist_t *tryconfig)
{
	ASSERT(hdl->lpc_ops->pco_refresh_config != NULL);

	return (hdl->lpc_ops->pco_refresh_config(hdl->lpc_lib_handle,
	    tryconfig));
}

/*
 * Determine if the vdev id is a hole in the namespace.
 */
static boolean_t
vdev_is_hole(uint64_t *hole_array, uint_t holes, uint_t id)
{
	int c;

	for (c = 0; c < holes; c++) {

		/* Top-level is a hole */
		if (hole_array[c] == id)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Convert our list of pools into the definitive set of configurations.  We
 * start by picking the best config for each toplevel vdev.  Once that's done,
 * we assemble the toplevel vdevs into a full config for the pool.  We make a
 * pass to fix up any incorrect paths, and then add it to the main list to
 * return to the user.
 */
static nvlist_t *
get_configs(libpc_handle_t *hdl, pool_list_t *pl, boolean_t active_ok,
    nvlist_t *policy, nvlist_t *admission)
{
#ifndef OZ_NATIVE_LABEL_CLIENT
	(void) admission;
#endif
	pool_entry_t *pe;
	vdev_entry_t *ve;
	config_entry_t *ce;
	nvlist_t *ret = NULL, *config = NULL, *tmp = NULL, *nvtop, *nvroot;
	nvlist_t **spares, **l2cache;
	uint_t i, nspares, nl2cache;
	boolean_t config_seen;
	uint64_t best_txg;
	const char *name, *hostname = NULL;
	uint64_t guid;
	uint_t children = 0;
	nvlist_t **child = NULL;
	uint64_t *hole_array, max_id;
	uint_t c;
	boolean_t isactive;
	nvlist_t *nvl;
	boolean_t valid_top_config = B_FALSE;

	if (nvlist_alloc(&ret, 0, 0) != 0)
		goto nomem;

	for (pe = pl->pools; pe != NULL; pe = pe->pe_next) {
		uint64_t id, max_txg = 0, hostid = 0;
		uint_t holes = 0;

		if (nvlist_alloc(&config, NV_UNIQUE_NAME, 0) != 0)
			goto nomem;
		config_seen = B_FALSE;

		/*
		 * Iterate over all toplevel vdevs.  Grab the pool configuration
		 * from the first one we find, and then go through the rest and
		 * add them as necessary to the 'vdevs' member of the config.
		 */
		for (ve = pe->pe_vdevs; ve != NULL; ve = ve->ve_next) {

			/*
			 * Determine the best configuration for this vdev by
			 * selecting the config with the latest transaction
			 * group.
			 */
			best_txg = 0;
			for (ce = ve->ve_configs; ce != NULL;
			    ce = ce->ce_next) {

				if (ce->ce_txg > best_txg) {
					tmp = ce->ce_config;
					best_txg = ce->ce_txg;
				}
			}

			/*
			 * We rely on the fact that the max txg for the
			 * pool will contain the most up-to-date information
			 * about the valid top-levels in the vdev namespace.
			 */
			if (best_txg > max_txg) {
				(void) nvlist_remove(config,
				    ZPOOL_CONFIG_VDEV_CHILDREN,
				    DATA_TYPE_UINT64);
				(void) nvlist_remove(config,
				    ZPOOL_CONFIG_HOLE_ARRAY,
				    DATA_TYPE_UINT64_ARRAY);

				max_txg = best_txg;
				hole_array = NULL;
				holes = 0;
				max_id = 0;
				valid_top_config = B_FALSE;

				if (nvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_VDEV_CHILDREN, &max_id) == 0) {
					verify(nvlist_add_uint64(config,
					    ZPOOL_CONFIG_VDEV_CHILDREN,
					    max_id) == 0);
					valid_top_config = B_TRUE;
				}

				if (nvlist_lookup_uint64_array(tmp,
				    ZPOOL_CONFIG_HOLE_ARRAY, &hole_array,
				    &holes) == 0) {
					verify(nvlist_add_uint64_array(config,
					    ZPOOL_CONFIG_HOLE_ARRAY,
					    hole_array, holes) == 0);
				}
			}

			if (!config_seen) {
				/*
				 * Copy the relevant pieces of data to the pool
				 * configuration:
				 *
				 *	version
				 *	pool guid
				 *	name
				 *	comment (if available)
				 *	compatibility features (if available)
				 *	pool state
				 *	hostid (if available)
				 *	hostname (if available)
				 */
				uint64_t state, version;
				const char *comment = NULL;
				const char *compatibility = NULL;

				version = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_VERSION);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_VERSION, version);
				guid = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_POOL_GUID);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_POOL_GUID, guid);
				name = fnvlist_lookup_string(tmp,
				    ZPOOL_CONFIG_POOL_NAME);
				fnvlist_add_string(config,
				    ZPOOL_CONFIG_POOL_NAME, name);

				if (nvlist_lookup_string(tmp,
				    ZPOOL_CONFIG_COMMENT, &comment) == 0)
					fnvlist_add_string(config,
					    ZPOOL_CONFIG_COMMENT, comment);

				if (nvlist_lookup_string(tmp,
				    ZPOOL_CONFIG_COMPATIBILITY,
				    &compatibility) == 0)
					fnvlist_add_string(config,
					    ZPOOL_CONFIG_COMPATIBILITY,
					    compatibility);

				state = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_POOL_STATE);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_POOL_STATE, state);

				hostid = 0;
				if (nvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_HOSTID, &hostid) == 0) {
					fnvlist_add_uint64(config,
					    ZPOOL_CONFIG_HOSTID, hostid);
					hostname = fnvlist_lookup_string(tmp,
					    ZPOOL_CONFIG_HOSTNAME);
					fnvlist_add_string(config,
					    ZPOOL_CONFIG_HOSTNAME, hostname);
				}

				config_seen = B_TRUE;
			}

			/*
			 * Add this top-level vdev to the child array.
			 */
			verify(nvlist_lookup_nvlist(tmp,
			    ZPOOL_CONFIG_VDEV_TREE, &nvtop) == 0);
			verify(nvlist_lookup_uint64(nvtop, ZPOOL_CONFIG_ID,
			    &id) == 0);

			if (id >= children) {
				nvlist_t **newchild;

				newchild = zutil_alloc(hdl, (id + 1) *
				    sizeof (nvlist_t *));
				if (newchild == NULL)
					goto nomem;

				for (c = 0; c < children; c++)
					newchild[c] = child[c];

				free(child);
				child = newchild;
				children = id + 1;
			}
			if (nvlist_dup(nvtop, &child[id], 0) != 0)
				goto nomem;

		}

		/*
		 * If we have information about all the top-levels then
		 * clean up the nvlist which we've constructed. This
		 * means removing any extraneous devices that are
		 * beyond the valid range or adding devices to the end
		 * of our array which appear to be missing.
		 */
		if (valid_top_config) {
			if (max_id < children) {
				for (c = max_id; c < children; c++)
					nvlist_free(child[c]);
				children = max_id;
			} else if (max_id > children) {
				nvlist_t **newchild;

				newchild = zutil_alloc(hdl, (max_id) *
				    sizeof (nvlist_t *));
				if (newchild == NULL)
					goto nomem;

				for (c = 0; c < children; c++)
					newchild[c] = child[c];

				free(child);
				child = newchild;
				children = max_id;
			}
		}

		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);

		/*
		 * The vdev namespace may contain holes as a result of
		 * device removal. We must add them back into the vdev
		 * tree before we process any missing devices.
		 */
		if (holes > 0) {
			ASSERT(valid_top_config);

			for (c = 0; c < children; c++) {
				nvlist_t *holey;

				if (child[c] != NULL ||
				    !vdev_is_hole(hole_array, holes, c))
					continue;

				if (nvlist_alloc(&holey, NV_UNIQUE_NAME,
				    0) != 0)
					goto nomem;

				/*
				 * Holes in the namespace are treated as
				 * "hole" top-level vdevs and have a
				 * special flag set on them.
				 */
				if (nvlist_add_string(holey,
				    ZPOOL_CONFIG_TYPE,
				    VDEV_TYPE_HOLE) != 0 ||
				    nvlist_add_uint64(holey,
				    ZPOOL_CONFIG_ID, c) != 0 ||
				    nvlist_add_uint64(holey,
				    ZPOOL_CONFIG_GUID, 0ULL) != 0) {
					nvlist_free(holey);
					goto nomem;
				}
				child[c] = holey;
			}
		}

		/*
		 * Look for any missing top-level vdevs.  If this is the case,
		 * create a faked up 'missing' vdev as a placeholder.  We cannot
		 * simply compress the child array, because the kernel performs
		 * certain checks to make sure the vdev IDs match their location
		 * in the configuration.
		 */
		for (c = 0; c < children; c++) {
			if (child[c] == NULL) {
				nvlist_t *missing;
				if (nvlist_alloc(&missing, NV_UNIQUE_NAME,
				    0) != 0)
					goto nomem;
				if (nvlist_add_string(missing,
				    ZPOOL_CONFIG_TYPE,
				    VDEV_TYPE_MISSING) != 0 ||
				    nvlist_add_uint64(missing,
				    ZPOOL_CONFIG_ID, c) != 0 ||
				    nvlist_add_uint64(missing,
				    ZPOOL_CONFIG_GUID, 0ULL) != 0) {
					nvlist_free(missing);
					goto nomem;
				}
				child[c] = missing;
			}
		}

		/*
		 * Put all of this pool's top-level vdevs into a root vdev.
		 */
		if (nvlist_alloc(&nvroot, NV_UNIQUE_NAME, 0) != 0)
			goto nomem;
		if (nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE,
		    VDEV_TYPE_ROOT) != 0 ||
		    nvlist_add_uint64(nvroot, ZPOOL_CONFIG_ID, 0ULL) != 0 ||
		    nvlist_add_uint64(nvroot, ZPOOL_CONFIG_GUID, guid) != 0 ||
		    nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
		    (const nvlist_t **)child, children) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}

		for (c = 0; c < children; c++)
			nvlist_free(child[c]);
		free(child);
		children = 0;
		child = NULL;

		/*
		 * Go through and fix up any paths and/or devids based on our
		 * known list of vdev GUID -> path mappings.
		 */
		if (fix_paths(hdl, nvroot, pl->names) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}

		/*
		 * Add the root vdev to this pool's configuration.
		 */
		if (nvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    nvroot) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}
		nvlist_free(nvroot);

		/*
		 * zdb uses this path to report on active pools that were
		 * imported or created using -R.
		 */
		if (active_ok)
			goto add_pool;

		/*
		 * Determine if this pool is currently active, in which case we
		 * can't actually import it.
		 */
		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0);
		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);

		if (zutil_pool_active(hdl, name, guid, &isactive) != 0)
			goto error;

		if (isactive) {
			nvlist_free(config);
			config = NULL;
			continue;
		}

		if (policy != NULL) {
			if (nvlist_add_nvlist(config, ZPOOL_LOAD_POLICY,
			    policy) != 0)
				goto nomem;
		}

#ifdef OZ_NATIVE_LABEL_CLIENT
		/* Never derive admission from label paths. This list was copied
		 * from the scan cache before any label results were processed. */
		if (admission == NULL || nvlist_add_nvlist(config,
		    OZ_IMPORT_ADMISSION, admission) != 0)
			goto nomem;
#endif
		if ((nvl = zutil_refresh_config(hdl, config)) == NULL) {
			nvlist_free(config);
			config = NULL;
			continue;
		}

		nvlist_free(config);
		config = nvl;
#ifdef OZ_NATIVE_LABEL_CLIENT
		/* Preserve the original candidate selection for the later import
		 * mutation; never reconstruct it from refreshed label paths. */
		if (nvlist_add_nvlist(config, OZ_IMPORT_ADMISSION, admission) != 0)
			goto nomem;
#endif

		/*
		 * Go through and update the paths for spares, now that we have
		 * them.
		 */
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &spares, &nspares) == 0) {
			for (i = 0; i < nspares; i++) {
				if (fix_paths(hdl, spares[i], pl->names) != 0)
					goto nomem;
			}
		}

		/*
		 * Update the paths for l2cache devices.
		 */
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache) == 0) {
			for (i = 0; i < nl2cache; i++) {
				if (fix_paths(hdl, l2cache[i], pl->names) != 0)
					goto nomem;
			}
		}

		/*
		 * Restore the original information read from the actual label.
		 */
		(void) nvlist_remove(config, ZPOOL_CONFIG_HOSTID,
		    DATA_TYPE_UINT64);
		(void) nvlist_remove(config, ZPOOL_CONFIG_HOSTNAME,
		    DATA_TYPE_STRING);
		if (hostid != 0) {
			verify(nvlist_add_uint64(config, ZPOOL_CONFIG_HOSTID,
			    hostid) == 0);
			verify(nvlist_add_string(config, ZPOOL_CONFIG_HOSTNAME,
			    hostname) == 0);
		}

add_pool:
		/*
		 * Add this pool to the list of configs.
		 */
		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0);

		if (nvlist_add_nvlist(ret, name, config) != 0)
			goto nomem;

		nvlist_free(config);
		config = NULL;
	}

	return (ret);

nomem:
	(void) zutil_no_memory(hdl);
error:
	nvlist_free(config);
	nvlist_free(ret);
	for (c = 0; c < children; c++)
		nvlist_free(child[c]);
	free(child);

	return (NULL);
}

/*
 * Return the offset of the given label.
 */
static uint64_t
label_offset(uint64_t size, int l)
{
	ASSERT0(P2PHASE_TYPED(size, sizeof (vdev_label_t), uint64_t));
	return (l * sizeof (vdev_label_t) + (l < VDEV_LABELS / 2 ?
	    0 : size - VDEV_LABELS * sizeof (vdev_label_t)));
}

/*
 * The same description applies as to zpool_read_label below,
 * except here we do it without aio, presumably because an aio call
 * errored out in a way we think not using it could circumvent.
 */
int
zpool_read_label_from(uint64_t size, zpool_label_reader_t reader, void *arg,
    nvlist_t **config, int *num_labels)
{
	int l, count = 0;
	vdev_phys_t *label;
	nvlist_t *expected_config = NULL;
	uint64_t expected_guid = 0;

	if (config == NULL || reader == NULL) {
		errno = EINVAL;
		return (-1);
	}
	*config = NULL;

	if (num_labels != NULL)
		*num_labels = 0;
	size = P2ALIGN_TYPED(size, sizeof (vdev_label_t), uint64_t);
	if (size < VDEV_LABELS * sizeof (vdev_label_t))
		return (0);

	label = (vdev_phys_t *)umem_alloc_aligned(sizeof (*label), PAGESIZE,
	    UMEM_DEFAULT);
	if (label == NULL)
		return (-1);

	for (l = 0; l < VDEV_LABELS; l++) {
		uint64_t state, guid, txg;
		off_t offset = label_offset(size, l) + VDEV_SKIP_SIZE;

		if (reader(arg, label, sizeof (vdev_phys_t), offset) != 0)
			continue;

		if (nvlist_unpack(label->vp_nvlist,
		    sizeof (label->vp_nvlist), config, 0) != 0)
			continue;

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_GUID,
		    &guid) != 0 || guid == 0) {
			nvlist_free(*config);
			continue;
		}

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state > POOL_STATE_L2CACHE) {
			nvlist_free(*config);
			continue;
		}

		if (state != POOL_STATE_SPARE && state != POOL_STATE_L2CACHE &&
		    (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_TXG,
		    &txg) != 0 || txg == 0)) {
			nvlist_free(*config);
			continue;
		}

		if (expected_guid) {
			if (expected_guid == guid)
				count++;

			nvlist_free(*config);
		} else {
			expected_config = *config;
			expected_guid = guid;
			count++;
		}
	}

	if (num_labels != NULL)
		*num_labels = count;

	umem_free_aligned(label, sizeof (*label));
	*config = expected_config;

	return (0);
}

static int
label_fd_read(void *arg, void *buffer, size_t count, uint64_t offset)
{
	int fd = *(int *)arg;
	return (pread64(fd, buffer, count, offset) == (ssize_t)count ? 0 : -1);
}

static int
zpool_read_label_slow(int fd, nvlist_t **config, int *num_labels)
{
	struct stat64 statbuf;
	*config = NULL;
	if (num_labels != NULL)
		*num_labels = 0;
	if (fstat64_blk(fd, &statbuf) == -1)
		return (0);
	return (zpool_read_label_from(statbuf.st_size, label_fd_read, &fd,
	    config, num_labels));
}

/*
 * Given a file descriptor, read the label information and return an nvlist
 * describing the configuration, if there is one.  The number of valid
 * labels found will be returned in num_labels when non-NULL.
 */
int
zpool_read_label(int fd, nvlist_t **config, int *num_labels)
{
#ifndef HAVE_AIO_H
	return (zpool_read_label_slow(fd, config, num_labels));
#else
	struct stat64 statbuf;
	struct aiocb aiocbs[VDEV_LABELS];
	struct aiocb *aiocbps[VDEV_LABELS];
	vdev_phys_t *labels;
	nvlist_t *expected_config = NULL;
	uint64_t expected_guid = 0, size;
	int error, l, count = 0;

	*config = NULL;

	if (fstat64_blk(fd, &statbuf) == -1)
		return (0);
	size = P2ALIGN_TYPED(statbuf.st_size, sizeof (vdev_label_t), uint64_t);

	labels = (vdev_phys_t *)umem_alloc_aligned(
	    VDEV_LABELS * sizeof (*labels), PAGESIZE, UMEM_DEFAULT);
	if (labels == NULL)
		return (-1);

	memset(aiocbs, 0, sizeof (aiocbs));
	for (l = 0; l < VDEV_LABELS; l++) {
		off_t offset = label_offset(size, l) + VDEV_SKIP_SIZE;

		aiocbs[l].aio_fildes = fd;
		aiocbs[l].aio_offset = offset;
		aiocbs[l].aio_buf = &labels[l];
		aiocbs[l].aio_nbytes = sizeof (vdev_phys_t);
		aiocbs[l].aio_lio_opcode = LIO_READ;
		aiocbps[l] = &aiocbs[l];
	}

	if (lio_listio(LIO_WAIT, aiocbps, VDEV_LABELS, NULL) != 0) {
		int saved_errno = errno;
		boolean_t do_slow = B_FALSE;
		error = -1;

		if (errno == EAGAIN || errno == EINTR || errno == EIO) {
			/*
			 * A portion of the requests may have been submitted.
			 * Clean them up.
			 */
			for (l = 0; l < VDEV_LABELS; l++) {
				errno = 0;
				switch (aio_error(&aiocbs[l])) {
				case EINVAL:
					break;
				case EINPROGRESS:
					/*
					 * This shouldn't be possible to
					 * encounter, die if we do.
					 */
					ASSERT(B_FALSE);
					zfs_fallthrough;
				case EREMOTEIO:
					/*
					 * May be returned by an NVMe device
					 * which is visible in /dev/ but due
					 * to a low-level format change, or
					 * other error, needs to be rescanned.
					 * Try the slow method.
					 */
					zfs_fallthrough;
				case EAGAIN:
				case EOPNOTSUPP:
				case ENOSYS:
					do_slow = B_TRUE;
					zfs_fallthrough;
				case 0:
				default:
					(void) aio_return(&aiocbs[l]);
				}
			}
		}
		if (do_slow) {
			/*
			 * At least some IO involved access unsafe-for-AIO
			 * files. Let's try again, without AIO this time.
			 */
			error = zpool_read_label_slow(fd, config, num_labels);
			saved_errno = errno;
		}
		umem_free_aligned(labels, VDEV_LABELS * sizeof (*labels));
		errno = saved_errno;
		return (error);
	}

	for (l = 0; l < VDEV_LABELS; l++) {
		uint64_t state, guid, txg;

		if (aio_return(&aiocbs[l]) != sizeof (vdev_phys_t))
			continue;

		if (nvlist_unpack(labels[l].vp_nvlist,
		    sizeof (labels[l].vp_nvlist), config, 0) != 0)
			continue;

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_GUID,
		    &guid) != 0 || guid == 0) {
			nvlist_free(*config);
			continue;
		}

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state > POOL_STATE_L2CACHE) {
			nvlist_free(*config);
			continue;
		}

		if (state != POOL_STATE_SPARE && state != POOL_STATE_L2CACHE &&
		    (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_TXG,
		    &txg) != 0 || txg == 0)) {
			nvlist_free(*config);
			continue;
		}

		if (expected_guid) {
			if (expected_guid == guid)
				count++;

			nvlist_free(*config);
		} else {
			expected_config = *config;
			expected_guid = guid;
			count++;
		}
	}

	if (num_labels != NULL)
		*num_labels = count;

	umem_free_aligned(labels, VDEV_LABELS * sizeof (*labels));
	*config = expected_config;

	return (0);
#endif
}

/*
 * Sorted by full path and then vdev guid to allow for multiple entries with
 * the same full path name.  This is required because it's possible to
 * have multiple block devices with labels that refer to the same
 * ZPOOL_CONFIG_PATH yet have different vdev guids.  In this case both
 * entries need to be added to the cache.  Scenarios where this can occur
 * include overwritten pool labels, devices which are visible from multiple
 * hosts and multipath devices.
 */
int
slice_cache_compare(const void *arg1, const void *arg2)
{
	const char  *nm1 = ((rdsk_node_t *)arg1)->rn_name;
	const char  *nm2 = ((rdsk_node_t *)arg2)->rn_name;
	uint64_t guid1 = ((rdsk_node_t *)arg1)->rn_vdev_guid;
	uint64_t guid2 = ((rdsk_node_t *)arg2)->rn_vdev_guid;
	int rv;

	rv = TREE_ISIGN(strcmp(nm1, nm2));
	if (rv)
		return (rv);

	return (TREE_CMP(guid1, guid2));
}

static int
label_paths_impl(libpc_handle_t *hdl, nvlist_t *nvroot, uint64_t pool_guid,
    uint64_t vdev_guid, const char **path, const char **devid)
{
	nvlist_t **child;
	uint_t c, children;
	uint64_t guid;
	const char *val;
	int error;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++) {
			error  = label_paths_impl(hdl, child[c],
			    pool_guid, vdev_guid, path, devid);
			if (error)
				return (error);
		}
		return (0);
	}

	if (nvroot == NULL)
		return (0);

	error = nvlist_lookup_uint64(nvroot, ZPOOL_CONFIG_GUID, &guid);
	if ((error != 0) || (guid != vdev_guid))
		return (0);

	error = nvlist_lookup_string(nvroot, ZPOOL_CONFIG_PATH, &val);
	if (error == 0)
		*path = val;

	error = nvlist_lookup_string(nvroot, ZPOOL_CONFIG_DEVID, &val);
	if (error == 0)
		*devid = val;

	return (0);
}

/*
 * Given a disk label fetch the ZPOOL_CONFIG_PATH and ZPOOL_CONFIG_DEVID
 * and store these strings as config_path and devid_path respectively.
 * The returned pointers are only valid as long as label remains valid.
 */
int
label_paths(libpc_handle_t *hdl, nvlist_t *label, const char **path,
    const char **devid)
{
	nvlist_t *nvroot;
	uint64_t pool_guid;
	uint64_t vdev_guid;
	uint64_t state;

	*path = NULL;
	*devid = NULL;
	if (nvlist_lookup_uint64(label, ZPOOL_CONFIG_GUID, &vdev_guid) != 0)
		return (ENOENT);

	/*
	 * In case of spare or l2cache, we directly return path/devid from the
	 * label.
	 */
	if (!(nvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_STATE, &state)) &&
	    (state == POOL_STATE_SPARE || state == POOL_STATE_L2CACHE)) {
		(void) nvlist_lookup_string(label, ZPOOL_CONFIG_PATH, path);
		(void) nvlist_lookup_string(label, ZPOOL_CONFIG_DEVID, devid);
		return (0);
	}

	if (nvlist_lookup_nvlist(label, ZPOOL_CONFIG_VDEV_TREE, &nvroot) ||
	    nvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_GUID, &pool_guid))
		return (ENOENT);

	return (label_paths_impl(hdl, nvroot, pool_guid, vdev_guid, path,
	    devid));
}

static void
zpool_find_import_scan_add_slice(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t *cache, const char *path, const char *name, int order)
{
	avl_index_t where;
	rdsk_node_t *slice;

	slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
	/* AROS realpath may preserve a volume-root trailing slash. Do not
	 * synthesize a double separator before native DOS conversion. */
	size_t path_length = strlen(path);
	const char *separator = path_length && path[path_length - 1] == '/' ? "" : "/";
	if (asprintf(&slice->rn_name, "%s%s%s", path, separator, name) == -1) {
		free(slice);
		return;
	}
	slice->rn_vdev_guid = 0;
	slice->rn_lock = lock;
	slice->rn_avl = cache;
	slice->rn_hdl = hdl;
	slice->rn_order = order + IMPORT_ORDER_SCAN_OFFSET;
	slice->rn_labelpaths = B_FALSE;

	pthread_mutex_lock(lock);
	if (avl_find(cache, slice, &where)) {
		free(slice->rn_name);
		free(slice);
	} else {
		avl_insert(cache, slice, where);
	}
	pthread_mutex_unlock(lock);
}

static int
zpool_find_import_scan_dir(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t *cache, const char *dir, int order)
{
	int error;
	char path[MAXPATHLEN];
	struct dirent64 *dp;
	DIR *dirp;

	if (realpath(dir, path) == NULL) {
		error = errno;
		if (error == ENOENT)
			return (0);

		zutil_error_aux(hdl, "%s", zfs_strerror(error));
		(void) zutil_error_fmt(hdl, LPC_BADPATH, dgettext(TEXT_DOMAIN,
		    "cannot resolve path '%s'"), dir);
		return (error);
	}

	dirp = opendir(path);
	if (dirp == NULL) {
		error = errno;
		zutil_error_aux(hdl, "%s", zfs_strerror(error));
		(void) zutil_error_fmt(hdl, LPC_BADPATH, dgettext(TEXT_DOMAIN,
		    "cannot open '%s'"), path);
		return (error);
	}

	while ((dp = readdir64(dirp)) != NULL) {
		const char *name = dp->d_name;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
			continue;

		switch (dp->d_type) {
		case DT_UNKNOWN:
		case DT_BLK:
		case DT_LNK:
#ifdef __FreeBSD__
		case DT_CHR:
#endif
		case DT_REG:
			break;
		default:
			continue;
		}

		zpool_find_import_scan_add_slice(hdl, lock, cache, path, name,
		    order);
	}

	(void) closedir(dirp);
	return (0);
}

static int
zpool_find_import_scan_path(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t *cache, const char *dir, int order)
{
	int error = 0;
	char path[MAXPATHLEN];
	char *d = NULL;
	ssize_t dl;
	const char *dpath, *name;

	/*
	 * Separate the directory and the basename.
	 * We do this so that we can get the realpath of
	 * the directory. We don't get the realpath on the
	 * whole path because if it's a symlink, we want the
	 * path of the symlink not where it points to.
	 */
	name = zfs_basename(dir);
	if ((dl = zfs_dirnamelen(dir)) == -1)
		dpath = ".";
	else
		dpath = d = zutil_strndup(hdl, dir, dl);

	if (realpath(dpath, path) == NULL) {
		error = errno;
		if (error == ENOENT) {
			error = 0;
			goto out;
		}

		zutil_error_aux(hdl, "%s", zfs_strerror(error));
		(void) zutil_error_fmt(hdl, LPC_BADPATH, dgettext(TEXT_DOMAIN,
		    "cannot resolve path '%s'"), dir);
		goto out;
	}

	zpool_find_import_scan_add_slice(hdl, lock, cache, path, name, order);

out:
	free(d);
	return (error);
}

/*
 * Scan a list of directories for zfs devices.
 */
#ifndef OZ_NATIVE_LABEL_CLIENT
static
#endif
int
zpool_find_import_scan(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t **slice_cache, const char * const *dir, size_t dirs)
{
	avl_tree_t *cache;
	rdsk_node_t *slice;
	void *cookie;
	int i, error;

	*slice_cache = NULL;
	cache = zutil_alloc(hdl, sizeof (avl_tree_t));
	avl_create(cache, slice_cache_compare, sizeof (rdsk_node_t),
	    offsetof(rdsk_node_t, rn_node));

	for (i = 0; i < dirs; i++) {
		struct stat sbuf;
#if defined(__AROS__) && !defined(OZ_NATIVE_LABEL_CLIENT)
		if (strncmp(dir[i], "/AROS/", 6) == 0) {
			struct oz_device *device = NULL;
			error = oz_device_bound_open(dir[i], 0, &device);
			oz_device_close(device);
			/* Explicit unavailable binding: keep it in the admission list,
			 * but discover labels only from surviving members. */
			if (error == ENODEV) continue;
			if (error) goto error;
			const char *name = strrchr(dir[i], '/');
			char *parent = zutil_strndup(hdl, dir[i], name - dir[i]);
			zpool_find_import_scan_add_slice(hdl, lock, cache,
			    parent, name + 1, i);
			free(parent);
			continue;
		}
#endif
#ifdef OZ_NATIVE_LABEL_CLIENT
		if (strcmp(dir[i], "/AROS") == 0) {
			char **paths = NULL;
			size_t count = 0;
			error = oz_native_import_paths(&paths, &count);
			if (error) {
				if (error == EACCES || error == EPERM)
					hdl->lpc_open_access_error = B_TRUE;
				zutil_error_aux(hdl, "%s", zfs_strerror(error));
				(void) zutil_error_fmt(hdl, LPC_BADPATH,
				    dgettext(TEXT_DOMAIN, "cannot enumerate native storage"));
				goto error;
			}
			for (size_t n = 0; n < count; n++) {
				char *name = strrchr(paths[n], '/');
				*name = '\0';
				zpool_find_import_scan_add_slice(hdl, lock, cache,
				    paths[n], name + 1, i);
				*name = '/';
			}
			oz_native_import_paths_free(paths, count);
			continue;
		}
		/* Native device/unit names are not DOS filesystem paths. Only
		 * explicit canonical candidates enter the cache; labels cannot
		 * cause enumeration of other devices or grant write ownership. */
		if (strncmp(dir[i], "/AROS/", 6) == 0) {
			struct oz_device_path native;
			error = oz_device_path_parse(dir[i], &native);
			if (error != 0) {
				zutil_error_aux(hdl, "%s", zfs_strerror(error));
				(void) zutil_error_fmt(hdl, LPC_BADPATH,
				    dgettext(TEXT_DOMAIN, "invalid native path '%s'"), dir[i]);
				goto error;
			}
			const char *name = strrchr(dir[i], '/');
			char *parent = zutil_strndup(hdl, dir[i], name - dir[i]);
			zpool_find_import_scan_add_slice(hdl, lock, cache,
			    parent, name + 1, i);
			free(parent);
			continue;
		}
#endif

		if (stat(dir[i], &sbuf) != 0) {
			error = errno;
			if (error == ENOENT) {
#ifdef OZ_NATIVE_LABEL_CLIENT
				/* Preserve an explicitly supplied missing member in the
				 * admission list. Labels cannot authorize this path for us.
				 * Resolve its existing parent; the label reader skips the
				 * absent file and the service pins/validates it at import. */
				if ((error = zpool_find_import_scan_path(hdl, lock,
				    cache, dir[i], i)) != 0)
					goto error;
#endif
				continue;
			}

			zutil_error_aux(hdl, "%s", zfs_strerror(error));
			(void) zutil_error_fmt(hdl, LPC_BADPATH, dgettext(
			    TEXT_DOMAIN, "cannot resolve path '%s'"), dir[i]);
			goto error;
		}

		/*
		 * If dir[i] is a directory, we walk through it and add all
		 * the entries to the cache. If it's not a directory, we just
		 * add it to the cache.
		 */
		if (S_ISDIR(sbuf.st_mode)) {
			if ((error = zpool_find_import_scan_dir(hdl, lock,
			    cache, dir[i], i)) != 0)
				goto error;
		} else {
			if ((error = zpool_find_import_scan_path(hdl, lock,
			    cache, dir[i], i)) != 0)
				goto error;
		}
	}

	*slice_cache = cache;
	return (0);

error:
	cookie = NULL;
	while ((slice = avl_destroy_nodes(cache, &cookie)) != NULL) {
		free(slice->rn_name);
		free(slice);
	}
	free(cache);

	return (error);
}

/*
 * Given a list of directories to search, find all pools stored on disk.  This
 * includes partial pools which are not available to import.  If no args are
 * given (argc is 0), then the default directory (/dev/dsk) is searched.
 * poolname or guid (but not both) are provided by the caller when trying
 * to import a specific pool.
 */
#ifndef OZ_NATIVE_LABEL_CLIENT
static
#endif
nvlist_t *
zpool_find_import_impl(libpc_handle_t *hdl, importargs_t *iarg,
    pthread_mutex_t *lock, avl_tree_t *cache)
{
	(void) lock;
	nvlist_t *ret = NULL;
	nvlist_t *admission = NULL;
	pool_list_t pools = { 0 };
	pool_entry_t *pe, *penext;
	vdev_entry_t *ve, *venext;
	config_entry_t *ce, *cenext;
	name_entry_t *ne, *nenext;
	rdsk_node_t *slice;
	void *cookie;
	tpool_t *t;

	verify(iarg->poolname == NULL || iarg->guid == 0);

	/*
	 * Create a thread pool to parallelize the process of reading and
	 * validating labels, a large number of threads can be used due to
	 * minimal contention.
	 */
#ifdef __AROS__
	/* Native processor.resource query shared with the libzpool adapter. */
	long threads = 2 * boot_ncpus;
#else
	long threads = 2 * sysconf(_SC_NPROCESSORS_ONLN);
#endif
#ifdef HAVE_AIO_H
	long am;
#ifdef _SC_AIO_LISTIO_MAX
	am = sysconf(_SC_AIO_LISTIO_MAX);
	if (am >= VDEV_LABELS)
		threads = MIN(threads, am / VDEV_LABELS);
#endif
#ifdef _SC_AIO_MAX
	am = sysconf(_SC_AIO_MAX);
	if (am >= VDEV_LABELS)
		threads = MIN(threads, am / VDEV_LABELS);
#endif
#endif
#ifdef OZ_NATIVE_LABEL_CLIENT
	/* One parent-owned session per scan, not one slot per worker. The parent
	 * outlives all readers; each worker still opens its own retained device.
	 * Bound concurrency by the descriptor capacity of that session. */
	struct oz_ticket native_session = { 0 };
	int native_error = 0;
	boolean_t native_open = B_FALSE, has_native = B_FALSE;
	const char *selected[32];unsigned selected_count = 0;
	for (slice = avl_first(cache); slice;
	    slice = avl_walk(cache, slice, AVL_AFTER)) {
		if (selected_count == 32) { native_error = E2BIG; break; }
		selected[selected_count++] = slice->rn_name;
	}
	if (!native_error)
		native_error = nvlist_alloc(&admission, NV_UNIQUE_NAME, 0);
	if (!native_error)
		native_error = nvlist_add_string_array(admission,
		    OZ_IMPORT_PATHS, selected, selected_count);
	for (slice = avl_first(cache); slice;
	    slice = avl_walk(cache, slice, AVL_AFTER))
		if (strncmp(slice->rn_name, "/AROS/", 6) == 0)
			has_native = B_TRUE;
	if (has_native && !native_error) {
		native_error = oz_session_open(OZ_ADMISSION_PORT, &native_session);
		native_open = native_error == 0;
		threads = MIN(threads, OZ_SESSION_FILE_LIMIT);
	}
	if (!native_error) {
#endif
	t = tpool_create(1, threads, 0, NULL);
	for (slice = avl_first(cache); slice;
	    (slice = avl_walk(cache, slice, AVL_AFTER))) {
#ifdef OZ_NATIVE_LABEL_CLIENT
		slice->rn_native_session = native_open ? &native_session : NULL;
#endif
		(void) tpool_dispatch(t, zpool_open_func, slice);
	}

	tpool_wait(t);
	tpool_destroy(t);
#ifdef OZ_NATIVE_LABEL_CLIENT
	}
	if (native_open)
		native_error = oz_session_close(OZ_ADMISSION_PORT, &native_session);
	if (native_error) {
		zutil_error_aux(hdl, "%s", zfs_strerror(native_error));
		(void) zutil_error_fmt(hdl, LPC_BADPATH,
		    dgettext(TEXT_DOMAIN, "native import session failed"));
	}
#endif

	/*
	 * Process the cache, filtering out any entries which are not
	 * for the specified pool then adding matching label configs.
	 */
	cookie = NULL;
	while ((slice = avl_destroy_nodes(cache, &cookie)) != NULL) {
		if (slice->rn_config != NULL) {
			nvlist_t *config = slice->rn_config;
			boolean_t matched = B_TRUE;
			boolean_t aux = B_FALSE;
			int fd;

			/*
			 * Check if it's a spare or l2cache device. If it is,
			 * we need to skip the name and guid check since they
			 * don't exist on aux device label.
			 */
			if (iarg->poolname != NULL || iarg->guid != 0) {
				uint64_t state;
				aux = nvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_POOL_STATE, &state) == 0 &&
				    (state == POOL_STATE_SPARE ||
				    state == POOL_STATE_L2CACHE);
			}

			if (iarg->poolname != NULL && !aux) {
				const char *pname;

				matched = nvlist_lookup_string(config,
				    ZPOOL_CONFIG_POOL_NAME, &pname) == 0 &&
				    strcmp(iarg->poolname, pname) == 0;
			} else if (iarg->guid != 0 && !aux) {
				uint64_t this_guid;

				matched = nvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_POOL_GUID, &this_guid) == 0 &&
				    iarg->guid == this_guid;
			}
			if (matched) {
				/*
				 * Verify all remaining entries can be opened
				 * exclusively. This will prune all underlying
				 * multipath devices which otherwise could
				 * result in the vdev appearing as UNAVAIL.
				 *
				 * Under zdb, this step isn't required and
				 * would prevent a zdb -e of active pools with
				 * no cachefile.
				 */
#ifdef __AROS__
				/* AROS rejects O_EXCL without O_CREAT. This adapter
				 * only admits regular file vdevs, not multipath/block
				 * devices; do not invent exclusive device ownership.
				 * Creation/import ownership remains caller policy. */
				struct stat file_stat;
				int native_admitted = 0;
#ifdef OZ_NATIVE_LABEL_CLIENT
				/* This candidate already passed the authenticated native
				 * label reader. It is not a DOS path and cannot pass stat
				 * or open. Retaining its config grants no write ownership;
				 * TRYIMPORT/import still need service-side admission. */
				native_admitted = strncmp(slice->rn_name, "/AROS/", 6) == 0;
#else
				if (strncmp(slice->rn_name, "/AROS/", 6) == 0) {
					struct oz_device *device = NULL;
					native_admitted = oz_device_bound_open(
					    slice->rn_name, 0, &device) == 0;
					oz_device_close(device);
				}
#endif
				fd = -1;
				if (stat(slice->rn_name, &file_stat) == 0 &&
				    S_ISREG(file_stat.st_mode))
					fd = open(slice->rn_name, O_RDONLY | O_CLOEXEC);
#else
				fd = open(slice->rn_name,
				    O_RDONLY | O_EXCL | O_CLOEXEC);
#endif
				if (fd >= 0 || iarg->can_be_active
#ifdef __AROS__
				    || native_admitted
#endif
				    ) {
					if (fd >= 0)
						close(fd);
					add_config(hdl, &pools,
					    slice->rn_name, slice->rn_order,
					    slice->rn_num_labels, config);
				}
			}
			nvlist_free(config);
		}
		free(slice->rn_name);
		free(slice);
	}
	avl_destroy(cache);
	free(cache);

#ifdef OZ_NATIVE_LABEL_CLIENT
	if (!native_error)
#endif
		ret = get_configs(hdl, &pools, iarg->can_be_active, iarg->policy, admission);
	nvlist_free(admission);

	for (pe = pools.pools; pe != NULL; pe = penext) {
		penext = pe->pe_next;
		for (ve = pe->pe_vdevs; ve != NULL; ve = venext) {
			venext = ve->ve_next;
			for (ce = ve->ve_configs; ce != NULL; ce = cenext) {
				cenext = ce->ce_next;
				nvlist_free(ce->ce_config);
				free(ce);
			}
			free(ve);
		}
		free(pe);
	}

	for (ne = pools.names; ne != NULL; ne = nenext) {
		nenext = ne->ne_next;
		free(ne->ne_name);
		free(ne);
	}

	return (ret);
}

/*
 * Given a config, discover the paths for the devices which
 * exist in the config.
 */
static int
discover_cached_paths(libpc_handle_t *hdl, nvlist_t *nv,
    avl_tree_t *cache, pthread_mutex_t *lock)
{
	const char *path = NULL;
	ssize_t dl;
	uint_t children;
	nvlist_t **child;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (int c = 0; c < children; c++) {
			discover_cached_paths(hdl, child[c], cache, lock);
		}
	}

	/*
	 * Once we have the path, we need to add the directory to
	 * our directory cache.
	 */
	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0) {
		int ret;
		char c = '\0';
		if ((dl = zfs_dirnamelen(path)) == -1) {
			path = ".";
		} else {
			c = path[dl];
			((char *)path)[dl] = '\0';

		}
		ret = zpool_find_import_scan_dir(hdl, lock, cache,
		    path, 0);
		if (c != '\0')
			((char *)path)[dl] = c;

		return (ret);
	}
	return (0);
}

/*
 * Given a cache file, return the contents as a list of importable pools.
 * poolname or guid (but not both) are provided by the caller when trying
 * to import a specific pool.
 */
#ifdef OZ_NATIVE_LABEL_CLIENT
/* A user-selected cache is a source of candidate paths, never authority to
 * bypass the engine's identity, topology, credential or ownership checks.
 * Do not expand this selection from subsequently refreshed device labels. */
static int
aros_cached_paths(nvlist_t *tree, const char **paths, unsigned *count,
    unsigned depth, unsigned *nodes)
{
	const char *type, *path;
	if (depth > 16 || ++*nodes > 256 ||
	    nvlist_lookup_string(tree, ZPOOL_CONFIG_TYPE, &type))
		return (EINVAL);
	if (!strcmp(type, VDEV_TYPE_FILE) || !strcmp(type, VDEV_TYPE_DISK)) {
		if (nvlist_lookup_string(tree, ZPOOL_CONFIG_PATH, &path) ||
		    path[0] != '/' || strlen(path) >= 256)
			return (EINVAL);
		unsigned i;
		for (i = 0; i < *count; i++)
			if (!strcmp(paths[i], path)) break;
		if (i == *count) {
			if (*count == 32) return (E2BIG);
			paths[(*count)++] = path;
		}
	}
	const char *arrays[] = { ZPOOL_CONFIG_CHILDREN, ZPOOL_CONFIG_SPARES,
	    ZPOOL_CONFIG_L2CACHE };
	for (unsigned a = 0; a < 3; a++) {
		if (!nvlist_exists(tree, arrays[a])) continue;
		nvlist_t **children; uint_t n;
		if (nvlist_lookup_nvlist_array(tree, arrays[a], &children, &n) || n > 32)
			return (EINVAL);
		for (unsigned i = 0; i < n; i++) {
			int error = aros_cached_paths(children[i], paths, count,
			    depth + 1, nodes);
			if (error) return (error);
		}
	}
	return (0);
}
#endif
static nvlist_t *
zpool_find_import_cached(libpc_handle_t *hdl, importargs_t *iarg)
{
	char *buf;
	int fd;
	struct stat64 statbuf;
	nvlist_t *raw, *src, *dst;
	nvlist_t *pools;
	nvpair_t *elem;
	const char *name;
	uint64_t this_guid;
	boolean_t active;

	verify(iarg->poolname == NULL || iarg->guid == 0);

	if ((fd = open(iarg->cachefile, O_RDONLY | O_CLOEXEC)) < 0) {
		zutil_error_aux(hdl, "%s", zfs_strerror(errno));
		(void) zutil_error(hdl, LPC_BADCACHE, dgettext(TEXT_DOMAIN,
		    "failed to open cache file"));
		return (NULL);
	}

	if (fstat64(fd, &statbuf) != 0) {
		zutil_error_aux(hdl, "%s", zfs_strerror(errno));
		(void) close(fd);
		(void) zutil_error(hdl, LPC_BADCACHE, dgettext(TEXT_DOMAIN,
		    "failed to get size of cache file"));
		return (NULL);
	}

	if ((buf = zutil_alloc(hdl, statbuf.st_size)) == NULL) {
		(void) close(fd);
		return (NULL);
	}

	if (read(fd, buf, statbuf.st_size) != statbuf.st_size) {
		(void) close(fd);
		free(buf);
		(void) zutil_error(hdl, LPC_BADCACHE, dgettext(TEXT_DOMAIN,
		    "failed to read cache file contents"));
		return (NULL);
	}

	(void) close(fd);

	if (nvlist_unpack(buf, statbuf.st_size, &raw, 0) != 0) {
		free(buf);
		(void) zutil_error(hdl, LPC_BADCACHE, dgettext(TEXT_DOMAIN,
		    "invalid or corrupt cache file contents"));
		return (NULL);
	}

	free(buf);

	/*
	 * Go through and get the current state of the pools and refresh their
	 * state.
	 */
	if (nvlist_alloc(&pools, 0, 0) != 0) {
		(void) zutil_no_memory(hdl);
		nvlist_free(raw);
		return (NULL);
	}

	elem = NULL;
	while ((elem = nvlist_next_nvpair(raw, elem)) != NULL) {
#ifdef OZ_NATIVE_LABEL_CLIENT
		/* A cache is external input, not an internal verified nvlist.
		 * Report malformed metadata rather than terminating the CLI. */
		if (nvpair_value_nvlist(elem, &src) != 0 ||
		    nvlist_lookup_string(src, ZPOOL_CONFIG_POOL_NAME, &name) != 0 ||
		    nvlist_lookup_uint64(src, ZPOOL_CONFIG_POOL_GUID, &this_guid) != 0 ||
		    name[0] == '\0' || this_guid == 0) {
			nvlist_free(raw);
			nvlist_free(pools);
			(void) zutil_error(hdl, LPC_BADCACHE, dgettext(TEXT_DOMAIN,
			    "invalid native cache pool metadata"));
			return (NULL);
		}
		/* Validate the bounded tree before either the scan walker or the
		 * platform path updater, which assume a trusted vdev nvlist. */
		nvlist_t *checked_tree = NULL;
		const char *checked_paths[32];
		unsigned checked_count = 0, checked_nodes = 0;
		if (nvlist_lookup_nvlist(src, ZPOOL_CONFIG_VDEV_TREE, &checked_tree) ||
		    aros_cached_paths(checked_tree, checked_paths, &checked_count,
		    0, &checked_nodes) || !checked_count) {
			nvlist_free(raw);
			nvlist_free(pools);
			(void) zutil_error(hdl, LPC_BADCACHE, dgettext(TEXT_DOMAIN,
			    "invalid native cache path selection"));
			return (NULL);
		}
#else
		src = fnvpair_value_nvlist(elem);

		name = fnvlist_lookup_string(src, ZPOOL_CONFIG_POOL_NAME);
		this_guid = fnvlist_lookup_uint64(src, ZPOOL_CONFIG_POOL_GUID);
#endif
		if (iarg->poolname != NULL && strcmp(iarg->poolname, name) != 0)
			continue;

		if (iarg->guid != 0 && iarg->guid != this_guid)
			continue;

		if (zutil_pool_active(hdl, name, this_guid, &active) != 0) {
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}

		if (active)
			continue;

		if (iarg->scan) {
			uint64_t saved_guid = iarg->guid;
			const char *saved_poolname = iarg->poolname;
			pthread_mutex_t lock;

			/*
			 * Create the device cache that will hold the
			 * devices we will scan based on the cachefile.
			 * This will get destroyed and freed by
			 * zpool_find_import_impl.
			 */
			avl_tree_t *cache = zutil_alloc(hdl,
			    sizeof (avl_tree_t));
			avl_create(cache, slice_cache_compare,
			    sizeof (rdsk_node_t),
			    offsetof(rdsk_node_t, rn_node));
			nvlist_t *nvroot = fnvlist_lookup_nvlist(src,
			    ZPOOL_CONFIG_VDEV_TREE);

			/*
			 * We only want to find the pool with this_guid.
			 * We will reset these values back later.
			 */
			iarg->guid = this_guid;
			iarg->poolname = NULL;

			/*
			 * We need to build up a cache of devices that exists
			 * in the paths pointed to by the cachefile. This allows
			 * us to preserve the device namespace that was
			 * originally specified by the user but also lets us
			 * scan devices in those directories in case they had
			 * been renamed.
			 */
			pthread_mutex_init(&lock, NULL);
			discover_cached_paths(hdl, nvroot, cache, &lock);
			nvlist_t *nv = zpool_find_import_impl(hdl, iarg,
			    &lock, cache);
			pthread_mutex_destroy(&lock);

			/*
			 * zpool_find_import_impl will return back
			 * a list of pools that it found based on the
			 * device cache. There should only be one pool
			 * since we're looking for a specific guid.
			 * We will use that pool to build up the final
			 * pool nvlist which is returned back to the
			 * caller.
			 */
			nvpair_t *pair = nvlist_next_nvpair(nv, NULL);
			if (pair == NULL)
				continue;
			fnvlist_add_nvlist(pools, nvpair_name(pair),
			    fnvpair_value_nvlist(pair));

			VERIFY0P(nvlist_next_nvpair(nv, pair));

			iarg->guid = saved_guid;
			iarg->poolname = saved_poolname;
			continue;
		}

		if (nvlist_add_string(src, ZPOOL_CONFIG_CACHEFILE,
		    iarg->cachefile) != 0) {
			(void) zutil_no_memory(hdl);
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}

		update_vdevs_config_dev_sysfs_path(src);

#ifdef OZ_NATIVE_LABEL_CLIENT
		const char *selected[32]; unsigned count = 0, nodes = 0;
		nvlist_t *tree = NULL, *admission = NULL;
		int selection_error = nvlist_lookup_nvlist(src,
		    ZPOOL_CONFIG_VDEV_TREE, &tree);
		if (!selection_error)
			selection_error = aros_cached_paths(tree, selected, &count, 0, &nodes);
		if (!selection_error && !count) selection_error = EINVAL;
		if (!selection_error)
			selection_error = nvlist_alloc(&admission, NV_UNIQUE_NAME, 0);
		if (!selection_error)
			selection_error = nvlist_add_string_array(admission,
			    OZ_IMPORT_PATHS, selected, count);
		/* Ignore any admission record stored in an untrusted cache. */
		(void) nvlist_remove_all(src, OZ_IMPORT_ADMISSION);
		if (!selection_error)
			selection_error = nvlist_add_nvlist(src, OZ_IMPORT_ADMISSION, admission);
		if (selection_error) {
			nvlist_free(admission); nvlist_free(raw); nvlist_free(pools);
			(void) zutil_error(hdl, LPC_BADCACHE,
			    dgettext(TEXT_DOMAIN, "invalid native cache path selection"));
			return (NULL);
		}
#endif

		if ((dst = zutil_refresh_config(hdl, src)) == NULL) {
#ifdef OZ_NATIVE_LABEL_CLIENT
			nvlist_free(admission);
#endif
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}

#ifdef OZ_NATIVE_LABEL_CLIENT
		int admission_error = nvlist_add_nvlist(dst, OZ_IMPORT_ADMISSION, admission);
		nvlist_free(admission);
		if (admission_error) {
			nvlist_free(dst); nvlist_free(raw); nvlist_free(pools);
			(void) zutil_no_memory(hdl);
			return (NULL);
		}
#endif
		if (nvlist_add_nvlist(pools, nvpair_name(elem), dst) != 0) {
			(void) zutil_no_memory(hdl);
			nvlist_free(dst);
			nvlist_free(raw);
			nvlist_free(pools);
			return (NULL);
		}
		nvlist_free(dst);
	}
	nvlist_free(raw);
	return (pools);
}

static nvlist_t *
zpool_find_import(libpc_handle_t *hdl, importargs_t *iarg)
{
	pthread_mutex_t lock;
	avl_tree_t *cache;
	nvlist_t *pools = NULL;

	verify(iarg->poolname == NULL || iarg->guid == 0);
	pthread_mutex_init(&lock, NULL);

	/*
	 * Locate pool member vdevs by blkid or by directory scanning.
	 * On success a newly allocated AVL tree which is populated with an
	 * entry for each discovered vdev will be returned in the cache.
	 * It's the caller's responsibility to consume and destroy this tree.
	 */
	if (iarg->scan || iarg->paths != 0) {
		size_t dirs = iarg->paths;
		const char * const *dir = (const char * const *)iarg->path;

		if (dirs == 0)
			dir = zpool_default_search_paths(&dirs);

		if (zpool_find_import_scan(hdl, &lock, &cache,
		    dir, dirs) != 0) {
			pthread_mutex_destroy(&lock);
			return (NULL);
		}
	} else {
		if (zpool_find_import_blkid(hdl, &lock, &cache) != 0) {
			pthread_mutex_destroy(&lock);
			return (NULL);
		}
	}

	pools = zpool_find_import_impl(hdl, iarg, &lock, cache);
	pthread_mutex_destroy(&lock);
	return (pools);
}


nvlist_t *
zpool_search_import(libpc_handle_t *hdl, importargs_t *import)
{
	nvlist_t *pools = NULL;

	verify(import->poolname == NULL || import->guid == 0);

	if (import->cachefile != NULL)
		pools = zpool_find_import_cached(hdl, import);
	else
		pools = zpool_find_import(hdl, import);

	if ((pools == NULL || nvlist_empty(pools)) &&
	    hdl->lpc_open_access_error && geteuid() != 0) {
		(void) zutil_error(hdl, LPC_EACCESS, dgettext(TEXT_DOMAIN,
		    "no pools found"));
	}

	return (pools);
}

static boolean_t
pool_match(nvlist_t *cfg, const char *tgt)
{
	uint64_t v, guid = strtoull(tgt, NULL, 0);
	const char *s;

	if (guid != 0) {
		if (nvlist_lookup_uint64(cfg, ZPOOL_CONFIG_POOL_GUID, &v) == 0)
			return (v == guid);
	} else {
		if (nvlist_lookup_string(cfg, ZPOOL_CONFIG_POOL_NAME, &s) == 0)
			return (strcmp(s, tgt) == 0);
	}
	return (B_FALSE);
}

int
zpool_find_config(libpc_handle_t *hdl, const char *target, nvlist_t **configp,
    importargs_t *args)
{
	nvlist_t *pools;
	nvlist_t *match = NULL;
	nvlist_t *config = NULL;
	char *sepp = NULL;
	int count = 0;
	char *targetdup = strdup(target);

	if (targetdup == NULL)
		return (ENOMEM);

	*configp = NULL;

	if ((sepp = strpbrk(targetdup, "/@")) != NULL)
		*sepp = '\0';

	pools = zpool_search_import(hdl, args);
	if (pools == NULL) {
		zutil_error_aux(hdl, dgettext(TEXT_DOMAIN, "no pools found"));
		(void) zutil_error_fmt(hdl, LPC_UNKNOWN, dgettext(TEXT_DOMAIN,
		    "failed to find config for pool '%s'"), targetdup);
		free(targetdup);
		return (ENOENT);
	}

	nvpair_t *elem = NULL;
	while ((elem = nvlist_next_nvpair(pools, elem)) != NULL) {
		VERIFY0(nvpair_value_nvlist(elem, &config));
		if (pool_match(config, targetdup)) {
			count++;
			if (match != NULL) {
				/* multiple matches found */
				continue;
			} else {
				match = fnvlist_dup(config);
			}
		}
	}
	fnvlist_free(pools);

	if (count == 0) {
		zutil_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "no matching pools"));
		(void) zutil_error_fmt(hdl, LPC_UNKNOWN, dgettext(TEXT_DOMAIN,
		    "failed to find config for pool '%s'"), targetdup);
		free(targetdup);
		return (ENOENT);
	}

	if (count > 1) {
		zutil_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "more than one matching pool"));
		(void) zutil_error_fmt(hdl, LPC_UNKNOWN, dgettext(TEXT_DOMAIN,
		    "failed to find config for pool '%s'"), targetdup);
		free(targetdup);
		fnvlist_free(match);
		return (EINVAL);
	}

	*configp = match;
	free(targetdup);

	return (0);
}

/* Return if a vdev is a leaf vdev.  Note: draid spares are leaf vdevs. */
static boolean_t
vdev_is_leaf(nvlist_t *nv)
{
	uint_t children = 0;
	nvlist_t **child;

	(void) nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children);

	return (children == 0);
}

/* Return if a vdev is a leaf vdev and a real device (disk or file) */
static boolean_t
vdev_is_real_leaf(nvlist_t *nv)
{
	const char *type = NULL;
	if (!vdev_is_leaf(nv))
		return (B_FALSE);

	(void) nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type);
	if ((strcmp(type, VDEV_TYPE_DISK) == 0) ||
	    (strcmp(type, VDEV_TYPE_FILE) == 0)) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * This function is called by our FOR_EACH_VDEV() macros.
 *
 * state:   State machine status (stored inside of a (nvlist_t *))
 * nv:	     The current vdev nvlist_t we are iterating over.
 * last_nv: The previous vdev nvlist_t we returned to the user in
 *          the last iteration of FOR_EACH_VDEV().  We use it
 *          to find the next vdev nvlist_t we should return.
 * real_leaves_only: Only return leaf vdevs.
 *
 * Returns 1 if we found the next vdev nvlist_t for this iteration.  0 if
 * we're still searching for it.
 */
static int
__for_each_vdev_macro_helper_func(void *state, nvlist_t *nv, void *last_nv,
    boolean_t real_leaves_only)
{
	enum {FIRST_NV = 0, NEXT_IS_MATCH = 1, STOP_LOOKING = 2};

	/* The very first entry in the NV list is a special case */
	if (*((nvlist_t **)state) == (nvlist_t *)FIRST_NV) {
		if (real_leaves_only && !vdev_is_real_leaf(nv))
			return (0);

		*((nvlist_t **)last_nv) = nv;
		*((nvlist_t **)state) = (nvlist_t *)STOP_LOOKING;
		return (1);
	}

	/*
	 * We came across our last_nv, meaning the next one is the one we
	 * want
	 */
	if (nv == *((nvlist_t **)last_nv)) {
		/* Next iteration of this function will return the nvlist_t */
		*((nvlist_t **)state) = (nvlist_t *)NEXT_IS_MATCH;
		return (0);
	}

	/*
	 * We marked NEXT_IS_MATCH on the previous iteration, so this is the one
	 * we want.
	 */
	if (*(nvlist_t **)state == (nvlist_t *)NEXT_IS_MATCH) {
		if (real_leaves_only && !vdev_is_real_leaf(nv))
			return (0);

		*((nvlist_t **)last_nv) = nv;
		*((nvlist_t **)state) = (nvlist_t *)STOP_LOOKING;
		return (1);
	}

	return (0);
}

int
for_each_vdev_macro_helper_func(void *state, nvlist_t *nv, void *last_nv)
{
	return (__for_each_vdev_macro_helper_func(state, nv, last_nv, B_FALSE));
}

int
for_each_real_leaf_vdev_macro_helper_func(void *state, nvlist_t *nv,
    void *last_nv)
{
	return (__for_each_vdev_macro_helper_func(state, nv, last_nv, B_TRUE));
}

/*
 * Internal function for iterating over the vdevs.
 *
 * For each vdev, func() will be called and will be passed 'zhp' (which is
 * typically the zpool_handle_t cast as a void pointer), the vdev's nvlist, and
 * a user-defined data pointer).
 *
 * The return values from all the func() calls will be OR'd together and
 * returned.
 */
int
for_each_vdev_cb(void *zhp, nvlist_t *nv, pool_vdev_iter_f func,
    void *data)
{
	nvlist_t **child;
	uint_t c, children;
	int ret = 0;
	int i;
	const char *type;

	const char *list[] = {
	    ZPOOL_CONFIG_SPARES,
	    ZPOOL_CONFIG_L2CACHE,
	    ZPOOL_CONFIG_CHILDREN
	};

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) != 0)
		return (ret);

	/* Don't run our function on indirect vdevs */
	if (strcmp(type, VDEV_TYPE_INDIRECT) != 0) {
		ret |= func(zhp, nv, data);
	}

	for (i = 0; i < ARRAY_SIZE(list); i++) {
		if (nvlist_lookup_nvlist_array(nv, list[i], &child,
		    &children) == 0) {
			for (c = 0; c < children; c++) {
				uint64_t ishole = 0;

				(void) nvlist_lookup_uint64(child[c],
				    ZPOOL_CONFIG_IS_HOLE, &ishole);

				if (ishole)
					continue;

				ret |= for_each_vdev_cb(zhp, child[c],
				    func, data);
			}
		}
	}

	return (ret);
}

/*
 * Given an ZPOOL_CONFIG_VDEV_TREE nvpair, iterate over all the vdevs, calling
 * func() for each one.  func() is passed the vdev's nvlist and an optional
 * user-defined 'data' pointer.
 */
int
for_each_vdev_in_nvlist(nvlist_t *nvroot, pool_vdev_iter_f func, void *data)
{
	return (for_each_vdev_cb(NULL, nvroot, func, data));
}
