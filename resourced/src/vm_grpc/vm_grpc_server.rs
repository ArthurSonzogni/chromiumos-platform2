// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures_util::future::{FutureExt as _, TryFutureExt as _};
use grpcio::{ChannelBuilder, Environment, ResourceQuota, RpcContext, ServerBuilder, UnarySink};
use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::Arc;

use crate::vm_grpc::proto::resourced_bridge::{RequestedInterval, ReturnCode, ReturnCode_Status};
use crate::vm_grpc::proto::resourced_bridge_grpc::{
    create_resourced_comm_listener, ResourcedCommListener,
};

use anyhow::{bail, Result};
use libchromeos::sys::{error, info, warn};
use std::path::Path;
use std::thread;

use crate::cpu_scaling::DeviceCpuStatus;

use super::proto::resourced_bridge::EmptyMessage;

// Server side handler
#[derive(Clone)]
struct ResourcedCommListenerService {
    cpu_dev: DeviceCpuStatus,
    packet_tx_freq: Arc<AtomicI64>,
}

//Server object
pub(crate) struct VmGrpcServer {
    cid: i16,
    port: u16,
    running: bool,
}

impl VmGrpcServer {
    /// Starts the GRPC server.
    /// This function will start a new grpc server instance on a
    /// separate thread and listen for incoming traffic on the given `vsock` ports.
    ///
    /// `cid`: CID of the VM to listen for.
    ///
    /// `port`: port for all incoming traffic.
    ///
    /// `path`: root path relative to sysfs.
    ///
    /// `pkt_tx_freq`: Arc<AtomicI64> that will be shared with client thread.
    ///             Server will modify this value based on VM request.  Client
    ///             Will use this value to set the update frequency of host metric
    ///             packets.  Client can also modify this value in case client detects
    ///             crash in the guest VM GRPC server.
    ///
    /// # Return
    ///
    /// An object with the status of the running server.
    /// TODO: Include and `Arc` object for thread control/exit.
    pub fn run(
        cid: i16,
        port: u16,
        root: &Path,
        pkt_tx_freq: Arc<AtomicI64>,
    ) -> Result<VmGrpcServer> {
        static mut SERVER_RUNNING: bool = false;

        // No other access to this address, functionally safe.
        unsafe {
            if !SERVER_RUNNING {
                SERVER_RUNNING = true;
            } else {
                bail!("Server was already started, ignoring run request");
            }
        }

        let cpu_dev = DeviceCpuStatus::new(root.to_path_buf())?;

        // This reference will be moved to the spawned thread.  Shared memory with
        // client thread.
        let packet_tx_freq = Arc::clone(&pkt_tx_freq);

        // Set this to default value at server start.  Server always starts at no_update
        // state (pkt_tx_freq = -1)
        packet_tx_freq.store(-1, Ordering::Relaxed);

        thread::spawn(move || {
            info!("Running grpc server");

            let env = Arc::new(Environment::new(1));
            let service = create_resourced_comm_listener(ResourcedCommListenerService {
                cpu_dev,
                packet_tx_freq,
            });

            let quota = ResourceQuota::new(Some("ResourcedServerQuota")).resize_memory(1024 * 1024);
            let ch_builder = ChannelBuilder::new(env.clone()).set_resource_quota(quota);

            let server = ServerBuilder::new(env)
                .register_service(service)
                .bind(format!("vsock:{}", cid), port)
                .channel_args(ch_builder.build_args())
                .build();

            match server {
                Ok(mut s) => {
                    s.start();

                    for (host, port) in s.bind_addrs() {
                        info!("resourced grpc server started on {}:{}", host, port);
                    }

                    info!("Parking Host Server thread");

                    // TODO(shahadath@): change to channel block_on for cleanup on
                    // vm_exit dbus signal
                    thread::park();
                }
                Err(e) => {
                    warn!("Could not start server. Is vsock support missing?");
                    warn!("{}", e);
                }
            }
        });

        Ok(VmGrpcServer {
            cid,
            port,
            running: true,
        })
    }

    //Print out server status (if already running).
    pub fn get_server_status(&self) -> bool {
        if self.running {
            info!("Server is running on cid={}, port={}", self.cid, self.port);
        } else {
            info!("Server is stopped");
        }

        self.running
    }

    // Exit internal server thread.
    // TODO: implementation.
    pub fn stop(mut self) -> Result<()> {
        self.running = false;
        info!("TODO: vm_exit implementation");
        Ok(())
    }
}

impl ResourcedCommListener for ResourcedCommListenerService {
    fn start_cpu_updates(
        &mut self,
        ctx: RpcContext<'_>,
        req: RequestedInterval,
        sink: UnarySink<ReturnCode>,
    ) {
        info!(
            "==> CPU update request: interval: {}",
            req.get_interval_ms().to_string()
        );

        self.packet_tx_freq
            .store(req.get_interval_ms(), Ordering::Relaxed);
        let resp = ReturnCode::default();
        let f = sink
            .success(resp)
            .map_err(move |e| error!("failed to reply {:?}: {:?}", req, e))
            .map(|_| ());
        ctx.spawn(f)
    }

    fn stop_cpu_updates(
        &mut self,
        ctx: RpcContext<'_>,
        req: EmptyMessage,
        sink: UnarySink<ReturnCode>,
    ) {
        info!("==> CPU update stop request");

        self.packet_tx_freq.store(-1, Ordering::Relaxed);
        let resp = ReturnCode::default();
        let f = sink
            .success(resp)
            .map_err(move |e| error!("failed to reply {:?}: {:?}", req, e))
            .map(|_| ());
        ctx.spawn(f)
    }

    fn set_cpu_frequency(
        &mut self,
        ctx: grpcio::RpcContext,
        req: crate::vm_grpc::proto::resourced_bridge::RequestedCpuFrequency,
        sink: grpcio::UnarySink<crate::vm_grpc::proto::resourced_bridge::ReturnCode>,
    ) {
        let mut resp = ReturnCode::default();
        match self.cpu_dev.set_all_max_cpu_freq(req.get_freq_val() as u64) {
            Ok(_) => {
                info!(
                    "==> CPU frequncy set to {}Hz",
                    req.get_freq_val().to_string()
                );
                resp.set_status(ReturnCode_Status::SUCCESS);
            }
            Err(_) => {
                warn!(
                    "Error setting CPU frequncy to {}Hz!",
                    req.get_freq_val().to_string()
                );
                resp.set_status(ReturnCode_Status::FAIL_UNABLE_TO_SET);
            }
        }

        let f = sink
            .success(resp)
            .map_err(move |e| error!("failed to reply {:?}: {:?}", req, e))
            .map(|_| ());
        ctx.spawn(f)
    }
}
