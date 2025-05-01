// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use chrono::prelude::Utc;
use lazy_static::lazy_static;
use regex::Regex;
use std::fs::{self, File};
use std::path::{Path, PathBuf};
use std::str::FromStr;

use crate::ccenc::execution_utils::execute;
use crate::{EncodeTestError, Resolution};

lazy_static! {
    // regexp to find the PSNR output in the tiny_ssim log.
    static ref REGEX_PSNR: Regex = Regex::new(r"(?m)^GlbPSNR:\s*(\d+\.\d+)").unwrap();
    // regexp to find the SSIM output in the tiny_ssim log.
    static ref REGEX_SSIM: Regex = Regex::new(r"(?m)^SSIM:\s*(\d+\.\d+)").unwrap();
}

/// Represents a command-line software decoder tool.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Decoder {
    /// TODO(b/403386695): Support SW decoders for other codecs.
    /// An H264 decoder built from https://github.com/ittiam-systems/libavc.
    Libavc,
    /// A VP8 and VP9 decoder built from https://chromium.googlesource.com/webm/libvpx/.
    Libvpx,
}

/// Decoder is a command line software decoder that can be used in compare_files().
impl Decoder {
    /// Returns the path to the decoder executable.
    pub fn command(&self) -> PathBuf {
        match self {
            Decoder::Libvpx => Path::new("vpxdec").to_path_buf(),
            &Decoder::Libavc => Path::new("avcdec").to_path_buf(),
        }
    }

    /// Returns the string of the decoder tool.
    pub fn name(&self) -> &'static str {
        match self {
            Decoder::Libvpx => "vpxdec",
            &Decoder::Libavc => "avcdec",
        }
    }
}

/// compare_files decodes encoded_file_path using software decoder
/// and compares it with yuv_file_path using tiny_ssim.
/// PSNR and SSIM values are returned on success (VMAF is under progress).
/// Caveats: This creates log files in out_dir. Calling twice overwrites the files.
/// TODO(b/411204913): Setup vmaf for encoding tests.
pub fn compare_files(
    yuv_file_path: &Path,
    encoded_file_path: &Path,
    out_dir: &Path,
    decoder: Decoder,
    size: Resolution,
    num_frames: usize,
) -> Result<(f64, f64), EncodeTestError> {
    log::info!(
        "Comparing original {:?} with decoded version of {:?}",
        yuv_file_path,
        encoded_file_path
    );

    // Constructs path for the temporary YUV file by appending ".temp" to the
    // original filename within out_dir.
    let parent_dir = yuv_file_path.parent().ok_or_else(|| {
        EncodeTestError::IoError(format!("Could not get parent directory of {:?}", yuv_file_path))
    })?;
    let temp_yuv_path = yuv_file_path
        .file_name()
        .map(|name| parent_dir.join(format!("{}.temp", name.to_string_lossy())))
        .ok_or_else(|| {
            EncodeTestError::IoError(format!(
                "Could not extract filename from path: {:?}",
                yuv_file_path
            ))
        })?;

    if let Err(e) = File::create(&temp_yuv_path) {
        return Err(EncodeTestError::IoError(format!(
            "Failed to create temp YUV file {:?}: {}",
            temp_yuv_path, e
        )));
    }

    let decode_args = match decoder {
        Decoder::Libavc => {
            vec![
                "--input".to_string(),
                encoded_file_path.display().to_string(),
                "--output".to_string(),
                temp_yuv_path.display().to_string(),
                // avcdec expects each argument and its value to be separate.
                "--save_output".to_string(),
                "1".to_string(),
                "--num_frames".to_string(),
                "-1".to_string(),
            ]
        }
        Decoder::Libvpx => {
            vec![
                encoded_file_path.display().to_string(),
                "--i420".to_string(), // Assume I420 output format
                "-o".to_string(),
                temp_yuv_path.display().to_string(),
                // TODO(b/400791632): The encoded IVF test file lacks an EOS marker
                // for vpxdec to recognize. Use --limit to explicitly specify the
                // expected frame counts.
                format!("--limit={}", num_frames),
            ]
        }
    };

    let decode_args_str = decode_args.iter().map(String::as_str).collect::<Vec<_>>();
    let decoder_name = decoder.name();
    let decoder_log_path = get_command_log_path(out_dir, decoder_name, decoder_name);
    execute(
        &decoder.command(),
        &decode_args_str,
        Some(&decoder_log_path),
        Some(&decoder_log_path),
    )?;

    let ssim_command = Path::new("tiny_ssim");
    let size_str = format!("{}x{}", size.width, size.height);
    let ssim_args =
        [yuv_file_path.display().to_string(), temp_yuv_path.display().to_string(), size_str];
    let ssim_args_str = ssim_args.iter().map(String::as_str).collect::<Vec<_>>();
    let ssim_log_path = get_command_log_path(out_dir, "tiny_ssim", decoder_name);
    execute(ssim_command, &ssim_args_str, Some(&ssim_log_path), None)?;
    let tiny_ssim_values =
        extract_float_values(&ssim_log_path, &[("PSNR", &REGEX_PSNR), ("SSIM", &REGEX_SSIM)])?;

    if tiny_ssim_values.len() != 2 {
        return Err(EncodeTestError::ParseError {
            file: ssim_log_path,
            details: format!("Expected 2 values (PSNR, SSIM) but found {}", tiny_ssim_values.len()),
        });
    }
    let psnr = tiny_ssim_values[0];
    let ssim = tiny_ssim_values[1];
    Ok((psnr, ssim))
}

/// extract_float_values parses a file using regexes and returns matched f64 values.
fn extract_float_values(
    file_path: &Path,
    regexes: &[(&str, &Regex)],
) -> Result<Vec<f64>, EncodeTestError> {
    let content = fs::read_to_string(file_path).map_err(|e| {
        EncodeTestError::IoError(format!("Failed to read file {:?}: {}", file_path, e))
    })?;
    let mut results = Vec::with_capacity(regexes.len());

    for (value_name, regex) in regexes {
        let captures: Vec<_> = regex.captures_iter(&content).collect();

        if captures.len() != 1 {
            return Err(EncodeTestError::ParseError {
                file: file_path.to_path_buf(),
                details: format!(
                    "Found {} matches for {} ({}); want exactly 1",
                    captures.len(),
                    value_name,
                    regex.as_str()
                ),
            });
        }

        // Proceed with the single match found
        if let Some(value_match) = captures[0].get(1) {
            let value_str = value_match.as_str();
            match f64::from_str(value_str) {
                Ok(value) => results.push(value),
                Err(parse_err) => {
                    return Err(EncodeTestError::ParseError {
                        file: file_path.to_path_buf(),
                        details: format!(
                            "Failed to parse '{}' as f64 for {}: {}",
                            value_str, value_name, parse_err
                        ),
                    });
                }
            }
        }
    }
    Ok(results)
}

// Test methods are run within the same module execution so artifacts
// including logs are shared between them. This function generates unique log
// file names to avoid collisions between test methods.
fn get_command_log_path(out_dir: &Path, command: &str, decoder_name: &str) -> PathBuf {
    let time = Utc::now();
    Path::new(out_dir).join(format!(
        "{}_{}_{}.txt",
        command,
        decoder_name,
        time.format("%Y-%m-%d_%H:%M:%S")
    ))
}
