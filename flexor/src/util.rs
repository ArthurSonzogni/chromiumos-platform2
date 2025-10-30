// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Context, Result};
use log::info;
use std::{
    fs::File,
    io::{BufRead, BufReader, Read},
    path::{Path, PathBuf},
    process::{Command, Stdio},
    thread,
};
use tar::Archive;
use xz2::bufread::XzDecoder;

/// Run a command and log its output (both stdout and stderr) at the
/// info level. An error is returned if the process fails to launch,
/// or if it exits non-zero.
pub fn execute_command(command: Command) -> Result<()> {
    info!("Executing command: {command:?}");

    execute_command_impl(command, |msg| info!("{msg}"))
}

/// Implementation for `execute_command`.
///
/// When a log is produced, the message is passed to the `log` function.
/// This allows tests to check the logs produced by the command.
fn execute_command_impl<L>(mut command: Command, log: L) -> Result<()>
where
    L: Fn(String) + Clone + Send + 'static,
{
    // Spawn the child with its output piped so that it can be logged.
    let mut child = command
        // The `Command` API doesn't have a convenient way to create a
        // shared pipe for stdout/stderr, so create two pipes.
        .stderr(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .context("Failed to spawn command")?;
    // OK to unwrap: stderr and stdout are set to capture above.
    let mut stderr = child.stderr.take().unwrap();
    let mut stdout = child.stdout.take().unwrap();

    // Spawn two background threads, one to log stdout and one to log
    // stderr. The threads will terminate when the output pipe is
    // broken, which happen until when the child exits.
    let log_clone = log.clone();
    let stderr_thread = thread::spawn(move || log_lines_from_reader(&mut stderr, log_clone));
    let stdout_thread = thread::spawn(move || log_lines_from_reader(&mut stdout, log));
    stderr_thread.join().unwrap();
    stdout_thread.join().unwrap();

    // Wait for the child process to exit completely.
    let status = child.wait().context("Failed to wait on process")?;

    // Check the status to return an error if needed.
    if !status.success() {
        bail!("Process exited non-zero: {status:?}");
    }

    Ok(())
}

/// Read all lines from `reader` and log them with a ">>> " prefix.
///
/// This is used for logging output from a child process.
fn log_lines_from_reader<L>(reader: &mut dyn Read, log: L)
where
    L: Fn(String),
{
    let reader = BufReader::new(reader);
    reader
        .lines()
        .map_while(Result::ok)
        .for_each(|line| log(format!(">>> {line}")));
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
    use std::sync::{Arc, Mutex};

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

    /// Test that `execute_command_impl` logs the command's stdout and
    /// stderr.
    #[test]
    fn test_execute_log() {
        let output = Arc::new(Mutex::new(Vec::new()));
        let output_clone = output.clone();

        // Create a command that writes to both stdout and stderr.
        let mut cmd = Command::new("sh");
        cmd.arg("-c");
        cmd.arg("echo write-to-stdout && >&2 echo write-to-stderr");
        execute_command_impl(cmd, move |msg| output_clone.lock().unwrap().push(msg)).unwrap();

        // Sort the lines to avoid depending on a specific output order.
        // The stdout/stderr streams are not synchronized together.
        let mut output: Vec<String> = output.lock().unwrap().clone();
        output.sort();

        assert_eq!(output, [">>> write-to-stderr", ">>> write-to-stdout"])
    }
}
