// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Ties together the various modules that make up the Sirenia library used by
//! both Trichechus and Dugong.

pub mod app_info;
pub mod secrets;

include!("bindings/include_modules.rs");
