/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/camera_device_adapter.h"

#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/strings/string_number_conversions.h>
#include <base/timer/elapsed_timer.h>
#include <drm_fourcc.h>
#include <hardware/camera3.h>
#include <libyuv.h>
#include <mojo/public/cpp/system/platform_handle.h>
#include <sync/sync.h>
#include <system/camera_metadata.h>

#include "common/camera_buffer_handle.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"
#include "cros-camera/ipc_util.h"
#include "cros-camera/utils/camera_config.h"
#include "hal_adapter/camera3_callback_ops_delegate.h"
#include "hal_adapter/camera3_device_ops_delegate.h"

namespace cros {

constexpr base::TimeDelta kMonitorTimeDelta = base::TimeDelta::FromSeconds(2);

CameraMonitor::CameraMonitor(const std::string& name)
    : name_(name), thread_(name + "Monitor"), is_kicked_(false) {}

void CameraMonitor::StartMonitor(base::OnceClosure timeout_callback) {
  timeout_callback_ = std::move(timeout_callback);
  thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&CameraMonitor::StartMonitorOnThread,
                                base::Unretained(this)));
}

void CameraMonitor::Kick() {
  base::AutoLock l(lock_);
  is_kicked_ = true;
  // Resume the monitor timer if it timed out before.
  // Need to check IsRunning() because the monitor may be detached during close
  // device.
  if (thread_.IsRunning()) {
    thread_.task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&CameraMonitor::MaybeResumeMonitorOnThread,
                                  base::Unretained(this)));
  }
}

void CameraMonitor::Attach() {
  if (!thread_.Start()) {
    LOGF(ERROR) << "Monitor thread failed to start";
    return;
  }
  auto future = cros::Future<void>::Create(nullptr);
  thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraMonitor::SetTaskRunnerOnThread,
                     base::Unretained(this), cros::GetFutureCallback(future)));
  future->Wait();
}

bool CameraMonitor::HasBeenKicked() {
  base::AutoLock l(lock_);
  return is_kicked_;
}

void CameraMonitor::Detach() {
  thread_.Stop();
  base::OneShotTimer::Stop();
}

void CameraMonitor::SetTaskRunnerOnThread(base::Callback<void()> callback) {
  DCHECK(thread_.task_runner()->BelongsToCurrentThread());
  base::OneShotTimer::SetTaskRunner(thread_.task_runner());
  callback.Run();
}

void CameraMonitor::StartMonitorOnThread() {
  DCHECK(thread_.task_runner()->BelongsToCurrentThread());
  if (base::OneShotTimer::IsRunning()) {
    base::OneShotTimer::Stop();
  }

  base::OneShotTimer::Start(
      FROM_HERE, kMonitorTimeDelta,
      base::Bind(&CameraMonitor::MonitorTimeout, base::Unretained(this)));
  LOGF(INFO) << "Start " << name_ << " monitor";
}

void CameraMonitor::MaybeResumeMonitorOnThread() {
  DCHECK(thread_.task_runner()->BelongsToCurrentThread());
  if (base::OneShotTimer::IsRunning()) {
    return;
  }

  base::OneShotTimer::Start(
      FROM_HERE, kMonitorTimeDelta,
      base::Bind(&CameraMonitor::MonitorTimeout, base::Unretained(this)));
  LOGF(INFO) << "Resume " << name_ << " monitor";
}

void CameraMonitor::MonitorTimeout() {
  DCHECK(thread_.task_runner()->BelongsToCurrentThread());
  base::AutoLock l(lock_);
  if (is_kicked_) {
    base::OneShotTimer::Start(
        FROM_HERE, kMonitorTimeDelta,
        base::Bind(&CameraMonitor::MonitorTimeout, base::Unretained(this)));
  } else {
    LOGF(WARNING) << "No " << name_ << " for more than " << kMonitorTimeDelta;
    if (timeout_callback_) {
      std::move(timeout_callback_).Run();
    }
  }
  is_kicked_ = false;
}

CameraDeviceAdapter::CameraDeviceAdapter(
    camera3_device_t* camera_device,
    uint32_t device_api_version,
    const camera_metadata_t* static_info,
    base::RepeatingCallback<int(int)> get_internal_camera_id_callback,
    base::RepeatingCallback<int(int)> get_public_camera_id_callback,
    base::Callback<void()> close_callback,
    std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators)
    : camera_device_ops_thread_("CameraDeviceOpsThread"),
      camera_callback_ops_thread_("CameraCallbackOpsThread"),
      fence_sync_thread_("FenceSyncThread"),
      reprocess_effect_thread_("ReprocessEffectThread"),
      get_internal_camera_id_callback_(get_internal_camera_id_callback),
      get_public_camera_id_callback_(get_public_camera_id_callback),
      close_callback_(close_callback),
      close_called_(false),
      camera_device_(camera_device),
      device_api_version_(device_api_version),
      static_info_(static_info),
      camera_metrics_(CameraMetrics::New()),
      capture_request_monitor_("CaptureRequest"),
      capture_result_monitor_("CaptureResult"),
      stream_manipulators_(std::move(stream_manipulators)) {
  VLOGF_ENTER() << ":" << camera_device_;
  camera3_callback_ops_t::process_capture_result = ProcessCaptureResult;
  camera3_callback_ops_t::notify = Notify;
}

CameraDeviceAdapter::~CameraDeviceAdapter() {
  VLOGF_ENTER() << ":" << camera_device_;

  // Make sure that the camera is closed when the device adapter is destructed.
  Close();

  camera_device_ops_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraDeviceAdapter::ResetDeviceOpsDelegateOnThread,
                     base::Unretained(this)));
  camera_callback_ops_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraDeviceAdapter::ResetCallbackOpsDelegateOnThread,
                     base::Unretained(this)));
  camera_device_ops_thread_.Stop();
  camera_callback_ops_thread_.Stop();
}

