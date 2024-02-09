// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_GENERATOR_BZIP_H_
#define UPDATE_ENGINE_PAYLOAD_GENERATOR_BZIP_H_

#include <brillo/secure_blob.h>

namespace chromeos_update_engine {

// Compresses the input buffer |in| into |out| with bzip2.
bool BzipCompress(const brillo::Blob& in, brillo::Blob* out);

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_GENERATOR_BZIP_H_
