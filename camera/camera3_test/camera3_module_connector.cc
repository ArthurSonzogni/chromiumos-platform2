// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera3_test/camera3_module_connector.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/no_destructor.h>
#include <base/unguessable_token.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <gtest/gtest.h>
#include <system/camera_metadata_hidden.h>

#include "camera/mojo/unguessable_token.mojom.h"
#include "camera3_test/camera3_device_connector.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/common.h"
#include "cros-camera/constants.h"
#include "cros-camera/future.h"
#include "cros-camera/ipc_util.h"

namespace {

std::optional<base::UnguessableToken> ReadTestClientToken() {
  static constexpr char kTestClientTokenPath[] =
      "/run/camera_tokens/testing/token";

  base::FilePath token_path(kTestClientTokenPath);
  std::string token_string;
  if (!base::ReadFileToString(token_path, &token_string)) {
    LOGF(ERROR) << "Failed to read token for test client";
    return std::nullopt;
  }
  return cros::TokenFromString(token_string);
}

}  // namespace

namespace camera3_test {

HalModuleConnector::HalModuleConnector(camera_module_t* cam_module,
                                       cros::CameraThread* hal_thread)
    : cam_module_(cam_module), hal_thread_(hal_thread) {}

int HalModuleConnector::GetNumberOfCameras() {
  if (!cam_module_) {
    return -ENODEV;
  }
  int result = -EINVAL;
  hal_thread_->PostTaskSync(
      FROM_HERE,
      base::BindOnce(&HalModuleConnector::GetNumberOfCamerasOnHalThread,
                     base::Unretained(this), &result));
  return result;
}

void HalModuleConnector::GetNumberOfCamerasOnHalThread(int* result) {
  *result = cam_module_->get_number_of_cameras();
}

std::unique_ptr<DeviceConnector> HalModuleConnector::OpenDevice(int cam_id) {
  if (!cam_module_) {
    return nullptr;
  }
  std::unique_ptr<DeviceConnector> dev_connector;
  hal_thread_->PostTaskSync(
      FROM_HERE,
      base::BindOnce(&HalModuleConnector::OpenDeviceOnHalThread,
                     base::Unretained(this), cam_id, &dev_connector));
  return dev_connector;
}

void HalModuleConnector::OpenDeviceOnHalThread(
    int cam_id, std::unique_ptr<DeviceConnector>* dev_connector) {
  hw_device_t* device = nullptr;
  char cam_id_name[3];
  snprintf(cam_id_name, sizeof(cam_id_name), "%d", cam_id);
  if (cam_module_->common.methods->open(&cam_module_->common, cam_id_name,
                                        &device) == 0) {
    *dev_connector = std::make_unique<HalDeviceConnector>(
        cam_id, reinterpret_cast<camera3_device_t*>(device));
  }
}

int HalModuleConnector::GetCameraInfo(int cam_id, camera_info* info) {
  if (!cam_module_) {
    return -ENODEV;
  }
  int result = -ENODEV;
  hal_thread_->PostTaskSync(
      FROM_HERE, base::BindOnce(&HalModuleConnector::GetCameraInfoOnHalThread,
                                base::Unretained(this), cam_id, info, &result));
  return result;
}

void HalModuleConnector::GetCameraInfoOnHalThread(int cam_id,
                                                  camera_info* info,
                                                  int* result) {
  *result = cam_module_->get_camera_info(cam_id, info);
}

ClientModuleConnector::ClientModuleConnector(CameraHalClient* cam_client)
    : cam_client_(cam_client) {}

int ClientModuleConnector::GetNumberOfCameras() {
  if (!cam_client_) {
    return -ENODEV;
  }
  return cam_client_->GetNumberOfCameras();
}

std::unique_ptr<DeviceConnector> ClientModuleConnector::OpenDevice(int cam_id) {
  auto dev_connector = std::make_unique<ClientDeviceConnector>();
  cam_client_->OpenDevice(cam_id, dev_connector->GetDeviceOpsReceiver());
  return dev_connector;
}

int ClientModuleConnector::GetCameraInfo(int cam_id, camera_info* info) {
  if (!cam_client_) {
    return -ENODEV;
  }
  return cam_client_->GetCameraInfo(cam_id, info);
}

// static
CameraHalClient* CameraHalClient::GetInstance() {
  static base::NoDestructor<CameraHalClient> c;
  return c.get();
}

CameraHalClient::CameraHalClient()
    : camera_hal_client_(this),
      mojo_module_callbacks_(this),
      ipc_initialized_(base::WaitableEvent::ResetPolicy::MANUAL,
                       base::WaitableEvent::InitialState::NOT_SIGNALED),
      vendor_tag_count_(0),
      ipc_task_runner_(
          cros::CameraMojoChannelManager::GetInstance()->GetIpcTaskRunner()) {}

int CameraHalClient::Start(camera_module_callbacks_t* callbacks) {
  static constexpr ::base::TimeDelta kIpcTimeout = ::base::Seconds(3);
  if (!callbacks) {
    return -EINVAL;
  }
  camera_module_callbacks_ = callbacks;

  auto future = cros::Future<int>::Create(nullptr);
  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraHalClient::ConnectToDispatcher,
                     base::Unretained(this), cros::GetFutureCallback(future)));
  int result = future->Get();
  if (result != 0) {
    LOGF(ERROR) << "Failed to connect to dispatcher";
    return result;
  }

  if (!ipc_initialized_.TimedWait(kIpcTimeout)) {
    LOGF(ERROR) << "Failed to set up channel and get vendor tags";
    return -EIO;
  }

  return 0;
}