bool CameraDeviceAdapter::Start(
    HasReprocessEffectVendorTagCallback
        has_reprocess_effect_vendor_tag_callback,
    ReprocessEffectCallback reprocess_effect_callback) {
  if (!camera_device_ops_thread_.Start()) {
    LOGF(ERROR) << "Failed to start CameraDeviceOpsThread";
    return false;
  }
  if (!camera_callback_ops_thread_.Start()) {
    LOGF(ERROR) << "Failed to start CameraCallbackOpsThread";
    return false;
  }
  device_ops_delegate_.reset(new Camera3DeviceOpsDelegate(
      this, camera_device_ops_thread_.task_runner()));
  partial_result_count_ = [&]() {
    camera_metadata_ro_entry entry;
    if (find_camera_metadata_ro_entry(
            static_info_, ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &entry) != 0) {
      return 1;
    }
    return entry.data.i32[0];
  }();
  camera_metadata_inspector_ =
      CameraMetadataInspector::Create(partial_result_count_);
  has_reprocess_effect_vendor_tag_callback_ =
      std::move(has_reprocess_effect_vendor_tag_callback);
  reprocess_effect_callback_ = std::move(reprocess_effect_callback);
  return true;
}

void CameraDeviceAdapter::Bind(
    mojo::PendingReceiver<mojom::Camera3DeviceOps> device_ops_receiver) {
  device_ops_delegate_->Bind(
      std::move(device_ops_receiver),
      // Close the device when the Mojo channel breaks.
      base::Bind(base::IgnoreResult(&CameraDeviceAdapter::Close),
                 base::Unretained(this)));
}

int32_t CameraDeviceAdapter::Initialize(
    mojo::PendingRemote<mojom::Camera3CallbackOps> callback_ops) {
  VLOGF_ENTER();
  {
    base::AutoLock l(fence_sync_thread_lock_);
    if (!fence_sync_thread_.Start()) {
      LOGF(ERROR) << "Fence sync thread failed to start";
      return -ENODEV;
    }
  }
  if (!reprocess_effect_thread_.Start()) {
    LOGF(ERROR) << "Reprocessing effect thread failed to start";
    return -ENODEV;
  }

  auto result_callback = base::BindRepeating(
      &CameraDeviceAdapter::ReturnResultToClient, base::Unretained(this));
  for (auto it = stream_manipulators_.begin(); it != stream_manipulators_.end();
       ++it) {
    (*it)->Initialize(static_info_, result_callback);
  }

  capture_request_monitor_.Attach();
  capture_result_monitor_.Attach();
  base::AutoLock l(callback_ops_delegate_lock_);
  // Unlike the camera module, only one peer is allowed to access a camera
  // device at any time.
  DCHECK(!callback_ops_delegate_);
  callback_ops_delegate_.reset(new Camera3CallbackOpsDelegate(
      camera_callback_ops_thread_.task_runner()));
  callback_ops_delegate_->Bind(
      std::move(callback_ops),
      base::Bind(&CameraDeviceAdapter::ResetCallbackOpsDelegateOnThread,
                 base::Unretained(this)));
  return camera_device_->ops->initialize(camera_device_, this);
}

int32_t CameraDeviceAdapter::ConfigureStreams(
    mojom::Camera3StreamConfigurationPtr config,
    mojom::Camera3StreamConfigurationPtr* updated_config) {
  VLOGF_ENTER();

  base::ElapsedTimer timer;

  base::AutoLock l(streams_lock_);

  // Free previous allocated buffers before new allocation.
  FreeAllocatedStreamBuffers();

  internal::ScopedStreams new_streams;
  for (const auto& s : config->streams) {
    LOGF(INFO) << "id = " << s->id << ", type = " << s->stream_type
               << ", size = " << s->width << "x" << s->height
               << ", format = " << s->format;
    uint64_t id = s->id;
    auto& stream = new_streams[id];
    stream.reset(new internal::camera3_stream_aux_t);
    memset(stream.get(), 0, sizeof(*stream.get()));
    stream->stream_type = static_cast<camera3_stream_type_t>(s->stream_type);
    stream->width = s->width;
    stream->height = s->height;
    stream->format = static_cast<int32_t>(s->format);
    stream->usage = s->usage;
    stream->max_buffers = s->max_buffers;
    stream->data_space = static_cast<android_dataspace_t>(s->data_space);
    stream->rotation = static_cast<camera3_stream_rotation_t>(s->rotation);
    if (device_api_version_ >= CAMERA_DEVICE_API_VERSION_3_5) {
      DCHECK(s->physical_camera_id.has_value());
      if (s->physical_camera_id.value() != "") {
        int public_camera_id;
        if (!base::StringToInt(s->physical_camera_id.value(),
                               &public_camera_id)) {
          LOGF(ERROR) << "Invalid physical camera ID: "
                      << s->physical_camera_id.value();
          return -EINVAL;
        }
        int internal_camera_id =
            get_internal_camera_id_callback_.Run(public_camera_id);
        if (internal_camera_id == -1) {
          LOGF(ERROR) << "Failed to find internal camera ID for camera "
                      << public_camera_id;
          return -EINVAL;
        }
        stream->physical_camera_id_string =
            base::NumberToString(internal_camera_id);
      } else {
        stream->physical_camera_id_string = "";
      }
      stream->physical_camera_id = stream->physical_camera_id_string.c_str();
    }
    stream->crop_rotate_scale_degrees = 0;
    if (s->crop_rotate_scale_info) {
      stream->crop_rotate_scale_degrees =
          static_cast<camera3_stream_rotation_t>(
              s->crop_rotate_scale_info->crop_rotate_scale_degrees);
    }

    // Currently we are not interest in the resolution of input stream and
    // bidirectional stream.
    if (stream->stream_type == CAMERA3_STREAM_OUTPUT) {
      camera_metrics_->SendConfigureStreamResolution(
          stream->width, stream->height, stream->format);
    }
  }
  streams_.swap(new_streams);

  std::vector<camera3_stream_t*> streams_ptr;
  for (const auto& s : streams_) {
    streams_ptr.push_back(s.second.get());
  }
  internal::ScopedCameraMetadata session_parameters;
  if (device_api_version_ >= CAMERA_DEVICE_API_VERSION_3_5) {
    session_parameters =
        internal::DeserializeCameraMetadata(config->session_parameters);
  }
  Camera3StreamConfiguration stream_config(camera3_stream_configuration_t{
      .num_streams = static_cast<uint32_t>(streams_ptr.size()),
      .streams = streams_ptr.data(),
      .operation_mode = static_cast<camera3_stream_configuration_mode_t>(
          config->operation_mode),
      .session_parameters = session_parameters.get(),
  });

  for (auto it = stream_manipulators_.begin(); it != stream_manipulators_.end();
       ++it) {
    (*it)->ConfigureStreams(&stream_config);
  }

  camera3_stream_configuration_t* raw_config = stream_config.Lock();
  int32_t result =
      camera_device_->ops->configure_streams(camera_device_, raw_config);
  stream_config.Unlock();

  // Call OnConfiguredStreams in reverse order so the stream manipulators can
  // unwind the stream modifications.
  for (auto it = stream_manipulators_.rbegin();
       it != stream_manipulators_.rend(); ++it) {
    (*it)->OnConfiguredStreams(&stream_config);
  }

  if (result == 0) {
    *updated_config = mojom::Camera3StreamConfiguration::New();
    (*updated_config)->operation_mode = config->operation_mode;
    if (device_api_version_ >= CAMERA_DEVICE_API_VERSION_3_5) {
      (*updated_config)->session_parameters =
          std::move(config->session_parameters);
    }
    for (const auto& s : streams_) {
      mojom::Camera3StreamPtr ptr = mojom::Camera3Stream::New();
      ptr->id = s.first;
      ptr->format = static_cast<mojom::HalPixelFormat>(s.second->format);
      ptr->width = s.second->width;
      ptr->height = s.second->height;
      ptr->stream_type =
          static_cast<mojom::Camera3StreamType>(s.second->stream_type);
      ptr->data_space = s.second->data_space;
      // HAL should only change usage and max_buffers.
      ptr->usage = s.second->usage;
      ptr->max_buffers = s.second->max_buffers;
      ptr->crop_rotate_scale_info = mojom::CropRotateScaleInfo::New(
          static_cast<mojom::Camera3StreamRotation>(
              s.second->crop_rotate_scale_degrees));
      if (device_api_version_ >= CAMERA_DEVICE_API_VERSION_3_5) {
        if (strlen(s.second->physical_camera_id) == 0) {
          ptr->physical_camera_id = "";
        } else {
          int internal_camera_id = 0;
          if (!base::StringToInt(s.second->physical_camera_id,
                                 &internal_camera_id)) {
            LOGF(ERROR) << "Invalid physical camera ID: "
                        << s.second->physical_camera_id;
            return -EINVAL;
          }
          int public_camera_id =
              get_public_camera_id_callback_.Run(internal_camera_id);
          if (public_camera_id == -1) {
            LOGF(ERROR)
                << "Failed to find public camera ID for internal camera "
                << internal_camera_id;
            return -EINVAL;
          }
          ptr->physical_camera_id = base::NumberToString(public_camera_id);
        }
      }
      (*updated_config)->streams.push_back(std::move(ptr));
    }

    base::RepeatingClosure timeout_callback = base::DoNothing();
    std::unique_ptr<CameraConfig> config =
        CameraConfig::Create(constants::kCrosCameraTestConfigPathString);
    if (config->GetBoolean(constants::kCrosAbortWhenCaptureMonitorTimeout,
                           false)) {
      timeout_callback = base::BindRepeating([]() { abort(); });
    }
    capture_request_monitor_.StartMonitor(timeout_callback);
    capture_result_monitor_.StartMonitor(timeout_callback);
  }

  camera_metrics_->SendConfigureStreamsLatency(timer.Elapsed());

  return result;
}

