// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Coordinates suspend-to-disk activities.

use getopts::{self, Options};
use hiberman::cookie::HibernateCookieValue;
use hiberman::metrics::{log_hibernate_failure, log_resume_failure};
use hiberman::{self, AbortResumeOptions, HibernateOptions, ResumeInitOptions, ResumeOptions};
use log::{error, warn};

fn print_usage(message: &str, error: bool) {
    if error {
        eprintln!("{}", message)
    } else {
        println!("{}", message);
    }
}

fn init_logging() -> std::result::Result<(), ()> {
    if let Err(e) = hiberman::hiberlog::init() {
        eprintln!("failed to initialize hiberlog: {}", e);
        return Err(());
    }

    Ok(())
}

fn cookie_usage(error: bool, options: &Options) {
    let brief = r#"Usage: hiberman cookie <path> [options]
Get or set the hibernate cookie info. With no options, gets the
current status of the hibernate cookie. Returns 0 if the cookie
indicates a valid hibernate image, or 1 otherwise.
"#;

    print_usage(&options.usage(brief), error);
}

fn hiberman_cookie(args: &mut std::env::Args) -> std::result::Result<(), ()> {
    // Note: Don't fire up logging immediately in this command as it's called
    // during very early init, before syslog is ready.
    let mut opts = Options::new();
    opts.optflag(
        "c",
        "clear",
        "Clear the cookie to indicate no valid hibernate image",
    );
    opts.optflag("h", "help", "Print this help text");
    opts.optflag(
        "s",
        "set",
        "Set the cookie to indicate a valid hibernate image",
    );
    opts.optflag("v", "verbose", "Print more during the command");
    opts.optopt("V",
                "value",
                "Set the cookie to a specific value (options are no_resume, resume_ready, in_progress, aborting, or ereboot)",
                "value");
    let args: Vec<String> = args.collect();
    let matches = match opts.parse(args) {
        Ok(m) => m,
        Err(e) => {
            eprintln!("Failed to parse arguments: {}", e);
            cookie_usage(true, &opts);
            return Err(());
        }
    };

    if matches.opt_present("h") {
        cookie_usage(false, &opts);
        return Ok(());
    }

    let clear_cookie = matches.opt_present("c");
    let set_cookie = matches.opt_present("s");
    let verbose = matches.opt_present("v");
    let value = matches.opt_str("V");
    let path = matches.free.get(0).cloned();

    let verbosity = if matches.opt_present("v") { 9 } else { 1 };
    stderrlog::new()
        .module(module_path!())
        .verbosity(verbosity)
        .init()
        .unwrap();

    if set_cookie || clear_cookie || value.is_some() {
        let value = if let Some(value) = value {
            if set_cookie || clear_cookie {
                eprintln!("Cannot mix --set/--clear with --value");
                return Err(());
            }

            match value.as_str() {
                "no_resume" => HibernateCookieValue::NoResume,
                "resume_ready" => HibernateCookieValue::ResumeReady,
                "in_progress" => HibernateCookieValue::ResumeInProgress,
                "aborting" => HibernateCookieValue::ResumeAborting,
                "ereboot" => HibernateCookieValue::EmergencyReboot,
                _ => {
                    eprintln!("Invalid cookie value: {}", value);
                    cookie_usage(true, &opts);
                    return Err(());
                }
            }
        } else if set_cookie {
            if clear_cookie {
                eprintln!("Cannot set both --set and --clear");
                return Err(());
            }
            HibernateCookieValue::ResumeReady
        } else {
            HibernateCookieValue::NoResume
        };

        if let Err(e) = hiberman::cookie::set_hibernate_cookie(path.as_ref(), value) {
            error!("Failed to write hibernate cookie: {}", e);
            return Err(());
        }
    } else {
        let value = match hiberman::cookie::get_hibernate_cookie(path.as_ref()) {
            Ok(v) => v,
            Err(e) => {
                error!("Failed to get hibernate cookie: {}", e);
                return Err(());
            }
        };

        let is_ready = value == hiberman::cookie::HibernateCookieValue::ResumeReady;
        let description = hiberman::cookie::cookie_description(&value);
        if verbose {
            println!("Hibernate cookie is set to: {}", description);
        }

        if !is_ready {
            return Err(());
        }
    }

    Ok(())
}

fn cat_usage(error: bool, options: &Options) {
    let brief = r#"Usage: hiberman cat [options] <file> [file...]
Print a disk file to stdout. Since disk files write to blocks
underneath the file system, they cannot be read reliably by normal
file system accesses.
"#;

    print_usage(&options.usage(brief), error);
}

