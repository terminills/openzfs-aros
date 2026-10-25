# Rename source UTF-8 admission

`d05ed5d` validates both rename components before normalized comparison on
UTF8-only datasets. Previously only the destination was checked explicitly;
malformed source input could reach the comparison assertion. The regression
requires EILSEQ for the invalid source, then performs a valid rename and
reopens the dataset to verify identity.

Two-CPU isolated `default-quota-run.9vljZw99` passes the complete baseline
quota suite. Coverage includes all nine UTF8/normalization/case configurations,
DOS alias/hardlink enumeration, mount lifecycle and user/group/project quota
checks. This build does not enable the optional full-dispatcher/zvol cases.

A second two-CPU run, `default-quota-run.7xDepkcb`, rebuilt with the full native
dispatcher and required the optional zvol results. It also passes, including
upstream creation properties, zvol write/reopen/read-only/resize/export-busy,
zvol rename/snapshot/recreate, and 80 dispatcher runtime cycles. See
`full-dispatcher/` for this distinct binary's hashes and results.

The two earlier attempts passed the Unicode checks but failed later with
EACCES: disposable dataset creation lacked explicit credential scopes.
`8ff6a77` and `64b01fe` fix only those test fixtures. The engine's refusal of
missing identity remains intact, and the test now checks that refusal before
entering its narrowly bounded fixture-creation scope. Both failed logs remain
here rather than being represented as passing full-suite results.

Input hashes, completion logs and the successful runner log are retained.
Complete runs reside under the external-backed repository `build-aros`
directory. Selected patched runtime libraries and incremental upstream objects
were used; this is neither physical-device nor stock-nightly qualification.
