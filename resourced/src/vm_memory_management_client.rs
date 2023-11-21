// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::os::unix::net::UnixStream as StdUnixStream;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::MutexGuard;
use std::time::Duration;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use async_trait::async_trait;
use dbus::arg::OwnedFd;
use dbus::nonblock::SyncConnection;
use log::error;
use log::info;
use log::warn;
use protobuf::Message as ProtoMessage;
use system_api::vm_memory_management::ConnectionType;
use system_api::vm_memory_management::PacketType;
use system_api::vm_memory_management::ResizePriority;
use system_api::vm_memory_management::VmMemoryManagementPacket;
use tokio::io::AsyncReadExt;
use tokio::io::AsyncWriteExt;
use tokio::net::UnixStream;
use tokio::time::sleep;

use crate::dbus::DEFAULT_DBUS_TIMEOUT;
use crate::dbus_ownership_listener::monitor_dbus_service;
use crate::dbus_ownership_listener::DbusOwnershipChangeCallback;
use crate::vm_concierge_client::VmConciergeClient;

const VM_CONCIERGE_SERVICE_NAME: &str = "org.chromium.VmConcierge";

/// A client to communicate with the VmMemoryManagement service (VMMS). The
/// connection established via the VmConcierge dbus API [1], and the connection
/// protocol is defined by the vm_memory_management proto [2].
///
/// The client internally manages the connection state, and will automatically
/// connect/disconnect as the process providing the VMMS changes.
///
/// This struct should only be used by one tokio task at a time.
///
/// TODO(b/306377872): Move VM memory coordination into resourced
///
/// [1] https://chromium.googlesource.com/chromiumos/platform2/+/main/vm_tools/dbus_bindings/org.chromium.VmConcierge.xml
/// [2] https://chromium.googlesource.com/chromiumos/platform2/+/main/system_api/non_standard_ipc/vm_memory_management/vm_memory_management.proto
pub struct VmMemoryManagementClient {
    // The state of the current VM memory management connection.
    state: Arc<Mutex<VmMMConnectionState>>,
}

enum VmMMConnectionState {
    // The connection is not established.
    Disconnected,
    // The connection is established and idle.
    Connected(VmMMConnection),
    // The connection is established and currently borrwed by a task.
    Borrowed,
}

struct VmMMConnection {
    // Connection to the VM memory management server.
    conn: UnixStream,
    // How long to wait before aborting a reclaim request.
    reclaim_request_timeout: Duration,
    // The sequence number to use for the next reclaim request.
    next_seq_num: u32,

    // Since reading from the socket can be cancelled by a timeout, we need a buffer
    // to store the data from a message split across multiple read attempts.
    read_buffer: Vec<u8>,
    // The index into read_buffer of the next unused byte.
    read_buffer_cursor: usize,
    // Messages to/from the VMMS are prefixed by a u32 header that specifies the
    // message length. This field specifies whether the next bytes read are part
    // of a messager header or a message body.
    read_state: ReadState,
}

enum ReadState {
    ReadHeader,
    ReadBody,
}

const HEADER_LENGTH: usize = std::mem::size_of::<u32>();

impl VmMMConnectionState {
    fn borrow_connection(&mut self) -> Option<VmMMConnection> {
        let state = std::mem::replace(self, VmMMConnectionState::Borrowed);
        match state {
            VmMMConnectionState::Disconnected => {
                *self = VmMMConnectionState::Disconnected;
                None
            }
            VmMMConnectionState::Borrowed => {
                error!("Concurrent borrows not supported");
                None
            }
            VmMMConnectionState::Connected(conn) => Some(conn),
        }
    }

    fn put_borrowed_connection(&mut self, conn: VmMMConnection) {
        if matches!(self, VmMMConnectionState::Borrowed) {
            *self = VmMMConnectionState::Connected(conn);
        }
    }
}

struct VmConciergeMonitor {
    state: Arc<Mutex<VmMMConnectionState>>,
    concierge: VmConciergeClient,
}

