# Native pre-storage ABI gate: negative fixture

Run `public-service-run.OXpUWkXI` used the production RuntimeCheck entry point
compiled through `aros/cli/test-runtime-errno-fault.c`. Only the fixture's
close() call was replaced with a function that returns -1 without setting
errno. This binary must never be packaged for users.

The guest emitted `RESULT pre-storage errno FAIL` and the runner exited 1.
No service lifecycle or pool/storage test markers appeared. The startup
sequence places the failing command before the first service launch; its
exit code 20 meets FailAt 20 and aborts that script. The captured result is
an expected rejection, not a storage PASS or a real mismatched-SDK test.

The corresponding positive run is documented in ../errno-prestorage/.
Together they qualify both normal acceptance and deliberate rejection on
the established alpha18c5 baseline. New-runtime ABI qualification remains
pending a complete, stable SDK and a fresh matched payload rebuild.
