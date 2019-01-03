// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_PERCEPTION_FAKE_VIDEO_CAPTURE_SERVICE_CLIENT_H_
#define MEDIA_PERCEPTION_FAKE_VIDEO_CAPTURE_SERVICE_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "media_perception/video_capture_service_client.h"

namespace mri {

class FakeVideoCaptureServiceClient : public VideoCaptureServiceClient {
 public:
  FakeVideoCaptureServiceClient() = default;

  void SetDevicesForGetDevices(std::vector<SerializedVideoDevice> devices);

  // VideoCaptureServiceClient:
  bool Connect() override;
  bool IsConnected() override;
  void GetDevices(const GetDevicesCallback& callback) override;
  void OpenDevice(const std::string& device_id,
                  const OpenDeviceCallback& callback) override;
  bool IsVideoCaptureStartedForDevice(
      const std::string& device_id,
      SerializedVideoStreamParams* capture_format) override;
  int AddFrameHandler(
      const std::string& device_id,
      const SerializedVideoStreamParams& capture_format,
      FrameHandler handler) override;
  bool RemoveFrameHandler(
      const std::string& device_id, int frame_handler_id) override;
  void CreateVirtualDevice(const SerializedVideoDevice& video_device,
                           const VirtualDeviceCallback& callback) override;
  void PushFrameToVirtualDevice(const std::string& device_id,
                                uint64_t timestamp_in_microseconds,
                                std::unique_ptr<const uint8_t[]> data,
                                int data_size,
                                RawPixelFormat pixel_format,
                                int frame_width, int frame_height) override;
  void CloseVirtualDevice(const std::string& device_id) override;

 private:
  std::vector<SerializedVideoDevice> devices_;
  bool connected_;

  DISALLOW_COPY_AND_ASSIGN(FakeVideoCaptureServiceClient);
};

}  // namespace mri

#endif  // MEDIA_PERCEPTION_FAKE_VIDEO_CAPTURE_SERVICE_CLIENT_H_
