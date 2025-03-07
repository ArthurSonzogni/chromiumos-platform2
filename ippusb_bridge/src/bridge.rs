// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::Infallible;

use hyper::http::StatusCode;
use hyper::server::conn::http1;
use hyper::service::service_fn;
use hyper::{Body, Request, Response};
use log::{debug, error, info};
use tokio::net::{TcpListener, TcpStream};
use tokio::runtime::Handle as AsyncHandle;
use tokio::sync::mpsc;

use crate::error::Error;
use crate::http::handle_request;
use crate::usb_connector::{UsbConnection, UsbConnector};

#[derive(Debug)]
pub enum ShutdownReason {
    Error,
    Signal,
    Unplugged,
}

pub struct Bridge {
    verbose_log: bool,
    num_clients: usize,

    shutdown: mpsc::Receiver<ShutdownReason>,
    listener: TcpListener,
    usb: UsbConnector,
    handle: AsyncHandle,
}

impl Bridge {
    pub fn new(
        verbose_log: bool,
        shutdown: mpsc::Receiver<ShutdownReason>,
        listener: TcpListener,
        usb: UsbConnector,
        handle: AsyncHandle,
    ) -> Self {
        Self {
            verbose_log,
            num_clients: 0,
            shutdown,
            listener,
            usb,
            handle,
        }
    }

    pub async fn run(&mut self) {
        'poll: loop {
            tokio::select! {
                shutdown_type = self.shutdown.recv() => {
                    info!(
                        "Shutdown event received: {:?}",
                        shutdown_type.unwrap_or(ShutdownReason::Error));
                    break 'poll;
                }

                c = self.listener.accept() => {
                    match c {
                        Ok((stream, addr)) => {
                            info!("Connection opened from {}", addr);
                            self.handle_connection(stream);
                        }
                        Err(err) => error!("Failed to accept connection: {}", err),
                    }
                }
            }
        }
    }

    async fn service_request(
        verbose: bool,
        usb: Option<UsbConnection>,
        request: Request<Body>,
        handle: AsyncHandle,
    ) -> std::result::Result<Response<Body>, Infallible> {
        if usb.is_none() {
            return Ok(Response::builder()
                .status(StatusCode::INTERNAL_SERVER_ERROR)
                .body(Body::empty())
                .unwrap());
        }
        let usb = usb.unwrap();

        handle_request(verbose, usb, request, handle)
            .await
            .or_else(|err| {
                error!("Request failed: {}", err);
                Ok(Response::builder()
                    .status(StatusCode::INTERNAL_SERVER_ERROR)
                    .body(Body::empty())
                    .unwrap())
            })
    }

    fn handle_connection(&mut self, stream: TcpStream) {
        let mut thread_usb = self.usb.clone();
        let verbose = self.verbose_log;
        self.num_clients += 1;
        let client_num = self.num_clients;
        let async_handle = self.handle.clone();

        self.handle.spawn(async move {
            if verbose {
                debug!("Connection {} opened", client_num);
            }
            if let Err(http_err) = http1::Builder::new()
                .title_case_headers(true)
                .preserve_header_case(true)
                .serve_connection(
                    stream,
                    service_fn(move |req| {
                        // We would normally want to extract usb_conn and return early if it's an
                        // error, but that doesn't work here because we can't match the return type
                        // of Bridge::service_request.  Instead, convert to an Option and handle a
                        // missing value in service_request.
                        let usb_conn = thread_usb
                            .get_connection()
                            .inspect_err(|err| {
                                error!("Getting USB connection failed: {}", err);
                            })
                            .ok();

                        Bridge::service_request(verbose, usb_conn, req, async_handle.clone())
                    }),
                )
                .await
            {
                error!("Error serving HTTP connection: {}", http_err);
            }
            Ok::<(), Error>(())
        });
    }
}
