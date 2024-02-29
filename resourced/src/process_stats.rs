// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::collections::HashSet;
use std::io::Error as IoError;
use std::os::fd::AsRawFd;
use std::os::fd::FromRawFd;
use std::os::fd::OwnedFd;
use std::path::Component;
use std::path::Path;
use std::path::PathBuf;
use std::sync::OnceLock;

use anyhow::anyhow;
use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use log::error;
use log::info;
use log::warn;

const INIT_PID: u32 = 1;

#[derive(Debug, Eq, Hash, PartialEq, Clone, Copy)]
#[repr(usize)]
#[allow(clippy::upper_case_acronyms)] // Needs to match UMA metric name
pub enum ProcessGroupKind {
    Browser = 0,
    Gpu = 1,
    Renderers = 2,
    ARC = 3,
    VMs = 4,     // Except for ARCVM
    Daemons = 5, // Everything else
    Count = 6,
}

impl From<usize> for ProcessGroupKind {
    fn from(val: usize) -> ProcessGroupKind {
        match val {
            0 => Self::Browser,
            1 => Self::Gpu,
            2 => Self::Renderers,
            3 => Self::ARC,
            4 => Self::VMs,
            5 => Self::Daemons,
            _ => panic!("Tried to convert {} to ProcessGroupKind", val),
        }
    }
}

#[derive(Debug, Eq, Hash, PartialEq, Clone, Copy)]
#[repr(usize)]
pub enum MemKind {
    Total = 0,
    Anon = 1,
    File = 2,
    Shmem = 3,
    Swap = 4,
    Count = 5,
}

impl MemKind {
    fn smaps_prefix(&self) -> &'static str {
        match self {
            Self::Total => "Pss:",
            Self::Anon => "Pss_Anon:",
            Self::File => "Pss_File:",
            Self::Shmem => "Pss_Shmem:",
            Self::Swap => "SwapPss:",
            _ => panic!("Instantiated MemKind::Count"),
        }
    }
}

impl From<usize> for MemKind {
    fn from(val: usize) -> MemKind {
        match val {
            0 => Self::Total,
            1 => Self::Anon,
            2 => Self::File,
            3 => Self::Shmem,
            4 => Self::Swap,
            _ => panic!("Tried to convert {} to MemKind", val),
        }
    }
}

/// Returns the UMA metric name based on the given process and memory kinds.
pub fn get_metric_name(process_kind: usize, mem_kind: usize) -> String {
    let process_kind = ProcessGroupKind::from(process_kind);
    let mem_kind = MemKind::from(mem_kind);
    format!("Platform.Memory.{:?}.{:?}", process_kind, mem_kind)
}

// Parses comm and ppid from the stats file content.
fn parse_stats_file(bytes: Vec<u8>) -> Result<u32> {
    // The format of the stats file is:
    //   pid (comm) run_state ppid ...
    // <comm> can contain spaces and parentheses, but there are no parentheses
    // anywhere else in the file. Rather than doing complicated regex, just find
    // search backwards for the last parentheses to skip over <comm>.
    let Some(comm_end) = bytes.iter().rposition(|c| *c == b')') else {
        bail!("failed to find comm parentheses");
    };

    let find_next_space = |start_idx| {
        bytes[start_idx..]
            .iter()
            .position(|c| *c == b' ')
            .map(|offset| start_idx + offset)
    };
    let run_state_start = comm_end + 2; // Advance past ") "
    let run_state_end = find_next_space(run_state_start).context("failed to skip run_state")?;
    let ppid_start = run_state_end + 1; // Advance past " "
    let ppid_end = find_next_space(ppid_start).context("failed to skip ppid")?;
    let ppid = String::from_utf8(bytes[ppid_start..ppid_end].to_vec())
        .context("failed to convert ppid to string")?
        .parse::<u32>()
        .context("failed to parse ppid")?;

    Ok(ppid)
}

// Constructs a map from pid -> Vec<child pids>
fn build_pid_map(procfs_path: &str) -> Result<HashMap<u32, Vec<u32>>> {
    let mut found_processes: HashSet<u32> = HashSet::new();
    let mut pid_map: HashMap<u32, Vec<u32>> = HashMap::new();

    let dir_entries =
        std::fs::read_dir(Path::new(procfs_path)).context("procfs read_dir failed")?;
    for dir_entry in dir_entries {
        let mut path = dir_entry.context("process enumeration error")?.path();
        // Skip any directories that aren't a process.
        let Some(pid) = path
            .file_name()
            .and_then(|name| name.to_str())
            .and_then(|pid| pid.parse::<u32>().ok())
        else {
            continue;
        };

        path.push("stat");
        let Ok(bytes) = std::fs::read(path) else {
            // Assume the read failed because the process exited.
            continue;
        };
        let ppid = match parse_stats_file(bytes) {
            Ok(res) => res,
            Err(e) => {
                warn!("failed to parse stats file {:?}", e);
                continue;
            }
        };
        if !found_processes.insert(pid) {
            warn!("duplicate process {}", pid);
        }
        if ppid != 0 {
            pid_map.entry(ppid).or_default().push(pid);
        }
    }

    if !found_processes.contains(&INIT_PID) {
        bail!("No init process found");
    }

    // Reparent any children whose parent we didn't find to init. This
    // probably means there was a race, but it should be rare.
    let mut need_reparenting = Vec::new();
    pid_map.retain(|pid, children| {
        let is_found = found_processes.contains(pid);
        if !is_found {
            info!("Parent {} for children {:?} not found", pid, children);
            need_reparenting.append(children);
        }
        is_found
    });
    pid_map
        .get_mut(&INIT_PID)
        .expect("init disappeared")
        .append(&mut need_reparenting);
    Ok(pid_map)
}

