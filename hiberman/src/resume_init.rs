// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Executes early resume initialization.

use std::path::PathBuf;

use anyhow::{Context, Result};
use log::info;

use crate::cookie::{get_hibernate_cookie, set_hibernate_cookie, HibernateCookieValue};
use crate::hiberutil::{HibernateError, ResumeInitOptions};
use crate::volume::VolumeManager;

pub struct ResumeInitConductor {
    options: ResumeInitOptions,
}

impl ResumeInitConductor {
    pub fn new(options: ResumeInitOptions) -> Self {
        Self { options }
    }

    pub fn resume_init(&mut self) -> Result<()> {
        let cookie =
            get_hibernate_cookie::<PathBuf>(None).context("Failed to get hibernate cookie")?;
        if cookie != HibernateCookieValue::ResumeReady {
            if self.options.force {
                info!("Hibernate cookie was not set, continuing anyway due to --force");
            } else {
                info!("Hibernate cookie was not set, doing nothing");
                return Err(HibernateError::CookieError(
                    "Cookie not set, doing nothing".to_string(),
                ))
                .context("Not preparing for resume");
            }
        }

        // Clear the cookie immediately to avoid boot loops.
        set_hibernate_cookie::<PathBuf>(None, HibernateCookieValue::NoResume)
            .context("Failed to set hibernate cookie")?;
        let mut volmgr = VolumeManager::new().context("Failed to create volume manager")?;
        volmgr
            .setup_hibernate_lv(false)
            .context("Failed to set up hibernate LV")?;
        volmgr
            .setup_stateful_snapshots()
            .context("Failed to set up stateful snapshots")?;

        // Change the thinpool to be read-only to avoid accidental thinpool
        // metadata changes that somehow get around the snapshot. Ideally we'd
        // do this before activating all the LVs under the snapshots, but doing
        // the activation seems to flip the pool back to being writeable.
        let ro_thinpool = volmgr
            .activate_thinpool_ro()
            .context("Failed to activate thinpool RO")?;
        if let Some(mut ro_thinpool) = ro_thinpool {
            ro_thinpool.dont_deactivate();
        }

        info!("Done with resume init");
        Ok(())
    }
}
