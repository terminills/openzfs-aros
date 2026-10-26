# Shared native clear admission

Source review found that Manager operation 17 performed retained-pool identity
checks and explicit invalidated-device readmission, while the upstream CLEAR
ioctl bypassed that native preparation. The common helper now serves both
paths, after configuration authorization and the MMP remote-activity check,
and before vdev_clear/zio_resume. A suspended pool's requested GUID is checked
before any binding refresh; the existing readmission implementation checks
member labels and native extent identity. Failure does not resume I/O.

The actual engine make fragment rebuilt all production units in
`mmake-engine.Pe5fadjd`. Production verification and shared library packaging
passed (`shared-engine.cmPmmLLk`). No diagnostic fault wrapper was linked.

Fresh eight-CPU isolated QEMU runs passed:

- `public-service-run.WJRz4tbY`: file-backed public storage suite.
- `public-service-run.C9776Whd`: native partition/discovery plus public storage.

Both exercise the real CLEAR wire request with an invalid GUID (ENODEV) and
whole-pool clear (success), followed by the remaining public acceptance suite,
handler data operations and readonly reimport. The runner requires this marker.
Selected CLI build is `mmake-cli.f2fvfhdP`; hashes and serial evidence are kept.
Successful run media were hashed/pruned, not retained as additional VMs.

These are healthy-pool integration checks. They do not exercise the suspended
readmission branch, prove CLI unplug/reinsert recovery, or implement rewind.
Those remain explicit qualification gaps. Older Manager fault tests are not
evidence for this new CLI entry point. No DUT or user-hosted session was used.
