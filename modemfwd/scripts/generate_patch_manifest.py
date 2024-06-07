#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generates a patch manifest on the ebuild's SRC_URI payloads."""

import argparse
import collections
import contextlib
import glob
import hashlib
import logging
import os
import shutil
import subprocess
import sys
import tempfile


MODEM_LIST = ["em060", "fm101", "fm350", "l850", "nl668", "rw101", "rw135"]

# Reference: https://wiki.gentoo.org/wiki/Repository_format/package/Manifest
EBUILD_MANIFEST_FILE = "Manifest"
EBUILD_MANIFEST_TYPE_DIST = "DIST"
EBUILD_MANIFEST_TYPE_HASH = "SHA512"

PATCH_MANIFEST_FILE = "patch_manifest.textproto"
PATCH_TOOL_COMMAND = "/usr/bin/patchmaker"

MIRROR_PATH = "gs://chromeos-localmirror/distfiles/"


@contextlib.contextmanager
def temp_dir(keep_tmp_files):
    _temp_dir = tempfile.mkdtemp()
    try:
        yield _temp_dir
    finally:
        if not keep_tmp_files:
            logging.info("Removing temporary files at %s", _temp_dir)
            shutil.rmtree(_temp_dir)


# Properties we need for each item in the ebuild's Manifest file
ManifestEntry = collections.namedtuple(
    "ManifestEntry", ["file_name", "file_size", "file_sha512sum"]
)


def run_subprocess(args, check_retcode=True):
    try:
        return subprocess.run(args, check=check_retcode, capture_output=True)

    except subprocess.CalledProcessError as e:
        logging.error("Caught an exception running %s. STDERR:", args)
        logging.error(e.stderr.decode("utf-8"))
        raise


def check_if_inside_chroot():
    return os.environ.get("CHROOT_CWD") is not None


def parse_manifest_entries(ebuild_manifest_path):
    manifest_entries = []
    with open(ebuild_manifest_path, "r", encoding="utf-8") as mf:
        for manifest_line in mf.readlines():
            if not manifest_line:
                continue

            # See above reference for structure definition. It is as follows:
            # <type> <filename> <size> <hash-type> <hash> [<hash-type> <hash>]
            entry_fields = manifest_line.split()
            file_name = entry_fields[1]
            file_size = entry_fields[2]

            # Continue until we find SHA512 key, grab value
            for i in range(3, len(entry_fields), 2):
                if entry_fields[i] == EBUILD_MANIFEST_TYPE_HASH:
                    manifest_entries.append(
                        ManifestEntry(file_name, file_size, entry_fields[i + 1])
                    )
                    break
            else:
                raise Exception("Failed to find SHA512 for: ", manifest_line)

    return manifest_entries


def fetch_and_verify_src_uris(manifest_entries, working_dir):
    logging.info("Fetching %d URIs into %s", len(manifest_entries), working_dir)
    gs_files_to_fetch = [
        os.path.join(MIRROR_PATH, entry.file_name) for entry in manifest_entries
    ]

    # Fetch all files with one copy call for performance
    args = ["gsutil", "cp", *gs_files_to_fetch, f"{working_dir}"]
    run_subprocess(args)

    # Assert all files are identical to the ones specified in the manifest
    for entry in manifest_entries:
        with open(os.path.join(working_dir, entry.file_name), "rb") as f:
            h = hashlib.sha512()
            h.update(f.read())

            assert h.hexdigest() == entry.file_sha512sum


def open_packages_in_place(manifest_entries, working_dir):
    logging.debug("Opening src_uris in %s", working_dir)
    for entry in manifest_entries:
        archive_file = os.path.join(working_dir, entry.file_name)

        args = ["tar", "-xf", f"{archive_file}", "-C", f"{working_dir}"]
        run_subprocess(args)

        # Remove the archives
        os.remove(os.path.join(working_dir, entry.file_name))

    # For patch creation to be effective, we also need to open any L850 firmware
    # files that were previously xz-compressed.
    for filename in glob.glob(f"{working_dir}/**/*.fls3.xz", recursive=True):
        run_subprocess(["unxz", f"{filename}"])


