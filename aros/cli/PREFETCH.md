# Native upstream pool prefetch

Manager operation 27 runs the installed `C:zpool prefetch` through the
asynchronous exact-argument worker. It inherits native identity, does not
construct a shell command, and retains the standard worker drain/join lifetime.
Confirmation is required. Empty options select upstream's DDT-then-BRT default;
`type=ddt` and `type=brt` select one table. Other options, arguments and flags
are rejected before spawning. The native Manager is rebuilt with this route.

The service admits ZFS_IOC_POOL_PREFETCH with a packed source nvlist, rejecting
unrelated inline fields and history. The real upstream dispatcher validates
the prefetch_type schema and value, enforces configuration privilege and pool
suspension policy, and executes ddt_prefetch_all or brt_prefetch_all. AROS does
not recreate those engines or substitute success for an unsupported request.

Prefetch fills cache and is not a filesystem repair or a guarantee of improved
performance. The initial test uses the existing mirror fixture; it does not
claim a populated-table workload, cache-hit measurement, cancellation, or
visual Manager verification. Test cases cover DDT, BRT, default both, invalid
options, missing confirmation, and a MuFS non-root request.

Build: source-engine.P2gN7Bsv/prefetch-service, using the previous fully rebuilt
engine object set plus freshly compiled adapters/dispatcher. CLI remains
cli-link.EFsIw7on. Build log: engine-prefetch-build-1.log. Private OS overlays
remain required; this is not stock-nightly or production qualification.

Run jw9zhgoE (`manager-prefetch-native-1.log`) passes all six cases and the
full native regression with exit 0 and unchanged original backing-image
hashes. The non-root response specifically reports permission denied from
upstream prefetch, not a transport failure. Evidence is registered under
the development test archive.
