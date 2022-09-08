// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Executes early resume initialization.

use std::path::PathBuf;

use anyhow::{Context, Result};
use log::info;

use crate::cookie::{
    cookie_description, get_hibernate_cookie, set_hibernate_cookie, HibernateCookieValue,
};
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

            // In the most common case, no resume from hibernate will be imminent.
            } else if cookie == HibernateCookieValue::NoResume {
                info!("Hibernate cookie was not set, doing nothing");
                return Err(HibernateError::CookieError(
                    "Cookie not set, doing nothing".to_string(),
                ))
                .context("Not preparing for resume");

            // This is the error path, where the system rebooted unexpectedly
            // while a resume or abort was underway. If a resume was
            // interrupted, the snapshots may contain data we want to preserve
            // (logs for investigation). If an abort was interrupted, the
            // stateful disk could be halfway merged. Either way, set up the
            // snapshots for a merge later in boot.
            } else if (cookie == HibernateCookieValue::ResumeInProgress)
                || (cookie == HibernateCookieValue::ResumeAborting)
            {
                info!(
                    "Hibernate interrupted (cookie was {}), wiring up snapshots",
                    cookie_description(cookie)
                );
                self.setup_snapshots()?;
                // The snapshots are valid and wired. Indicate to the main
                // hiberman resume process that it should immediately abort and
                // merge.
                set_hibernate_cookie::<PathBuf>(None, HibernateCookieValue::ResumeAborting)
                    .context("Failed to set hibernate cookie to ResumeAborting")?;

                return Ok(());
            }
        }

        self.setup_snapshots()?;
        // The snapshots are valid, so indicate that a resume is in progress,
        // and the main resume process later should go for it.
        set_hibernate_cookie::<PathBuf>(None, HibernateCookieValue::ResumeInProgress)
            .context("Failed to set hibernate cookie to ResumeInProgress")?;
        info!("Done with resume init");
        Ok(())
    }

    /// Wire up the snapshot images on top of the logical volumes.
    fn setup_snapshots(&self) -> Result<()> {
        // First clear the cookie to try and minimize the chances of getting
        // stuck in a boot loop. If this is the first time through (eg for a
        // valid resume image), the snapshots are not yet valid and have no
        // data, so not wiring them up upon interruption is the right thing to
        // do. If this is not the first time around (eg crash/powerloss during
        // resume), then this at least avoids getting stuck in a boot loop due
        // to a crash within this setup code.
        set_hibernate_cookie::<PathBuf>(None, HibernateCookieValue::NoResume)
            .context("Failed to set hibernate cookie to NoResume")?;
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

        Ok(())
    }
}
