#!/usr/bin/env python3
"""Fresh regular files only; tests never operate user disk images."""
import importlib.util
import os
from pathlib import Path
import tempfile
from unittest.mock import patch
import errno

spec = importlib.util.spec_from_file_location(
    "sparse_digest", Path(__file__).with_name("sparse-digest.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory(prefix="openzfs-sparse-check-") as temp:
    path = Path(temp) / "disk"
    size = 13 * 1024**4
    with path.open("wb") as f:
        f.truncate(size)
    empty = module.digest(path)
    assert empty == module.digest(path)
    with path.open("r+b") as f:
        f.seek(size - 512)
        f.write(b"X" * 512)
        f.flush()
        os.fsync(f.fileno())
    written = module.digest(path)
    assert written != empty and written == module.digest(path)
    with path.open("r+b") as f:
        f.seek(size - 512)
        f.write(b"Y" * 512)
        f.flush()
        os.fsync(f.fileno())
    assert module.digest(path) != written
    before = module.digest(path)
    with path.open("r+b") as f:
        f.seek(1024**3)
        f.write(bytes(4096))
        f.flush()
        os.fsync(f.fileno())
    assert module.digest(path) != before  # allocated zero data is not a hole
    before = module.digest(path)
    with path.open("r+b") as f:
        f.truncate(size - 1024**3)
    assert module.digest(path) != before
    link = Path(temp) / "link"
    link.symlink_to(path)
    try:
        module.digest(link)
    except OSError:
        pass
    else:
        raise AssertionError("symlink accepted")
    with patch.object(module.os, "lseek", side_effect=OSError(errno.ENOTSUP, "unsupported")):
        try:
            module.digest(path)
        except OSError as error:
            assert error.errno == errno.ENOTSUP
        else:
            raise AssertionError("unsupported extent reporting accepted")
print("PASS sparse digest: 13TiB, stable, high-offset data, mutation, zero allocation, resize, symlink refusal, unsupported refusal")
