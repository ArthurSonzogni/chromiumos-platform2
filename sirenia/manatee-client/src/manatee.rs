// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The client binary for interacting with ManaTEE from command line on Chrome OS.

#![deny(unsafe_op_in_unsafe_fn)]

use std::env;
use std::fs::File;
use std::io::{self, copy, stdin, stdout, BufRead, BufReader, Read, Write};
use std::mem::replace;
use std::os::unix::io::FromRawFd;
use std::path::{Path, PathBuf};
use std::process::{exit, ChildStderr, Command, Stdio};
use std::str::FromStr;
use std::thread::{spawn, JoinHandle};
use std::time::Duration;

use dbus::blocking::Connection;
use getopts::Options;
use libsirenia::{
    cli::{
        self, TransportTypeOption, VerbosityOption, DEFAULT_TRANSPORT_TYPE_LONG_NAME,
        DEFAULT_TRANSPORT_TYPE_SHORT_NAME,
    },
    communication::trichechus::{AppInfo, Trichechus, TrichechusClient},
    rpc,
    transport::{
        self, Transport, TransportType, DEFAULT_CLIENT_PORT, DEFAULT_SERVER_PORT, LOOPBACK_DEFAULT,
    },
};
use log::{self, error, info};
use manatee_client::client::OrgChromiumManaTEEInterface;
use sys_util::{wait_for_interrupt, KillOnDrop};
use thiserror::Error as ThisError;

const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(25);

const DEVELOPER_SHELL_APP_ID: &str = "shell";
const SANDBOXED_SHELL_APP_ID: &str = "sandboxed-shell";

const MINIJAIL_NAME: &str = "minijail0";
const CRONISTA_NAME: &str = "cronista";
const TRICHECHUS_NAME: &str = "trichechus";
const DUGONG_NAME: &str = "dugong";

const CRONISTA_USER: &str = "cronista";
const DUGONG_USER: &str = "dugong";

#[derive(ThisError, Debug)]
enum Error {
    #[error("failed parse command line options: {0:}")]
    OptionsParse(getopts::Fail),
    #[error("failed to get transport type option: {0}")]
    TransportOptionsParse(cli::Error),
    #[error("failed set incompatible options: {0:?}")]
    ConflictingOptions(Vec<String>),
    #[error("failed to get D-Bus connection: {0:}")]
    NewDBusConnection(dbus::Error),
    #[error("failed to call D-Bus method: {0:}")]
    DBusCall(dbus::Error),
    #[error("failed to locate command '{0:}' because: {1:}")]
    LocateCmd(String, which::Error),
    #[error("failed to start command '{0:}' because: {1:}")]
    StartCmd(String, io::Error),
    #[error("failed to read stderr of {0:}: '{1:}'")]
    ReadLineFailed(String, io::Error),
    #[error("failed to bind to socket: {0}")]
    TransportBind(transport::Error),
    #[error("failed to connect to socket: {0}")]
    TransportConnection(transport::Error),
    #[error("failed to locate listening URI for {0:}; last line: '{1:}'")]
    FindUri(String, String),
    #[error("failed to parse port number from line '{0:}'")]
    ParsePort(String),
    #[error("failed to call rpc: {0}")]
    Rpc(rpc::Error),
    #[error("start app failed with code: {0:}")]
    StartApp(i32),
    #[error("Load app failed with: {0:}")]
    LoadApp(String),
    #[error("copy failed: {0:}")]
    Copy(io::Error),
    #[error("open failed: {0:}")]
    Open(io::Error),
    #[error("read failed: {0:}")]
    Read(io::Error),
    #[error("failed to get port: {0:}")]
    GetPort(transport::Error),
    #[error("failed to get client for transport: {0:}")]
    IntoClient(transport::Error),
}

/// The result of an operation in this crate.
type Result<T> = std::result::Result<T, Error>;

fn handle_app_fds<R: 'static + Read + Send + Sized, W: 'static + Write + Send + Sized>(
    mut input: R,
    mut output: W,
) -> Result<()> {
    let output_thread_handle = spawn(move || -> Result<()> {
        copy(&mut input, &mut stdout()).map_err(Error::Copy)?;
        // Once stdout is closed, stdin is invalid and it is time to exit.
        exit(0);
    });

    copy(&mut stdin(), &mut output).map_err(Error::Copy)?;

    output_thread_handle
        .join()
        .map_err(|boxed_err| *boxed_err.downcast::<Error>().unwrap())?
}

fn dbus_start_manatee_app(app_id: &str) -> Result<()> {
    info!("Connecting to D-Bus.");
    let connection = Connection::new_system().map_err(Error::NewDBusConnection)?;
    let conn_path = connection.with_proxy(
        "org.chromium.ManaTEE",
        "/org/chromium/ManaTEE1",
        DEFAULT_DBUS_TIMEOUT,
    );
    info!("Starting TEE application: {}", app_id);
    let (fd_in, fd_out) = match conn_path
        .start_teeapplication(app_id)
        .map_err(Error::DBusCall)?
    {
        (0, fd_in, fd_out) => (fd_in, fd_out),
        (code, _, _) => return Err(Error::StartApp(code)),
    };
    info!("Forwarding stdio.");

    // Safe because ownership of the file descriptor is transferred.
    let file_in = unsafe { File::from_raw_fd(fd_in.into_fd()) };
    // Safe because ownership of the file descriptor is transferred.
    let file_out = unsafe { File::from_raw_fd(fd_out.into_fd()) };

    handle_app_fds(file_in, file_out)
}

