# Fresh handler three-boot persistence

Run `public-service-run.3eBZivSx` completed with exit 0 using the same fresh
handler and wrapper as `../fresh-handler-native/`. Set the additional options:

```sh
AROS_TEST_PERSISTENT=1
AROS_TEST_COLD_RELEASE=1
AROS_TEST_STARTUP_IMPORT=1
AROS_TEST_STARTUP_ARCHIVE=1
AROS_TEST_FAT_HANDLER=/mnt/short-term/openzfs-bootenv-candidate-20261025/build-audit/public-service-run.uL30YO9M/fat-handler
AROS_TEST_HOST_ZDB=/home/terminills/Documents/GitHub/openzfs-aros/build-control/zdb
```

Writer, fresh-boot healthy/degraded import, exact DOS readback, unchanged reader
members and writable cold release passed. Persisted deletion was verified by
independent zdb. All three ISO runtime identities and handler input identities
are retained alongside logs. Eight virtual CPUs; selected patched platform
libraries. Own VM stopped and slot released; foreign VM untouched. This closes
the fresh-handler cold persistence check, not physical storage qualification
or the complete GUI/discovery package matrix for this new handler binary.
