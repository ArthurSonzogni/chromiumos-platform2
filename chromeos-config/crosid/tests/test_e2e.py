# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import subprocess
import struct

import pytest

import cros_config_host.configfs


def make_fake_sysroot(
    path,
    smbios_name=None,
    smbios_sku=None,
    fdt_compatible=None,
    fdt_sku=None,
    vpd_values=None,
    configs=(),
):
    smbios_sysfs_path = path / "sys" / "class" / "dmi" / "id"
    if smbios_name is not None:
        smbios_sysfs_path.mkdir(exist_ok=True, parents=True)
        (smbios_sysfs_path / "product_name").write_text("{}\n".format(smbios_name))

    if smbios_sku is not None:
        smbios_sysfs_path.mkdir(exist_ok=True, parents=True)
        (smbios_sysfs_path / "product_sku").write_text("sku{}\n".format(smbios_sku))

    proc_fdt_path = path / "proc" / "device-tree"
    if fdt_compatible is not None:
        proc_fdt_path.mkdir(exist_ok=True, parents=True)
        contents = "".join("{}\0".format(compat) for compat in fdt_compatible)
        (proc_fdt_path / "compatible").write_text(contents)

    proc_fdt_coreboot_path = proc_fdt_path / "firmware" / "coreboot"
    if fdt_sku is not None:
        proc_fdt_coreboot_path.mkdir(exist_ok=True, parents=True)
        contents = fdt_sku.to_bytes(4, byteorder="big")
        (proc_fdt_coreboot_path / "sku-id").write_bytes(contents)

    if vpd_values:
        vpd_sysfs_path = path / "sys" / "firmware" / "vpd" / "ro"
        vpd_sysfs_path.mkdir(exist_ok=True, parents=True)
        for name, value in vpd_values.items():
            (vpd_sysfs_path / name).write_text(value)

    configs_full = {"chromeos": {"configs": [{"identity": c} for c in configs]}}
    config_path = path / "usr" / "share" / "chromeos-config"
    config_path.mkdir(exist_ok=True, parents=True)
    with open(config_path / "identity.bin", "wb") as output_file:
        cros_config_host.configfs.WriteIdentityStruct(configs_full, output_file)


REEF_CONFIGS = [
    {
        "smbios-name-match": "Reef",
        "sku-id": 8,
        "customization-id": "PARMA-ELECTRO",
    },
    {
        "smbios-name-match": "Reef",
        "sku-id": 0,
        "customization-id": "OEM2-BASKING",
    },
    {
        "smbios-name-match": "Pyro",
        "customization-id": "NEWTON2-PYRO",
    },
    {
        "smbios-name-match": "Sand",
        "customization-id": "ACER-SAND",
    },
    {
        "smbios-name-match": "Snappy",
        "sku-id": 7,
        "customization-id": "ALAN-DOLPHIN",
    },
    {
        "smbios-name-match": "Snappy",
        "sku-id": 2,
        "customization-id": "BENTLEY-BIGDADDY",
    },
    {
        "smbios-name-match": "Snappy",
        "sku-id": 5,
        "customization-id": "BENTLEY-BIGDADDY",
    },
    {
        "smbios-name-match": "Snappy",
        "sku-id": 8,
        "customization-id": "MORGAN-SNAPPY",
    },
    {
        "smbios-name-match": "Snappy",
    },
]


@pytest.mark.parametrize("config_idx", list(range(len(REEF_CONFIGS))))
def test_reef(tmp_path, executable_path, config_idx):
    cfg = REEF_CONFIGS[config_idx]
    vpd = {}

    customization_id = cfg.get("customization-id")
    if customization_id:
        vpd["customization_id"] = customization_id

    make_fake_sysroot(
        tmp_path,
        smbios_name=cfg["smbios-name-match"],
        smbios_sku=cfg.get("sku-id"),
        vpd_values=vpd,
        configs=REEF_CONFIGS,
    )

    result = subprocess.run(
        [executable_path, "--sysroot", tmp_path, "-v"],
        check=True,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )

    assert result.stdout == "SKU={}\nCONFIG_INDEX={}\n".format(
        cfg.get("sku-id", "none"), config_idx
    )