fn read_cmdline(procfs_path: &str, pid: u32) -> Option<Vec<u8>> {
    match std::fs::read(Path::new(&format!("{}/{}/cmdline", procfs_path, pid))) {
        Ok(bytes) => Some(bytes),
        Err(err) => {
            let os_err = err.raw_os_error();
            // If cmdline doesn't exist or we got ESRCH reading it, then we raced
            // with the process dying. Otherwise something unexpected happened, so
            // log an error.
            if os_err != Some(libc::ENOENT) && os_err != Some(libc::ESRCH) {
                warn!("Failed to read {}'s cmdline: {:?}", pid, err);
            }
            None
        }
    }
}

// Classifies a pid in a daemon tree. Returns the pid's type as well as
// the type of the tree of its descendants.
fn classify_daemon(
    procfs_path: &str,
    pid: u32,
    arc_container_pid: Option<u32>,
) -> Option<(TreeKind, ProcessGroupKind)> {
    if Some(pid) == arc_container_pid {
        return Some((TreeKind::Arc, ProcessGroupKind::ARC));
    }

    let Some(exe) = get_exe(procfs_path, pid) else {
        return None;
    };

    let exe = exe.as_os_str();
    if exe == "/opt/google/chrome/chrome" {
        return Some((TreeKind::Chrome, ProcessGroupKind::Browser));
    } else if exe == "/usr/bin/vm_concierge" {
        return Some((TreeKind::Concierge, ProcessGroupKind::VMs));
    } else if exe == "/usr/bin/seneschal" {
        return Some((TreeKind::Vm, ProcessGroupKind::VMs));
    }

    Some((TreeKind::Daemon, ProcessGroupKind::Daemons))
}

fn strip_chrome_directory(exe: &Path) -> Option<PathBuf> {
    // Check for ash or running lacros from rootfs.
    if let Ok(exe) = exe
        .strip_prefix(Path::new("/opt/google/chrome"))
        .or_else(|_| exe.strip_prefix(Path::new("/run/lacros")))
    {
        return Some(exe.to_path_buf());
    }
    // Check for lacros from the stateful partition, which is of the form:
    //   /run/imageloader/lacros-<channel>/104.0.xxxx.xx
    if let Ok(stripped) = exe.strip_prefix(Path::new("/run/imageloader")) {
        let mut components = stripped.components();
        let Some(Component::Normal(dir_name)) = components.next() else {
            return None;
        };
        if dir_name
            .to_str()
            .map_or(false, |dir_name| dir_name.starts_with("lacros-"))
        {
            return Some(components.skip(1).collect::<PathBuf>());
        }
    }
    None
}

// Chrome process classification.  We rely on the "--type=xyz" command line flag
// to processes.  A partial list of types is in
// content/public/common/content_switches.cc.  We classify them as shown:
//
// const char kGpuProcess[]                    = "gpu-process";    // GPU
// const char kPpapiBrokerProcess[]            = "ppapi-broker";   // browser
// const char kPpapiPluginProcess[]            = "ppapi";          // renderer
// const char kRenderersProcess[]              = "renderer";       // renderer
// const char kUtilityProcess[]                = "utility";        // renderer
//
// (PPAPI stands for "pepper plugin API", which includes Flash).  Additionally
// there is "zygote" and "broker", which we classify as browser.
//
// The browser process does not have a --type==xyz flag.
fn classify_chrome(procfs_path: &str, pid: u32) -> Option<ProcessGroupKind> {
    let Some(exe) = get_exe(procfs_path, pid) else {
        return Some(ProcessGroupKind::Browser);
    };

    let Some(exe) = strip_chrome_directory(&exe) else {
        warn!("Unknown chrome process {} running {:?}", pid, exe);
        return Some(ProcessGroupKind::Browser);
    };

    if exe == Path::new("nacl_helper") {
        return Some(ProcessGroupKind::Browser);
    } else if exe != Path::new("chrome") {
        warn!("Unknown chrome process {} running {:?}", pid, exe);
        return Some(ProcessGroupKind::Browser);
    }

    let cmdline = read_cmdline(procfs_path, pid)?;
    let Some(type_switch) = find_cmdline_arg_by_prefix(&cmdline, b"--type") else {
        return Some(ProcessGroupKind::Browser);
    };

    Some(match type_switch.as_str() {
        "--type=broker" => ProcessGroupKind::Browser,
        "--type=ppapi-broker" => ProcessGroupKind::Browser,
        "--type=zygote" => ProcessGroupKind::Browser,
        "--type=renderer" => ProcessGroupKind::Renderers,
        "--type=ppapi" => ProcessGroupKind::Renderers,
        "--type=sandbox" => ProcessGroupKind::Renderers,
        "--type=utility" => ProcessGroupKind::Renderers,
        "--type=gpu-process" => ProcessGroupKind::Gpu,
        _ => {
            warn!("Unknown chrome process {} with type {}", pid, type_switch);
            ProcessGroupKind::Browser
        }
    })
}

