/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_FAKE_METADATA_HANDLER_H_
#define CAMERA_HAL_FAKE_METADATA_HANDLER_H_

#include <absl/status/status.h>
#include <camera/camera_metadata.h>

namespace cros {
absl::Status FillDefaultMetadata(android::CameraMetadata* static_metadata,
                                 android::CameraMetadata* request_metadata);

absl::Status FillResultMetadata(android::CameraMetadata* metadata);
}

#endif  // CAMERA_HAL_FAKE_METADATA_HANDLER_H_
