/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/fake/camera_hal.h"

#include <memory>

#include <base/no_destructor.h>
#include <base/strings/string_number_conversions.h>
#include <base/threading/sequenced_task_runner_handle.h>

#include "cros-camera/common.h"
#include "cros-camera/cros_camera_hal.h"

namespace cros {

namespace {
// The default fake hal spec file. The file should contain a JSON that is
// parsed to HalSpec struct.
static constexpr const char kDefaultFakeHalSpecFile[] =
    "/etc/camera/fake_hal.json";
static constexpr const char kOverrideFakeHalSpecFile[] =
    "/run/camera/fake_hal.json";
}  // namespace

CameraHal::CameraHal() {
  // The constructor is first called by set_up which is not on the same
  // sequence as the other methods this class is run on.
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

CameraHal::~CameraHal() = default;

CameraHal& CameraHal::GetInstance() {
  // Leak the static camera HAL here, since it has a non-trivial destructor
  // (from ReloadableConfigFile -> base::FilePathWatcher).
  static base::NoDestructor<CameraHal> camera_hal;
  return *camera_hal;
}

int CameraHal::GetNumberOfCameras() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return 0;
}

int CameraHal::SetCallbacks(const camera_module_callbacks_t* callbacks) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return 0;
}

int CameraHal::Init() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  config_file_ = std::make_unique<ReloadableConfigFile>(
      ReloadableConfigFile::Options{base::FilePath(kDefaultFakeHalSpecFile),
                                    base::FilePath(kOverrideFakeHalSpecFile)});
  config_file_->SetCallback(
      base::BindRepeating(&CameraHal::OnSpecUpdated, base::Unretained(this)));

  return 0;
}

void CameraHal::SetUp(CameraMojoChannelManagerToken* token) {}

void CameraHal::TearDown() {}

void CameraHal::SetPrivacySwitchCallback(
    PrivacySwitchStateChangeCallback callback) {}

int CameraHal::OpenDevice(int id,
                          const hw_module_t* module,
                          hw_device_t** hw_device,
                          ClientType client_type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return -EINVAL;
}

int CameraHal::GetCameraInfo(int id,
                             struct camera_info* info,
                             ClientType client_type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return -EINVAL;
}

void CameraHal::OnSpecUpdated(const base::Value& json_values) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto hal_spec = ParseHalSpecFromJsonValue(json_values);
  if (!hal_spec.has_value()) {
    LOGF(WARNING) << "config file is not formatted correctly, ignored.";
    return;
  }

  // TODO(pihsun): Applies the config diff.
  hal_spec_ = hal_spec.value();

  for (const auto& c : hal_spec_.cameras) {
    LOGF(INFO) << "id = " << c.id << ", connected = " << c.connected;
  }
}

static int camera_device_open_ext(const hw_module_t* module,
                                  const char* name,
                                  hw_device_t** device,
                                  ClientType client_type) {
  // Make sure hal adapter loads the correct symbol.
  if (module != &HAL_MODULE_INFO_SYM.common) {
    LOGF(ERROR) << "Invalid module " << module << " expected "
                << &HAL_MODULE_INFO_SYM.common;
    return -EINVAL;
  }

  int id;
  if (!base::StringToInt(name, &id)) {
    LOGF(ERROR) << "Invalid camera name " << name;
    return -EINVAL;
  }
  return CameraHal::GetInstance().OpenDevice(id, module, device, client_type);
}

static int get_camera_info_ext(int id,
                               struct camera_info* info,
                               ClientType client_type) {
  return CameraHal::GetInstance().GetCameraInfo(id, info, client_type);
}

static int camera_device_open(const hw_module_t* module,
                              const char* name,
                              hw_device_t** device) {
  return camera_device_open_ext(module, name, device, ClientType::kChrome);
}

static int get_number_of_cameras() {
  return CameraHal::GetInstance().GetNumberOfCameras();
}

static int get_camera_info(int id, struct camera_info* info) {
  return get_camera_info_ext(id, info, ClientType::kChrome);
}

static int set_callbacks(const camera_module_callbacks_t* callbacks) {
  return CameraHal::GetInstance().SetCallbacks(callbacks);
}

static int init() {
  return CameraHal::GetInstance().Init();
}

static void set_up(CameraMojoChannelManagerToken* token) {
  CameraHal::GetInstance().SetUp(token);
}

static void tear_down() {
  CameraHal::GetInstance().TearDown();
}

static void set_privacy_switch_callback(
    PrivacySwitchStateChangeCallback callback) {
  CameraHal::GetInstance().SetPrivacySwitchCallback(callback);
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
