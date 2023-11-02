// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    ffi::{CStr, CString},
    marker::PhantomData,
    os::unix::prelude::OsStrExt,
    path::Path,
};

use anyhow::{bail, Context, Result};
use gpt_disk_types::{guid, Guid, LbaRangeInclusive};
use vboot_reference_sys::vboot_host;

// Partition type for the thirteenth partition we are adding. We are marking
// the partition as CHROMEOS_FUTURE_USE type.
const CHROMEOS_FUTURE_USE_PART_TYPE: Guid = guid!("2e0a753d-9e48-43b0-8337-b15192cb1b5e");

// Adds a new GPT partition with the given arguments. The new partition has the
// type GUID `CHROMEOS_FUTURE_USE_PART_TYPE`.
pub fn add_cgpt_partition(
    index: u32,
    dst: &Path,
    label: &str,
    range: LbaRangeInclusive,
) -> Result<()> {
    let begin = range.start().to_u64();
    // Add one, because the range type promises "inclusive".
    let size = range.end().to_u64() - range.start().to_u64() + 1;
    let drive_name = CString::new(dst.as_os_str().as_bytes())
        .context("unable to convert drive path to CString")?;
    let label = CString::new(label).context("unable to convert label to CString")?;

    let mut params = get_cgpt_add_params(
        index,
        drive_name.as_c_str(),
        label.as_c_str(),
        begin,
        size,
        Some(CHROMEOS_FUTURE_USE_PART_TYPE),
    );

    params.cgpt_add()
}

// Resizes an existing partition at `index`.
pub fn resize_cgpt_partition(
    index: u32,
    dst: &Path,
    label: &str,
    new_range: LbaRangeInclusive,
) -> Result<()> {
    let begin = new_range.start().to_u64();
    // Add one, because the range type promises "inclusive".
    let size = new_range.end().to_u64() - new_range.start().to_u64() + 1;
    let drive_name = CString::new(dst.as_os_str().as_bytes())
        .context("unable to convert drive path to CString")?;
    let label = CString::new(label).context("unable to convert label to CString")?;

    let mut params = get_cgpt_add_params(
        index,
        drive_name.as_c_str(),
        label.as_c_str(),
        begin,
        size,
        /*type_guid=*/ None,
    );

    params.cgpt_add()
}

// Wrapper that handles lifetimes of the members of
// [`vboot_host::CgptAddParams`], which holds pointers to [`CString`].
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

// Returns valid [`vboot_host::CgptAddParams`] for the given arguments.
// [`vboot_host::CgptAdd`] (which takes those arguments), is used for
// both adding partitions as well as modifying partitions. The caller
// controls the behaviour with supplying the correct arguments here.
fn get_cgpt_add_params<'args, 'params>(
    index: u32,
    dst: &'args CStr,
    label: &'args CStr,
    begin: u64,
    size: u64,
    type_guid: Option<Guid>,
) -> CgptAddParamsWrapper<'params>
where
    'args: 'params,
{
    let set_type_guid: i32 = if type_guid.is_some() { 1 } else { 0 };
    let type_guid_c = type_guid.map_or(empty_guid(), to_cgpt_guid);

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
            begin,
            partition: index,
            drive_name: dst.as_ptr(),
            size,
            type_guid: type_guid_c,
            label: label.as_ptr(),

            // For each of these fields, a value of 1 means to update
            // the value in the partition, and a value of 0 means don't
            // update it.
            set_begin: 1,
            set_size: 1,
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

// Creates a [`vboot_host::Guid`] from a [`gpt_disk_types::Guid`]. This
// conversion relies on the fact that it is only possible to obtain valid guids
// using [`gpt_disk_types::Guid`], so it just places the raw bytes of the guid.
fn to_cgpt_guid(guid: Guid) -> vboot_host::Guid {
    vboot_host::Guid {
        u: vboot_host::Guid__bindgen_ty_1 {
            raw: guid.to_bytes(),
        },
    }
}

// Returns an empty [`vboot_host::Guid`].
fn empty_guid() -> vboot_host::Guid {
    vboot_host::Guid {
        u: vboot_host::Guid__bindgen_ty_1 { raw: [0; 16] },
    }
}

// This needs to be defined here since CgptAdd needs this present. In the cgpt
// binary this is replaced by a call to libuuid, but we don't link against that
// and need to provide this on our own.
#[no_mangle]
pub unsafe extern "C" fn GenerateGuid(newguid: *mut std::ffi::c_void) -> std::ffi::c_int {
    let newguid: *mut vboot_host::Guid = newguid.cast();
    (*newguid).u = vboot_host::Guid__bindgen_ty_1 {
        // Use a hardcoded GUID for now.
        raw: guid!("772831ce-b9c4-4d9e-891e-56df227885b2").to_bytes(),
    };

    0
}
