# Current-source standalone and shared engine qualification

Source revision `80e0ce6` was rebuilt in `source-engine.phZ4CAdP`: all 248
upstream inventory units, service adapters, full dispatcher, Manager and
acceptance client, plus a freshly compiled nvpair/XDR archive. No old engine
objects seeded this build. The selected compiler/SDK, private pthread,
crypto and zlib archives remain external prebuilt inputs; this is not an
official SDK or fully bootstrapped dependency build.

`public-service-run.TI4CmAlj` passed the full file/native-partition force
suite with that standalone service, including create/add/attach/replace
through CLI and Manager APIs. The acceptance client and CLI were the
separately rebuilt current-source inputs identified by the run hashes.

The same fresh engine object was used to build `shared-engine.Nmm3fNwi`
with a private copy of the selected genmodule. The launcher symbol check
rejects embedding the engine in the launcher. `public-service-run.3flp4eVv`
passed the same full native force suite using the real shared library and
launcher. The runner now requires its actual library lifetime test before
starting resident services: eight repeated load/unload cycles, invalid modes,
nested opens, foreign-task refusal and denied-start/exit/reload recovery.
Both runs exited zero; test media was hashed and pruned.

These are eight-CPU isolated QEMU tests with selected DOS/CRT prerequisites,
not physical storage or user-hosted-session tests. Build manifests and logs
are under `build/`, and the two runtime records under `standalone/` and
`shared-native/`. The full-port objective remains incomplete.
