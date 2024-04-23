// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::str::SplitAsciiWhitespace;
use std::time::SystemTime;

use log::error;
use log::info;

use crate::context::Context;
use crate::error::HwsecError;
use crate::gsc::run_gsctool_cmd;
use crate::gsc::run_metrics_client;
use crate::gsc::GSC_METRICS_PREFIX;

// This should match the definitions in Cr50 firmware.
const CR50_FE_LOG_NVMEM: u64 = 5;
const CR50_FE_LOG_AP_RO_VERIFICATION: u64 = 9;
const CR50_APROF_CHECK_TRIGGERED: u64 = 3;

// This should match the definitions in Cr50 firmware.
const TI50_FE_LOG_NVMEM: u64 = 3;
const TI50_FE_LOG_AP_RO_VERIFICATION: u64 = 7;

// Offsets used to expand nvmem and apro verification FLOG codes.
const NVMEM_MALLOC: u64 = 200;
const APRO_OFFSET: u64 = 160;

// Events are reported in 1 byte.
const EVENT_ID_MAX: u64 = 255;
const EVENT_IGNORE_ENTRY: u64 = EVENT_ID_MAX + 1;

pub fn read_prev_timestamp_from_file(
    ctx: &mut impl Context,
    file_path: &str,
) -> Result<u64, HwsecError> {
    if !ctx.path_exists(file_path) {
        info!("{} not found, creating.", file_path);
        match ctx.write_contents_to_file(file_path, b"0") {
            Ok(_) => return Ok(0),
            Err(_) => return Err(HwsecError::FileError),
        }
    }

    let file_string = ctx.read_file_to_string(file_path)?;

    file_string
        .parse::<u64>()
        .map_err(|_| HwsecError::InternalError)
}

pub fn update_timestamp_file(
    ctx: &mut impl Context,
    new_stamp: u64,
    file_path: &str,
) -> Result<(), HwsecError> {
    ctx.write_contents_to_file(file_path, &new_stamp.to_ne_bytes())
}

pub fn set_log_file_time_base(ctx: &mut impl Context) -> Result<(), HwsecError> {
    let epoch_secs = match SystemTime::now().duration_since(SystemTime::UNIX_EPOCH) {
        Ok(epoch) => epoch.as_secs(),
        Err(_) => return Err(HwsecError::SystemTimeError),
    };

    let gsctool_result = run_gsctool_cmd(ctx, vec!["--any", "--tstamp", &epoch_secs.to_string()])?;
    if !gsctool_result.status.success() {
        error!("Failed to set Cr50 flash log time base to {}", epoch_secs);
        return Err(HwsecError::GsctoolError(
            gsctool_result.status.code().unwrap(),
        ));
    }
    info!("Set Cr50 flash log base time to {}", epoch_secs);
    Ok(())
}

fn get_next_u64_from_iterator(
    iter: &mut SplitAsciiWhitespace,
    radix: u32,
) -> Result<u64, HwsecError> {
    match iter.next() {
        None => {
            error!("Failed to parse gsctool log line");
            Err(HwsecError::InternalError)
        }
        Some(s) => u64::from_str_radix(s, radix).map_err(|_| {
            error!("Failed to parse gsctool log line");
            HwsecError::InternalError
        }),
    }
}

#[cfg(feature = "ti50_onboard")]
fn expand_cr50_event_id(_event_id: u64, _fe_log_event_id: u64) -> bool {
    return false;
}

#[cfg(feature = "cr50_onboard")]
fn expand_cr50_event_id(event_id: u64, fe_log_event_id: u64) -> bool {
    return event_id == fe_log_event_id;
}

#[cfg(feature = "ti50_onboard")]
fn expand_ti50_event_id(event_id: u64, fe_log_event_id: u64) -> bool {
    return event_id == fe_log_event_id;
}

#[cfg(feature = "cr50_onboard")]
fn expand_ti50_event_id(_event_id: u64, _fe_log_event_id: u64) -> bool {
    return false;
}

