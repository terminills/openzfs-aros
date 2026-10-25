# Old root build-tree cleanup

Removed 40 generated test ISOs (25.17 GiB allocated) and three obsolete
4 GiB POSIX offset-test output files from
`/home/terminills/Documents/GitHub/openzfs-aros/build-aros`.
The POSIX fixtures each had retained `RESULT raw POSIX failures=0` logs.
Their hosted installations were not removed.

Exact paths, sizes, allocated blocks and SHA256 hashes are retained here.
Open-file checks preceded hashing and removal. The source checkout was clean;
no visible holders or build/QEMU jobs were found for the old tree. No RAID
pool member, source file, executable archive, log or screenshot was removed.
Deleted generated files must be regenerated; hashes do not recover their bytes.

Both cleanup commands exited zero. Root free space increased from about
86 GiB before this pass to 124 GiB afterward; the old build tree decreased
from approximately 521 GiB to 484 GiB. Exact filesystem measurements are in
the df logs. This is a first bounded cleanup, not complete deduplication of
all historical test sets or build trees.
