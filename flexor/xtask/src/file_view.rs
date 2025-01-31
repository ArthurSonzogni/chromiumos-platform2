// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fs_err::File;
use std::io::{self, ErrorKind, Read, Seek, SeekFrom, Write};
use std::num::TryFromIntError;
use std::ops::RangeInclusive;

/// File-like operations on a section of a file.
///
/// Only bytes within a particular range are visible to reads and
/// writes, and it's an error to seek before the start of the range.
///
/// This implements `Read`, `Write`, and `Seek`, so it can be used like
/// a regular file.
///
/// It's assumed that the underlying file is large enough to contain the
/// byte range; if not, all operations will fail.
#[derive(Debug)]
pub struct FileView<'a> {
    file: &'a mut File,
    range: RangeInclusive<u64>,
}

impl<'a> FileView<'a> {
    /// Create a new file view.
    ///
    /// The file will be seeked to the start of `range`.
    pub fn new(file: &'a mut File, range: RangeInclusive<u64>) -> io::Result<Self> {
        file.seek(SeekFrom::Start(*range.start()))?;
        Ok(FileView { file, range })
    }

    /// Get the number of bytes between the current file position and
    /// the end of the view.
    ///
    /// If the number of bytes is larger than `usize`, `usize::MAX` is
    /// returned.
    fn remaining_bytes(&mut self) -> io::Result<usize> {
        let position = self.file.stream_position()?;

        if let Some(remaining) = self
            .range
            .end()
            .checked_sub(position)
            .and_then(|v| v.checked_add(1))
        {
            Ok(usize::try_from(remaining).unwrap_or(usize::MAX))
        } else {
            // Past the end of view, nothing remaining.
            Ok(0)
        }
    }

    /// Convert from a `SeekFrom` within the view to an absolute offset
    /// from the start of the underlying file.
    fn seek_from_to_absolute(&mut self, seek_from: SeekFrom) -> io::Result<u64> {
        fn to_io_err(err: TryFromIntError) -> io::Error {
            io::Error::new(ErrorKind::Other, err)
        }

        match seek_from {
            SeekFrom::Start(offset_from_start) => Ok(self.range.start() + offset_from_start),
            SeekFrom::End(offset_from_end) => {
                let end = i64::try_from(*self.range.end()).map_err(to_io_err)?;
                u64::try_from(end + offset_from_end).map_err(to_io_err)
            }
            SeekFrom::Current(offset_from_current) => {
                let current = i64::try_from(self.file.stream_position()?).map_err(to_io_err)?;
                u64::try_from(current + offset_from_current).map_err(to_io_err)
            }
        }
    }
}

impl Read for FileView<'_> {
    fn read(&mut self, mut buf: &mut [u8]) -> io::Result<usize> {
        let remaining = self.remaining_bytes()?;

        // Shrink the output buffer if it extends past the end of the view.
        if buf.len() > remaining {
            buf = &mut buf[..remaining];
        }

        self.file.read(buf)
    }
}

impl Write for FileView<'_> {
    fn write(&mut self, mut buf: &[u8]) -> io::Result<usize> {
        let remaining = self.remaining_bytes()?;

        // Shrink the input buffer if it extends past the end of the view.
        if buf.len() > remaining {
            buf = &buf[..remaining];
        }

        self.file.write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.file.flush()
    }
}

impl Seek for FileView<'_> {
    fn seek(&mut self, position: SeekFrom) -> std::io::Result<u64> {
        // Get the absolute position in the file.
        let position = self.seek_from_to_absolute(position)?;

        // Fail if the requested seek goes before the start of the
        // range.
        //
        // Note that seeking past the end is allowed under file
        // semantics.
        if position < *self.range.start() {
            return Err(io::Error::new(
                ErrorKind::Other,
                "seek before start of file view",
            ));
        }

        // Seek in the underlying file to the new position.
        self.file.seek(SeekFrom::Start(position))?;

        Ok(position - self.range.start())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fs_err::OpenOptions;

    #[test]
    fn test_file_view() -> io::Result<()> {
        let tmp_dir = tempfile::tempdir()?;

        let path = tmp_dir.path().join("file");

        fs_err::write(&path, b"0123456789")?;

        let mut file = OpenOptions::new().read(true).write(true).open(&path)?;
        let mut view = FileView::new(&mut file, 2..=5)?;

        // Read the whole view.
        let mut buf = [0; 4];
        view.read_exact(&mut buf)?;
        assert_eq!(buf, *b"2345");

        // Now at the end of the range, nothing left to read.
        assert_eq!(view.read(&mut buf)?, 0);

        // Seek within the view.
        view.seek(SeekFrom::Start(1))?;

        // Read to the end.
        let mut buf = [0; 3];
        view.read_exact(&mut buf)?;
        assert_eq!(buf, *b"345");

        // Write within the view.
        let buf = b"abcd";
        view.seek(SeekFrom::Start(0))?;
        view.write_all(buf)?;
        view.flush()?;
        assert_eq!(fs_err::read(&path)?, b"01abcd6789");

        view.seek(SeekFrom::Start(0))?;

        // Invalid seeks.
        assert!(view.seek(SeekFrom::Current(-1)).is_err());
        assert!(view.seek(SeekFrom::End(-4)).is_err());

        Ok(())
    }
}
