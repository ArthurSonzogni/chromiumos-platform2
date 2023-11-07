#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script post-process profraws.

It fetches raw profiles from DUT and generates
html coverage reports from the raw profiles.
"""

import argparse
import enum
import itertools
import json
from pathlib import Path
import shutil
import subprocess
import sys
import types
from typing import List, Optional


# Max number of profraws to be processes at once.
MAX_CHUNK_SIZE = 2000

# Max number of times the profdata generation should be executed.
MAX_TRIES = 3

# Absolute path of gencov.py.
SRC_DIR = Path(__file__).resolve().parent

# Default excluded files.
EXCLUDED_FILES = tuple(
    (
        ".*/metrics_library.h",
        ".*/usr/include/*",
        ".*/var/cache/*",
    )
)

# Include coverage info for shared libs only.
EXCLUDED_FILES_FROM_SHARED_LIBS = tuple(
    (
        ".*/tpm_manager/*",
        ".*/trunks/*",
        ".*/chaps/*",
        ".*/attestation/*",
        ".*/bootlockbox/*",
        ".*/u2fd/*",
        ".*/vtpm/*",
        ".*/cryptohome/*",
        ".*/pca_agent/*",
        ".*/libchrome/*",
        ".*/platform2/metrics/*",
    )
)

# Include coverage info for relevant packages only.
EXCLUDED_FILES_FROM_PACKAGE = types.MappingProxyType(
    {
        "attestation": [
            ".*/tpm_manager/*",
            ".*/trunks/*",
            ".*/chaps/*",
            ".*/libhwsec/*",
            ".*/libhwsec-foundation/*",
        ],
        "bootlockbox": [
            ".*/trunks/*",
            ".*/libhwsec/*",
            ".*/libhwsec-foundation/*",
        ],
        "tpm_manager": [
            ".*/trunks/*",
            ".*/libhwsec/*",
            ".*/libhwsec-foundation/*",
        ],
        "vtpm": [
            ".*/tpm_manager/*",
            ".*/trunks/*",
            ".*/libhwsec/*",
            ".*/libhwsec-foundation/*",
        ],
        "chaps": [
            ".*/trunks/*",
            ".*/libhwsec/*",
            ".*/libhwsec-foundation/*",
            ".*/metrics/*",
        ],
        "cryptohome": [
            ".*/trunks/*",
            ".*/libhwsec/*",
            ".*/libhwsec-foundation/*",
            ".*/metrics/*",
        ],
        "pca_agent": [
            ".*/trunks/*",
            ".*/libhwsec/*",
            ".*/libhwsec-foundation/*",
            ".*/metrics/*",
        ],
        "trunks": [
            ".*/libhwsec/*",
            ".*/libhwsec-foundation/*",
            ".*/metrics/*",
        ],
        "u2fd": [
            ".*/trunks/*",
            ".*/libhwsec/*",
            ".*/libhwsec-foundation/*",
            ".*/metrics/*",
        ],
    }
)

BIN_PATH = types.MappingProxyType(
    {
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
)

LIB_PATH = types.MappingProxyType(
    {
        "libhwsec": "lib64/libhwsec.so",
        "libhwsec-foundation": "lib64/libhwsec-foundation.so",
    }
)


class bcolors(enum.Enum):
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
    """Definition of DUT object for profiling purpose"""

    def __init__(self, ip, board, packages, port=22):
        self.ip = ip
        self.port = port
        self.board = board
        self.packages = packages

    def __repr__(self):
        return f"""DUT(ip={self.ip}, port={self.port},
board={self.board}, packages={self.packages})"""


def dut_object(arg):
    """Convert the argument string to DUT object"""
    obj_dict = json.loads(arg)
    return DUT(**obj_dict)


def remove_file(path):
    """Remove a file at path if exists."""
    path.unlink(missing_ok=True)


def move_file(source_path, destination_directory):
    """Moves a file from source_path to destination_directory.

    Creates the destination_directory if not present.
    """
    destination_directory.mkdir(parents=True, exist_ok=True)
    destination_file_path = destination_directory / source_path.name
    shutil.move(source_path, destination_file_path)


def cleanup_contents(content_type):
    print(f"Cleaning up contents inside {content_type} dir...")
    for path in (SRC_DIR / content_type).glob("**/*"):
        if path.is_file():
            path.unlink()
        elif path.is_dir():
            shutil.rmtree(path)
    print(f"Done cleaning {content_type} dir.")


def cleanup_old_reports():
    """Cleanup old generated cov reports."""
    cleanup_contents("coverage-reports")


def cleanup():
    """Clean up generated files (profraws, profdata)."""
    cleanup_contents("profraws")
    cleanup_contents("profdata")


def split_list(lst, chunk_size):
    """Split the list `lst` in maximum of `chunk_size` each"""
    for i in range(0, len(lst), chunk_size):
        yield lst[i : i + chunk_size]


def run_command(command):
    """Runs a shell command

    Args:
        command: Command to run as string.
    """
    print(f"{bcolors.OKGREEN.value}Running: {command}")
    try:
        subprocess.run(command, check=True, capture_output=True)
    except subprocess.CalledProcessError as e:
        return e.stderr


def modify_binary_name_if_necessary(package):
    """Generate bin name from package name.

    Some targets have identical names as their binaries. Need to skip them.
    """

    if package not in ["u2fd"]:
        package += "d"

    return package


def restart_daemons(duts):
    """Restart target daemons to get the latest profraws"""

    def restart_target_daemon(dut, package):
        """Restart daemon"""

        bin_name = modify_binary_name_if_necessary(package)
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

    for dut in duts:
        for package in dut.packages:
            restart_target_daemon(dut, package)


def fetch_profraws(duts):
    """Fetch profraws from DUTs.

    Each DUT object might contain multiple packages. This function
    would collect profraws by package from each DUT and store them
    in profraws/{package} dir.
    """

    (SRC_DIR / "profraws").mkdir(exist_ok=True)

    def fetch(dut, package="merged"):
        (SRC_DIR / "profraws" / package).mkdir(exist_ok=True)
        file_prefix = "" if package == "merged" else package
        cmd = [
            "rsync",
            f"--rsh=ssh -p{dut.port}",
            (
                f"root@{dut.ip}:"
                f"/mnt/stateful_partition/unencrypted/profraws/{file_prefix}*"
            ),
            SRC_DIR / "profraws" / package,
        ]
        run_command(cmd)

    # Default user is root.
    for dut in duts:
        for package in dut.packages:
            fetch(dut, package)

    # There are profraws generated by various services using shared libs.
    # So generally they don't have common prefixes like package-wise profraws.
    # Fetch them all.
    for dut in duts:
        fetch(dut)


def merge_profdata(profdata_path, file_list):
    """Merge list of profraws into profdata

    Args:
        profdata_path: path to profdata file
        file_list: list of raw profiles
    """
    cmd = [
        "llvm-profdata",
        "merge",
        profdata_path,
        "-o",
        profdata_path,
    ] + file_list
    err = run_command(cmd)
    if err is not None:
        try:
            corrupted_path = Path(
                [
                    word
                    for word in err.decode("utf-8").split()
                    if word.endswith(".profraw:")
                ][0][:-1]
            )
            move_file(corrupted_path, SRC_DIR / "corrupted_profraws")
            return False
        except Exception as e:
            print(f"Error occurred while merging profdata: {e}")
    return True


def create_indexed_profdata(duts):
    """Index raw profiles for packages and generate profdata

    Args:
        duts: List of DUT object
    """

    # This profdata will be used for merged coverage report.
    merged_profdata_path = SRC_DIR / "profdata" / "merged.profdata"
    merged_profdata_path.parent.mkdir(parents=True, exist_ok=True)
    merged_profdata_path.touch()

    for _ in range(MAX_TRIES):
        corrupted_profraw_detected = False
        # Generate profdata for each individual package from duts.
        for dut in duts:
            for package in dut.packages:
                profdata_path = SRC_DIR / "profdata" / f"{package}.profdata"
                profdata_path.parent.mkdir(parents=True, exist_ok=True)
                profdata_path.touch(exist_ok=True)

                files = [
                    str(file)
                    for file in (SRC_DIR / "profraws" / package).glob(
                        "*.profraw"
                    )
                ]
                file_chunks = list(split_list(files, MAX_CHUNK_SIZE))
                for file_list in file_chunks:
                    corrupted_profraw_detected = merge_profdata(
                        profdata_path, file_list
                    ) or merge_profdata(merged_profdata_path, file_list)

        # Process profraws from shared libs.
        files = [
            str(file)
            for file in (SRC_DIR / "profraws" / "merged").glob("*.profraw")
        ]
        file_chunks = list(split_list(files, MAX_CHUNK_SIZE))
        for file_list in file_chunks:
            corrupted_profraw_detected = merge_profdata(
                merged_profdata_path, file_list
            )

        if corrupted_profraw_detected is False:
            break


def generate_html_report(opts):
    """Generate a line by line html coverage report from indexed profdata.

    Args:
        opts: Input args
    """

    def gen_report(bin_list, package="merged"):
        if package != "merged" and package not in BIN_PATH:
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

        boards = []
        for idx, bin_location in enumerate(bin_list):
            # As per https://llvm.org/docs/CommandGuide/llvm-cov.html#id3,
            # if we have more than one instrumented binaries to pass, we
            # need to pass them by -object arg (starting from the latter).
            cmd.append(bin_location if idx == 0 else f"-object={bin_location}")
            boards.append(bin_location.parent.parent.parent.name)

        # In the merged report, include the shared lib coverage too.
        if package == "merged":
            for board in boards:
                for libpath in LIB_PATH.values():
                    # Currently we support "elm" as our TPM1.2 candidate.
                    if board in ["elm"]:
                        libpath = libpath.replace("lib64", "lib")
                    cmd.append(f"-object=/build/{board}/usr/{libpath}")

        # Exclude default files.
        for filename in EXCLUDED_FILES:
            cmd.append(f"-ignore-filename-regex={filename}")
        for filename in EXCLUDED_FILES_FROM_PACKAGE.get(package, []):
            cmd.append(f"-ignore-filename-regex={filename}")

        # Exclude files from args.
        for filename in opts.ignore_filename_regex or []:
            cmd.append(f"-ignore-filename-regex={filename}")

        print(f"Generating report for {package}...")
        run_command(cmd)

    def gen_report_for_shared_lib():
        # Generate cov report for shared libs.
        for lib, libpath in LIB_PATH.items():
            instantiations_stat = str(opts.show_instantiations).lower()
            cmd = [
                "llvm-cov",
                "show",
                "-format=html",
                "-use-color",
                f"--show-instantiations={instantiations_stat}",
                f"-output-dir={SRC_DIR}/coverage-reports/{lib}/",
                f"-instr-profile={SRC_DIR}/profdata/merged.profdata",
            ]
            for dut in opts.duts:
                if dut.board == "elm":
                    libpath = libpath.replace("lib64", "lib")
                cmd.append(f"-object=/build/{dut.board}/usr/{libpath}")
            for exfile in EXCLUDED_FILES_FROM_SHARED_LIBS:
                cmd.append(f"-ignore-filename-regex={exfile}")
            print(f"Generating report for {lib}...")
            run_command(cmd)

    # Generate bin locations and make a list by package name.
    pkgbin = {}
    for dut in opts.duts:
        for package in dut.packages:
            if package not in pkgbin:
                pkgbin[package] = []
            pkg_path = Path(f"/build/{dut.board}/usr/{BIN_PATH[package]}")
            pkgbin[package].append(pkg_path)

    # Generate package-wise cov report.
    for pkg_name, bin_list in pkgbin.items():
        gen_report(bin_list, pkg_name)

    # Generate cov report for all packages.
    gen_report(list(itertools.chain.from_iterable(pkgbin.values())))

    # Generate cov report for shared libs.
    gen_report_for_shared_lib()


def parse_command_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--duts",
        type=argparse.FileType("r"),
        required=True,
        help="""JSON string representing DUT instances with "ip", "board",
                "port", "packages" attributes.""",
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

    opts = parser.parse_args(argv)
    opts.duts = json.load(opts.duts)
    opts.duts = [DUT(**dut) for dut in opts.duts]
    return opts


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    opts = parse_command_arguments(argv)
    try:
        if opts.c:
            cleanup_old_reports()
        if opts.restart_daemons:
            restart_daemons(opts.duts)
        fetch_profraws(opts.duts)
        create_indexed_profdata(opts.duts)
        generate_html_report(opts)
    finally:
        if opts:
            cleanup()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
