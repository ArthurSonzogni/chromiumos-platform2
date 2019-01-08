// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA3_TEST_CAMERA3_DEVICE_FIXTURE_H_
#define CAMERA3_TEST_CAMERA3_DEVICE_FIXTURE_H_

#include <camera/camera_metadata.h>
#include <gtest/gtest.h>
#include <hardware/camera3.h>
#include <hardware/hardware.h>

#include "camera3_module_fixture.h"
#include "camera3_test_gralloc.h"
#include "common/camera_buffer_handle.h"

namespace camera3_test {

struct CameraMetadataDeleter {
  inline void operator()(camera_metadata_t* metadata) {
    if (metadata) {
      free_camera_metadata(metadata);
    }
  }
};

// Forward declaration
class Camera3DeviceImpl;

typedef std::unique_ptr<camera_metadata_t, struct CameraMetadataDeleter>
    CameraMetadataUniquePtr;

template <typename T>
int UpdateMetadata(uint32_t tag,
                   const T* data,
                   size_t data_count,
                   CameraMetadataUniquePtr* metadata_unique_ptr) {
  android::CameraMetadata metadata(metadata_unique_ptr->release());
  int result = metadata.update(tag, data, data_count);
  metadata_unique_ptr->reset(metadata.release());
  return result;
}

class Camera3Device {
 public:
  explicit Camera3Device(int cam_id);

  ~Camera3Device();

  // Initialize
  int Initialize(Camera3Module* cam_module);

  // Destroy
  void Destroy();

  typedef base::Callback<void(const camera3_capture_result* result)>
      ProcessCaptureResultCallback;

  typedef base::Callback<void(const camera3_notify_msg* msg)> NotifyCallback;

  typedef base::Callback<void(uint32_t frame_number,
                              CameraMetadataUniquePtr metadata,
                              std::vector<BufferHandleUniquePtr> buffers)>
      ProcessResultMetadataOutputBuffersCallback;

  typedef base::Callback<void(
      std::vector<CameraMetadataUniquePtr>* partial_metadata)>
      ProcessPartialMetadataCallback;

  // Register callback function to process capture result
  void RegisterProcessCaptureResultCallback(ProcessCaptureResultCallback cb);

  // Register callback function for notification
  void RegisterNotifyCallback(NotifyCallback cb);

  // Register callback function to process result metadata and output buffers
  void RegisterResultMetadataOutputBufferCallback(
      ProcessResultMetadataOutputBuffersCallback cb);

  // Register callback function to process partial metadata
  void RegisterPartialMetadataCallback(ProcessPartialMetadataCallback cb);

  // Whether or not the template is supported
  bool IsTemplateSupported(int32_t type);

  // Construct default request settings
  const camera_metadata_t* ConstructDefaultRequestSettings(int type);

  // Add output stream in preparation for stream configuration
  void AddOutputStream(int format, int width, int height);

  // Configure streams and return configured streams if |streams| is not null
  int ConfigureStreams(std::vector<const camera3_stream_t*>* streams);

  // Allocate output buffers for all configured streams and return them
  // in the stream buffer format, which has the buffer associated to the
  // corresponding stream. The allocated buffers are owned by Camera3Device.
  int AllocateOutputStreamBuffers(
      std::vector<camera3_stream_buffer_t>* output_buffers);

  // Allocate output buffers for given streams |streams| and return them
  // in the stream buffer format, which has the buffer associated to the
  // corresponding stream. The allocated buffers are owned by Camera3Device.
  int AllocateOutputBuffersByStreams(
      const std::vector<const camera3_stream_t*>& streams,
      std::vector<camera3_stream_buffer_t>* output_buffers);

  // Register buffer |unique_buffer| that is associated with the given stream
  // |stream|. Camera3Device takes buffer ownership.
  int RegisterOutputBuffer(const camera3_stream_t& stream,
                           BufferHandleUniquePtr unique_buffer);

  // Process given capture request |capture_request|. The frame number field of
  // |capture_request| will be overwritten if this method returns 0 on success.
  int ProcessCaptureRequest(camera3_capture_request_t* capture_request);

  // Wait for shutter with timeout. |abs_timeout| specifies an absolute timeout
  // in seconds and nanoseconds since the Epoch, 1970-01-01 00:00:00 +0000
  // (UTC), that the call should block if the shutter is immediately available.
  int WaitShutter(const struct timespec& abs_timeout);

  // Wait for capture result with timeout. |abs_timeout| specifies an absolute
  // timeout in seconds and nanoseconds since the Epoch, 1970-01-01 00:00:00
  // +0000 (UTC), that the call should block if the shutter is immediately
  // available.
  int WaitCaptureResult(const struct timespec& abs_timeout);

  // Flush all currently in-process captures and all buffers in the pipeline
  int Flush();

  // Get static information
  class StaticInfo;
  const StaticInfo* GetStaticInfo() const;

