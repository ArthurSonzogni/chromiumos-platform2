// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use dbus::arg::PropMap;
use dbus::blocking::Connection;
use dbus_crossroads::Crossroads;
use log::error;

// D-Bus constants.
const MINI_UDISKS_INTERFACE_BLOCK: &str = "org.freedesktop.UDisks2.Block";
const MINI_UDISKS_INTERFACE_FILESYSTEM: &str = "org.freedesktop.UDisks2.Filesystem";
const MINI_UDISKS_INTERFACE_MANAGER: &str = "org.freedesktop.UDisks2.Manager";
const MINI_UDISKS_INTERFACE_PARTITION: &str = "org.freedesktop.UDisks2.Partition";
const MINI_UDISKS_PATH: &str = "/org/freedesktop/UDisks2";
const MINI_UDISKS_NAME: &str = "org.freedesktop.UDisks2";

/// Mount path for the fake ESP. Note that this is the path within
/// fwupd's minijail sandbox; it is a bind mount of
/// `/mnt/stateful_partition/unencrypted/uefi_capsule_updates`.
const ESP_MOUNT_POINT: &str = "/run/uefi_capsule_updates";

/// Arbitrary name for the ESP disk device.
const FAKE_DISK_NAME: &str = "fakedisk";

/// Arbitrary ESP partition number. This happens to match the real ESP
/// partition number, but the value doesn't matter.
const ESP_PARTITION_NUM: u32 = 12;

/// Partition type GUID that identifies the ESP.
///
/// Defined in:
/// https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html
const ESP_TYPE_GUID: &'static str = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";

/// Unique ID for the ESP.
///
/// This must be a valid GUID, but the value is otherwise arbitrary.
const ESP_GUID: &'static str = "99cc6f39-2fd1-4d85-b15a-543e7b023a1f";

/// Filesystem type for the ESP.
///
/// As of <https://github.com/fwupd/fwupd/pull/7299>, fwupd filters out
/// everything but vfat filesystems. This does not accurately reflect
/// the actual filesystem type we're writing to, but that's fine because
/// the value is only used for filtering.
const ESP_FS_TYPE: &'static str = "vfat";

// Create a minimal UDisks2 D-Bus API. This only exposes one block
// device, the a fake ESP partition. Only parts of the UDisks2 API that
// are required by fwupd are exposed.
fn create_dbus_api() -> Result<Crossroads> {
    let mut cr = Crossroads::new();

    // D-Bus path for the fake ESP block device:
    // "/org/freedesktop/UDisks2/block_devices/fakedisk12".
    let dbus_esp_block_device_path = dbus::Path::new(format!(
        "{MINI_UDISKS_PATH}/block_devices/{FAKE_DISK_NAME}{ESP_PARTITION_NUM}",
    ))
    .expect("invalid dbus path");

    // Manager interface.
    let dbus_esp_block_device_path_clone = dbus_esp_block_device_path.clone();
    let manager_interface = cr.register(MINI_UDISKS_INTERFACE_MANAGER, move |b| {
        b.method(
            "GetBlockDevices",
            ("options",),
            ("block_objects",),
            move |_, _, (_,): (PropMap,)| {
                let dbus_path = dbus_esp_block_device_path_clone.clone();
                let devices = vec![dbus_path];

                Ok((devices,))
            },
        );
    });

    // Block interface.
    let block_interface = cr.register(MINI_UDISKS_INTERFACE_BLOCK, |b| {
        b.property("Device")
            .get(|_, _| Ok(format!("/dev/{FAKE_DISK_NAME}{ESP_PARTITION_NUM}")));
        b.property("IdType").get(|_, _| Ok(ESP_FS_TYPE.to_string()));
    });

    // Partition interface.
    let partition_interface = cr.register(MINI_UDISKS_INTERFACE_PARTITION, |b| {
        b.property("Number").get(|_, _| Ok(ESP_PARTITION_NUM));
        b.property("Type").get(|_, _| Ok(ESP_TYPE_GUID.to_string()));
        b.property("UUID").get(|_, _| Ok(ESP_GUID.to_string()));
    });

    // Filesystem interface.
    let filesystem_interface = cr.register(MINI_UDISKS_INTERFACE_FILESYSTEM, |b| {
        b.property("MountPoints")
            .get(|_, _| -> Result<Vec<Vec<u8>>, _> {
                let mut mount_point = ESP_MOUNT_POINT.as_bytes().to_vec();
                // Add required null terminator.
                mount_point.push(0);

                Ok(vec![mount_point])
            });
    });

    cr.insert(
        format!("{MINI_UDISKS_PATH}/Manager"),
        &[manager_interface],
        (),
    );

    cr.insert(
        dbus_esp_block_device_path,
        &[block_interface, partition_interface, filesystem_interface],
        (),
    );

    Ok(cr)
}

fn run_daemon(api: Crossroads) -> Result<()> {
    let conn = Connection::new_system()?;
    conn.request_name(
        MINI_UDISKS_NAME,
        /*allow_replacement=*/ false,
        /*replace_existing=*/ true,
        /*do_not_queue=*/ false,
    )?;

    api.serve(&conn)?;
    unreachable!();
}

fn main() -> Result<()> {
    libchromeos::panic_handler::install_memfd_handler();

    libchromeos::syslog::init("mini-udisks".to_string(), /*log_to_stderr=*/ true)
        .expect("failed to initialize logger");

    let api = match create_dbus_api() {
        Ok(api) => api,
        Err(err) => {
            error!("failed to create dbus api: {err}");
            return Err(err);
        }
    };

    if let Err(err) = run_daemon(api) {
        error!("daemon error: {err}");
        Err(err)
    } else {
        Ok(())
    }
}
