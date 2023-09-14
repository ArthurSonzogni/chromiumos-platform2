// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::hal_bindings;
use async_trait::async_trait;
use lazy_static::lazy_static;
use log::{debug, error};
use scopeguard::defer;
use std::convert::TryFrom;
use std::convert::TryInto;
use std::sync::{Arc, Mutex};
use tokio::sync::mpsc;
use uwb_core::error::{Error as UwbError, Result as UwbResult};
use uwb_core::params::uci_packets::SessionId;
use uwb_core::uci::{UciHal, UciHalPacket};

/// Struct used to send Event-id and Status received from the HAL as
/// a single message across the channel.
#[derive(Debug)]
struct HalEvent {
    event_id: u32,
    status: u32,
}

/// Struct used to protect an UnboundedSender used to send HAL events
/// or data within a mutex.
struct HalSender<T> {
    sender: Arc<Mutex<Option<mpsc::UnboundedSender<T>>>>,
}

impl<T> HalSender<T> {
    /// Stores the sender.
    fn set(&self, sender: mpsc::UnboundedSender<T>) {
        *self.sender.lock().unwrap() = Some(sender);
    }

    /// Clears the sender.
    fn clear(&self) {
        *self.sender.lock().unwrap() = None;
    }

    /// Sends a packet over the channel using the stored sender.
    fn send(&self, packet: T) -> Result<(), ()> {
        if let Some(sender) = self.sender.lock().unwrap().as_ref() {
            if let Err(err) = sender.send(packet) {
                error!("UCI_HAL: Send failed: {}", err);
                return Err(());
            }
        } else {
            error!("UCI_HAL: Hal Sender is None");
            return Err(());
        }

        Ok(())
    }
}

lazy_static! {
    // Sender used by the callback registered with the HAL to send events containing the status
    // of the requested open, init and close operations.
    static ref HAL_EVT_SENDER: HalSender<HalEvent> =
        HalSender { sender: Arc::new(Mutex::new(None)) };

    // Sender used by the callback registered with the HAL to send UCI packets.
    // static mut uci_packet_tx: Option<mpsc::UnboundedSender<UciHalPacket>> = None;
    static ref UCI_PACKET_SENDER: HalSender<Vec<u8>> =
        HalSender { sender: Arc::new(Mutex::new(None)) };
}

/// Callback function registered with the HAL to receive events containing the status of the
/// requested operation.
extern "C" fn hal_event_status_cb(
    event: hal_bindings::uwb_event_t,
    event_status: hal_bindings::uwb_status_t,
) {
    let hal_event: HalEvent = HalEvent {
        event_id: u32::from(event),
        status: u32::from(event_status),
    };

    if HAL_EVT_SENDER.send(hal_event).is_err() {
        error!(
            "UCI_HAL: Error sending event:{} status:{}",
            event, event_status
        );
    }
}

/// Returns a Vec created from a raw pointer to a buffer.
///
/// # Safety
///
/// ptr_to_data must be non-null.
///
/// ptr_to_data must point to data_len consecutive properly initialized values of
/// type u8.
///
/// Adding data_len to ptr_to_data must not wrap around the address space.
unsafe fn ptr_to_vec(ptr_to_data: *const u8, data_len: u16) -> Vec<u8> {
    let data_slice: &[u8] = std::slice::from_raw_parts(ptr_to_data as *mut u8, data_len as usize);

    data_slice.to_vec()
}

/// Callback function registered with the HAL to receive UCI packets.
extern "C" fn hal_data_cb(data_len: u16, p_data: *mut u8) {
    // Check for null.
    if p_data.is_null() {
        error!("UCI_HAL: p_data is NULL");
        return;
    }

    // Check for wrap around.
    if p_data.wrapping_add(data_len.into()) < p_data {
        error!("UCI_HAL: Data wraps around address space");
        return;
    }

    let data_vec: Vec<u8>;

    // Unsafe because we need to access and read in the data from a raw pointer.
    // SAFETY: We just checked that p_data is not null and
    // that the data being accessed does not wrap around
    // the address space.
    unsafe {
        data_vec = ptr_to_vec(p_data, data_len);
    }

    if UCI_PACKET_SENDER.send(data_vec).is_err() {
        error!("UCI_HAL: Failed to send UCI packet");
    }
}

/// Initializes the UWB hardware via the interface exposed by the HAL.
async fn hal_core_init() -> UwbResult<()> {
    debug!("UCI HAL: Core init");

    let (init_sender, mut init_receiver) = mpsc::unbounded_channel();
    let hal_core_init_result: u16;

    HAL_EVT_SENDER.set(init_sender);
    defer! {
        HAL_EVT_SENDER.clear();
    }

    // Unsafe because the function being called is defined in the HAL library that is
    // implemented in C and not in Rust.
    unsafe {
        hal_core_init_result = hal_bindings::phNxpUciHal_coreInitialization();
    }

    if u32::from(hal_core_init_result) != hal_bindings::NxpUwbHalStatus_HAL_STATUS_OK {
        error!(
            "UCI HAL: Error initializing NXP HAL: 0x{:X}",
            hal_core_init_result
        );
        return Err(UwbError::ForeignFunctionInterface);
    }

    if let Some(hal_evt) = init_receiver.recv().await {
        if hal_evt.event_id == hal_bindings::UWB_EVT_HAL_UWB_INIT_CPLT_EVT
            && hal_evt.status == hal_bindings::UWB_EVT_STATUS_HAL_UWB_STATUS_OK
        {
            debug!("UCI HAL: Core init succesful");
            Ok(())
        } else {
            error!(
                "UCI HAL: Error initializing NXP HAL: Event: {}
             Status: {}",
                hal_evt.event_id, hal_evt.status
            );
            Err(UwbError::ForeignFunctionInterface)
        }
    } else {
        error!("UCI HAL: Error receiving message on init_receiver");
        Err(UwbError::ForeignFunctionInterface)
    }
}

