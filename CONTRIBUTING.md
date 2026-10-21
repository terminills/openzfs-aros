# Contributing

Issues and pull requests are welcome. Describe the AROS version, architecture,
hosted/QEMU/hardware environment, build inputs, and exact reproduction steps.
Include source revisions and relevant logs; remove credentials and private data.

Keep changes focused and preserve upstream licensing and attribution. Separate
platform/runtime changes from filesystem changes where possible. Add regression
coverage and distinguish host-only checks from actual AROS runtime tests.

Use only disposable pools for destructive tests. Do not weaken flush, identity,
provenance, or packaging checks to make a test pass. Document known limitations
honestly and retain failed-test evidence as well as successful results.

Start with BUILDING.md and PORT-STATUS.md. Historical test notes are references,
not substitutes for rerunning tests against the binaries in your environment.