fn hiberman_cat(args: &mut std::env::Args) -> std::result::Result<(), ()> {
    init_logging()?;
    let mut opts = Options::new();
    opts.optflag("l", "log", "Treat the file(s) as log files");
    opts.optflag("h", "help", "Print this help text");
    let args: Vec<String> = args.collect();
    let matches = match opts.parse(args) {
        Ok(m) => m,
        Err(e) => {
            error!("Failed to parse arguments: {}", e);
            cat_usage(true, &opts);
            return Err(());
        }
    };

    if matches.opt_present("h") {
        cat_usage(false, &opts);
        return Ok(());
    }

    let mut result = Ok(());
    let is_log = matches.opt_present("l");
    for f in matches.free {
        if let Err(e) = hiberman::cat::cat_disk_file(&f, is_log) {
            error!("Failed to cat {}: {:?}", &f, e);
            result = Err(())
        }
    }

    result
}

fn hibernate_usage(error: bool, options: &Options) {
    let brief = r#"Usage: hiberman hibernate [options]
Hibernate the system now.
"#;

    print_usage(&options.usage(brief), error);
}

fn hiberman_hibernate(args: &mut std::env::Args) -> std::result::Result<(), ()> {
    init_logging()?;
    let mut opts = Options::new();
    opts.optflag("h", "help", "Print this help text");
    opts.optflag(
        "k",
        "no-kernel-encryption",
        "Avoid using kernel based encryption for the hibernation image",
    );
    opts.optflag("n", "dry-run", "Create the hibernate image, but then exit rather than shutting down. This image should only be restored with --dry-run");
    opts.optflag(
        "p",
        "platform-mode",
        "Force enable the use of suspending to platform mode (S4)",
    );
    opts.optflag(
        "u",
        "unencrypted",
        "Do not encrypt the hibernate image. Use only for test and debugging",
    );
    opts.optflag("t", "test-keys", "Use test keys for debugging");
    let args: Vec<String> = args.collect();
    let matches = match opts.parse(args) {
        Ok(m) => m,
        Err(e) => {
            error!("Failed to parse arguments: {}", e);
            hibernate_usage(true, &opts);
            return Err(());
        }
    };

    if matches.opt_present("h") {
        hibernate_usage(false, &opts);
        return Ok(());
    }

    let options = HibernateOptions {
        dry_run: matches.opt_present("n"),
        force_platform_mode: matches.opt_present("p"),
        test_keys: matches.opt_present("t"),
        unencrypted: matches.opt_present("u"),
        no_kernel_encryption: matches.opt_present("k"),
    };

    if let Err(e) = hiberman::hibernate(options) {
        if let Err(e) = log_hibernate_failure() {
            warn!("Failed to log hibernate failure: {}", e);
        }
        error!("Failed to hibernate: {:?}", e);
        return Err(());
    }

    Ok(())
}

fn resume_init_usage(error: bool, options: &Options) {
    let brief = r#"Usage: hiberman resume-init [options]
Perform early init preparations, if required, to make resume from
hibernation possible this boot.
"#;

    print_usage(&options.usage(brief), error);
}

fn hiberman_resume_init(args: &mut std::env::Args) -> std::result::Result<(), ()> {
    let mut opts = Options::new();
    opts.optflag(
        "f",
        "force",
        "Set up a resume world even if the resume cookie is not set",
    );
    opts.optflag("h", "help", "Print this help text");
    opts.optflag("v", "verbose", "Print more logs");
    let args: Vec<String> = args.collect();
    let matches = match opts.parse(args) {
        Ok(m) => m,
        Err(e) => {
            error!("Failed to parse arguments: {}", e);
            resume_init_usage(true, &opts);
            return Err(());
        }
    };

    if matches.opt_present("h") {
        resume_init_usage(false, &opts);
        return Ok(());
    }

    let verbosity = if matches.opt_present("v") { 9 } else { 1 };

    // Syslog is not yet available, so just log to stderr.
    stderrlog::new()
        .module(module_path!())
        .verbosity(verbosity)
        .init()
        .unwrap();
    let options = ResumeInitOptions {
        force: matches.opt_present("f"),
    };

    if let Err(e) = hiberman::resume_init(options) {
        error!("Failed to initialize resume: {:#?}", e);
        return Err(());
    }

    Ok(())
}

fn abort_resume_usage(error: bool, options: &Options) {
    let brief = r#"Usage: hiberman abort-resume [options]
Send an abort request over dbus to another hiberman process currently executing a resume.
"#;

    print_usage(&options.usage(brief), error);
}

