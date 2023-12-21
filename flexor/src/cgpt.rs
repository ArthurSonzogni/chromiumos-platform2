// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    ffi::{CStr, CString},
    marker::PhantomData,
    os::unix::prelude::OsStrExt,
    path::Path,
    ptr::null,
};

use anyhow::{bail, Context, Result};
use gpt_disk_types::{guid, LbaRangeInclusive};
use uguid::Guid;
use libchromeos::rand;
use vboot_reference_sys::vboot_host;

/// Partition type for the thirteenth partition we are adding. We are marking
/// the partition as CHROMEOS_FUTURE_USE type.
const CHROMEOS_FUTURE_USE_PART_TYPE: Guid = guid!("2e0a753d-9e48-43b0-8337-b15192cb1b5e");

/// Adds a new GPT partition with the given arguments. The new partition has the
/// type GUID `CHROMEOS_FUTURE_USE_PART_TYPE`.
pub fn add_cgpt_partition(
    index: u32,
    disk_path: &Path,
    label: &str,
    range: LbaRangeInclusive,
) -> Result<()> {
    let begin = range.start().to_u64();
    let size = range.num_blocks();
    let disk_path = CString::new(disk_path.as_os_str().as_bytes())
        .context("unable to convert drive path to CString")?;
    let label = CString::new(label).context("unable to convert label to CString")?;

    let mut params = get_cgpt_add_params(
        index,
        disk_path.as_c_str(),
        Some(label.as_c_str()),
        Some(begin),
        Some(size),
        Some(CHROMEOS_FUTURE_USE_PART_TYPE),
    );

    params.cgpt_add()
}

/// Removes a partition at `index`.
pub fn remove_cgpt_partition(index: u32, disk_path: &Path) -> Result<()> {
    let disk_path = CString::new(disk_path.as_os_str().as_bytes())
        .context("unable to convert drive path to CString")?;

    let mut params = get_cgpt_add_params(
        index,
        disk_path.as_c_str(),
        /*label=*/ None,
        /*begin=*/ None,
        /*size=*/ None,
        Some(Guid::ZERO),
    );

    params.cgpt_add()
}

/// Resizes an existing partition at `index`.
pub fn resize_cgpt_partition(
    index: u32,
    disk_path: &Path,
    label: &str,
    new_range: LbaRangeInclusive,
) -> Result<()> {
    let begin = new_range.start().to_u64();
    let size = new_range.num_blocks();
    let disk_path = CString::new(disk_path.as_os_str().as_bytes())
        .context("unable to convert drive path to CString")?;
    let label = CString::new(label).context("unable to convert label to CString")?;

    let mut params = get_cgpt_add_params(
        index,
        disk_path.as_c_str(),
        Some(label.as_c_str()),
        Some(begin),
        Some(size),
        /*type_guid=*/ None,
    );

    params.cgpt_add()
}

/// Wrapper that handles lifetimes of the members of
/// [`vboot_host::CgptAddParams`], which holds pointers to [`CString`].
struct CgptAddParamsWrapper<'a> {
    params: vboot_host::CgptAddParams,
    phantom_data: PhantomData<&'a ()>,
}

impl CgptAddParamsWrapper<'_> {
    pub fn cgpt_add(&mut self) -> Result<()> {
        // SAFETY: The params we pass here are valid for the duration of `CgptAdd`, as
        // made sure by the lifetime of `self`.
        let err = unsafe { vboot_host::CgptAdd(&mut self.params) };
        if err != 0 {
            bail!("CgptAdd returned error status code {err}");
        }
        Ok(())
    }
}

/// Returns valid [`vboot_host::CgptAddParams`] for the given arguments.
/// [`vboot_host::CgptAdd`] (which takes those arguments), is used for
/// both adding partitions as well as modifying partitions. The caller
/// controls the behaviour with supplying the correct arguments here.
fn get_cgpt_add_params<'args, 'params>(
    index: u32,
    disk_path: &'args CStr,
    label: Option<&'args CStr>,
    begin: Option<u64>,
    size: Option<u64>,
    type_guid: Option<Guid>,
) -> CgptAddParamsWrapper<'params>
where
    'args: 'params,
{
    let set_type_guid: i32 = if type_guid.is_some() { 1 } else { 0 };
    let set_begin: i32 = if begin.is_some() { 1 } else { 0 };
    let set_size: i32 = if size.is_some() { 1 } else { 0 };

    let type_guid_c = type_guid.map_or(empty_guid(), to_cgpt_guid);
    let label_ptr = label.map_or(null(), |label_str| label_str.as_ptr());

    CgptAddParamsWrapper {
        params: vboot_host::CgptAddParams {
            // Use the default value.
            drive_size: 0,
            unique_guid: empty_guid(),
            error_counter: 0,
            successful: 0,
            tries: 0,
            priority: 0,
            required: 0,
            legacy_boot: 0,
            raw_value: 0,

            // Passed by the caller.
            begin: begin.unwrap_or(0),
            partition: index,
            drive_name: disk_path.as_ptr(),
            size: size.unwrap_or(0),
            type_guid: type_guid_c,
            label: label_ptr,

            // For each of these fields, a value of 1 means to update
            // the value in the partition, and a value of 0 means don't
            // update it.
            set_begin,
            set_size,
            set_type: set_type_guid,
            set_unique: 0,
            set_error_counter: 0,
            set_successful: 0,
            set_tries: 0,
            set_priority: 0,
            set_required: 0,
            set_legacy_boot: 0,
            set_raw: 0,
        },
        phantom_data: PhantomData,
    }
}

