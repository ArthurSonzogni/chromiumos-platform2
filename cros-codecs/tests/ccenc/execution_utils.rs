// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::env::current_exe;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitStatus, Stdio};

use crate::ccenc::quality::{compare_files, Decoder};
use crate::ccenc::yuv::decode_in_i420;
use crate::{EncodeTestError, Resolution, WebMFile};

const CCENC_BINARY: &str = "ccenc";

/// Returns the parent directory of the caller executable.
pub fn get_parent_path() -> PathBuf {
    current_exe()
        .unwrap()
        .parent()
        .expect("Could not get parent directory of executable")
        .to_path_buf()
}

fn get_ccenc_path() -> PathBuf {
    get_parent_path().join(CCENC_BINARY)
}

fn get_ccenc_args(
    yuv_file: &Path,
    size: Resolution,
    fps: usize,
    output_format: &str,
) -> Result<(Vec<String>, PathBuf, i32), EncodeTestError> {
    let mut args = vec![
        yuv_file.display().to_string(),
        "--width".to_string(),
        size.width.to_string(),
        "--height".to_string(),
        size.height.to_string(),
        "--framerate".to_string(),
        fps.to_string(),
        "--fourcc".to_string(),
        "i420".to_string(),
    ];

    let yuv_metadata = fs::metadata(yuv_file).unwrap();
    let yuv_file_size = yuv_metadata.len();
    let yuv_frame_size =
        size.width * size.height + ((size.width + 1) >> 1) * ((size.height + 1) >> 1) * 2;
    if yuv_file_size % yuv_frame_size != 0 {
        return Err(EncodeTestError::InvalidYuvFileSize { yuv_file_size, yuv_frame_size });
    }
    let num_frames = yuv_file_size / yuv_frame_size;
    args.push("--count".to_string());
    args.push(num_frames.to_string());

    let mut target_bitrate = (0.1 * fps as f64 * size.width as f64 * size.height as f64) as i32;
    let encoded_file_path = match output_format {
        "vp9" => yuv_file.with_extension("ivf"),
        "h264" => yuv_file.with_extension("h264"),
        _ => {
            return Err(EncodeTestError::UnsupportedOutputFormat(output_format.to_string()));
        }
    };
    args.push("--codec".to_string());
    args.push(output_format.to_string());

    if output_format == "vp9" {
        // VP9 encoding is LP only on newer Intel devices.
        args.push("--low-power".to_string());

        // VP9 is 30% more efficient than H264
        target_bitrate = (target_bitrate as f64 * 0.7) as i32;
    }

    args.push("--output".to_string());
    let encoded_file = encoded_file_path.display().to_string();
    args.push(encoded_file);
    args.push("--bitrate".to_string());
    args.push(target_bitrate.to_string());
    Ok((args, encoded_file_path, target_bitrate))
}

/// Calculates the bitrate of an encoded file.
fn calculate_bitrate(
    encoded_file_path: &Path,
    fps: usize,
    num_frames: usize,
) -> Result<f64, EncodeTestError> {
    let metadata = fs::metadata(encoded_file_path).map_err(|e| {
        EncodeTestError::IoError(format!(
            "Failed to get metadata for file {:?}: {}",
            encoded_file_path, e
        ))
    })?;
    let file_size_bytes = metadata.len();
    assert_ne!(num_frames, 0);
    Ok((file_size_bytes as f64) * 8.0 * (fps as f64) / (num_frames as f64))
}

/// Writes to a log file in append mode.
fn write_to_log(output: &str, log_path: Option<&Path>) {
    if let Some(path) = log_path {
        if let Ok(mut file) = OpenOptions::new().create(true).append(true).open(path) {
            if let Err(err) = file.write_all(output.as_bytes()) {
                log::error!("Failed to append stdout to {:?}: {}", path, err);
            }
        } else {
            log::error!("Failed to open log file {:?}", path);
        }
    }
}

