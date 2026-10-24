# Native encrypted root wrong-key recovery

Eight-CPU isolated QEMU run `public-service-run.UjSGl0eq` passed the full
upstream storage suite with the production `shared-engine.TEtOTOsm` engine.
The native encrypted pool root was exported and imported read-only, then
exported and imported writable. In each case the test:

1. Confirmed that the key was unavailable.
2. Attempted to load a different 32-byte raw key and required failure.
3. Rechecked that the key remained unavailable.
4. Required a DOS handler mount to fail with EACCES.
5. Loaded the correct key and read the exact original root payload.

The existing six root-property checks, read-only write refusal and neighboring
pool coexistence checks also passed. Both temporary key files were removed.
The runner requires the explicit wrong-key marker and terminal storage PASS.

This extends native-partition same-boot export/import coverage. It does not
claim a fresh guest boot, hardware persistence, or corrupted-ciphertext
authentication coverage. Only the acceptance client and runner changed;
the production archive and engine were not rebuilt for this test.