// Shmem that has been swapped is not included in the SwapPss of any process's
// smaps_rollup. In the kernel, PSS and shmem are abstractions in different
// layers (address space and file systems), which makes resolving this issue
// in the kernel very difficult.
//
// On ChromeOS, the lack of shmem in SwapPss is primarily a problem because
// crosvm uses shmem for guest memory. Rather than trying to solve the general
// case in the kernel, we can deal with this specific problem by parsing the
// smaps of any root crosvm processes and summing up the Swap field of any
// memfd regions.
struct ProcessInfo {
    pid: u32,
    is_root_crosvm: bool,
}

// Classifies a pid in a concierge tree. Returns the pid's type, the type
// of the tree of its descendants, and whether or not the process is a root
// crosvm process (see ProcessInfo).
fn classify_concierge(procfs_path: &str, pid: u32) -> Option<(TreeKind, ProcessGroupKind, bool)> {
    let cmdline = read_cmdline(procfs_path, pid)?;
    if find_cmdline_arg_by_prefix(&cmdline, b"androidboot.hardware=bertha").is_some() {
        Some((TreeKind::Arc, ProcessGroupKind::ARC, true))
    } else {
        let is_root_crosvm = get_exe(procfs_path, pid)
            .and_then(|exe| exe.file_name().map(|name| name == "crosvm"))
            .unwrap_or(false);
        Some((TreeKind::Vm, ProcessGroupKind::VMs, is_root_crosvm))
    }
}

fn is_seperator_byte(b: u8) -> bool {
    b == b'\0' || b.is_ascii_whitespace()
}

// Given the raw cmdline bytes, finds the first cmdline entry which starts
// with |prefix|. This function only converts the matching entry to a String,
// so other parts of the cmdline do not have to be valid UTF8.
fn find_cmdline_arg_by_prefix(cmdline: &[u8], prefix: &[u8]) -> Option<String> {
    let mut idx = 0;
    let mut iter = cmdline.iter();
    while let Some(start_offset) = iter.position(|b| !is_seperator_byte(*b)) {
        let start = idx + start_offset;
        idx += start_offset + 1; // +1 to move past the non-space character
        let substr = match iter.position(|b| is_seperator_byte(*b)) {
            Some(space_offset) => {
                idx += space_offset + 1; // +1 to move past the space
                &cmdline[start..=(start + space_offset)]
            }
            None => &cmdline[start..],
        };
        if substr.len() >= prefix.len() && &substr[..prefix.len()] == prefix {
            return String::from_utf8(substr.to_vec()).ok();
        }
    }
    None
}

// Read the exe path from /proc/pid/exe.
fn get_exe(procfs_path: &str, pid: u32) -> Option<PathBuf> {
    match std::fs::read_link(Path::new(&format!("{}/{}/exe", procfs_path, pid))) {
        Ok(path) => Some(path.to_path_buf()),
        Err(err) => {
            let os_err = err.raw_os_error();
            // ENOENT just means we raced with the process dying. Otherwise log the error.
            if os_err != Some(libc::ENOENT) {
                warn!("Failed to read {}'s cmdline: {:?}", pid, err);
            }
            None
        }
    }
}

// The type of the process tree that we're walking, which tells us the
// function that should be to classify a given process.
#[derive(Clone, Copy)]
enum TreeKind {
    Daemon,
    Chrome,
    Concierge,
    Arc,
    Vm,
}

impl TreeKind {
    // Returns the pid's type, the type of the tree of its descendants, and whether
    // the process is a root crosvm process (see ProcessInfo).
    fn classify_process(
        &self,
        procfs_path: &str,
        pid: u32,
        arc_container_pid: Option<u32>,
    ) -> Option<(TreeKind, ProcessGroupKind, bool)> {
        match self {
            Self::Daemon => classify_daemon(procfs_path, pid, arc_container_pid)
                .map(|(tree, process)| (tree, process, false)),
            Self::Chrome => classify_chrome(procfs_path, pid).map(|k| (Self::Chrome, k, false)),
            Self::Concierge => classify_concierge(procfs_path, pid),
            Self::Arc => Some((Self::Arc, ProcessGroupKind::ARC, false)),
            Self::Vm => Some((Self::Vm, ProcessGroupKind::VMs, false)),
        }
    }
}

