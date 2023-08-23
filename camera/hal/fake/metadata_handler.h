/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_FAKE_METADATA_HANDLER_H_
#define CAMERA_HAL_FAKE_METADATA_HANDLER_H_

#include <camera/camera_metadata.h>

#include "hal/fake/hal_spec.h"
#include "hardware/camera3.h"

namespace cros {

constexpr android_pixel_format_t kSupportedHalFormats[] = {
    HAL_PIXEL_FORMAT_BLOB,
    HAL_PIXEL_FORMAT_YCBCR_420_888,
    // The HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED for fake HAL returns exactly
    // the same content as HAL_PIXEL_FORMAT_YCBCR_420_888.
    HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
};

bool FillDefaultMetadata(android::CameraMetadata* static_metadata,
                         android::CameraMetadata* request_metadata,
                         const CameraSpec& spec);

bool FillResultMetadata(android::CameraMetadata* metadata, uint64_t timestamp);

// MetadataHandler is used for saving metadata states of CameraClient.
class MetadataHandler {
 public:
  MetadataHandler(const android::CameraMetadata& request_template,
                  const CameraSpec& spec);

  const camera_metadata_t* GetDefaultRequestSettings(int template_type);

 private:
  android::CameraMetadata CreateDefaultRequestSettings(int template_type);

  bool FillDefaultPreviewSettings(android::CameraMetadata* metadata);

  bool FillDefaultStillCaptureSettings(android::CameraMetadata* metadata);

  bool FillDefaultVideoRecordSettings(android::CameraMetadata* metadata);

  bool FillDefaultVideoSnapshotSettings(android::CameraMetadata* metadata);

  bool FillDefaultZeroShutterLagSettings(android::CameraMetadata* metadata);

  bool FillDefaultManualSettings(android::CameraMetadata* metadata);

  const android::CameraMetadata& request_template_;

  const CameraSpec& spec_;

  // Static array of standard camera settings templates.
  android::CameraMetadata template_settings_[CAMERA3_TEMPLATE_COUNT];
};

}  // namespace cros

#endif  // CAMERA_HAL_FAKE_METADATA_HANDLER_H_
