extern crate anyhow;
extern crate dbus;
extern crate lazy_static;

#[cfg(test)]
mod test;

use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::Path;
use std::sync::Mutex;
use std::time::Duration;

use anyhow::{bail, Context, Result};
use dbus::blocking::LocalConnection;
use dbus::tree::{Factory, MTFn, MethodErr, MethodInfo, MethodResult};
use lazy_static::lazy_static;

// Extract the parsing function for unittest.
fn parse_file_to_u64<R: BufRead>(reader: R) -> Result<u64> {
    let first_line = reader.lines().next().context("No content in buffer")??;
    first_line
        .parse()
        .with_context(|| format!("Couldn't parse \"{}\" as u64", first_line))
}

/// Get the first line in a file and parse as u64.
fn read_file_to_u64<P: AsRef<Path>>(filename: P) -> Result<u64> {
    let reader = File::open(filename).map(BufReader::new)?;
    parse_file_to_u64(reader)
}

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
        - read_file_to_u64("/proc/sys/vm/extra_free_kbytes").unwrap_or(0))
}

/// Returns the percentage of the recent 10 seconds that some process is blocked
/// by memory.
/// Example input:
///   some avg10=0.00 avg60=0.00 avg300=0.00 total=0
///   full avg10=0.00 avg60=0.00 avg300=0.00 total=0
fn parse_psi_memory<R: BufRead>(reader: R) -> Result<f64> {
    for line in reader.lines() {
        let line = line?;
        let mut tokens = line.split_whitespace();
        if tokens.next() != Some("some") {
            continue;
        }
        if let Some(pair) = tokens.next() {
            let mut elements = pair.split("=");
            if elements.next() != Some("avg10") {
                continue;
            }
            if let Some(value) = elements.next() {
                return value.parse().context("Couldn't parse the avg10 value");
            }
        }
        bail!("Couldn't parse /proc/pressure/memory, line: {}", line);
    }
    bail!("Couldn't parse /proc/pressure/memory");
}

#[allow(dead_code)]
fn get_psi_memory_pressure_10_seconds() -> Result<f64> {
    let reader = File::open(Path::new("/proc/pressure/memory"))
        .map(BufReader::new)
        .context("Couldn't read /proc/pressure/memory")?;
    parse_psi_memory(reader)
}

/// Struct to hold parsed /proc/meminfo data, only contains used fields.
#[derive(Default)]
struct MemInfo {
    total: u64,
    free: u64,
    active_anon: u64,
    inactive_anon: u64,
    active_file: u64,
    inactive_file: u64,
    dirty: u64,
    swap_free: u64,
}

/// Parsing /proc/meminfo.
fn parse_meminfo<R: BufRead>(reader: R) -> Result<MemInfo> {
    let mut result = MemInfo::default();
    for line in reader.lines() {
        let line = line?;
        let mut tokens = line.split_whitespace();
        let key = if let Some(k) = tokens.next() {
            k
        } else {
            continue;
        };
        let value = if let Some(v) = tokens.next() {
            v.parse()?
        } else {
            continue;
        };
        if key == "MemTotal:" {
            result.total = value;
        } else if key == "MemFree:" {
            result.free = value;
        } else if key == "Active(anon):" {
            result.active_anon = value;
        } else if key == "Inactive(anon):" {
            result.inactive_anon = value;
        } else if key == "Active(file):" {
            result.active_file = value;
        } else if key == "Inactive(file):" {
            result.inactive_file = value;
        } else if key == "Dirty:" {
            result.dirty = value;
        } else if key == "SwapFree:" {
            result.swap_free = value;
        }
    }
    Ok(result)
}

/// Return MemInfo object containing /proc/meminfo data.
fn get_meminfo() -> Result<MemInfo> {
    let reader = File::open(Path::new("/proc/meminfo"))
        .map(BufReader::new)
        .context("Couldn't read /proc/meminfo")?;
    parse_meminfo(reader)
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
    free_component
        .saturating_add(cache_component)
        .saturating_add(swap_component)
}

