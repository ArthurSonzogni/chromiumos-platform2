// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod meminfo;
mod page_size;
mod psi_memory_handler;
mod psi_monitor;
mod psi_policy;
mod vmstat;

use std::collections::BTreeMap;
use std::fmt;
use std::fs::File;
use std::io::BufRead;
use std::io::BufReader;
use std::io::Write;
use std::path::Path;
use std::sync::Mutex;
use std::time::Duration;
use std::time::Instant;

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use log::error;
use log::info;
use once_cell::sync::Lazy;
use system_api::vm_memory_management::ResizePriority;

pub use self::meminfo::MemInfo;
pub use self::psi_memory_handler::PsiMemoryHandler;
pub use self::psi_memory_handler::Result as PsiPolicyResult;
pub use self::psi_memory_handler::MAX_PSI_ERROR_TYPE;
pub use self::psi_memory_handler::UMA_NAME_PSI_POLICY_ERROR;
use crate::common;
use crate::common::read_from_file;
use crate::feature;
use crate::metrics;
use crate::swappiness_config::SwappinessConfig;
use crate::sync::NoPoison;
use crate::vm_memory_management_client::VmMemoryManagementClient;

// Critical margin is 5.2% of total memory, moderate margin is 40% of total
// memory. See also /usr/share/cros/init/swap.sh on DUT. BPS are basis points.
const DEFAULT_MODERATE_MARGIN_BPS: u64 = 4000;
const DEFAULT_CRITICAL_MARGIN_BPS: u64 = 520;
const DEFAULT_CRITICAL_PROTECTED_MARGIN_BPS: u64 = DEFAULT_CRITICAL_MARGIN_BPS;

// A quarter of swap free is counted as available memory.
const DEFAULT_RAM_SWAP_WEIGHT: u64 = 4;

// Memory config paths.
const RESOURCED_CONFIG_DIR: &str = "run/resourced";
// /run/resourced/margins_kb file is removed. Use D-Bus method SetMemoryMargins to set the
// margins instead.
// The old margins_kb is confusing that it doesn't update when the D-Bus method is called to update
// the margin.
const RAM_SWAP_WEIGHT_FILENAME: &str = "ram_swap_weight";

// The available memory for background components is discounted by 300 MiB.
const GAME_MODE_OFFSET_KB: u64 = 300 * 1024;

const VMMMS_RECLAIM_ENTIRE_TARGET_FEATURE_NAME: &str =
    "CrOSLateBootResourcedVmmmsReclaimEntireTarget";

const DISCARD_STALE_AT_MODERATE_PRESSURE_FEATURE_NAME: &str =
    "CrOSLateBootDiscardStaleAtModeratePressure";

// Devices which have more RAM than this will not discard stale tabs at moderate pressure.
const DISCARD_STALE_AT_MODERATE_HIGH_RAM_CUTOFF_KB: u64 = 8 * 1024 * 1024;

#[cfg(not(test))]
const DISCARD_STALE_AT_MODERATE_PRESSURE_MIN_VISIBLE_SECONDS_THRESHOLD_PARAM: &str =
    "MinLastVisibleDurationSeconds";
#[cfg(not(test))]
const DISCARD_STALE_AT_MODERATE_PRESSURE_MAX_VISIBLE_SECONDS_THRESHOLD_PARAM: &str =
    "MaxLastVisibleDurationSeconds";

// Proportion of the reclaim target to attempt to reclaim from VMMS.
// Represented as a percentage. For example a value of 50 means that
// 50% of the reclaim target will attempt to be reclaimed from VMMS.
const DISCARD_STALE_AT_MODERATE_PRESSURE_VMMS_RECLAIM_PROPORTION_PARAM: &str =
    "VmmmsReclaimProportion";

// By default attempt to reclaim 100% of the reclaim target from VMMS.
const DISCARD_STALE_AT_MODERATE_PRESSURE_VMMS_RECLAIM_PROPORTION_DEFAULT: u64 = 0;

// The minimum allowed interval between stale tab discard attempts.
const DISCARD_STALE_AT_MODERATE_PRESSURE_MIN_DISCARD_INTERVAL: &str = "MinDiscardIntervalSeconds";
const MIN_DISCARD_INTERVAL_DEFAULT: Duration = Duration::from_secs(10);

const PSI_ADJUST_AVAILABLE_FEATURE_NAME: &str = "CrOSLateBootPsiAdjustAvailable";

const PSI_ADJUST_AVAILABLE_TOP_THRESHOLD_PARAM: &str = "PsiTopThreshold";

const PSI_ADJUST_AVAILABLE_TOP_THRESHOLD_DEFAULT: f32 = 60.0;

const PSI_ADJUST_AVAILABLE_USE_FULL_PARAM: &str = "PsiUseFull";

const PSI_ADJUST_AVAILABLE_COOLDOWN_MILLIS_PARAM: &str = "CooldownMillis";

const PSI_ADJUST_AVAILABLE_COOLDOWN_DEFAULT: Duration = Duration::from_secs(10);

const MGLRU_SPLIT_GENERATIONS: &str = "CrOSLateBootSplitGenerations";

const MGLRU_SPLIT_GENERATIONS_SWAPPINESS: &str = "Swappiness";

const MGLRU_CONSERVATIVE_MM: &str = "CrOSLateBootMglruConservativeMm";

fn update_mglru_sysfs(mask: u64, enable: bool) -> Result<()> {
    const MGLRU_ENABLE_PATH: &str = "/sys/kernel/mm/lru_gen/enabled";

    let enabled_setting: u64 = u64::from_str_radix(
        std::str::from_utf8(
            &std::fs::read(MGLRU_ENABLE_PATH).context("failed to read MGLRU enable state")?,
        )
        .context("malformed kernel output")?
        .trim_start_matches("0x")
        .trim(),
        16,
    )
    .context("non-hex kernel output")?;

    let enabled_setting = if enable {
        enabled_setting | mask
    } else {
        enabled_setting & !mask
    };

    let mut bytes = Vec::new();
    write!(&mut bytes, "0x{:x}", enabled_setting).expect("Failed to format string");
    std::fs::write(MGLRU_ENABLE_PATH, bytes).context("Failed to update MGLRU enable state")?;
    Ok(())
}

fn update_mglru_split_settings(
    enable_split_gens: bool,
    swappiness_config: &SwappinessConfig,
) -> Result<()> {
    let swappiness_val = if enable_split_gens {
        feature::get_feature_param_as::<u32>(
            MGLRU_SPLIT_GENERATIONS,
            MGLRU_SPLIT_GENERATIONS_SWAPPINESS,
        )
        .ok()
        .flatten()
    } else {
        None
    };

    // Set the linked gen bit to the inverse of the split gen experiment state.
    const MGLRU_LINKED_GEN_BIT: u64 = 0x10;
    update_mglru_sysfs(MGLRU_LINKED_GEN_BIT, !enable_split_gens)?;

    swappiness_config.update_default_swappiness(swappiness_val);
    Ok(())
}

