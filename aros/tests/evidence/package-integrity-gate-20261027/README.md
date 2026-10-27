# Contradictory evidence must not qualify a binary bundle

The old split destination in `../split-persistent-20261027/old-hole` prints
`No leaks` for block accounting and then reports a space-map feature reference
count mismatch. Its full-object report also identifies leaked MOS objects.
The old packaging validator only searched for `No leaks`, so this is a real
counterexample to that acceptance rule, not just a synthetic malformed log.

The staging validator now rejects contradictory zdb diagnostics and requires
full MOS-object reachability reports alongside block checks for the principal
cold pool, the changed-member pool and optional writable-release phase.
Guest assertions, page faults and failed results cannot be masked by a later
PASS. Timestamp-prefixed diagnostics are recognized.

Thirteen host tests pass, including rejection of the actual failed split logs,
acceptance of the corrected cold reports, missing reports, malformed/contradictory
diagnostics and suffixed failed results. These tests verify the evidence gate;
they do not qualify a new bundle or repair previously failed storage.

The persistent QEMU runner now generates and checks those object reports before
its unchanged-member check. Fresh eight-CPU run `public-service-run.M625rCxw`
passes writer, cold healthy/degraded/restored imports and third-boot writable
deferred-snapshot release. All three block reports and all three full-object
reports pass the actual staging functions. The changed-pool and optional-release
composite gates also pass on this retained native evidence.

The first composite-gate replay rejected the current writer marker because the
stager still expected `exported-force-refused=3`. Source inspection confirms
that the current test refuses active members with force, refuses exported
members without force, and adds destroyed members without force. The gate now
requires the current exact marker including `add-without-force=1`; the stale
marker is explicitly rejected by a regression test. This changes an outdated
evidence expectation, not the admission policy or native test outcome.

No application/runtime binary changes or newly staged release bundle are
claimed. Full GUI/companion/encrypted-root/package checks and corresponding
source closure remain separate requirements. Selected native results, host
reports, hashes and gate logs are retained here; full run inputs remain in the
external build directory. The successful runner prunes its generated ISOs.
