/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_CAMERA_DEVICE_ADAPTER_H_
#define CAMERA_HAL_ADAPTER_CAMERA_DEVICE_ADAPTER_H_

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <hardware/camera3.h>

#include <base/callback_helpers.h>
#include <base/containers/flat_map.h>
#include <base/files/scoped_file.h>
#include <base/synchronization/lock.h>
#include <base/threading/thread.h>
#include <base/timer/timer.h>
#include <camera/camera_metadata.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <system/camera_metadata.h>

#include "camera/mojo/camera3.mojom.h"
#include "common/camera_hal3_helpers.h"
#include "common/stream_manipulator.h"
#include "common/utils/common_types.h"
#include "common/utils/cros_camera_mojo_utils.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metrics.h"
#include "hal_adapter/camera_metadata_inspector.h"
#include "hal_adapter/scoped_yuv_buffer_handle.h"

namespace cros {

class Camera3DeviceOpsDelegate;

class Camera3CallbackOpsDelegate;

// It is a watchdog-like monitor. It detects the kick event. If there is no
// kick event between 2 timeout it outputs log to indicate it. We can use it to
// detect if there is any continuous event stopped. e.g. capture request.
class CameraMonitor {
 public:
  enum class MonitorType {
    kRequestsMonitor,
    kResultsMonitor,
  };

  CameraMonitor();
  CameraMonitor(const CameraMonitor&) = delete;
  CameraMonitor& operator=(const CameraMonitor&) = delete;

  ~CameraMonitor();

  void StartMonitor(MonitorType type,
                    base::OnceClosure timeout_callback = base::NullCallback());
  void StopMonitor(MonitorType type);
  void Kick(MonitorType type);
  bool HasBeenKicked(MonitorType type);

 private:
  // Per-type monitor state.
  struct State {
    // A repeating oneshot timer to periodically check whether |is_kicked| is
    // set. |timer| stops if the monitor is not kicked before timeout and will
    // be resumed when kicked again.
    std::unique_ptr<base::RetainingOneShotTimer> timer;

    // Set when we stop monitoring for the given type. |timer| will not resume
    // when |is_stopped| is set.
    bool is_stopped = true;

    bool is_kicked = false;
    base::OnceClosure timeout_callback = base::NullCallback();

    void ResetTimer() {
      is_kicked = false;
      if (timer) {
        timer->Reset();
      }
    }
  };

  void StartMonitorOnThread(MonitorType type,
                            base::OnceClosure timeout_callback);
  void StopMonitorOnThread(MonitorType type);
  void KickOnThread(MonitorType type);
  void MonitorTimeoutOnThread(MonitorType type);

  // A thread that handles timeouts of request/response monitors.
  base::Thread thread_;
  // Access to |monitor_states_| is sequenced on |thread_|.
  base::flat_map<MonitorType, State> monitor_states_;
};

class CameraDeviceAdapter : public camera3_callback_ops_t {
 public:
  CameraDeviceAdapter(
      camera3_device_t* camera_device,
      uint32_t device_api_version,
      const camera_metadata_t* static_info,
      base::RepeatingCallback<int(int)> get_internal_camera_id_callback,
      base::RepeatingCallback<int(int)> get_public_camera_id_callback,
      base::OnceCallback<void()> close_callback,
      std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators);

  CameraDeviceAdapter(const CameraDeviceAdapter&) = delete;
  CameraDeviceAdapter& operator=(const CameraDeviceAdapter&) = delete;

  ~CameraDeviceAdapter();

  using HasReprocessEffectVendorTagCallback =
      base::RepeatingCallback<bool(const camera_metadata_t&)>;
  using ReprocessEffectCallback =
      base::RepeatingCallback<int32_t(const camera_metadata_t&,
                                      ScopedYUVBufferHandle*,
                                      uint32_t,
                                      uint32_t,
                                      android::CameraMetadata*,
                                      ScopedYUVBufferHandle*)>;
  using AllocatedBuffers =
      base::flat_map<uint64_t, std::vector<mojom::Camera3StreamBufferPtr>>;
  // Starts the camera device adapter.  This method must be called before all
  // the other methods are called.
  bool Start(HasReprocessEffectVendorTagCallback
                 has_reprocess_effect_vendor_tag_callback,
             ReprocessEffectCallback reprocess_effect_callback);

  // Bind() is called by CameraHalAdapter in OpenDevice() on the mojo IPC
  // handler thread in |module_delegate_|.
  void Bind(mojo::PendingReceiver<mojom::Camera3DeviceOps> device_ops_receiver);

