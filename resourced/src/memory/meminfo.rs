// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;
use std::io;
use std::io::BufRead;
use std::io::BufReader;

/// Struct to hold parsed /proc/meminfo data, only contains used fields.
#[derive(Default, Clone)]
pub struct MemInfo {
    pub total: u64,
    pub free: u64,
    pub active_anon: u64,
    pub inactive_anon: u64,
    pub active_file: u64,
    pub inactive_file: u64,
    pub dirty: u64,
    pub swap_free: u64,
    pub swap_total: u64,
}

impl MemInfo {
    /// Load /proc/meminfo and parse it.
    pub fn load() -> io::Result<Self> {
        let reader = File::open("/proc/meminfo")?;
        let reader = BufReader::new(reader);
        Self::parse(reader)
    }

    fn parse<R: BufRead>(reader: R) -> io::Result<Self> {
        let mut result = Self::default();
        for line in reader.lines() {
            let line = line?;
            let mut tokens = line.split_whitespace();
            let Some(key) = tokens.next() else {
                continue;
            };
            let field = match key {
                "MemTotal:" => &mut result.total,
                "MemFree:" => &mut result.free,
                "Active(anon):" => &mut result.active_anon,
                "Inactive(anon):" => &mut result.inactive_anon,
                "Active(file):" => &mut result.active_file,
                "Inactive(file):" => &mut result.inactive_file,
                "Dirty:" => &mut result.dirty,
                "SwapFree:" => &mut result.swap_free,
                "SwapTotal:" => &mut result.swap_total,
                _ => continue,
            };
            let Some(value) = tokens.next() else {
                continue;
            };
            let Ok(value) = value.parse::<u64>() else {
                continue;
            };
            *field = value;
        }
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_meminfo() {
        let mock_meminfo = r#"
MemTotal:        8025656 kB
MemFree:         4586928 kB
MemAvailable:    6704404 kB
Buffers:          659640 kB
Cached:          1949056 kB
SwapCached:            0 kB
Active:          1430416 kB
Inactive:        1556968 kB
Active(anon):     489640 kB
Inactive(anon):    29188 kB
Active(file):     940776 kB
Inactive(file):  1527780 kB
Unevictable:      151128 kB
Mlocked:           41008 kB
SwapTotal:      11756332 kB
SwapFree:       11756331 kB
Dirty:              5712 kB
Writeback:             0 kB
AnonPages:        529800 kB
Mapped:           321468 kB
Shmem:            140156 kB
Slab:             169252 kB
SReclaimable:     115540 kB
SUnreclaim:        53712 kB
KernelStack:        7072 kB
PageTables:        13340 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:    15769160 kB
Committed_AS:    2483600 kB
VmallocTotal:   34359738367 kB
VmallocUsed:           0 kB
VmallocChunk:          0 kB
Percpu:             2464 kB
AnonHugePages:     40960 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
DirectMap4k:      170216 kB
DirectMap2M:     5992448 kB
DirectMap1G:     3145728 kB"#;
        let meminfo = MemInfo::parse(mock_meminfo.as_bytes()).unwrap();
        assert_eq!(meminfo.free, 4586928);
        assert_eq!(meminfo.active_anon, 489640);
        assert_eq!(meminfo.inactive_anon, 29188);
        assert_eq!(meminfo.active_file, 940776);
        assert_eq!(meminfo.inactive_file, 1527780);
        assert_eq!(meminfo.dirty, 5712);
        assert_eq!(meminfo.swap_free, 11756331);
        assert_eq!(meminfo.swap_total, 11756332);
    }
}