#[derive(Clone, Copy, PartialEq)]
enum GameMode {
    // Game mode is off.
    Off = 0,
    // Game mode is on, borealis is the foreground subsystem.
    Borealis = 1,
}

// lazy_static + Mutex is safer than static mutable variable.
lazy_static! {
    static ref GAME_MODE: Mutex<GameMode> = Mutex::new(GameMode::Off);
}

const GAME_MODE_OFFSET_KB: u64 = 300 * 1024;

fn set_game_mode(mode: GameMode) -> Result<()> {
    match GAME_MODE.lock() {
        Ok(mut data) => {
            *data = mode;
            Ok(())
        }
        Err(_) => bail!("Failed to set game mode"),
    }
}

fn get_game_mode() -> Result<GameMode> {
    match GAME_MODE.lock() {
        Ok(data) => Ok(*data),
        Err(_) => bail!("Failed to get game mode"),
    }
}

fn get_available_memory_kb() -> Result<u64> {
    let meminfo = get_meminfo()?;
    let p = get_memory_parameters();
    Ok(calculate_available_memory_kb(
        &meminfo,
        p.reserved_free,
        p.min_filelist,
        p.ram_swap_weight,
    ))
}

fn get_foreground_available_memory_kb() -> Result<u64> {
    get_available_memory_kb()
}

fn get_background_available_memory_kb() -> Result<u64> {
    let available = get_available_memory_kb()?;
    if get_game_mode()? != GameMode::Off {
        if available > GAME_MODE_OFFSET_KB {
            Ok(available - GAME_MODE_OFFSET_KB)
        } else {
            Ok(0)
        }
    } else {
        Ok(available)
    }
}

fn parse_margins<R: BufRead>(reader: R) -> Result<Vec<u64>> {
    let first_line = reader
        .lines()
        .next()
        .context("No content in margin buffer")??;
    let margins = first_line
        .split_whitespace()
        .map(|x| x.parse().context("Couldn't parse an element in margins"))
        .collect::<Result<Vec<u64>>>()?;
    if margins.len() < 2 {
        bail!("Less than 2 numbers in margin content.");
    } else {
        Ok(margins)
    }
}

fn get_memory_margins_kb_impl() -> (u64, u64) {
    // TODO(vovoy): Use a regular config file instead of sysfs file.
    let margin_path = "/sys/kernel/mm/chromeos-low_mem/margin";
    match File::open(Path::new(margin_path)).map(BufReader::new) {
        Ok(reader) => match parse_margins(reader) {
            Ok(margins) => return (margins[0] * 1024, margins[1] * 1024),
            Err(e) => println!("Couldn't parse {}: {}", margin_path, e),
        },
        Err(e) => println!("Couldn't read {}: {}", margin_path, e),
    }

    // Critical margin is 5.2% of total memory, moderate margin is 40% of total
    // memory. See also /usr/share/cros/init/swap.sh on DUT.
    let total_memory_kb = match get_meminfo() {
        Ok(meminfo) => meminfo.total,
        Err(e) => {
            println!("Assume 2 GiB total memory if get_meminfo failed: {}", e);
            2 * 1024
        }
    };
    (total_memory_kb * 13 / 250, total_memory_kb * 2 / 5)
}

fn get_memory_margins_kb() -> (u64, u64) {
    lazy_static! {
        static ref MARGINS: (u64, u64) = get_memory_margins_kb_impl();
    }
    *MARGINS
}

struct MemoryParameters {
    reserved_free: u64,
    min_filelist: u64,
    ram_swap_weight: u64,
}