fn hiberman_abort_resume(args: &mut std::env::Args) -> std::result::Result<(), ()> {
    let mut opts = Options::new();
    opts.optopt("m", "message", "Supply the reason for the abort", "reason");
    opts.optflag("h", "help", "Print this help text");
    opts.optflag("v", "verbose", "Print more logs");
    let args: Vec<String> = args.collect();
    let matches = match opts.parse(args) {
        Ok(m) => m,
        Err(e) => {
            error!("Failed to parse arguments: {}", e);
            abort_resume_usage(true, &opts);
            return Err(());
        }
    };

    if matches.opt_present("h") {
        abort_resume_usage(false, &opts);
        return Ok(());
    }

    let verbosity = if matches.opt_present("v") { 9 } else { 1 };

    // Syslog is not yet available, so just log to stderr.
    stderrlog::new()
        .module(module_path!())
        .verbosity(verbosity)
        .init()
        .unwrap();
    let mut options = AbortResumeOptions::default();
    if let Some(reason) = matches.opt_str("m") {
        options.reason = reason;
    }

    if let Err(e) = hiberman::abort_resume(options) {
        error!("Failed to abort resume: {:#?}", e);
        return Err(());
    }

    Ok(())
}

fn resume_usage(error: bool, options: &Options) {
    let brief = r#"Usage: hiberman resume [options]
Resume the system now. On success, does not return, but jumps back into the
resumed image.
"#;

    print_usage(&options.usage(brief), error);
}

fn hiberman_resume(args: &mut std::env::Args) -> std::result::Result<(), ()> {
    init_logging()?;
    let mut opts = Options::new();
    opts.optflag("h", "help", "Print this help text");
    opts.optflag("n", "dry-run", "Create the hibernate image, but then exit rather than shutting down. This image should only be restored with --dry-run");
    opts.optflag("p", "no-preloader", "Do not use the ImagePreloader");
    opts.optflag(
        "u",
        "unencrypted",
        "Do not encrypt the hibernate image. Use only for test and debugging",
    );
    opts.optflag("t", "test-keys", "Use test keys for debugging");
    let args: Vec<String> = args.collect();
    let matches = match opts.parse(args) {
        Ok(m) => m,
        Err(e) => {
            error!("Failed to parse arguments: {}", e);
            resume_usage(true, &opts);
            return Err(());
        }
    };

    if matches.opt_present("h") {
        resume_usage(false, &opts);
        return Ok(());
    }

    let options = ResumeOptions {
        dry_run: matches.opt_present("n"),
        no_preloader: matches.opt_present("p"),
        test_keys: matches.opt_present("t"),
        unencrypted: matches.opt_present("u"),
    };

    if let Err(e) = hiberman::resume(options) {
        if let Err(e) = log_resume_failure() {
            warn!("Failed to log resume: {}", e);
        }
        error!("Failed to resume: {:#?}", e);
        return Err(());
    }

    Ok(())
}

fn app_usage(error: bool) {
    let usage_msg = r#"Usage: hiberman subcommand [options]
This application coordinates suspend-to-disk activities. Try
hiberman <subcommand> --help for details on specific subcommands.

Valid subcommands are:
    help -- Print this help text.
    hibernate -- Suspend the machine to disk now.
    resume-init -- Perform early initialization for resume.
    resume -- Resume the system now.
    abort-resume -- Send an abort request to an in-progress resume.
    cat -- Write a disk file contents to stdout.
    cookie -- Read or write the hibernate cookie.
"#;
    print_usage(usage_msg, error);
}

fn hiberman_main() -> std::result::Result<(), ()> {
    let mut args = std::env::args();
    if args.next().is_none() {
        eprintln!("Expected executable name");
        return Err(());
    }

    let subcommand = match args.next() {
        Some(subcommand) => subcommand,
        None => {
            eprintln!("Expected a subcommand");
            return Err(());
        }
    };

    match subcommand.as_ref() {
        "--help" | "-h" | "help" => {
            app_usage(false);
            Ok(())
        }
        "abort-resume" => hiberman_abort_resume(&mut args),
        "cat" => hiberman_cat(&mut args),
        "cookie" => hiberman_cookie(&mut args),
        "hibernate" => hiberman_hibernate(&mut args),
        "resume-init" => hiberman_resume_init(&mut args),
        "resume" => hiberman_resume(&mut args),
        _ => {
            eprintln!("Unknown subcommand: {}", subcommand);
            Err(())
        }
    }
}

fn main() {
    libchromeos::panic_handler::install_memfd_handler();
    std::process::exit(if hiberman_main().is_ok() { 0 } else { 1 });
}

#[cfg(test)]
mod tests {
    //use super::*;
}