// Helper function for recursively classifying the a process tree.
fn classify_processes_walker(
    procfs_path: &str,
    pid: u32,
    tree_kind: TreeKind,
    arc_container_pid: Option<u32>,
    pid_map: &HashMap<u32, Vec<u32>>,
    kind_map: &mut [Vec<ProcessInfo>; ProcessGroupKind::Count as usize],
) {
    let tree_kind = match tree_kind.classify_process(procfs_path, pid, arc_container_pid) {
        Some((tree_kind, process_kind, is_root_crosvm)) => {
            kind_map[process_kind as usize].push(ProcessInfo {
                pid,
                is_root_crosvm,
            });
            tree_kind
        }
        None => tree_kind,
    };

    if let Some(children) = pid_map.get(&pid) {
        for child in children.iter() {
            classify_processes_walker(
                procfs_path,
                *child,
                tree_kind,
                arc_container_pid,
                pid_map,
                kind_map,
            );
        }
    };
}

fn get_arc_container_init_pid(run_path: &str) -> Result<Option<u32>> {
    const ARC_CONTAINER_FILE: &str = "containers/android-run_oci/container.pid";
    let arc_container_path = Path::new(run_path).join(ARC_CONTAINER_FILE);
    if !Path::exists(&arc_container_path) {
        return Ok(None);
    }
    let pid_bytes = std::fs::read(arc_container_path).context("failed to read arc pid ")?;
    let pid_str = String::from_utf8(pid_bytes).context("failed to get utf8 arc pid")?;
    pid_str
        .trim()
        .parse::<u32>()
        .map(Some)
        .context("failed to parse arc pid")
}

fn classify_processes(
    procfs_path: &str,
    run_path: &str,
    pid_map: HashMap<u32, Vec<u32>>,
) -> [Vec<ProcessInfo>; ProcessGroupKind::Count as usize] {
    let arc_container_pid = match get_arc_container_init_pid(run_path) {
        Ok(pid) => pid,
        Err(e) => {
            error!("Failed to read ARC pid {:?}", e);
            None
        }
    };
    let mut kind_map: [Vec<ProcessInfo>; ProcessGroupKind::Count as usize] = Default::default();
    // Walking from init is an easy way to skip kthreadd and its descendants.
    classify_processes_walker(
        procfs_path,
        INIT_PID,
        TreeKind::Daemon,
        arc_container_pid,
        &pid_map,
        &mut kind_map,
    );
    kind_map
}

fn parse_dev_id_from_vma_header(line: &str) -> Option<&str> {
    // Extract the 4th column from lines of this form:
    // 7e44556a8000-7e44556a9000 rw-s 00000000 00:01 4716441 /memfd:name (deleted)
    line.split(' ').filter(|s| !s.is_empty()).nth(3)
}

fn parse_memfd_dev_id(addr: *const libc::c_void) -> Result<String> {
    let smaps_bytes = std::fs::read("/proc/self/smaps")?;
    let smaps = String::from_utf8_lossy(&smaps_bytes);

    for line in smaps.split('\n') {
        if let Some(idx) = line.find('-') {
            if let Ok(vma_start) = usize::from_str_radix(&line[..idx], 16) {
                if vma_start == addr as usize {
                    if let Some(id) = parse_dev_id_from_vma_header(line) {
                        return Ok(id.to_string());
                    } else {
                        bail!("Failed to parse smaps header {}", line);
                    }
                }
            }
        }
    }
    Err(anyhow!("Failed to find target VMA region"))
}

fn get_memfd_dev_id_from_smaps() -> Result<String> {
    // SAFETY: Safe because memfd_create doesn't affect memory and we check the return.
    let memfd = unsafe {
        libc::syscall(
            libc::SYS_memfd_create,
            b"memfd\0".as_ptr() as *const libc::c_char,
            0,
        )
    };
    if memfd < 0 {
        bail!("memfd_create failed {:?}", IoError::last_os_error());
    }
    // SAFETY: Safe because we checked that memfd is a valid fd.
    let memfd = unsafe { OwnedFd::from_raw_fd(memfd as i32) };

    const MAP_SIZE: usize = 4096;
    // SAFETY: Safe because mmap64 doesn't affect any rust-controlled memory
    // and we check the return value.
    let addr = unsafe {
        libc::mmap64(
            std::ptr::null_mut(),
            MAP_SIZE,
            libc::PROT_NONE,
            libc::MAP_SHARED,
            memfd.as_raw_fd(),
            0,
        )
    };
    if addr == libc::MAP_FAILED {
        bail!("failed to mmap memfd {:?}", IoError::last_os_error());
    }

    let dev_id = parse_memfd_dev_id(addr);

    // SAFETY: Safe because this only affects the non-rust controlled
    // memory we allocated above with mmap64.
    let res = unsafe { libc::munmap(addr, MAP_SIZE) };
    if res != 0 {
        warn!("failed to munmap memfd {:?}", IoError::last_os_error());
    }

    dev_id
}

