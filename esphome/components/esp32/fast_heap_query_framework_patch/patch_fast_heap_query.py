"""Install the reviewed, default-off ESP-IDF 5.5.6 largest-query experiment."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_plan(framework_dir: Path) -> list[tuple[Path, bytes, bytes]]:
    """Validate all SDK owners and postimages before any mutation."""
    version = (framework_dir / "version.txt").read_text(encoding="utf-8").strip()
    if version != "v5.5.6":
        raise RuntimeError("Fast heap query requires reviewed ESP-IDF 5.5.6")
    patches = json.loads(
        Path(__file__).with_name("patches.json").read_text(encoding="utf-8")
    )
    plans = []
    for name, patch in patches.items():
        target = (framework_dir / name).resolve()
        if not target.is_relative_to(framework_dir.resolve()):
            raise RuntimeError("SDK patch target is outside the framework")
        original = target.read_bytes()
        current = digest(original)
        if current == patch["after"]:
            continue
        if current != patch["before"]:
            raise RuntimeError(f"SDK source changed; review fast heap query: {name}")
        after = original
        for old, new in patch["replacements"]:
            old_bytes = old.encode("utf-8")
            if after.count(old_bytes) != 1:
                raise RuntimeError(f"Reviewed SDK owner block missing: {name}")
            after = after.replace(old_bytes, new.encode("utf-8"))
        if digest(after) != patch["after"]:
            raise RuntimeError(f"SDK postimage mismatch: {name}")
        plans.append((target, original, after))
    return plans


def patch_framework(framework_dir: Path, check_only: bool = False) -> str:
    plans = load_plan(framework_dir)
    if check_only:
        return f"verified {len(plans)} pending SDK files; no writes"
    if not plans:
        return "already guarded"
    lock = framework_dir / ".esphome-fast-heap-query.lock"
    # Avoid overlapping patch installers. The build owner must also serialize
    # other builds using this SDK, or supply a private framework copy.
    lock_fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    temporary = None
    try:
        os.close(lock_fd)
        plans = load_plan(framework_dir)
        for target, original, after in plans:
            if target.read_bytes() != original:
                raise RuntimeError(f"SDK source changed during install: {target}")
            with tempfile.NamedTemporaryFile(
                dir=target.parent, prefix=target.name + ".", suffix=".tmp", delete=False
            ) as stream:
                temporary = Path(stream.name)
                stream.write(after)
                stream.flush()
                os.fsync(stream.fileno())
            temporary.replace(target)
            temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        lock.unlink(missing_ok=True)
    return f"installed guarded fast heap query in {len(plans)} SDK files"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("framework_dir", type=Path)
    parser.add_argument(
        "--check", action="store_true", help="Verify only; do not write"
    )
    args = parser.parse_args()
    print(patch_framework(args.framework_dir, args.check))


if __name__ == "__main__":
    main()