#[async_trait]
impl DbusOwnershipChangeCallback for VmConciergeMonitor {
    async fn on_ownership_change(&self, old: String, new: String) -> Result<()> {
        if !old.is_empty() {
            *self.state.lock().expect("failed to lock") = VmMMConnectionState::Disconnected;
        }

        if !new.is_empty() {
            match self.concierge.get_vm_memory_management_connection().await {
                Ok((fd, reclaim_request_timeout)) => {
                    info!("Enabling VmMemoryManagementClient");
                    let mut conn = VmMMConnection::new(fd, reclaim_request_timeout)
                        .context("create VmMMConnection")?;
                    conn.connect().await.context("initialize VmMMConnection")?;
                    *self.state.lock().expect("poisoned lock") =
                        VmMMConnectionState::Connected(conn)
                }
                Err(e) => {
                    info!("VmMemoryManagementClient not activating: {}", e);
                }
            }
        }

        Ok(())
    }
}

impl VmMemoryManagementClient {
    /// Create a new VmMemoryManagementClient
    pub async fn new(conn: Arc<SyncConnection>) -> Result<Self> {
        let state = Arc::new(Mutex::new(VmMMConnectionState::Disconnected));
        let cb = VmConciergeMonitor {
            concierge: VmConciergeClient::new(conn.clone(), DEFAULT_DBUS_TIMEOUT),
            state: state.clone(),
        };
        monitor_dbus_service(&conn, VM_CONCIERGE_SERVICE_NAME, cb)
            .await
            .context("failed to monitor concierge")?;
        Ok(VmMemoryManagementClient { state })
    }

    /// Tell the VMMS to try to reclaim memory from running VMs for use by the
    /// host. `size_kb` is how much to reclaim, and `priorioty` is used to prioritize
    /// the request relative to requests made by the VMs. This function returns
    /// the number of bytes reclaimed, which may be less than the number of bytes
    /// requested. If the client is not active, then this will immediately return 0.
    pub async fn try_reclaim_memory(&self, size_kb: u64, priority: ResizePriority) -> u64 {
        let Some(mut conn) = self.acquire_state().borrow_connection() else {
            return 0;
        };
        let ret = conn.try_reclaim_memory(size_kb, priority).await;
        self.acquire_state().put_borrowed_connection(conn);
        ret
    }

    /// Inform VMMS that the host is under memory pressure but that it has nothing
    /// to reclaim. This will prevent VMMS from granting reclaim requests of VMs.
    pub async fn send_no_kill_candidates(&self) {
        let Some(mut conn) = self.acquire_state().borrow_connection() else {
            return;
        };
        conn.send_no_kill_candidates().await;
        self.acquire_state().put_borrowed_connection(conn);
    }

    /// Returns true if there is an active connection to the VMMS. This will
    /// return false if the VMMS hasn't started yet, if the feature is not enabled
    /// on this device, or if some other error occurred while initializing the
    /// connection.
    pub fn is_active(&self) -> bool {
        !matches!(*self.acquire_state(), VmMMConnectionState::Disconnected)
    }

    fn acquire_state(&self) -> MutexGuard<'_, VmMMConnectionState> {
        // Panic on poisoned mutex.
        self.state.lock().expect("poisoned lock")
    }
}

impl VmMMConnection {
    fn new(fd: OwnedFd, reclaim_request_timeout: Duration) -> Result<Self> {
        let stream = StdUnixStream::from(fd);
        stream
            .set_nonblocking(true)
            .context("failed to set nonblocking")?;
        Ok(Self {
            conn: UnixStream::from_std(stream).context("failed to construct tokio stream")?,
            reclaim_request_timeout,
            next_seq_num: 0,
            read_buffer: vec![0_u8; HEADER_LENGTH],
            read_buffer_cursor: 0,
            read_state: ReadState::ReadHeader,
        })
    }

    async fn connect(&mut self) -> Result<()> {
        let mut packet = VmMemoryManagementPacket::new();
        packet.type_ = PacketType::PACKET_TYPE_HANDSHAKE.into();
        packet.mut_handshake().type_ = ConnectionType::CONNECTION_TYPE_KILLS.into();

        self.write_message(packet).await.context("send handshake")?;

        let response = self.read_message().await.context("receive ack")?;
        if response.type_.enum_value_or_default() != PacketType::PACKET_TYPE_CONNECTION_ACK {
            bail!("bad response type {:?}", response.type_);
        }
        Ok(())
    }

