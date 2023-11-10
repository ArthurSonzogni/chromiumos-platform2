// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;

use anyhow::{anyhow, Context, Result};
use gpt_disk_io::{BlockIo, Disk, MutSliceBlockIo, StdBlockIo};
use gpt_disk_types::{BlockSize, GptPartitionEntry, GptPartitionName};

// Implementations of the [`BlockIo`] trait that we use for [`Gpt`].
// Currently this boils down to either a file (in case of the real
// disk) or a byte slice in case of testing.
// TODO(b/308389814): Replace this with gpt_disk_io::BlockIoAdapter once released.
enum BlockIoImpls<'a> {
    FileBacked(StdBlockIo<'a, File>),
    #[allow(dead_code)]
    SliceBacked(MutSliceBlockIo<'a>),
}

impl<'a> BlockIo for BlockIoImpls<'a> {
    type Error = anyhow::Error;

    fn block_size(&self) -> BlockSize {
        match self {
            BlockIoImpls::FileBacked(backing_type) => backing_type.block_size(),
            BlockIoImpls::SliceBacked(backing_type) => backing_type.block_size(),
        }
    }

    fn num_blocks(&mut self) -> std::result::Result<u64, Self::Error> {
        match self {
            BlockIoImpls::FileBacked(backing_type) => Ok(backing_type.num_blocks()?),
            BlockIoImpls::SliceBacked(backing_type) => Ok(backing_type.num_blocks()?),
        }
    }

    fn read_blocks(
        &mut self,
        start_lba: gpt_disk_types::Lba,
        dst: &mut [u8],
    ) -> std::result::Result<(), Self::Error> {
        match self {
            BlockIoImpls::FileBacked(backing_type) => Ok(backing_type.read_blocks(start_lba, dst)?),
            BlockIoImpls::SliceBacked(backing_type) => {
                Ok(backing_type.read_blocks(start_lba, dst)?)
            }
        }
    }

    fn write_blocks(
        &mut self,
        start_lba: gpt_disk_types::Lba,
        src: &[u8],
    ) -> std::result::Result<(), Self::Error> {
        match self {
            BlockIoImpls::FileBacked(backing_type) => {
                Ok(backing_type.write_blocks(start_lba, src)?)
            }
            BlockIoImpls::SliceBacked(backing_type) => {
                Ok(backing_type.write_blocks(start_lba, src)?)
            }
        }
    }

    fn flush(&mut self) -> std::result::Result<(), Self::Error> {
        match self {
            BlockIoImpls::FileBacked(backing_type) => Ok(backing_type.flush()?),
            BlockIoImpls::SliceBacked(backing_type) => Ok(backing_type.flush()?),
        }
    }
}

// Holds information about a GPT formatted disk.
pub struct Gpt<'a> {
    block_size: BlockSize,
    disk: Disk<BlockIoImpls<'a>>,
}

impl<'a> Gpt<'a> {
    // Creates a new [`Gpt`] from a file. The file should point to a disk with a
    // valid GPT header table on it, e.g. when opening "/dev/nvme0n1".
    pub fn from_file(file: &'a mut File, block_size: BlockSize) -> Result<Self> {
        let block_io = BlockIoImpls::FileBacked(StdBlockIo::new(file, block_size));
        let mut disk = Disk::new(block_io)?;

        Self::verify_disk(&mut disk, block_size)?;

        Ok(Self { block_size, disk })
    }

    // Used for testing, creates a [`Gpt`] from a byte slice that should contain
    // a valid GPT header table.
    #[allow(dead_code)]
    fn from_slice(slice: &'a mut [u8], block_size: BlockSize) -> Result<Self> {
        let block_io = BlockIoImpls::SliceBacked(MutSliceBlockIo::new(slice, block_size));
        let mut disk = Disk::new(block_io)?;

        Self::verify_disk(&mut disk, block_size)?;

        Ok(Self { block_size, disk })
    }

