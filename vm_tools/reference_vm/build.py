#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Build a VM disk image for Bruschetta.

This script builds a disk imagethat can boot as a guest on ChromeOS with VM
integrations.
"""

import argparse
import contextlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
from typing import Dict, List, Optional, Tuple

# pylint: disable=import-error
import jinja2
import requests
import yaml


SCRIPT_PATH = pathlib.Path(__file__).parent
DISK_CONFIG_TEMPLATE = SCRIPT_PATH / "disk_config.tpl"
SETUP_SCRIPT = SCRIPT_PATH / "setup.sh"
DATA_PATH = SCRIPT_PATH / "data"
LVM_VG_NAME = "refvm"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "-o",
        "--out",
        default="refvm.img",
        help="output file (default: %(default)s)",
    )
    ap.add_argument(
        "-s",
        "--size",
        default=10,
        type=int,
        help="image size in GiB (default: %(default)s)",
    )
    ap.add_argument("--cache-dir", help="directory for debootstrap caches")
    ap.add_argument(
        "--cros-version",
        type=int,
        help="install VM tools for this CrOS version",
    )
    ap.add_argument(
        "--cros-tools",
        default="release",
        choices=["release", "staging"],
        help="source of VM tools (default: %(default)s)",
    )
    ap.add_argument(
        "--debian-release",
        default="trixie",
        help="OS version to be installed (default: %(default)s)",
    )
    ap.add_argument(
        "--vg-name",
        default="refvm",
        help="name of LVM VG in installed OS (default: %(default)s)",
    )
    ap.add_argument(
        "-u",
        "--update-config-only",
        action="store_true",
        help=(
            "update an existing image's configuration instead of "
            "creating a new one"
        ),
    )
    args = ap.parse_args()

    cache_dir = pathlib.Path(args.cache_dir) if args.cache_dir else None

    cros_bucket_name = {
        "release": "cros-packages",
        "staging": "cros-packages-staging",
    }[args.cros_tools]
    cros_version = args.cros_version or get_latest_cros_version(
        cros_bucket_name
    )
    cros_packages_url = (
        f"https://storage.googleapis.com/{cros_bucket_name}/{cros_version}/"
    )

    image_path = pathlib.Path(args.out)
    image_size = args.size * 1024**3

    if args.update_config_only and not image_path.exists():
        print(
            f"Warning: Image file '{image_path}' does not exist. "
            "Falling back to full build."
        )
        args.update_config_only = False

    if image_path.exists() and not args.update_config_only:
        print(f"Error: Output file '{image_path}' already exists.")
        print(
            "Please delete the existing file or specify a different "
            "output name with -o, or use --update-config-only."
        )
        sys.exit(1)

    image_mode = "ab" if args.update_config_only else "wb"
    with (
        tempfile.TemporaryDirectory() as temp_dir_name,
        open(image_path, image_mode) as image,
    ):
        temp_dir = pathlib.Path(temp_dir_name)
        if not args.update_config_only:
            image.seek(image_size)
            image.truncate()
            image.seek(0)
        with setup_loop(
            loop_file=image_path,
            vg_name=args.vg_name,
            update=args.update_config_only,
        ) as loop:
            disk_vars, fstab = setup_storage(
                temp_dir=temp_dir,
                target_device=loop,
                vg_name=args.vg_name,
                update=args.update_config_only,
            )

            target = temp_dir / "target"
            with mount_target(target, disk_vars):
                if not args.update_config_only:
                    run_debootstrap(
                        args.debian_release, target, cache_dir=cache_dir
                    )
                    with open(target / "etc/fstab", "w", encoding="utf-8") as f:
                        print("Writing fstab")
                        f.write(fstab)

                with prepare_chroot(target):
                    shutil.copytree(DATA_PATH, target / "tmp/data")
                    shutil.copy(SETUP_SCRIPT, target / "tmp/setup.sh")
                    os.chmod(target / "tmp/setup.sh", 0o755)
                    run_in_chroot(
                        target,
                        ["/tmp/setup.sh"],
                        env={
                            "CROS_PACKAGES_URL": cros_packages_url,
                            "RELEASE": args.debian_release,
                            "UPDATE": "1" if args.update_config_only else "0",
                        },
                    )

                    print("Install complete, syncing")
                    subprocess.run(["sync"], check=True)

    print(f"Built image '{image_path}' ({os.path.getsize(image_path)} bytes)")


def get_latest_cros_version(bucket: str) -> int:
    res = requests.get(
        f"https://storage.googleapis.com/storage/v1/b/{bucket}/o"
        "?delimiter=/&matchGlob=**/"
    )
    res.raise_for_status()
    # The returned prefixes include a trailing /, remove it.
    prefixes = [p[:-1] for p in res.json()["prefixes"]]
    # And find the latest version among valid version numbers.
    version = max(int(p) for p in prefixes if p.isnumeric())
    print(f"Detected latest CrOS version for {bucket} is {version}")
    return version


@contextlib.contextmanager
def setup_loop(loop_file: pathlib.Path, vg_name: str, update: bool = False):
    # -P tells the kernel to scan the partition table and
    # create /dev/loopXpY device nodes for each partition.
    res = subprocess.run(
        ["losetup", "--show", "-f", "-P", loop_file],
        capture_output=True,
        check=True,
        text=True,
    )

    loop_device = pathlib.Path(res.stdout.strip())

    try:
        if update:
            # Re-activate the VG if it already exists.
            subprocess.run(["vgchange", "-ay", vg_name], check=True)
        yield loop_device
    finally:
        if vg_name in list_volume_groups():
            subprocess.run(["vgchange", "-an", vg_name], check=True)
        subprocess.run(["losetup", "-d", loop_device], check=True)


def setup_storage(
    temp_dir: pathlib.Path,
    target_device: pathlib.Path,
    vg_name: str,
    update: bool = False,
) -> Tuple[Dict[str, str], str]:
    jinja_env = jinja2.Environment(
        loader=jinja2.FileSystemLoader("/"),
        autoescape=False,
        keep_trailing_newline=True,
    )
    template = jinja_env.get_template(str(DISK_CONFIG_TEMPLATE))
    disk_config = template.render(vg_name=vg_name)

    ignored_vgs = [
        vg for vg in list_volume_groups() if not vg.startswith(vg_name)
    ]

    setup_storage_args = [
        "setup-storage",
        "-X",
        "-y",
        "-L",
        str(temp_dir),
        "-f",
        "-",
        "-D",
        str(target_device.relative_to(pathlib.Path("/dev"))),
    ]
    if update:
        disk_vars = {
            "ROOT_PARTITION": f"/dev/{vg_name}/root",
            "BOOT_PARTITION": f"{target_device}p2",
            "ESP_DEVICE": f"{target_device}p1",
        }
        return (disk_vars, "")

    subprocess.run(
        setup_storage_args,
        check=True,
        env={"SS_IGNORE_VG": " ".join(ignored_vgs), **os.environ},
        input=disk_config,
        text=True,
    )

    with open(temp_dir / "disk_var.yml", encoding="utf-8") as f:
        disk_vars = yaml.load(f, Loader=yaml.SafeLoader)
    with open(temp_dir / "fstab", encoding="utf-8") as f:
        fstab = f.read()

    return (disk_vars, fstab)


def list_volume_groups() -> List[str]:
    res = subprocess.run(
        ["vgs", "--reportformat=json"],
        capture_output=True,
        check=True,
        text=True,
    )
    data = json.loads(res.stdout)
    vg_names = []
    for report in data.get("report", []):
        for vg in report.get("vg", []):
            vg_names.append(vg["vg_name"])

    return vg_names


@contextlib.contextmanager
def mount_target(target: pathlib.Path, disk_vars: Dict[str, str]):
    try:
        location = target
        location.mkdir(exist_ok=True)
        subprocess.run(
            ["mount", disk_vars["ROOT_PARTITION"], location], check=True
        )

        location = target / "boot"
        location.mkdir(exist_ok=True)
        subprocess.run(
            ["mount", disk_vars["BOOT_PARTITION"], location], check=True
        )

        location = target / "boot/efi"
        location.mkdir(parents=True, exist_ok=True)
        subprocess.run(["mount", disk_vars["ESP_DEVICE"], location], check=True)

        yield
    finally:
        subprocess.run(["umount", "-R", target], check=True)


@contextlib.contextmanager
def prepare_chroot(target: pathlib.Path):
    mounts = []
    try:
        mountpoint = target / "dev"
        subprocess.run(["mount", "--bind", "/dev", mountpoint], check=True)
        mounts.append(mountpoint)

        mountpoint = target / "dev/pts"
        subprocess.run(["mount", "--bind", "/dev/pts", mountpoint], check=True)
        mounts.append(mountpoint)

        mountpoint = target / "sys"
        subprocess.run(
            ["mount", "-t", "sysfs", "sysfs", mountpoint], check=True
        )
        mounts.append(mountpoint)

        mountpoint = target / "proc"
        subprocess.run(["mount", "-t", "proc", "proc", mountpoint], check=True)
        mounts.append(mountpoint)

        mountpoint = target / "tmp"
        subprocess.run(
            ["mount", "-t", "tmpfs", "tmpfs", mountpoint], check=True
        )
        mounts.append(mountpoint)

        mountpoint = target / "run"
        subprocess.run(
            ["mount", "-t", "tmpfs", "tmpfs", mountpoint], check=True
        )
        mounts.append(mountpoint)

        yield
    finally:
        for mountpoint in mounts[::-1]:
            subprocess.run(["umount", mountpoint], check=True)


def run_debootstrap(
    suite: str, target: pathlib.Path, cache_dir: Optional[pathlib.Path] = None
):
    cache_args = []
    if cache_dir:
        if not cache_dir.exists():
            cache_dir.mkdir()
        cache_args += ["--cache-dir", cache_dir]
    # Run with eatmydata to improve build time.
    subprocess.run(
        [
            "eatmydata",
            "debootstrap",
            "--include=eatmydata",
            "--no-merged-usr",
            *cache_args,
            suite,
            target,
        ],
        check=True,
    )


def run_in_chroot(
    target: pathlib.Path,
    command: List[str],
    env: Optional[Dict[str, str]] = None,
):
    env = {**os.environ, "LANG": "C.UTF-8", **(env if env else {})}
    # Run with eatmydata to improve build time.
    subprocess.run(
        ["eatmydata", "chroot", target, *command], check=True, env=env
    )


if __name__ == "__main__":
    main()