/// Creates a [`vboot_host::Guid`] from a [`gpt_disk_types::Guid`]. This
/// conversion relies on the fact that it is only possible to obtain valid guids
/// using [`gpt_disk_types::Guid`], so it just places the raw bytes of the guid.
fn to_cgpt_guid(guid: Guid) -> vboot_host::Guid {
    vboot_host::Guid {
        u: vboot_host::Guid__bindgen_ty_1 {
            raw: guid.to_bytes(),
        },
    }
}

/// Returns an empty [`vboot_host::Guid`].
fn empty_guid() -> vboot_host::Guid {
    vboot_host::Guid {
        u: vboot_host::Guid__bindgen_ty_1 { raw: [0; 16] },
    }
}

/// This needs to be defined here since CgptAdd needs this present. In the cgpt
/// binary this is replaced by a call to libuuid, but we don't link against that
/// and need to provide this on our own.
#[no_mangle]
pub unsafe extern "C" fn GenerateGuid(newguid: *mut std::ffi::c_void) -> std::ffi::c_int {
    let mut buffer = [0u8; 16];
    // Fall back to the hardcoded UUID in case we can't generate one.
    let uuid = match rand::rand_bytes(&mut buffer, rand::Source::Pseudorandom) {
        Ok(_) => Guid::from_random_bytes(buffer).to_bytes(),
        Err(_) => guid!("772831ce-b9c4-4d9e-891e-56df227885b2").to_bytes(),
    };

    let newguid: *mut vboot_host::Guid = newguid.cast();
    (*newguid).u = vboot_host::Guid__bindgen_ty_1 {
        // Use a hardcoded GUID for now.
        raw: uuid,
    };

    0
}

#[cfg(test)]
mod tests {
    use crate::gpt::Gpt;

    use super::*;
    use std::{fs::File, process::Command};

    use anyhow::Result;
    use gpt_disk_types::{BlockSize, Lba};

    const TEST_IMAGE_PATH: &str = "fake_disk.bin";
    const NUM_SECTORS: u32 = 1000;

    const DATA_START: u64 = 100;
    const DATA_SIZE: u64 = 20;
    const DATA_LABEL: &str = "data stuff";
    const DATA_GUID: &str = "0fc63daf-8483-4772-8e79-3d69d8477de4";
    const DATA_NUM: u32 = 1;

    fn setup_test_disk_environment() -> Result<tempfile::TempDir> {
        // Create a tempdir to mount a tempfs to.
        let tmp_dir = tempfile::tempdir()?;
        let path = tmp_dir.path().join(TEST_IMAGE_PATH);
        let path = path.as_path();

        // Create a fake disk at path.
        let status = Command::new("dd")
            .arg("if=/dev/zero")
            .arg(&format!("of={}", path.display()))
            .arg("conv=fsync")
            .arg("bs=512")
            .arg(&format!("count={NUM_SECTORS}"))
            .status()?;
        assert!(status.success());

        // Create a GPT header table.
        let status = Command::new("cgpt").arg("create").arg(path).status()?;
        assert!(status.success());

        // Add the initial data partition.
        let status = Command::new("cgpt")
            .arg("add")
            .args(["-b", &format!("{DATA_START}")])
            .args(["-s", &format!("{DATA_SIZE}")])
            .args(["-t", DATA_GUID])
            .args(["-l", DATA_LABEL])
            .arg(path)
            .status()?;
        assert!(status.success());

        Ok(tmp_dir)
    }

    #[test]
    fn test_cgpt_resize_and_add() -> Result<()> {
        // Setup.
        let temp_dir = setup_test_disk_environment()?;
        let path = temp_dir.path().join(TEST_IMAGE_PATH);
        let path = path.as_path();
        assert!(matches!(path.try_exists(), Ok(true)));

        // Test `resize_partition_gpt_partition`.
        let data_part_end = DATA_START + 2;
        let new_range = LbaRangeInclusive::new(Lba(DATA_START), Lba(data_part_end)).unwrap();
        resize_cgpt_partition(DATA_NUM, path, DATA_LABEL, new_range)?;

        let file = File::open(path)?;
        let mut gpt = Gpt::from_file(file, BlockSize::BS_512)?;

        let partition = gpt.get_entry_for_partition_with_label(DATA_LABEL.parse()?)?;
        assert!(partition.is_some());

        assert_eq!(partition.unwrap().ending_lba.to_u64(), data_part_end);

        // Test `add_cgpt_partition`.
        let new_part_end = DATA_START + 4;
        let range = LbaRangeInclusive::new(Lba(data_part_end + 1), Lba(new_part_end)).unwrap();
        let new_part_label = "TEST".to_string();
        add_cgpt_partition(DATA_NUM + 1, path, &new_part_label, range)?;

        let file = File::open(path)?;
        let mut gpt = Gpt::from_file(file, BlockSize::BS_512)?;

        let partition = gpt.get_entry_for_partition_with_label(new_part_label.parse()?)?;
        assert!(partition.is_some());

        assert_eq!(partition.unwrap().starting_lba.to_u64(), data_part_end + 1);

        assert_eq!(partition.unwrap().ending_lba.to_u64(), new_part_end);

        // Test `remove_cgpt_partition`.
        remove_cgpt_partition(DATA_NUM + 1, path)?;

        let file = File::open(path)?;
        let mut gpt = Gpt::from_file(file, BlockSize::BS_512)?;

        let partition = gpt.get_entry_for_partition_with_label(new_part_label.parse()?)?;
        assert!(partition.is_none());

        Ok(())
    }
}
