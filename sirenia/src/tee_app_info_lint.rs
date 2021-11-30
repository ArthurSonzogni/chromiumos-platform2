// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Validate or convert TEE app configuration files.

#![deny(unsafe_op_in_unsafe_fn)]

use std::collections::BTreeSet as Set;
use std::env::args;
use std::fs::File;
use std::io::{stdout, Read, Stdout, Write};
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use getopts::Options;
use libsirenia::cli::{HelpOption, VerbosityOption};
use libsirenia::{
    app_info::{entries_from_path, AppManifestEntry, ExecutableInfo},
    compute_sha256,
};

const CHECK_ONLY: &str = "c";
const OUTPUT_JSON: &str = "j";
const OUTPUT_FILE: &str = "o";
const TARGET_ROOT: &str = "R";

fn get_usage() -> String {
    "tee_app_info_lint: <Options> [Input-path ..]".to_string()
}

fn validate_entries(entries: &[AppManifestEntry]) -> Result<()> {
    let mut app_ids = Set::<&str>::new();
    for entry in entries {
        if !app_ids.insert(&entry.app_name) {
            bail!("Duplicate entry for id: '{}'", &entry.app_name);
        }
    }
    Ok(())
}

fn convert_entries<A: AsRef<Path>>(target_root: A, entries: &mut [AppManifestEntry]) -> Result<()> {
    for entry in entries {
        let exec_info = &mut entry.exec_info;
        if let ExecutableInfo::CrosPath(path, digest) = exec_info {
            let exec_path = target_root.as_ref().join(path.trim_start_matches('/'));
            let mut exec_data = Vec::<u8>::new();
            File::open(&exec_path)
                .with_context(|| format!("Failed to open entry executable path: {:?}", &exec_path))?
                .read_to_end(&mut exec_data)
                .with_context(|| {
                    format!("Failed to read entry executable path: {:?}", &exec_path)
                })?;
            let exec_digest =
                compute_sha256(&exec_data).context("Failed to compute SHA256 digest.")?;
            if let Some(expected_digest) = digest {
                if *expected_digest != exec_digest {
                    bail!(
                        "configured digest ({}) does not match executable ({})",
                        expected_digest,
                        exec_digest
                    );
                }
            } else {
                *digest = Some(exec_digest);
            }
        }
    }
    Ok(())
}

fn main() -> Result<()> {
    stderrlog::new()
        .module(module_path!())
        .init()
        .context("Failed to initialize log")?;

    let mut options = Options::default();
    let verbose_opt = VerbosityOption::default(&mut options);

    options.optflag(
        CHECK_ONLY,
        "check",
        "Parse the configurations without printing the result.",
    );
    options.optflag(OUTPUT_JSON, "json", "Write the output as JSON.");
    options.optopt(
        OUTPUT_FILE,
        "output",
        "Output file name.",
        "config.flex.bin",
    );
    options.optopt(
        TARGET_ROOT,
        "root",
        "Path to root file system from which to convert binary paths to hashes.",
        "/build/amd64-generic",
    );

    let help_opt = HelpOption::new(&mut options);

    let args: Vec<String> = args().collect();
    let matches = help_opt.parse_and_check_self(&options, &args, get_usage);
    let verbosity = verbose_opt.from_matches(&matches);
    log::set_max_level(
        match verbosity {
            0 => log::Level::Error,
            1 => log::Level::Warn,
            2 => log::Level::Info,
            3 => log::Level::Debug,
            _ => log::Level::Trace,
        }
        .to_level_filter(),
    );

    let mut entries = Vec::<AppManifestEntry>::new();

    if matches.free.len() <= 1 {
        options.usage(&get_usage());
        bail!("Missing input path");
    }
    for path_name in &matches.free[1..] {
        let path = Path::new(path_name);
        entries.append(&mut entries_from_path(path).context("Failed to get entries from path.")?);
    }

    validate_entries(&entries)?;

    if let Some(target_root) = matches
        .opt_get::<PathBuf>(TARGET_ROOT)
        .context("Failed to get validation root option")?
    {
        convert_entries(target_root, &mut entries)?;
    }

    if matches.opt_present(CHECK_ONLY) {
        return Ok(());
    }

    let mut file: File;
    let mut stdout_instance: Stdout;
    let writer: &mut dyn Write = if let Some(output_filename) = matches
        .opt_get::<PathBuf>(OUTPUT_FILE)
        .context("Failed to get output file option")?
    {
        file = File::create(output_filename).context("Failed to open file")?;
        &mut file
    } else {
        stdout_instance = stdout();
        &mut stdout_instance
    };

    if matches.opt_present(OUTPUT_JSON) {
        serde_json::to_writer_pretty(writer, &entries).context("Failed to serialize")
    } else {
        let serialized: Vec<u8> = flexbuffers::to_vec(&entries).context("Failed to serialize")?;
        writer
            .write_all(&serialized)
            .context("Failed to write output")
    }
}