// The output from "gsctool -a -M -L 0" may look as follows:
//         1:00
// 1623743076:09 00
// 1623743077:09 02
// 1623743085:09 00
// 1623743086:09 01
// 1666170902:09 00
// 1666170905:09 02
fn parse_timestamp_and_event_id_from_log_entry(line: &str) -> Result<(u64, u64), HwsecError> {
    let binding = line.trim().replace(':', " ");
    let mut parts = binding.split_ascii_whitespace();
    let stamp: u64 = get_next_u64_from_iterator(&mut parts, 10)?;
    let mut event_id: u64 = get_next_u64_from_iterator(&mut parts, 16)?;
    let mut offset: u64 = 0;

    if event_id > EVENT_ID_MAX {
        return Err(HwsecError::InternalError);
    }

    // Events that expand the first byte.
    if expand_cr50_event_id(event_id, CR50_FE_LOG_NVMEM)
        || expand_ti50_event_id(event_id, TI50_FE_LOG_NVMEM)
    {
        offset = NVMEM_MALLOC;
    } else if expand_ti50_event_id(event_id, TI50_FE_LOG_AP_RO_VERIFICATION) {
        offset = APRO_OFFSET;
    }
    if offset != 0 {
        // If there's a non-zero offset, then return the expand the result using the
        // event offset and add the first byte of the payload.
        // ex: if event_id is 05 on a cr50 device, which is CR50_FE_LOG_NVMEM,
        // then adopt '200 + the first byte of payload' as an new event_id, as
        // defined as enum Cr50FlashLogs in
        // event_id=05, payload[0]=00, then new event id is 200, which is
        // labeled as 'Nvmem Malloc'.
        // https://chromium.googlesource.com/chromium/src/+//main:tools/metrics/
        // histograms/enums.xml.
        let payload_0: u64 = get_next_u64_from_iterator(&mut parts, 16)?;
        event_id = offset + payload_0;
    }
    // Cr50 AP RO verification events require special handling, because it logs
    // non-critical AP RO verification events. Handle it separately from the
    // other events that process the first byte.
    if expand_cr50_event_id(event_id, CR50_FE_LOG_AP_RO_VERIFICATION) {
        // If event_id is 09, which is CR50_FE_LOG_AP_RO_VERIFICATION, then adopt
        // '160 + the first byte of payload' as an new event_id, as defined as
        // enum Cr50FlashLogs in
        // https://chromium.googlesource.com/chromium/src/+//main:tools/metrics/
        // histograms/enums.xml.

        // For example, event_id=09, payload[0]=03, then new event id is 163, which
        // is labeled as 'AP RO Triggered'.
        let payload_0: u64 = get_next_u64_from_iterator(&mut parts, 16)?;
        // Ignore the codes less than CHECK_TRIGGERED. They're normal. Verification
        // did not run.
        if payload_0 < CR50_APROF_CHECK_TRIGGERED {
            event_id = EVENT_IGNORE_ENTRY;
        } else {
            event_id = APRO_OFFSET + payload_0;
        }
    }

    Ok((stamp, event_id))
}

