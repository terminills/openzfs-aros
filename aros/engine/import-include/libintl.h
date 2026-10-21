/* SPDX-License-Identifier: CDDL-1.0 */
/* Import-library bootstrap without installed message catalogs. Only the
 * diagnostic translation hook is identity-mapped; no storage operation is
 * replaced. Keep this include directory local to the import build until
 * AROS locale/catalog integration supplies translations. */
#ifndef OZ_IMPORT_NO_NLS_H
#define OZ_IMPORT_NO_NLS_H
static inline char *dgettext(const char *domain,const char *message)
{
    (void)domain;
    return (char *)message;
}
#endif
