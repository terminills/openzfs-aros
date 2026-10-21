/* Engine-private error values, kept distinct from AROS libc errno values.
 * These must be translated at the eventual DOS/admin boundary. */
#ifndef OPENZFS_AROS_ERRNO_H
#define OPENZFS_AROS_ERRNO_H
#include <errno.h>
_Static_assert(__POSIXC_ELAST < 4096, "OpenZFS private errno range overlaps libc");
#define ECKSUM 4096
#define EFRAGS 4097
#define ENOTACTIVE 4098
#define EREMOTEIO 4099
#define ESHUTDOWN 4100
#define ERESTART 4101
#define ECHRNG 4102
#endif