static MEMFD_DEV_ID: OnceLock<Option<String>> = OnceLock::new();

fn get_memfd_dev_id() -> Option<String> {
    MEMFD_DEV_ID
        .get_or_init(|| match get_memfd_dev_id_from_smaps() {
            Ok(dev_id) => Some(dev_id),
            Err(err) => {
                error!("Failed to parse memfd dev_id: {:?}", err);
                None
            }
        })
        .clone()
}

fn parse_smaps_line(line: &str) -> Option<u64> {
    let parts: Vec<&str> = line.split(' ').filter(|s| !s.is_empty()).collect();
    if parts.len() != 3 {
        return None;
    }
    parts[1].parse::<u64>().ok().map(|kb| kb * 1024)
}

// Parse shmem swap from full smaps for the given pid.
fn parse_shmem_swap_from_smaps(procfs_path: &str, pid: u32) -> Option<u64> {
    let smaps_bytes = std::fs::read(format!("{}/{}/smaps", procfs_path, pid)).ok()?;
    // smaps contains file names, so we may not necessarily be able to convert it
    // to a utf8 string. However, we only care about the ASCII parts of the string
    // returned by the kernel, so a lossy conversion is fine.
    let smaps = String::from_utf8_lossy(&smaps_bytes);

    let mut res = 0;

    let dev_id = get_memfd_dev_id()?;

    let mut prev_line: Option<&str> = None;
    let mut processing_shmem = false;
    for line in smaps.split('\n') {
        let local_prev_line = prev_line;
        prev_line = Some(line);

        // Rather than trying to match the address in the VMA headers, it's easier
        // to match the 'Size:' prefix on the second line.
        if line.starts_with("Size:") {
            processing_shmem = local_prev_line.map_or(false, |header| {
                parse_dev_id_from_vma_header(header) == Some(&dev_id)
            });
        }

        if processing_shmem && line.starts_with("Swap:") {
            if let Some(bytes) = parse_smaps_line(line) {
                res += bytes;
            }
        }
    }

    Some(res)
}

// Gets memory stats for the given process.
fn get_memory_stats(
    procfs_path: &str,
    info: &ProcessInfo,
) -> Option<[u64; MemKind::Count as usize]> {
    // If this fails, the process is probably dead, so just return.
    let smaps_rollup_bytes =
        std::fs::read(format!("{}/{}/smaps_rollup", procfs_path, info.pid)).ok()?;
    let smaps_rollup = String::from_utf8(smaps_rollup_bytes).ok()?;

    let mut res: [u64; MemKind::Count as usize] = Default::default();

    let mut mem_kind_idx = 0;
    for line in smaps_rollup.split('\n') {
        let kind = MemKind::from(mem_kind_idx);
        if !line.starts_with(kind.smaps_prefix()) {
            continue;
        }
        let bytes = parse_smaps_line(line).unwrap_or_else(|| panic!("bad line in rollup {}", line));
        res[mem_kind_idx] += bytes;
        mem_kind_idx += 1;
        if mem_kind_idx == MemKind::Count as usize {
            break;
        }
    }

    if info.is_root_crosvm {
        if let Some(bytes) = parse_shmem_swap_from_smaps(procfs_path, info.pid) {
            res[MemKind::Swap as usize] += bytes;
        }
    }

    Some(res)
}

// Accumulates memory statistics for all classified processes.
fn accumulate_memory_stats(
    procfs_path: &str,
    processes: [Vec<ProcessInfo>; ProcessGroupKind::Count as usize],
) -> MemStats {
    let mut res: MemStats = Default::default();
    for (process_kind, processes) in processes.into_iter().enumerate() {
        for process in processes.iter() {
            if let Some(stats) = get_memory_stats(procfs_path, process) {
                for (i, stat) in stats.iter().enumerate() {
                    res[process_kind][i] += stat;
                }
            }
        }
    }
    res
}

pub type MemStats = [[u64; MemKind::Count as usize]; ProcessGroupKind::Count as usize];

/// Gather the Platform.Memory.<ProcessGroupKind>.<MemKind> UMA statistics.
pub fn get_all_memory_stats(procfs_path: &str, run_path: &str) -> Result<MemStats> {
    let process_map = build_pid_map(procfs_path)?;
    let classified_processes = classify_processes(procfs_path, run_path, process_map);
    let res = accumulate_memory_stats(procfs_path, classified_processes);
    Ok(res)
}

#[cfg(test)]
mod tests {
    use tempfile::TempDir;

    use super::*;

    const MIB: u64 = 1024 * 1024;