void CameraHalClient::ConnectToDispatcher(
    base::OnceCallback<void(int)> callback) {
  ASSERT_TRUE(ipc_task_runner_->BelongsToCurrentThread());

  cros::CameraMojoChannelManager::GetInstance()
      ->RequestServiceFromMojoServiceManager(
          /*service_name=*/chromeos::mojo_services::kCrosCameraHalDispatcher,
          dispatcher_.BindNewPipeAndPassReceiver().PassPipe());

  if (!dispatcher_.is_bound()) {
    LOGF(ERROR) << "Failed to bind mojo dispatcher";
    std::move(callback).Run(-EIO);
    return;
  }

  auto token = ReadTestClientToken();
  if (!token.has_value()) {
    LOGF(ERROR) << "Failed to read test client token";
    std::move(callback).Run(-EIO);
    return;
  }
  auto mojo_token = mojo_base::mojom::UnguessableToken::New();
  mojo_token->high = token->GetHighForSerialization();
  mojo_token->low = token->GetLowForSerialization();
  dispatcher_->RegisterClientWithToken(
      camera_hal_client_.BindNewPipeAndPassRemote(),
      cros::mojom::CameraClientType::TESTING, std::move(mojo_token),
      std::move(callback));
}

void CameraHalClient::SetUpChannel(
    mojo::PendingRemote<cros::mojom::CameraModule> camera_module) {
  ASSERT_TRUE(ipc_task_runner_->BelongsToCurrentThread());
  camera_module_.Bind(std::move(camera_module));
  camera_module_.set_disconnect_handler(base::BindOnce(
      &CameraHalClient::onIpcConnectionLost, base::Unretained(this)));

  camera_module_->SetCallbacksAssociated(
      mojo_module_callbacks_.BindNewEndpointAndPassRemote(),
      base::BindOnce(&CameraHalClient::OnSetCallbacks, base::Unretained(this)));
}