pub fn register_features(swappiness: SwappinessConfig) {
    feature::register_feature(VMMMS_RECLAIM_ENTIRE_TARGET_FEATURE_NAME, true, None);

    feature::register_feature(DISCARD_STALE_AT_MODERATE_PRESSURE_FEATURE_NAME, true, None);

    feature::register_feature(PSI_ADJUST_AVAILABLE_FEATURE_NAME, true, None);

    let kernel_version = sys_info::os_release().unwrap_or("".to_string());
    if kernel_version.starts_with("5.15") || kernel_version.starts_with("6.6") {
        feature::register_feature(
            MGLRU_SPLIT_GENERATIONS,
            false,
            Some(Box::new(move |enabled| {
                if let Err(e) = update_mglru_split_settings(enabled, &swappiness) {
                    error!("Failed to update MGLRU split settings {:?}", e);
                }
            })),
        );

        feature::register_feature(
            MGLRU_CONSERVATIVE_MM,
            false,
            Some(Box::new(move |enabled| {
                // Set the aggressive mm bit to the inverse of the split gen experiment state.
                const MGLRU_AGGRESSIVE_MM_BIT: u64 = 0x30;
                if let Err(e) = update_mglru_sysfs(MGLRU_AGGRESSIVE_MM_BIT, !enabled) {
                    error!("Failed to update MGLRU conservative mm {:?}", e);
                }
            })),
        )
    }
}

pub const PSI_MEMORY_POLICY_FEATURE_NAME: &str = "CrOSLateBootPSIMemoryPolicy";

/// calculate_reserved_free_kb() calculates the reserved free memory in KiB from
/// /proc/zoneinfo.  Reserved pages are free pages reserved for emergent kernel
/// allocation and are not available to the user space.  It's the sum of high
/// watermarks and max protection pages of memory zones.  It implements the same
/// reserved pages calculation in linux kernel calculate_totalreserve_pages().
///
/// /proc/zoneinfo example:
/// ...
/// Node 0, zone    DMA32
///   pages free     422432
///         min      16270
///         low      20337
///         high     24404
///         ...
///         protection: (0, 0, 1953, 1953)
///
/// The high field is the high watermark for this zone.  The protection field is
/// the protected pages for lower zones.  See the lowmem_reserve_ratio section
/// in https://www.kernel.org/doc/Documentation/sysctl/vm.txt.
fn calculate_reserved_free_kb<R: BufRead>(reader: R) -> Result<u64> {
    let page_size_kb = 4;
    let mut num_reserved_pages: u64 = 0;

    for line in reader.lines() {
        let line = line?;
        let mut tokens = line.split_whitespace();
        let key = if let Some(k) = tokens.next() {
            k
        } else {
            continue;
        };
        if key == "high" {
            num_reserved_pages += if let Some(v) = tokens.next() {
                v.parse::<u64>()
                    .with_context(|| format!("Couldn't parse the high field: {}", line))?
            } else {
                0
            };
        } else if key == "protection:" {
            num_reserved_pages += tokens.try_fold(0u64, |maximal, token| -> Result<u64> {
                let pattern = &['(', ')', ','][..];
                let num = token
                    .trim_matches(pattern)
                    .parse::<u64>()
                    .with_context(|| format!("Couldn't parse protection field: {}", line))?;
                Ok(std::cmp::max(maximal, num))
            })?;
        }
    }
    Ok(num_reserved_pages * page_size_kb)
}

fn get_reserved_memory_kb() -> Result<u64> {
    // Reserve free pages is high watermark + lowmem_reserve. extra_free_kbytes
    // raises the high watermark.  Nullify the effect of extra_free_kbytes by
    // excluding it from the reserved pages.  The default extra_free_kbytes
    // value is 0 if the file couldn't be accessed.
    let reader = File::open(Path::new("/proc/zoneinfo"))
        .map(BufReader::new)
        .context("Couldn't read /proc/zoneinfo")?;
    Ok(calculate_reserved_free_kb(reader)?
        - read_from_file(&"/proc/sys/vm/extra_free_kbytes").unwrap_or(0))
}

/// Adjusts the memory component according to PSI memory some. When PSI memory some is higher,
/// returns lower memory component in KiB.
fn psi_adjust_memory_kb(memory_component_kb: u64) -> u64 {
    let psi = match procfs::MemoryPressure::new() {
        Ok(psi) => psi,
        Err(e) => {
            error!("procfs::MemoryPressure::new() failed: {}", e);
            return memory_component_kb;
        }
    };
    let psi_avg10 = match feature::get_feature_param_as::<bool>(
        PSI_ADJUST_AVAILABLE_FEATURE_NAME,
        PSI_ADJUST_AVAILABLE_USE_FULL_PARAM,
    ) {
        Ok(Some(true)) => psi.full.avg10,
        _ => psi.some.avg10,
    };

    // If PSI memory some is higher than the top threshold, discount the memory component to 0. If
    // PSI memory some is lower than the bottom threshold, do not discount.
    const PSI_BOTTOM_THRESHOLD: f32 = 5.0;
    let psi_top_threshold = match feature::get_feature_param_as::<f32>(
        PSI_ADJUST_AVAILABLE_FEATURE_NAME,
        PSI_ADJUST_AVAILABLE_TOP_THRESHOLD_PARAM,
    ) {
        Ok(Some(threshold)) => threshold,
        _ => PSI_ADJUST_AVAILABLE_TOP_THRESHOLD_DEFAULT,
    };

    let psi_multiplier = if psi_avg10 >= psi_top_threshold {
        0.0
    } else if psi_avg10 < PSI_BOTTOM_THRESHOLD {
        1.0
    } else {
        // When PSI is between the bottom threshold and top threshold, discount a portion of the
        // reclaimable memory. This portion should be the percentage that current PSI is between
        // the top and bottom thresholds.
        (psi_top_threshold - psi_avg10 as f32) / (psi_top_threshold - PSI_BOTTOM_THRESHOLD)
    };

    let result_f64 = (memory_component_kb as f32) * psi_multiplier;
    result_f64.round() as u64
}

/// calculate_available_memory_kb implements similar available memory
/// calculation as kernel function get_available_mem_adj().  The available memory
/// consists of 3 parts: the free memory, the file cache, and the swappable
/// memory.  The available free memory is free memory minus reserved free memory.
/// The available file cache is the total file cache minus reserved file cache
/// (min_filelist).  Because swapping is prohibited if there is no anonymous
/// memory or no swap free, the swappable memory is the minimal of anonymous
/// memory and swap free.  As swapping memory is more costly than dropping file
/// cache, only a fraction (1 / ram_swap_weight) of the swappable memory
/// contributes to the available memory.
fn calculate_available_memory_kb(
    info: &MemInfo,
    reserved_free: u64,
    min_filelist: u64,
    ram_swap_weight: u64,
    do_thrashing_discount: bool,
) -> u64 {
    let free = info.free;
    let anon = info.active_anon.saturating_add(info.inactive_anon);
    let file = info.active_file.saturating_add(info.inactive_file);
    let dirty = info.dirty;
    let free_component = free.saturating_sub(reserved_free);
    let cache_component = file.saturating_sub(dirty).saturating_sub(min_filelist);
    let swappable = std::cmp::min(anon, info.swap_free);
    let swap_component = if ram_swap_weight != 0 {
        swappable / ram_swap_weight
    } else {
        0
    };
    let reclaimable_component = cache_component.saturating_add(swap_component);
    let reclaimable_adjusted = if do_thrashing_discount {
        // When PSI memory some is high, the cost of reclaim memory is high, discount the
        // reclaimable component of available memory. Do not adjust the free component according
        // to PSI to avoid making free too high.
        psi_adjust_memory_kb(reclaimable_component)
    } else {
        reclaimable_component
    };
    free_component.saturating_add(reclaimable_adjusted)
}

