/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_STREAM_OUTPUT_H
#define OZ_STREAM_OUTPUT_H
#include <stddef.h>
struct oz_packet_security;
struct oz_stream_output;
/* Private staging directory on the destination filesystem. Original caller
 * context is borrowed until finish. Publication uses native no-replace rename.
 * Current service task must be configured native root; this is not a helper
 * for unprivileged callers to impersonate a management service. */
int oz_stream_output_begin(const struct oz_packet_security *,const char *,
    struct oz_stream_output **);
int oz_stream_output_write(void *,const void *,size_t);
/* Always consumes output. commit=0 removes only this owned staging output. */
int oz_stream_output_finish(struct oz_stream_output *,int commit);
#endif