mojom::CameraMetadataPtr CameraDeviceAdapter::ConstructDefaultRequestSettings(
    mojom::Camera3RequestTemplate type) {
  VLOGF_ENTER();
  size_t type_index = static_cast<size_t>(type);
  if (type_index >= CAMERA3_TEMPLATE_COUNT) {
    LOGF(ERROR) << "Invalid template index given";
    return mojom::CameraMetadata::New();
  }
  android::CameraMetadata& request_template = request_templates_[type_index];
  if (request_template.isEmpty()) {
    int32_t request_type = static_cast<int32_t>(type);
    request_template.acquire(clone_camera_metadata(
        camera_device_->ops->construct_default_request_settings(camera_device_,
                                                                request_type)));
    for (auto it = stream_manipulators_.begin();
         it != stream_manipulators_.end(); ++it) {
      (*it)->ConstructDefaultRequestSettings(&request_template, request_type);
    }
  }
  return internal::SerializeCameraMetadata(request_template.getAndLock());
}

int32_t CameraDeviceAdapter::ProcessCaptureRequest(
    mojom::Camera3CaptureRequestPtr request) {
  VLOGF_ENTER();

  // Complete the pending reprocess request first if exists. We need to
  // prioritize reprocess requests because CCA can be waiting for the
  // reprocessed picture before unblocking UI.
  {
    base::AutoLock lock(process_reprocess_request_callback_lock_);
    if (!process_reprocess_request_callback_.is_null())
      std::move(process_reprocess_request_callback_).Run();
  }
  if (!request) {
    return 0;
  }

  camera3_capture_request_t req;
  req.frame_number = request->frame_number;

  internal::ScopedCameraMetadata settings =
      internal::DeserializeCameraMetadata(request->settings);
  if (settings) {
    capture_settings_ = std::move(settings);
  }

  capture_request_monitor_.Kick();

  // Deserialize input buffer.
  buffer_handle_t input_buffer_handle;
  camera3_stream_buffer_t input_buffer;
  if (!request->input_buffer.is_null()) {
    base::AutoLock streams_lock(streams_lock_);
    base::AutoLock buffer_handles_lock(buffer_handles_lock_);
    if (request->input_buffer->buffer_handle) {
      if (RegisterBufferLocked(
              std::move(request->input_buffer->buffer_handle))) {
        LOGF(ERROR) << "Failed to register input buffer";
        return -EINVAL;
      }
    }
    input_buffer.buffer =
        const_cast<const native_handle_t**>(&input_buffer_handle);
    internal::DeserializeStreamBuffer(request->input_buffer, streams_,
                                      buffer_handles_, &input_buffer);
    req.input_buffer = &input_buffer;
  } else {
    req.input_buffer = nullptr;
  }

  // Deserialize output buffers.
  size_t num_output_buffers = request->output_buffers.size();
  DCHECK_GT(num_output_buffers, 0);

  std::vector<camera3_stream_buffer_t> output_buffers(num_output_buffers);
  {
    base::AutoLock streams_lock(streams_lock_);
    base::AutoLock buffer_handles_lock(buffer_handles_lock_);
    for (size_t i = 0; i < num_output_buffers; ++i) {
      mojom::Camera3StreamBufferPtr& out_buf_ptr = request->output_buffers[i];
      if (out_buf_ptr->buffer_handle) {
        if (RegisterBufferLocked(std::move(out_buf_ptr->buffer_handle))) {
          LOGF(ERROR) << "Failed to register output buffer";
          return -EINVAL;
        }
      }
      internal::DeserializeStreamBuffer(out_buf_ptr, streams_, buffer_handles_,
                                        &output_buffers.at(i));
    }
    req.num_output_buffers = output_buffers.size();
    req.output_buffers =
        const_cast<const camera3_stream_buffer_t*>(output_buffers.data());
  }

  req.settings = capture_settings_.get();

  std::vector<const char*> phys_ids;
  std::vector<std::string> phys_ids_string;
  std::vector<const camera_metadata_t*> phys_settings;
  std::vector<internal::ScopedCameraMetadata> phys_settings_scoped;
  if (device_api_version_ >= CAMERA_DEVICE_API_VERSION_3_5) {
    DCHECK(request->physcam_settings.has_value());
    req.num_physcam_settings = request->physcam_settings.value().size();
    if (req.num_physcam_settings > 0) {
      for (int i = 0; i < req.num_physcam_settings; ++i) {
        int public_camera_id = request->physcam_settings.value()[i]->id;
        int internal_camera_id =
            get_internal_camera_id_callback_.Run(public_camera_id);
        if (internal_camera_id == -1) {
          LOGF(ERROR) << "Failed to find internal camera ID for camera "
                      << public_camera_id;
          return -EINVAL;
        }
        phys_ids_string.push_back(base::NumberToString(internal_camera_id));
        phys_settings_scoped.push_back(internal::DeserializeCameraMetadata(
            request->physcam_settings.value()[i]->metadata));
      }
      for (const auto& id : phys_ids_string) {
        phys_ids.push_back(id.c_str());
      }
      for (const auto& setting : phys_settings_scoped) {
        phys_settings.push_back(setting.get());
      }
      req.physcam_id = phys_ids.data();
      req.physcam_settings = phys_settings.data();
    } else {
      req.physcam_id = nullptr;
      req.physcam_settings = nullptr;
    }
  }

  // Apply reprocessing effects
  if (req.input_buffer && req.num_output_buffers != 0 &&
      has_reprocess_effect_vendor_tag_callback_.Run(*req.settings)) {
    VLOGF(1) << "Applying reprocessing effects on input buffer";
    // Run reprocessing effect asynchronously so that it does not block other
    // requests.  It introduces a risk that buffers of the same stream may be
    // returned out of order.  Since CTS would not go this way and GCA would
    // not mix reprocessing effect captures with normal ones, it should be
    // fine.
    auto req_ptr = std::make_unique<Camera3CaptureDescriptor>(req);
    reprocess_effect_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &CameraDeviceAdapter::ReprocessEffectsOnReprocessEffectThread,
            base::Unretained(this), std::move(req_ptr)));

    if (camera_metadata_inspector_) {
      camera_metadata_inspector_->InspectRequest(&req);
    }
    return 0;
  }

  // TODO(jcliang): We may need to cache the last request settings here. In case
  // where the client sets a null settings we can pass the cached settings to
  // the stream manipulators so that they can still do incremental changes on
  // top of the cached settings.
  Camera3CaptureDescriptor request_descriptor(req);
  for (auto it = stream_manipulators_.begin(); it != stream_manipulators_.end();
       ++it) {
    (*it)->ProcessCaptureRequest(&request_descriptor);
  }
  camera3_capture_request_t* locked_request =
      request_descriptor.LockForRequest();
  if (camera_metadata_inspector_) {
    camera_metadata_inspector_->InspectRequest(locked_request);
  }

  int ret = camera_device_->ops->process_capture_request(camera_device_,
                                                         locked_request);

  return ret;
}