    #[allow(clippy::too_many_arguments)]
    fn create_proc_entry(
        procfs_path: &Path,
        pid: u32,
        ppid: u32,
        name: &[u8],
        cmdline: Option<(&str, &str)>,
        total_mib: u64,
        anon_mib: u64,
        file_mib: u64,
        shmem_mib: u64,
        swap_mib: u64,
        shmem_swap_mib: u64,
    ) {
        let memfd_dev_id = MEMFD_DEV_ID
            .get_or_init(|| Some("00:01".to_string()))
            .as_ref()
            .unwrap();

        let pid_path = procfs_path.join(format!("{}", pid));
        std::fs::create_dir(&pid_path).unwrap();
        if let Some(cmdline) = cmdline {
            std::os::unix::fs::symlink(Path::new(cmdline.0), pid_path.join("exe")).unwrap();
            std::fs::write(
                pid_path.join("cmdline"),
                format!("{} {}", cmdline.0, cmdline.1),
            )
            .unwrap();
        } else {
            std::fs::File::create(pid_path.join("cmdline")).unwrap();
        }
        let stats_bytes = [
            format!("{} (", pid).as_bytes(),
            name,
            format!(") R {} 33 44 a b c \n", ppid).as_bytes(),
        ]
        .concat();
        std::fs::write(pid_path.join("stat"), stats_bytes).unwrap();
        if total_mib != 0 {
            let smaps_rollup_content = format!(
                "blah\n\
                 Rss:                123 kB\n\
                 Pss:                 {} kB\n\
                 Pss_Anon:            {} kB\n\
                 Pss_File:            {} kB\n\
                 Pss_Shmem:           {} kB\n\
                 Shared_Clean:       456 kB\n\
                 Swap:               789 kB\n\
                 SwapPss:             {} kB\n\
                 Locked:             987 kB",
                total_mib * 1024,
                anon_mib * 1024,
                file_mib * 1024,
                shmem_mib * 1024,
                swap_mib * 1024
            );
            std::fs::write(pid_path.join("smaps_rollup"), smaps_rollup_content).unwrap();

            let smaps_content = format!(
                "7ffcd41f5000-7ffcd41f8000 r-xp 00000000 00:00 0 [vdso]\n\
                 Size:                  123 kB\n\
                 Pss:                   456 kB\n\
                 Swap:                  789 kb\n\
                 7ffcd41f8000-7ffcd41f9000 rw-s 00000000 {} 4988838 /memfd:name (deleted)\n\
                 Size:                  321 kB\n\
                 Pss:                   654 kB\n\
                 Swap:                  {} kb\n\
                 7bf540e00000-7bf541e00000 r--p 001db000 103:05 12012 /lib64/libc.so.6\n\
                 Size:                  987 kB\n\
                 Pss:                   654 kB\n\
                 Swap:                  321 kB\n",
                memfd_dev_id,
                shmem_swap_mib * 1024
            );
            std::fs::write(pid_path.join("smaps"), smaps_content).unwrap();
        } else {
            std::fs::File::create(pid_path.join("smaps_rollup")).unwrap();
        }
    }

