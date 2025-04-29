// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;
use std::path::{Path, PathBuf};

use crate::ccenc::execution_utils::execute;
use crate::ccenc::quality::Decoder;
use crate::{EncodeTestError, WebMFile};

fn get_vpxdec_args(webm_file_path: &Path, yuv_file_path: &Path) -> Vec<String> {
    vec![
        webm_file_path.display().to_string(),
        "-o".to_string(),
        yuv_file_path.display().to_string(),
        "--codec=vp9".to_string(),
        "--i420".to_string(),
    ]
}

/// decode_in_i420 decodes a WebM video file and saved in an I420 file.
/// The returned value is the path of the created an I420 file.
/// The input WebM file must be vp9 webm file. They are generated from raw YUV data by libvpx
/// like  "vpxenc foo.yuv -o foo.webm --codec=vp9 -w <width> -h <height> --lossless=1".
/// Please use "--lossless=1" option. Lossless compression is required to ensure we are testing
/// streams at the same quality as original raw streams, to test encoder capabilities
/// (performance, bitrate convergence, etc.) correctly and with sufficient complexity/PSNR.
pub fn decode_in_i420(webm_file: &WebMFile) -> Result<PathBuf, EncodeTestError> {
    let webm_suffix = "vp9.webm";
    if !webm_file.name.ends_with(webm_suffix) {
        log::error!("Source video {:?} must be VP9 WebM", webm_file.path);
        return Err(EncodeTestError::InvalidWebMFile(webm_file.path.display().to_string()));
    }

    let yuv_file_name = webm_file.name.strip_suffix(webm_suffix).unwrap().to_string() + "i420.yuv";
    let yuv_file_path = PathBuf::from(&yuv_file_name);

    if let Err(e) = File::create(&yuv_file_path) {
        return Err(EncodeTestError::IoError(format!(
            "Failed to create a temporary YUV file {:?}: {:?}",
            yuv_file_path, e
        )));
    }

    let vpxdec_args = get_vpxdec_args(&webm_file.path, &yuv_file_path);
    let vpxdec_args_str = vpxdec_args.iter().map(String::as_str).collect::<Vec<_>>();
    let vpxdec_command = Decoder::Libvpx.command();
    execute(&vpxdec_command, &vpxdec_args_str, None, None)?;

    Ok(yuv_file_path)
}
