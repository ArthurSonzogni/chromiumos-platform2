// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;
use std::io;
use std::io::BufRead;
use std::io::BufReader;

/// Struct to hold parsed /proc/vmstat data, only contains used fields.
#[derive(Debug, Default, Clone)]
pub struct Vmstat {
    pub workingset_refault_anon: usize,
    pub workingset_refault_file: usize,
    pub pgsteal_direct: usize,
}

impl Vmstat {
    /// Load /proc/vmstat and parse it.
    pub fn load() -> io::Result<Self> {
        let reader = File::open("/proc/vmstat")?;
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
                "workingset_refault_anon" => &mut result.workingset_refault_anon,
                "workingset_refault_file" => &mut result.workingset_refault_file,
                // kernels before 5.9 don't have workingset_refault_anon and
                // workingset_refault_file but workingset_refault only for file page cache.
                "workingset_refault" => &mut result.workingset_refault_file,
                "pgsteal_direct" => &mut result.pgsteal_direct,
                _ => {
                    continue;
                }
            };
            let Some(value) = tokens.next() else {
                continue;
            };
            let Ok(value) = value.parse::<usize>() else {
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
    fn test_parse_vmstat() {
        let mock_vmstat = r#"
nr_free_pages 44453
nr_zone_inactive_anon 100986
nr_zone_active_anon 294376
nr_zone_inactive_file 61713
nr_zone_active_file 62802
nr_zone_unevictable 22615
nr_zone_write_pending 400
nr_mlock 18
nr_bounce 0
nr_zspages 270755
nr_free_cma 0
nr_inactive_anon 100986
nr_active_anon 294376
nr_inactive_file 61713
nr_active_file 62802
nr_unevictable 22615
nr_slab_reclaimable 20307
nr_slab_unreclaimable 39410
nr_isolated_anon 0
nr_isolated_file 0
workingset_nodes 2292
workingset_refault_anon 72178310
workingset_refault_file 3998735
workingset_activate_anon 2282026
workingset_activate_file 301341
workingset_restore_anon 70302599
workingset_restore_file 2139605
workingset_nodereclaim 0
nr_anon_pages 299987
nr_mapped 123606
nr_file_pages 243333
nr_dirty 400
nr_writeback 0
nr_writeback_temp 0
nr_shmem 115859
nr_shmem_hugepages 0
nr_shmem_pmdmapped 0
nr_file_hugepages 0
nr_file_pmdmapped 0
nr_anon_transparent_hugepages 0
nr_vmscan_write 103599919
nr_vmscan_immediate_reclaim 0
nr_dirtied 10919793
nr_written 115964117
nr_kernel_misc_reclaimable 0
nr_foll_pin_acquired 0
nr_foll_pin_released 0
nr_kernel_stack 15792
nr_page_table_pages 18652
nr_swapcached 2959
nr_dirty_threshold 95570
nr_dirty_background_threshold 7935
pgpgin 50300534
pgpgout 97467164
pswpin 90414350
pswpout 105433731
pgalloc_dma 18365
pgalloc_dma32 355149358
pgalloc_normal 372416197
pgalloc_movable 0
pgsteal_kswapd 41539543
pgsteal_direct 5087936
pgsteal_anon 34022233
pgsteal_file 12605246"#;
        let vmstat = Vmstat::parse(mock_vmstat.as_bytes()).unwrap();
        assert_eq!(vmstat.workingset_refault_anon, 72178310);
        assert_eq!(vmstat.workingset_refault_file, 3998735);
        assert_eq!(vmstat.pgsteal_direct, 5087936);
    }

    #[test]
    fn test_parse_vmstat_workingset_refault() {
        let mock_vmstat = "workingset_refault 12345";
        let vmstat = Vmstat::parse(mock_vmstat.as_bytes()).unwrap();
        assert_eq!(vmstat.workingset_refault_anon, 0);
        assert_eq!(vmstat.workingset_refault_file, 12345);
    }
}