void CameraDeviceAdapter::Dump(mojo::ScopedHandle fd) {
  VLOGF_ENTER();
  base::ScopedFD dump_fd(mojo::UnwrapPlatformHandle(std::move(fd)).TakeFD());
  camera_device_->ops->dump(camera_device_, dump_fd.get());
}

int32_t CameraDeviceAdapter::Flush() {
  VLOGF_ENTER();
  for (auto it = stream_manipulators_.begin(); it != stream_manipulators_.end();
       ++it) {
    (*it)->Flush();
  }
  return camera_device_->ops->flush(camera_device_);
}

int32_t CameraDeviceAdapter::RegisterBuffer(
    uint64_t buffer_id,
    mojom::Camera3DeviceOps::BufferType type,
    std::vector<mojo::ScopedHandle> fds,
    uint32_t drm_format,
    mojom::HalPixelFormat hal_pixel_format,
    uint32_t width,
    uint32_t height,
    const std::vector<uint32_t>& strides,
    const std::vector<uint32_t>& offsets) {
  base::AutoLock l(buffer_handles_lock_);
  return CameraDeviceAdapter::RegisterBufferLocked(
      buffer_id, std::move(fds), drm_format, hal_pixel_format, width, height,
      strides, offsets);
}

int32_t CameraDeviceAdapter::Close() {
  // Close the device.
  VLOGF_ENTER();
  {
    base::AutoLock l(close_lock_);
    if (close_called_) {
      return 0;
    }
    close_called_ = true;
  }
  // Stop the capture monitors before closing the streams in case it takes time
  // and triggers the timeout.
  capture_request_monitor_.Detach();
  capture_result_monitor_.Detach();

  reprocess_effect_thread_.Stop();
  int32_t ret = camera_device_->common.close(&camera_device_->common);
  DCHECK_EQ(ret, 0);
  {
    base::AutoLock l(fence_sync_thread_lock_);
    fence_sync_thread_.Stop();
  }
  FreeAllocatedStreamBuffers();

  close_callback_.Run();
  return ret;
}

int32_t CameraDeviceAdapter::ConfigureStreamsAndGetAllocatedBuffers(
    mojom::Camera3StreamConfigurationPtr config,
    mojom::Camera3StreamConfigurationPtr* updated_config,
    AllocatedBuffers* allocated_buffers) {
  VLOGF_ENTER();
  int32_t result = ConfigureStreams(std::move(config), updated_config);

  // Early return if configure streams failed.
  if (result) {
    return result;
  }

  bool is_success =
      AllocateBuffersForStreams((*updated_config)->streams, allocated_buffers);

  if (!is_success) {
    FreeAllocatedStreamBuffers();
  }

  return result;
}

