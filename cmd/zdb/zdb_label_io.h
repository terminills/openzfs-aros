/* SPDX-License-Identifier: CDDL-1.0 */
/* Label/L2ARC reader only. Native handles never become process POSIX fds. */
#ifndef ZDB_LABEL_IO_H
#define ZDB_LABEL_IO_H
#ifdef __AROS__
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>
#include "../../aros/cli/device-path.h"
#include "../../aros/engine/device-io.h"
#include "../../aros/engine/partition-io.h"
#endif

typedef struct zdb_label_reader {
	int fd;
#ifdef __AROS__
	struct oz_device *device;
	struct Library *security;
#endif
} zdb_label_reader_t;

static void
zdb_label_close(zdb_label_reader_t *reader)
{
#ifdef __AROS__
	oz_device_close(reader->device);
	reader->device = NULL;
	if (reader->security != NULL)
		CloseLibrary(reader->security);
	reader->security = NULL;
#endif
	if (reader->fd >= 0)
		(void) close(reader->fd);
	reader->fd = -1;
}

#ifdef __AROS__
static int
zdb_label_authorized(zdb_label_reader_t *reader)
{
	struct Library *secBase = reader->security;
	/* Same native root policy as authenticated admission device inspection.
	 * Missing/unconfigured security never synthesizes privileged identity. */
	return (secBase != NULL && secIsConfigured() &&
	    (secGetTaskOwner(FindTask(NULL)) >> 16) == secROOT_UID);
}
#endif

static int
zdb_label_open(zdb_label_reader_t *reader, const char *path,
    struct stat64 *st)
{
	memset(reader, 0, sizeof (*reader));
	reader->fd = -1;
#ifdef __AROS__
	if (strncmp(path, "/AROS/", 6) == 0) {
		struct oz_device_path parsed;
		int error = oz_device_path_parse(path, &parsed);
		if (!error) {
			reader->security = OpenLibrary((CONST_STRPTR)SECURITYNAME,
			    SECURITYVERSION);
			if (!zdb_label_authorized(reader))
				error = EACCES;
		}
		if (!error) {
			if (parsed.partition)
				error = oz_device_open_partition(parsed.device,
				    parsed.unit, parsed.position, 0, &reader->device);
			else
				error = oz_device_open(parsed.device, parsed.unit,
				    0, &reader->device);
		}
		if (!error && oz_device_size(reader->device) > INT64_MAX)
			error = EOVERFLOW;
		if (error) {
			zdb_label_close(reader);
			errno = error;
			return (-1);
		}
		memset(st, 0, sizeof (*st));
		st->st_mode = S_IFBLK;
		st->st_size = oz_device_size(reader->device);
		return (0);
	}
#endif
	reader->fd = open64(path, O_RDONLY);
	if (reader->fd < 0)
		return (-1);
	if (fstat64_blk(reader->fd, st) != 0) {
		int error = errno;
		zdb_label_close(reader);
		errno = error;
		return (-1);
	}
	return (0);
}

static ssize_t
zdb_label_read(zdb_label_reader_t *reader, void *buffer, size_t count,
    uint64_t offset)
{
#ifdef __AROS__
	if (reader->device != NULL) {
		int error = zdb_label_authorized(reader) ? 0 : EACCES;
		uint64_t size = oz_device_size(reader->device);
		if (!error && (count > SSIZE_MAX || offset > size ||
		    count > size - offset))
			error = EINVAL;
		if (!error)
			error = oz_device_transfer(reader->device, buffer, count,
			    offset, 0);
		if (error) {
			errno = error;
			return (-1);
		}
		return ((ssize_t)count);
	}
#endif
	return (pread64(reader->fd, buffer, count, offset));
}

static int
zdb_label_invalidate(zdb_label_reader_t *reader)
{
#ifdef __AROS__
	/* Exec read-only inspection must never issue a flush/write command. It
	 * does not promise a snapshot against external raw writers. */
	if (reader->device != NULL)
		return (0);
	/* The AROS POSIX stat adapter accepts regular files only, so there is
	 * no POSIX block-device descriptor to invalidate on this platform. */
	errno = ENOTSUP;
	return (-1);
#else
	return (zfs_dev_flush(reader->fd));
#endif
}
#endif
