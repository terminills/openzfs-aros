/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_CREATE_INTENT_H
#define OZ_CREATE_INTENT_H
/* Transient client intent, removed by service admission before spa_create.
 * This never substitutes for caller authorization or member reservations. */
#define OZ_CREATE_FORCE "org.aros:create-force"
#define OZ_ADD_FORCE "org.aros:add-force"
#define OZ_ATTACH_FORCE "org.aros:attach-force"
#endif
