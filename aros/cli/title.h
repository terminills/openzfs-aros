/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_CLI_TITLE_H
#define OZ_CLI_TITLE_H
void zfs_setproctitle_init(int, char **, char **);
void zfs_setproctitle(const char *, ...);
/* Owner must join progress workers before restoring/exiting. */
int oz_cli_title_restore(void);
#endif
