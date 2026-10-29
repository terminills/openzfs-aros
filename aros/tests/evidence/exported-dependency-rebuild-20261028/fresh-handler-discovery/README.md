# Fresh handler native encrypted discovery

Run `public-service-run.vFUy9dxL` completed with exit 0, eight virtual CPUs and
the same fresh handler selection as `../fresh-handler-native/`, adding
`AROS_TEST_REQUIRE_PARTITIONS=1 AROS_TEST_NATIVE_DISCOVERY=1`.

Native discovery handler writes, readonly import/read and write denial,
writable reimport/read, conflicting-label protection/restoration, encrypted-root
locked import, denied mount, wrong-key refusal and correct-key recovery passed.
Both partition.raw and blank.raw post-run checks passed unchanged. Retained
logs, selected handler identity and actual ISO runtime hashes accompany this
record. Own VM stopped and relay slot released; foreign VM untouched.

This completes this candidate's discovery check, not its actual GUI acceptance
or hardware qualification. Existing packaged binaries remain unchanged.
