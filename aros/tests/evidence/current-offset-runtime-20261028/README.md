# Current runtime large-file offset discrimination

Fresh probes `offset-probes.iw1zsGfQ` were compiled from current sources with
the explicitly selected SDK/compiler. Build logs were empty. The POSIX probe
now closes and reopens its scratch file, checking all six patterned offsets
and three sampled holes again. The runner uses a new private hosted instance,
external artifact storage and records stdc/stdcio as well as DOS/POSIX/DOS64.

Control `hosted-offset.KRyuRTi2` used the old hosted template's DOS64 binary
e8cdf3f9996df492d3123aa73ad7184205da10d0dce74027d711e37d00729f79.
Raw past-EOF SEEK64 reports 219 but the wrapper reports 207; POSIX growth
fails at 4294966784 and 4294967296. The pre-sized DOS64 file and POSIX reads
of that existing file pass. The runner exits nonzero; this is retained failure
evidence, not successful current-runtime qualification.

Comparison `hosted-offset.t89C654n` changes only DOS64 to the retained corrected
binary 2c392b8ec4b0fd7c674ec897a329af68b9dca8ea925baaabb4f458f5002d9e58.
Both error paths report 219, all offset writes/reads pass, earlier data remains
distinct, sampled holes are zero, close/reopen passes, and inputs verify unchanged.
The runner exits zero. This closes the tested current-CRT combination, not
physical durability, native filesystem backing or whole-port qualification.

This dependency fix already existed: retained provenance names AROS commit
3df3790e58, verified in the local aros-adt-src history. We did not invent a new
OpenZFS workaround or change the shared AROS source tree. The current runtime
libraries were stdc px9y2jvo, stdcio uo0iplm_, POSIX j099bkwp and DOS 4CUMjP0r.

After these runs the runner was tightened to require explicit DOS64 selection
and the exact raw/wrapper past-EOF discriminator. Both retained logs have been
checked against that discriminator (control rejects, corrected passes).
Full release packaging still needs to bind DOS64 and native acceptance to the
selected runtime. Scratch data stays in the external run directories; no large
test file or runtime copy is committed. DUT/user hosted sessions were untouched.
