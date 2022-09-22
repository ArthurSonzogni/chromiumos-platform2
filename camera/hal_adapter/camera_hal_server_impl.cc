/*
 * Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/camera_hal_server_impl.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/safe_strerror.h>
#include <base/threading/thread_task_runner_handle.h>

#include "common/utils/camera_hal_enumerator.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"
#include "cros-camera/utils/camera_config.h"
#include "features/feature_profile.h"
#include "hal_adapter/camera_hal_test_adapter.h"
#include "hal_adapter/camera_trace_event.h"

namespace cros {

namespace {

// One time initialization to connect to Perfetto system backend and register
// the camera trace categories.
void InitializeCameraTrace() {
  perfetto::TracingInitArgs args;
  args.backends |= perfetto::kSystemBackend;
  perfetto::Tracing::Initialize(args);
  cros_camera::TrackEvent::Register();
}

}  // namespace

CameraHalServerImpl::CameraHalServerImpl()
    : mojo_manager_(CameraMojoChannelManager::FromToken(
          CameraMojoChannelManagerToken::CreateInstance())),
      ipc_bridge_(new IPCBridge(this, mojo_manager_.get())) {
  VLOGF_ENTER();
  InitializeCameraTrace();
}

CameraHalServerImpl::~CameraHalServerImpl() {
  VLOGF_ENTER();

  ExitOnMainThread(0);
}

void CameraHalServerImpl::Start() {
  VLOGF_ENTER();
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (GpuResources::IsSupported()) {
    gpu_resources_ = std::make_unique<GpuResources>();
    if (!gpu_resources_->Initialize()) {
      LOGF(ERROR) << "Failed to initialize GPU resources";
      ExitOnMainThread(-ENODEV);
    }
  }

  int result = LoadCameraHal();
  if (result != 0) {
    ExitOnMainThread(result);
  }

  // We assume that |camera_hal_adapter_| will only be set once. If the
  // assumption changed, we should consider another way to provide
  // CameraHalAdapter.
  mojo_manager_->GetIpcTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraHalServerImpl::IPCBridge::Start,
                     ipc_bridge_->GetWeakPtr(), camera_hal_adapter_.get(),
                     base::BindRepeating(
                         [](const std::vector<cros_camera_hal_t*>& hals,
                            PrivacySwitchStateChangeCallback callback) {
                           for (const auto* hal : hals) {
                             if (hal->set_privacy_switch_callback != nullptr) {
                               hal->set_privacy_switch_callback(
                                   std::move(callback));
                             }
                           }
                         },
                         cros_camera_hals_)));
}

CameraHalServerImpl::IPCBridge::IPCBridge(
    CameraHalServerImpl* camera_hal_server,
    CameraMojoChannelManager* mojo_manager)
    : camera_hal_server_(camera_hal_server),
      mojo_manager_(mojo_manager),
      ipc_task_runner_(mojo_manager_->GetIpcTaskRunner()),
      main_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      receiver_(this) {}

CameraHalServerImpl::IPCBridge::~IPCBridge() {
  receiver_.reset();
  callbacks_.reset();
}

void CameraHalServerImpl::IPCBridge::Start(
    CameraHalAdapter* camera_hal_adapter,
    SetPrivacySwitchCallback set_privacy_switch_callback) {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  if (receiver_.is_bound()) {
    return;
  }

  camera_hal_adapter_ = camera_hal_adapter;

  mojo::PendingRemote<mojom::CameraHalServer> server =
      receiver_.BindNewPipeAndPassRemote();
  receiver_.set_disconnect_handler(
      base::BindOnce(&CameraHalServerImpl::IPCBridge::OnServiceMojoChannelError,
                     GetWeakPtr()));
  mojo_manager_->RegisterServer(
      std::move(server),
      base::BindOnce(&CameraHalServerImpl::IPCBridge::OnServerRegistered,
                     GetWeakPtr(), std::move(set_privacy_switch_callback)),
      base::BindOnce(&CameraHalServerImpl::IPCBridge::OnServiceMojoChannelError,
                     GetWeakPtr()));
}

void CameraHalServerImpl::IPCBridge::CreateChannel(
    mojo::PendingReceiver<mojom::CameraModule> camera_module_receiver,
    mojom::CameraClientType camera_client_type) {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  camera_hal_adapter_->OpenCameraHal(std::move(camera_module_receiver),
                                     camera_client_type);
}

void CameraHalServerImpl::IPCBridge::SetTracingEnabled(bool enabled) {
  // Since we have migrated to use Perfetto SDK for camera tracing, the tracing
  // overhead is neglectable if the interested categories are not enabled so we
  // don't need to enable/disable it ourselves.
  // TODO(b/212231270): Remove this function once the call site (Chrome) is
  // removed.
}

void CameraHalServerImpl::IPCBridge::SetAutoFramingState(
    mojom::CameraAutoFramingState state) {
  if (state == mojom::CameraAutoFramingState::ON_MULTI) {
    LOG(WARNING) << "auto framing multi people mode is not implemented yet, "
                 << "fallback to single person mode.";
    state = mojom::CameraAutoFramingState::ON_SINGLE;
  }
  camera_hal_adapter_->SetAutoFramingState(state);
}

void CameraHalServerImpl::IPCBridge::GetCameraSWPrivacySwitchState(
    mojom::CameraHalServer::GetCameraSWPrivacySwitchStateCallback callback) {
  std::move(callback).Run(camera_hal_adapter_->GetCameraSWPrivacySwitchState());
}

void CameraHalServerImpl::IPCBridge::SetCameraSWPrivacySwitchState(
    mojom::CameraPrivacySwitchState state) {
  if (camera_hal_adapter_->GetCameraSWPrivacySwitchState() == state) {
    return;
  }
  camera_hal_adapter_->SetCameraSWPrivacySwitchState(state);
  callbacks_->CameraSWPrivacySwitchStateChange(state);
}

void CameraHalServerImpl::IPCBridge::GetAutoFramingSupported(
    mojom::CameraHalServer::GetAutoFramingSupportedCallback callback) {
  FeatureProfile feature_profile;
  std::move(callback).Run(
      feature_profile.IsEnabled(FeatureProfile::FeatureType::kAutoFraming));
}

void CameraHalServerImpl::IPCBridge::NotifyCameraActivityChange(
    int32_t camera_id, bool opened, mojom::CameraClientType type) {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  DCHECK(callbacks_.is_bound());

  callbacks_->CameraDeviceActivityChange(camera_id, opened, type);
}

base::WeakPtr<CameraHalServerImpl::IPCBridge>
CameraHalServerImpl::IPCBridge::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void CameraHalServerImpl::IPCBridge::OnServerRegistered(
    SetPrivacySwitchCallback set_privacy_switch_callback,
    int32_t result,
    mojo::PendingRemote<mojom::CameraHalServerCallbacks> callbacks) {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  if (result != 0) {
    LOGF(ERROR) << "Failed to register camera server: "
                << base::safe_strerror(-result);
    return;
  }
  callbacks_.Bind(std::move(callbacks));

  std::move(set_privacy_switch_callback)
      .Run(base::BindRepeating(
          &CameraHalServerImpl::IPCBridge::OnPrivacySwitchStatusChanged,
          base::Unretained(this)));

  LOGF(INFO) << "Successfully registered camera server.";
}

void CameraHalServerImpl::IPCBridge::OnServiceMojoChannelError() {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  // The CameraHalDispatcher Mojo parent is probably dead. We need to restart
  // another process in order to connect to the new Mojo parent.
  LOGF(INFO) << "Mojo connection to (Chrome) CameraHalDispatcher is "
                "disconnected. Chrome may have crashed.";
  receiver_.reset();
  main_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraHalServerImpl::ExitOnMainThread,
                     base::Unretained(camera_hal_server_), ECONNRESET));
}

void CameraHalServerImpl::IPCBridge::OnPrivacySwitchStatusChanged(
    int camera_id, PrivacySwitchState state) {
  cros::mojom::CameraPrivacySwitchState state_in_mojo;
  if (state == PrivacySwitchState::kUnknown) {
    state_in_mojo = cros::mojom::CameraPrivacySwitchState::UNKNOWN;
  } else if (state == PrivacySwitchState::kOn) {
    state_in_mojo = cros::mojom::CameraPrivacySwitchState::ON;
  } else {  // state == PrivacySwitchState::kOff
    state_in_mojo = cros::mojom::CameraPrivacySwitchState::OFF;
  }
  callbacks_->CameraPrivacySwitchStateChange(state_in_mojo, camera_id);
}

int CameraHalServerImpl::LoadCameraHal() {
  VLOGF_ENTER();
  DCHECK(!camera_hal_adapter_);
  DCHECK_EQ(cros_camera_hals_.size(), 0);
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  std::vector<std::pair<camera_module_t*, cros_camera_hal_t*>>
      camera_interfaces;
  std::unique_ptr<CameraConfig> config =
      CameraConfig::Create(constants::kCrosCameraTestConfigPathString);
  bool enable_front =
           config->GetBoolean(constants::kCrosEnableFrontCameraOption, true),
       enable_back =
           config->GetBoolean(constants::kCrosEnableBackCameraOption, true),
       enable_external =
           config->GetBoolean(constants::kCrosEnableExternalCameraOption, true);

  for (const auto& dll : GetCameraHalPaths()) {
    LOGF(INFO) << "Try to load camera hal " << dll.value();

    void* handle = dlopen(dll.value().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
      LOGF(INFO) << "Failed to dlopen: " << dlerror();
      return -ENOENT;
    }

    cros_camera_hal_t* cros_camera_hal = static_cast<cros_camera_hal_t*>(
        dlsym(handle, CROS_CAMERA_HAL_INFO_SYM_AS_STR));
    if (cros_camera_hal) {
      // libcamera may not implement the interface.
      cros_camera_hal->set_up(mojo_manager_.get());
      cros_camera_hals_.push_back(cros_camera_hal);
    }

    auto* module = static_cast<camera_module_t*>(
        dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR));
    if (!module) {
      LOGF(ERROR) << "Failed to get camera_module_t pointer with symbol name "
                  << HAL_MODULE_INFO_SYM_AS_STR << " from " << dll.value();
      return ELIBBAD;
    }

    camera_interfaces.emplace_back(module, cros_camera_hal);
  }

  auto active_callback =
      base::BindRepeating(&CameraHalServerImpl::OnCameraActivityChange,
                          base::Unretained(this), ipc_bridge_->GetWeakPtr());
  if (enable_front && enable_back && enable_external) {
    camera_hal_adapter_ = std::make_unique<CameraHalAdapter>(
        camera_interfaces, mojo_manager_.get(), active_callback);
  } else {
    camera_hal_adapter_ = std::make_unique<CameraHalTestAdapter>(
        camera_interfaces, mojo_manager_.get(), active_callback, enable_front,
        enable_back, enable_external);
  }

  LOGF(INFO) << "Running camera HAL adapter on " << getpid();

  if (!camera_hal_adapter_->Start(gpu_resources_.get())) {
    LOGF(ERROR) << "Failed to start camera HAL adapter";
    return -ENODEV;
  }

  return 0;
}

void CameraHalServerImpl::ExitOnMainThread(int exit_status) {
  VLOGF_ENTER();
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  for (auto* cros_camera_hal : cros_camera_hals_) {
    cros_camera_hal->tear_down();
  }

  auto future = Future<void>::Create(nullptr);
  auto delete_ipc_bridge = base::BindOnce(
      [](std::unique_ptr<IPCBridge> ipc_bridge,
         base::OnceCallback<void(void)> callback) {
        std::move(callback).Run();
      },
      std::move(ipc_bridge_), cros::GetFutureCallback(future));
  mojo_manager_->GetIpcTaskRunner()->PostTask(FROM_HERE,
                                              std::move(delete_ipc_bridge));
  future->Wait(-1);

  // To make sure all the devices are properly closed before triggering the exit
  // handlers on Camera HALs side, we explicitly reset the CameraHalAdapter.
  camera_hal_adapter_.reset();

  exit(exit_status);
}

void CameraHalServerImpl::OnCameraActivityChange(
    base::WeakPtr<IPCBridge> ipc_bridge,
    int32_t camera_id,
    bool opened,
    mojom::CameraClientType type) {
  mojo_manager_->GetIpcTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &CameraHalServerImpl::IPCBridge::NotifyCameraActivityChange,
          ipc_bridge, camera_id, opened, type));
}

}  // namespace cros