bool CameraDeviceAdapter::IsRequestOrResultStalling() {
  return !capture_request_monitor_.HasBeenKicked() ||
         !capture_result_monitor_.HasBeenKicked();
}

// static
void CameraDeviceAdapter::ProcessCaptureResult(
    const camera3_callback_ops_t* ops, const camera3_capture_result_t* result) {
  VLOGF_ENTER();
  CameraDeviceAdapter* self = const_cast<CameraDeviceAdapter*>(
      static_cast<const CameraDeviceAdapter*>(ops));

  self->capture_result_monitor_.Kick();

  // Call ProcessCaptureResult in reverse order of that of ProcessCaptureRequest
  // so the stream manipulators can unwind the buffer states.
  Camera3CaptureDescriptor result_descriptor(*result);
  for (auto it = self->stream_manipulators_.rbegin();
       it != self->stream_manipulators_.rend(); ++it) {
    (*it)->ProcessCaptureResult(&result_descriptor);
  }

  ReturnResultToClient(ops, std::move(result_descriptor));
}

// static
void CameraDeviceAdapter::ReturnResultToClient(
    const camera3_callback_ops_t* ops,
    Camera3CaptureDescriptor result_descriptor) {
  VLOGF_ENTER();
  if (!result_descriptor.has_metadata() &&
      result_descriptor.GetInputBuffer() == nullptr &&
      result_descriptor.num_output_buffers() == 0) {
    // Android camera framework doesn't accept empty capture results. Since ZSL
    // would remove the input buffer, output buffers and metadata it added, it's
    // possible that we end up with an empty capture result.
    VLOGFID(1, result_descriptor.frame_number()) << "Drop empty capture result";
    return;
  }

  CameraDeviceAdapter* self = const_cast<CameraDeviceAdapter*>(
      static_cast<const CameraDeviceAdapter*>(ops));
  camera3_stream_buffer_t in_buf = {};
  mojom::Camera3CaptureResultPtr result_ptr;
  {
    base::AutoLock reprocess_handles_lock(self->reprocess_handles_lock_);
    const camera3_stream_buffer_t* input_buffer =
        result_descriptor.GetInputBuffer();
    if (input_buffer && !self->reprocess_handles_.empty() &&
        *input_buffer->buffer ==
            *self->reprocess_handles_.front().GetHandle()) {
      in_buf = *input_buffer;
      // Restore original input buffer
      base::AutoLock buffer_handles_lock(self->buffer_handles_lock_);
      in_buf.buffer =
          &self->buffer_handles_.at(self->input_buffer_handle_ids_.front())
               ->self;
      self->reprocess_handles_.pop_front();
      self->input_buffer_handle_ids_.pop_front();
      result_descriptor.SetInputBuffer(in_buf);
    }
  }
  {
    base::AutoLock reprocess_result_metadata_lock(
        self->reprocess_result_metadata_lock_);
    auto it =
        self->reprocess_result_metadata_.find(result_descriptor.frame_number());
    if (it != self->reprocess_result_metadata_.end() && !it->second.isEmpty() &&
        result_descriptor.has_metadata()) {
      result_descriptor.AppendMetadata(it->second.getAndLock());
      self->reprocess_result_metadata_.erase(result_descriptor.frame_number());
    }
    result_ptr = self->PrepareCaptureResult(result_descriptor.LockForResult());
  }

  base::AutoLock l(self->callback_ops_delegate_lock_);
  if (self->callback_ops_delegate_) {
    if (self->camera_metadata_inspector_) {
      self->camera_metadata_inspector_->InspectResult(
          result_descriptor.GetLockedResult());
    }
    self->callback_ops_delegate_->ProcessCaptureResult(std::move(result_ptr));
  }
}

// static
void CameraDeviceAdapter::Notify(const camera3_callback_ops_t* ops,
                                 const camera3_notify_msg_t* msg) {
  VLOGF_ENTER();

  CameraDeviceAdapter* self = const_cast<CameraDeviceAdapter*>(
      static_cast<const CameraDeviceAdapter*>(ops));

  if (msg->type == CAMERA3_MSG_ERROR) {
    self->camera_metrics_->SendError(msg->message.error.error_code);
    if (msg->message.error.error_code == CAMERA3_MSG_ERROR_DEVICE) {
      LOGF(ERROR) << "Fatal device error; aborting the camera service";
      _exit(EIO);
    }
  }

  camera3_notify_msg_t* mutable_msg = const_cast<camera3_notify_msg_t*>(msg);
  for (auto it = self->stream_manipulators_.begin();
       it != self->stream_manipulators_.end(); ++it) {
    (*it)->Notify(mutable_msg);
  }

  mojom::Camera3NotifyMsgPtr msg_ptr = self->PrepareNotifyMsg(mutable_msg);
  base::AutoLock l(self->callback_ops_delegate_lock_);
  if (self->callback_ops_delegate_) {
    self->callback_ops_delegate_->Notify(std::move(msg_ptr));
  }
}

