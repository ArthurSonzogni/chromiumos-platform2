// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Context, Result};
use log::{error, info};
use std::{
    fs::File,
    io::BufReader,
    path::{Path, PathBuf},
    process::Command,
};
use tar::Archive;
use xz2::bufread::XzDecoder;

/// Executes a command and logs its result. Returns an error in case something
/// goes wrong.
pub fn execute_command(mut command: Command) -> Result<()> {
    info!("Executing command: {:?}", command);

    match command.output() {
        Ok(output) => {
            if output.status.success() {
                return Ok(());
            }

            let stdout = String::from_utf8_lossy(&output.stdout);
            let stderr = String::from_utf8_lossy(&output.stderr);
            let code = output.status.code().unwrap_or(1);
            error!(
                "Command {command:?} failed with code {code}\nStdout:\n{stdout}\nStderr:\n{stderr}"
            );
            bail!("Unable to execute command: Got non-zero status code");
        }
        Err(err) => {
            error!("Unable to execute command: {err}");
            bail!(err);
        }
    }
}

/// Uncompresses a tar from `src` to `dst`. In this case `src` needs to point to
/// a tar archive and `dst` to a folder where the items are unpacked to. This
/// also returns an `Vec<PathBuf>` of the entries that have been successfully
/// unpacked to `dst`. Please note that these paths are relative to `dst´.
pub fn uncompress_tar_xz(src: &Path, dst: &Path) -> Result<Vec<PathBuf>> {
    let file = File::open(src).context("Unable to open tar archive")?;
    let xz_decoder = XzDecoder::new(BufReader::new(file));

    let mut result: Vec<PathBuf> = vec![];
    let mut archive = Archive::new(xz_decoder);
    for entry in archive
        .entries()
        .context("Unable to access all contents of the tar")?
    {
        let mut entry = entry.context("Unable to read entry of the tar")?;
        entry
            .unpack_in(dst)
            .context("Unable to unpack entry of the tar")?;

        result.push(
            entry
                .path()
                .context("Unable to get tar entries path")?
                .to_path_buf(),
        );
    }

    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    const FILE_CONTENTS: &[u8] = b"Hello World!";
    const FILE_NAME: &str = "foo.txt";
    const TAR_NAME: &str = "foo.tar.xz";

    fn setup_tar_xz() -> Result<tempfile::TempDir> {
        // First setup a tempdir.
        let tempdir = tempfile::tempdir()?;

        // Next create a file in there.
        let file_path = tempdir.path().join(FILE_NAME);
        std::fs::write(file_path, FILE_CONTENTS)?;

        // Now create a tar.xz of that file.
        let mut tar_cmd = Command::new("tar");
        // Tell tar to (c)ompress an xz (J) of a (f)ile.
        tar_cmd.arg("-cJf").arg(tempdir.path().join(TAR_NAME));
        // Change dir to the temp path so that that the file is added
        // without a directory prefix.
        tar_cmd.arg("-C");
        tar_cmd.arg(tempdir.path());
        // We want to compress the newly created file.
        tar_cmd.arg(FILE_NAME);
        execute_command(tar_cmd)?;

        Ok(tempdir)
    }

    #[test]
    fn test_uncompress_tar_xz() -> Result<()> {
        let tempdir = setup_tar_xz()?;
        // Create a new dir where we uncompress to.
        let new_dir_path = tempdir.path().join("uncompressed");
        std::fs::create_dir(&new_dir_path)?;

        // Uncompress the file.
        let file_path = tempdir.path().join(TAR_NAME);
        let result = uncompress_tar_xz(&file_path, &new_dir_path)?;

        // Compare for equality.
        assert_eq!(result, vec![Path::new(FILE_NAME)]);

        let buf = std::fs::read(new_dir_path.join(&result[0]))?;

        assert_eq!(&buf, FILE_CONTENTS);
        Ok(())
    }

    #[test]
    fn test_execute_bad_commands() {
        // This fails even before executing the command because it doesn't exist.
        let result = execute_command(Command::new("/this/does/not/exist"));
        assert!(result.is_err());

        // This fails due to a bad status code of the command.
        let result = execute_command(Command::new("false"));
        assert!(result.is_err());

        // This succeeds.
        let result = execute_command(Command::new("ls"));
        assert!(result.is_ok());
    }
}
