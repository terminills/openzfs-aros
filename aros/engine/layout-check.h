/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/dbuf.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dnode.h>
#define OZ_LAYOUT_VALUES {sizeof(avl_tree_t), sizeof(zfs_refcount_t), \
    sizeof(dmu_buf_impl_t), offsetof(dmu_buf_impl_t, db_dirty_records), \
    sizeof(dnode_t), sizeof(objset_t), sizeof(dmu_tx_t), sizeof(zfs_uio_t)}
#define OZ_LAYOUT_COUNT 8
void oz_engine_layout(size_t *);
