# Root ZPL fixed-engine bundle qualification

Archive: `build-aros/mmake-bundle.knv27h5n.tar.xz`

SHA256: `091b410750703c3bb60ef96d2534b1859191186975caf2bfda25a1a0f985fa39`

- GUI: `native-gui-run.dHKuj0ax`, eight CPUs, Manager and Wanderer. Mounted
  OZPUBLIC files are visible. Drawer and Manager closed while Wanderer stayed
  running; post-UI exact file read and full storage suite passed.
- Cold: `public-service-run.JGCaHIi6`, eight CPUs, persistent startup import
  and mount restore enabled. Three read-only root-file checks and startup
  restores passed; member hashes stayed unchanged and zdb found no leaks.
- Extracted: `public-service-run.lXNUEfyn`, eight CPUs, full RAM-backed upstream
  acceptance suite passed with freshly extracted production binaries.

The private staging view selects the current MetaMake stage and
`shared-engine.NAQEJFme`, leaving the older build package unchanged. The existing
package gate verified tested binary hashes, release options, full engine
symbols and terminal runtime evidence before staging. Stage and extracted
SHA256SUMS verification output is retained.

An initial GUI run `Wj9aMb4L` answered a live pool query but timed out after
mixed relative/absolute mouse input moved its window instead of closing it.
Its failure logs are retained. A subsequent basic GUI run `4bjWRJxo` passed,
but was not sufficient for the package gate's required Wanderer post-UI check;
the full desktop run above supplied that evidence without weakening the gate.

The archive is local, not uploaded. Platform overlays are explicit separate
integration inputs. This does not establish stock-nightly compatibility,
physical-media durability or repair of older uninitialized pool roots.