struct MemoryParameters {
    reserved_free: u64,
    min_filelist: u64,
    ram_swap_weight: u64,
}

fn get_memory_parameters() -> MemoryParameters {
    static RESERVED_FREE: Lazy<u64> = Lazy::new(|| match get_reserved_memory_kb() {
        Ok(reserved) => reserved,
        Err(e) => {
            error!("get_reserved_memory_kb failed: {}", e);
            0
        }
    });
    let min_filelist: u64 = read_from_file(&"/proc/sys/vm/min_filelist_kbytes").unwrap_or(0);
    static RAM_SWAP_WEIGHT: Lazy<u64> = Lazy::new(|| {
        read_from_file(
            &Path::new("/")
                .join(RESOURCED_CONFIG_DIR)
                .join(RAM_SWAP_WEIGHT_FILENAME),
        )
        .unwrap_or(DEFAULT_RAM_SWAP_WEIGHT)
    });
    MemoryParameters {
        reserved_free: *RESERVED_FREE,
        min_filelist,
        ram_swap_weight: *RAM_SWAP_WEIGHT,
    }
}

fn get_available_memory_kb(meminfo: &MemInfo, do_thrashing_discount: bool) -> u64 {
    let p = get_memory_parameters();
    calculate_available_memory_kb(
        meminfo,
        p.reserved_free,
        p.min_filelist,
        p.ram_swap_weight,
        do_thrashing_discount,
    )
}

pub fn get_foreground_available_memory_kb(meminfo: &MemInfo) -> u64 {
    get_available_memory_kb(meminfo, false)
}

// |game_mode| is passed rather than implicitly queried. This saves us a query
// (hence a lock) in the case where the caller needs the game mode state for a
// separate purpose (see |get_memory_pressure_status|).
pub fn get_background_available_memory_kb(
    meminfo: &MemInfo,
    game_mode: common::GameMode,
    do_thrashing_discount: bool,
) -> u64 {
    let available = get_available_memory_kb(meminfo, do_thrashing_discount);
    if game_mode != common::GameMode::Off {
        available.saturating_sub(GAME_MODE_OFFSET_KB)
    } else {
        available
    }
}

pub struct MemoryMarginsBps {
    pub moderate: u64,
    pub critical: u64,
    pub critical_protected: u64,
}

#[derive(Clone, Copy)]
pub struct MemoryMarginsKb {
    pub moderate: u64,
    pub critical: u64,
    critical_protected: u64,
}

static MEMORY_MARGINS: Lazy<Mutex<MemoryMarginsKb>> =
    Lazy::new(|| Mutex::new(get_default_memory_margins_kb_impl()));

// Given the total system memory in KB and the basis points for the margin, calculate the absolute
// values in KBs.
fn total_mem_bps_to_kb(total_mem_kb: u64, margin_bps: u64) -> u64 {
    // A basis point is 1/100th of a percent, so we need to convert to whole digit percent and then
    // convert into a fraction of 1, so we divide by 100 twice, ie. 4000bps -> 40% -> .4.
    let total_mem_kb = total_mem_kb as f64;
    let bps = margin_bps as f64;
    (total_mem_kb * (bps / 100.0) / 100.0) as u64
}

fn get_memory_margins_kb_from_bps(margins_bps: MemoryMarginsBps) -> MemoryMarginsKb {
    let total_memory_kb = match MemInfo::load() {
        Ok(meminfo) => meminfo.total,
        Err(e) => {
            error!("Assume 2 GiB total memory if get_meminfo failed: {}", e);
            2 * 1024
        }
    };

    MemoryMarginsKb {
        moderate: total_mem_bps_to_kb(total_memory_kb, margins_bps.moderate),
        critical: total_mem_bps_to_kb(total_memory_kb, margins_bps.critical),
        critical_protected: total_mem_bps_to_kb(total_memory_kb, margins_bps.critical_protected),
    }
}

fn get_default_memory_margins_kb_impl() -> MemoryMarginsKb {
    let margins_bps = MemoryMarginsBps {
        moderate: DEFAULT_MODERATE_MARGIN_BPS,
        critical: DEFAULT_CRITICAL_MARGIN_BPS,
        critical_protected: DEFAULT_CRITICAL_PROTECTED_MARGIN_BPS,
    };
    get_memory_margins_kb_from_bps(margins_bps)
}

pub fn get_memory_margins_kb() -> MemoryMarginsKb {
    let data = MEMORY_MARGINS.do_lock();
    *data
}

pub fn set_memory_margins_bps(margins_bps: MemoryMarginsBps) {
    let mut data = MEMORY_MARGINS.do_lock();
    *data = get_memory_margins_kb_from_bps(margins_bps);
}

pub struct ArcMarginsKb {
    pub foreground: u64,
    pub perceptible: u64,
    pub cached: u64,
}

pub struct ComponentMarginsKb {
    pub chrome_moderate: u64,
    pub chrome_critical: u64,
    pub chrome_critical_protected: u64,
    pub arcvm: ArcMarginsKb,
    pub arc_container: ArcMarginsKb,
}

impl ComponentMarginsKb {
    fn compute_chrome_pressure(&self, available: u64) -> (PressureLevelChrome, u64) {
        if available < self.chrome_critical_protected {
            (
                PressureLevelChrome::Critical,
                // Computing the amount to free with the critical margin may discard protected tabs
                // between the critical protected and critical margins, but we need to get the
                // device back into a better state.
                self.chrome_critical - available,
            )
        } else if available < self.chrome_critical {
            (
                PressureLevelChrome::DiscardUnprotected,
                self.chrome_critical - available,
            )
        } else if available < self.chrome_moderate {
            (
                PressureLevelChrome::Moderate,
                self.chrome_moderate - available,
            )
        } else {
            (PressureLevelChrome::None, 0)
        }
    }
}

