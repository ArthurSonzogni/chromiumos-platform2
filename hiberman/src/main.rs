// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Coordinates suspend-to-disk activities

use getopts::{self, Options};
use hiberman::{self, HibernateOptions, ResumeOptions};
use log::error;

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
indicates a valid hibernate image, or 1 if no image.
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
    let path = matches.free.get(0).cloned();

    // In verbose mode, or for anything other than "get", fire up logging.
    if verbose || set_cookie || clear_cookie {
        init_logging()?;
    }

    if set_cookie || clear_cookie {
        if let Err(e) = hiberman::cookie::set_hibernate_cookie(path.as_ref(), set_cookie) {
            error!("Failed to write hibernate cookie: {}", e);
            return Err(());
        }
    } else {
        let is_set = match hiberman::cookie::get_hibernate_cookie(path.as_ref()) {
            Ok(s) => s,
            Err(e) => {
                error!("Failed to get hibernate cookie: {}", e);
                return Err(());
            }
        };

        if verbose {
            if is_set {
                println!("Hibernate cookie is set");
            } else {
                println!("Hibernate cookie is not set");
            }
        }

        if !is_set {
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
            error!("Failed to cat {}: {}", &f, e);
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
    };

    if let Err(e) = hiberman::hibernate(options) {
        error!("Failed to hibernate: {:?}", e);
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
    resume -- Resume the system now.
    cat -- Write a disk file contents to stdout.
    cookie -- Read or write the hibernate cookie.
"#;
    print_usage(usage_msg, error);
}

fn hiberman_main() -> std::result::Result<(), ()> {
    let mut args = std::env::args();
    if args.next().is_none() {
        eprintln!("expected executable name.");
        return Err(());
    }

    let subcommand = match args.next() {
        Some(subcommand) => subcommand,
        None => {
            eprintln!("expected a subcommand");
            return Err(());
        }
    };

    match subcommand.as_ref() {
        "--help" | "-h" | "help" => {
            app_usage(false);
            Ok(())
        }
        "cat" => hiberman_cat(&mut args),
        "cookie" => hiberman_cookie(&mut args),
        "hibernate" => hiberman_hibernate(&mut args),
        "resume" => hiberman_resume(&mut args),
        _ => {
            eprintln!("unknown subcommand: {}", subcommand);
            Err(())
        }
    }
}

fn main() {
    std::process::exit(if hiberman_main().is_ok() { 0 } else { 1 });
}

#[cfg(test)]
mod tests {
    //use super::*;
}
