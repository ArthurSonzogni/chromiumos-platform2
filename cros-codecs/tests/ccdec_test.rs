// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    use std::env::current_exe;
    use std::path::Path;
    use std::path::PathBuf;
    use std::process::{Command, ExitStatus};

    const CCDEC_BINARY: &str = "ccdec";

    fn get_ccdec_path() -> PathBuf {
        let parent_test_path = current_exe()
            .unwrap()
            .parent()
            .expect("Could not get parent directory of executable")
            .to_path_buf();
        parent_test_path.join(CCDEC_BINARY)
    }

    fn get_ccdec_args(
        test_file_path: &Path,
        json_file_path: &Path,
        input_format: &str,
    ) -> Vec<String> {
        vec![
            test_file_path.display().to_string(),
            "--golden".to_string(),
            json_file_path.display().to_string(),
            "--input-format".to_string(),
            input_format.to_string(),
        ]
    }

    type ExecuteResult = Result<ExitStatus, String>;
    fn execute(test_binary_path: &PathBuf, args: &[&str]) -> ExecuteResult {
        let mut command = Command::new(test_binary_path);
        command.args(args);
        let output = command.status();

        match output {
            Ok(status) => {
                if status.success() {
                    log::info!("Command succeeded: {:?}", command);
                    Ok(status)
                } else {
                    log::error!("Command failed: {:?} with status: {:?}", command, status);
                    Err(format!("Command failed with status: {:?}", status))
                }
            }
            Err(err) => Err(format!("Error executing command: {}", err)),
        }
    }

    fn cros_codecs_decode(test_file: &str, input_format: &str) {
        let test_binary_path = get_ccdec_path();
        let test_file_path = Path::new(&test_file);
        assert!(test_file_path.exists(), "{:?} is not a valid path", test_file_path);

        let json_file = format!("{}.json", test_file_path.display());
        let json_file_path = Path::new(&json_file);
        assert!(json_file_path.exists(), "{:?} is not a valid path", json_file_path);

        let ccdec_args = get_ccdec_args(test_file_path, json_file_path, input_format);
        let ccdec_args_str = ccdec_args.iter().map(String::as_str).collect::<Vec<_>>();

        let test_file_name = test_file_path.file_name().unwrap().to_str().unwrap();
        assert!(
            execute(&test_binary_path, &ccdec_args_str).is_ok(),
            "Cros-codecs decode test failed: {}",
            test_file_name
        );
        log::info!("Cros-codecs decode test succeeded: {}", test_file_name);
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn av1_decode() {
        const AV1_DATA_PATH: &str = "src/codec/av1/test_data/test-25fps.av1.ivf";
        cros_codecs_decode(AV1_DATA_PATH, "av1");
    }

    #[test]
    fn h264_decode() {
        const H264_DATA_PATH: &str = "src/codec/h264/test_data/test-25fps.h264";
        cros_codecs_decode(H264_DATA_PATH, "h264");
    }

    #[test]
    #[cfg(target_arch = "x86_64")]
    fn h265_decode() {
        const H265_DATA_PATH: &str = "src/codec/h265/test_data/test-25fps.h265";
        cros_codecs_decode(H265_DATA_PATH, "h265");
    }

    #[test]
    fn vp8_decode() {
        const VP8_DATA_PATH: &str = "src/codec/vp8/test_data/test-25fps.vp8";
        cros_codecs_decode(VP8_DATA_PATH, "vp8");
    }

    #[test]
    fn vp9_decode() {
        const VP9_DATA_PATH: &str = "src/codec/vp9/test_data/test-25fps.vp9";
        cros_codecs_decode(VP9_DATA_PATH, "vp9");
    }
}
