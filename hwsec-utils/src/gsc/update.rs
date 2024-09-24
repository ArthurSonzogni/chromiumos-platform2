// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use log::error;
use log::info;

use crate::context::Context;
use crate::error::HwsecError;
use crate::gsc::gsc_get_names;
use crate::gsc::gsctool_cmd_successful;
use crate::gsc::run_gsctool_cmd;
use crate::gsc::GSCTOOL_CMD_NAME;

const MAX_RETRIES: u32 = 3;
const SLEEP_DURATION: u64 = 70;

pub fn gsc_update(ctx: &mut impl Context, root: &Path) -> Result<(), HwsecError> {
    info!("Starting");

    let options: Vec<&str>;
    // determine the best way to communicate with the GSC.
    if gsctool_cmd_successful(ctx, vec!["--fwver", "--systemdev"]) {
        info!("Will use /dev/tpm0");
        options = vec!["--systemdev"];
    } else if gsctool_cmd_successful(ctx, vec!["--fwver", "--trunks_send"]) {
        info!("Will use trunks_send");
        options = vec!["--trunks_send"];
    } else {
        error!("Could not communicate with GSC");
        return Err(HwsecError::GsctoolError(1));
    }

    let gsc_images = gsc_get_names(ctx, &options[..])?
        .iter()
        .map(|base| {
            root.join(base)
                .to_str()
                .ok_or_else(|| {
                    error!("Cannot convert {} to string.", base.display());
                    HwsecError::GsctoolResponseBadFormatError
                })
                .and_then(|path| {
                    if ctx.path_exists(path) {
                        Ok(path.to_owned())
                    } else {
                        info!("{path} not found, quitting.");
                        Err(HwsecError::GsctoolError(1))
                    }
                })
        })
        .collect::<Result<Vec<_>, _>>()?;
    let gsc_images: Vec<_> = gsc_images.iter().map(|s| s.as_ref()).collect();

    let mut exit_status: i32 = 0;
    for retries in 0..MAX_RETRIES {
        if retries != 0 {
            // Need to sleep for at least a minute to get around GSC update throttling:
            // it rejects repeat update attempts happening sooner than 60 seconds after
            // the previous one.
            ctx.sleep(SLEEP_DURATION);
        }

        let exe_result = run_gsctool_cmd(
            ctx,
            [&options[..], &["--upstart"], &gsc_images[..]].concat(),
        )?;
        exit_status = exe_result.status.code().unwrap();

        // Exit status values 2 or below indicate successful update, nonzero
        // values mean that reboot is required for the new version to kick in.
        if exit_status <= 2 {
            // Callers of this script do not care about the details and consider any
            // non-zero value an error.
            info!("success ({})", exit_status);
            exit_status = 0;
            break;
        }

        info!(
            "{} (options: {:?}) attempt {} error {}",
            GSCTOOL_CMD_NAME,
            options,
            retries + 1,
            exit_status
        );

        let output = format!(
            "{}{}",
            std::str::from_utf8(&exe_result.stdout)
                .map_err(|_| HwsecError::GsctoolResponseBadFormatError)?,
            std::str::from_utf8(&exe_result.stderr)
                .map_err(|_| HwsecError::GsctoolResponseBadFormatError)?
        );

        // Log output text one line at a time, otherwise they are all concatenated
        // into a single long entry with messed up line breaks.
        for line in output.lines() {
            info!("{}", line);
        }
    }
    if exit_status == 0 {
        Ok(())
    } else {
        Err(HwsecError::GsctoolError(exit_status))
    }
}

#[cfg(test)]
mod tests {
    use std::path::Path;

    use crate::context::mock::MockContext;
    use crate::context::Context;
    use crate::error::HwsecError;
    use crate::gsc::gsc_update;
    use crate::gsc::update::MAX_RETRIES;
    use crate::gsc::GSC_IMAGE_BASE_NAME;

    #[test]
    fn test_gsc_update_could_not_communicate_with_cr50() {
        let mut mock_ctx = MockContext::new();
        mock_ctx
            .cmd_runner()
            .add_gsctool_interaction(vec!["--fwver", "--systemdev"], 1, "", "");
        mock_ctx
            .cmd_runner()
            .add_gsctool_interaction(vec!["--fwver", "--trunks_send"], 1, "", "");
        let result = gsc_update(&mut mock_ctx, Path::new("/"));
        assert_eq!(result, Err(HwsecError::GsctoolError(1)));
    }

    #[test]
    fn test_gsc_update_image_not_exist() {
        let mut mock_ctx = MockContext::new();
        mock_ctx
            .cmd_runner()
            .add_gsctool_interaction(vec!["--fwver", "--systemdev"], 0, "", "");
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["--systemdev", "--board_id"],
            0,
            "Board ID space: 43425559:bcbdaaa6:00007f10",
            "",
        );
        let result = gsc_update(&mut mock_ctx, Path::new("/"));
        assert_eq!(result, Err(HwsecError::GsctoolError(1)));
    }

    #[test]
    fn test_gsc_update_ok() {
        let mut mock_ctx = MockContext::new();
        mock_ctx
            .cmd_runner()
            .add_gsctool_interaction(vec!["--fwver", "--systemdev"], 0, "", "");
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["--systemdev", "--board_id"],
            0,
            "Board ID space: 43425559:bcbdaaa6:00007f10",
            "",
        );

        let mut flags = vec!["--systemdev", "--upstart"];
        let paths: Vec<_> = GSC_IMAGE_BASE_NAME
            .iter()
            .map(|&path| format!("/{path}.prepvt"))
            .collect();
        for path in &paths {
            mock_ctx.create_path(path);
            flags.push(path);
        }

        mock_ctx
            .cmd_runner()
            .add_gsctool_interaction(flags, 0, "", "");

        let result = gsc_update(&mut mock_ctx, Path::new("/"));
        assert_eq!(result, Ok(()));
    }

    #[test]
    fn test_gsc_update_failed_exceed_max_retries() {
        let mut mock_ctx = MockContext::new();
        mock_ctx
            .cmd_runner()
            .add_gsctool_interaction(vec!["--fwver", "--systemdev"], 0, "", "");
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["--systemdev", "--board_id"],
            0,
            "Board ID space: 43425559:bcbdaaa6:00007f10",
            "",
        );

        let mut flags = vec!["--systemdev", "--upstart"];
        let paths: Vec<_> = GSC_IMAGE_BASE_NAME
            .iter()
            .map(|&path| format!("/{path}.prepvt"))
            .collect();
        for path in &paths {
            mock_ctx.create_path(path);
            flags.push(path);
        }

        for _ in 0..MAX_RETRIES {
            mock_ctx
                .cmd_runner()
                .add_gsctool_interaction(flags.clone(), 3, "", "");
        }

        let result = gsc_update(&mut mock_ctx, Path::new("/"));
        assert_eq!(result, Err(HwsecError::GsctoolError(3)));
    }
}