/// Executes an external command and optionally logs its stdout and stderr to files.
pub fn execute(
    test_binary_path: &Path,
    args: &[&str],
    stdout_path: Option<&Path>,
    stderr_path: Option<&Path>,
) -> Result<ExitStatus, EncodeTestError> {
    let mut command = Command::new(test_binary_path);
    command.args(args);

    command.stdout(Stdio::piped()).stderr(Stdio::piped());
    let output_result = command.output();
    let command_str = format!("{} {}", test_binary_path.display(), args.join(" "));
    log::info!("Executing command: {}", command_str);

    match output_result {
        Ok(output) => {
            let status = output.status;
            let stdout_output = String::from_utf8_lossy(&output.stdout);
            write_to_log(&stdout_output, stdout_path);
            let stderr_output = String::from_utf8_lossy(&output.stderr);
            write_to_log(&stderr_output, stderr_path);
            if status.success() {
                log::info!("Command succeeded: {}", command_str);
                Ok(status)
            } else {
                log::error!("Command: {} failed with error: {:?}", command_str, stderr_output);
                Err(EncodeTestError::CommandExecutionError {
                    command: command_str,
                    stderr: format!("{}", stderr_output),
                })
            }
        }
        Err(err) => Err(EncodeTestError::IoError(format!(
            "Error executing command: {} with error: {:?}",
            command_str, err
        ))),
    }
}

/// Runs cros-codecs integration test for a WebM file.
pub fn cros_codecs_encode(webm_file: &WebMFile, output_format: &str, decoder: Decoder) {
    let test_binary_path = get_ccenc_path();
    assert!(webm_file.path.exists(), "{:?} is not a valid path", webm_file.path);

    let yuv_file_path = decode_in_i420(webm_file).unwrap_or_else(|err| {
        log::error!("Failed to prepare YUV file {:?}: {:?}", webm_file.name, err);
        panic!("Failed to prepare YUV file {:?}: {:?}", webm_file.name, err);
    });
    assert!(yuv_file_path.exists(), "{:?} is not a valid path", yuv_file_path);

    let (ccenc_args, encoded_file_path, target_bitrate) =
        get_ccenc_args(&yuv_file_path, webm_file.size, webm_file.fps, output_format)
            .unwrap_or_else(|err| {
                log::error!("Failed to get ccenc args: {:?}", err);
                panic!("Failed to get ccenc args: {:?}", err);
            });

    let ccenc_args_str = ccenc_args.iter().map(String::as_str).collect::<Vec<_>>();
    if let Err(err) = execute(&test_binary_path, &ccenc_args_str, None, None) {
        log::error!("Failed to encode {:?} with ccenc: {:?}", yuv_file_path, err);
        panic!("Failed to encode {:?} with ccenc: {:?}", yuv_file_path, err);
    }
    assert!(encoded_file_path.exists(), "{:?} is not a valid path", encoded_file_path);

    let out_dir = get_parent_path();
    let (psnr, ssim) = compare_files(
        &yuv_file_path,
        &encoded_file_path,
        &out_dir,
        decoder,
        webm_file.size,
        webm_file.num_frames,
    )
    .unwrap_or_else(|err| {
        log::error!("Failed to decode and compare results: {:?}", err);
        panic!("Failed to decode and compare results: {:?}", err);
    });
    log::info!("SSIM: {:?}, PSNR: {:?}", ssim * 100.0, psnr);

    let actual_bitrate = calculate_bitrate(&encoded_file_path, webm_file.fps, webm_file.num_frames)
        .unwrap_or_else(|err| {
            log::error!("Failed to calculate resulting bitrate: {:?}", err);
            panic!("Failed to calculate resulting bitrate: {:?}", err);
        });
    let bitrate_deviation = (100.0 * actual_bitrate / (target_bitrate as f64)) - 100.0;
    log::info!("Actual bitrate: {:?}", actual_bitrate);
    log::info!("Bitrate Deviations: {:?}%", bitrate_deviation);

    // TODO(b/412365877): Counts IDR frame/keyframe in file using ffprobe.
}
