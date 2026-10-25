# Manager frontend on pristine nightly SDK/runtime

Scope: native frontend startup, no-service feedback, shell shutdown and a
second Workbench launch. **Not engine, authorization-worker, pthread stress,
pool, hardware or full port qualification.**

Published 20261023 SMP ISO SHA-256:
`38b5689f4d5c23b0765a3fbc266c67928d841d81c754675a107dd8bc0b50d6e3`.
Manager built against that ISO's unmodified iso-Developer headers/libraries,
including its own libpthread.a and pthread.h. No private dependency archive,
runtime library or boot-module overlay was installed.

Build helper now supports explicit `AROS_PTHREAD_BACKEND=sdk` with AROS_SDK and
AROS_CC supplied. Private backend remains unchanged/default for existing
qualified recipes. SDK mode rejects simultaneous private pthread inputs rather
than silently mixing archives and headers. Target, ABI assembly, SDK hashes,
source/binary hashes and compiler provenance are retained. Build exited zero
with an empty diagnostic log; mixed-input negative check correctly rejected.

Successful run:
`/mnt/short-term/openzfs-official-sdk-audit.xi9RkF3r/publication/build-aros/manager-startup-run.zY6tCXB7`.
Build outputs: `/mnt/short-term/openzfs-env-matrix.cJJ5YNnS/OpenZFSManagerOfficial*`.
QEMU KVM, CPU host, two CPUs, 2048 MiB, private read-only CD, no writable disks,
no network, no DUT or user-hosted session.

Observed shell-manager.png: native Manager opens with Service unavailable;
response states that no service is registered, request was not delivered and
no storage was changed. After focusing the guest shell and sending Ctrl-C,
the Manager's supported signal shutdown returned to Startup-Sequence. That
script subsequently started Wanderer and used WBRun with a real tool icon.
Observed workbench-launch.png: independently launched Manager on Workbench
with the same correct no-service response. No restart occurred between launches.
The second instance was not interactively closed; the private VM was stopped
after capturing this evidence. Do not claim its clean shutdown was tested.

Initial run manager-startup-run.1cgeHWSB failed before boot because the absolute
HMP socket pathname exceeded 108 bytes. Failed-qemu.log retains the error.
Runner now starts QEMU from its own run directory and binds relative `monitor`;
clients should likewise connect after chdir to that directory when paths are
long. BIOS-only test repacking also clears inherited system-area metadata.

Another initial attempt stopped before allocation because the new private
checkout had no build-aros directory; creating that output directory satisfied
the existing free-space preflight. Neither failure was a guest/Manager defect.

Screenshots are from the isolated guest framebuffer, not a crop of any host
window. The test-only HMP mouse selection and Ctrl-C input touched only this
guest. Terminal run ISOs were pruned after open-file checks and saved hashes;
logs, screenshots and binaries remain available.
