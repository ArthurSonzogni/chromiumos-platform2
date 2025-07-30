// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

#[derive(Copy, Clone, Default)]
pub enum DiskOpType {
    #[default]
    Create,
    Resize,
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub enum VmType {
    #[default]
    Unknown,
    Termina,
    ArcVm,
    PluginVm,
    Borealis,
    Bruschetta,
    Baguette,
}

impl fmt::Display for VmType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            VmType::Unknown => write!(f, "unknown"),
            VmType::Termina => write!(f, "crostini"),
            VmType::ArcVm => write!(f, "arcvm"),
            VmType::PluginVm => write!(f, "pvm"),
            VmType::Borealis => write!(f, "borealis"),
            VmType::Bruschetta => write!(f, "bruschetta"),
            VmType::Baguette => write!(f, "baguette"),
        }
    }
}

#[derive(Copy, Clone, Debug, Default)]
pub enum VmDiskImageType {
    Raw,
    Qcow2,
    #[default]
    Auto,
    PluginVm,
}

impl fmt::Display for VmDiskImageType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            VmDiskImageType::Raw => write!(f, "raw"),
            VmDiskImageType::Qcow2 => write!(f, "qcow2"),
            VmDiskImageType::Auto => write!(f, "auto"),
            VmDiskImageType::PluginVm => write!(f, "pvm"),
        }
    }
}

#[derive(Copy, Clone, Debug, Default)]
pub enum VmState {
    Starting,
    Running,
    #[default]
    Stopped,
}

impl fmt::Display for VmState {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            VmState::Starting => write!(f, "starting"),
            VmState::Running => write!(f, "running"),
            VmState::Stopped => write!(f, "stopped"),
        }
    }
}

/// Information about a single VM disk image.
#[derive(Default, Debug)]
pub struct DiskInfo {
    /// Name of the VM contained in this disk.
    pub name: String,
    /// Size of the disk in bytes.
    pub size: u64,
    /// Minimum size the disk image may be resized to, if known.
    pub min_size: Option<u64>,
    /// Disk image type (raw, QCOW2, etc.).
    pub image_type: VmDiskImageType,
    /// Whether the disk size is user-specified (true) or automatically sized (false).
    pub user_chosen_size: bool,
    /// Indicates state of the VM owning this disk.
    pub state: VmState,
    /// On-disk VM type of the image.
    pub vm_type: VmType,
    /// Whether on-disk vm_type is available.
    pub has_vm_type: bool,
}
