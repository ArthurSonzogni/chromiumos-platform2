#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A tool to manage the fingerprint study.

# Dependencies

| Software Needed   | Gentoo/ChromiumOS-SDK Pkgs | Debian/gLinux Pkgs |
| ----------------- | -------------------------- | ------------------ |
| shred command     | sys-apps/coreutils         | coreutils          |
| mogrify command   | media-gfx/imagemagick      | imagemagick        |
| gpg command       | app-crypt/gnupg            | gnupg              |
| python gpg lib    | dev-python/python-gnupg    | python3-gnupg      |
"""

from __future__ import annotations
from __future__ import print_function

import argparse
import glob
import logging
import math
import os
import shutil
import stat
import subprocess
import sys
import tempfile
from typing import Literal

# The following imports will be available on the test image, but will usually
# be missing in the SDK.
# pylint: disable=import-error
import gnupg


class Sensor:
    """Hold the parameters for a given fingerprint sensor."""

    def __init__(
        self,
        name: str,
        width: int,
        height: int,
        bits: int,
        frame_size: int,
        frame_offset_image: int,
    ):
        self.name = name
        self.width = width
        self.height = height
        self.bits = bits
        # This is the full vendor frame size that encapsulates the captured
        # image.
        self.frame_size = frame_size
        # The odd little offset into the raw vendor frame buffer where the
        # capture image begins.
        self.frame_offset_image = frame_offset_image


SENSORS = {
    "FPC1145": Sensor(
        "FPC1145", 56, 192, 8, frame_size=35460, frame_offset_image=2340
    ),
    "FPC1025": Sensor(
        "FPC1025", 160, 160, 8, frame_size=26260, frame_offset_image=400
    ),
    "ELAN80SG": Sensor(
        "ELAN80SG", 80, 80, 14, frame_size=12800, frame_offset_image=0
    ),
}

SENSORS_TYPING = Literal["FPC1145", "FPC1025"]

OUTPUT_IMAGE_FILE_EXTS = [
    # The intermediate ASCII Image format.
    "pgm",
    # The fomllowing are created using a tool.
    "pnm",
    "png",
    "jpg",
]

OUTPUT_IMAGE_FILE_EXT_TYPING = Literal["pgm", "pnm", "png", "jpg"]

CAPTURE_FILE_EXTS = [
    "gpg",
    "raw",
    # A special FPC image.
    "fmi",
]


def find_files(path: str, ext: str) -> list:
    """Find all files that have the specified file extension.

    Args:
        path: A directory or single file path, where we will search for file(s)
            of the given |ext|.
        ext: The file extension.

    Returns:
        A list of file paths that matching |path| and |ext|.
    """

    files = []
    if os.path.isdir(path):
        files = glob.glob(path + "/**/*" + ext, recursive=True)
    elif os.path.isfile(path):
        _, path_ext = os.path.splitext(path)
        if path_ext != ext:
            raise Exception(f'The given path "{path}" is not a "{ext}" file')
        files = [path]
    else:
        raise Exception(f'The given path "{path}" is not a directory or file')

    return files


def decrypt(private_key: str, private_key_pass: str, files: list):
    """Decrypt the given file."""

    # Enable basic stdout logging for gnupg.
    h = logging.StreamHandler()
    l = logging.getLogger("gnupg")
    # Change this to logging.DEBUG to debug gnupg issues.
    l.setLevel(logging.INFO)
    l.addHandler(h)

    with tempfile.TemporaryDirectory() as gnupghome:
        os.chmod(gnupghome, stat.S_IRWXU)
        # Creating this directory makes old gnupg versions happy.
        os.makedirs(f"{gnupghome}/private-keys-v1.d", mode=stat.S_IRWXU)

        try:
            gpg = gnupg.GPG(
                gnupghome=gnupghome,
                verbose=False,
                options=[
                    "--no-options",
                    "--no-default-recipient",
                    "--trust-model",
                    "always",
                ],
            )

            with open(private_key, mode="rb") as key_file:
                key_data = key_file.read()
                if gpg.import_keys(key_data).count != 1:
                    raise Exception(f"Failed to import key {private_key}.")

            for file in files:
                file_parts = os.path.splitext(file)
                assert file_parts[1] == ".gpg"
                file_output = file_parts[0]
                print(f"Decrypting file {file} to {file_output}.")
                with open(file, mode="rb") as file_input_stream:
                    ret = gpg.decrypt_file(
                        file_input_stream,
                        always_trust=True,
                        passphrase=private_key_pass,
                        output=file_output,
                    )
                    if not ret.ok:
                        raise Exception(f"Failed to decrypt file {file}")

                    if not os.path.exists(file_output):
                        raise Exception(
                            f"Output file {file_output} was not created"
                        )
        finally:
            # Shred all remnants GPG keys in the temp directory.
            os.system(f"find {gnupghome} -type f | xargs shred -v")


def convert(
    raw_capture: bytes,
    sensor_name: SENSORS_TYPING,
    out_type: OUTPUT_IMAGE_FILE_EXT_TYPING,
) -> bytes:
    """Convert a raw fingerprint capture to another usable format."""

    if not sensor_name in SENSORS:
        raise ValueError(f"Arg sensor must be one of {SENSORS}.")

    if not out_type in OUTPUT_IMAGE_FILE_EXTS:
        raise ValueError(
            f"Arg out_type must be one of {OUTPUT_IMAGE_FILE_EXTS}."
        )

    if out_type != "pgm" and not shutil.which("mogrify"):
        # The mogrify utility can be found in the imagemagick package.
        raise RuntimeError(
            f"Conversion to {out_type} requires the mogrify utility. "
            "Please install the imagemagick package."
        )

    sensor = SENSORS[sensor_name]

    # We always build the ASCII PGM representation of the image.
    # If the user wants a PGM image, we just save it to a file.
    # If the user wants a more complex type, we feed the PGM representation
    # into mogrify and save the output image binary.

    # More information about PGM can be found at the following webpages:
    # - https://en.wikipedia.org/wiki/Netpbm#File_formats
    # - https://netpbm.sourceforge.net/doc/pgm.html
    #
    # This raw to PGM conversion can also be seen in the upload_pgm_image
    # function of ec/common/fpsensor/fpsensor.c and the cmd_fp_frame
    # function of ec/util/ectool.c. Check commit description for more info.
    pgm_buffer = ""
    if len(raw_capture) != sensor.frame_size:
        raise ValueError(
            f"Raw frame size {len(raw_capture)} != "
            f"expected size {sensor.frame_size}."
        )

    # Use magic vendor frame offset.
    raw_capture = raw_capture[sensor.frame_offset_image :]

    # Write graymap PGM ASCII header.
    pgm_buffer += "P2\n"
    pgm_buffer += f"# {sensor.name} is {sensor.width}x{sensor.height} "
    pgm_buffer += f"{sensor.bits}bpp\n"
    pgm_buffer += f"{sensor.width} {sensor.height}\n"
    # The Max Value can be any value between 0 and 65536, exclusive.
    pgm_buffer += "# Max Value:\n"
    pixel_max_value = 2**sensor.bits - 1
    pgm_buffer += f"{pixel_max_value}\n"
    # Write table of pixel values.
    pixel_bytes_count = math.ceil(sensor.bits / 8)
    for h in range(sensor.height):
        for w in range(sensor.width):
            pixel_index = pixel_bytes_count * (sensor.width * h + w)
            pixel_raw_bytes = raw_capture[
                pixel_index : pixel_index + pixel_bytes_count
            ]
            pixel_value = int.from_bytes(
                pixel_raw_bytes,
                byteorder="little",
                signed=False,
            )
            if pixel_value > pixel_max_value:
                raise ValueError(
                    f"Parsed pixel value of {pixel_value}"
                    f"(0x{pixel_raw_bytes.hex()}) at offset {pixel_index} is"
                    f"larger than max value {pixel_max_value}."
                )
            pgm_buffer += f"{pixel_value} "
        pgm_buffer += "\n"
    # Write non-essential footer.
    pgm_buffer += "# END OF FILE\n"

    if out_type == "pgm":
        return bytes(pgm_buffer, "utf-8")
    else:
        # The mogrify utility can be found in the imagemagick package.
        # mogrify -format png *.pgm
        p = subprocess.run(
            ["mogrify", "-format", out_type, "-"],
            capture_output=True,
            input=bytes(pgm_buffer, "utf-8"),
            check=False,
        )
        if p.returncode != 0:
            print("mogrify:", str(p.stderr, "utf-8"))
            raise RuntimeError(f"The mogrify utility returned {p.returncode}.")
        return bytes(p.stdout)


def cmd_decrypt(args: argparse.Namespace) -> int:
    """Decrypt all gpg encrypted fingerprint captures."""

    if not os.path.isfile(args.key):
        print(f"Error - The given key file {args.key} does not exist.")
        return 1

    try:
        files = find_files(args.path, ".gpg")
    except Exception as e:
        print(f"Error - {e}")
        return 1
    if not files:
        print("Error - The given path does not contain gpg files.")
        return 1

    if not files:
        print("Error - The given dir path does not contain encrypted files.")
        return 1

    if not shutil.which("shred"):
        print("Error - The shred utility does not exist.")
        return 1

    try:
        decrypt(args.key, args.password, files)
    except Exception as e:
        print(f"Error - {e}.")
        print(
            "Ensure that you provided or were prompted for the private "
            "key password."
        )
        return 1


def cmd_convert(args: argparse.Namespace) -> int:
    """Convert all raw samples to the specified output format."""

    try:
        files = find_files(args.path, ".raw")
    except Exception as e:
        print(f"Error - {e}.")
        return 1
    if not files:
        print("Error - The given path does not contain raw files.")
        return 1

    for infile in files:
        outfile, _ = os.path.splitext(infile)
        outfile += "." + args.outtype

        with open(infile, "rb") as fin:
            b = fin.read()
            out_bytes = convert(b, args.sensor, args.outtype)
            with open(outfile, "wb") as fout:
                fout.write(out_bytes)

    return 0


def cmd_rm(args: argparse.Namespace) -> int:
    """Recursively shred and remove files of a certain extension."""

    try:
        files = find_files(args.path, args.ext)
    except Exception as e:
        print(f"Error - {e}.")
        return 1
    if not files:
        print(f"Error - The given path does not contain {args.ext} files.")
        return 1

    if args.ext in CAPTURE_FILE_EXTS:
        print(
            f"WARNING: You are about to destroy {len(files)} original "
            f'".{args.ext}" fingerprint capture files from path '
            f'"{args.path}".'
        )
        resp = input("Confirm y/n: ")
        if not resp in ["y", "Y"]:
            print("Aborting.")
            return 0

    if not shutil.which("shred"):
        print("Error - The shred utility does not exist.")
        return 1

    files_list = "\n".join(files) + "\n"
    print(f"Shredding {len(files)} files.")
    p = subprocess.run(
        ["xargs", "shred", "-v"],
        capture_output=True,
        input=bytes(files_list, "utf-8"),
        check=False,
    )
    if p.returncode != 0:
        print("shred stdout:\n", str(p.stdout, "utf-8"))
        print("shred stderr:\n", str(p.stderr, "utf-8"))
        print(f"Error - shred returned {p.returncode}.")
        return 1

    for file in files:
        print(f"Removing {file}.")
        os.remove(file)


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    subparsers = parser.add_subparsers(
        dest="subcommand", required=True, title="subcommands"
    )

    # Parser for "decrypt" subcommand.
    parser_decrypt = subparsers.add_parser("decrypt", help=cmd_decrypt.__doc__)
    parser_decrypt.add_argument("key", help="Path to the GPG private key")
    parser_decrypt.add_argument(
        "path",
        help="Path to directory of encrypted captures "
        "or single encrypted file",
    )
    parser_decrypt.add_argument(
        "--password", default=None, help="Password for private key"
    )
    parser_decrypt.set_defaults(func=cmd_decrypt)

    # Parser for "convert" subcommand.
    parser_convert = subparsers.add_parser("convert", help=cmd_convert.__doc__)
    parser_convert.add_argument(
        "sensor",
        choices=SENSORS,
        help="The sensor that generated the raw samples",
    )
    parser_convert.add_argument(
        "outtype",
        type=str,
        choices=OUTPUT_IMAGE_FILE_EXTS,
        help="The output image type to convert to",
    )
    parser_convert.add_argument(
        "path", help="Path to directory of raw captures or single raw file"
    )
    parser_convert.set_defaults(func=cmd_convert)

    # Parser for "rm" subcommand.
    parser_rm = subparsers.add_parser("rm", help=cmd_rm.__doc__)
    parser_rm.add_argument(
        "ext",
        type=str,
        choices=OUTPUT_IMAGE_FILE_EXTS + CAPTURE_FILE_EXTS,
        help="The file extension to remove",
    )
    parser_rm.add_argument(
        "path", help="Path to directory of raw captures or single raw file"
    )
    parser_rm.set_defaults(func=cmd_rm)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
