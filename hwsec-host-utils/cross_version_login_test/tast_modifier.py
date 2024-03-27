#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script updates the config of x-ver data in tast-tests.

This script is capable of:
- walks through the existing cross version data
- generates the corresponding configuration of x-ver fixture data and formats
  it
- generates the parameter of cross version test
"""

import argparse
import os
from pathlib import Path
import re
import sys
from typing import List, NamedTuple, Optional

import util


COPYRIGHT_HEADER = """// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"""

HWSEC_HEADER = """package hwsec

"""


class HSMInfo(NamedTuple):
    """Represents the information of hardware security module."""

    name: str
    camel_name: str
    display_name: str


HSM_LIST = [
    HSMInfo("tpm2", "Tpm2", "TPM2.0 simulator"),
    HSMInfo("tpm_dynamic", "TpmDynamic", "TPM dynamic"),
    HSMInfo("ti50", "Ti50", "Ti50 emulator"),
]


class TastModifier:
    """Modifies x-ver data config file in tast-tests"""

    def __init__(self):
        self.src = util.chromiumos_src()
        self.tast_repo = self.src / Path("platform/tast-tests")
        self.tast_src = self.tast_repo / Path(
            "src/go.chromium.org/tast-tests/cros"
        )
        self.xver_data_config = self.tast_src / Path(
            "common/hwsec/cross_version_fixt_data_config.go"
        )
        self.data_dir = self.tast_src / Path(
            "local/bundles/cros/hwsec/fixture/data/cross_version_login"
        )
        self.go_script = self.src / Path("platform/tast/tools/go.sh")
        self.local_hwsec = Path(
            "go.chromium.org/tast-tests/cros/local/bundles/cros/hwsec"
        )

    def check_git_clean(self, file_path: Path):
        is_dirty = bool(
            util.check_run(
                "git", "status", "--porcelain", file_path, cwd=self.tast_repo
            )
        )
        if is_dirty:
            raise RuntimeError(
                "Repository of tast tests is not clean. Please stash or commit "
                "the current changes."
            )

    def generate_hsm_config(self, info: HSMInfo) -> str:
        """Generate the section of x-ver data config for specific HSM"""
        hsm_dir = self.data_dir / info.name
        contents = []
        for file_path in hsm_dir.iterdir():
            match = re.fullmatch(r"(R(\d+).*)_config\.json", file_path.name)
            if not match:
                continue
            data_prefix = match.group(1)
            milestone = int(match.group(2))
            contents.append((milestone, data_prefix))
        contents.sort()
        first_milestone = contents[0][0]
        last_milestone = contents[-1][0]
        serialized = ""
        for content in contents:
            serialized += f'\t{content[0]}: "{content[1]}",\n'

        first_ms_var = f"First{info.camel_name}DataMilestone"
        latest_ms_var = f"Latest{info.camel_name}DataMilestone"
        prefixes_var = f"{info.camel_name}DataPrefixes"
        cross_version_description = f"cross version with {info.display_name}"
        return (
            f"// {first_ms_var} is the first milestone we have prepared for "
            f"{cross_version_description}\n"
            f"var {first_ms_var} = {first_milestone}\n"
            f"// {latest_ms_var} is the latest milestone we have prepared for "
            f"{cross_version_description}\n"
            f"var {latest_ms_var} = {last_milestone}\n"
            f"// {prefixes_var} are the data prefixed for "
            f"{cross_version_description}\n"
            f"var {prefixes_var} = map[int]string{{\n"
            f"{serialized}"
            f"}}\n"
        )

    def tast_generate_update(self, package_dir: Path):
        env = os.environ.copy()
        env["TAST_GENERATE_UPDATE"] = "1"
        util.check_run(self.go_script, "test", "-count=1", package_dir, env=env)

    def generate_xver_data_config(self, edit: bool):
        """Generates the config of xver data.

        If |edit| is true, it would modify the corresponding config file in
        tast-tests and run the "cros format" and tast genparam process.
        Otherwise, it would print the generated contents of config to stdout.
        """
        content = COPYRIGHT_HEADER + HWSEC_HEADER
        for hsm_info in HSM_LIST:
            content += self.generate_hsm_config(hsm_info)
        if edit:
            self.check_git_clean(self.xver_data_config)
            with open(self.xver_data_config, "w", encoding="utf-8") as f:
                f.write(content)
            util.check_run("cros", "format", self.xver_data_config)
            self.tast_generate_update(self.local_hwsec)
        else:
            print(content)


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    parser = argparse.ArgumentParser(
        description="Update the config of cross verion data in tast-tests"
    )
    parser.add_argument(
        "--debug",
        help="shows debug logging",
        action="store_true",
    )
    parser.add_argument(
        "--edit",
        help="if specified, edits the config in tast-tests, else, print "
        "the new generated config (unformatted)",
        action="store_true",
    )
    opts = parser.parse_args(argv)

    util.init_logging(opts.debug)
    modifier = TastModifier()
    modifier.generate_xver_data_config(opts.edit)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