    async fn try_reclaim_memory(&mut self, size_kb: u64, priority: ResizePriority) -> u64 {
        let now = tokio::time::Instant::now();

        let seq_num = self.next_seq_num;
        self.next_seq_num = self.next_seq_num.wrapping_add(1);

        let mut packet = VmMemoryManagementPacket::new();
        packet.type_ = PacketType::PACKET_TYPE_KILL_REQUEST.into();
        packet.mut_kill_decision_request().sequence_num = seq_num;
        packet.mut_kill_decision_request().size_kb = size_kb as u32;
        packet.mut_kill_decision_request().priority = priority.into();

        if let Err(e) = self.write_message(packet).await {
            error!("error writing kill decision request {:?}", e);
            return 0;
        }

        let timeout = self.reclaim_request_timeout;
        let balloon_reclaim_kb = tokio::select! {
            biased;
            size_freed_kb = self.read_reclaim_response(seq_num) => {
                match size_freed_kb {
                    Ok(size_freed_kb) => Some(size_freed_kb),
                    Err(e) => {
                        error!("error reading reclaim response {:?}", e);
                        return 0;
                    }
                }
            },
            () = sleep(timeout) => {
                error!("timeout trying to reclaim guest memory");
                None
            },
        };

        let mut packet = VmMemoryManagementPacket::new();
        packet.type_ = PacketType::PACKET_TYPE_DECISION_LATENCY.into();
        packet.mut_decision_latency().sequence_num = seq_num;
        packet.mut_decision_latency().latency_ms = if balloon_reclaim_kb.is_some() {
            now.elapsed().as_millis() as u32
        } else {
            u32::MAX
        };

        if let Err(e) = self.write_message(packet).await {
            warn!("error writing decision latency {:?}", e);
        }

        balloon_reclaim_kb.unwrap_or(0)
    }

    async fn send_no_kill_candidates(&mut self) {
        let mut packet = VmMemoryManagementPacket::new();
        packet.type_ = PacketType::PACKET_TYPE_NO_KILL_CANDIDATES.into();

        if let Err(e) = self.write_message(packet).await {
            warn!("Failed to send no kill candidates packet {:?}", e);
        }
    }

    async fn read_reclaim_response(&mut self, seq_num: u32) -> Result<u64> {
        loop {
            let packet = self.read_message().await?;
            if packet.type_.enum_value_or_default() != PacketType::PACKET_TYPE_KILL_DECISION
                || !packet.has_kill_decision_response()
            {
                bail!(
                    "Recieved unexpected message: type={:?} has_kill_decision_response={}",
                    packet.type_,
                    packet.has_kill_decision_response()
                );
            }
            let response = packet.kill_decision_response();
            if response.sequence_num == seq_num {
                return Ok(response.size_freed_kb as u64);
            }
        }
    }

    // Since try_reclaim_memory implements a timeout for the reclaim response, reading
    // the response needs to be cancel safe. Accomplish that by storing all intermediate
    // state of read_message in self, so subsequent calls can resume if the underlying
    // read is cancelled.
    async fn read_message(&mut self) -> Result<VmMemoryManagementPacket> {
        loop {
            let len = self
                .conn
                .read(&mut self.read_buffer[self.read_buffer_cursor..])
                .await
                .context("read failed")?;
            if len == 0 {
                bail!("VmMMConnection closed");
            }
            self.read_buffer_cursor += len;
            if self.read_buffer.len() == self.read_buffer_cursor {
                match self.read_state {
                    ReadState::ReadHeader => {
                        let len = u32::from_le_bytes(
                            self.read_buffer
                                .clone()
                                .try_into()
                                .expect("bad slice length"),
                        );
                        self.read_buffer = vec![0_u8; len as usize];
                        self.read_buffer_cursor = 0;
                        self.read_state = ReadState::ReadBody;
                    }
                    ReadState::ReadBody => {
                        let packet =
                            VmMemoryManagementPacket::parse_from_bytes(self.read_buffer.as_slice())
                                .context("parsing packet failed")?;
                        self.read_buffer = vec![0_u8; HEADER_LENGTH];
                        self.read_buffer_cursor = 0;
                        self.read_state = ReadState::ReadHeader;
                        return Ok(packet);
                    }
                }
            }
        }
    }

