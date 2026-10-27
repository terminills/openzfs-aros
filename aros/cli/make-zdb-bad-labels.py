#!/usr/bin/env python3
"""Make disposable damaged copies; never open the source for writing."""
import json
import shutil
import sys
from pathlib import Path


def prepare(source, destination):
    source = Path(source).resolve(strict=True)
    destination = Path(destination)
    size = source.stat().st_size
    label = 256 * 1024
    # This test is deliberately bounded to the qualification file fixture.
    if not source.is_file() or not 4 * label <= size <= 128 * 1024 * 1024:
        raise ValueError("expected a regular 1..128 MiB fixture")
    if size % label:
        raise ValueError("fixture must be label aligned")
    destination.mkdir(exist_ok=False)
    offsets = [0, label, size - 2 * label, size - label]
    manifest = {}
    for name, indices in (("one-bad", [0]), ("all-bad", [0, 1, 2, 3])):
        # Exclusive create prevents writing through a preexisting file/symlink.
        with source.open("rb") as src, (destination / name).open("xb") as out:
            shutil.copyfileobj(src, out)
            for index in indices:
                # vdev_label_t: 8K pad + 8K bootenv + 112K vdev_phys.
                # Leave uberblocks and all pool data unchanged.
                out.seek(offsets[index] + 16 * 1024)
                out.write(bytes(112 * 1024))
        manifest[name] = {"size": size, "zeroed_config_offsets":
                          [offsets[i] + 16 * 1024 for i in indices]}
    (destination / "mutation.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    prepare(sys.argv[1], sys.argv[2])