pub fn get_component_margins_kb() -> ComponentMarginsKb {
    let margins_kb = get_memory_margins_kb();
    ComponentMarginsKb {
        chrome_moderate: margins_kb.moderate,

        chrome_critical: margins_kb.critical,

        chrome_critical_protected: margins_kb.critical_protected,

        arcvm: ArcMarginsKb {
            // 75 % of critical.
            foreground: margins_kb.critical * 3 / 4,

            // 100 % of critical.
            perceptible: margins_kb.critical,

            cached: 2 * margins_kb.critical,
        },

        arc_container: ArcMarginsKb {
            // Don't kill ARC container foreground process. It might be supported in the future.
            foreground: 0,

            perceptible: margins_kb.critical,

            // Minimal of moderate and 200 % of critical.
            cached: std::cmp::min(margins_kb.moderate, 2 * margins_kb.critical),
        },
    }
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum PressureLevelChrome {
    // There is enough memory to use.
    None,
    // Chrome is advised to free buffers that are cheap to re-allocate and not
    // immediately needed.
    Moderate,
    // Chrome is advised to discard unprotected tabs to free memory.
    DiscardUnprotected,
    // Chrome is advised to free all possible memory.
    Critical,
}

// The pressure level parameter for the D-Bus Chrome memory pressure signal.
enum PressureLevelChromeDbus {
    // There is enough memory to use.
    None = 0,
    // Chrome is advised to free buffers that are cheap to re-allocate and not
    // immediately needed.
    Moderate = 1,
    // Chrome is advised to free all possible memory.
    Critical = 2,
}

// The discard type parameter for the D-Bus Chrome memory pressure signal.
enum DiscardTypeDbus {
    // Only unprotected pages can be discarded.
    Unprotected = 0,
    // Both unprotected and protected pages can be discarded.
    Protected = 1,
}

impl PressureLevelChrome {
    // Returns the pressure level and the discard type for the Chrome memory pressure signal.
    pub fn to_dbus_params(&self) -> (u8, u8) {
        match self {
            PressureLevelChrome::None => (
                PressureLevelChromeDbus::None as u8,
                DiscardTypeDbus::Unprotected as u8,
            ),
            PressureLevelChrome::Moderate => (
                PressureLevelChromeDbus::Moderate as u8,
                DiscardTypeDbus::Unprotected as u8,
            ),
            PressureLevelChrome::DiscardUnprotected => (
                PressureLevelChromeDbus::Critical as u8,
                DiscardTypeDbus::Unprotected as u8,
            ),
            PressureLevelChrome::Critical => (
                PressureLevelChromeDbus::Critical as u8,
                DiscardTypeDbus::Protected as u8,
            ),
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd)]
pub enum PressureLevelArcContainer {
    // There is enough memory to use.
    None = 0,
    // ARC container is advised to kill cached processes to free memory.
    Cached = 1,
    // ARC container is advised to kill perceptible processes to free memory.
    Perceptible = 2,
    // ARC container is advised to kill foreground processes to free memory.
    Foreground = 3,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub struct PressureStatus {
    pub chrome_level: PressureLevelChrome,
    pub chrome_reclaim_target_kb: u64,
    pub arc_container_level: PressureLevelArcContainer,
    pub arc_container_reclaim_target_kb: u64,
}

macro_rules! get_arc_level {
    ($fn:ident, $ret_type:ty) => {
        fn $fn(margins: &ArcMarginsKb, available: u64, background_memory: u64) -> ($ret_type, u64) {
            // We should kill background tabs before foreground/perceptible ARC apps, so
            // background memory is counted as available for those ARC pressure levels.
            if available + background_memory < margins.foreground {
                (<$ret_type>::Foreground, margins.foreground - available)
            } else if available + background_memory < margins.perceptible {
                (<$ret_type>::Perceptible, margins.perceptible - available)
            } else if available < margins.cached {
                (<$ret_type>::Cached, margins.cached - available)
            } else {
                (<$ret_type>::None, 0)
            }
        }
    };
}

get_arc_level!(get_arc_container_level, PressureLevelArcContainer);

// Should only be called when the Chrome pressure level is moderate.
// If necessary, attempts to reclaim memory from VMMS in concierge at
// the appropriate priority for moderate memory pressure.
async fn try_vmms_reclaim_memory_moderate(
    vmms_client: &VmMemoryManagementClient,
    reclaim_target: u64,
) -> u64 {
    // If the discard stale at moderate feature is enabled,
    // attempt to reclaim from vmms at stale cached tab priority.
    // Note that this reclaim is attempted regardless of if there
    // is actually any stale cached tab memory from Chrome.
    // There may still be stale memory to clean up in other
    // contexts (i.e. ARCVM).

    // In some cases it might be better to only reclaim a
    // portion of the target from VMMS to better balance
    // memory pressure.
    let vmms_reclaim_proportion = match feature::get_feature_param_as::<u64>(
        DISCARD_STALE_AT_MODERATE_PRESSURE_FEATURE_NAME,
        DISCARD_STALE_AT_MODERATE_PRESSURE_VMMS_RECLAIM_PROPORTION_PARAM,
    ) {
        // Quick sanity check on the feature param value.
        Ok(Some(v)) => {
            if v <= 100 {
                v
            } else {
                DISCARD_STALE_AT_MODERATE_PRESSURE_VMMS_RECLAIM_PROPORTION_DEFAULT
            }
        }
        _ => DISCARD_STALE_AT_MODERATE_PRESSURE_VMMS_RECLAIM_PROPORTION_DEFAULT,
    };

    let adjusted_reclaim_target = (reclaim_target * vmms_reclaim_proportion) / 100;

    if adjusted_reclaim_target > 0 {
        vmms_client
            .try_reclaim_memory(
                adjusted_reclaim_target,
                ResizePriority::RESIZE_PRIORITY_STALE_CACHED_TAB,
            )
            .await
    } else {
        0
    }
}

// Should only be called when the Chrome pressure level is critical.
// Attempts to reclaim memory from VMMS in concierge at the appropriate priorities for
// critical memory pressure.
// Returns the total amount of memory reclaimed by VMMS.
async fn try_vmms_reclaim_memory_critical(
    vmms_client: &VmMemoryManagementClient,
    chrome_level: PressureLevelChrome,
    mut reclaim_target: u64,
    chrome_background_memory: u64,
    game_mode: common::GameMode,
) -> u64 {
    let reclaim_entire_target =
        feature::is_feature_enabled(VMMMS_RECLAIM_ENTIRE_TARGET_FEATURE_NAME).unwrap_or(false);

    // Tell VM Memory Management Service to reclaim memory from guests to
    // try to save the background tabs.
    let cached_target = if reclaim_entire_target {
        reclaim_target
    } else {
        std::cmp::min(chrome_background_memory, reclaim_target)
    };

    let cached_actual = if cached_target > 0 {
        vmms_client
            .try_reclaim_memory(cached_target, ResizePriority::RESIZE_PRIORITY_CACHED_TAB)
            .await
    } else {
        0
    };

    // If the needed memory is less than what was reclaimed from guests plus what Chrome
    // will reclaim from background tabs, then no need for higher priority requests.
    reclaim_target = reclaim_target.saturating_sub(cached_actual + chrome_background_memory);
    if reclaim_target == 0 {
        return cached_actual;
    }

    // Don't try to reclaim memory at higher than cached priority when in ARC
    // game mode, to make sure we don't kill the game or something it depends upon.
    if game_mode == common::GameMode::Arc {
        return cached_actual;
    }
    // Don't try to reclaim memory at higher than cached priority when the discard type is
    // unprotected tabs only.
    if chrome_level < PressureLevelChrome::Critical {
        return cached_actual;
    }

    // Tell VM MMS to reclaim memory at perceptible priority for any protected tabs
    // that Chrome will kill after it kills all background tabs.
    let protected_memory = get_chrome_memory_kb(ChromeProcessType::Protected, None, reclaim_target);
    let perceptible_actual = if protected_memory > 0 {
        let perceptible_target = if reclaim_entire_target {
            reclaim_target
        } else {
            std::cmp::min(protected_memory, reclaim_target)
        };

        vmms_client
            .try_reclaim_memory(
                perceptible_target,
                ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_TAB,
            )
            .await
    } else {
        0
    };

    // If the reclaimed perceptible guest plus protected tabs still isn't enough,
    // we don't have anything else to reclaim. Tell that to VM MMS.
    reclaim_target = reclaim_target.saturating_sub(protected_memory + perceptible_actual);
    if reclaim_target > 0 {
        vmms_client.send_no_kill_candidates().await;
    }

    cached_actual + perceptible_actual
}

// Attempts to reclaim memory from VMMS in concierge at the correct priority according to
// the current memory pressure level and reclaim targets.
// Returns the total amount of memory reclaimed from VMMMS.
async fn try_vmms_reclaim_memory(
    vmms_client: &VmMemoryManagementClient,
    chrome_level: PressureLevelChrome,
    reclaim_target: u64,
    chrome_background_memory: u64,
    game_mode: common::GameMode,
    discard_stale_at_moderate: bool,
) -> u64 {
    let now = Instant::now();

    // When the vmms client is not connected, nothing can be reclaimed.
    if !vmms_client.is_active() {
        return 0;
    }

    let vmms_reclaim_actual = match chrome_level {
        // When there is no memory pressure, never attempt to reclaim from VMMS.
        PressureLevelChrome::None => {
            return 0;
        }

        PressureLevelChrome::Moderate => {
            // If the discard stale at moderate feature is not enabled,
            // do not attempt to reclaim at moderate memory pressure.
            if !discard_stale_at_moderate {
                return 0;
            }

            try_vmms_reclaim_memory_moderate(vmms_client, reclaim_target).await
        }

        PressureLevelChrome::DiscardUnprotected | PressureLevelChrome::Critical => {
            try_vmms_reclaim_memory_critical(
                vmms_client,
                chrome_level,
                reclaim_target,
                chrome_background_memory,
                game_mode,
            )
            .await
        }
    };

    if let Err(e) = report_vmms_reclaim_memory_duration(chrome_level, now.elapsed()) {
        error!("Failed to report try_vmms_reclaim_memory duration {:?}", e);
    }

    vmms_reclaim_actual
}

fn report_vmms_reclaim_memory_duration(
    chrome_level: PressureLevelChrome,
    duration: Duration,
) -> Result<()> {
    const DURATION_BASE: &str = "Platform.Resourced.VmmsReclaimMemoryDuration.";
    const MODERATE: &str = "Moderate";
    const CRITICAL: &str = "Critical";

    metrics::send_to_uma(
        &format!(
            "{}{}",
            DURATION_BASE,
            match chrome_level {
                // No VMMS reclaim for none level.
                PressureLevelChrome::None => {
                    return Ok(());
                }
                PressureLevelChrome::Moderate => MODERATE,
                PressureLevelChrome::DiscardUnprotected | PressureLevelChrome::Critical => CRITICAL,
            }
        ), // Metric name
        duration.as_millis() as i32, // Sample
        0,                           // Min
        5000,                        // Max
        50,                          // Number of buckets
    )
}

fn get_min_stale_discard_interval() -> Duration {
    let min_discard_interval = get_individual_duration_param(
        DISCARD_STALE_AT_MODERATE_PRESSURE_FEATURE_NAME,
        DISCARD_STALE_AT_MODERATE_PRESSURE_MIN_DISCARD_INTERVAL,
    );

    let Ok(min_discard_interval) = min_discard_interval else {
        return MIN_DISCARD_INTERVAL_DEFAULT;
    };

    min_discard_interval
}

// The time of the last stale tab discard attempt.
static LAST_STALE_TAB_DISCARD: Mutex<Option<Instant>> = Mutex::new(None);

fn try_discard_stale_at_moderate(
    margins: &ComponentMarginsKb,
    chrome_level: PressureLevelChrome,
    chrome_reclaim_target_kb: u64,
) -> (PressureLevelChrome, u64) {
    let stale_tab_cutoff =
        get_stale_tab_age_cutoff(margins, chrome_level, chrome_reclaim_target_kb);

    // Only one stale tab is discarded at a time, so using a threshold of 1 is fine.
    // get_chrome_memory_kb will still return the size of the most stale tab (if any).
    let stale_background_memory_kb =
        get_chrome_memory_kb(ChromeProcessType::Background, Some(stale_tab_cutoff), 1);

    let mut last_discard_time = LAST_STALE_TAB_DISCARD.do_lock();
    let min_stale_discard_interval = get_min_stale_discard_interval();

    // If chrome pressure is Moderate, and there are stale background tabs, send a
    // one-off critical memory pressure signal to clear out a stale background tab.
    // This signal is rate-limited will only be sent if the min discard interval has
    // elapsed since the previous attempt.
    if chrome_level == PressureLevelChrome::Moderate
        && stale_background_memory_kb > 0
        && last_discard_time.map_or(Duration::MAX, |d| d.elapsed()) > min_stale_discard_interval
    {
        info!(
            "Discarding single tab at moderate memory pressure.
             Stale Tab Cutoff: {}s Stale Background Memory: {}kb",
            stale_tab_cutoff.as_secs(),
            stale_background_memory_kb
        );
        *last_discard_time = Some(Instant::now());
        (
            PressureLevelChrome::DiscardUnprotected,
            // Regardless of what the reclaim target is Chrome will kill at least one tab.
            // Since this isn't critical memory pressure and memory doesn't need to be
            // freed quickly, just send '1' so that a single stale tab will be discarded.
            1,
        )
    } else {
        (chrome_level, chrome_reclaim_target_kb)
    }
}

static LAST_THRASHING_DISCOUNT_CRITICAL_PRESSURE_AT: Mutex<Option<Instant>> = Mutex::new(None);

fn should_do_thrashing_discount() -> bool {
    if !feature::is_feature_enabled(PSI_ADJUST_AVAILABLE_FEATURE_NAME).unwrap_or(false) {
        return false;
    }
    if let Some(last_critical_at) = *LAST_THRASHING_DISCOUNT_CRITICAL_PRESSURE_AT.do_lock() {
        let cooldown_duration = match feature::get_feature_param_as::<u64>(
            PSI_ADJUST_AVAILABLE_FEATURE_NAME,
            PSI_ADJUST_AVAILABLE_COOLDOWN_MILLIS_PARAM,
        ) {
            Ok(Some(millis)) => Duration::from_millis(millis),
            _ => PSI_ADJUST_AVAILABLE_COOLDOWN_DEFAULT,
        };
        // Skip considering adjusting swap memory component based on PSI just after sending critical
        // pressure signal to avoid discarding too many tabs because:
        // * There can be a delay between the critical signal is sent and the tabs are actually
        //   discarded.
        // * 10 second average of PSI can contain high value which is before discarding.
        if Instant::now().duration_since(last_critical_at) <= cooldown_duration {
            return false;
        }
    }
    // If there is protected processes only, we need to use PressureLevelChrome::Critical to discard
    // protected tabs. However PressureLevelChrome::Critical need to be judged by conservative
    // available memory size.
    background_tabs_exist()
}

pub async fn get_memory_pressure_status(
    vmms_client: &VmMemoryManagementClient,
) -> Result<PressureStatus> {
    let game_mode = common::get_game_mode();
    let meminfo = MemInfo::load().context("load meminfo")?;
    let margins = get_component_margins_kb();

    let is_high_ram = meminfo.total > DISCARD_STALE_AT_MODERATE_HIGH_RAM_CUTOFF_KB;

    // Never enable moderate pressure discards on devices with high ram.
    let discard_stale_at_moderate = !is_high_ram
        && feature::is_feature_enabled(DISCARD_STALE_AT_MODERATE_PRESSURE_FEATURE_NAME)?;

    let do_thrashing_discount = should_do_thrashing_discount();
    let available = get_background_available_memory_kb(&meminfo, game_mode, do_thrashing_discount);
    let (mut raw_chrome_level, raw_chrome_reclaim_target_kb) =
        margins.compute_chrome_pressure(available);

    if do_thrashing_discount && raw_chrome_level > PressureLevelChrome::DiscardUnprotected {
        // Avoid discarding protected tabs based on non-conservative available memory size.
        //
        // Even if the actual memory pressure is critical enough without thrashing discount to
        // discard protected tabs, PressureLevelChrome::DiscardUnprotected can discard some tabs
        // because should_do_thrashing_discount() checks there is at least 1 background tabs.
        // The background tabs may not be enough to satisfy reclaiming target. But protected tabs
        // will be discarded in the next round of get_memory_pressure_status() which is called by
        // margin_memory_handler_loop() every 1 second under high memory pressure anyway.
        raw_chrome_level = PressureLevelChrome::DiscardUnprotected;
    }

    // We cap ARC pressure levels at cached if reclaiming Chrome background memory will
    // be sufficient to push us back over the ARC perceptible margin. Compute the
    // maximum we need to reclaim to short circult Chrome background memory measurement.
    let arcvm_perceptible_target = margins.arcvm.perceptible.saturating_sub(available);
    let arc_container_perceptible_target =
        margins.arc_container.perceptible.saturating_sub(available);
    let max_target = arcvm_perceptible_target
        .max(arc_container_perceptible_target)
        .max(raw_chrome_reclaim_target_kb);
    let background_memory_kb =
        get_chrome_memory_kb(ChromeProcessType::Background, None, max_target);

    let vmms_reclaim_kb = try_vmms_reclaim_memory(
        vmms_client,
        raw_chrome_level,
        raw_chrome_reclaim_target_kb,
        background_memory_kb,
        game_mode,
        discard_stale_at_moderate,
    )
    .await;

    let (after_vmms_chrome_level, after_vmms_chrome_reclaim_target_kb) =
        margins.compute_chrome_pressure(available + vmms_reclaim_kb);
    // Ensure that new chrome_level is lower than or equals to the original chrome level. The
    // inversion can happen if raw_chrome_level is adjusted due to non-conservative available
    // memory.
    let after_vmms_chrome_level = std::cmp::min(after_vmms_chrome_level, raw_chrome_level);

    let (arc_container_level, arc_container_reclaim_target_kb) =
        get_arc_container_level(&margins.arc_container, available, background_memory_kb);

    let (final_chrome_level, final_chrome_reclaim_target_kb) = if discard_stale_at_moderate {
        try_discard_stale_at_moderate(
            &margins,
            after_vmms_chrome_level,
            after_vmms_chrome_reclaim_target_kb,
        )
    } else {
        (after_vmms_chrome_level, after_vmms_chrome_reclaim_target_kb)
    };

    if do_thrashing_discount && final_chrome_level > PressureLevelChrome::Moderate {
        *LAST_THRASHING_DISCOUNT_CRITICAL_PRESSURE_AT.do_lock() = Some(Instant::now());
    }

    Ok(PressureStatus {
        chrome_level: final_chrome_level,
        chrome_reclaim_target_kb: final_chrome_reclaim_target_kb,
        arc_container_level,
        arc_container_reclaim_target_kb,
    })
}

pub fn init_memory_configs() -> Result<()> {
    init_memory_configs_impl(Path::new("/"))
}

/// Initialize the margins file if it doesn't exist.
/// These files are used as the default values.
fn init_memory_configs_impl(root: &Path) -> Result<()> {
    // Checks the config directory.
    let config_path = root.join(RESOURCED_CONFIG_DIR);
    if !config_path.exists() {
        bail!(
            "The config directory {} doesn't exist.",
            config_path.display()
        );
    } else if !config_path.is_dir() {
        bail!(
            "The config directory {} is not a directory.",
            config_path.display()
        );
    }

    // Creates the memory ram_swap_weight config file.
    let ram_swap_weight_path = config_path.join(RAM_SWAP_WEIGHT_FILENAME);
    if !ram_swap_weight_path.exists() {
        let mut ram_swap_weight_file = File::create(ram_swap_weight_path)?;

        ram_swap_weight_file.write_all(DEFAULT_RAM_SWAP_WEIGHT.to_string().as_bytes())?;
    } else if !ram_swap_weight_path.is_file() {
        bail!(
            "The ram swap weight path {} is not a regular file.",
            ram_swap_weight_path.display()
        );
    }

    Ok(())
}

// The browser type of the process list.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum BrowserType {
    // Ash Chrome.
    Ash = 0,
    // Lacros Chrome.
    Lacros = 1,
}

impl fmt::Display for BrowserType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            BrowserType::Ash => write!(f, "Ash"),
            BrowserType::Lacros => write!(f, "Lacros"),
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum ChromeProcessType {
    Background,
    Protected,
}

impl fmt::Display for ChromeProcessType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ChromeProcessType::Background => write!(f, "Background"),
            ChromeProcessType::Protected => write!(f, "Protected"),
        }
    }
}

