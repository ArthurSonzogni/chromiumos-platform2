#!/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import tempfile

import guestfs


def convert_raw_to_subvolume_inplace(raw_image_path: str, subvolume_name: str):
    """
    Converts a raw disk image with a Btrfs rootfs to use a subvolume in-place.

    Args:
        raw_image_path: Path to the raw disk image.
    """

    g = guestfs.GuestFS(python_return_dict=True)

    try:
        # Add the raw image as a disk
        g.add_drive_opts(raw_image_path, format="raw", readonly=0)

        # Launch the guestfs instance
        g.launch()

        # Get the root partition
        devices = g.list_devices()
        if len(devices) != 1:
            raise RuntimeError("device count incorrect!")
        root_partition = devices[0]

        # Mount the root partition
        root_mount_point = "/"
        g.mount(root_partition, root_mount_point)

        # Check if the rootfs is already in a subvolume
        subvolumes = g.btrfs_subvolume_list(root_mount_point)
        if subvolumes:
            raise RuntimeError("Subvolume already exists!")

        entries = g.ls(root_mount_point)

        new_root_subvolume = "rootfs_subvol"

        if new_root_subvolume in entries:
            raise RuntimeError(f"{new_root_subvolume} folder already exists!")

        # Create a new subvolume for the rootfs
        g.btrfs_subvolume_create(f"{root_mount_point}/{new_root_subvolume}")

        # Move the remaining files and directories to the new subvolume
        for entry in entries:
            g.mv(
                os.path.join(root_mount_point, entry),
                os.path.join(root_mount_point, new_root_subvolume),
            )

        # Get the subvolume ID for setting the default and for fstab
        subvol_path = f"{root_mount_point}/{new_root_subvolume}"
        subvol_info = g.btrfs_subvolume_show(subvol_path)

        subvol_id = int(subvol_info["Subvolume ID"])

        # Set the new subvolume as the default
        g.btrfs_subvolume_set_default(subvol_id, root_mount_point)

        # Unmount the partition
        g.umount(root_mount_point)

    finally:
        g.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Convert rootfs into a default btrfs subvolume in an image"
    )

    parser.add_argument(
        "--subvol-name",
        help="Name of the default subvolume (default: rootfs_subvol))",
        default="rootfs_subvol",
    )
    parser.add_argument(
        "--compression-level",
        type=int,
        default=10,
        help="Zstd compression level (default: 10)",
    )
    parser.add_argument("source_path", help="Path to rootfs tarball")
    parser.add_argument(
        "dest_path", help="Path to the generated compressed OS disk image"
    )

    args = parser.parse_args()

    # Create a temporary file, but close it immediately
    temp_image_file = tempfile.NamedTemporaryFile(delete=False)
    temp_image_path = temp_image_file.name
    temp_image_file.close()  # Close the file so virt-make-fs can use it

    print(f"Creating temporary raw disk image at {temp_image_path}")

    # Create a Btrfs image
    subprocess.run(
        [
            "virt-make-fs",
            "-t",
            "btrfs",
            "--size=+200M",
            args.source_path,
            temp_image_path,
        ],
        check=True,
    )

    print(
        f"Converting {temp_image_path} to use btrfs subvolume {args.subvol_name} as default subvolume"
    )

    convert_raw_to_subvolume_inplace(temp_image_path, args.subvol_name)

    print(f"Compressing {temp_image_path} to {args.dest_path}")

    subprocess.run(
        [
            "zstd",
            f"-{args.compression_level}",
            "-T0",
            temp_image_path,
            "-o",
            args.dest_path,
        ],
        check=True,
    )

    print(f"Cleaning up temporary raw disk image {temp_image_path}")
    os.unlink(temp_image_path)
