# Fresh handler native RAM verification

`public-service-run.MIfTQgza` passed with fresh handler
`handler-build.4DtJL9h3/OpenZFS-Handler`, hash
`4b249520bc6809f502c26600a4fe3983dfd9b52161223a4ab4366a5cbf178dfa`.
The retained wrapper now honors an explicit handler selection. Invocation:

```sh
AROS_TEST_OWNED_QEMU_SLOT='qemu codex slot 2' \
AROS_TEST_HANDLER_BINARY=/run/media/terminills/Amiwest/openzfs-source-replay-20261028/build/handler-build.4DtJL9h3/OpenZFS-Handler \
bash /run/media/terminills/Amiwest/openzfs-source-replay-20261028/run-current.sh
```

Harness exit 0, persistent=0, terminal storage PASS. Serial evidence includes
authenticated mirror DOS read/write, readonly reimport, notifications and
service lifecycle. Eight virtual CPUs; retained source-built platform libraries
and current shared engine/CLI. This is a fresh native QEMU RAM test, not cold
persistent or physical-device qualification. The released package is unchanged.
Own VM stopped and relay slot released after the test; foreign VM untouched.
