/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_PROJECT_WORKER_H
#define OZ_PROJECT_WORKER_H
#include "api.h"
struct oz_project_worker;
/* Exact upstream command, inherited native identity, no shell. Callers must
 * drain/join the worker before destroying their process. No cancellation. */
int oz_project_execute(struct oz_message *);
int oz_diff_execute(struct oz_message *);
int oz_clone_execute(struct oz_message *);
int oz_project_worker_start(const struct oz_message *,struct oz_project_worker **);
/* Local read-only prerequisite report; no service wire operation or shell. */
int oz_runtime_worker_start(struct oz_project_worker **);
int oz_project_worker_poll(struct oz_project_worker *,struct oz_message *);
int oz_project_worker_free(struct oz_project_worker *);
#endif
