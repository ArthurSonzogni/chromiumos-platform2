# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import shlex
import subprocess
import struct

import pytest

import cros_config_host.identity_table


def getvars(output):
    """Get a dictionary from the output of crosid."""
    result = {}
    for word in shlex.split(output):
        key, _, value = word.partition("=")
        result[key] = value
    return result


def make_config(
    model_name,
    smbios_name_match=None,
    sku_id=None,
    fdt_match=None,
    customization_id=None,
    whitelabel_tag=None,
):
    identity = {}
    if smbios_name_match is not None:
        identity["smbios-name-match"] = smbios_name_match
    if sku_id is not None:
        identity["sku-id"] = sku_id
    if fdt_match is not None:
        identity["device-tree-compatible-match"] = fdt_match
    if customization_id is not None:
        identity["customization-id"] = customization_id
    if whitelabel_tag is not None:
        identity["whitelabel-tag"] = whitelabel_tag
    return {
        "name": model_name,
        "identity": identity,
    }


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

    configs_full = {"chromeos": {"configs": configs}}
    config_path = path / "usr" / "share" / "chromeos-config"
    config_path.mkdir(exist_ok=True, parents=True)
    with open(config_path / "identity.bin", "wb") as output_file:
        cros_config_host.identity_table.WriteIdentityStruct(configs_full, output_file)


REEF_CONFIGS = [
    make_config(
        "electro",
        smbios_name_match="Reef",
        sku_id=8,
        customization_id="PARMA-ELECTRO",
    ),
    make_config(
        "basking",
        smbios_name_match="Reef",
        sku_id=0,
        customization_id="OEM2-BASKING",
    ),
    make_config(
        "pyro",
        smbios_name_match="Pyro",
        customization_id="NEWTON2-PYRO",
    ),
    make_config(
        "sand",
        smbios_name_match="Sand",
        customization_id="ACER-SAND",
    ),
    make_config(
        "alan",
        smbios_name_match="Snappy",
        sku_id=7,
        customization_id="DOLPHIN-ALAN",
    ),
    make_config(
        "bigdaddy",
        smbios_name_match="Snappy",
        sku_id=2,
        customization_id="BENTLEY-BIGDADDY",
    ),
    make_config(
        "bigdaddy",
        smbios_name_match="Snappy",
        sku_id=5,
        customization_id="BENTLEY-BIGDADDY",
    ),
    make_config(
        "snappy",
        smbios_name_match="Snappy",
        sku_id=8,
        customization_id="MORGAN-SNAPPY",
    ),
    make_config(
        "snappy",
        smbios_name_match="Snappy",
    ),
]


@pytest.mark.parametrize("config_idx", list(range(len(REEF_CONFIGS))))
def test_reef(tmp_path, executable_path, config_idx):
    cfg = REEF_CONFIGS[config_idx]
    identity = cfg["identity"]
    vpd = {}

    customization_id = identity.get("customization-id")
    if customization_id:
        vpd["customization_id"] = customization_id

    make_fake_sysroot(
        tmp_path,
        smbios_name=identity["smbios-name-match"],
        smbios_sku=identity.get("sku-id"),
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

    assert getvars(result.stdout) == {
        "SKU": str(identity.get("sku-id", "none")),
        "CONFIG_INDEX": str(config_idx),
        "FIRMWARE_MANIFEST_KEY": cfg["name"],
    }


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
    assert getvars(result.stdout) == {
        "SKU": "none",
        "CONFIG_INDEX": "unknown",
        "FIRMWARE_MANIFEST_KEY": "",
    }


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
    make_config("trogdor", fdt_match="google,trogdor"),
    make_config("lazor", fdt_match="google,lazor", sku_id=0),
    make_config("lazor", fdt_match="google,lazor", sku_id=1),
    make_config("lazor", fdt_match="google,lazor", sku_id=2),
    make_config("lazor", fdt_match="google,lazor", sku_id=3),
    make_config("limozeen", fdt_match="google,lazor", sku_id=5, whitelabel_tag=""),
    make_config(
        "limozeen", fdt_match="google,lazor", sku_id=6, whitelabel_tag="lazorwl"
    ),
    make_config("limozeen", fdt_match="google,lazor", sku_id=6, whitelabel_tag=""),
    make_config("lazor", fdt_match="google,lazor"),
]


@pytest.mark.parametrize("config_idx", list(range(len(TROGDOR_CONFIGS))))
def test_trogdor(tmp_path, executable_path, config_idx):
    cfg = TROGDOR_CONFIGS[config_idx]
    identity = cfg["identity"]

    vpd = {}
    whitelabel_tag = identity.get("whitelabel-tag")
    if whitelabel_tag:
        vpd["whitelabel_tag"] = whitelabel_tag

    make_fake_sysroot(
        tmp_path,
        fdt_compatible=[
            "google,snapdragon",
            "google,sc7180",
            identity["device-tree-compatible-match"],
            "google,chromebook",
        ],
        fdt_sku=identity.get("sku-id"),
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

    assert getvars(result.stdout) == {
        "SKU": str(identity.get("sku-id", "none")),
        "CONFIG_INDEX": str(config_idx),
        "FIRMWARE_MANIFEST_KEY": cfg["name"],
    }


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
    assert getvars(result.stdout) == {
        "SKU": "none",
        "CONFIG_INDEX": "unknown",
        "FIRMWARE_MANIFEST_KEY": "",
    }


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
    assert getvars(result.stdout) == {
        "SKU": "none",
        "CONFIG_INDEX": "unknown",
        "FIRMWARE_MANIFEST_KEY": "",
    }


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
    assert getvars(result.stdout) == {
        "SKU": "none",
        "CONFIG_INDEX": "unknown",
        "FIRMWARE_MANIFEST_KEY": "",
    }


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

    assert getvars(result.stdout) == {
        "SKU": "none",
        "CONFIG_INDEX": "8",
        "FIRMWARE_MANIFEST_KEY": "snappy",
    }


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

    assert getvars(result.stdout) == {
        "SKU": "none",
        "CONFIG_INDEX": "8",
        "FIRMWARE_MANIFEST_KEY": "lazor",
    }