fn get_memory_parameters() -> MemoryParameters {
    lazy_static! {
        static ref RESERVED_FREE: u64 = match get_reserved_memory_kb() {
            Ok(reserved) => reserved,
            Err(e) => {
                println!("get_reserved_memory_kb failed: {}", e);
                0
            }
        };
        static ref MIN_FILELIST: u64 = read_file_to_u64("/proc/sys/vm/min_filelist_kbytes").unwrap_or(0);
        // TODO(vovoy): Use a regular config file instead of sysfs file.
        static ref RAM_SWAP_WEIGHT: u64 = read_file_to_u64("/sys/kernel/mm/chromeos-low_mem/ram_vs_swap_weight").unwrap_or(0);
    }
    MemoryParameters {
        reserved_free: *RESERVED_FREE,
        min_filelist: *MIN_FILELIST,
        ram_swap_weight: *RAM_SWAP_WEIGHT,
    }
}

fn dbus_method_get_available_memory_kb(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    match get_background_available_memory_kb() {
        // One message will be returned - the method return (and should always be there).
        Ok(available) => Ok(vec![m.msg.method_return().append1(available)]),
        Err(_) => Err(MethodErr::failed("Couldn't get available memory")),
    }
}

fn dbus_method_get_foreground_available_memory_kb(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    match get_foreground_available_memory_kb() {
        Ok(available) => Ok(vec![m.msg.method_return().append1(available)]),
        Err(_) => Err(MethodErr::failed(
            "Couldn't get foreground available memory",
        )),
    }
}

fn dbus_method_get_memory_margins_kb(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    let margins = get_memory_margins_kb();
    Ok(vec![m.msg.method_return().append2(margins.0, margins.1)])
}

fn dbus_method_get_game_mode(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    match get_game_mode() {
        Ok(GameMode::Off) => Ok(vec![m.msg.method_return().append1(0u8)]),
        Ok(GameMode::Borealis) => Ok(vec![m.msg.method_return().append1(1u8)]),
        Err(_) => Err(MethodErr::failed("Failed to get game mode")),
    }
}

fn dbus_method_set_game_mode(m: &MethodInfo<MTFn<()>, ()>) -> MethodResult {
    let mode = match m.msg.read1::<u8>()? {
        0 => GameMode::Off,
        1 => GameMode::Borealis,
        _ => return Err(MethodErr::failed("Unsupported game mode value")),
    };
    match set_game_mode(mode) {
        Ok(()) => Ok(vec![m.msg.method_return()]),
        Err(_) => Err(MethodErr::failed("Failed to set game mode")),
    }
}

fn start_service() -> Result<()> {
    let service_name = "org.chromium.ResourceManager";
    let path_name = "/org/chromium/ResourceManager";
    let interface_name = service_name;
    // Let's start by starting up a connection to the system bus and request a name.
    let c = LocalConnection::new_system()?;
    c.request_name(service_name, false, true, false)?;

    let f = Factory::new_fn::<()>();

    // We create a tree with one object path inside and make that path introspectable.
    let tree = f.tree(()).add(
        f.object_path(path_name, ()).introspectable().add(
            f.interface(interface_name, ())
                .add_m(
                    f.method(
                        "GetAvailableMemoryKB",
                        (),
                        dbus_method_get_available_memory_kb,
                    )
                    // Our method has one output argument.
                    .outarg::<u64, _>("available"),
                )
                .add_m(
                    f.method(
                        "GetForegroundAvailableMemoryKB",
                        (),
                        dbus_method_get_foreground_available_memory_kb,
                    )
                    .outarg::<u64, _>("available"),
                )
                .add_m(
                    f.method("GetMemoryMarginsKB", (), dbus_method_get_memory_margins_kb)
                        .outarg::<u64, _>("reply"),
                )
                .add_m(
                    f.method("GetGameMode", (), dbus_method_get_game_mode)
                        .outarg::<u8, _>("game_mode"),
                )
                .add_m(
                    f.method("SetGameMode", (), dbus_method_set_game_mode)
                        .inarg::<u8, _>("game_mode"),
                ),
        ),
    );

    tree.start_receive(&c);

    // Serve clients forever.
    loop {
        c.process(Duration::from_millis(u64::MAX))?;
    }
}

fn main() -> Result<()> {
    start_service()
}
