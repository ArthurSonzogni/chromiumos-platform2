/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <base/strings/string_number_conversions.h>
#include <brillo/dbus/dbus_connection.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <utility>

#include "cros-camera/common.h"
#include "cros-camera/export.h"
#include "dbus_proxies/dbus-proxies.h"
#include "hal/ip/camera_hal.h"
#include "hal/ip/metadata_handler.h"

namespace cros {

CameraHal::CameraHal()
    : binding_(this),
      next_camera_id_(0),
      callbacks_set_(base::WaitableEvent::ResetPolicy::MANUAL,
                     base::WaitableEvent::InitialState::NOT_SIGNALED),
      callbacks_(nullptr) {
  mojo_channel_ = CameraMojoChannelManager::CreateInstance();
}

CameraHal::~CameraHal() {
  auto return_val = Future<void>::Create(nullptr);
  mojo::edk::GetIOTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(&CameraHal::DestroyOnIpcThread,
                                base::Unretained(this), return_val));
  return_val->Wait();
}

CameraHal& CameraHal::GetInstance() {
  static CameraHal camera_hal;
  return camera_hal;
}

int CameraHal::OpenDevice(int id,
                          const hw_module_t* module,
                          hw_device_t** hw_device) {
  base::AutoLock l(camera_map_lock_);
  if (cameras_.find(id) == cameras_.end()) {
    LOGF(ERROR) << "Camera " << id << " is invalid";
    return -EINVAL;
  }

  if (cameras_[id]->IsOpen()) {
    LOGF(ERROR) << "Camera " << id << " is already open";
    return -EBUSY;
  }

  cameras_[id]->Open(module, hw_device);

  return 0;
}

int CameraHal::GetNumberOfCameras() const {
  // Should always return 0, only built-in cameras are counted here
  return 0;
}

int CameraHal::GetCameraInfo(int id, struct camera_info* info) {
  base::AutoLock l(camera_map_lock_);
  auto it = cameras_.find(id);
  if (it == cameras_.end()) {
    LOGF(ERROR) << "Camera id " << id << " is not valid";
    return -EINVAL;
  }

  info->facing = CAMERA_FACING_EXTERNAL;
  info->orientation = 0;
  info->device_version = CAMERA_DEVICE_API_VERSION_3_3;
  info->static_camera_characteristics =
      it->second->GetStaticMetadata()->getAndLock();
  info->resource_cost = 0;
  info->conflicting_devices = nullptr;
  info->conflicting_devices_length = 0;
  return 0;
}

int CameraHal::SetCallbacks(const camera_module_callbacks_t* callbacks) {
  callbacks_ = callbacks;
  callbacks_set_.Signal();
  return 0;
}

int CameraHal::Init() {
  if (initialized_.IsSet()) {
    LOGF(ERROR) << "Init called more than once";
    return -EBUSY;
  }

  auto return_val = Future<int>::Create(nullptr);
  mojo::core::GetIOTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(&CameraHal::InitOnIpcThread,
                                base::Unretained(this), return_val));
  int ret = return_val->Get();
  initialized_.Set();
  return ret;
}

void CameraHal::InitOnIpcThread(scoped_refptr<Future<int>> return_val) {
  brillo::DBusConnection dbus_connection;
  org::chromium::IpPeripheralServiceProxy proxy(
      dbus_connection.Connect(), "org.chromium.IpPeripheralService");

  mojo::PlatformChannel channel;
  brillo::dbus_utils::FileDescriptor handle(
      channel.TakeRemoteEndpoint().TakePlatformHandle().TakeFD().release());

  if (!proxy.BootstrapMojoConnection(handle, nullptr)) {
    LOGF(ERROR) << "Failed to send handle over DBus";
    return_val->Set(-ENODEV);
    return;
  }

  isolated_connection_ = std::make_unique<mojo::IsolatedConnection>();
  mojo::ScopedMessagePipeHandle pipe =
      isolated_connection_->Connect(channel.TakeLocalEndpoint());

  detector_.Bind(mojom::IpCameraDetectorPtrInfo(std::move(pipe), 0u));
  detector_.set_connection_error_handler(
      base::Bind(&CameraHal::OnConnectionError, base::Unretained(this)));

  mojom::IpCameraConnectionListenerPtr listener;
  binding_.Bind(mojo::MakeRequest(&listener));
  binding_.set_connection_error_handler(
      base::Bind(&CameraHal::OnConnectionError, base::Unretained(this)));

  detector_->RegisterConnectionListener(std::move(listener));
  return_val->Set(0);
}

void CameraHal::DestroyOnIpcThread(scoped_refptr<Future<void>> return_val) {
  binding_.Close();
  detector_.reset();

  {
    base::AutoLock l(camera_map_lock_);
    cameras_.clear();
  }

  isolated_connection_ = nullptr;
  mojo_channel_.reset();
  return_val->Set();
}

