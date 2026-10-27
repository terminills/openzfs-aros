/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_CLEAR_POLICY_H
#define OZ_CLEAR_POLICY_H
#include <string.h>
struct oz_clear_policy { int extreme, assessment; };
/* Nonempty recovery options only; ordinary clear remains a separate route. */
static int oz_parse_clear_policy(const char *text, struct oz_clear_policy *out)
{
    struct oz_clear_policy value = {0,0};
    unsigned seen = 0;
    if (!text || !*text || !out) return 0;
    while (*text) {
        const char *end = strchr(text, ';');
        size_t n = end ? (size_t)(end-text) : strlen(text);
        unsigned bit = 0;
        if (n==13 && !memcmp(text,"recovery=safe",13)) bit=1;
        else if (n==16 && !memcmp(text,"recovery=extreme",16)) { bit=1; value.extreme=1; }
        else if (n==13 && !memcmp(text,"assessment=on",13)) { bit=2; value.assessment=1; }
        else if (n==14 && !memcmp(text,"assessment=off",14)) bit=2;
        else return 0;
        if (seen & bit) return 0;
        seen |= bit;
        if (!end) break;
        text=end+1;
        if (!*text) return 0;
    }
    if (!(seen&1)) return 0;
    *out=value;
    return 1;
}
#endif