fn direct_start_manatee_app(
    trichechus_uri: TransportType,
    app_id: &str,
    elf: Option<Vec<u8>>,
) -> Result<()> {
    info!("Opening connection to trichechus");
    // Adjust the source port when connecting to a non-standard port to facilitate testing.
    let bind_port = match trichechus_uri.get_port().map_err(Error::GetPort)? {
        DEFAULT_SERVER_PORT => DEFAULT_CLIENT_PORT,
        port => port + 1,
    };
    let mut transport = trichechus_uri
        .try_into_client(Some(bind_port))
        .map_err(Error::IntoClient)?;

    let transport = transport.connect().map_err(|e| {
        error!("transport connect failed: {}", e);
        Error::TransportConnection(e)
    })?;
    let client = TrichechusClient::new(transport);

    info!("Setting up app vsock.");
    let mut app_transport = trichechus_uri
        .try_into_client(None)
        .map_err(Error::IntoClient)?;
    let addr = app_transport.bind().map_err(Error::TransportBind)?;
    let app_info = AppInfo {
        app_id: app_id.to_string(),
        port_number: addr.get_port().map_err(Error::GetPort)?,
    };

    if let Some(elf) = elf {
        info!("Transmitting TEE app.");
        client
            .load_app(app_id.to_string(), elf)
            .map_err(Error::Rpc)?
            .map_err(Error::LoadApp)?;
    }

    info!("Starting rpc.");
    client.start_session(app_info).map_err(Error::Rpc)?;

    info!("Starting TEE application: {}", app_id);
    match app_transport.connect() {
        Ok(Transport { r, w, id: _ }) => {
            info!("Forwarding stdio.");
            handle_app_fds(r, w)
        }
        Err(err) => Err(Error::TransportConnection(err)),
    }
}

fn locate_command(name: &str) -> Result<PathBuf> {
    which::which(name).map_err(|err| Error::LocateCmd(name.to_string(), err))
}

fn read_line<R: Read>(
    job_name: &str,
    reader: &mut BufReader<R>,
    mut line: &mut String,
) -> Result<()> {
    line.clear();
    reader
        .read_line(&mut line)
        .map_err(|err| Error::ReadLineFailed(job_name.to_string(), err))?;
    eprint!("{}", &line);
    Ok(())
}

fn get_listening_port<R: Read + Send + 'static, C: Fn(&str) -> bool>(
    job_name: &'static str,
    read: R,
    conditions: &[C],
) -> Result<(u32, JoinHandle<()>)> {
    let mut reader = BufReader::new(read);
    let mut line = String::new();
    read_line(job_name, &mut reader, &mut line)?;

    for condition in conditions {
        if condition(&line) {
            read_line(job_name, &mut reader, &mut line)?;
        }
    }

    if !line.contains("waiting for connection at: ip://127.0.0.1:") {
        return Err(Error::FindUri(job_name.to_string(), line));
    }
    let port = u32::from_str(&line[line.rfind(':').unwrap() + 1..line.len() - 1])
        .map_err(|_| Error::ParsePort(line.clone()))?;

    let join_handle =
        spawn(
            move || {
                while read_line(job_name, &mut reader, &mut line).is_ok() && !line.is_empty() {}
            },
        );
    Ok((port, join_handle))
}