void CameraHalClient::OnSetCallbacks(int32_t result) {
  ASSERT_TRUE(ipc_task_runner_->BelongsToCurrentThread());
  if (result != 0) {
    LOGF(ERROR) << "Failed to set callbacks";
    exit(EXIT_FAILURE);
  }

  camera_module_->GetVendorTagOps(
      vendor_tag_ops_.BindNewPipeAndPassReceiver(),
      base::BindOnce(&CameraHalClient::OnGotVendorTagOps,
                     base::Unretained(this)));
}

void CameraHalClient::OnGotVendorTagOps() {
  vendor_tag_ops_->GetAllTags(
      base::BindOnce(&CameraHalClient::OnGotAllTags, base::Unretained(this)));
}

void CameraHalClient::OnGotAllTags(const std::vector<uint32_t>& tag_array) {
  if (tag_array.empty()) {
    ipc_initialized_.Signal();
    return;
  }
  vendor_tag_count_ = tag_array.size();
  for (const auto& tag : tag_array) {
    vendor_tag_ops_->GetSectionName(
        tag, base::BindOnce(&CameraHalClient::OnGotSectionName,
                            base::Unretained(this), tag));
  }
}

void CameraHalClient::OnGotSectionName(uint32_t tag,
                                       const std::optional<std::string>& name) {
  ASSERT_NE(std::nullopt, name);
  vendor_tag_map_[tag].section_name = *name;

  vendor_tag_ops_->GetTagName(tag,
                              base::BindOnce(&CameraHalClient::OnGotTagName,
                                             base::Unretained(this), tag));
}

void CameraHalClient::OnGotTagName(uint32_t tag,
                                   const std::optional<std::string>& name) {
  ASSERT_NE(std::nullopt, name);
  vendor_tag_map_[tag].tag_name = *name;

  vendor_tag_ops_->GetTagType(tag,
                              base::BindOnce(&CameraHalClient::OnGotTagType,
                                             base::Unretained(this), tag));
}

void CameraHalClient::OnGotTagType(uint32_t tag, int32_t type) {
  vendor_tag_map_[tag].type = type;

  if ((--vendor_tag_count_) == 0) {
    for (const auto& it : vendor_tag_map_) {
      ASSERT_TRUE(vendor_tag_manager_.Add(it.first, it.second.section_name,
                                          it.second.tag_name, it.second.type));
    }
    vendor_tag_map_.clear();
    if (set_camera_metadata_vendor_ops(&vendor_tag_manager_) != 0) {
      ADD_FAILURE() << "Failed to set vendor ops to camera metadata";
    }

    ipc_initialized_.Signal();
  }
}

int CameraHalClient::GetNumberOfCameras() {
  auto future = cros::Future<int32_t>::Create(nullptr);
  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraHalClient::GetNumberOfCamerasOnIpcThread,
                     base::Unretained(this), cros::GetFutureCallback(future)));
  if (!future->Wait()) {
    ADD_FAILURE() << "Wait timeout";
    return -ENODEV;
  }
  return future->Get();
}

void CameraHalClient::GetNumberOfCamerasOnIpcThread(
    base::OnceCallback<void(int32_t)> cb) {
  if (!ipc_initialized_.IsSignaled()) {
    std::move(cb).Run(-ENODEV);
    return;
  }
  camera_module_->GetNumberOfCameras(std::move(cb));
}

int CameraHalClient::GetCameraInfo(int cam_id, camera_info* info) {
  if (!info) {
    return -EINVAL;
  }
  auto future = cros::Future<int32_t>::Create(nullptr);
  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&CameraHalClient::GetCameraInfoOnIpcThread,
                                base::Unretained(this), cam_id, info,
                                cros::GetFutureCallback(future)));
  if (!future->Wait()) {
    ADD_FAILURE() << "Wait timeout";
    return -ENODEV;
  }
  return future->Get();
}

