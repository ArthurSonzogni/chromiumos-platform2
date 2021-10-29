// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Listing for hibernate library components.

pub mod cat;
pub mod cookie;
pub mod hiberlog;

mod crypto;
mod dbus;
mod diskfile;
mod fiemap;
mod files;
mod hibermeta;
mod hiberutil;
mod imagemover;
mod keyman;
mod mmapbuf;
mod preloader;
mod resume;
mod snapdev;
mod splitter;
mod suspend;
mod sysfs;

pub use hiberutil::{HibernateOptions, ResumeOptions};

use anyhow::Result;
use resume::ResumeConductor;
use suspend::SuspendConductor;

/// Hibernate the system. This returns either upon failure to hibernate, or
/// after the system has successfully hibernated and resumed.
pub fn hibernate(options: HibernateOptions) -> Result<()> {
    let mut conductor = SuspendConductor::new()?;
    conductor.hibernate(options)
}

// Resume a previously stored hibernate image. If there is no valid resume
// image, this still potentially blocks waiting to get the hibernate key from
// cryptohome so it can be saved for the next hibernate. If there is a valid
// resume image, this returns if there was an error resuming the system. Upon a
// successful resume, this function does not return, as the system will be
// executing in the resumed image.
pub fn resume(options: ResumeOptions) -> Result<()> {
    let mut conductor = ResumeConductor::new()?;
    conductor.resume(options)
}