 private:
  std::unique_ptr<Camera3DeviceImpl> impl_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(Camera3Device);
};

class Camera3Device::StaticInfo {
 public:
  explicit StaticInfo(const camera_info& cam_info);

  // Determine whether or not all the keys are available
  bool IsKeyAvailable(uint32_t tag) const;
  bool AreKeysAvailable(std::vector<uint32_t> tags) const;

  // Whether or not the hardware level reported is at least full
  bool IsHardwareLevelAtLeastFull() const;

  // Whether or not the hardware level reported is at least limited
  bool IsHardwareLevelAtLeastLimited() const;

  // Determine whether the current device supports a capability or not
  bool IsCapabilitySupported(int capability) const;

  // Check if depth output is supported, based on the depth capability
  bool IsDepthOutputSupported() const;

  // Check if standard outputs (PRIVATE, YUV, JPEG) outputs are supported,
  // based on the backwards-compatible capability
  bool IsColorOutputSupported() const;

  // Get available edge modes
  std::set<uint8_t> GetAvailableEdgeModes() const;

  // Get available noise reduction modes
  std::set<uint8_t> GetAvailableNoiseReductionModes() const;

  // Get available noise reduction modes
  std::set<uint8_t> GetAvailableColorAberrationModes() const;

  // Get available tone map modes
  std::set<uint8_t> GetAvailableToneMapModes() const;

  // Get available formats for a given direction
  // direction: ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT or
  //            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT
  std::set<int32_t> GetAvailableFormats(int32_t direction) const;

  // Check if a stream format is supported
  bool IsFormatAvailable(int format) const;

  // Get the image output resolutions in this stream configuration
  std::vector<ResolutionInfo> GetSortedOutputResolutions(int32_t format) const;

  // Determine if camera device support AE lock control
  bool IsAELockSupported() const;

  // Determine if camera device support AWB lock control
  bool IsAWBLockSupported() const;

  // Get the maximum number of partial result a request can expect
  // Returns: maximum number of partial results; it is 1 by default.
  int32_t GetPartialResultCount() const;

  // Get the number of maximum pipeline stages a frame has to go through from
  // when it's exposed to when it's available to the framework
  // Returns: number of maximum pipeline stages on success; corresponding error
  // code on failure.
  int32_t GetRequestPipelineMaxDepth() const;

  // Get the maxium size of JPEG image
  // Returns: maximum size of JPEG image on success; corresponding error code
  // on failure.
  int32_t GetJpegMaxSize() const;

  // Get the sensor orientation
  // Returns: degrees on success; corresponding error code on failure.
  int32_t GetSensorOrientation() const;

  // Get available thumbnail sizes
  // Returns: 0 on success; corresponding error code on failure.
  int32_t GetAvailableThumbnailSizes(
      std::vector<ResolutionInfo>* resolutions) const;

  // Get available focal lengths
  // Returns: 0 on success; corresponding error code on failure.
  int32_t GetAvailableFocalLengths(std::vector<float>* focal_lengths) const;

  // Get available apertures
  // Returns: 0 on success; corresponding error code on failure.
  int32_t GetAvailableApertures(std::vector<float>* apertures) const;

  // Get available AF modes
  // Returns: 0 on success; corresponding error code on failure.
  int32_t GetAvailableAFModes(std::vector<int32_t>* af_modes) const;

 private:
  // Return the supported hardware level of the device, or fail if no value is
  // reported
  int32_t GetHardwareLevel() const;

  bool IsHardwareLevelAtLeast(int32_t level) const;

  std::set<uint8_t> GetAvailableModes(int32_t key,
                                      int32_t min_value,
                                      int32_t max_value) const;

  void GetStreamConfigEntry(camera_metadata_ro_entry_t* entry) const;

  const camera_metadata_t* characteristics_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(StaticInfo);
};

class Camera3DeviceFixture : public testing::Test {
 public:
  explicit Camera3DeviceFixture(int cam_id) : cam_device_(cam_id) {}

  virtual void SetUp() override;

  virtual void TearDown() override;

 protected:

  Camera3Module cam_module_;

  Camera3Device cam_device_;

 private:
  // Process result metadata and/or output buffers. Tests can override this
  // function to handle metadata/buffers to suit their purpose. Note that
  // the metadata |metadata| and output buffers kept in |buffers| will be
  // freed after returning from this call; a test can "std::move" the unique
  // pointers to keep the metadata and buffer.
  virtual void ProcessResultMetadataOutputBuffers(
      uint32_t frame_number,
      CameraMetadataUniquePtr metadata,
      std::vector<BufferHandleUniquePtr> buffers) {}

  // Process partial metadata. Tests can override this function to handle all
  // received partial metadata.
  virtual void ProcessPartialMetadata(
      std::vector<CameraMetadataUniquePtr>* partial_metadata) {}

  DISALLOW_IMPLICIT_CONSTRUCTORS(Camera3DeviceFixture);
};

}  // namespace camera3_test

#endif  // CAMERA3_TEST_CAMERA3_DEVICE_FIXTURE_H_