void CameraHalClient::GetCameraInfoOnIpcThread(
    int cam_id, camera_info* info, base::OnceCallback<void(int32_t)> cb) {
  if (!ipc_initialized_.IsSignaled()) {
    std::move(cb).Run(-ENODEV);
    return;
  }
  camera_module_->GetCameraInfo(
      cam_id,
      base::BindOnce(&CameraHalClient::OnGotCameraInfo, base::Unretained(this),
                     cam_id, info, std::move(cb)));
}

void CameraHalClient::OnGotCameraInfo(int cam_id,
                                      camera_info* info,
                                      base::OnceCallback<void(int32_t)> cb,
                                      int32_t result,
                                      cros::mojom::CameraInfoPtr info_ptr) {
  if (result == 0) {
    memset(info, 0, sizeof(*info));
    info->facing = static_cast<int>(info_ptr->facing);
    info->orientation = info_ptr->orientation;
    info->device_version = info_ptr->device_version;
    if (!base::Contains(static_characteristics_map_, cam_id)) {
      static_characteristics_map_[cam_id] =
          cros::internal::DeserializeCameraMetadata(
              info_ptr->static_camera_characteristics);
    }
    info->static_camera_characteristics =
        static_characteristics_map_[cam_id].get();
    info->resource_cost = info_ptr->resource_cost->resource_cost;
    if (!base::Contains(conflicting_devices_map_, cam_id)) {
      for (const auto& it : *info_ptr->conflicting_devices) {
        conflicting_devices_char_map_[cam_id].emplace_back(it.begin(),
                                                           it.end());
        conflicting_devices_char_map_[cam_id].back().push_back('\0');
        conflicting_devices_map_[cam_id].push_back(
            conflicting_devices_char_map_[cam_id].back().data());
      }
    }
    info->conflicting_devices_length = conflicting_devices_map_[cam_id].size();
    info->conflicting_devices = conflicting_devices_map_[cam_id].data();
  }
  std::move(cb).Run(result);
}

void CameraHalClient::OpenDevice(
    int cam_id, mojo::PendingReceiver<cros::mojom::Camera3DeviceOps> dev_ops) {
  auto future = cros::Future<int32_t>::Create(nullptr);
  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraHalClient::OpenDeviceOnIpcThread,
                     base::Unretained(this), cam_id, std::move(dev_ops),
                     cros::GetFutureCallback(future)));
  if (!future->Wait()) {
    ADD_FAILURE() << __func__ << " timeout";
  }
}

void CameraHalClient::OpenDeviceOnIpcThread(
    int cam_id,
    mojo::PendingReceiver<cros::mojom::Camera3DeviceOps> dev_ops,
    base::OnceCallback<void(int32_t)> cb) {
  if (!ipc_initialized_.IsSignaled()) {
    std::move(cb).Run(-ENODEV);
    return;
  }
  camera_module_->OpenDevice(cam_id, std::move(dev_ops), std::move(cb));
}

void CameraHalClient::CameraDeviceStatusChange(
    int32_t camera_id, cros::mojom::CameraDeviceStatus new_status) {
  ASSERT_TRUE(ipc_task_runner_->BelongsToCurrentThread());
  camera_module_callbacks_->camera_device_status_change(
      camera_module_callbacks_, camera_id,
      static_cast<camera_device_status_t>(new_status));
}

void CameraHalClient::TorchModeStatusChange(
    int32_t camera_id, cros::mojom::TorchModeStatus new_status) {
  ASSERT_TRUE(ipc_task_runner_->BelongsToCurrentThread());
  std::stringstream ss;
  ss << camera_id;
  camera_module_callbacks_->torch_mode_status_change(
      camera_module_callbacks_, ss.str().c_str(),
      static_cast<camera_device_status_t>(new_status));
}

void CameraHalClient::onIpcConnectionLost() {
  camera_module_.reset();
  ipc_initialized_.Reset();
  static_characteristics_map_.clear();
  vendor_tag_map_.clear();
  conflicting_devices_char_map_.clear();
  conflicting_devices_map_.clear();
}

}  // namespace camera3_test
