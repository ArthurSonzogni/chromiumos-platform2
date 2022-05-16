// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Listing for hibernate library components.

pub mod cat;
pub mod cookie;
pub mod hiberlog;
pub mod metrics;

mod crypto;
mod dbus;
mod diskfile;
mod fiemap;
mod files;
mod hibermeta;
mod hiberutil;
mod imagemover;
mod keyman;
mod lvm;
mod mmapbuf;
mod powerd;
mod preloader;
mod resume;
mod resume_init;
mod snapdev;
mod splitter;
mod suspend;
mod sysfs;
mod volume;

pub use hiberutil::{HibernateOptions, ResumeInitOptions, ResumeOptions};

use anyhow::Result;
use resume::ResumeConductor;
use resume_init::ResumeInitConductor;
use suspend::SuspendConductor;

/// Hibernate the system. This returns either upon failure to hibernate, or
/// after the system has successfully hibernated and resumed.
pub fn hibernate(options: HibernateOptions) -> Result<()> {
    let mut conductor = SuspendConductor::new()?;
    conductor.hibernate(options)
}

/// Prepare the system for resume. This is run very early in boot (from
/// chromeos_startup) before the stateful partition has been mounted. It checks
/// the hibernate cookie and clears it. If the cookie was set, it sets up
/// dm-snapshots for the logical volumes.
pub fn resume_init(options: ResumeInitOptions) -> Result<()> {
    let mut conductor = ResumeInitConductor::new(options);
    conductor.resume_init()
}

/// Resume a previously stored hibernate image. If there is no valid resume
/// image, this still potentially blocks waiting to get the hibernate key from
/// cryptohome so it can be saved for the next hibernate. If there is a valid
/// resume image, this returns if there was an error resuming the system. Upon a
/// successful resume, this function does not return, as the system will be
/// executing in the resumed image.
pub fn resume(options: ResumeOptions) -> Result<()> {
    let mut conductor = ResumeConductor::new()?;
    conductor.resume(options)
}
