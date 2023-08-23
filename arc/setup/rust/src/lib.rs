// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(unsafe_op_in_unsafe_fn)]

use anyhow::{anyhow, Result};
use quick_xml::events::{attributes::Attribute, BytesStart, Event};
use quick_xml::name::QName;
use quick_xml::Reader;
use quick_xml::Writer;
use std::ffi::{CStr, CString};
use std::io::Cursor;
use std::os::raw::c_char;

// Copies XML events into vec until reaching a </CamcorderProfiles> event. Will include the
// CamcorderProfiles closing tag in the vector.
fn consume_camera_profile(rdr: &mut Reader<&[u8]>, vec: &mut Vec<Event>) -> Result<()> {
    loop {
        let mut done = false;
        let ev = rdr.read_event()?;
        match ev {
            Event::Eof => {
                return Err(anyhow!("unexpected end of profile xml"));
            }
            Event::End(ref t) => {
                if QName(b"CamcorderProfiles") == t.name() {
                    done = true;
                }
            }
            _ => {}
        };
        vec.push(ev.into_owned());
        if done {
            return Ok(());
        }
    }
}

fn safe_filter_camera_config(
    orig_xml: &CStr,
    enable_front: bool,
    enable_back: bool,
) -> Result<CString> {
    let mut beforep0 = Vec::new();
    let mut afterp0 = Vec::new();
    let mut current = &mut beforep0;
    let mut p0 = Vec::new();
    let mut p1 = Vec::new();
    let mut found_media_settings = false;
    let xml_str = orig_xml.to_string_lossy();
    let mut xml_reader = Reader::from_str(xml_str.as_ref());

    loop {
        match xml_reader.read_event()? {
            Event::Eof => break,
            Event::Start(ref t) => {
                let mut pushorig = true;
                if QName(b"MediaSettings") == t.name() {
                    found_media_settings = true
                } else if QName(b"CamcorderProfiles") == t.name() {
                    let pno = match t.try_get_attribute("cameraId")? {
                        None => Err(anyhow!("cameraId attr missing")),
                        Some(ref v) => {
                            let vstr = String::from_utf8_lossy(v.value.as_ref());
                            if vstr == "0" {
                                Ok(&mut p0)
                            } else if vstr == "1" {
                                Ok(&mut p1)
                            } else {
                                Err(anyhow!("unknown cameraId in media profile content"))
                            }
                        }
                    }?;

                    if pno.len() != 0 {
                        return Err(anyhow!("duplicate cameraId"));
                    }

                    let mut newt = t.to_owned();
                    newt.clear_attributes();
                    for maybeattr in t.attributes() {
                        let attr = maybeattr?.to_owned();
                        if attr.key != QName(b"cameraId") {
                            newt.push_attribute(attr);
                        }
                    }
                    newt.push_attribute(Attribute::from(("cameraId", "0")));

                    pno.push(Event::Start(newt.into_owned()));
                    consume_camera_profile(&mut xml_reader, pno)?;
                    current = &mut afterp0;
                    pushorig = false;
                }
                if pushorig {
                    current.push(Event::Start(t.to_owned()));
                }
            }
            e => current.push(e),
        }
    }
    if !found_media_settings {
        return Err(anyhow!("could not find MediaSettings tag"));
    };
    if p0.is_empty() != p1.is_empty() {
        // The original content of media profile may already be filtered by test code[1]. Here we
        // ensure there's always at least one camera to be tested after applying all filtering.
        // TODO(b/187239915): Remove filter in test code and unify filter logic here.
        // [1]
        // https://source.corp.google.com/chromeos_public/src/third_party/labpack/files/server/cros/camerabox_utils.py;rcl=d30bb56fe7ae9c39b122a28f1d5d2b64f928555c;l=106
        return Ok(CString::new(orig_xml.to_bytes())?);
    }
    let mut wrt = Writer::new_with_indent(Cursor::new(Vec::new()), ' ' as u8, 4);
    for ev in beforep0 {
        wrt.write_event(ev)?
    }
    if enable_back {
        for ev in p0 {
            wrt.write_event(ev)?
        }
    }
    if enable_front {
        for ev in p1 {
            wrt.write_event(ev)?
        }
    }
    for ev in afterp0 {
        wrt.write_event(ev)?
    }
    Ok(CString::new(wrt.into_inner().into_inner())?)
}

