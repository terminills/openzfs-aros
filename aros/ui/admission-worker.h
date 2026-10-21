/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_ADMISSION_WORKER_H
#define OZ_ADMISSION_WORKER_H
#include "admission-api.h"
#include "partition-api.h"
#include "gpt-api.h"
#include "../device/mapping.h"
struct oz_mapping_catalog { unsigned count; struct oz_device_mapping units[32]; };
struct oz_mapping_worker;
/* Direct resident-device query; no service launch, mapping changes or retries.
 * Same poll/join/free contract as the other workers. */
int oz_mapping_worker_start(struct oz_mapping_worker **);
int oz_mapping_worker_remap_start(unsigned,const char *,int,struct oz_mapping_worker **);
int oz_mapping_worker_eject_start(unsigned,struct oz_mapping_worker **);
/* After successful poll/join: distinguishes committed remap/eject from errors
 * in later refresh or session cleanup. Read only after joining. */
int oz_mapping_worker_remapped(struct oz_mapping_worker *);
int oz_mapping_worker_poll(struct oz_mapping_worker *,struct oz_mapping_catalog *,int *);
int oz_mapping_worker_free(struct oz_mapping_worker *);
struct oz_gpt_worker;
/* Same ownership/drain contract as the admission worker below. No retries. */
int oz_gpt_worker_start(const char *,const struct oz_gpt_query *,struct oz_gpt_worker **);
int oz_gpt_worker_poll(struct oz_gpt_worker *,struct oz_gpt_query *,int *);
int oz_gpt_worker_free(struct oz_gpt_worker *);
struct oz_partition_worker;
struct oz_inventory_worker;
/* Read-only authorized inventory; poll/join before free or process exit. */
int oz_inventory_worker_start(const char *,struct oz_inventory_worker **);
int oz_inventory_worker_poll(struct oz_inventory_worker *,struct oz_storage_inventory *,int *);
int oz_inventory_worker_free(struct oz_inventory_worker *);
int oz_partition_worker_start(const char *,const char *,int32_t,struct oz_partition_worker **);
int oz_partition_worker_poll(struct oz_partition_worker *,struct oz_partition_catalog *,int *);
int oz_partition_worker_free(struct oz_partition_worker *);
/* Available after successful poll/join, even when no partition table exists. */
int oz_partition_worker_geometry(struct oz_partition_worker *,struct oz_device_geometry *);
struct oz_admission_worker;
/* All calls belong to one UI task. Worker never calls MUI. start copies
 * both inputs. No cancellation: poll until completion before closing the
 * process. EAGAIN means storage and worker MUST remain alive. */
int oz_worker_start(const char *, const struct oz_message *,
    struct oz_admission_worker **);
int oz_worker_poll(struct oz_admission_worker *, struct oz_message *,
    struct oz_ticket *, int *operation_error, int *release_error);
/* Only succeeds after poll joined the worker. */
int oz_worker_free(struct oz_admission_worker *);
#endif
