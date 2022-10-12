// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod common;
mod service;
mod shader_cache_mount;

use common::*;
use service::*;
use shader_cache_mount::*;

use anyhow::Result;
use dbus::channel::MatchingReceiver;
use dbus::message::MatchRule;
use dbus_crossroads::Crossroads;
use libchromeos::sys::{debug, error, info, syslog};
use tokio::signal::unix::{signal, SignalKind};

#[tokio::main]
pub async fn main() -> Result<()> {
    if let Err(e) = syslog::init() {
        panic!("Failed to initialize syslog: {}", e);
    }

    info!("Starting shadercached...");
    // Mount points are VM GPU cache mounting destinations. Each mount point has
    // metadata on what is last requested to be mounted there and current mount
    // status.
    // Note: MountPoints is Arc-ed (cloning returns pointer to the object,
    // thread safe).
    let mount_points = new_mount_map();

    let (resource, c) = dbus_tokio::connection::new_system_sync()?;
    // If D-Bus connection drops unexpectedly, cleanup the mount points then
    // exit.
    let mount_points_clone1 = mount_points.clone();
    tokio::spawn(async {
        let err = resource.await;
        // Unmount all mount points
        clean_up(mount_points_clone1).await;
        error!("Lost connection to D-Bus: {}", err);
        panic!("Lost connection to D-Bus: {}", err);
    });

    // Get the service name from system D-Bus.
    c.request_name(SERVICE_NAME, false, true, false).await?;

    // Setup crossroads with async support.
    let mut cr = Crossroads::new();
    cr.set_async_support(Some((
        c.clone(),
        Box::new(|x| {
            tokio::spawn(x);
        }),
    )));

    // D-Bus interface for ShaderCache service
    let iface_token = cr.register(INTERFACE_NAME, |builder| {
        let c_clone1 = c.clone();
        let mount_points_clone1 = mount_points.clone();
        // Method Install
        builder.method_with_cr_async(
            INSTALL_METHOD,
            ("install_request_proto",),
            (),
            move |mut ctx, _, (raw_bytes,): (Vec<u8>,)| {
                info!("Received install request");
                let handler =
                    handle_install(raw_bytes, mount_points_clone1.clone(), c_clone1.clone());
                async move {
                    match handler.await {
                        Ok(result) => ctx.reply(Ok(result)),
                        Err(e) => ctx.reply(Err(e)),
                    }
                }
            },
        );

        let c_clone2 = c.clone();
        let mount_points_clone2 = mount_points.clone();
        // Method Uninstall
        builder.method_with_cr_async(
            UNINSTALL_METHOD,
            ("uninstall_request_proto",),
            (),
            move |mut ctx, _, (raw_bytes,): (Vec<u8>,)| {
                info!("Received uninstall request");
                let handler =
                    handle_uninstall(raw_bytes, mount_points_clone2.clone(), c_clone2.clone());
                async move {
                    match handler.await {
                        Ok(result) => ctx.reply(Ok(result)),
                        Err(e) => ctx.reply(Err(e)),
                    }
                }
            },
        );

        let c_clone3 = c.clone();
        let mount_points_clone3 = mount_points.clone();
        // Method Uninstall
        builder.method_with_cr_async(PURGE_METHOD, (), (), move |mut ctx, _, ()| {
            info!("Received purge request");
            let handler = handle_purge(mount_points_clone3.clone(), c_clone3.clone());
            async move {
                match handler.await {
                    Ok(result) => ctx.reply(Ok(result)),
                    Err(e) => ctx.reply(Err(e)),
                }
            }
        });
    });
    cr.insert(PATH_NAME, &[iface_token], ());

    // Listen to DlcService DlcStateChanged
    let mr = MatchRule::new_signal(
        dlc_service::INTERFACE_NAME,
        dlc_service::DLC_STATE_CHANGED_SIGNAL,
    );
    debug!("Matching DlcService signal: {}", mr.match_str());

    // We need to create a new connection to receive signals explicitly.
    // Reusing existing connection rejects the D-Bus signals.
    let (resource_listen, c_listen) = dbus_tokio::connection::new_system_sync()?;
    let mount_points_clone2 = mount_points.clone();
    tokio::spawn(async {
        let err = resource_listen.await;
        // Unmount all mount points
        clean_up(mount_points_clone2).await;
        error!("Lost connection to D-Bus: {}", err);
        panic!("Lost connection to D-Bus: {}", err);
    });

    // For sending signals, we still have to use existing object with correct
    // service name.
    let c_send = c.clone();
    let mount_points_clone3 = mount_points.clone();
    // |msg_match| should remain in this scope to serve
    let msg_match = c_listen
        .add_match(mr)
        .await?
        .cb(move |_, (raw_bytes,): (Vec<u8>,)| {
            tokio::spawn(handle_dlc_state_changed(
                raw_bytes,
                mount_points_clone3.clone(),
                c_send.clone(),
            ));
            true
        });

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
    // Drop connections and unmount all
    c.stop_receive(receive_token);
    // Delete |msg_match| to stop listening to DlcService signals
    drop(msg_match);
    clean_up(mount_points).await;

    info!("Exiting!");
    Ok(())
}
