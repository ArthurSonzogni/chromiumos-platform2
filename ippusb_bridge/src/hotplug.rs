// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use libchromeos::deprecated::{EventFd, PollContext};
use log::{error, info};
use rusb::{Context, Registration, UsbContext};

use crate::error::Error;
use crate::error::Result;

pub struct UnplugDetector {
    event_thread_run: Arc<AtomicBool>,
    // These are always Some until the destructor runs.
    registration: Option<Registration<Context>>,
    event_thread: Option<std::thread::JoinHandle<()>>,
    join_event_thread: bool,
}

impl UnplugDetector {
    pub fn new(
        device: rusb::Device<Context>,
        shutdown_fd: EventFd,
        shutdown: &'static AtomicBool,
        delay_shutdown: bool,
    ) -> Result<Self> {
        let context = device.context().clone();
        let handler = CallbackHandler::new(device, shutdown_fd, shutdown, delay_shutdown);
        let registration = rusb::HotplugBuilder::new()
            .enumerate(false)
            .register(&context, Box::new(handler))
            .map_err(Error::RegisterCallback)?;

        // Spawn thread to handle triggering the plug/unplug events.
        // While this is technically busy looping, the thread wakes up
        // only once every 60 seconds unless an event is detected.
        // When the callback is unregistered in Drop, an unplug event will
        // be triggered so we will wake up immediately.
        let run = Arc::new(AtomicBool::new(true));
        let thread_run = run.clone();
        let event_thread = std::thread::spawn(move || {
            while thread_run.load(Ordering::Relaxed) {
                let result = context.handle_events(None);
                if let Err(e) = result {
                    error!("Failed to handle libusb events: {}", e);
                }
            }
            info!("Shutting down libusb event thread.");
        });

        Ok(Self {
            event_thread_run: run,
            registration: Some(registration),
            event_thread: Some(event_thread),
            join_event_thread: !delay_shutdown, // Only wait if we're not managed by upstart.
        })
    }
}

impl Drop for UnplugDetector {
    fn drop(&mut self) {
        self.event_thread_run.store(false, Ordering::Relaxed);

        // The callback is unregistered when the registration is dropped.
        // Unwrap is safe because self.registration is always Some until we drop it here.
        drop(self.registration.take().unwrap());

        // Dropping the callback above wakes the event thread, so this should complete quickly.
        // Unwrap is safe because event_thread only becomes None at drop.
        let t = self.event_thread.take().unwrap();
        if self.join_event_thread {
            t.join()
                .unwrap_or_else(|e| error!("Failed to join event thread: {:?}", e));
        }
    }
}

struct CallbackHandler {
    device: rusb::Device<Context>,
    shutdown_fd: EventFd,
    shutdown_requested: &'static AtomicBool,
    delay_shutdown: bool,
}

impl CallbackHandler {
    fn new(
        device: rusb::Device<Context>,
        shutdown_fd: EventFd,
        shutdown_requested: &'static AtomicBool,
        delay_shutdown: bool,
    ) -> Self {
        Self {
            device,
            shutdown_fd,
            shutdown_requested,
            delay_shutdown,
        }
    }

    // If delayed shutdown is requested, wait for another shutdown event for up to 2s.
    // This gives upstart time to send the process a signal after a USB devices is unplugged.
    fn wait_for_shutdown(&mut self) -> Result<()> {
        if !self.delay_shutdown {
            return Ok(());
        }

        if self.shutdown_requested.load(Ordering::Relaxed) {
            // No need to wait if shutdown has already been requested from another source.
            return Ok(());
        }

        info!("Waiting for shutdown signal");
        let timeout = Duration::from_secs(2);
        let ctx: PollContext<u32> = PollContext::new().map_err(Error::Poll)?;
        ctx.add(&self.shutdown_fd, 1).map_err(Error::Poll)?;
        ctx.wait_timeout(timeout).map_err(Error::Poll)?;
        Ok(())
    }
}

impl rusb::Hotplug<Context> for CallbackHandler {
    fn device_arrived(&mut self, _device: rusb::Device<Context>) {
        // Do nothing.
    }

    fn device_left(&mut self, device: rusb::Device<Context>) {
        if device == self.device {
            info!("Device was unplugged, shutting down");

            if let Err(e) = self.wait_for_shutdown() {
                error!("Failed to wait for signal: {}", e);
            }

            self.shutdown_requested.store(true, Ordering::Relaxed);
            if let Err(e) = self.shutdown_fd.write(1) {
                error!("Failed to trigger shutdown: {}", e);
            }
        }
    }
}