bool CameraDeviceAdapter::AllocateBuffersForStreams(
    const std::vector<mojom::Camera3StreamPtr>& streams,
    AllocatedBuffers* allocated_buffers) {
  AllocatedBuffers tmp_allocated_buffers;
  auto* camera_buffer_manager = CameraBufferManager::GetInstance();
  for (const auto& stream : streams) {
    std::vector<mojom::Camera3StreamBufferPtr> new_buffers;
    uint32_t stream_format = static_cast<uint32_t>(stream->format);
    uint64_t stream_id = stream->id;
    DCHECK(stream_format == HAL_PIXEL_FORMAT_BLOB ||
           stream_format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED ||
           stream_format == HAL_PIXEL_FORMAT_YCbCr_420_888);
    uint32_t buffer_width;
    uint32_t buffer_height;
    int status;
    if (stream_format == HAL_PIXEL_FORMAT_BLOB) {
      camera_metadata_ro_entry entry;
      status = find_camera_metadata_ro_entry(static_info_,
                                             ANDROID_JPEG_MAX_SIZE, &entry);
      if (status) {
        LOGF(ERROR) << "No Jpeg max size information in metadata.";
        return false;
      }
      buffer_width = entry.data.i32[0];
      buffer_height = 1;
    } else {
      buffer_width = stream->width;
      buffer_height = stream->height;
    }
    for (size_t i = 0; i < stream->max_buffers; i++) {
      mojom::Camera3StreamBufferPtr new_buffer =
          mojom::Camera3StreamBuffer::New();
      new_buffer->stream_id = stream_id;

      mojom::CameraBufferHandlePtr mojo_buffer_handle =
          mojom::CameraBufferHandle::New();

      buffer_handle_t buffer_handle;
      uint32_t buffer_stride;
      status = camera_buffer_manager->Allocate(buffer_width, buffer_height,
                                               stream_format, stream->usage,
                                               &buffer_handle, &buffer_stride);
      if (status) {
        LOGF(ERROR) << "Failed to allocate buffer.";
        return false;
      }

      mojo_buffer_handle->width = buffer_width;
      mojo_buffer_handle->height = buffer_height;
      mojo_buffer_handle->drm_format =
          camera_buffer_manager->ResolveDrmFormat(stream_format, stream->usage);
      CHECK_NE(mojo_buffer_handle->drm_format, 0);

      auto num_planes = CameraBufferManager::GetNumPlanes(buffer_handle);
      mojo_buffer_handle->sizes = std::vector<uint32_t>();
      for (size_t plane = 0; plane < num_planes; plane++) {
        mojo_buffer_handle->fds.push_back(
            mojo::WrapPlatformFile(base::ScopedFD(buffer_handle->data[plane])));
        mojo_buffer_handle->strides.push_back(
            CameraBufferManager::GetPlaneStride(buffer_handle, plane));
        mojo_buffer_handle->offsets.push_back(
            CameraBufferManager::GetPlaneOffset(buffer_handle, plane));
        mojo_buffer_handle->sizes->push_back(
            CameraBufferManager::GetPlaneSize(buffer_handle, plane));
      }

      auto* camera_buffer_handle =
          camera_buffer_handle_t::FromBufferHandle(buffer_handle);
      uint64_t buffer_id = camera_buffer_handle->buffer_id;
      mojo_buffer_handle->buffer_id = buffer_id;
      mojo_buffer_handle->hal_pixel_format = stream->format;

      new_buffer->buffer_id = buffer_id;
      new_buffer->buffer_handle = std::move(mojo_buffer_handle);
      new_buffers.push_back(std::move(new_buffer));

      allocated_stream_buffers_[buffer_id] = std::move(buffer_handle);
    }
    tmp_allocated_buffers.insert(
        std::pair<uint64_t, std::vector<mojom::Camera3StreamBufferPtr>>(
            stream_id, std::move(new_buffers)));
  }
  *allocated_buffers = std::move(tmp_allocated_buffers);
  return true;
}

void CameraDeviceAdapter::FreeAllocatedStreamBuffers() {
  auto camera_buffer_manager = CameraBufferManager::GetInstance();
  if (allocated_stream_buffers_.empty()) {
    return;
  }

  for (auto it : allocated_stream_buffers_) {
    camera_buffer_manager->Free(it.second);
  }
  allocated_stream_buffers_.clear();
}

int32_t CameraDeviceAdapter::RegisterBufferLocked(
    uint64_t buffer_id,
    std::vector<mojo::ScopedHandle> fds,
    uint32_t drm_format,
    mojom::HalPixelFormat hal_pixel_format,
    uint32_t width,
    uint32_t height,
    const std::vector<uint32_t>& strides,
    const std::vector<uint32_t>& offsets) {
  size_t num_planes = fds.size();
  std::unique_ptr<camera_buffer_handle_t> buffer_handle =
      std::make_unique<camera_buffer_handle_t>();
  buffer_handle->base.version = sizeof(buffer_handle->base);
  buffer_handle->base.numFds = kCameraBufferHandleNumFds;
  buffer_handle->base.numInts = kCameraBufferHandleNumInts;

  buffer_handle->magic = kCameraBufferMagic;
  buffer_handle->buffer_id = buffer_id;
  buffer_handle->drm_format = drm_format;
  buffer_handle->hal_pixel_format = static_cast<uint32_t>(hal_pixel_format);
  buffer_handle->width = width;
  buffer_handle->height = height;
  for (size_t i = 0; i < num_planes; ++i) {
    buffer_handle->fds[i] =
        mojo::UnwrapPlatformHandle(std::move(fds[i])).ReleaseFD();
    buffer_handle->strides[i] = strides[i];
    buffer_handle->offsets[i] = offsets[i];
  }

  if (!CameraBufferManager::GetInstance()->IsValidBuffer(buffer_handle->self)) {
    LOGF(ERROR) << "Invalid buffer handle";
    return -EINVAL;
  }

  buffer_handles_[buffer_id] = std::move(buffer_handle);

  VLOGF(1) << std::hex << "Buffer 0x" << buffer_id << " registered: "
           << "format: " << FormatToString(drm_format)
           << " dimension: " << std::dec << width << "x" << height
           << " num_planes: " << num_planes;
  return 0;
}

int32_t CameraDeviceAdapter::RegisterBufferLocked(
    mojom::CameraBufferHandlePtr buffer) {
  return RegisterBufferLocked(buffer->buffer_id, std::move(buffer->fds),
                              buffer->drm_format, buffer->hal_pixel_format,
                              buffer->width, buffer->height, buffer->strides,
                              buffer->offsets);
}