#[derive(Copy, Clone)]
pub struct TabProcess {
    pub pid: i32,
    pub last_visible: Instant,
}

// Lists of the processes to estimate the host memory usage.
static CHROME_TAB_PROCESSES: Mutex<BTreeMap<(BrowserType, ChromeProcessType), Vec<TabProcess>>> =
    Mutex::new(BTreeMap::new());

// Returns the process list for a given browser/process type pair
fn get_chrome_tab_processes(
    browser_type: BrowserType,
    process_type: ChromeProcessType,
    min_last_visible_age: Option<Duration>,
) -> Result<Vec<TabProcess>> {
    // Panic on poisoned mutex.
    let all_tab_processes = CHROME_TAB_PROCESSES.do_lock();
    let Some(filtered_process_list) = all_tab_processes.get(&(browser_type, process_type)) else {
        // Returns empty list if process list is not present.
        // E.g., When Lacros Chrome is not running.
        return Ok(Vec::new());
    };

    let now = Instant::now();

    return match min_last_visible_age {
        Some(duration) => Ok(filtered_process_list
            .iter()
            .filter(|tab_process| now - tab_process.last_visible >= duration)
            .cloned()
            .collect()),
        None => Ok(filtered_process_list.clone()),
    };
}

fn background_tabs_exist() -> bool {
    let all_tab_processes = CHROME_TAB_PROCESSES.do_lock();
    all_tab_processes
        .iter()
        .map(|((_, process_type), processes)| {
            if process_type == &ChromeProcessType::Background {
                !processes.is_empty()
            } else {
                false
            }
        })
        .any(|v| v)
}

