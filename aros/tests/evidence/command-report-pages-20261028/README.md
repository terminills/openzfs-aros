# Native retained command-report paging

The local command worker now returns a typed `command-report` row when its
output exceeds the response window. The Manager consumes this metadata only
from its joined local worker, never from service-supplied rows or parsed text.
It opens the retained report read-only; Next page reads bounded byte pages
without executing the command again. A new displayed response or Manager exit
closes the file. The temporary original is retained for manual copying.

Pages may split long lines or multibyte sequences; they are not line/Unicode
pagination. Save response saves the current page, not the full retained file.
Full-file export and backward navigation remain follow-up work.

## Host tests

`bash aros/ui/test-report-save.sh` includes the report-page tests: 257 buffer
capacities verify exact byte reconstruction, including exact page boundaries,
single-byte pages and long lines. Empty files, NUL bytes in normal reads and
lookahead, missing files and closed descriptors are checked.

## Real native capture fixture

`build-audit/command-report.mA3ENCAH` compiles the real internal capture path
with a test-only child executable. The child writes 131085 bytes including a
128 KiB line and exits 23. Three captures each preserve the error status and
all report bytes; the production pager reads 33 pages and checks every byte.
The fixture removes only its own reports. It is not shipped as an application.

Hosted run `9pZQAdol`, inputs `imegQLTq`, passed this fixture plus the full
writer/cold-reader storage and runtime checks. This is an actual AROS executable
test in isolated hosted AROS, not a Linux-only parser test.

## Manager interaction with actual upstream output

Fresh MetaMake build `mmake-cli.6ryiAJlV` was used in hosted run `b6fjHTHg`,
inputs `olqO1C29`. Selecting Pools/events with `verbose=on` ran the real
`zpool events -v --` worker. It exited zero and retained its oversized report.
Screenshots show the completed response and distinct pages 1 and 2. The activity
log records a single events invocation, followed by authenticated snapshot
creation. Closing Manager allowed the fixture to complete; a new cold process
found the UI snapshot, verified read-only files and unchanged members, and
passed independent host zdb checks. The native capture fixture also passed
again in this run. Full runtime prerequisite checks passed on both boots.

Runtime inputs were source-built stdc px9y2jvo, stdcio uo0iplm_, POSIX j099bkwp
and DOS 4CUMjP0r. These are private hosted results, not native QEMU acceptance
or physical-device durability. The DUT and user hosted session were untouched.
