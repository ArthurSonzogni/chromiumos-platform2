# Supported Probe Functions

Runtime Probe supports probing components and their attributes with probe
functions.

The implementation of probe functions can be found in
[function\_templates/][function-templates-dir] and [functions/][functions-dir].
Available attributes and their definition for each component could be found in
[runtime\_probe.proto].

Note that the probe function set is different from [factory probe functions].

If OEMs/ODMs have any trouble on this, please [contact us][team-contact].

[TOC]

## AP I2C

### `ap_i2c`

Read data from a register on an AP I2C component.

Arguments:

*   `i2c_bus` (int): The bus of the I2C connected to AP.
*   `chip_addr` (int): The I2C address.
*   `data_addr` (int): The register offset.

Result attributes:

*   `data` (uint32): The value in the register.

## Audio Codec

### `audio_codec`

TBD

## Battery

### `generic_battery`

Probe battery components.

TBD

## Camera

### `generic_camera`

Probe generic camera components.

TBD

### `mipi_camera`

Probe MIPI camera components.

TBD

### `usb_camera`

Probe USB camera components.

TBD

## CPU

### `cpu`

Probe CPU components.

TBD

## EC I2C

### `ec_i2c`

Read data from a register on an EC I2C component.

Arguments:

*   `i2c_bus` (int): The bus of the I2C connected to EC.
*   `chip_addr` (int): The I2C address.
*   `data_addr` (int): The register offset.
*   `size` (int, optional): The data size in bits. Default: 8.

Result attributes:

*   `data` (uint32): The value in the register.

## EDID

### `edid`

Probe display panel components with their EDID (Extended Display Identification Data).

TBD

## Input Device

### `input_device`

Probe input devices such as stylus, touchpad, and touchscreen.

Arguments:

*   `device_type` (string, optional): Input device type: stylus, touchpad, touchscreen.

Result attributes:

*   `name` (string): Device name.
*   `path` (string): Pathname of the sysfs entry of the device.
*   `event` (string): Event of the device.
*   `bus` (uint32): Bus number. 16 bits.
*   `vendor` (uint32): Vendor ID. 16 bits.
*   `product` (uint32): Product ID. 16 bits.
*   `version` (uint32): Version number. 16 bits.
*   `fw_version` (string): Firmware version.
*   `device_type` (enum InputDevice_Type): Device type: stylus, touchpad, touchscreen, unknown.

## Memory

### `dram`

Probe DRAM memories. Only support AMD64 platforms.

TBD

## Network

### `generic_network`

Probe generic network components.

TBD

### `cellular_network`

Probe cellular network components.

TBD

### `ethernet_network`

Probe non-removable ethernet cellular components.

TBD

### `wireless_network`

Probe wireless cellular components.

TBD

## Storage

### `generic_storage`

Probe non-removable generic storage components.

TBD

### `ata_storage`

Probe non-removable ATA storage components.

TBD

### `mmc_storage`

Probe non-removable eMMC storage components.

TBD

### `nvme_storage`

Probe non-removable NVMe storage components.

TBD

### `ufs_storage`

Probe non-removable UFS storage components.

TBD

## TCPC

### `tcpc`

Probe TCPC components.

Result attributes:

*   `port` (uint32): The USB-C port number. 8 bits.
*   `vendor_id` (uint32): Vendor ID. 16 bits.
*   `product_id` (uint32): Product ID. 16 bits.
*   `device_id` (uint32): Device ID. 16 bits.

## TPM

### `tpm`

Probe TPM components.

TBD

[function-templates-dir]: ../function_templates/
[functions-dir]: ../functions/
[runtime\_probe.proto]: ../../system_api/dbus/runtime_probe/runtime_probe.proto
[factory probe functions]: https://chromium.googlesource.com/chromiumos/platform/factory/+/refs/heads/main/py/probe/functions/
[team-contact]: mailto:chromeos-runtime-probe@google.com