    #[test]
    fn report_process_stats_container() {
        let temp_dir = TempDir::new().unwrap();
        let proc_dir_path = temp_dir.path().join("proc");
        let run_dir_path = temp_dir.path().join("run");
        std::fs::create_dir(&proc_dir_path).unwrap();
        std::fs::create_dir(&run_dir_path).unwrap();

        let arc_init_pid = 22;
        let arc_pid_path = run_dir_path.join("containers/android-run_oci");
        std::fs::create_dir_all(&arc_pid_path).unwrap();
        std::fs::write(
            arc_pid_path.join("container.pid"),
            format!("{}", arc_init_pid),
        )
        .unwrap();

        #[rustfmt::skip]
        {
            // init.
            create_proc_entry(
                &proc_dir_path, 1, 0,
                b"init", Some(("/sbin/init", "")),
                10, 5, 5, 0, 7, 3,
            );
            // kthreadd (kernel daemon)
            create_proc_entry(&proc_dir_path, 2, 0, b"kthreadd", None, 0, 0, 0, 0, 0, 0);
            // kworker with a space in its name
            create_proc_entry(
                &proc_dir_path, 3, 2,
                b"kworker/0:0-My worker", None,
                0, 0, 0, 0, 0, 0,
            );
            // ARC init.
            create_proc_entry(
                &proc_dir_path, arc_init_pid, 1,
                b"arc-init", Some(("/blah/arc/init", "")),
                10, 5, 5, 0, 1, 0,
            );
            // ARC child process
            create_proc_entry(
                &proc_dir_path, 300, arc_init_pid,
                b"system_server", Some(("/blah/arc/system_server", "")),
                100, 7, 6, 13, 10, 0,
            );
            // Browser processes.
            create_proc_entry(
                &proc_dir_path, 100, 1,
                b"chrome", Some(("/opt/google/chrome/chrome", "blah")),
                300, 200, 90, 10, 2, 0,
            );
            create_proc_entry(
                &proc_dir_path, 101, 100,
                b"chrome", Some(("/opt/google/chrome/chrome", "--type=broker")),
                5, 4, 3, 2, 1, 0,
            );
            // Other spawned-from-chrome processes with a ) in the name.
            // Anything spawned from the Chrome browser process will count under browser
            // if it doesn't count under one of the other categories.
            create_proc_entry(
                &proc_dir_path, 102, 100,
                b"bash (stuff)", Some(("/bin/bash /usr/bin/somescript", "")),
                400, 50, 245, 100, 5, 0,
            );
            create_proc_entry(
                &proc_dir_path, 103, 100,
                b"corrupt )))) R Q", Some(("/bin/bash /usr/bin/somescript", "")),
                100, 33, 33, 33, 1, 0,
            );
            // GPU.
            create_proc_entry(
                &proc_dir_path, 110, 100,
                b"chrome", Some(("/opt/google/chrome/chrome", "--type=gpu-process")),
                400, 70, 30, 300, 3, 0,
            );
            // Renderers.
            create_proc_entry(
                &proc_dir_path, 120, 100,
                b"chrome", Some(("/opt/google/chrome/chrome", "--type=renderer")),
                500, 450, 30, 20, 13, 0,
            );
            create_proc_entry(
                &proc_dir_path, 121, 100,
                b"chrome", Some(("/opt/google/chrome/chrome", "--type=renderer")),
                500, 450, 30, 20, 13, 0,
            );
            // Name not UTF-8, but still a child of the browser
            create_proc_entry(
                &proc_dir_path, 122, 100,
                b"p\xb9Q\xc8", Some(("/opt/google/chrome/chrome", "--type=renderer")),
                113, 33, 80, 0, 0, 0,
            );
            // Daemons.
            create_proc_entry(
                &proc_dir_path, 200, 1,
                b"shill", Some(("/usr/bin/shill", "")),
                100, 30, 70, 0, 0, 0,
            );
            // Parent has died, will get reparented as a daemon
            create_proc_entry(
                &proc_dir_path, 45, 151,
                b"frecon", Some(("/usr/bin/frecon", "")),
                213, 133, 80, 32, 48, 0,
            );
        };

        let stats = get_all_memory_stats(
            &proc_dir_path.to_string_lossy(),
            &run_dir_path.to_string_lossy(),
        )
        .unwrap();

        let expected = [
            // browser
            [805 * MIB, 287 * MIB, 371 * MIB, 145 * MIB, 9 * MIB],
            // gpu
            [400 * MIB, 70 * MIB, 30 * MIB, 300 * MIB, 3 * MIB],
            // renderers
            [1113 * MIB, 933 * MIB, 140 * MIB, 40 * MIB, 26 * MIB],
            // arc
            [110 * MIB, 12 * MIB, 11 * MIB, 13 * MIB, 11 * MIB],
            // vms
            [0, 0, 0, 0, 0],
            // daemons
            [323 * MIB, 168 * MIB, 155 * MIB, 32 * MIB, 55 * MIB],
        ];
        assert_eq!(stats, expected);
    }

    #[test]
    fn report_process_stats_arcvm() {
        let temp_dir = TempDir::new().unwrap();
        let proc_dir_path = temp_dir.path().join("proc");
        let run_dir_path = temp_dir.path().join("run");
        std::fs::create_dir(&proc_dir_path).unwrap();
        std::fs::create_dir(&run_dir_path).unwrap();

        #[rustfmt::skip]
        {
            // init.
            create_proc_entry(
                &proc_dir_path, 1, 0,
                b"init", Some(("/sbin/init", "")),
                10, 5, 5, 0, 7, 0);

            // vm_concierge
            create_proc_entry(
                &proc_dir_path, 100, 1,
                b"vm_concierge", Some(("/usr/bin/vm_concierge", "")),
                10, 5, 5, 0, 1, 0,
            );

            // ARCVM
            create_proc_entry(
                &proc_dir_path, 200, 100,
                b"crosvm", Some(("/usr/bin/crosvm", "androidboot.hardware=bertha vmlinux")),
                100, 50, 50, 10, 10, 645,
            );
            create_proc_entry(
                &proc_dir_path, 201, 200,
                b"crosvm", Some(("/usr/bin/crosvm", "androidboot.hardware=bertha vmlinux")),
                100, 50, 50, 10, 10, 321,
            );
            create_proc_entry(
                &proc_dir_path, 202, 201,
                b"crosvm", Some(("/usr/libexec/virgl_render_server", "")),
                12, 31, 56, 78, 90, 321,
            );
            create_proc_entry(
                &proc_dir_path, 203, 201,
                b"crosvm", Some(("/usr/bin/crosvm", "androidboot.hardware=bertha vmlinux")),
                98, 76, 54, 32, 10, 321,
            );

            // Other VMs
            create_proc_entry(
                &proc_dir_path, 300, 100,
                b"crosvm", Some(("/usr/bin/crosvm", "vmlinux")),
                10, 5, 5, 0, 1, 478,
            );
            create_proc_entry(
                &proc_dir_path, 301, 300,
                b"crosvm", Some(("/usr/bin/crosvm", "vmlinux")),
                10, 5, 5, 0, 1, 213,
            );

            // seneschal
            create_proc_entry(
                &proc_dir_path, 101, 1,
                b"seneschal", Some(("/usr/bin/seneschal", "")),
                35, 2, 91, 11, 13, 0,
            );
        };

        let stats = get_all_memory_stats(
            &proc_dir_path.to_string_lossy(),
            &run_dir_path.to_string_lossy(),
        )
        .unwrap();

        let expected = [
            // browser
            [0, 0, 0, 0, 0],
            // gpu
            [0, 0, 0, 0, 0],
            // renderers
            [0, 0, 0, 0, 0],
            // arc
            [310 * MIB, 207 * MIB, 210 * MIB, 130 * MIB, 765 * MIB],
            // vms
            [65 * MIB, 17 * MIB, 106 * MIB, 11 * MIB, 494 * MIB],
            // daemons
            [10 * MIB, 5 * MIB, 5 * MIB, 0, 7 * MIB],
        ];
        assert_eq!(stats, expected);
    }