pub fn set_browser_tab_processes(
    browser_type: BrowserType,
    background_tab_processes: Vec<TabProcess>,
    protected_tab_processes: Vec<TabProcess>,
) {
    let mut chrome_tab_processes = CHROME_TAB_PROCESSES.do_lock();
    chrome_tab_processes.insert(
        (browser_type, ChromeProcessType::Background),
        background_tab_processes,
    );
    chrome_tab_processes.insert(
        (browser_type, ChromeProcessType::Protected),
        protected_tab_processes,
    );
}

// Returns the amount of memory in KiB of the given chrome process type where the
// last visible time of the tab is older than |min_last_visible_age|. To reduce
// the work when there are a lot of chrome processes, it would stop counting if the
// memory exceeds |threshold_kb|. When |threshold_kb| is 0, it returns 0.
fn get_chrome_memory_kb(
    process_type: ChromeProcessType,
    min_last_visible_age: Option<Duration>,
    threshold_kb: u64,
) -> u64 {
    if threshold_kb == 0 {
        return 0;
    }

    let mut total_background_memory_kb = 0;
    for browser_type in [BrowserType::Ash, BrowserType::Lacros] {
        let tab_processes =
            match get_chrome_tab_processes(browser_type, process_type, min_last_visible_age) {
                Ok(tab_processes) => tab_processes,
                Err(e) => {
                    error!(
                        "Failed to get chrome {} {} tabs: {}",
                        browser_type, process_type, e
                    );
                    continue;
                }
            };
        for tab_process in tab_processes {
            match get_chrome_process_memory_usage(tab_process.pid) {
                Ok(result) => {
                    total_background_memory_kb += result;
                    if total_background_memory_kb > threshold_kb {
                        return total_background_memory_kb;
                    }
                }
                Err(e) => {
                    // It's Ok to continue when failed to get memory usage for a pid.
                    // When get_chrome_process_memory_usage() failed, total_background_memory_kb
                    // would be less and the pressure level would tend to be unmodified.
                    error!(
                        "Failed to get memory usage, pid: {}, error: {}",
                        tab_process.pid, e
                    );
                }
            }
        }
    }
    total_background_memory_kb
}