mojom::Camera3CaptureResultPtr CameraDeviceAdapter::PrepareCaptureResult(
    const camera3_capture_result_t* result) {
  mojom::Camera3CaptureResultPtr r = mojom::Camera3CaptureResult::New();

  r->frame_number = result->frame_number;
  r->result = internal::SerializeCameraMetadata(result->result);
  r->partial_result = result->partial_result;

  // Serialize output buffers.  This may be none as num_output_buffers may be 0.
  if (result->output_buffers) {
    base::AutoLock streams_lock(streams_lock_);
    base::AutoLock buffer_handles_lock(buffer_handles_lock_);
    std::vector<mojom::Camera3StreamBufferPtr> output_buffers;
    for (size_t i = 0; i < result->num_output_buffers; i++) {
      mojom::Camera3StreamBufferPtr out_buf = internal::SerializeStreamBuffer(
          result->output_buffers + i, streams_, buffer_handles_);
      if (out_buf.is_null()) {
        LOGF(ERROR) << "Failed to serialize output stream buffer";
        // TODO(jcliang): Handle error?
      }
      buffer_handles_[out_buf->buffer_id]->state = kReturned;
      RemoveBufferLocked(*(result->output_buffers + i));
      output_buffers.push_back(std::move(out_buf));
    }
    if (output_buffers.size() > 0) {
      r->output_buffers = std::move(output_buffers);
    }
  }

  // Serialize input buffer.
  if (result->input_buffer) {
    base::AutoLock streams_lock(streams_lock_);
    base::AutoLock buffer_handles_lock(buffer_handles_lock_);
    mojom::Camera3StreamBufferPtr input_buffer =
        internal::SerializeStreamBuffer(result->input_buffer, streams_,
                                        buffer_handles_);
    if (input_buffer.is_null()) {
      LOGF(ERROR) << "Failed to serialize input stream buffer";
    }
    buffer_handles_[input_buffer->buffer_id]->state = kReturned;
    RemoveBufferLocked(*result->input_buffer);
    r->input_buffer = std::move(input_buffer);
  }

  if (device_api_version_ >= CAMERA_DEVICE_API_VERSION_3_5) {
    // TODO(lnishan): Handle the errors here.
    std::vector<mojom::Camera3PhyscamMetadataPtr> phys_metadata;
    for (int i = 0; i < result->num_physcam_metadata; ++i) {
      phys_metadata[i] = mojom::Camera3PhyscamMetadata::New();
      int internal_camera_id = 0;
      if (!base::StringToInt(result->physcam_ids[i], &internal_camera_id)) {
        LOGF(ERROR) << "Invalid physical camera ID: " << result->physcam_ids[i];
      }
      int public_camera_id =
          get_public_camera_id_callback_.Run(internal_camera_id);
      if (public_camera_id == -1) {
        LOGF(ERROR) << "Failed to find public camera ID for internal camera "
                    << internal_camera_id;
      }
      phys_metadata[i]->id = public_camera_id;
      phys_metadata[i]->metadata =
          internal::SerializeCameraMetadata(result->physcam_metadata[i]);
    }
    r->physcam_metadata = std::move(phys_metadata);
  }

  return r;
}

mojom::Camera3NotifyMsgPtr CameraDeviceAdapter::PrepareNotifyMsg(
    const camera3_notify_msg_t* msg) {
  // Fill in the data from msg...
  mojom::Camera3NotifyMsgPtr m = mojom::Camera3NotifyMsg::New();
  m->type = static_cast<mojom::Camera3MsgType>(msg->type);
  m->message = mojom::Camera3NotifyMsgMessage::New();

  if (msg->type == CAMERA3_MSG_ERROR) {
    mojom::Camera3ErrorMsgPtr error = mojom::Camera3ErrorMsg::New();
    error->frame_number = msg->message.error.frame_number;
    uint64_t stream_id = 0;
    {
      base::AutoLock l(streams_lock_);
      for (const auto& s : streams_) {
        if (s.second.get() == msg->message.error.error_stream) {
          stream_id = s.first;
          break;
        }
      }
    }
    error->error_stream_id = stream_id;
    error->error_code =
        static_cast<mojom::Camera3ErrorMsgCode>(msg->message.error.error_code);
    m->message->set_error(std::move(error));
  } else if (msg->type == CAMERA3_MSG_SHUTTER) {
    mojom::Camera3ShutterMsgPtr shutter = mojom::Camera3ShutterMsg::New();
    shutter->frame_number = msg->message.shutter.frame_number;
    shutter->timestamp = msg->message.shutter.timestamp;
    m->message->set_shutter(std::move(shutter));
  } else {
    LOGF(ERROR) << "Invalid notify message type: " << msg->type;
  }

  return m;
}

void CameraDeviceAdapter::RemoveBufferLocked(
    const camera3_stream_buffer_t& buffer) {
  buffer_handles_lock_.AssertAcquired();
  int release_fence = buffer.release_fence;
  base::ScopedFD scoped_release_fence;
  if (release_fence != -1) {
    release_fence = dup(release_fence);
    if (release_fence == -1) {
      PLOGF(ERROR) << "Failed to dup release_fence";
      return;
    }
    scoped_release_fence.reset(release_fence);
  }

  // Remove the allocated camera buffer handle from |buffer_handles_| and
  // pass it to RemoveBufferOnFenceSyncThread. The buffer handle will be
  // freed after the release fence is signalled.
  const camera_buffer_handle_t* handle =
      camera_buffer_handle_t::FromBufferHandle(*(buffer.buffer));
  if (!handle) {
    return;
  }
  // Remove the buffer handle from |buffer_handles_| now to avoid a race
  // condition where the process_capture_request sends down an existing buffer
  // handle which hasn't been removed in RemoveBufferHandleOnFenceSyncThread.
  uint64_t buffer_id = handle->buffer_id;
  if (buffer_handles_[buffer_id]->state == kRegistered) {
    // Framework registered a new buffer with the same |buffer_id| before we
    // remove the old buffer handle from |buffer_handles_|.
    return;
  }
  std::unique_ptr<camera_buffer_handle_t> buffer_handle;
  buffer_handles_[buffer_id].swap(buffer_handle);
  buffer_handles_.erase(buffer_id);

  {
    base::AutoLock l(fence_sync_thread_lock_);
    if (!fence_sync_thread_.IsRunning()) {
      return;
    }
    fence_sync_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&CameraDeviceAdapter::RemoveBufferOnFenceSyncThread,
                       base::Unretained(this), std::move(scoped_release_fence),
                       std::move(buffer_handle)));
  }
}

void CameraDeviceAdapter::RemoveBufferOnFenceSyncThread(
    base::ScopedFD release_fence,
    std::unique_ptr<camera_buffer_handle_t> buffer) {
  // In theory the release fence should be signaled by HAL as soon as possible,
  // and we could just set a large value for the timeout.  The timeout here is
  // set to 3 ms to allow testing multiple fences in round-robin if there are
  // multiple active buffers.
  const int kSyncWaitTimeoutMs = 3;
  const camera_buffer_handle_t* handle = buffer.get();
  DCHECK(handle);

  if (!release_fence.is_valid() ||
      !sync_wait(release_fence.get(), kSyncWaitTimeoutMs)) {
    VLOGF(1) << "Buffer 0x" << std::hex << handle->buffer_id << " removed";
  } else {
    // sync_wait() timeout. Reschedule and try to remove the buffer again.
    VLOGF(2) << "Release fence sync_wait() timeout on buffer 0x" << std::hex
             << handle->buffer_id;
    fence_sync_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&CameraDeviceAdapter::RemoveBufferOnFenceSyncThread,
                       base::Unretained(this), std::move(release_fence),
                       std::move(buffer)));
  }
}