def test_no_match(tmp_path, executable_path):
    # Test the case that no configs match (e.g., running wrong image
    # on device)
    make_fake_sysroot(
        tmp_path,
        smbios_name="Samus",
        configs=REEF_CONFIGS,
    )

    result = subprocess.run(
        [executable_path, "--sysroot", tmp_path, "-v"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )

    assert result.returncode != 0
    assert result.stdout == "SKU=none\nCONFIG_INDEX=unknown\n"


def test_both_customization_id_and_whitelabel(tmp_path, executable_path):
    # Having both a customization_id and whitelabel_tag indicates the
    # RO VPD was tampered/corrupted, and should result in errors.
    make_fake_sysroot(
        tmp_path,
        smbios_name="Sand",
        vpd_values={
            "customization_id": "ACER-SAND",
            "whitelabel_tag": "some_wl",
        },
        configs=REEF_CONFIGS,
    )

    result = subprocess.run(
        [executable_path, "--sysroot", tmp_path, "-v"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )

    assert result.returncode != 0
    assert result.stdout == ""


TROGDOR_CONFIGS = [
    {
        "device-tree-compatible-match": "google,trogdor",
    },
    {
        "device-tree-compatible-match": "google,lazor",
        "sku-id": 0,
    },
    {
        "device-tree-compatible-match": "google,lazor",
        "sku-id": 1,
    },
    {
        "device-tree-compatible-match": "google,lazor",
        "sku-id": 2,
    },
    {
        "device-tree-compatible-match": "google,lazor",
        "sku-id": 4,
    },
    {
        "device-tree-compatible-match": "google,lazor",
        "sku-id": 5,
        "whitelabel-tag": "",
    },
    {
        "device-tree-compatible-match": "google,lazor",
        "sku-id": 6,
        "whitelabel-tag": "lazorwl",
    },
    {
        "device-tree-compatible-match": "google,lazor",
        "sku-id": 6,
        "whitelabel-tag": "",
    },
    {
        "device-tree-compatible-match": "google,lazor",
    },
]


@pytest.mark.parametrize("config_idx", list(range(len(TROGDOR_CONFIGS))))
def test_trogdor(tmp_path, executable_path, config_idx):
    cfg = TROGDOR_CONFIGS[config_idx]

    vpd = {}
    whitelabel_tag = cfg.get("whitelabel-tag")
    if whitelabel_tag:
        vpd["whitelabel_tag"] = whitelabel_tag

    make_fake_sysroot(
        tmp_path,
        fdt_compatible=[
            "google,snapdragon",
            "google,sc7180",
            cfg["device-tree-compatible-match"],
            "google,chromebook",
        ],
        fdt_sku=cfg.get("sku-id"),
        vpd_values=vpd,
        configs=TROGDOR_CONFIGS,
    )

    result = subprocess.run(
        [executable_path, "--sysroot", tmp_path, "-v"],
        check=True,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )

    assert result.stdout == "SKU={}\nCONFIG_INDEX={}\n".format(
        cfg.get("sku-id", "none"), config_idx
    )


def test_fdt_compatible_missing(tmp_path, executable_path):
    # When /proc/device-tree/compatible is not present on ARM, that
    # should be an error.
    make_fake_sysroot(
        tmp_path,
        configs=TROGDOR_CONFIGS,
    )

    result = subprocess.run(
        [executable_path, "--sysroot", tmp_path, "-v"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )

    assert result.returncode != 0
    assert result.stdout == "SKU=none\nCONFIG_INDEX=unknown\n"


def test_missing_identity_table(tmp_path, executable_path):
    # When identity.bin is missing, crosid should exit with an error.
    result = subprocess.run(
        [executable_path, "--sysroot", tmp_path],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )

    assert result.returncode != 0
    assert result.stdout == "SKU=none\nCONFIG_INDEX=unknown\n"


@pytest.mark.parametrize(
    "contents",
    [
        b"",  # too small for header
        struct.pack("<LLL4x", 42, 0, 0),  # bad version
        struct.pack("<LLL4x", 0, 0, 1),  # too small for entries
    ],
)
def test_corrupted_identity_table(tmp_path, executable_path, contents):
    # When identity.bin is corrupted, crosid should exit with an error.
    identity_file = tmp_path / "usr" / "share" / "chromeos-config" / "identity.bin"
    identity_file.parent.mkdir(exist_ok=True, parents=True)
    identity_file.write_bytes(contents)

    result = subprocess.run(
        [executable_path, "--sysroot", tmp_path],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )

    assert result.returncode != 0
    assert result.stdout == "SKU=none\nCONFIG_INDEX=unknown\n"


@pytest.mark.parametrize(
    "contents",
    [
        "",
        "\n",
        "sku\n",
        "sku-\n",
        "sku8z\n",
        "8\n",
        "SKU8\n",
    ],
)
def test_corrupted_sku_x86(tmp_path, executable_path, contents):
    # Test with a corrupted SKU file that we won't match a specific
    # SKU.
    make_fake_sysroot(
        tmp_path,
        smbios_name="Snappy",
        vpd_values={
            "customization_id": "MORGAN-SNAPPY",
        },
        configs=REEF_CONFIGS,
    )

    sku_file = tmp_path / "sys" / "class" / "dmi" / "id" / "product_sku"
    sku_file.write_text(contents)

    result = subprocess.run(
        [executable_path, "--sysroot", tmp_path],
        check=True,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )

    assert result.stdout == "SKU=none\nCONFIG_INDEX=8\n"


@pytest.mark.parametrize(
    "contents",
    [
        b"",
        b"\x00",
        b"\x00\x00\x00\x00\x00",
    ],
)
def test_corrupted_sku_arm(tmp_path, executable_path, contents):
    # Test with a corrupted SKU file that we won't match a specific
    # SKU.
    make_fake_sysroot(
        tmp_path,
        fdt_compatible=["google,lazor"],
        fdt_sku=0,
        configs=TROGDOR_CONFIGS,
    )

    sku_file = tmp_path / "proc" / "device-tree" / "firmware" / "coreboot" / "sku-id"
    sku_file.write_bytes(contents)

    result = subprocess.run(
        [executable_path, "--sysroot", tmp_path],
        check=True,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )

    assert result.stdout == "SKU=none\nCONFIG_INDEX=8\n"
