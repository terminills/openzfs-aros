# Native-device production cold import

`public-service-run.GZppiXzE` completed with exit zero using eight vCPUs and
the normal shared engine `shared-engine.rfZPXg5j` documented in `../production`.
All external artifacts reside under
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.

The initial guest wrote and exported an encrypted native-device pool. A fresh
QEMU guest imported it locked, refused mounting without a key, loaded the key,
read exact data, refused writes on the read-only mount and exported it.
The native backing hash remained unchanged during that read-only boot. The
public-service lifecycle/CLI/DOS checks also passed. This is emulated ATA
device evidence, not physical-drive durability, healing or power-loss proof.

The first launch selected PERSISTENT=1 together with native discovery. That
incompatible selection was rejected before a VM was launched; its log is
retained. The corrected selection uses NATIVE_COLD=1 and PERSISTENT=0, because
PERSISTENT selects the separate file-backed pool test. Commit ccc6a77 makes
that configuration failure explicit rather than a silent shell exit.

The successful runner pruned both disposable ISOs (9.14 GiB); hashes, logs,
backing files and source snapshot remain retained. This adds cold-import
integration evidence, not a new directory-exchange API or full-port completion.