void CameraDeviceAdapter::ReprocessEffectsOnReprocessEffectThread(
    std::unique_ptr<Camera3CaptureDescriptor> desc) {
  VLOGF_ENTER();
  DCHECK(desc->GetInputBuffer());
  DCHECK_GT(desc->num_output_buffers(), 0);
  const camera3_stream_t* input_stream = desc->GetInputBuffer()->stream;
  const camera3_stream_t* output_stream = desc->GetOutputBuffers()[0].stream;
  // Here we assume reprocessing effects can provide only one output of the
  // same size and format as that of input. Invoke HAL reprocessing if more
  // outputs, scaling and/or format conversion are required since ISP
  // may provide hardware acceleration for these operations.
  bool need_hal_reprocessing =
      (desc->num_output_buffers() != 1) ||
      (input_stream->width != output_stream->width) ||
      (input_stream->height != output_stream->height) ||
      (input_stream->format != output_stream->format);

  struct ReprocessContext {
    ReprocessContext(CameraDeviceAdapter* d,
                     const camera3_capture_request_t* r,
                     bool n)
        : result(0),
          device_adapter(d),
          capture_request(r),
          need_hal_reprocessing(n) {}

    ~ReprocessContext() {
      if (result != 0) {
        camera3_notify_msg_t msg{
            .type = CAMERA3_MSG_ERROR,
            .message.error.frame_number = capture_request->frame_number,
            .message.error.error_code = CAMERA3_MSG_ERROR_REQUEST};
        device_adapter->Notify(device_adapter, &msg);
      }
      if (result != 0 || !need_hal_reprocessing) {
        camera3_capture_result_t capture_result{
            .frame_number = capture_request->frame_number,
            .result = capture_request->settings,
            .num_output_buffers = capture_request->num_output_buffers,
            .output_buffers = capture_request->output_buffers,
            .input_buffer = capture_request->input_buffer};
        device_adapter->ProcessCaptureResult(device_adapter, &capture_result);
      }
    }

    int32_t result;
    CameraDeviceAdapter* device_adapter;
    const camera3_capture_request_t* capture_request;
    bool need_hal_reprocessing;
  };
  const camera3_capture_request_t* req = desc->LockForRequest();
  ReprocessContext reprocess_context(this, req, need_hal_reprocessing);
  ScopedYUVBufferHandle scoped_output_handle =
      need_hal_reprocessing ?
                            // Allocate reprocessing buffer
          ScopedYUVBufferHandle::AllocateScopedYUVHandle(
              input_stream->width, input_stream->height,
              GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN)
                            :
                            // Wrap the output buffer
          ScopedYUVBufferHandle::CreateScopedYUVHandle(
              *req->output_buffers[0].buffer, output_stream->width,
              output_stream->height,
              GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN);
  if (!scoped_output_handle) {
    LOGF(ERROR) << "Failed to create scoped output buffer";
    reprocess_context.result = -EINVAL;
    return;
  }
  ScopedYUVBufferHandle scoped_input_handle =
      ScopedYUVBufferHandle::CreateScopedYUVHandle(
          *req->input_buffer->buffer, input_stream->width, input_stream->height,
          GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN);
  if (!scoped_input_handle) {
    LOGF(ERROR) << "Failed to create scoped input buffer";
    reprocess_context.result = -EINVAL;
    return;
  }
  android::CameraMetadata reprocess_result_metadata;
  reprocess_context.result = reprocess_effect_callback_.Run(
      *req->settings, &scoped_input_handle, input_stream->width,
      input_stream->height, &reprocess_result_metadata, &scoped_output_handle);
  if (reprocess_context.result != 0) {
    LOGF(ERROR) << "Failed to apply reprocess effect";
    return;
  }
  if (need_hal_reprocessing) {
    // Replace the input buffer with reprocessing output buffer
    {
      base::AutoLock reprocess_handles_lock(reprocess_handles_lock_);
      reprocess_handles_.push_back(std::move(scoped_output_handle));
      input_buffer_handle_ids_.push_back(
          reinterpret_cast<const camera_buffer_handle_t*>(
              *req->input_buffer->buffer)
              ->buffer_id);
      req->input_buffer->buffer = reprocess_handles_.back().GetHandle();
    }
    {
      base::AutoLock reprocess_result_metadata_lock(
          reprocess_result_metadata_lock_);
      reprocess_result_metadata_.emplace(req->frame_number,
                                         reprocess_result_metadata);
    }
    // Store the HAL reprocessing request and wait for CameraDeviceOpsThread to
    // complete it. Also post a null capture request to guarantee it will be
    // called when there's no existing capture requests.
    desc->Unlock();
    auto future = cros::Future<int32_t>::Create(nullptr);
    {
      base::AutoLock lock(process_reprocess_request_callback_lock_);
      DCHECK(process_reprocess_request_callback_.is_null());
      process_reprocess_request_callback_ = base::BindOnce(
          &CameraDeviceAdapter::ProcessReprocessRequestOnDeviceOpsThread,
          base::Unretained(this), std::move(desc),
          cros::GetFutureCallback(future));
    }
    camera_device_ops_thread_.task_runner()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](CameraDeviceAdapter* adapter) {
                         // Ignore returned value.
                         adapter->ProcessCaptureRequest(nullptr);
                       },
                       base::Unretained(this)));
    reprocess_context.result = future->Get();
    if (reprocess_context.result != 0) {
      LOGF(ERROR) << "Failed to process capture request after reprocessing";
    }
  }
}

void CameraDeviceAdapter::ProcessReprocessRequestOnDeviceOpsThread(
    std::unique_ptr<Camera3CaptureDescriptor> desc,
    base::Callback<void(int32_t)> callback) {
  VLOGF_ENTER();
  callback.Run(camera_device_->ops->process_capture_request(
      camera_device_, desc->LockForRequest()));
}

void CameraDeviceAdapter::ResetDeviceOpsDelegateOnThread() {
  DCHECK(camera_device_ops_thread_.task_runner()->BelongsToCurrentThread());
  device_ops_delegate_.reset();
}

void CameraDeviceAdapter::ResetCallbackOpsDelegateOnThread() {
  DCHECK(camera_callback_ops_thread_.task_runner()->BelongsToCurrentThread());
  base::AutoLock l(callback_ops_delegate_lock_);
  callback_ops_delegate_.reset();
}

}  // namespace cros
