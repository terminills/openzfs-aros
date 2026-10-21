/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_SUPPORT_API_H
#define OZ_SUPPORT_API_H
#include <exec/types.h>
#include <stddef.h>
#define OZ_SUPPORT_ACTION 0x4f5a4d3cL
#define OZ_SUPPORT_VERSION 1
#define OZ_HOSTID_ACTION 0x4f5a4d3eL
struct oz_hostid_query { ULONG version, hostid; };
int oz_hostid_query(const char *, ULONG *);
#define OZ_ENGINE_VERSION_ACTION 0x4f5a4d3fL
struct oz_engine_version_query { ULONG version, reserved; char engine[128]; };
int oz_engine_version_query(const char *, char *, size_t);
/* Fixed-size pointer-free query. Supported must be zero on entry. */
struct oz_support_query {
    ULONG version, supported;
    char scope[32], name[256];
};
int oz_support_query(const char *, const char *, const char *, int *);
#endif