pub fn gsc_flash_log(ctx: &mut impl Context, prev_stamp: u64) -> Result<u64, (HwsecError, u64)> {
    let Ok(gsctool_result) = run_gsctool_cmd(
        ctx,
        vec!["--any", "--machine", "--flog", &prev_stamp.to_string()],
    ) else {
        return Err((HwsecError::GsctoolError(1), 0));
    };

    if !gsctool_result.status.success() {
        error!("Failed to get flash log entries");
        return Err((
            HwsecError::GsctoolError(gsctool_result.status.code().unwrap()),
            0,
        ));
    }

    let Ok(content) = std::str::from_utf8(&gsctool_result.stdout) else {
        return Err((HwsecError::GsctoolResponseBadFormatError, 0));
    };

    let mut new_stamp: u64 = 0;
    for entry in content.lines() {
        let Ok((stamp, event_id)) = parse_timestamp_and_event_id_from_log_entry(entry) else {
            return Err((HwsecError::InternalError, new_stamp));
        };

        if event_id > EVENT_ID_MAX {
            continue;
        }

        let Ok(metrics_client_result) = run_metrics_client(
            ctx,
            vec![
                "-s",
                &format!("{}.FlashLog", GSC_METRICS_PREFIX),
                &format!("0x{:02x}", event_id),
            ],
        ) else {
            return Err((HwsecError::MetricsClientFailureError, new_stamp));
        };

        if metrics_client_result.status.code().unwrap() == 0 {
            new_stamp = stamp;
        } else {
            error!(
                "Failed to log event {} at timestamp {}",
                event_id, new_stamp
            );
            return Err((HwsecError::InternalError, new_stamp));
        }
    }
    Ok(new_stamp)
}

#[cfg(test)]
mod tests {
    use super::parse_timestamp_and_event_id_from_log_entry;
    use crate::context::mock::MockContext;
    use crate::context::Context;
    use crate::error::HwsecError;
    use crate::gsc::gsc_flash_log;