/// Initializes the UWB HAL and the hardware.
async fn hal_open() -> UwbResult<()> {
    debug!("UCI HAL: Open");

    let (open_sender, mut open_receiver) = mpsc::unbounded_channel();
    let hal_open_result: u16;

    HAL_EVT_SENDER.set(open_sender);
    defer! {
        HAL_EVT_SENDER.clear();
    }

    // Unsafe because the function being called is defined in the HAL library that is
    // implemented in C and not in Rust.
    unsafe {
        hal_open_result =
            hal_bindings::phNxpUciHal_open(Some(hal_event_status_cb), Some(hal_data_cb));
    }

    if u32::from(hal_open_result) != hal_bindings::NxpUwbHalStatus_HAL_STATUS_OK {
        error!("UCI HAL: Error opening NXP HAL: 0x{:X}", hal_open_result);
        HAL_EVT_SENDER.clear();
        return Err(UwbError::ForeignFunctionInterface);
    }

    if let Some(hal_open_evt) = open_receiver.recv().await {
        if hal_open_evt.event_id == hal_bindings::UWB_EVT_HAL_UWB_OPEN_CPLT_EVT
            && hal_open_evt.status == hal_bindings::UWB_EVT_STATUS_HAL_UWB_STATUS_OK
        {
            debug!("UCI HAL: Open successful");
            hal_core_init().await
        } else {
            error!(
                "UCI HAL: Error opening NXP HAL: Event: {} Status: {}",
                hal_open_evt.event_id, hal_open_evt.status
            );
            Err(UwbError::ForeignFunctionInterface)
        }
    } else {
        error!("UCI HAL: Error receiving message on open_receiver");
        Err(UwbError::ForeignFunctionInterface)
    }
}

/// A UciHal implementation for ChromeOS.
pub struct UciHalImpl {}

#[async_trait]
impl UciHal for UciHalImpl {
    async fn open(&mut self, packet_sender: mpsc::UnboundedSender<UciHalPacket>) -> UwbResult<()> {
        // Save the packet sender. This handle is used to send UCI messages to the higher layers.
        UCI_PACKET_SENDER.set(packet_sender);

        hal_open().await
    }

    async fn close(&mut self) -> UwbResult<()> {
        debug!("UCI HAL: Close");

        let (close_sender, mut close_receiver) = mpsc::unbounded_channel();
        let hal_close_result: u16;

        HAL_EVT_SENDER.set(close_sender);
        defer! {
            HAL_EVT_SENDER.clear();
        }

        // Unsafe because the function being called is defined in the HAL library that is
        // implemented in C and not in Rust.
        unsafe {
            hal_close_result = hal_bindings::phNxpUciHal_close();
        }

        if u32::from(hal_close_result) != hal_bindings::NxpUwbHalStatus_HAL_STATUS_OK {
            error!("UCI HAL: Error closing NXP HAL: 0x{:X}", hal_close_result);
            return Err(UwbError::ForeignFunctionInterface);
        }

        if let Some(hal_close_evt) = close_receiver.recv().await {
            if hal_close_evt.event_id == hal_bindings::UWB_EVT_HAL_UWB_CLOSE_CPLT_EVT
                && hal_close_evt.status == hal_bindings::UWB_EVT_STATUS_HAL_UWB_STATUS_OK
            {
                debug!("UCI HAL: Close successful");
                Ok(())
            } else {
                error!(
                    "UCI HAL: Error closing NXP HAL: Event: {} Status: {}",
                    hal_close_evt.event_id, hal_close_evt.status
                );
                Err(UwbError::ForeignFunctionInterface)
            }
        } else {
            error!("UCI HAL: Error receiving message on close_receiver");
            Err(UwbError::ForeignFunctionInterface)
        }
    }

    async fn send_packet(&mut self, packet: UciHalPacket) -> UwbResult<()> {
        debug!(
            "UCI HAL: send_packet len:{}; packet:{:?}",
            packet.len(),
            packet
        );

        let hal_write_result: u16;

        let packet_len: Result<u16, <u16 as TryFrom<usize>>::Error> = packet.len().try_into();

        if let Ok(len) = packet_len {
            // Unsafe because the function being called is defined in the HAL library that is
            // implemented in C and not in Rust.
            // SAFETY: We ensured that the len being passed in is a valid u16 converted
            // from usize
            unsafe {
                hal_write_result = hal_bindings::phNxpUciHal_write(len, packet.as_ptr());
            }

            if usize::from(hal_write_result) == packet.len() {
                Ok(())
            } else {
                error!(
                    "UCI HAL: Error writing to NXP HAL: 0x{:X}",
                    hal_write_result
                );
                Err(UwbError::ForeignFunctionInterface)
            }
        } else {
            error!("UCI HAL: Invalid packet length: {}", packet.len());
            Err(UwbError::ForeignFunctionInterface)
        }
    }

    async fn notify_session_initialized(&mut self, session_id: SessionId) -> UwbResult<()> {
        debug!(
            "UCI HAL: notify_session_initialized session_id: {}",
            session_id
        );

        let hal_result: u16;

        // Unsafe because the function being called is defined in the HAL library
        // that is implemented in C and not in Rust.
        unsafe {
            hal_result = hal_bindings::phNxpUciHal_sessionInitialization(session_id);
        }

        if u32::from(hal_result) == hal_bindings::NxpUwbHalStatus_HAL_STATUS_OK {
            Ok(())
        } else {
            error!("UCI HAL: Error initializing session: 0x{:X}", hal_result);
            Err(UwbError::ForeignFunctionInterface)
        }
    }
}
