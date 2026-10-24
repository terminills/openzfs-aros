# Automatic drawer title lifetime investigation

Status: source-supported hypothesis, not a qualified desktop fix.

The automatic WBRun opening can show a garbled title while the mounted files
remain readable. Manual opening through the desktop volume icon previously
showed the correct `OZPUBLIC:` title twice (`native-gui-run.wx4RRXdn`). The
clean-engine GUI run `native-gui-run.KQyxomdW` reproduces the automatic-title
symptom. Native run `public-service-run.YcFEA8Za` independently passes root
`Examine` name and `NameFromLock` path assertions, plus directory enumeration
and the complete public storage test. This narrows the issue but does not
exclude every possible memory-corruption source.

## Borrowed title path

Read-only audit of ADT AROS commit
`671efe3b321926332e3330a0e138ed4a3d4e820e` (the files below had no local changes):

- `workbench/libs/workbench/openworkbenchobjecta.c`, `HandleDrawer` (line 556):
  allocates a copy of the requested name for the asynchronous open message.
- `workbench/system/Wanderer/wanderer.c`, `WBHM_TYPE_OPEN` (line 3732):
  passes that name to drawer creation, then replies to the message.
  `CreateDrawerWindow` forwards the drawer pointer as `MUIA_IconWindow_Location`.
- `workbench/system/Wanderer/iconwindow.c` (lines 501, 684, 730):
  construction takes that location pointer as the title, passes it as
  `MUIA_Window_Title`, and retains it in `iwd_Title`.
- `workbench/libs/muimaster/classes/window.c` (line 3212): the title setter
  retains the supplied pointer rather than duplicating its string.
- `workbench/libs/workbench/support_messages.c` (lines 88–98): message cleanup
  frees `Open.Name`.

The candidate defect is therefore a window title outliving the message-owned
string. IconWindow's later location setter already copies the path into its
owned `iwd_DirectoryPath` buffer and updates the window title (lines 974–981).
Construction needs equivalent lifetime safety, not a filesystem name workaround.

## Required follow-up

Coordinate the core desktop change with its owner. In an isolated build, ensure
the title references storage owned for the window's lifetime; audit construction,
location changes and disposal together. Test automatic and manual openings,
repeated close/reopen and location changes after message cleanup, alongside
the unchanged OpenZFS directory/readback gates. No Wanderer or Muimaster source
was modified by this investigation, and no runtime fix is claimed here.
