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
from pathlib import Path
import subprocess
import sys
from typing import List, Optional


# Absolute path of gencov.py.
SRC_DIR = Path(__file__).resolve().parent

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


def remove_file(path):
    """Remove a file at path if exists.

    TODO: Once we require Python 3.8+, switch to missing_ok=True.
    """
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def cleanup():
    """Clean up generated files (profraws, profdata)."""
    for p in SRC_DIR.glob("*.profdata"):
        p.unlink()

    for p in (SRC_DIR / "profraws").glob("*.profraw"):
        p.unlink()


def run_command(command, stdout=subprocess.DEVNULL):
    """Runs a shell command

    Args:
        command: Command to run as string.
        stdout: Captured stdout from the child process.
    """
    print(f"{bcolors.OKGREEN.value}Running: {command}")
    subprocess.run(command, check=True, stdout=stdout)


def fetch_profraws(dut_ip, packages, port=22):
    """Fetch profraws of target `package` from `dut_ip`

    Args:
        dut_ip: IP address of DUT.
        packages: List of target packages.
        port: Port, if any. Default is 22.
    """
    (SRC_DIR / "profraws").mkdir(exist_ok=True)

    # Default user is root.
    for package in packages:
        command = [
            "rsync",
            f"--rsh=ssh -p{port}",
            (
                f"root@{dut_ip}:"
                f"/mnt/stateful_partition/unencrypted/profraws/{package}*"
            ),
            SRC_DIR / "profraws",
        ]
        run_command(command)


def create_indexed_profdata(packages):
    """Index raw profiles for packages and generate profdata

    Args:
        packages: List of target packages
    """

    # This profdata will be used for merged coverage report.
    merged_profdata_path = SRC_DIR / "merged.profdata"
    merged_profdata_path.write_text("")

    # Generate profdata for each individual package.
    for package in packages:
        profdata_path = SRC_DIR / f"{package}.profdata"
        profdata_path.write_text("")

        for file in (SRC_DIR / "profraws").glob(f"{package}*.profraw"):
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


def generate_html_report(opts, packages):
    """Generate a line by line html coverage report from indexed profdata.

    Args:
        opts: Input args
        packages: List of target packages
    """
    cmd_for_merged_profdata = [
        "llvm-cov",
        "show",
        "-format=html",
        "-use-color",
        f"--show-instantiations={str(opts.show_instantiations).lower()}",
        f"-output-dir={SRC_DIR}/coverage-reports/merged/",
        f"-instr-profile={SRC_DIR}/merged.profdata",
    ]

    for idx, package in enumerate(packages):
        if package not in BIN_PATH:
            print(f"Invalid package name {package}!")
            cleanup()
            sys.exit(2)

        base = f"/build/{opts.board}/usr"
        bin_location = base + BIN_PATH[package]

        # As per https://llvm.org/docs/CommandGuide/llvm-cov.html#id3,
        # if we have more than one instrumented binaries to pass, we
        # need to pass them by -object arg (starting from the latter).

        cmd_for_merged_profdata.append(
            bin_location if idx == 0 else f"-object={bin_location}"
        )
        command = [
            "llvm-cov",
            "show",
            bin_location,
            "-format=html",
            "-use-color",
            f"--show-instantiations={str(opts.show_instantiations).lower()}",
            f"-output-dir={SRC_DIR}/coverage-reports/{package}/",
            f"-instr-profile={SRC_DIR}/{package}.profdata",
        ]

        for ext in opts.ignore_filename_regex or []:
            command.append(f"-ignore-filename-regex={ext}")
        print(f"Generating report for {package}...")
        run_command(command)

    for ext in opts.ignore_filename_regex or []:
        cmd_for_merged_profdata.append(f"-ignore-filename-regex={ext}")
    print("Generating merged report...")
    run_command(cmd_for_merged_profdata)


def modify_binary_name(package):
    """Generate bin name from package name.

    Some targets have identical names as their binaries. Need to skip them.
    """

    if package not in ["u2fd"]:
        package += "d"

    return package


def restart_daemons(host, packages):
    """Restart target daemons to dump the latest profraws"""

    def restart_target_daemon(package):
        """Restart daemon"""

        bin_name = modify_binary_name(package)
        with subprocess.Popen(
            [
                "ssh",
                f"root@{host}",
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

    for package in packages:
        restart_target_daemon(package)


def parse_command_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--dut", type=str, required=True, help="Address of the target DUT."
    )
    parser.add_argument(
        "--board", type=str, required=True, help="Board of the target DUT."
    )
    parser.add_argument(
        "--packages",
        nargs="+",
        required=True,
        help="List of the target packages separated by spaces.",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=22,
        help="Port that the target DUT is currently using (if any).",
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

    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    # Clean up if there exists any previously generated files.
    cleanup()

    opts = parse_command_arguments(argv)

    if opts.restart_daemons:
        restart_daemons(opts.dut, opts.packages)
    fetch_profraws(opts.dut, opts.packages, opts.port)
    create_indexed_profdata(opts.packages)
    generate_html_report(opts, opts.packages)
    cleanup()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