// Transforms the camera config XML in |raw_xml| such that one of the cameras is excluded, and the
// other included, with cameraId set to 0 if it is not already. Returns either new XML or an error
// message, and sets is_error accordingly. |raw_xml| must be a null-terminated string.
#[no_mangle]
pub unsafe extern "C" fn filter_camera_config(
    orig_xml: *const c_char,
    enable_front: bool,
    enable_back: bool,
    is_error: *mut bool,
) -> *mut c_char {
    unwrap_result(
        safe_filter_camera_config(
            // Safe because orig_xml is a null-terminated C string.
            unsafe { CStr::from_ptr(orig_xml) },
            enable_front,
            enable_back,
        ),
        // Safe because is_error is owned by the caller and is non-null.
        unsafe { &mut *is_error },
    )
}

fn safe_append_feature_management(orig_xml: &CStr, features: &Vec<&CStr>) -> Result<CString> {
    let mut wrt = Writer::new_with_indent(Cursor::new(Vec::new()), ' ' as u8, 2);
    let xml_str = orig_xml.to_string_lossy();
    let mut xml_reader = Reader::from_str(xml_str.as_ref());
    xml_reader.trim_text(true);
    loop {
        match xml_reader.read_event()? {
            Event::Eof => break,
            Event::End(ref t) => {
                if QName(b"permissions") == t.name() {
                    for feat in features {
                        let mut featel = BytesStart::new("feature");
                        let mut nameat = "org.chromium.arc.feature_management.".to_owned();
                        nameat.push_str(feat.to_string_lossy().as_ref());
                        featel.push_attribute(Attribute::from(("name", nameat.as_ref())));
                        wrt.write_event(Event::Empty(featel))?;
                    }
                }
                wrt.write_event(Event::End(t.to_owned()))?;
            }
            ev => wrt.write_event(ev.to_owned())?,
        }
    }
    Ok(CString::new(wrt.into_inner().into_inner())?)
}

fn unwrap_result(r: Result<CString>, is_error: &mut bool) -> *mut c_char {
    match r {
        Ok(c) => {
            *is_error = false;
            c
        }
        Err(e) => {
            *is_error = true;
            CString::new(e.to_string()).expect("cannot convert error to string")
        }
    }
    .into_raw()
}

// Adds one or more <feature> tags, each with a name attr matching what is in the features array.
// |raw_xml| must be a null-terminated string. |features| must be an array of null-terminated
// strings that ends with a null pointer. Returns either new XML or an error message, and sets
// |is_error| accordingly, which must be non-null.
#[no_mangle]
pub unsafe extern "C" fn append_feature_management(
    orig_xml: *const c_char,
    features: *const *const c_char,
    is_error: *mut bool,
) -> *mut c_char {
    let mut featvec = Vec::new();
    let mut f = features;

    // Convert C array of strings terminated with a null ptr to a Vec<String>.
    unsafe {
        while *f != std::ptr::null() {
            featvec.push(CStr::from_ptr(*f));
            f = f.offset(1);
        }
    }

    unwrap_result(
        safe_append_feature_management(
            // Safe because orig_xml is a null-terminated C string.
            unsafe { CStr::from_ptr(orig_xml) },
            &featvec,
        ),
        // Safe because is_error is owned by the caller and is non-null.
        unsafe { &mut *is_error },
    )
}

// This is only safe if |cstr| is a string returned by append_feature_management or
// filter_camera_config.
#[no_mangle]
pub unsafe extern "C" fn free_rs_string(cstr: *mut c_char) {
    // Safe because cstr was originally returned by CString::into_raw.
    unsafe {
        let _ = CString::from_raw(cstr);
    }
}