  // Callback interface for Camera3DeviceOpsDelegate.
  // These methods are callbacks for |device_ops_delegate_| and are executed on
  // the mojo IPC handler thread in |device_ops_delegate_|.

  int32_t Initialize(
      mojo::PendingRemote<mojom::Camera3CallbackOps> callback_ops);

  int32_t ConfigureStreams(
      mojom::Camera3StreamConfigurationPtr config,
      mojom::Camera3StreamConfigurationPtr* updated_config);

  mojom::CameraMetadataPtr ConstructDefaultRequestSettings(
      mojom::Camera3RequestTemplate type);

  int32_t ProcessCaptureRequest(mojom::Camera3CaptureRequestPtr request);

  void Dump(mojo::ScopedHandle fd);

  int32_t Flush();

  int32_t RegisterBuffer(uint64_t buffer_id,
                         mojom::Camera3DeviceOps::BufferType type,
                         std::vector<mojo::ScopedHandle> fds,
                         uint32_t drm_format,
                         mojom::HalPixelFormat hal_pixel_format,
                         uint32_t width,
                         uint32_t height,
                         const std::vector<uint32_t>& strides,
                         const std::vector<uint32_t>& offsets);

  int32_t Close();

  int32_t ConfigureStreamsAndGetAllocatedBuffers(
      mojom::Camera3StreamConfigurationPtr config,
      mojom::Camera3StreamConfigurationPtr* updated_config,
      AllocatedBuffers* allocated_buffers);

  bool IsRequestOrResultStalling();

 private:
  // Implementation of camera3_callback_ops_t.
  static void ProcessCaptureResult(const camera3_callback_ops_t* ops,
                                   const camera3_capture_result_t* result);
  static void ReturnResultToClient(const camera3_callback_ops_t* ops,
                                   Camera3CaptureDescriptor result);

  static void Notify(const camera3_callback_ops_t* ops,
                     const camera3_notify_msg_t* msg);

  // Allocates buffers for given |streams|. Returns true and the allocated
  // buffers will be put in |allocated_buffers| if the allocation succeeds.
  // Otherwise, false is returned.
  bool AllocateBuffersForStreams(
      const std::vector<mojom::Camera3StreamPtr>& streams,
      AllocatedBuffers* allocated_buffers);

  // Frees all allocated stream buffers that are allocated locally.
  void FreeAllocatedStreamBuffers();

  int32_t RegisterBufferLocked(uint64_t buffer_id,
                               std::vector<mojo::ScopedHandle> fds,
                               uint32_t drm_format,
                               mojom::HalPixelFormat hal_pixel_format,
                               uint32_t width,
                               uint32_t height,
                               const std::vector<uint32_t>& strides,
                               const std::vector<uint32_t>& offsets);
  int32_t RegisterBufferLocked(mojom::CameraBufferHandlePtr buffer);

  // NOTE: All the fds in |result| (e.g. fences and buffer handles) will be
  // closed after the function returns.  The caller needs to dup a fd in
  // |result| if the fd will be accessed after calling ProcessCaptureResult.
  mojom::Camera3CaptureResultPtr PrepareCaptureResult(
      const camera3_capture_result_t* result);

  mojom::Camera3NotifyMsgPtr PrepareNotifyMsg(const camera3_notify_msg_t* msg);

  // Caller must hold |buffer_handles_lock_|.
  void RemoveBufferLocked(const camera3_stream_buffer_t& buffer);

  // Waits until |release_fence| is signaled and then deletes |buffer|.
  void RemoveBufferOnFenceSyncThread(
      base::ScopedFD release_fence,
      std::unique_ptr<camera_buffer_handle_t> buffer);

  void ReprocessEffectsOnReprocessEffectThread(
      std::unique_ptr<Camera3CaptureDescriptor> req);

  void ProcessReprocessRequestOnDeviceOpsThread(
      std::unique_ptr<Camera3CaptureDescriptor> req,
      base::OnceCallback<void(int32_t)> callback);

  void ResetDeviceOpsDelegateOnThread();
  void ResetCallbackOpsDelegateOnThread();

  // The thread that all the camera3 device ops operate on.
  base::Thread camera_device_ops_thread_;

  // The thread that all the Mojo communications of camera3 callback ops operate
  // on.
  base::Thread camera_callback_ops_thread_;

  // A thread to asynchronously wait for release fences and destroy
  // corresponding buffer handles.  |fence_sync_thread_lock_| is used to
  // synchronize thread start/stop/status checking on different threads.
  base::Lock fence_sync_thread_lock_;
  base::Thread fence_sync_thread_;