    async fn write_message(&mut self, message: VmMemoryManagementPacket) -> Result<()> {
        let message_bytes = message
            .write_to_bytes()
            .context("failed serializing packet")?;
        let bytes = [
            &(message_bytes.len() as u32).to_le_bytes()[..],
            &message_bytes,
        ]
        .concat();
        self.conn.write_all(&bytes).await.context("write failed")
    }
}

#[cfg(test)]
mod tests {
    use std::io::Read;
    use std::io::Write;

    use super::*;

    const TEST_RECLAIM_DECISION_TIMEOUT: Duration = Duration::from_secs(2);

    fn read_server(server: &mut StdUnixStream) -> VmMemoryManagementPacket {
        let mut header: [u8; 4] = [0; 4];
        server.read_exact(&mut header).unwrap();
        let len = u32::from_le_bytes(header);
        let mut body = vec![0_u8; len as usize];
        server.read_exact(body.as_mut_slice()).unwrap();
        VmMemoryManagementPacket::parse_from_bytes(body.as_slice()).unwrap()
    }

    fn write_server(server: &mut StdUnixStream, packet: VmMemoryManagementPacket) {
        let body = packet.write_to_bytes().unwrap();
        let header = (body.len() as u32).to_le_bytes();
        server.write_all(&header).unwrap();
        server.write_all(&body).unwrap();
    }

    fn new_connection() -> (VmMMConnection, StdUnixStream) {
        let (client, server) = StdUnixStream::pair().unwrap();
        let conn =
            VmMMConnection::new(OwnedFd::from(client), TEST_RECLAIM_DECISION_TIMEOUT).unwrap();
        (conn, server)
    }

    fn create_reply(request: &VmMemoryManagementPacket) -> VmMemoryManagementPacket {
        let mut reply = VmMemoryManagementPacket::new();
        reply.type_ = PacketType::PACKET_TYPE_KILL_DECISION.into();
        reply.mut_kill_decision_response().sequence_num =
            request.kill_decision_request().sequence_num;
        reply.mut_kill_decision_response().size_freed_kb = request.kill_decision_request().size_kb;
        reply
    }

    async fn advance_paused_runtime() {
        // For a paused runtime, there doesn't seem to be any built-in
        // equivalent to RunUntilIdle, so just yield an unreasonable
        // number of times.
        for _ in 0..100 {
            tokio::task::yield_now().await
        }
    }

    #[tokio::test]
    async fn test_connect_success() {
        let (mut conn, mut server) = new_connection();

        tokio::task::spawn_blocking(move || {
            let packet = read_server(&mut server);
            assert_eq!(packet.type_, PacketType::PACKET_TYPE_HANDSHAKE.into());
            assert_eq!(
                packet.handshake().type_,
                ConnectionType::CONNECTION_TYPE_KILLS.into()
            );

            let mut resp = VmMemoryManagementPacket::new();
            resp.type_ = PacketType::PACKET_TYPE_CONNECTION_ACK.into();
            write_server(&mut server, resp);
        });

        conn.connect().await.unwrap();
    }

    #[tokio::test]
    async fn test_connect_nack() {
        let (mut conn, mut server) = new_connection();

        tokio::task::spawn_blocking(move || {
            let packet = read_server(&mut server);
            assert_eq!(packet.type_, PacketType::PACKET_TYPE_HANDSHAKE.into());
            assert_eq!(
                packet.handshake().type_,
                ConnectionType::CONNECTION_TYPE_KILLS.into()
            );

            let mut resp = VmMemoryManagementPacket::new();
            resp.type_ = PacketType::PACKET_TYPE_CONNECTION_NACK.into();
            write_server(&mut server, resp);
        });

        assert!(conn.connect().await.is_err());
    }

