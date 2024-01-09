#!/usr/bin/env vpython3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Convenience script to regenerate all auto-generated files."""

import argparse
import contextlib
import functools
from pathlib import Path
import site
import sys
import tempfile
from typing import Callable, Dict, List, Optional


HERE = Path(__file__).resolve().parent
BOXSTER_PYTHON_PATH = HERE.parent.parent / "config" / "python"
site.addsitedir(HERE)
site.addsitedir(BOXSTER_PYTHON_PATH)

# pylint: disable=wrong-import-position
from chromiumos.config.test import fake_config  # pylint: disable=import-error
from cros_config_host import cros_config_proto_converter
from cros_config_host import cros_config_schema
from cros_config_host import generate_schema_doc
from cros_config_host import power_manager_prefs_gen_schema


# pylint: enable=wrong-import-position


SCHEMA_YAML = HERE / "cros_config_host" / "cros_config_schema.yaml"


def regen_readme(output_path: Path) -> None:
    """Regenerate README.md."""
    # Sections of the current README are preserved.  Copy to update.
    current_readme = HERE / "README.md"
    if current_readme != output_path:
        output_path.write_text(
            current_readme.read_text(encoding="utf-8"),
            encoding="utf-8",
        )
    generate_schema_doc.Main(schema=SCHEMA_YAML, output=output_path)


def regen_test_data(
    output_path: Path, configs: List[Path], filter_build_details: bool = False
) -> None:
    """Regenerate cros_config_schema test data."""
    cros_config_schema.Main(
        schema=SCHEMA_YAML,
        config=None,
        output=output_path,
        configs=configs,
        filter_build_details=filter_build_details,
    )


def regen_proto_converter(output_path: Path) -> None:
    """Regenerate cros_config_proto_converter files."""
    # cros_config_proto_converter writes file paths in generated files.  We
    # change directories and use relative paths to keep paths consistent.
    output_dir = output_path.parents[3]
    output_path = output_path.relative_to(output_dir)
    with contextlib.chdir(output_dir):
        cros_config_proto_converter.Main(
            project_configs=[fake_config.FAKE_PROJECT_CONFIG],
            program_config=fake_config.FAKE_PROGRAM_CONFIG,
            output=output_path,
            dtd_path=HERE / "cros_config_host" / "media_profiles.dtd",
        )


GENERATORS: Dict[str, Callable[[Path], None]] = {
    "cros_config_host/power_manager_prefs_schema.yaml": (
        power_manager_prefs_gen_schema.Main
    ),
    "test_data/test_import.json": functools.partial(
        regen_test_data, configs=[HERE / "test_data" / "test_import.yaml"]
    ),
    "test_data/test_merge.json": functools.partial(
        regen_test_data,
        configs=[
            HERE / "test_data" / "test_merge_base.yaml",
            HERE / "test_data" / "test_merge_overlay.yaml",
        ],
    ),
    "test_data/test_build.json": functools.partial(
        regen_test_data, configs=[HERE / "test_data" / "test.yaml"]
    ),
    "test_data/test.json": functools.partial(
        regen_test_data,
        configs=[HERE / "test_data" / "test.yaml"],
        filter_build_details=True,
    ),
    "test_data/test_arm.json": functools.partial(
        regen_test_data,
        configs=[HERE / "test_data" / "test_arm.yaml"],
        filter_build_details=True,
    ),
    "test_data/proto_converter/sw_build_config/fake_project.json": (
        regen_proto_converter
    ),
    "README.md": regen_readme,
}


def regenerate_all() -> None:
    """Regenerate all generated files."""
    for output_filename, generator in GENERATORS.items():
        generator(HERE / output_filename)


def check_all() -> List[Path]:
    """Ensure no generated files need updated.

    Returns:
        A list of paths that require update.
    """
    files_need_updated: List[Path] = []
    with tempfile.TemporaryDirectory() as tmpdir:
        for output_filename, generator in GENERATORS.items():
            current_path = HERE / output_filename
            new_path = Path(tmpdir) / output_filename
            new_path.parent.mkdir(exist_ok=True, parents=True)
            generator(new_path)
            if not current_path.exists() or new_path.read_text(
                encoding="utf-8"
            ) != current_path.read_text(encoding="utf-8"):
                files_need_updated.append(current_path)
    return files_need_updated


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """CLI frontend."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Ensure no generated files need updated.",
    )
    opts = parser.parse_args(argv)

    if opts.check:
        files_need_updated = check_all()
        if files_need_updated:
            print(
                "ERROR: Generated files in chromeos-config need updated:\n",
                file=sys.stderr,
            )
            for path in files_need_updated:
                print(f"- {path}", file=sys.stderr)
            print(f"\nPlease run: {sys.argv[0]}", file=sys.stderr)
            return 1
    else:
        regenerate_all()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
