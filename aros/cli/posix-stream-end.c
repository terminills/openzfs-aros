/* SPDX-License-Identifier: CDDL-1.0 */
/* Private runtime overlay: flush streams before priority-2 descriptor teardown.
 * Inherited DOS handles are deliberately not closed by posixc, so relying on
 * Close() to flush them loses buffered output on normal return or exit().
 */
#include "__posixc_intbase.h"
#include "__stdio.h"
#include <stdio.h>
#include <aros/symbolsets.h>

static void oz_posix_stream_end(struct PosixCIntBase *base)
{
    FILENODE *node;
    ForeachNode(&base->stdio_files,node) {
        if(node->File.flags & __POSIXC_STDIO_WRITE)
            (void)fflush(&node->File);
    }
    /* Continue after one failed stream so stderr/other streams still flush.
     * Library close cannot return an application I/O status; callers requiring
     * error reporting must explicitly fflush/fsync before exit as usual.
     * Do not close handles or free wrappers owned by the existing teardown.
     */
}
ADD2CLOSELIB(oz_posix_stream_end, 3);