    fn verify_disk<'b>(disk: &'b mut Disk<BlockIoImpls<'a>>, block_size: BlockSize) -> Result<()> {
        let mut block_buf = vec![0u8; block_size.to_usize().context("Failed to read block size")?];
        let primary_header = disk.read_primary_gpt_header(&mut block_buf)?;

        if !primary_header.is_signature_valid() {
            return Err(anyhow!("Error: Invalid GPT header signature"));
        }

        Ok(())
    }

    // Reads the GPT partition table and returns information about the first
    // partition with `label` that is found.
    pub fn get_entry_for_partition_with_label(
        &mut self,
        label: GptPartitionName,
    ) -> Result<Option<GptPartitionEntry>> {
        let mut block_buf = vec![
            0;
            self.block_size
                .to_usize()
                .context("Failed to read block size")?
        ];
        let header_table = self.disk.read_primary_gpt_header(&mut block_buf)?;
        let layout = header_table.get_partition_entry_array_layout()?;

        let mut block_buf = vec![
            0;
            layout
                .num_bytes_rounded_to_block_as_usize(self.block_size)
                .unwrap()
        ];
        let entry_array = self
            .disk
            .read_gpt_partition_entry_array(layout, &mut block_buf)?;

        for index in 0..layout.num_entries {
            let entry = entry_array.get_partition_entry(index).unwrap();
            if entry.name == label {
                return Ok(Some(*entry));
            }
        }
        Ok(None)
    }
}

#[cfg(test)]
mod tests {
    use anyhow::Result;
    use gpt_disk_io::{Disk, MutSliceBlockIo};
    use gpt_disk_types::{
        BlockSize, GptHeader, GptPartitionEntry, GptPartitionEntryArray, LbaLe, U32Le,
    };

    use crate::gpt::Gpt;

    fn setup_disk_with_valid_header(
        disk_storage: &mut [u8],
        block_size: BlockSize,
    ) -> Result<()> {
        let block_io = MutSliceBlockIo::new(disk_storage, block_size);
        let mut disk = Disk::new(block_io)?;

        let primary_header = GptHeader {
            partition_entry_lba: LbaLe::from_u64(2),
            number_of_partition_entries: U32Le::from_u32(128),
            ..Default::default()
        };
        let partition_entry = GptPartitionEntry {
            name: "STATE".parse().unwrap(),
            ..Default::default()
        };
        let layout = primary_header.get_partition_entry_array_layout()?;
        let mut bytes = vec![
            0;
            layout
                .num_bytes_rounded_to_block_as_usize(block_size)
                .unwrap()
        ];
        let mut entry_array = GptPartitionEntryArray::new(layout, block_size, &mut bytes)?;
        *entry_array.get_partition_entry_mut(0).unwrap() = partition_entry;

        let mut block_buf = vec![0u8; block_size.to_usize().unwrap()];
        disk.write_protective_mbr(&mut block_buf)?;
        disk.write_primary_gpt_header(&primary_header, &mut block_buf)?;
        disk.write_gpt_partition_entry_array(&entry_array)?;

        Ok(())
    }

    #[test]
    fn test_can_construct() -> Result<()> {
        let mut disk = vec![0; 4 * 1024 * 1024];
        let block_size = BlockSize::BS_512;
        setup_disk_with_valid_header(&mut disk, block_size)?;

        let gpt = Gpt::from_slice(&mut disk, block_size);

        assert!(gpt.is_ok());
        Ok(())
    }

    #[test]
    fn test_get_entry_for_partition_with_label() -> Result<()> {
        let mut disk = vec![0; 4 * 1024 * 1024];
        let block_size = BlockSize::BS_512;
        setup_disk_with_valid_header(&mut disk, block_size)?;

        let mut gpt = Gpt::from_slice(&mut disk, block_size)?;
        let state_entry = gpt.get_entry_for_partition_with_label("STATE".parse().unwrap())?;
        assert!(state_entry.is_some());

        Ok(())
    }
}
