/* SPDX-License-Identifier: CDDL-1.0 */
/* Explicit no-NLS CLI build: retain upstream English messages. This does not
 * implement catalog loading; enable a real gettext implementation for NLS. */
#ifndef OZ_CLI_NO_NLS_H
#define OZ_CLI_NO_NLS_H
#ifdef ENABLE_NLS
#error "The AROS CLI no-NLS header cannot provide message catalogs"
#endif
static inline char *gettext(const char *message)
{
    return (char *)message;
}
static inline char *dgettext(const char *domain, const char *message)
{
    (void)domain;
    return (char *)message;
}
static inline char *ngettext(const char *one, const char *many, unsigned long n)
{
    return (char *)(n == 1 ? one : many);
}
static inline char *textdomain(const char *domain)
{
    (void)domain;
    return (char *)"messages";
}
#endif