    const PREV_STAMP: u64 = 0;

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_entry_ok() {
        let line: &str = &format!("{:>10}:00", 1);
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        assert_eq!(result, Ok((1, 0)));
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_entry_ok_hex_event() {
        let line: &str = "100:0A";
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        assert_eq!(result, Ok((100, 10)));
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_entry_expand_cr50_nvmem() {
        use super::CR50_FE_LOG_NVMEM;
        use super::NVMEM_MALLOC;

        let line: &str = &format!("{:>10}:{:02x} 00", 1, CR50_FE_LOG_NVMEM);
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        if cfg!(feature = "cr50_onboard") {
            assert_eq!(result, Ok((1, NVMEM_MALLOC)));
        } else {
            assert_eq!(result, Ok((1, CR50_FE_LOG_NVMEM)));
        }
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_entry_expand_ti50_nvmem() {
        use super::NVMEM_MALLOC;
        use super::TI50_FE_LOG_NVMEM;

        let line: &str = &format!("{:>10}:{:02x} 00", 1, TI50_FE_LOG_NVMEM);
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        if cfg!(feature = "ti50_onboard") {
            assert_eq!(result, Ok((1, NVMEM_MALLOC)));
        } else {
            assert_eq!(result, Ok((1, TI50_FE_LOG_NVMEM)));
        }
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_entry_expand_cr50_apro() {
        use super::APRO_OFFSET;
        use super::CR50_APROF_CHECK_TRIGGERED;
        use super::CR50_FE_LOG_AP_RO_VERIFICATION;

        let line: &str = &format!(
            "{:>10}:{:02x} {:02x}",
            1, CR50_FE_LOG_AP_RO_VERIFICATION, CR50_APROF_CHECK_TRIGGERED
        );
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        if cfg!(feature = "cr50_onboard") {
            assert_eq!(result, Ok((1, APRO_OFFSET + CR50_APROF_CHECK_TRIGGERED)));
        } else {
            assert_eq!(result, Ok((1, CR50_FE_LOG_AP_RO_VERIFICATION)));
        }
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_entry_expand_cr50_ignored_apro() {
        use super::CR50_FE_LOG_AP_RO_VERIFICATION;
        use super::EVENT_IGNORE_ENTRY;

        let line: &str = &format!("{:>10}:{:02x} 02", 1, CR50_FE_LOG_AP_RO_VERIFICATION);
        let result = parse_timestamp_and_event_id_from_log_entry(line);

        if cfg!(feature = "cr50_onboard") {
            assert_eq!(result, Ok((1, EVENT_IGNORE_ENTRY)));
        } else {
            assert_eq!(result, Ok((1, CR50_FE_LOG_AP_RO_VERIFICATION)));
        }
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_entry_expand_ti50_apro() {
        use super::APRO_OFFSET;
        use super::CR50_APROF_CHECK_TRIGGERED;
        use super::TI50_FE_LOG_AP_RO_VERIFICATION;

        let line: &str = &format!(
            "{:>10}:{:02x} {:02x}",
            1, TI50_FE_LOG_AP_RO_VERIFICATION, CR50_APROF_CHECK_TRIGGERED
        );
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        if cfg!(feature = "ti50_onboard") {
            assert_eq!(result, Ok((1, APRO_OFFSET + CR50_APROF_CHECK_TRIGGERED)));
        } else {
            assert_eq!(result, Ok((1, TI50_FE_LOG_AP_RO_VERIFICATION)));
        }
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_entry_expand_ti50_apro_ok() {
        use super::APRO_OFFSET;
        use super::TI50_FE_LOG_AP_RO_VERIFICATION;

        // Cr50 reporting drops ap ro verification events less than 3. Make sure
        // Ti50 doesn't.
        let line: &str = &format!("{:>10}:{:02x} 00", 1, TI50_FE_LOG_AP_RO_VERIFICATION);
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        if cfg!(feature = "ti50_onboard") {
            assert_eq!(result, Ok((1, APRO_OFFSET)));
        } else {
            assert_eq!(result, Ok((1, TI50_FE_LOG_AP_RO_VERIFICATION)));
        }
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_entry_not_integer() {
        let line: &str = "TEST";
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        assert_eq!(result, Err(HwsecError::InternalError));
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_entry_hex_timestamp() {
        let line: &str = "A:00";
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        assert_eq!(result, Err(HwsecError::InternalError));
    }

    #[test]
    fn test_parse_large_event_id() {
        use super::EVENT_ID_MAX;
        let line: &str = &format!("{:>10}:{:02x}", 1, EVENT_ID_MAX + 1);
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        assert_eq!(result, Err(HwsecError::InternalError));
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_entry_missing_event_id() {
        let line: &str = &format!("{:>10}", 1);
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        assert_eq!(result, Err(HwsecError::InternalError));
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_cr50_nvmem_missing_payload_0() {
        use super::CR50_FE_LOG_NVMEM;

        let line: &str = &format!("{:>10}:{:02x}", 1, CR50_FE_LOG_NVMEM);
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        if cfg!(feature = "cr50_onboard") {
            assert_eq!(result, Err(HwsecError::InternalError));
        } else {
            assert_eq!(result, Ok((1, CR50_FE_LOG_NVMEM)));
        }
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_ti50_nvmem_missing_payload_0() {
        use super::TI50_FE_LOG_NVMEM;

        let line: &str = &format!("{:>10}:{:02x}", 1, TI50_FE_LOG_NVMEM);
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        if cfg!(feature = "ti50_onboard") {
            assert_eq!(result, Err(HwsecError::InternalError));
        } else {
            assert_eq!(result, Ok((1, TI50_FE_LOG_NVMEM)));
        }
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_cr50_apro_missing_payload_0() {
        use super::CR50_FE_LOG_AP_RO_VERIFICATION;

        let line: &str = &format!("{:>10}:{:02x}", 1, CR50_FE_LOG_AP_RO_VERIFICATION);
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        if cfg!(feature = "cr50_onboard") {
            assert_eq!(result, Err(HwsecError::InternalError));
        } else {
            assert_eq!(result, Ok((1, CR50_FE_LOG_AP_RO_VERIFICATION)));
        }
    }

    #[test]
    fn test_parse_timestamp_and_event_id_from_log_ti50_apro_missing_payload_0() {
        use super::TI50_FE_LOG_AP_RO_VERIFICATION;

        let line: &str = &format!("{:>10}:{:02x}", 1, TI50_FE_LOG_AP_RO_VERIFICATION);
        let result = parse_timestamp_and_event_id_from_log_entry(line);
        if cfg!(feature = "ti50_onboard") {
            assert_eq!(result, Err(HwsecError::InternalError));
        } else {
            assert_eq!(result, Ok((1, TI50_FE_LOG_AP_RO_VERIFICATION)));
        }
    }

    #[test]
    fn test_gsc_flash_log_empty_flash_log() {
        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["--any", "--machine", "--flog", "0"],
            0,
            "",
            "",
        );

        let result = gsc_flash_log(&mut mock_ctx, PREV_STAMP);
        assert_eq!(result, Ok(0));
    }

    #[test]
    fn test_gsc_flash_log_multiple_lines_flash_log() {
        use super::APRO_OFFSET;
        use super::CR50_APROF_CHECK_TRIGGERED;
        use super::CR50_FE_LOG_AP_RO_VERIFICATION;

        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["--any", "--machine", "--flog", "0"],
            0,
            &format!(
                "{:>10}:00\n{:>10}:{:02x} {:02x}\n{:>10}:{:02x} {:02x}",
                1,
                2,
                CR50_FE_LOG_AP_RO_VERIFICATION,
                CR50_APROF_CHECK_TRIGGERED,
                3,
                CR50_FE_LOG_AP_RO_VERIFICATION,
                CR50_APROF_CHECK_TRIGGERED
            ),
            "",
        );

        mock_ctx.cmd_runner().add_metrics_client_expectation(0);

        let mut expected_event: u64 = CR50_FE_LOG_AP_RO_VERIFICATION;
        if cfg!(feature = "cr50_onboard") {
            expected_event = APRO_OFFSET + CR50_APROF_CHECK_TRIGGERED;
        }

        for _ in 0..2 {
            mock_ctx
                .cmd_runner()
                .add_metrics_client_expectation(expected_event);
        }

        let result = gsc_flash_log(&mut mock_ctx, PREV_STAMP);
        assert_eq!(result, Ok(3));
    }

    #[test]
    fn test_gsc_flash_log_event_too_large() {
        use super::EVENT_ID_MAX;

        let last_good_timestamp: u64 = 75;
        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["--any", "--machine", "--flog", "0"],
            0,
            &format!(
                "{:>10}:00\n{:>10}:{:02x}\n{:>10}:{:02x}\n{:>10}:{:02x}",
                1,
                2,
                EVENT_ID_MAX - 1,
                last_good_timestamp,
                EVENT_ID_MAX,
                4,
                EVENT_ID_MAX + 1
            ),
            "",
        );

        mock_ctx.cmd_runner().add_metrics_client_expectation(0);
        mock_ctx
            .cmd_runner()
            .add_metrics_client_expectation(EVENT_ID_MAX - 1);
        mock_ctx
            .cmd_runner()
            .add_metrics_client_expectation(EVENT_ID_MAX);

        let result = gsc_flash_log(&mut mock_ctx, PREV_STAMP);
        assert_eq!(
            result,
            Err((HwsecError::InternalError, last_good_timestamp))
        );
    }

    #[test]
    fn test_gsc_flash_log_multiple_lines_cr50_ignore_apro() {
        use super::CR50_APROF_CHECK_TRIGGERED;
        use super::CR50_FE_LOG_AP_RO_VERIFICATION;

        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["--any", "--machine", "--flog", "0"],
            0,
            &format!(
                "{:>10}:00\n{:>10}:{:02x} {:02x}",
                1,
                2,
                CR50_FE_LOG_AP_RO_VERIFICATION,
                CR50_APROF_CHECK_TRIGGERED - 1
            ),
            "",
        );

        mock_ctx.cmd_runner().add_metrics_client_expectation(0);

        let mut num_cmds: u64 = 1;
        if !cfg!(feature = "cr50_onboard") {
            num_cmds = 2;
            mock_ctx
                .cmd_runner()
                .add_metrics_client_expectation(CR50_FE_LOG_AP_RO_VERIFICATION);
        }

        let result = gsc_flash_log(&mut mock_ctx, PREV_STAMP);
        assert_eq!(result, Ok(num_cmds));
    }

    #[test]
    fn test_gsc_flash_log_multiple_lines_ti50_apro() {
        use super::APRO_OFFSET;
        use super::CR50_APROF_CHECK_TRIGGERED;
        use super::TI50_FE_LOG_AP_RO_VERIFICATION;

        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["--any", "--machine", "--flog", "0"],
            0,
            &format!(
                "{:>10}:00\n{:>10}:{:02x} {:02x}\n{:>10}:{:02x} {:02x}",
                1,
                2,
                TI50_FE_LOG_AP_RO_VERIFICATION,
                CR50_APROF_CHECK_TRIGGERED - 1,
                3,
                TI50_FE_LOG_AP_RO_VERIFICATION,
                CR50_APROF_CHECK_TRIGGERED
            ),
            "",
        );

        mock_ctx.cmd_runner().add_metrics_client_expectation(0);

        if cfg!(feature = "ti50_onboard") {
            mock_ctx
                .cmd_runner()
                .add_metrics_client_expectation(APRO_OFFSET + CR50_APROF_CHECK_TRIGGERED - 1);
            mock_ctx
                .cmd_runner()
                .add_metrics_client_expectation(APRO_OFFSET + CR50_APROF_CHECK_TRIGGERED);
        } else {
            for _ in 0..2 {
                mock_ctx
                    .cmd_runner()
                    .add_metrics_client_expectation(TI50_FE_LOG_AP_RO_VERIFICATION);
            }
        }

        let result = gsc_flash_log(&mut mock_ctx, PREV_STAMP);
        assert_eq!(result, Ok(3));
    }

    #[test]
    fn test_gsc_flash_log_event_gsctool_error() {
        let mut mock_ctx = MockContext::new();
        mock_ctx.cmd_runner().add_gsctool_interaction(
            vec!["--any", "--machine", "--flog", "0"],
            1,
            "",
            "",
        );

        let result = gsc_flash_log(&mut mock_ctx, PREV_STAMP);
        assert_eq!(result, Err((HwsecError::GsctoolError(1), 0)));
    }

    #[test]
    fn test_read_prev_timestamp_from_file_ok() {
        use super::read_prev_timestamp_from_file;
        let mut mock_ctx = MockContext::new();
        mock_ctx.create_path("mock_file_path");
        assert_eq!(
            mock_ctx.write_contents_to_file("mock_file_path", b"1"),
            Ok(())
        );
        let result = read_prev_timestamp_from_file(&mut mock_ctx, "mock_file_path");
        assert_eq!(result, Ok(1));
    }

    #[test]
    fn test_read_prev_timestamp_from_file_not_exist() {
        use super::read_prev_timestamp_from_file;
        let mut mock_ctx = MockContext::new();
        let result = read_prev_timestamp_from_file(&mut mock_ctx, "mock_file_path");
        assert_eq!(result, Ok(0));
    }

    #[test]
    fn test_read_prev_timestamp_from_file_multiple_lines() {
        use super::read_prev_timestamp_from_file;
        let mut mock_ctx = MockContext::new();
        mock_ctx.create_path("mock_file_path");
        assert_eq!(
            mock_ctx.write_contents_to_file("mock_file_path", b"1\n2"),
            Ok(())
        );
        let result = read_prev_timestamp_from_file(&mut mock_ctx, "mock_file_path");
        assert_eq!(result, Err(HwsecError::InternalError));
    }
    #[test]
    fn test_read_prev_timestamp_from_file_not_u64() {
        use super::read_prev_timestamp_from_file;
        let mut mock_ctx = MockContext::new();
        mock_ctx.create_path("mock_file_path");
        assert_eq!(
            mock_ctx.write_contents_to_file("mock_file_path", b"test"),
            Ok(())
        );
        let result = read_prev_timestamp_from_file(&mut mock_ctx, "mock_file_path");
        assert_eq!(result, Err(HwsecError::InternalError));
    }
}