    #[tokio::test]
    async fn test_connect_send_failure() {
        let (mut conn, server) = new_connection();
        drop(server);
        assert!(conn.connect().await.is_err());
    }

    #[tokio::test]
    async fn test_connect_recieve_failure() {
        let (mut conn, mut server) = new_connection();

        tokio::task::spawn_blocking(move || {
            read_server(&mut server);
            drop(server);
        });

        assert!(conn.connect().await.is_err());
    }

    #[tokio::test]
    async fn test_no_kills_candidate() {
        let (mut conn, mut server) = new_connection();

        conn.send_no_kill_candidates().await;

        let packet = read_server(&mut server);
        assert_eq!(
            packet.type_,
            PacketType::PACKET_TYPE_NO_KILL_CANDIDATES.into()
        );
    }

    #[tokio::test]
    async fn test_try_reclaim_memorys() {
        let (mut conn, mut server) = new_connection();

        let join = tokio::task::spawn_blocking(move || {
            // Check the first request
            let packet = read_server(&mut server);
            assert_eq!(packet.type_, PacketType::PACKET_TYPE_KILL_REQUEST.into());
            assert_eq!(packet.kill_decision_request().size_kb, 123);
            assert_eq!(
                packet.kill_decision_request().priority,
                ResizePriority::RESIZE_PRIORITY_CACHED_TAB.into()
            );
            let seq_num = packet.kill_decision_request().sequence_num;

            // Send the first reply
            let mut resp = VmMemoryManagementPacket::new();
            resp.type_ = PacketType::PACKET_TYPE_KILL_DECISION.into();
            resp.mut_kill_decision_response().sequence_num = seq_num;
            resp.mut_kill_decision_response().size_freed_kb = 321;
            write_server(&mut server, resp);

            // Drop the latency report
            let _ = read_server(&mut server);

            // Check the second request
            let packet = read_server(&mut server);
            assert_eq!(packet.type_, PacketType::PACKET_TYPE_KILL_REQUEST.into());
            assert_eq!(packet.kill_decision_request().size_kb, 456);
            assert_eq!(
                packet.kill_decision_request().priority,
                ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_TAB.into()
            );
            let seq_num2 = packet.kill_decision_request().sequence_num;

            // Check that sequence numbers are increasing
            assert!(seq_num < seq_num2);

            // Send the second reply
            let mut resp = VmMemoryManagementPacket::new();
            resp.type_ = PacketType::PACKET_TYPE_KILL_DECISION.into();
            resp.mut_kill_decision_response().sequence_num = seq_num2;
            resp.mut_kill_decision_response().size_freed_kb = 654;
            write_server(&mut server, resp);

            // Drop the second latency report
            let _ = read_server(&mut server);
        });

        assert_eq!(
            conn.try_reclaim_memory(123, ResizePriority::RESIZE_PRIORITY_CACHED_TAB)
                .await,
            321
        );
        assert_eq!(
            conn.try_reclaim_memory(456, ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_TAB)
                .await,
            654,
        );
        join.await.unwrap();
    }

    #[tokio::test(flavor = "current_thread")]
    async fn test_try_reclaim_memory_timeout() {
        let (mut conn, mut server) = new_connection();
        tokio::time::pause();

        let reclaim_resp = tokio::spawn(async move {
            conn.try_reclaim_memory(1234, ResizePriority::RESIZE_PRIORITY_CACHED_TAB)
                .await
        });
        // Advance the runtime to start execution of try_reclaim_memory
        advance_paused_runtime().await;
        // Advance the clock past the timeout duration
        tokio::time::advance(
            TEST_RECLAIM_DECISION_TIMEOUT.saturating_add(Duration::from_millis(1)),
        )
        .await;
        // Check that we actually timed out
        assert_eq!(reclaim_resp.await.unwrap(), 0);

        let packet = read_server(&mut server);
        let seq_num = packet.kill_decision_request().sequence_num;

        // Verify the timeout latency packet
        let packet = read_server(&mut server);
        assert_eq!(
            packet.type_,
            PacketType::PACKET_TYPE_DECISION_LATENCY.into()
        );
        assert_eq!(packet.decision_latency().sequence_num, seq_num);
        assert_eq!(packet.decision_latency().latency_ms, u32::MAX);
    }

