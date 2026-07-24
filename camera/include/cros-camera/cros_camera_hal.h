/*
 * Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_

#define CROS_CAMERA_HAL_INFO_SYM CCHI
#define CROS_CAMERA_HAL_INFO_SYM_AS_STR "CCHI"

#include <vector>

#if !defined(CROS_CAMERA_DISABLE_HAL_CALLBACKS)
#include <base/functional/callback.h>
#endif

#include <hardware/camera3.h>
#include <hardware/camera_common.h>

#include "cros-camera/camera_mojo_channel_manager_token.h"

#if USE_CAMERA_FEATURE_FACE_DETECTION || defined(FACE_DETECTION)
#include "cros-camera/camera_face_detection.h"
#endif

namespace cros {

enum class PrivacySwitchState {
  kUnknown,
  kOn,
  kOff,
};

// Synced with CameraClientType in cros_camera_service.mojom.
enum class ClientType {
  kUnknown = 0,
  kTesting = 1,
  kChrome = 2,
  kAndroid = 3,
  kPluginVm = 4,
  kAshChrome = 5,
  kLacrosChrome = 6
};

struct FaceDetectionResult {
  // The frame number that the face detection was run on.
  uint32_t frame_number;

#if USE_CAMERA_FEATURE_FACE_DETECTION || defined(FACE_DETECTION)
  // The detected face ROIs.
  std::vector<human_sensing::CrosFace> faces;
#endif
};

// =========================================================================
// HAL Callback Types & Libchrome Dependency
// =========================================================================
// The HAL callback types (FaceDetectionResultCallback and
// PrivacySwitchStateChangeCallback) rely on base::RepeatingCallback from
// libchrome (<base/functional/callback.h>).
//
// Because libchrome headers require C++23, standalone/external modules like
// libcamera (which do not need these callbacks) define
// CROS_CAMERA_DISABLE_HAL_CALLBACKS. This allows libcamera to use fallback
// opaque pointer types to preserve the memory layout of cros_camera_hal_t
// without pulling in libchrome headers.
// =========================================================================
#if !defined(CROS_CAMERA_DISABLE_HAL_CALLBACKS)
using FaceDetectionResultCallback =
    base::RepeatingCallback<FaceDetectionResult()>;

using PrivacySwitchStateChangeCallback =
    base::RepeatingCallback<void(int camera_id, PrivacySwitchState state)>;
#else
// Fallback for libcamera and standalone modules to break the libchrome
// dependency. This preserves the exact memory layout of cros_camera_hal_t
// because the size of a function pointer is constant regardless of its arguments.
struct OpaqueCallback;
using FaceDetectionResultCallback = OpaqueCallback*;
using PrivacySwitchStateChangeCallback = OpaqueCallback*;
#endif

typedef struct cros_camera_hal {
  /**
   * Sets up the camera HAL. The |token| can be used for communication through
   * Mojo.
   */
  void (*set_up)(CameraMojoChannelManagerToken* token) = nullptr;

  /**
   * Tears down the camera HAL.
   */
  void (*tear_down)() = nullptr;

  /**
   * Registers camera privacy switch observer.
   */
  void (*set_privacy_switch_callback)(
      PrivacySwitchStateChangeCallback callback) = nullptr;

  /**
   *  Open the camera device by client type.
   */
  int (*camera_device_open_ext)(const hw_module_t* module,
                                const char* name,
                                hw_device_t** device,
                                ClientType client_type) = nullptr;

  /**
   * Gets the camera info by client type.
   */
  int (*get_camera_info_ext)(int id,
                             struct camera_info* info,
                             ClientType client_type) = nullptr;

  /**
   * Registers facessd detect callback.
   */
  void (*set_face_detection_result_callback)(
      int camera_id, FaceDetectionResultCallback callback) = nullptr;

  /**
   * Sets the software privacy switch state.
   */
  void (*set_privacy_switch_state)(bool on) = nullptr;

  /**
   * Reserved for future use. Initialize it so users of named field initializers
   * don't get warnings from Clang: b/305723283.
   */
  void* reserved[4] = {};
} cros_camera_hal_t;

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_