def combine_packages_to_host_structure(working_dir):
    unprocessed_directories = {
        os.path.normpath(f.path) for f in os.scandir(working_dir) if f.is_dir()
    }

    # Gather firmware files and manifest
    for modem_name in MODEM_LIST:
        glob_str = f"cellular-firmware-*-{modem_name}-*"
        package_dirs = glob.glob(os.path.join(working_dir, glob_str))

        modem_dir = os.path.join(working_dir, modem_name)
        if package_dirs and not os.path.exists(modem_dir):
            os.makedirs(modem_dir)

        # Move package contents into the modem dir, and remove the package
        for package_dir in package_dirs:
            for p in os.listdir(package_dir):
                shutil.move(os.path.join(package_dir, p), modem_dir)

            unprocessed_directories.remove(os.path.normpath(package_dir))
            os.rmdir(package_dir)

    # ensure every directory was processed via our glob handling
    assert len(unprocessed_directories) == 0

    logging.debug(
        "Packages combined into modem directories: %s",
        [os.path.basename(f.path) for f in os.scandir(working_dir)],
    )


def generate_patch_manifest(src_dir, dest_dir):
    logging.info("Patching files from %s into %s", src_dir, dest_dir)

    args = [
        PATCH_TOOL_COMMAND,
        "--encode",
        f"--src_path={src_dir}",
        f"--dest_path={dest_dir}",
    ]
    run_subprocess(args)


def parse_arguments(argv):
    """Parses command line arguments.

    Args:
        argv: List of commandline arguments.

    Returns:
        Namespace object containing parsed arguments.
    """

    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter
    )

    parser.add_argument(
        "manifest_path",
        help="The path to the Manifest file for the firmware package.",
    )

    parser.add_argument(
        "output_patch_path",
        help="The path the new patch manifest should be written to.",
    )

    parser.add_argument(
        "--keep-files",
        default=False,
        action="store_true",
        help="Don't delete the working directories in /tmp.",
    )

    return parser.parse_args(argv[1:])


def main(argv):
    logging.basicConfig(level=logging.DEBUG)
    opts = parse_arguments(argv)

    # For convenience, we have the caller pass the Manifest file as argument.
    # This will be our source-of-truth for the SRC_URIs needed, as well as their
    # hashes to be confident we are pulling the exact version of the file that
    # the ebuild will be using
    if (
        not os.path.isfile(opts.manifest_path)
        or not os.path.basename(opts.manifest_path) == EBUILD_MANIFEST_FILE
    ):
        logging.error("Expected a valid Manifest file")
        sys.exit(1)

    if not check_if_inside_chroot():
        logging.error("Expected to be run inside chroot")
        sys.exit(1)

    print(f"Write patch manifest to {opts.output_patch_path}? Y/n")
    if input().lower() != "y":
        print("Won't write this file without confirmation")
        sys.exit(1)

    parsed_manifest_entries = parse_manifest_entries(opts.manifest_path)

    with temp_dir(opts.keep_files) as patch_dir, temp_dir(
        opts.keep_files
    ) as src_dir:
        # Fetch, validate, and combine packages into src_dir
        fetch_and_verify_src_uris(parsed_manifest_entries, src_dir)
        open_packages_in_place(parsed_manifest_entries, src_dir)
        combine_packages_to_host_structure(src_dir)

        # Generate file patches into patch_dir
        generate_patch_manifest(src_dir, patch_dir)

        # Copy generated patch_manifest.textproto file into output_patch_path
        new_manifest = os.path.join(patch_dir, PATCH_MANIFEST_FILE)
        shutil.copy(new_manifest, opts.output_patch_path)


if __name__ == "__main__":
    main(sys.argv)
