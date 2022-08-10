/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros-camera/cros_camera_hal.h"

namespace cros {
static int camera_device_open(const hw_module_t* module,
                              const char* name,
                              hw_device_t** device) {
  return -ENODEV;
}

static int get_number_of_cameras() {
  return 0;
}

static int get_camera_info(int id, struct camera_info* info) {
  return -ENODEV;
}

static int set_callbacks(const camera_module_callbacks_t* callbacks) {
  return 0;
}

static void get_vendor_tag_ops(vendor_tag_ops_t* ops) {}

static int open_legacy(const struct hw_module_t* module,
                       const char* id,
                       uint32_t halVersion,
                       struct hw_device_t** device) {
  return -ENOSYS;
}

static int set_torch_mode(const char* camera_id, bool enabled) {
  return -ENOSYS;
}

static int init() {
  return 0;
}

static void set_up(CameraMojoChannelManagerToken* token) {}

static void tear_down() {}

static void set_privacy_switch_callback(
    PrivacySwitchStateChangeCallback callback) {}

static int camera_device_open_ext(const hw_module_t* module,
                                  const char* name,
                                  hw_device_t** device,
                                  ClientType client_type) {
  return -EINVAL;
}

static int get_camera_info_ext(int id,
                               struct camera_info* info,
                               ClientType client_type) {
  return -EINVAL;
}
}  // namespace cros

static hw_module_methods_t gCameraModuleMethods = {
    .open = cros::camera_device_open};

camera_module_t HAL_MODULE_INFO_SYM CROS_CAMERA_EXPORT = {
    .common = {.tag = HARDWARE_MODULE_TAG,
               .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
               .hal_api_version = HARDWARE_HAL_API_VERSION,
               .id = CAMERA_HARDWARE_MODULE_ID,
               .name = "Fake Camera HAL",
               .author = "The ChromiumOS Authors",
               .methods = &gCameraModuleMethods,
               .dso = nullptr,
               .reserved = {}},
    .get_number_of_cameras = cros::get_number_of_cameras,
    .get_camera_info = cros::get_camera_info,
    .set_callbacks = cros::set_callbacks,
    .get_vendor_tag_ops = cros::get_vendor_tag_ops,
    .open_legacy = cros::open_legacy,
    .set_torch_mode = cros::set_torch_mode,
    .init = cros::init,
    .reserved = {}};

cros::cros_camera_hal_t CROS_CAMERA_HAL_INFO_SYM CROS_CAMERA_EXPORT = {
    .set_up = cros::set_up,
    .tear_down = cros::tear_down,
    .set_privacy_switch_callback = cros::set_privacy_switch_callback,
    .camera_device_open_ext = cros::camera_device_open_ext,
    .get_camera_info_ext = cros::get_camera_info_ext};
