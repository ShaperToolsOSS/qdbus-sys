#!/usr/bin/env python3
# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

"""Compare two unstripped staged installations with libabigail's abidiff."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

LIBRARIES = (
    "libiwdinterface.so",
    "libqsystemd-networkd-interface.so",
    "libqdbus-sys-connectivityd-interface.so",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="previous release's staged installation root")
    parser.add_argument("candidate", type=Path, help="new release's staged installation root")
    parser.add_argument("--libdir", default="usr/lib", help="relative library directory in both roots")
    parser.add_argument("--includedir", default="usr/include", help="relative public-header directory")
    args = parser.parse_args()
    tool = shutil.which("abidiff")
    if not tool:
        parser.error("abidiff is required; install libabigail (no symbol-only fallback is used)")
    for directory in (args.libdir, args.includedir):
        if Path(directory).is_absolute() or ".." in Path(directory).parts:
            parser.error("--libdir and --includedir must stay relative to the installation roots")
    baseline, candidate = args.baseline.resolve(), args.candidate.resolve()
    # Check all inputs before reporting any successful comparisons.
    for root in (baseline, candidate):
        if not (root / args.includedir / "qdbus-sys-version.h").is_file():
            parser.error(f"missing version header in {root / args.includedir}")
        for name in LIBRARIES:
            if not (root / args.libdir / name).is_file():
                parser.error(f"missing library: {root / args.libdir / name}")
    failed = False
    for name in LIBRARIES:
        print(f"Checking {name}", flush=True)
        result = subprocess.run([
            tool, "--fail-no-debug-info", "--no-added-syms", "--drop-private-types",
            "--headers-dir1", str(baseline / args.includedir),
            "--headers-dir2", str(candidate / args.includedir),
            str(baseline / args.libdir / name), str(candidate / args.libdir / name),
        ], check=False)
        if result.returncode:
            # Exit 4 can include layout changes requiring review; accepting only
            # the 'incompatible' bit would miss those. Tool errors fail too.
            print(f"{name}: ABI difference or tool error requires review (exit {result.returncode})", file=sys.stderr)
            failed = True
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
