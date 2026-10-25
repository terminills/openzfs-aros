# Pre-storage errno ABI gate

`AROS_TEST_REQUIRE_ERRNO=1` requires companion tools and a RuntimeCheck binary
with `--errno-serial`. The runner inserts that command before service startup
and requires its distinct PASS marker in captured kernel output. A failing
probe exits 20, stopping the startup script before service/pool operations.
The binary-string preflight only checks probe availability, not ABI correctness.

Six host preflight cases pass, including rejection of an old RuntimeCheck and
acceptance of the focused serial probe. Native eight-CPU QEMU run
`public-service-run.9KolgY2I` passed the pre-storage marker before the first
service lifecycle marker, then the full configured storage suite, exit zero.
The exact startup sequence, selected inputs, results and runner log are retained.
This remains alpha18c5 qualification, not new-stdlib-runtime qualification.

Initial run `public-service-run.ueXySEEm` used DOS SER: redirection; the early
marker was absent from the kernel capture even though later storage tests
passed. It was deliberately ended through its own QMP socket and exited 1.
It is not a passing pre-storage gate. The corrected diagnostic mode writes
one marker through AROS bug() directly; ordinary RuntimeCheck and
--errno-only do not write to the debug stream.
