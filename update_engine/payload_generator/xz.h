// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_GENERATOR_XZ_H_
#define UPDATE_ENGINE_PAYLOAD_GENERATOR_XZ_H_

#include <brillo/secure_blob.h>

namespace chromeos_update_engine {

// Initialize the xz compression unit. Call once before any call to
// XzCompress().
void XzCompressInit();

// Compresses the input buffer |in| into |out| with xz. The compressed stream
// will be the equivalent of running xz -9 --check=none
bool XzCompress(const brillo::Blob& in, brillo::Blob* out);

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_GENERATOR_XZ_H_
