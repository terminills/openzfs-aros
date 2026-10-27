# Authenticated Manager API split

The same production engine now has separate authenticated operation-14 coverage
for special and dedup mirrors, rather than treating CLI coverage as Manager
coverage. The test obtains the selected leaves' GUIDs from live operation-4
pool-status rows, submits through the admission-ticket API, waits for the reply
and releases the ticket. Setup, sync, export and readonly import still use the
upstream CLI; the split itself does not.

Eight-CPU isolated QEMU runs:

| Class | Manager writer | Separate cold reader |
| --- | --- | --- |
| special | `FDiWlaDa` | `jpKOHbi2` |
| dedup | `VctWq8iV` | `QzyESh7n` |

All pass both-pool exact 4 KiB payload reads, readonly-write refusal, clean FAT,
independent block and full MOS checks, persisted allocation bias, nonzero class
allocation and unchanged cold member/writer-container hashes. Dedup also checks
the duplicate file and real shared blocks. Writer logs contain the distinct
`RESULT split topology authenticated Manager API PASS` marker. The historical
general `native CLI split topology` summary remains because the surrounding
fixture uses the CLI; it must not be used alone to identify the split entrypoint.
The new cold-source guard requires the Manager marker when Manager mode is
requested; the retained prior CLI writers do not satisfy that guard.

This is native Manager API/backend coverage, not a GUI click-through, new
production functionality, concurrent split stress or physical power-loss proof.
Source changes are confined to the acceptance fixture and its runner.
