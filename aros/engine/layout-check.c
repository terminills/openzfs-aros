/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include "layout-check.h"
void oz_engine_layout(size_t *out)
{
    const size_t actual[OZ_LAYOUT_COUNT] = OZ_LAYOUT_VALUES;
    memcpy(out, actual, sizeof (actual));
}