  // A thread to apply reprocessing effects
  base::Thread reprocess_effect_thread_;

  // The delegate that handles the Camera3DeviceOps mojo IPC.
  std::unique_ptr<Camera3DeviceOpsDelegate> device_ops_delegate_;

  // The delegate that handles the Camera3CallbackOps mojo IPC.
  std::unique_ptr<Camera3CallbackOpsDelegate> callback_ops_delegate_;
  // Lock to protect |callback_ops_delegate_| as it is accessed on multiple
  // threads.
  base::Lock callback_ops_delegate_lock_;

  // A callback to get the internal camera ID given its public camera ID.
  base::RepeatingCallback<int(int)> get_internal_camera_id_callback_;

  // A callback to get the public camera ID given its internal camera ID.
  base::RepeatingCallback<int(int)> get_public_camera_id_callback_;

  // The callback to run when the device is closed.
  base::OnceCallback<void()> close_callback_;

  // Since Close() can be called in |camera_device_ops_thread_| or in
  // main thread, use lock to protect |close_called_|.
  base::Lock close_lock_;

  // Set when Close() is called. No more calls to the device APIs may be
  // made once |close_called_| is set.
  bool close_called_ GUARDED_BY(close_lock_);

  // The real camera device.
  camera3_device_t* camera_device_;

  // The API version of the camera device (e.g., CAMERA_DEVICE_API_VERSION_3_5).
  uint32_t device_api_version_;

  // The non-owning read-only view of the static camera characteristics of this
  // device.
  const camera_metadata_t* static_info_;

  // Stores the request template for a given request type. The local reference
  // is needed here because we need to modify the templates from HAL if ZSL is
  // supported.
  std::array<android::CameraMetadata, CAMERA3_TEMPLATE_COUNT>
      request_templates_;

  // A mapping from Andoird HAL for all the configured streams.
  internal::ScopedStreams streams_;

  // A mutex to guard |streams_|.
  base::Lock streams_lock_;

  // A mapping from the locally created buffer handle to the handle ID of the
  // imported buffer.  We need to return the correct handle ID in
  // ProcessCaptureResult so the camera client, which allocated the imported
  // buffer, can restore the buffer handle in the capture result before passing
  // up to the upper layer.
  std::unordered_map<uint64_t, std::unique_ptr<camera_buffer_handle_t>>
      buffer_handles_;

  // A mapping that stores all buffer handles that are allocated when streams
  // are configured locally. When the session is over, all of these handles
  // should be freed.
  std::map<uint64_t, buffer_handle_t> allocated_stream_buffers_;

  // A queue of reprocessing buffers.
  std::deque<ScopedYUVBufferHandle> reprocess_handles_;

  // A queue of original input buffer handles replaced by reprocessing ones.
  std::deque<uint64_t> input_buffer_handle_ids_;

  // A mapping from the frame number to the result metadata generated by
  // reprocessing effects
  std::unordered_map<uint32_t, android::CameraMetadata>
      reprocess_result_metadata_;

  // A pending reprocessing request task for HAL to run.
  base::OnceClosure process_reprocess_request_callback_;
  base::Lock process_reprocess_request_callback_lock_;

  // A mutex to guard |buffer_handles_|.
  base::Lock buffer_handles_lock_;

  // A mutex to guard |reprocess_handles_| and |input_buffer_handle_ids_|.
  base::Lock reprocess_handles_lock_;

  // A mutex to guard |reprocess_result_metadata_|.
  base::Lock reprocess_result_metadata_lock_;

  // The callback to check reprocessing effect vendor tags.
  HasReprocessEffectVendorTagCallback has_reprocess_effect_vendor_tag_callback_;

  // The callback to handle reprocessing effect.
  ReprocessEffectCallback reprocess_effect_callback_;

  // The metadata inspector to dump capture requests / results in realtime
  // for debugging if enabled.
  std::unique_ptr<CameraMetadataInspector> camera_metadata_inspector_;

  // Metrics for camera service.
  std::unique_ptr<CameraMetrics> camera_metrics_;

  // ANDROID_PARTIAL_RESULT_COUNT from static metadata.
  int32_t partial_result_count_;

  // Cached request settings that always track the latest set of request
  // metadata.
  internal::ScopedCameraMetadata capture_settings_;

  // Monitors capture requests and capture results. If there is no capture
  // requests/responses for a while the monitors will output a log to indicate
  // this situation.
  CameraMonitor capture_monitor_;

  std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators_;
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA_DEVICE_ADAPTER_H_
