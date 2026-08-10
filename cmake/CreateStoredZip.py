#!/usr/bin/env python3

import argparse
import os
import zipfile
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create a ZIP whose entries use the stored method."
    )
    parser.add_argument("output", type=Path, help="Output ZIP file")
    parser.add_argument("source", type=Path, help="Directory to archive")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    source = args.source.resolve()
    output = args.output.resolve()

    if not source.is_dir():
        raise SystemExit(f"Source directory does not exist: {source}")

    entries = sorted(
        (path for path in source.rglob("*") if path.is_file()),
        key=lambda path: path.relative_to(source).as_posix(),
    )
    if not entries:
        raise SystemExit(f"No files found in {source}")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f"{output.name}.tmp")

    try:
        with zipfile.ZipFile(
            temporary, "w", compression=zipfile.ZIP_STORED, allowZip64=True
        ) as archive:
            for path in entries:
                archive.write(
                    path,
                    path.relative_to(source).as_posix(),
                    compress_type=zipfile.ZIP_STORED,
                )
        os.replace(temporary, output)
    except BaseException:
        if temporary.exists():
            temporary.unlink()
        raise


if __name__ == "__main__":
    main()
