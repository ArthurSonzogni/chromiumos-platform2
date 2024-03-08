// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod esp;

use anyhow::Result;
use dbus::arg::PropMap;
use dbus::blocking::Connection;
use dbus_crossroads::Crossroads;
use esp::Esp;
use log::error;

const MINI_UDISKS_INTERFACE_BLOCK: &str = "org.freedesktop.UDisks2.Block";
const MINI_UDISKS_INTERFACE_FILESYSTEM: &str = "org.freedesktop.UDisks2.Filesystem";
const MINI_UDISKS_INTERFACE_MANAGER: &str = "org.freedesktop.UDisks2.Manager";
const MINI_UDISKS_INTERFACE_PARTITION: &str = "org.freedesktop.UDisks2.Partition";
const MINI_UDISKS_PATH: &str = "/org/freedesktop/UDisks2";
const MINI_UDISKS_NAME: &str = "org.freedesktop.UDisks2";

// Create a minimal UDisks2 D-Bus API. This only exposes one block
// device, the ESP partition. Only parts of the UDisks2 API that are
// required by fwupd are exposed.
//
// If there is no ESP mount, no block devices will be exposed.
fn create_dbus_api(esp: Option<Esp>) -> Result<Crossroads> {
    let mut cr = Crossroads::new();

    // Get the D-Bus path for the ESP block device. Example:
    // "/org/freedesktop/UDisks2/block_devices/sda12".
    let dbus_esp_block_device_path = esp.as_ref().map(|esp| {
        dbus::Path::new(format!(
            "{MINI_UDISKS_PATH}/block_devices/{}",
            esp.device_name
        ))
        .expect("invalid dbus path")
    });

    // Manager interface.
    let dbus_esp_block_device_path_clone = dbus_esp_block_device_path.clone();
    let manager_interface = cr.register(MINI_UDISKS_INTERFACE_MANAGER, move |b| {
        b.method(
            "GetBlockDevices",
            ("options",),
            ("block_objects",),
            move |_, _, (_,): (PropMap,)| {
                let mut devices = Vec::new();
                if let Some(dbus_path) = dbus_esp_block_device_path_clone.clone() {
                    devices.push(dbus_path);
                }

                Ok((devices,))
            },
        );
    });

    // Block interface.
    let esp_clone = esp.clone();
    let block_interface = cr.register(MINI_UDISKS_INTERFACE_BLOCK, move |b| {
        if let Some(esp) = esp_clone {
            b.property("Device")
                .get(move |_, _| Ok(esp.device_path.clone()));
        }
    });

    // Partition interface.
    let esp_clone = esp.clone();
    let partition_interface = cr.register(MINI_UDISKS_INTERFACE_PARTITION, move |b| {
        if let Some(esp) = esp_clone {
            b.property("Number").get(move |_, _| Ok(esp.partition_num));
            b.property("Type")
                .get(|_, _| Ok(Esp::TYPE_GUID.to_string()));
            b.property("UUID")
                .get(move |_, _| Ok(esp.unique_id.clone()));
        }
    });

    // Filesystem interface.
    let esp_clone = esp.clone();
    let filesystem_interface = cr.register(MINI_UDISKS_INTERFACE_FILESYSTEM, |b| {
        b.property("MountPoints")
            .get(move |_, _| -> Result<Vec<Vec<u8>>, _> {
                let mut mount_points = Vec::new();
                if esp_clone.is_some() {
                    mount_points.push(Esp::MOUNT_POINT.as_bytes().to_vec());
                }

                Ok(mount_points)
            });
    });

    cr.insert(
        format!("{MINI_UDISKS_PATH}/Manager"),
        &[manager_interface],
        (),
    );

    if let Some(dbus_path) = dbus_esp_block_device_path {
        cr.insert(
            dbus_path,
            &[block_interface, partition_interface, filesystem_interface],
            (),
        );
    }

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

    // Get the ESP device/mount information. The ESP is mounted at boot;
    // we can get this information once at startup and do not need to
    // refresh it.
    //
    // Treat errors as fatal. Note that the ESP not being mounted is not
    // an error; that will be a `None` value.
    let esp = match Esp::find() {
        Ok(esp) => esp,
        Err(err) => {
            error!("failed to find ESP mount: {err}");
            return Err(err);
        }
    };

    let api = create_dbus_api(esp)?;

    run_daemon(api)
}
