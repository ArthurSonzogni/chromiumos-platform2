// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Debug, PartialEq)]
pub struct RmaSnBits {
    pub sn_data_version: [u8; 3],
    pub rma_status: u8,
    pub sn_bits: [u8; 12],
    pub standalone_rma_sn_bits: Option<[u8; 4]>,
}