void CameraHal::OnConnectionError() {
  binding_.Close();
  detector_.reset();

  {
    base::AutoLock l(camera_map_lock_);
    while (!detector_ids_.empty()) {
      int32_t detector_id = detector_ids_.begin()->first;

      base::AutoUnlock u(camera_map_lock_);
      OnDeviceDisconnected(detector_id);
    }
  }

  isolated_connection_ = nullptr;

  LOGF(FATAL) << "Lost connection to IP peripheral server";
}

void CameraHal::OnDeviceConnected(int32_t id,
                                  mojom::IpCameraDevicePtr device_ptr,
                                  mojom::IpCameraStreamPtr default_stream) {
  int camera_id = -1;
  {
    base::AutoLock l(camera_map_lock_);
    camera_id = next_camera_id_;

    auto device = std::make_unique<CameraDevice>(camera_id);
    if (device->Init(std::move(device_ptr), default_stream->format,
                     default_stream->width, default_stream->height,
                     default_stream->fps)) {
      LOGF(ERROR) << "Error creating camera device";
      return;
    }

    next_camera_id_++;
    detector_ids_[id] = camera_id;
    cameras_[camera_id] = std::move(device);
  }

  callbacks_set_.Wait();
  callbacks_->camera_device_status_change(callbacks_, camera_id,
                                          CAMERA_DEVICE_STATUS_PRESENT);
}

void CameraHal::OnDeviceDisconnected(int32_t id) {
  callbacks_set_.Wait();

  int hal_id = -1;
  {
    base::AutoLock l(camera_map_lock_);
    if (detector_ids_.find(id) == detector_ids_.end()) {
      LOGF(ERROR) << "Camera detector id " << id << " is invalid";
      return;
    }
    hal_id = detector_ids_[id];

    if (cameras_.find(hal_id) == cameras_.end()) {
      LOGF(ERROR) << "Camera id " << hal_id << " is invalid";
      return;
    }
  }

  callbacks_->camera_device_status_change(callbacks_, hal_id,
                                          CAMERA_DEVICE_STATUS_NOT_PRESENT);

  {
    base::AutoLock l(camera_map_lock_);
    if (cameras_[hal_id]->IsOpen()) {
      cameras_[hal_id]->Close();
    }

    detector_ids_.erase(id);
    cameras_.erase(hal_id);
  }
}

static int camera_device_open(const hw_module_t* module,
                              const char* name,
                              hw_device_t** device) {
  if (module != &HAL_MODULE_INFO_SYM.common) {
    LOGF(ERROR) << std::hex << std::showbase << "Invalid module " << module
                << " expected " << &HAL_MODULE_INFO_SYM.common;
    return -EINVAL;
  }

  int id;
  if (!base::StringToInt(name, &id)) {
    LOGF(ERROR) << "Invalid camera name " << name;
    return -EINVAL;
  }

  return CameraHal::GetInstance().OpenDevice(id, module, device);
}

static int get_number_of_cameras() {
  return CameraHal::GetInstance().GetNumberOfCameras();
}

static int get_camera_info(int id, struct camera_info* info) {
  return CameraHal::GetInstance().GetCameraInfo(id, info);
}

static int set_callbacks(const camera_module_callbacks_t* callbacks) {
  return CameraHal::GetInstance().SetCallbacks(callbacks);
}

static void get_vendor_tag_ops(vendor_tag_ops_t* /*ops*/) {}

static int open_legacy(const struct hw_module_t* /*module*/,
                       const char* /*id*/,
                       uint32_t /*halVersion*/,
                       struct hw_device_t** /*device*/) {
  return -ENOSYS;
}

static int set_torch_mode(const char* /*camera_id*/, bool /*enabled*/) {
  return -ENOSYS;
}

static int init() {
  return CameraHal::GetInstance().Init();
}

}  // namespace cros

static hw_module_methods_t gCameraModuleMethods = {
    .open = cros::camera_device_open};

camera_module_t HAL_MODULE_INFO_SYM CROS_CAMERA_EXPORT = {
    .common = {.tag = HARDWARE_MODULE_TAG,
               .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
               .hal_api_version = HARDWARE_HAL_API_VERSION,
               .id = CAMERA_HARDWARE_MODULE_ID,
               .name = "IP Camera HAL v3",
               .author = "The Chromium OS Authors",
               .methods = &gCameraModuleMethods,
               .dso = nullptr,
               .reserved = {0}},
    .get_number_of_cameras = cros::get_number_of_cameras,
    .get_camera_info = cros::get_camera_info,
    .set_callbacks = cros::set_callbacks,
    .get_vendor_tag_ops = cros::get_vendor_tag_ops,
    .open_legacy = cros::open_legacy,
    .set_torch_mode = cros::set_torch_mode,
    .init = cros::init,
    .reserved = {0}};