    #[test]
    fn report_process_stats_lacros() {
        let temp_dir = TempDir::new().unwrap();
        let proc_dir_path = temp_dir.path().join("proc");
        let run_dir_path = temp_dir.path().join("run");
        std::fs::create_dir(&proc_dir_path).unwrap();
        std::fs::create_dir(&run_dir_path).unwrap();

        #[rustfmt::skip]
        {
            // init.
            create_proc_entry(
                &proc_dir_path, 1, 0,
                b"init", Some(("/sbin/init", "")),
                10, 5, 5, 0, 7, 0);
            // Browser processes.
            create_proc_entry(
                &proc_dir_path, 100, 1,
                b"chrome", Some(("/opt/google/chrome/chrome", "blah")),
                300, 200, 90, 10, 2, 0,
            );
            // GPU.
            create_proc_entry(
                &proc_dir_path, 110, 100,
                b"chrome", Some(("/opt/google/chrome/chrome", "--type=gpu-process")),
                400, 70, 30, 300, 3, 0,
            );
            // Renderers.
            create_proc_entry(
                &proc_dir_path, 120, 100,
                b"chrome", Some(("/opt/google/chrome/chrome", "--type=renderer")),
                500, 450, 30, 20, 13, 0,
            );
            // Lacros browser processes.
            create_proc_entry(
                &proc_dir_path, 200, 100,
                b"chrome", Some(("/run/lacros/chrome", "blah")),
                30, 20, 9, 1, 3, 0,
            );
            create_proc_entry(
                &proc_dir_path, 201, 200,
                b"chrome", Some(("/run/lacros/nacl_helper", "blah")),
                10, 30, 87, 42, 2, 0,
            );
            // Lacros GPU.
            create_proc_entry(
                &proc_dir_path, 210, 200,
                b"chrome", Some(("/opt/google/chrome/chrome", "--type=gpu-process")),
                40, 7, 3, 30, 5, 0,
            );
            // Lacros Renderers.
            create_proc_entry(
                &proc_dir_path, 220, 200,
                b"chrome", Some(("/opt/google/chrome/chrome", "--type=renderer")),
                50, 45, 3, 2, 7, 0,
            );
        };

        let stats = get_all_memory_stats(
            &proc_dir_path.to_string_lossy(),
            &run_dir_path.to_string_lossy(),
        )
        .unwrap();

        let expected = [
            // browser
            [340 * MIB, 250 * MIB, 186 * MIB, 53 * MIB, 7 * MIB],
            // gpu
            [440 * MIB, 77 * MIB, 33 * MIB, 330 * MIB, 8 * MIB],
            // renderers
            [550 * MIB, 495 * MIB, 33 * MIB, 22 * MIB, 20 * MIB],
            // arc
            [0, 0, 0, 0, 0],
            // vms
            [0, 0, 0, 0, 0],
            // daemons
            [10 * MIB, 5 * MIB, 5 * MIB, 0, 7 * MIB],
        ];
        assert_eq!(stats, expected);
    }

    #[test]
    fn check_metric_names() {
        assert_eq!(
            "Platform.Memory.Browser.Total",
            get_metric_name(ProcessGroupKind::Browser as usize, MemKind::Total as usize)
        );
        assert_eq!(
            "Platform.Memory.Gpu.Anon",
            get_metric_name(ProcessGroupKind::Gpu as usize, MemKind::Anon as usize)
        );
        assert_eq!(
            "Platform.Memory.Renderers.File",
            get_metric_name(ProcessGroupKind::Renderers as usize, MemKind::File as usize)
        );
        assert_eq!(
            "Platform.Memory.ARC.Shmem",
            get_metric_name(ProcessGroupKind::ARC as usize, MemKind::Shmem as usize)
        );
        assert_eq!(
            "Platform.Memory.VMs.Swap",
            get_metric_name(ProcessGroupKind::VMs as usize, MemKind::Swap as usize)
        );
        assert_eq!(
            "Platform.Memory.Daemons.Total",
            get_metric_name(ProcessGroupKind::Daemons as usize, MemKind::Total as usize)
        );
    }
}
