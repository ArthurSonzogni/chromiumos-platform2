// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{parse_file_to_u64, set_epp};
use crate::memory::{
    calculate_available_memory_kb, calculate_reserved_free_kb, parse_margins, parse_meminfo,
    parse_psi_memory, MemInfo,
};

use tempfile::TempDir;

#[test]
fn test_parse_file_to_u64() {
    assert_eq!(
        parse_file_to_u64("123".to_string().as_bytes()).unwrap(),
        123
    );
    assert_eq!(
        parse_file_to_u64("456\n789".to_string().as_bytes()).unwrap(),
        456
    );
    assert!(parse_file_to_u64("".to_string().as_bytes()).is_err());
    assert!(parse_file_to_u64("abc".to_string().as_bytes()).is_err());
}

#[test]
fn test_calculate_reserved_free_kb() {
    let mock_partial_zoneinfo = r#"
Node 0, zone      DMA
  pages free     3968
        min      137
        low      171
        high     205
        spanned  4095
        present  3999
        managed  3976
        protection: (0, 1832, 3000, 3786)
Node 0, zone    DMA32
  pages free     422432
        min      16270
        low      20337
        high     24404
        spanned  1044480
        present  485541
        managed  469149
        protection: (0, 0, 1953, 1500)
Node 0, zone   Normal
  pages free     21708
        min      17383
        low      21728
        high     26073
        spanned  524288
        present  524288
        managed  501235
        protection: (0, 0, 0, 0)"#;
    let page_size_kb = 4;
    let high_watermarks = 205 + 24404 + 26073;
    let lowmem_reserves = 3786 + 1953;
    let reserved = calculate_reserved_free_kb(mock_partial_zoneinfo.as_bytes()).unwrap();
    assert_eq!(reserved, (high_watermarks + lowmem_reserves) * page_size_kb);
}

#[test]
fn test_parse_psi_memory() {
    let mock_psi_memory = r#"
some avg10=57.25 avg60=35.97 avg300=10.18 total=32748793
full avg10=29.29 avg60=19.01 avg300=5.44 total=17589167"#;
    let pressure = parse_psi_memory(mock_psi_memory.as_bytes()).unwrap();
    assert!((pressure - 57.25).abs() < f64::EPSILON);
}

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
SwapFree:       11756332 kB
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
    let meminfo = parse_meminfo(mock_meminfo.as_bytes()).unwrap();
    assert_eq!(meminfo.free, 4586928);
    assert_eq!(meminfo.active_anon, 489640);
    assert_eq!(meminfo.inactive_anon, 29188);
    assert_eq!(meminfo.active_file, 940776);
    assert_eq!(meminfo.inactive_file, 1527780);
    assert_eq!(meminfo.dirty, 5712);
    assert_eq!(meminfo.swap_free, 11756332);
}

#[test]
fn test_calculate_available_memory_kb() {
    let mut info = MemInfo::default();
    let min_filelist = 400 * 1024;
    let reserved_free = 0;
    let ram_swap_weight = 4;

    // Available determined by file cache.
    info.active_file = 500 * 1024;
    info.inactive_file = 500 * 1024;
    info.dirty = 10 * 1024;
    let file = info.active_file + info.inactive_file;
    let available =
        calculate_available_memory_kb(&info, reserved_free, min_filelist, ram_swap_weight);
    assert_eq!(available, file - min_filelist - info.dirty);

    // Available determined by swap free.
    info.swap_free = 1200 * 1024;
    info.active_anon = 1000 * 1024;
    info.inactive_anon = 1000 * 1024;
    info.active_file = 0;
    info.inactive_file = 0;
    info.dirty = 0;
    let available =
        calculate_available_memory_kb(&info, reserved_free, min_filelist, ram_swap_weight);
    assert_eq!(available, info.swap_free / ram_swap_weight);

    // Available determined by anonymous.
    info.swap_free = 6000 * 1024;
    info.active_anon = 500 * 1024;
    info.inactive_anon = 500 * 1024;
    let anon = info.active_anon + info.inactive_anon;
    let available =
        calculate_available_memory_kb(&info, reserved_free, min_filelist, ram_swap_weight);
    assert_eq!(available, anon / ram_swap_weight);

    // When ram_swap_weight is 0, swap is ignored in available.
    info.swap_free = 1200 * 1024;
    info.active_anon = 1000 * 1024;
    info.inactive_anon = 1000 * 1024;
    info.active_file = 500 * 1024;
    info.inactive_file = 500 * 1024;
    let file = info.active_file + info.inactive_file;
    let ram_swap_weight = 0;
    let available =
        calculate_available_memory_kb(&info, reserved_free, min_filelist, ram_swap_weight);
    assert_eq!(available, file - min_filelist);
}

#[test]
fn test_parse_margins() {
    assert!(parse_margins("".to_string().as_bytes()).is_err());
    assert!(parse_margins("123 4a6".to_string().as_bytes()).is_err());
    assert!(parse_margins("123.2 412.3".to_string().as_bytes()).is_err());
    assert!(parse_margins("123".to_string().as_bytes()).is_err());

    let margins = parse_margins("123 456".to_string().as_bytes()).unwrap();
    assert_eq!(margins.len(), 2);
    assert_eq!(margins[0], 123);
    assert_eq!(margins[1], 456);
}

#[test]
fn test_set_epp() {
    let dir = TempDir::new().unwrap();

    // Create the fake sysfs paths in temp directory
    let mut tpb0 = dir.path().to_owned();
    tpb0.push("sys/devices/system/cpu/cpufreq/policy0/");
    // let dirpath_str0 = tpb0.clone().into_os_string().into_string().unwrap();
    std::fs::create_dir_all(&tpb0).unwrap();

    let mut tpb1 = dir.path().to_owned();
    tpb1.push("sys/devices/system/cpu/cpufreq/policy1/");
    std::fs::create_dir_all(&tpb1).unwrap();

    // Set the EPP
    set_epp(dir.path().to_str().unwrap(), "179").unwrap();

    // Verify that files were written
    tpb0.push("energy_performance_preference");
    tpb1.push("energy_performance_preference");
    assert_eq!(std::fs::read_to_string(&tpb0).unwrap(), "179".to_string());
    assert_eq!(std::fs::read_to_string(&tpb1).unwrap(), "179".to_string());
}
