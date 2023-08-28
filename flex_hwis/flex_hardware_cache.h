// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_FLEX_HARDWARE_CACHE_H_
#define FLEX_HWIS_FLEX_HARDWARE_CACHE_H_

#include "flex_hwis/hwis_data.pb.h"

#include <base/files/file_path.h>

namespace flex_hwis {

// Take the Device proto |data| and write to our on-disk hardware cache.
// |root| is usually "/", set to something else for testing.
// Returns false if any data couldn't be written, true otherwise.
bool WriteCacheToDisk(hwis_proto::Device& data, const base::FilePath& root);

}  // namespace flex_hwis

#endif  // FLEX_HWIS_FLEX_HARDWARE_CACHE_H_
