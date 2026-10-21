# Checkpoint space-map histogram refcount verification

The existing zdb get_checkpoint_refcount counted every checkpoint space map
toward SPA_FEATURE_SPACEMAP_HISTOGRAM. Unlike the corresponding metaslab,
DTL and obsolete-map counters, it never inspected the map's bonus format.
Upstream space_map_alloc uses SPACE_MAP_SIZE_V0 (24 bytes) when the histogram
feature is disabled and does not increment that feature's refcount.
zpool_checkpoint has no dependency on spacemap_histogram.

The correction reads the checkpoint map object from the top-vdev ZAP and
checks dmu_object_info.doi_bonus_size against sizeof(space_map_phys_t).
Only histogram-format checkpoint maps count. Missing/corrupt object lookup
remains a verification failure. This does not change allocation or disk data.

## Retained evidence under build-aros

- file-backing-run.lgWMfI3f: real AROS mirror with only zpool_checkpoint
  enabled. Native create/conflict checks and separate cold read-only import
  pass, including payload, directory enumeration and unchanged member bytes.
  Original linux-zdb.log reports no block leaks but exits 2 because expected
  histogram refcount=0 and unconditional checkpoint count=1.
- legacy-spacemap-inspect.log in that directory resolves the top-vdev
  checkpoint map to object 79 and shows its 24-byte bonus header. Feature
  write ZAP contains the checkpoint reference, no histogram reference.
- linux-zdb-fixed.log verifies the SAME saved member files with the fixed
  verifier: exit 0, no leaks. SHA256 before/after verification matched and
  both read-member files still match their corresponding write-member files.
- file-backing-run.Sb0S26eX: new native RAIDZ1 fixture explicitly enables
  spacemap_histogram before checkpoint creation. Both guest boots pass and
  the fixed verifier exits 0 with no leaks. This covers the positive counted
  format as well as the legacy format. The runner checks read-only bytes.

Build log: checkpoint-zdb-build.log. This is evidence of a format-accounting
bug in the independent verifier, not justification to ignore its errors.
The original failed output is retained. No production pool was accessed.
