# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

import guestfs


def convert_raw_to_subvolume_inplace(raw_image):
    """
    Converts a raw disk image with a Btrfs rootfs to use a subvolume in-place.

    Args:
        raw_image: Path to the raw disk image.
    """

    g = guestfs.GuestFS(python_return_dict=True)

    try:
        # Add the raw image as a disk
        g.add_drive_opts(raw_image, format="raw", readonly=0)

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
    import argparse

    parser = argparse.ArgumentParser(
        description="Convert btrfs rootfs into a subvolume"
    )
    parser.add_argument("raw_image", help="Path to the raw disk image.")
    args = parser.parse_args()

    convert_raw_to_subvolume_inplace(args.raw_image)
