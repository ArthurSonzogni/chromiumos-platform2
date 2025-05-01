// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::capabilities::DecoderCapability;
use crate::EncodedFormat;

pub fn is_supported(codec: EncodedFormat) -> Result<Option<DecoderCapability>, String> {
    // TODO(bchoobineh): Implement device query capability logic for VAAPI devices.
    Err("TODO".to_string())
}