// Memory usage estimation of |pid|, it's the sum of anonymous RSS and swap.
// Returns error if the program name (Name in /proc/PID/status) is not chrome. The program name
// should be chrome for both Ash and Lacros Chrome.
fn get_chrome_process_memory_usage(pid: i32) -> Result<u64> {
    let process = match procfs::process::Process::new(pid) {
        Ok(p) => p,
        Err(_) => {
            // Returns 0 if the /proc/PID doesn't exist.
            return Ok(0);
        }
    };

    let status = process.status()?;
    if status.name.ne("chrome") {
        bail!("The program name {} is not chrome", status.name);
    }
    let rssanon = status
        .rssanon
        .with_context(|| format!("Couldn't get the RssAnon field in /proc/{}/status", pid))?;
    let vmswap = status
        .vmswap
        .with_context(|| format!("Couldn't get the VmSwap field in /proc/{}/status", pid))?;
    Ok(rssanon + vmswap)
}

fn get_stale_tab_age_cutoff(
    margins: &ComponentMarginsKb,
    chrome_level: PressureLevelChrome,
    reclaim_target_kb: u64,
) -> Duration {
    // If there is no memory pressure, nothing is treated as stale.
    // Return the max cutoff value to exclude everything.
    if chrome_level == PressureLevelChrome::None {
        return Duration::MAX;
    }

    // For critical memory pressure, do not exclude anything, so return the
    // zero duration.
    if chrome_level == PressureLevelChrome::Critical {
        return Duration::ZERO;
    }

    let (min_last_visible, max_last_visible) = get_last_visible_threshold_values();

    // Find the actual cutoff as a linear scale between the min and max values above
    // in relation to the moderate memory pressure range.
    // Example:
    //                   RAM Usage
    // 0GB........................................4GB
    // |------None------||---Moderate---||-Critical-|
    //                               ^
    //                         Current Usage
    // In this case, used RAM is 80% of the way through the moderate memory
    // pressure range.
    // Return the value that is 80% of the way between min_last_visible and max_last_visible.
    let percent_into_moderate =
        ((reclaim_target_kb * 100) / (margins.chrome_moderate - margins.chrome_critical)) as u32;

    max_last_visible - ((max_last_visible - min_last_visible) * percent_into_moderate) / 100
}

#[cfg(not(test))]
fn get_last_visible_threshold_values() -> (Duration, Duration) {
    // By default, use the same threshold values as Chrome memory saver which is
    // 2 hours regardless of memory pressure level.
    const LAST_VISIBLE_THRESHOLD_DEFAULT: Duration = Duration::from_secs(2 * 60 * 60);

    let min_last_visible = get_individual_duration_param(
        DISCARD_STALE_AT_MODERATE_PRESSURE_FEATURE_NAME,
        DISCARD_STALE_AT_MODERATE_PRESSURE_MIN_VISIBLE_SECONDS_THRESHOLD_PARAM,
    );
    let max_last_visible = get_individual_duration_param(
        DISCARD_STALE_AT_MODERATE_PRESSURE_FEATURE_NAME,
        DISCARD_STALE_AT_MODERATE_PRESSURE_MAX_VISIBLE_SECONDS_THRESHOLD_PARAM,
    );

    let (Ok(min_last_visible), Ok(max_last_visible)) = (min_last_visible, max_last_visible) else {
        return (
            LAST_VISIBLE_THRESHOLD_DEFAULT,
            LAST_VISIBLE_THRESHOLD_DEFAULT,
        );
    };

    // Sanity check for the cutoffs since the threshold calculation requires
    // min to be less than max.
    // Min and max being the same is fine since we may want to test a constant cutoff.
    if min_last_visible > max_last_visible {
        return (
            LAST_VISIBLE_THRESHOLD_DEFAULT,
            LAST_VISIBLE_THRESHOLD_DEFAULT,
        );
    }

    (min_last_visible, max_last_visible)
}

fn get_individual_duration_param(feature_name: &str, param_name: &str) -> Result<Duration> {
    let threshold_seconds = feature::get_feature_param_as::<u64>(feature_name, param_name)?
        .context("No valid feature param")?;

    Ok(Duration::from_secs(threshold_seconds))
}

// For tests, have non-default min/max values.
#[cfg(test)]
fn get_last_visible_threshold_values() -> (Duration, Duration) {
    (
        Duration::from_secs(5 * 60),
        Duration::from_secs(2 * 60 * 60),
    )
}

#[cfg(test)]
mod tests {
    use std::fs::OpenOptions;

    use tempfile::tempdir;

    use super::*;

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
        let available = calculate_available_memory_kb(
            &info,
            reserved_free,
            min_filelist,
            ram_swap_weight,
            false,
        );
        assert_eq!(available, file - min_filelist - info.dirty);

        // Available determined by swap free.
        info.swap_free = 1200 * 1024;
        info.active_anon = 1000 * 1024;
        info.inactive_anon = 1000 * 1024;
        info.active_file = 0;
        info.inactive_file = 0;
        info.dirty = 0;
        let available = calculate_available_memory_kb(
            &info,
            reserved_free,
            min_filelist,
            ram_swap_weight,
            false,
        );
        assert_eq!(available, info.swap_free / ram_swap_weight);