fn run_test_environment() -> Result<()> {
    let minijail_path = locate_command(MINIJAIL_NAME)?;
    let cronista_path = locate_command(CRONISTA_NAME)?;
    let trichechus_path = locate_command(TRICHECHUS_NAME)?;
    let dugong_path = locate_command(DUGONG_NAME)?;

    // Cronista.
    let mut cronista = KillOnDrop::from(
        Command::new(&minijail_path)
            .args(&[
                "-u",
                CRONISTA_USER,
                "--",
                cronista_path.to_str().unwrap(),
                "-U",
                "ip://127.0.0.1:0",
            ])
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|err| Error::StartCmd(CRONISTA_NAME.to_string(), err))?,
    );

    let (cronista_port, cronista_stderr_print) = get_listening_port(
        CRONISTA_NAME,
        replace(&mut cronista.as_mut().stderr, Option::<ChildStderr>::None).unwrap(),
        &[|l: &str| l.ends_with("starting cronista\n")],
    )?;

    // Trichechus.
    let mut trichechus = KillOnDrop::from(
        Command::new(trichechus_path)
            .args(&[
                "-U",
                "ip://127.0.0.1:0",
                "-C",
                &format!("ip://127.0.0.1:{}", cronista_port),
            ])
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|err| Error::StartCmd(TRICHECHUS_NAME.to_string(), err))?,
    );

    let conditions = [
        |l: &str| l == "Syslog exists.\n" || l == "Creating syslog.\n",
        |l: &str| l.contains("starting trichechus:"),
        |l: &str| l.contains("Unable to start new process group:"),
    ];
    let (trichechus_port, trichechus_stderr_print) = get_listening_port(
        TRICHECHUS_NAME,
        replace(&mut trichechus.as_mut().stderr, Option::<ChildStderr>::None).unwrap(),
        &conditions,
    )?;

    // Dugong.
    let dugong = KillOnDrop::from(
        Command::new(&minijail_path)
            .args(&[
                "-u",
                DUGONG_USER,
                "--",
                dugong_path.to_str().unwrap(),
                "-U",
                &format!("ip://127.0.0.1:{}", trichechus_port),
            ])
            .spawn()
            .map_err(|err| Error::StartCmd(DUGONG_NAME.to_string(), err))?,
    );

    println!("*** Press Ctrl-C to continue. ***");
    wait_for_interrupt().ok();

    drop(dugong);
    drop(trichechus);
    drop(cronista);

    trichechus_stderr_print.join().unwrap();
    cronista_stderr_print.join().unwrap();

    Ok(())
}

fn main() -> Result<()> {
    const HELP_SHORT_NAME: &str = "h";
    const RUN_SERVICES_LOCALLY_SHORT_NAME: &str = "r";
    const SANDBOX_SHORT_NAME: &str = "s";
    const APP_ID_SHORT_NAME: &str = "a";
    const APP_ELF_SHORT_NAME: &str = "X";

    let mut options = Options::new();
    options.optflag(HELP_SHORT_NAME, "help", "Show this help string.");
    options.optflag(
        SANDBOX_SHORT_NAME,
        "enable-sandbox",
        "Run the shell in the default sandbox.",
    );
    options.optflag(
        RUN_SERVICES_LOCALLY_SHORT_NAME,
        "run-services-locally",
        "Run a test sirenia environment locally.",
    );
    options.optopt(
        APP_ID_SHORT_NAME,
        "app-id",
        "Specify the app ID to invoke.",
        "demo_app",
    );
    options.optopt(
        APP_ELF_SHORT_NAME,
        "app-elf",
        "Specify the app elf file to load.",
        "/bin/bash",
    );
    let trichechus_uri_opt = TransportTypeOption::new(
        DEFAULT_TRANSPORT_TYPE_SHORT_NAME,
        DEFAULT_TRANSPORT_TYPE_LONG_NAME,
        "trichechus URI (set to bypass dugong D-Bus)",
        LOOPBACK_DEFAULT,
        &mut options,
    );
    let verbosity_opt = VerbosityOption::default(&mut options);

    let args: Vec<String> = env::args().collect();
    let matches = options.parse(&args[1..]).map_err(|err| {
        eprintln!("{}", options.usage(""));
        Error::OptionsParse(err)
    })?;

    stderrlog::new()
        .verbosity(verbosity_opt.from_matches(&matches))
        .init()
        .unwrap();

    if matches.opt_present(HELP_SHORT_NAME) {
        println!("{}", options.usage(""));
        return Ok(());
    }

    let mut opts = Vec::<String>::new();
    for short_name in &[
        SANDBOX_SHORT_NAME,
        RUN_SERVICES_LOCALLY_SHORT_NAME,
        APP_ID_SHORT_NAME,
    ] {
        if matches.opt_present(short_name) {
            opts.push(format!("-{}", short_name));
        }
    }

    if matches.opt_present(RUN_SERVICES_LOCALLY_SHORT_NAME) {
        if opts.len() > 1 {
            eprintln!("{}", options.usage(""));
            return Err(Error::ConflictingOptions(opts));
        }
        return run_test_environment();
    }

    let app_id = if matches.opt_present(SANDBOX_SHORT_NAME) {
        if matches.opt_present(APP_ID_SHORT_NAME) {}
        SANDBOXED_SHELL_APP_ID.to_string()
    } else if let Some(app_id) = matches.opt_get(APP_ID_SHORT_NAME).unwrap() {
        app_id
    } else {
        DEVELOPER_SHELL_APP_ID.to_string()
    };

    let elf = if let Some(elf_path) = matches.opt_get::<String>(APP_ELF_SHORT_NAME).unwrap() {
        let mut data = Vec::<u8>::new();
        File::open(Path::new(&elf_path))
            .map_err(Error::Open)?
            .read_to_end(&mut data)
            .map_err(Error::Read)?;
        Some(data)
    } else {
        None
    };

    match trichechus_uri_opt
        .from_matches(&matches)
        .map_err(Error::TransportOptionsParse)?
    {
        None => dbus_start_manatee_app(&app_id),
        Some(trichechus_uri) => direct_start_manatee_app(trichechus_uri, &app_id, elf),
    }
}
