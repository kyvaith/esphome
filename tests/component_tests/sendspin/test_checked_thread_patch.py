"""Guard/idempotency checks against the explicitly provided reviewed SDK sources."""

import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
PATCH = (
    ROOT / "esphome/components/sendspin/sendspin_memory_patch/patch_sendspin_memory.py"
)


def test_patch_transaction_and_idempotency():
    supplied = os.environ.get("ESPHOME_TEST_SENDSPIN_BASELINE")
    if not supplied:
        raise unittest.SkipTest(
            "Provide the reviewed Sendspin 0.9.3 managed-component directory"
        )
    spec = importlib.util.spec_from_file_location("checked_patch", PATCH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    thread = module._THREAD_PATCH
    source = Path(supplied)
    names = set(thread.BEFORE) | set(module.GUARDED_SHA256)
    with tempfile.TemporaryDirectory(prefix="sendspin-patch-") as directory:
        target = Path(directory)
        (target / "idf_component.yml").write_text("version: 0.9.3\n", encoding="utf8")
        for name in names:
            path = target / name
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / name, path)
        before = {name: (target / name).read_bytes() for name in names}
        assert module.patch_component(target) == 13
        expected = dict(module.GUARDED_SHA256)
        expected.update(thread.AFTER)
        for name, digest in expected.items():
            assert hashlib.sha256((target / name).read_bytes()).hexdigest() == digest
        assert module.patch_component(target) == 0
        # Unknown late owner must be rejected before any earlier output is written.
        for name, data in before.items():
            (target / name).write_bytes(data)
        corrupt = target / "include/sendspin/client.h"
        corrupt.write_bytes(b"unreviewed source\n")
        snapshot = {name: (target / name).read_bytes() for name in names}
        try:
            module.patch_component(target)
            raise AssertionError("unknown owner accepted")
        except RuntimeError as error:
            assert "Unreviewed" in str(error)
        assert all(
            (target / name).read_bytes() == data for name, data in snapshot.items()
        )


if __name__ == "__main__":
    test_patch_transaction_and_idempotency()