        // Available determined by anonymous.
        info.swap_free = 6000 * 1024;
        info.active_anon = 500 * 1024;
        info.inactive_anon = 500 * 1024;
        let anon = info.active_anon + info.inactive_anon;
        let available = calculate_available_memory_kb(
            &info,
            reserved_free,
            min_filelist,
            ram_swap_weight,
            false,
        );
        assert_eq!(available, anon / ram_swap_weight);

        // When ram_swap_weight is 0, swap is ignored in available.
        info.swap_free = 1200 * 1024;
        info.active_anon = 1000 * 1024;
        info.inactive_anon = 1000 * 1024;
        info.active_file = 500 * 1024;
        info.inactive_file = 500 * 1024;
        let file = info.active_file + info.inactive_file;
        let ram_swap_weight = 0;
        let available = calculate_available_memory_kb(
            &info,
            reserved_free,
            min_filelist,
            ram_swap_weight,
            false,
        );
        assert_eq!(available, file - min_filelist);
    }

    #[test]
    fn test_total_mem_bps_to_kb() {
        let margin1 = total_mem_bps_to_kb(100000 /* 100mb */, 1200 /* 12% */);
        assert_eq!(margin1, 12000 /* 12mb */);

        let margin2 = total_mem_bps_to_kb(100000 /* 100mb */, 3600 /* 36% */);
        assert_eq!(margin2, 36000 /* 36mb */);

        let margin3 = total_mem_bps_to_kb(1000000 /* 1000mb */, 1250 /* 12.50% */);
        assert_eq!(margin3, 125000 /* 125mb */);

        let margin4 = total_mem_bps_to_kb(1000000 /* 1000mb */, 7340 /* 73.4% */);
        assert_eq!(margin4, 734000 /* 734mb */);
    }

    #[test]
    fn test_init_memory_configs_not_dir() {
        let root = tempdir().unwrap();
        std::fs::create_dir(root.path().join("run")).unwrap();
        // Touches /run/resourced.
        OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(root.path().join(RESOURCED_CONFIG_DIR))
            .unwrap();
        // Returns error when /run/resourced is not a directory.
        assert!(init_memory_configs_impl(root.path()).is_err());
    }

    #[test]
    fn test_init_memory_configs_create_margins() {
        let root = tempdir().unwrap();
        std::fs::create_dir_all(root.path().join(RESOURCED_CONFIG_DIR)).unwrap();
        // Checks that config dir already exists. Creates the margins file.
        assert!(init_memory_configs_impl(root.path()).is_ok());
    }

    #[test]
    fn test_init_memory_configs_ram_swap_weight_exists() {
        let root = tempdir().unwrap();
        std::fs::create_dir_all(root.path().join(RESOURCED_CONFIG_DIR)).unwrap();
        let mut ram_swap_weight_file = File::create(
            root.path()
                .join(RESOURCED_CONFIG_DIR)
                .join(RAM_SWAP_WEIGHT_FILENAME),
        )
        .unwrap();
        ram_swap_weight_file.write_all("2".as_bytes()).unwrap();
        assert!(init_memory_configs_impl(root.path()).is_ok());
    }

    #[test]
    fn test_init_memory_configs_ram_swap_weight_is_dir() {
        let root = tempdir().unwrap();
        std::fs::create_dir_all(
            root.path()
                .join(RESOURCED_CONFIG_DIR)
                .join(RAM_SWAP_WEIGHT_FILENAME),
        )
        .unwrap();
        assert!(init_memory_configs_impl(root.path()).is_err());
    }

    #[test]
    fn test_get_background_available_memory_kb() {
        let p = get_memory_parameters();
        let meminfo = MemInfo {
            free: 400 * 1024 + p.reserved_free,
            ..Default::default()
        };
        assert_eq!(
            get_background_available_memory_kb(&meminfo, common::GameMode::Off, false),
            400 * 1024
        );
        assert_eq!(
            get_background_available_memory_kb(&meminfo, common::GameMode::Arc, false),
            400 * 1024 - GAME_MODE_OFFSET_KB
        );
        assert_eq!(
            get_background_available_memory_kb(&meminfo, common::GameMode::Borealis, false),
            400 * 1024 - GAME_MODE_OFFSET_KB
        );

        // When available memory is less than GAME_MODE_OFFSET_KB.
        let meminfo = MemInfo {
            free: 200 * 1024 + p.reserved_free,
            ..Default::default()
        };
        assert_eq!(
            get_background_available_memory_kb(&meminfo, common::GameMode::Off, false),
            200 * 1024
        );
        assert_eq!(
            get_background_available_memory_kb(&meminfo, common::GameMode::Arc, false),
            0
        );
    }

    #[test]
    fn test_get_stale_tab_age_cutoff() {
        let margins = ComponentMarginsKb {
            chrome_moderate: 5000,
            chrome_critical: 1000,
            chrome_critical_protected: 1000,
            arc_container: ArcMarginsKb {
                foreground: 0,
                perceptible: 0,
                cached: 0,
            },
            arcvm: ArcMarginsKb {
                foreground: 0,
                perceptible: 0,
                cached: 0,
            },
        };

        // With no memory pressure, the max duration should be returned.
        let stale_tab_cutoff = get_stale_tab_age_cutoff(&margins, PressureLevelChrome::None, 0);
        assert!(stale_tab_cutoff == Duration::MAX);

        // At the very beginning of moderate memory pressure, the max stale target should be
        // returned.
        let stale_tab_cutoff = get_stale_tab_age_cutoff(&margins, PressureLevelChrome::Moderate, 0);
        assert!(stale_tab_cutoff == Duration::from_secs(7200));

        // At moderate memory pressure with a reclaim target of 1000, the pressure level is 25% of
        // the way into moderate. Therefore, the stale tab cutoff should be 25% of the way between
        // 7200 seconds an 300 seconds.
        let stale_tab_cutoff =
            get_stale_tab_age_cutoff(&margins, PressureLevelChrome::Moderate, 1000);
        assert!(stale_tab_cutoff == Duration::from_secs(5475));

        // Halfway through moderate memory pressure should be halfway between 7200 and 300.
        let stale_tab_cutoff =
            get_stale_tab_age_cutoff(&margins, PressureLevelChrome::Moderate, 2000);
        assert!(stale_tab_cutoff == Duration::from_secs(3750));

        // 75% through moderate memory pressure should be 75% between 7200 and 300.
        let stale_tab_cutoff =
            get_stale_tab_age_cutoff(&margins, PressureLevelChrome::Moderate, 3000);
        assert!(stale_tab_cutoff == Duration::from_secs(2025));

        // All the way through should be the minimum stale value.
        let stale_tab_cutoff =
            get_stale_tab_age_cutoff(&margins, PressureLevelChrome::Moderate, 4000);
        assert!(stale_tab_cutoff == Duration::from_secs(300));

        // With critical memory pressure, the min duration should be returned.
        let stale_tab_cutoff = get_stale_tab_age_cutoff(&margins, PressureLevelChrome::Critical, 0);
        assert!(stale_tab_cutoff == Duration::ZERO);
    }
}