    #[tokio::test(flavor = "current_thread")]
    async fn test_try_reclaim_memory_latency_packet() {
        let (mut conn, mut server) = new_connection();
        tokio::time::pause();

        let reclaim_resp = tokio::spawn(async move {
            conn.try_reclaim_memory(1234, ResizePriority::RESIZE_PRIORITY_CACHED_TAB)
                .await
        });
        // Advance the runtime to start execution of try_reclaim_memory
        advance_paused_runtime().await;
        // Advance the clock a little bit so we can check the latency later
        tokio::time::advance(Duration::from_millis(123)).await;

        // Handle the request
        let packet = read_server(&mut server);
        write_server(&mut server, create_reply(&packet));

        // Check that the reclaim response is done
        assert_eq!(reclaim_resp.await.unwrap(), 1234);

        // Verify the timeout latency packet
        let latency = read_server(&mut server);
        assert_eq!(
            latency.type_,
            PacketType::PACKET_TYPE_DECISION_LATENCY.into()
        );
        assert_eq!(
            latency.decision_latency().sequence_num,
            packet.kill_decision_request().sequence_num
        );
        assert_eq!(latency.decision_latency().latency_ms, 123);
    }

    #[tokio::test(flavor = "current_thread")]
    async fn test_try_reclaim_memory_timeout_then_success() {
        let (mut conn, mut server) = new_connection();
        tokio::time::pause();
        const DECISION_2_SIZE: u64 = 5678;

        let reclaim_resp = tokio::spawn(async move {
            let r1 = conn
                .try_reclaim_memory(1234, ResizePriority::RESIZE_PRIORITY_CACHED_TAB)
                .await;
            let r2 = conn
                .try_reclaim_memory(
                    DECISION_2_SIZE,
                    ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_TAB,
                )
                .await;
            (r1, r2)
        });

        // Advance the runtime to start execution of try_reclaim_memory
        advance_paused_runtime().await;

        // Read the first request, and write the first part of the header
        let request = read_server(&mut server);
        let reply = create_reply(&request);

        let body = reply.write_to_bytes().unwrap();
        let header = (body.len() as u32).to_le_bytes();
        const HEADER_SPLIT_IDX: usize = 2;
        server.write_all(&header[..HEADER_SPLIT_IDX]).unwrap();

        // Advance the runtime to read the header
        advance_paused_runtime().await;
        // Time out the read
        tokio::time::advance(
            TEST_RECLAIM_DECISION_TIMEOUT.saturating_add(Duration::from_millis(1)),
        )
        .await;
        // Advance to propagate the read failure
        advance_paused_runtime().await;

        // Finish writing the message
        server.write_all(&header[HEADER_SPLIT_IDX..]).unwrap();
        server.write_all(&body).unwrap();

        // Read the latency packet
        let _ = read_server(&mut server);
        // Read the second request and respond to it
        let request2 = read_server(&mut server);
        let reply = create_reply(&request2);
        write_server(&mut server, reply);

        // Advance the runtime to process the response
        advance_paused_runtime().await;

        // Ignore the second latency packet
        let _ = read_server(&mut server);

        let (r1, r2) = reclaim_resp.await.unwrap();
        assert_eq!(r1, 0);
        assert_eq!(r2, DECISION_2_SIZE);
    }

    #[tokio::test]
    async fn test_try_reclaim_memory_send_failure() {
        let (mut conn, server) = new_connection();
        drop(server);
        assert_eq!(
            conn.try_reclaim_memory(123, ResizePriority::RESIZE_PRIORITY_CACHED_TAB)
                .await,
            0
        );
    }

    #[tokio::test]
    async fn test_try_reclaim_memory_recieve_failure() {
        let (mut conn, mut server) = new_connection();

        tokio::task::spawn_blocking(move || {
            read_server(&mut server);
            drop(server);
        });

        assert_eq!(
            conn.try_reclaim_memory(123, ResizePriority::RESIZE_PRIORITY_CACHED_TAB)
                .await,
            0
        );
    }
}
