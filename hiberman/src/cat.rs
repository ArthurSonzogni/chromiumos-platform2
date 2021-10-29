// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement the cat debug command

use std::io::{copy, Read, Write};
use std::path::Path;

use anyhow::{Context, Result};

use crate::files::open_bounced_disk_file;

/// Entry point to the `cat` command, which can lift DiskFile content out into
/// the file system. Disk file content is not accessible via normal file system
/// means, usually because either the underlying extents are marked
/// "uninitialized" in the file system, or because file system writes end up in
/// the journal and not in the actual disk extents. This command is useful for
/// getting at the actual contents seen by the hibernate utility. The is_log
/// argument tells the function to stop after the first blank newline is found,
/// which is also the stopping condition used to replay log files (since the
/// size is fixed at creation time).
pub fn cat_disk_file<P: AsRef<Path>>(path: P, is_log: bool) -> Result<()> {
    let mut file = open_bounced_disk_file(path)?;
    let mut stdout = std::io::stdout();
    if is_log {
        let mut buf = Vec::<u8>::new();
        file.read_to_end(&mut buf)
            .context("Failed to read log file")?;
        stdout.write_all(&buf).context("Failed to print log file")?;
    } else {
        let _ = copy(&mut file, &mut stdout)?;
    }

    Ok(())
}
