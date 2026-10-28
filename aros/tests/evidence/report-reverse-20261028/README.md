# Retained-report backward navigation

Previous seeks within the already-open retained report. Fixed-capacity byte
pages use checked 64-bit offsets, reset lookahead after seeking, and never
rerun the producing command. The control is unavailable on page one, while
exporting, and for live service pages. New responses reset report navigation.

Host tests compare forward and backward output across 257 capacities, including
exact boundaries and invalid/overflow offsets. Native fixture SBINUqxp captures
131085 bytes through the real command worker, checks all 33 pages backward,
then exports the complete original despite the pager having returned to page
one. It checks no overwrite and closed-pager lifetime across three cycles.

Fresh MetaMake build hG2vWP1T and isolated hosted run hkxxb2ak (inputs 7uIuJGew)
pass native reverse/export checks, writer/cold runtime checks, persistent mirror
DOS reads, independent host zdb and unchanged cold-reader member hashes.

GUI run kS3cLLIo (inputs 4SjnTpy6) uses the same Manager. Screenshots capture
actual upstream verbose events: page one, page two, Previous returning to page
one, then Next returning to page two. The activity list shows one command
execution. Previous is disabled on page one and enabled on page two. The row
fits the 800x600 desktop. Snapshot confirmation and post-UI verification are
recorded separately in this run's logs. The run completed with exit zero:
snapshot creation returned errno zero, both runtime checks passed, and the cold
reader plus independent zdb and unchanged-member checks passed.

This is isolated hosted evidence, not physical storage or native QEMU acceptance.
No DUT or user hosted session was used. Byte pages can split lines or multibyte
characters; this change does not implement text-aware pagination.
