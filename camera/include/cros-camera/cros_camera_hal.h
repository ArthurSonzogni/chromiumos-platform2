/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_

#define CROS_CAMERA_HAL_INFO_SYM CCHI
#define CROS_CAMERA_HAL_INFO_SYM_AS_STR "CCHI"

#include <base/callback.h>
#include <hardware/camera_common.h>

#include "cros-camera/camera_mojo_channel_manager_token.h"

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

using PrivacySwitchStateChangeCallback =
    base::RepeatingCallback<void(PrivacySwitchState state)>;

typedef struct cros_camera_hal {
  /**
   * Sets up the camera HAL. The |token| can be used for communication through
   * Mojo.
   */
  void (*set_up)(CameraMojoChannelManagerToken* token);

  /**
   * Tears down the camera HAL.
   */
  void (*tear_down)();

  /**
   * Registers camera privacy switch observer.
   */
  void (*set_privacy_switch_callback)(
      PrivacySwitchStateChangeCallback callback);

  /**
   *  Open the camera device by client type.
   */
  int (*camera_device_open_ext)(const hw_module_t* module,
                                const char* name,
                                hw_device_t** device,
                                ClientType client_type);

  /**
   * Gets the camera info by client type.
   */
  int (*get_camera_info_ext)(int id,
                             struct camera_info* info,
                             ClientType client_type);

  /* reserved for future use */
  void* reserved[4];
} cros_camera_hal_t;

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_CROS_CAMERA_HAL_H_
