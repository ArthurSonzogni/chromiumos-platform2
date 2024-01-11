// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use resourced::process_stats;
use resourced::process_stats::MemKind;
use resourced::process_stats::ProcessGroupKind;

fn main() -> Result<()> {
    let stats = process_stats::get_all_memory_stats("/proc", "/sys")?;

    println!("group     total  anon  file shmem  swap");
    for (process_kind, stats) in stats.iter().enumerate() {
        const MIB: u64 = 1024 * 1024;
        let group_name = format!("{:?}", ProcessGroupKind::from(process_kind));
        println!(
            "{:<9} {:>5} {:>5} {:>5} {:>5} {:>5} ",
            group_name.to_ascii_lowercase(),
            stats[MemKind::Total as usize] / MIB,
            stats[MemKind::Anon as usize] / MIB,
            stats[MemKind::File as usize] / MIB,
            stats[MemKind::Shmem as usize] / MIB,
            stats[MemKind::Swap as usize] / MIB,
        );
    }
    Ok(())
}
