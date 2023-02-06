// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate lazy_static;

mod common;
mod dbus_constants;
mod service;
mod shader_cache_mount;

use common::*;

use anyhow::Result;
use dbus::channel::MatchingReceiver;
use dbus::message::MatchRule;
use dbus::MethodErr;
use dbus_crossroads::Crossroads;
use libchromeos::sys::{debug, error, info};
use libchromeos::syslog;
use tokio::signal::unix::{signal, SignalKind};

const BINARY_IDENTITY: &str = "shadercached";

#[tokio::main]
pub async fn main() -> Result<()> {
    libchromeos::panic_handler::install_memfd_handler();

    let identity =
        syslog::get_ident_from_process().unwrap_or_else(|| String::from(BINARY_IDENTITY));
    if let Err(e) = syslog::init_with_level(identity, false, syslog::LevelFilter::Info) {
        panic!("Failed to initialize syslog: {}", e);
    }

    info!("Starting shadercached...");
    // Mount points are VM GPU cache mounting destinations. Each mount point has
    // metadata on what is last requested to be mounted there and current mount
    // status.
    // Note: MountPoints is Arc-ed (cloning returns pointer to the object,
    // thread safe).
    let mount_map = shader_cache_mount::new_mount_map();

    debug!(
        "GPU PCI device ID is {:04x}, DLC variant {}",
        *GPU_DEVICE_ID, *GPU_DEVICE_DLC_VARIANT
    );

    let (resource, c) = dbus_tokio::connection::new_system_sync()?;
    // If D-Bus connection drops unexpectedly, cleanup the mount points then
    // exit.
    let mount_map_resource = mount_map.clone();
    tokio::spawn(async {
        let err = resource.await;
        attempt_unmount_all(mount_map_resource).await;
        error!("Lost connection to D-Bus: {}", err);
        panic!("Lost connection to D-Bus: {}", err);
    });

    // Get the service name from system D-Bus.
    c.request_name(dbus_constants::SERVICE_NAME, false, true, false)
        .await?;

    // Setup crossroads with async support.
    let mut cr = Crossroads::new();
    cr.set_async_support(Some((
        c.clone(),
        Box::new(|x| {
            tokio::spawn(x);
        }),
    )));

    // D-Bus interface for ShaderCache service
    let iface_token = cr.register(dbus_constants::INTERFACE_NAME, |builder| {
        let c_clone1 = c.clone();
        let mount_map_clone1 = mount_map.clone();
        // Method Install
        builder.method_with_cr_async(
            dbus_constants::INSTALL_METHOD,
            ("install_request_proto",),
            (),
            move |mut ctx, _, (raw_bytes,): (Vec<u8>,)| {
                debug!("Received install request");
                let handler =
                    service::handle_install(raw_bytes, mount_map_clone1.clone(), c_clone1.clone());
                async move {
                    match handler.await.map_err(to_method_err) {
                        Ok(result) => ctx.reply(Ok(result)),
                        Err(e) => ctx.reply(Err(e)),
                    }
                }
            },
        );

        let c_clone2 = c.clone();
        let mount_map_clone2 = mount_map.clone();
        // Method Uninstall
        builder.method_with_cr_async(
            dbus_constants::UNINSTALL_METHOD,
            ("uninstall_request_proto",),
            (),
            move |mut ctx, _, (raw_bytes,): (Vec<u8>,)| {
                debug!("Received uninstall request");
                let handler = service::handle_uninstall(
                    raw_bytes,
                    mount_map_clone2.clone(),
                    c_clone2.clone(),
                );
                async move {
                    match handler.await.map_err(to_method_err) {
                        Ok(result) => ctx.reply(Ok(result)),
                        Err(e) => ctx.reply(Err(e)),
                    }
                }
            },
        );

        let c_clone3 = c.clone();
        let mount_map_clone3 = mount_map.clone();
        // Method Purge
        builder.method_with_cr_async(
            dbus_constants::PURGE_METHOD,
            (),
            (),
            move |mut ctx, _, ()| {
                info!("Received purge request");
                let handler = service::handle_purge(mount_map_clone3.clone(), c_clone3.clone());
                async move {
                    match handler.await.map_err(to_method_err) {
                        Ok(result) => ctx.reply(Ok(result)),
                        Err(e) => ctx.reply(Err(e)),
                    }
                }
            },
        );

        let mount_map_clone4 = mount_map.clone();
        // Method umount only
        builder.method_with_cr_async(
            dbus_constants::UNMOUNT_METHOD,
            ("unmount_request_proto",),
            (),
            move |mut ctx, _, (raw_bytes,): (Vec<u8>,)| {
                debug!("Received unmount request");
                let handler = service::handle_unmount(raw_bytes, mount_map_clone4.clone());
                async move {
                    match handler.await.map_err(to_method_err) {
                        Ok(result) => ctx.reply(Ok(result)),
                        Err(e) => ctx.reply(Err(e)),
                    }
                }
            },
        );
    });
    cr.insert(dbus_constants::PATH_NAME, &[iface_token], ());

    // Periodic unmounter
    let c_clone_unmounter = c.clone();
    let mount_map_unmounter = mount_map.clone();
    tokio::spawn(async move {
        // Periodic unmount
        debug!(
            "Periodic unmounter thread stated with interval {:?}",
            UNMOUNTER_INTERVAL
        );
        loop {
            tokio::time::sleep(UNMOUNTER_INTERVAL).await;
            let mut mount_map = mount_map_unmounter.write().await;
            for (vm_id, shader_cache_mount) in mount_map.iter_mut() {
                let mut mount_statuses = shader_cache_mount.process_unmount_queue();
                for status in mount_statuses.iter_mut() {
                    status.set_vm_name(vm_id.vm_name.clone());
                    status.set_vm_owner_id(vm_id.vm_owner_id.clone());
                }

                if let Err(e) =
                    service::signal::signal_mount_status(mount_statuses, &c_clone_unmounter)
                {
                    error!("{}", e);
                }
            }
        }
    });

    // We need to create a new connection to receive signals explicitly.
    // Reusing existing connection rejects the D-Bus signals.
    let mount_map_listener = mount_map.clone();
    let (resource_listen, c_listen) = dbus_tokio::connection::new_system_sync()?;
    tokio::spawn(async {
        let err = resource_listen.await;
        attempt_unmount_all(mount_map_listener).await;

        error!("Lost connection to D-Bus: {}", err);
        panic!("Lost connection to D-Bus: {}", err);
    });

    // Listen to DlcService DlcStateChanged
    let mr_dlc_service_dlc_state_changed = MatchRule::new_signal(
        dbus_constants::dlc_service::INTERFACE_NAME,
        dbus_constants::dlc_service::DLC_STATE_CHANGED_SIGNAL,
    );
    debug!(
        "Matching DlcService signal: {}",
        mr_dlc_service_dlc_state_changed.match_str()
    );
    // For sending signals, we still have to use existing object with correct
    // service name.
    let c_send = c.clone();
    let mount_map_dlc_listener = mount_map.clone();
    // |msg_match| should remain in this scope to serve
    let dlc_service_match = c_listen
        .add_match(mr_dlc_service_dlc_state_changed)
        .await?
        .cb(move |_, (raw_bytes,): (Vec<u8>,)| {
            tokio::spawn(service::handle_dlc_state_changed(
                raw_bytes,
                mount_map_dlc_listener.clone(),
                c_send.clone(),
            ));
            true
        });

    // Listen to VM stopped signals
    let mr_concierge_vm_stopping = MatchRule::new_signal(
        dbus_constants::vm_concierge::INTERFACE_NAME,
        dbus_constants::vm_concierge::VM_STOPPING_SIGNAL,
    );
    debug!(
        "Matching Concierge signal: {}",
        mr_concierge_vm_stopping.match_str()
    );
    let mount_map_concierge_listener = mount_map.clone();
    // |msg_match| should remain in this scope to serve
    let concierge_match = c_listen.add_match(mr_concierge_vm_stopping).await?.cb(
        move |_, (raw_bytes,): (Vec<u8>,)| {
            tokio::spawn(service::handle_vm_stopped(
                raw_bytes,
                mount_map_concierge_listener.clone(),
            ));
            true
        },
    );

    // Start serving D-Bus methods
    let receive_token = c.start_receive(
        MatchRule::new_method_call(),
        Box::new(move |msg, conn| {
            cr.handle_message(msg, conn).unwrap();
            true
        }),
    );
    info!("shadercached serving!");

    // Wait for process exit
    signal(SignalKind::terminate()).unwrap().recv().await;

    info!("Cleaning up...");
    // Stop receiving connections
    c.stop_receive(receive_token);
    // Delete |msg_match| to stop listening to DlcService signals
    drop(dlc_service_match);
    drop(concierge_match);

    attempt_unmount_all(mount_map).await;

    info!("Exiting with successful cleanup!");
    Ok(())
}

async fn attempt_unmount_all(mount_map: shader_cache_mount::ShaderCacheMountMapPtr) {
    match mount_map.clear_all_mounts(None).await {
        Ok(_) => {
            if let Err(e) = mount_map
                .wait_unmount_completed(None, UNMOUNTER_INTERVAL)
                .await
            {
                error!("Failed to wait for unmounts to complete: {}", e)
            }
        }
        Err(e) => error!("Failed to queue unmounts: {}", e),
    }
}

fn to_method_err<T: std::fmt::Display>(result: T) -> MethodErr {
    MethodErr::failed(&result)
}
