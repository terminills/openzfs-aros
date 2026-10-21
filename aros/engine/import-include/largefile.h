/* Native AROS off_t is already 64 bit. Labels use private descriptors, so
 * the CRT's seek/read/restore pread implementation has no shared offset here. */
#include <unistd.h>
_Static_assert(sizeof(off_t)==8,"Import requires 64-bit native offsets");
/* The target CRT exports pread although this SDK omits its prototype. */
extern ssize_t pread(int,void *,size_t,off_t);
#define pread64 pread
