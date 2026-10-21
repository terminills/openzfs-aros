/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include "../engine/layout-check.h"
int oz_zpl_layout_test(void)
{
    size_t engine[OZ_LAYOUT_COUNT];
    const size_t native[OZ_LAYOUT_COUNT] = OZ_LAYOUT_VALUES;
    oz_engine_layout(engine);
    for (unsigned i = 0; i < OZ_LAYOUT_COUNT; i++)
        if (engine[i] != native[i]) return (i + 1);
    return (0);
}
