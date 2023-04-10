#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script post-process profraws.

It fetches raw profiles from DUT and generates
html coverage reports from the raw profiles.
"""

import argparse
from enum import Enum
import json
from pathlib import Path
from shutil import rmtree
import subprocess
import sys
from typing import List, Optional


# Absolute path of gencov.py.
SRC_DIR = Path(__file__).resolve().parent

# Default excluded files.
# TODO(b/264759042): libhwsec will be included after this.
EXCLUDED_FILES = [
    ".*/metrics_library.h",
    ".*/usr/include/*",
    ".*/var/cache/*",
    ".*/libhwsec-foundation/*",
    ".*/libhwsec/*",
]


BIN_PATH = {
    "attestation": "/sbin/attestationd",
    "bootlockbox": "/sbin/bootlockboxd",
    "chaps": "/sbin/chapsd",
    "cryptohome": "/sbin/cryptohomed",
    "pca_agent": "/sbin/pca_agentd",
    "tpm_manager": "/sbin/tpm_managerd",
    "trunks": "/sbin/trunksd",
    "u2fd": "/bin/u2fd",
    "vtpm": "/sbin/vtpmd",
}


class bcolors(Enum):
    """Colors to decorate messages"""

    HEADER = "\033[95m"
    OKBLUE = "\033[94m"
    OKCYAN = "\033[96m"
    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    FAIL = "\033[91m"
    ENDC = "\033[m"
    BOLD = "\033[1m"
    UNDERLINE = "\033[4m"


class DUT:
    """Definition of DUT object"""

    def __init__(self, ip, board, port=22):
        self.ip = ip
        self.board = board
        self.port = port

    def __repr__(self):
        return f"DUT(ip={self.ip}, board={self.board}, port={self.port})"


def dut_object(arg):
    """Convert the argument string to DUT object"""
    obj_dict = json.loads(arg)
    return DUT(**obj_dict)


def remove_file(path):
    """Remove a file at path if exists.

    TODO: Once we require Python 3.8+, switch to missing_ok=True.
    """
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def cleanup_contents(content_type):
    print(f"Cleaning up contents inside {content_type} dir...")
    for path in (SRC_DIR / content_type).glob("**/*"):
        if path.is_file():
            path.unlink()
        elif path.is_dir():
            rmtree(path)
    print(f"Done cleaning {content_type} dir.")


def cleanup_old_reports():
    """Cleanup old generated cov reports."""
    cleanup_contents("coverage-reports")


def cleanup():
    """Clean up generated files (profraws, profdata)."""
    cleanup_contents("profraws")
    cleanup_contents("profdata")


def run_command(command, stdout=subprocess.DEVNULL):
    """Runs a shell command

    Args:
        command: Command to run as string.
        stdout: Captured stdout from the child process.
    """
    print(f"{bcolors.OKGREEN.value}Running: {command}")
    subprocess.run(command, check=True, stdout=stdout)


def modify_binary_name(package):
    """Generate bin name from package name.

    Some targets have identical names as their binaries. Need to skip them.
    """

    if package not in ["u2fd"]:
        package += "d"

    return package


def restart_daemons(opts):
    """Restart target daemons to get the latest profraws"""

    def restart_target_daemon(dut, package):
        """Restart daemon"""

        bin_name = modify_binary_name(package)
        with subprocess.Popen(
            [
                "ssh",
                f"root@{dut.ip}",
                f"-p {dut.port}" if int(dut.port) != 22 else "",
                f"restart {bin_name}",
            ],
            shell=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ) as ssh:
            result = ssh.stdout.readlines()
            if not result:
                print(ssh.stderr.readlines())
            else:
                print(result[0].decode("utf-8"))

    for dut in opts.duts:
        for package in opts.packages:
            restart_target_daemon(dut, package)


def fetch_profraws(opts):
    """Fetch profraws from opts.duts"""

    (SRC_DIR / "profraws").mkdir(exist_ok=True)

    def fetch(dut, package):
        (SRC_DIR / "profraws" / package).mkdir(exist_ok=True)
        cmd = [
            "rsync",
            f"--rsh=ssh -p{dut.port}",
            (
                f"root@{dut.ip}:"
                f"/mnt/stateful_partition/unencrypted/profraws/{package}*"
            ),
            SRC_DIR / "profraws" / package,
        ]
        run_command(cmd)

    # Default user is root.
    for dut in opts.duts:
        for package in opts.packages:
            fetch(dut, package)


def create_indexed_profdata(packages):
    """Index raw profiles for packages and generate profdata

    Args:
        packages: List of target packages
    """

    # This profdata will be used for merged coverage report.
    merged_profdata_path = SRC_DIR / "profdata" / "merged.profdata"
    merged_profdata_path.parent.mkdir(parents=True, exist_ok=True)
    merged_profdata_path.touch()

    # Generate profdata for each individual package.
    for package in packages:
        profdata_path = SRC_DIR / "profdata" / f"{package}.profdata"
        profdata_path.parent.mkdir(parents=True, exist_ok=True)
        profdata_path.touch()

        for file in (SRC_DIR / "profraws" / package).glob("*.profraw"):
            cmd = [
                "llvm-profdata",
                "merge",
                file,
                profdata_path,
                "-o",
                profdata_path,
            ]
            run_command(cmd)
            cmd = [
                "llvm-profdata",
                "merge",
                file,
                merged_profdata_path,
                "-o",
                merged_profdata_path,
            ]
            run_command(cmd)


def generate_html_report(opts):
    """Generate a line by line html coverage report from indexed profdata.

    Args:
        opts: Input args
    """

    def gen_merged_report_single_pkg(package):
        """Generate a line-level cov report for a single package for DUTs.

        Args:
            package: target package
        """
        if package not in BIN_PATH:
            print(f"Invalid package {package}!")
            return

        cmd = [
            "llvm-cov",
            "show",
            "-format=html",
            "-use-color",
            f"--show-instantiations={str(opts.show_instantiations).lower()}",
            f"-output-dir={SRC_DIR}/coverage-reports/{package}/",
            f"-instr-profile={SRC_DIR}/profdata/{package}.profdata",
        ]
        for idx, dut in enumerate(opts.duts):
            bin_location = Path(f"/build/{dut.board}/usr/{BIN_PATH[package]}")
            # As per https://llvm.org/docs/CommandGuide/llvm-cov.html#id3,
            # if we have more than one instrumented binaries to pass, we
            # need to pass them by -object arg (starting from the latter).
            cmd.append(bin_location if idx == 0 else f"-object={bin_location}")

        # Exclude default files.
        for filename in EXCLUDED_FILES:
            cmd.append(f"-ignore-filename-regex={filename}")

        # Exclude files from args.
        for filename in opts.ignore_filename_regex or []:
            cmd.append(f"-ignore-filename-regex={filename}")

        print(f"Generating report for {package}...")
        run_command(cmd)

    def gen_merged_report_multi_pkg(packages):
        # All of the package name should be valid.
        for package in packages:
            if package not in BIN_PATH:
                print(f"Invalid package {package}")
                return

        cmd = [
            "llvm-cov",
            "show",
            "-format=html",
            "-use-color",
            f"--show-instantiations={str(opts.show_instantiations).lower()}",
            f"-output-dir={SRC_DIR}/coverage-reports/merged/",
            f"-instr-profile={SRC_DIR}/profdata/merged.profdata",
        ]

        for idx, package in enumerate(packages):
            for jdx, dut in enumerate(opts.duts):
                bin_location = Path(
                    f"/build/{dut.board}/usr/{BIN_PATH[package]}"
                )
                # As per https://llvm.org/docs/CommandGuide/llvm-cov.html#id3,
                # if we have more than one instrumented binaries to pass, we
                # need to pass them by -object arg (starting from the latter).
                cmd.append(
                    bin_location
                    if idx | jdx == 0
                    else f"-object={bin_location}"
                )

        # Exclude default files.
        for filename in EXCLUDED_FILES:
            cmd.append(f"-ignore-filename-regex={filename}")

        # Exclude files from args.
        for filename in opts.ignore_filename_regex or []:
            cmd.append(f"-ignore-filename-regex={filename}")

        print(f"Generating report for {packages}...")
        run_command(cmd)

    for package in opts.packages:
        gen_merged_report_single_pkg(package)
    gen_merged_report_multi_pkg(opts.packages)


def parse_command_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--duts",
        nargs="+",
        type=dut_object,
        required=True,
        help='JSON string representing DUT instances with "ip", "board", "port" attributes.',
    )
    parser.add_argument(
        "--packages",
        nargs="+",
        required=True,
        help="List of the target packages separated by spaces.",
    )
    parser.add_argument(
        "--show-instantiations",
        action="store_true",
        help="If enabled, cov reports will contain each instantiation data.",
    )
    parser.add_argument(
        "--restart-daemons",
        action="store_true",
        help="If enabled, daemons will be restarted before fetching profraws.",
    )
    parser.add_argument(
        "-i",
        "--ignore-filename-regex",
        nargs="+",
        help="Skip source code files with file paths that match the given "
        "regular expression. i.e, use '-i usr/* third_party/*' "
        "to exclude files in third_party/ and usr/ folders from the report.",
    )
    parser.add_argument(
        "-c",
        action="store_true",
        help="Cleanup previously generated cov reports from local storage.",
    )

    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    try:
        opts = parse_command_arguments(argv)
        if opts.c:
            cleanup_old_reports()
        if opts.restart_daemons:
            restart_daemons(opts)
        fetch_profraws(opts)
        create_indexed_profdata(opts.packages)
        generate_html_report(opts)
    finally:
        cleanup()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
