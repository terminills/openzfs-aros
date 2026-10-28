# Full retained-report export

Manager now offers Save full report for retained local command output, rather
than saving only the displayed page. A joined background worker duplicates the
open source descriptor, reads from offset zero without moving the pager cursor,
and creates a new destination exclusively. Existing files are never truncated.
Partial output remains on I/O failure. This is not a power-loss durability claim.

Host tests cover exact binary bytes, descriptor lifetime, unchanged cursor,
overwrite and symlink refusal, EINTR, short I/O, read errors, ENOSPC, zero writes
and close errors. Native fixture command-report-export.Cv7LA937 uses the real
capture path and verifies three 131085-byte exports after closing the pager,
then checks refusal to overwrite each output.

Fresh MetaMake build mmake-cli.ytPXhTxu includes the production changes.
Isolated hosted run oYrY3nYc (inputs eqyk2kZs) passed all three native export
cycles, writer and cold runtime checks, persistent mirror cold DOS reads,
independent host zdb validation and unchanged pool-member hashes.

GUI run 6LoVBxaL (inputs jykLRbvW) saved two complete 269552-byte upstream
events reports, including an export after advancing the pager. Both have SHA256
74b242307ce959dfccf132d173e9d16b57b9c14561da9ff28a33c4cec83c71cd.
Screenshots preserve the save result and next page. This run reached its GUI
deadline before the final snapshot confirmation: it is NOT a complete GUI/cold
pass. Screenshot 06 does NOT prove GUI overwrite refusal; the filename ended
up in the options field. Native fixture evidence proves the worker refusal,
not that uncompleted UI interaction.

Attempt TMuftqTz passed native exports but the harness incorrectly expected
cycle numbers 1/2/3 instead of 0/1/2 and exited before cold verification.
The corrected harness requires all three actual cycles; oYrY3nYc is its rerun.

Runtime inputs: stdc px9y2jvo, stdcio uo0iplm_, POSIX j099bkwp and DOS
4CUMjP0r. These are isolated hosted results, not QEMU acceptance, hardware
durability, or full-port completion. Backward report navigation remains open.
No DUT or user hosted session was used. Logs and screenshots are retained;
duplicated runtime trees and pool disks are not committed.
