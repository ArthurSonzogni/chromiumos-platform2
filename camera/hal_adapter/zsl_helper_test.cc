/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/zsl_helper.h"

#include <cmath>
#include <memory>
#include <vector>

#include <hardware/camera3.h>
#include <camera/camera_metadata.h>
#include <base/at_exit.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <system/camera_metadata.h>

#include "common/utils/common_types.h"
#include "cros-camera/camera_buffer_manager.h"

using testing::_;
using testing::Return;

namespace {

constexpr int32_t kPreviewStreamWidth = 1280;
constexpr int32_t kPreviewStreamHeight = 720;
constexpr uint32_t kPreviewStreamMaxBuffers = 4;
constexpr int64_t kPreviewMinFrameDuration = 33333333;

constexpr int32_t kStillCaptureStreamWidth = 1280;
constexpr int32_t kStillCaptureStreamHeight = 720;
constexpr uint32_t kStillCaptureStreamMaxBuffers = 4;
constexpr int64_t kStillCaptureMinFrameDuration = 66666666;

constexpr int32_t kZslBiStreamWidth = 1920;
constexpr int32_t kZslBiStreamHeight = 1080;
constexpr int64_t kZslBiStreamMinFrameDuration = 33333333;
constexpr uint32_t kZslBiStreamMaxBuffers = 4;

constexpr int32_t kMaxNumInputStreams = 1;

constexpr int64_t kMockCurrentTimestamp = 1'000'000'000LL;

class MockCameraBufferManager : public cros::CameraBufferManager {
 public:
  MOCK_METHOD(int,
              Allocate,
              (size_t, size_t, uint32_t, uint32_t, buffer_handle_t*, uint32_t*),
              (override));

  MOCK_METHOD(int, Free, (buffer_handle_t), (override));

  MOCK_METHOD(int, Register, (buffer_handle_t), (override));

  MOCK_METHOD(int, Deregister, (buffer_handle_t), (override));

  MOCK_METHOD(int,
              Lock,
              (buffer_handle_t,
               uint32_t,
               uint32_t,
               uint32_t,
               uint32_t,
               uint32_t,
               void**),
              (override));

  MOCK_METHOD(int,
              LockYCbCr,
              (buffer_handle_t,
               uint32_t,
               uint32_t,
               uint32_t,
               uint32_t,
               uint32_t,
               struct android_ycbcr* out_ycbcr),
              (override));

  MOCK_METHOD(int, Unlock, (buffer_handle_t), (override));

  MOCK_METHOD(uint32_t, ResolveDrmFormat, (uint32_t, uint32_t), (override));
};

class MockZslBufferManager : public cros::ZslBufferManager {
 public:
  MOCK_METHOD(bool, Initialize, (size_t, const camera3_stream_t*), (override));

  MOCK_METHOD(buffer_handle_t*, GetBuffer, (), (override));

  MOCK_METHOD(bool, ReleaseBuffer, (buffer_handle_t), (override));
};

}  // namespace

namespace cros {

namespace tests {

class ZslBufferManagerTest : public ::testing::Test {
 public:
  static constexpr size_t kBufferPoolSize = 24;

  ZslBufferManagerTest() {
    zsl_bi_stream_ = {.stream_type = CAMERA3_STREAM_BIDIRECTIONAL,
                      .width = kZslBiStreamWidth,
                      .height = kZslBiStreamHeight,
                      .format = cros::ZslHelper::kZslPixelFormat,
                      .usage = 0,
                      .max_buffers = kZslBiStreamMaxBuffers};
  }

 protected:
  void SetUp() override {
    cbm_ = std::make_unique<MockCameraBufferManager>();
    zsl_buffer_manager_ = std::make_unique<ZslBufferManager>();
  }

  void TearDown() override {
    // Ordering is important here. ZslBufferManager calls
    // CameraBufferManager::Free to release previously-allocated buffer. We need
    // to make sure MockCameraBufferManager is destructed after.
    // Also important is that we destruct each object during TearDown() because
    // EXPECT_CALL is evaluated during destruction.
    zsl_buffer_manager_ = nullptr;
    cbm_ = nullptr;
  }

  void DoSetCameraBufferManagerForTesting(CameraBufferManager* buffer_manager) {
    zsl_buffer_manager_->SetCameraBufferManagerForTesting(buffer_manager);
  }

  void DoReset() { zsl_buffer_manager_->Reset(); }

  std::unique_ptr<ZslBufferManager> zsl_buffer_manager_;
  std::unique_ptr<MockCameraBufferManager> cbm_;
  camera3_stream_t zsl_bi_stream_;
};

TEST_F(ZslBufferManagerTest, UninitializedTest) {
  EXPECT_EQ(zsl_buffer_manager_->GetBuffer(), nullptr)
      << "ZSL returned a buffer despite being uninitialized";
}

TEST_F(ZslBufferManagerTest, GetBufferTest) {
  EXPECT_CALL(*cbm_.get(), Free)
      .Times(kBufferPoolSize)
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*cbm_.get(), Allocate)
      .Times(kBufferPoolSize)
      .WillRepeatedly(Return(0));
  DoSetCameraBufferManagerForTesting(cbm_.get());
  zsl_buffer_manager_->Initialize(kBufferPoolSize, &zsl_bi_stream_);
  for (size_t i = 0; i < kBufferPoolSize; ++i) {
    ASSERT_NE(zsl_buffer_manager_->GetBuffer(), nullptr)
        << "Failed to get the " << (i + 1) << "th buffer";
  }
  EXPECT_EQ(zsl_buffer_manager_->GetBuffer(), nullptr)
      << "ZSL buffer manager returned a buffer when it should've been empty";
}

TEST_F(ZslBufferManagerTest, ReleaseBufferTest) {
  EXPECT_CALL(*cbm_.get(), Free)
      .Times(kBufferPoolSize)
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*cbm_.get(), Allocate)
      .Times(kBufferPoolSize)
      .WillRepeatedly(Return(0));
  DoSetCameraBufferManagerForTesting(cbm_.get());
  zsl_buffer_manager_->Initialize(kBufferPoolSize, &zsl_bi_stream_);
  std::vector<buffer_handle_t> gotten_buffers;
  gotten_buffers.reserve(kBufferPoolSize);
  for (size_t i = 0; i < kBufferPoolSize; ++i) {
    buffer_handle_t* buffer = zsl_buffer_manager_->GetBuffer();
    ASSERT_NE(nullptr, buffer);
    // Intentionally storing only |buffer_handle_t| to test if ZslBufferManager
    // can match it back to |buffer_handle_t*| in its internal mapping.
    gotten_buffers.push_back(*buffer);
  }
  for (const auto& buffer : gotten_buffers) {
    EXPECT_TRUE(zsl_buffer_manager_->ReleaseBuffer(buffer));
  }
}

class ZslHelperTest : public ::testing::Test {
 public:
  ZslHelperTest() {
    preview_stream_ = {.stream_type = CAMERA3_STREAM_OUTPUT,
                       .width = kPreviewStreamWidth,
                       .height = kPreviewStreamHeight,
                       .format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
                       .usage = 0,
                       .max_buffers = kPreviewStreamMaxBuffers};
    still_capture_stream_ = {.stream_type = CAMERA3_STREAM_OUTPUT,
                             .width = kStillCaptureStreamWidth,
                             .height = kStillCaptureStreamHeight,
                             .format = HAL_PIXEL_FORMAT_BLOB,
                             .usage = 0,
                             .max_buffers = kStillCaptureStreamMaxBuffers};
  }

 protected:
  struct CaptureRequestDeleter {
    void operator()(camera3_capture_request_t* request) {
      if (!request) {
        return;
      }
      // request->input_buffer comes from ZSL ring buffer. No need to delete it.
      if (request->output_buffers) {
        delete[] request->output_buffers;
      }
      if (request->settings) {
        free_camera_metadata(const_cast<camera_metadata_t*>(request->settings));
      }
      delete request;
    }
  };
  using ScopedCaptureRequest =
      std::unique_ptr<camera3_capture_request_t, CaptureRequestDeleter>;

  void SetUp() override {
    android::CameraMetadata static_metadata;
    // Expose private reprocessing as an available request capability.
    ASSERT_EQ(
        static_metadata.update(
            ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
            {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING}),
        0);

    // List |kZslBiStreamWidth| x |kZslBiStreamHeight| as an available input
    // stream.
    std::vector<int32_t> stream_configs = {
        // ZSL input
        ZslHelper::kZslPixelFormat, kZslBiStreamWidth, kZslBiStreamHeight,
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT,
        // ZSL output
        ZslHelper::kZslPixelFormat, kZslBiStreamWidth, kZslBiStreamHeight,
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        // Preview
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, kPreviewStreamWidth,
        kPreviewStreamHeight,
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        // Still capture
        HAL_PIXEL_FORMAT_BLOB, kStillCaptureStreamWidth,
        kStillCaptureStreamHeight,
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT};
    ASSERT_EQ(
        static_metadata.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                               std::move(stream_configs)),

        0);

    // Set min frame duration to 30fps.
    std::vector<int64_t> min_frame_durations = {
        // ZSL
        ZslHelper::kZslPixelFormat, kZslBiStreamWidth, kZslBiStreamHeight,
        kZslBiStreamMinFrameDuration,
        // Preview
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, kPreviewStreamWidth,
        kPreviewStreamHeight, kPreviewMinFrameDuration,
        // Still capture
        HAL_PIXEL_FORMAT_BLOB, kStillCaptureStreamWidth,
        kStillCaptureStreamHeight, kStillCaptureMinFrameDuration};
    ASSERT_EQ(
        static_metadata.update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                               std::move(min_frame_durations)),
        0);

    // A value of 1 means that partial results are not supported (irrelevant).
    std::vector<int32_t> partial_result_count = {1};
    ASSERT_EQ(static_metadata.update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
                                     std::move(partial_result_count)),
              0);

    // Set maximum number of input streams to 1 to allow for the private stream.
    std::vector<int32_t> max_num_input_streams{kMaxNumInputStreams};
    ASSERT_EQ(static_metadata.update(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
                                     std::move(max_num_input_streams)),
              0);

    // Set timestamp source to realtime (irrelevant).
    ASSERT_EQ(
        static_metadata.update(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
                               {ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME}),
        0);

    cbm_ = std::make_unique<MockCameraBufferManager>();
    zsl_helper_ = std::make_unique<ZslHelper>(static_metadata.getAndLock());
  }

  void TearDown() override {
    zsl_helper_ = nullptr;
    cbm_ = nullptr;
  }

  camera3_stream_t* GetZslBiStream() { return zsl_helper_->bi_stream_.get(); }

  ScopedCaptureRequest GetMockCaptureRequest(
      camera_metadata_enum_android_control_capture_intent_t capture_intent) {
    static uint32_t frame_number_iter = 0;

    ScopedCaptureRequest request(new camera3_capture_request_t);
    request->frame_number = frame_number_iter++;
    request->input_buffer = nullptr;
    request->num_output_buffers = 1;

    camera3_stream_buffer_t* output_buffers = new camera3_stream_buffer_t[1];
    camera3_stream_buffer_t& buffer = output_buffers[0];
    buffer.acquire_fence = -1;
    buffer.release_fence = -1;
    buffer.status = CAMERA3_BUFFER_STATUS_OK;
    buffer.buffer = nullptr;  // Not used in tests.
    buffer.stream =
        capture_intent == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE
            ? &still_capture_stream_
            : &preview_stream_;
    request->output_buffers = output_buffers;

    android::CameraMetadata metadata;
    EXPECT_EQ(metadata.update(ANDROID_CONTROL_CAPTURE_INTENT,
                              reinterpret_cast<uint8_t*>(&capture_intent), 1),
              0);
    if (capture_intent == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE) {
      uint8_t enable_zsl = 1;
      EXPECT_EQ(metadata.update(ANDROID_CONTROL_ENABLE_ZSL, &enable_zsl, 1), 0)
          << "Failed to set ANDROID_CONTROL_ENABLE_ZSL";
    }
    request->settings = metadata.release();
    return request;
  }

  void InitializeZslHelper() {
    camera3_stream_t* bi_stream = GetZslBiStream();
    // Pretend we're the camera HAL and give |max_buffers| a value like HALs
    // would do during configure_streams().
    bi_stream->max_buffers = kZslBiStreamMaxBuffers;
    zsl_buffer_manager_ = new MockZslBufferManager();

    std::vector<camera3_stream_t*> streams = {bi_stream,
                                              &still_capture_stream_};
    camera3_stream_configuration_t stream_list = {
        .num_streams = static_cast<uint32_t>(streams.size()),
        .streams = streams.data()};

    EXPECT_CALL(*zsl_buffer_manager_, Initialize).WillOnce(Return(true));
    DoSetZslBufferManagerForTesting(
        std::unique_ptr<MockZslBufferManager>(zsl_buffer_manager_));
    zsl_helper_->Initialize(&stream_list);
  }

  void FillZslRingBuffer(bool ring_buffer_3a_converged) {
    zsl_helper_->ring_buffer_.clear();
    uint32_t frame_number_iter = 1;
    // First fill some buffers whose buffer and metadata aren't ready.
    for (int i = 0; i < kZslBiStreamMaxBuffers; ++i) {
      camera3_stream_buffer stream_buffer = {};
      // Here we make an initial ZslBuffer, the metadata and buffer of which
      // aren't ready by default.
      ZslBuffer buffer(frame_number_iter++, stream_buffer);
      zsl_helper_->ring_buffer_.push_front(std::move(buffer));
    }

    // Now we fill the candidate buffers we can choose from.
    auto Set3AState = [&](android::CameraMetadata* metadata, uint32_t tag,
                          uint8_t value) {
      ASSERT_EQ(metadata->update(tag, &value, 1), 0);
    };
    size_t num_candidate_buffers =
        std::ceil(static_cast<double>(ZslHelper::kZslDefaultLookbackNs) /
                  kZslBiStreamMinFrameDuration);
    for (int i = 0; i < num_candidate_buffers; ++i) {
      camera3_stream_buffer_t stream_buffer = {};
      ZslBuffer buffer(frame_number_iter++, stream_buffer);
      // Set 3A metadata.
      Set3AState(&buffer.metadata, ANDROID_CONTROL_AE_MODE,
                 ANDROID_CONTROL_AE_MODE_ON);
      Set3AState(&buffer.metadata, ANDROID_CONTROL_AE_STATE,
                 ring_buffer_3a_converged ? ANDROID_CONTROL_AE_STATE_CONVERGED
                                          : ANDROID_CONTROL_AE_STATE_SEARCHING);
      Set3AState(&buffer.metadata, ANDROID_CONTROL_AF_MODE,
                 ANDROID_CONTROL_AF_MODE_AUTO);
      Set3AState(&buffer.metadata, ANDROID_CONTROL_AF_STATE,
                 ring_buffer_3a_converged
                     ? ANDROID_CONTROL_AF_STATE_PASSIVE_FOCUSED
                     : ANDROID_CONTROL_AF_STATE_PASSIVE_UNFOCUSED);
      Set3AState(&buffer.metadata, ANDROID_CONTROL_AWB_MODE,
                 ANDROID_CONTROL_AWB_MODE_AUTO);
      Set3AState(&buffer.metadata, ANDROID_CONTROL_AWB_STATE,
                 ring_buffer_3a_converged
                     ? ANDROID_CONTROL_AWB_STATE_CONVERGED
                     : ANDROID_CONTROL_AWB_STATE_SEARCHING);
      // Set timestamp metadata.
      // The older the buffer, the smaller the timestamp has to be.
      int64_t timestamp =
          kMockCurrentTimestamp -
          (num_candidate_buffers - i - 1) * kZslBiStreamMinFrameDuration;
      ASSERT_EQ(buffer.metadata.update(ANDROID_SENSOR_TIMESTAMP, &timestamp, 1),
                0);
      // |buffer| is not selected by default, so these buffers can all be
      // selected for private reprocessing.
      buffer.metadata_ready = true;
      buffer.buffer_ready = true;
      zsl_helper_->ring_buffer_.push_front(std::move(buffer));
    }
  }

  bool DoCanEnableZsl(std::vector<camera3_stream_t*>* streams) {
    return zsl_helper_->CanEnableZsl(streams);
  }

  bool DoIsZslRequested(camera_metadata_t* settings) {
    return zsl_helper_->IsZslRequested(settings);
  }

  bool DoIs3AConverged(const android::CameraMetadata& android_metadata) {
    return zsl_helper_->Is3AConverged(android_metadata);
  }

  void DoSetZslBufferManagerForTesting(
      std::unique_ptr<ZslBufferManager> zsl_buffer_manager) {
    zsl_helper_->SetZslBufferManagerForTesting(std::move(zsl_buffer_manager));
  }

  void DoOverrideCurrentTimestampForTesting(int64_t timestamp) {
    zsl_helper_->OverrideCurrentTimestampForTesting(timestamp);
  }

  std::unique_ptr<ZslHelper> zsl_helper_;
  MockZslBufferManager* zsl_buffer_manager_;
  std::unique_ptr<MockCameraBufferManager> cbm_;

  camera3_stream_t preview_stream_;
  camera3_stream_t still_capture_stream_;
};

// Test that ENABLE_ZSL is added to the available request keys when the device
// supports private reprocessing and doesn't already support ZSL itself.
TEST(ZslHelperStaticTest, TryAddEnableZslKeyTest) {
  android::CameraMetadata static_metadata;

  ASSERT_EQ(static_metadata.update(
                ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING}),
            0);
  std::vector<int32_t> request_keys;
  ASSERT_EQ(static_metadata.update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
                                   request_keys),
            0);

  EXPECT_TRUE(ZslHelper::TryAddEnableZslKey(&static_metadata));

  request_keys = {ANDROID_CONTROL_ENABLE_ZSL};
  ASSERT_EQ(static_metadata.update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
                                   request_keys),
            0);
  EXPECT_FALSE(ZslHelper::TryAddEnableZslKey(&static_metadata))
      << "We shouldn't add ANDROID_CONTROL_ENABLE_ZSL if HAL already supports "
         "ZSL";
}

// Test that ZSL attaches its own ZSL bidirectional stream to stream
// configurations.
TEST_F(ZslHelperTest, AttachZslStreamTest) {
  std::vector<camera3_stream_t*> streams{&still_capture_stream_};
  EXPECT_TRUE(DoCanEnableZsl(&streams));

  camera3_stream_configuration_t stream_list = {
      .num_streams = static_cast<uint32_t>(streams.size()),
      .streams = streams.data()};
  ASSERT_TRUE(zsl_helper_->AttachZslStream(&stream_list, &streams));

  EXPECT_EQ(stream_list.num_streams, 2);
  EXPECT_EQ(streams.back(), GetZslBiStream());
  EXPECT_NE(streams.back(), &still_capture_stream_);
}

// Test that ZSL initializes correctly with the given stream configurations and
// designate the expected number of buffers in ZSL buffer manager.
TEST_F(ZslHelperTest, InitializeTest) {
  size_t expected_buffer_pool_size =
      std::ceil(static_cast<double>(ZslHelper::kZslDefaultLookbackNs) /
                kZslBiStreamMinFrameDuration) +
      kZslBiStreamMaxBuffers + kStillCaptureStreamMaxBuffers;
  camera3_stream_t* bi_stream = GetZslBiStream();
  // Pretend we're the camera HAL and give |max_buffers| a value like HALs would
  // do during configure_streams().
  bi_stream->max_buffers = kZslBiStreamMaxBuffers;

  MockZslBufferManager* zsl_buffer_manager = new MockZslBufferManager();
  EXPECT_CALL(*zsl_buffer_manager,
              Initialize(expected_buffer_pool_size, bi_stream))
      .WillOnce(Return(true));
  DoSetZslBufferManagerForTesting(
      std::unique_ptr<MockZslBufferManager>(zsl_buffer_manager));

  std::vector<camera3_stream_t*> streams = {GetZslBiStream(),
                                            &still_capture_stream_};
  camera3_stream_configuration_t stream_list = {
      .num_streams = static_cast<uint32_t>(streams.size()),
      .streams = streams.data()};
  EXPECT_TRUE(zsl_helper_->Initialize(&stream_list));
}

// Test that |ZslHelper| correctly attaches a private buffer to a preview
// request rather than transforming it.
TEST_F(ZslHelperTest, ProcessZslCaptureRequestPreview) {
  InitializeZslHelper();
  DoOverrideCurrentTimestampForTesting(kMockCurrentTimestamp);
  FillZslRingBuffer(/*ring_buffer_3a_converged=*/true);
  auto scoped_request =
      GetMockCaptureRequest(ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW);
  std::vector<camera3_stream_buffer_t> output_buffers{
      scoped_request->output_buffers,
      scoped_request->output_buffers + scoped_request->num_output_buffers};
  internal::ScopedCameraMetadata scoped_metadata(
      clone_camera_metadata(scoped_request->settings));

  // Intentionally initialize it in case someone accesses its content.
  buffer_handle_t buffer = nullptr;
  EXPECT_CALL(*zsl_buffer_manager_, GetBuffer).WillOnce(Return(&buffer));
  ASSERT_FALSE(zsl_helper_->ProcessZslCaptureRequest(
      scoped_request.get(), &output_buffers, &scoped_metadata));
  EXPECT_EQ(output_buffers.back().stream, GetZslBiStream());
}

// Test that |ZslHelper| transforms capture requests correctly.
TEST_F(ZslHelperTest, ProcessZslCaptureRequestStillCapture) {
  InitializeZslHelper();
  DoOverrideCurrentTimestampForTesting(kMockCurrentTimestamp);
  FillZslRingBuffer(/*ring_buffer_3a_converged=*/true);
  auto scoped_request =
      GetMockCaptureRequest(ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE);
  std::vector<camera3_stream_buffer_t> output_buffers{
      scoped_request->output_buffers,
      scoped_request->output_buffers + scoped_request->num_output_buffers};

  // Assign JPEG metadata and make sure it is re-added to the processed request
  // metadata.
  constexpr int32_t kJpegOrientation = 90;
  android::CameraMetadata metadata(
      clone_camera_metadata(scoped_request->settings));
  ASSERT_EQ(metadata.update(ANDROID_JPEG_ORIENTATION, &kJpegOrientation, 1), 0);
  const std::vector<int32_t> kJpegThumbnailSize{320, 240};
  ASSERT_EQ(
      metadata.update(ANDROID_JPEG_THUMBNAIL_SIZE, kJpegThumbnailSize.data(),
                      kJpegThumbnailSize.size()),
      0);
  internal::ScopedCameraMetadata scoped_metadata(metadata.release());

  EXPECT_TRUE(zsl_helper_->ProcessZslCaptureRequest(
      scoped_request.get(), &output_buffers, &scoped_metadata));
  EXPECT_TRUE(scoped_request->input_buffer != nullptr);

  camera_metadata_ro_entry_t entry;
  ASSERT_EQ(find_camera_metadata_ro_entry(scoped_metadata.get(),
                                          ANDROID_JPEG_ORIENTATION, &entry),
            0);
  EXPECT_EQ(entry.data.i32[0], kJpegOrientation)
      << "ANDROID_JPEG_ORIENTATION should be re-added to metadata";
  ASSERT_EQ(find_camera_metadata_ro_entry(scoped_metadata.get(),
                                          ANDROID_JPEG_THUMBNAIL_SIZE, &entry),
            0);
  EXPECT_EQ(entry.count, kJpegThumbnailSize.size());
  EXPECT_EQ(entry.data.i32[0], kJpegThumbnailSize[0]);
  EXPECT_EQ(entry.data.i32[1], kJpegThumbnailSize[1]);

  // Now make sure we don't select buffers that are too old. We test this by
  // making the current timestamp very new.
  DoOverrideCurrentTimestampForTesting(kMockCurrentTimestamp +
                                       ZslHelper::kZslDefaultLookbackNs + 1);
  scoped_request =
      GetMockCaptureRequest(ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE);
  output_buffers = {
      scoped_request->output_buffers,
      scoped_request->output_buffers + scoped_request->num_output_buffers};
  scoped_metadata = internal::ScopedCameraMetadata(
      clone_camera_metadata(scoped_request->settings));
  EXPECT_FALSE(zsl_helper_->ProcessZslCaptureRequest(
      scoped_request.get(), &output_buffers, &scoped_metadata));

  // Test that we don't select a buffer when 3A is not converged.
  DoOverrideCurrentTimestampForTesting(kMockCurrentTimestamp);
  FillZslRingBuffer(/*ring_buffer_3a_converged=*/false);
  scoped_request =
      GetMockCaptureRequest(ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE);
  output_buffers = {
      scoped_request->output_buffers,
      scoped_request->output_buffers + scoped_request->num_output_buffers};
  scoped_metadata = internal::ScopedCameraMetadata(
      clone_camera_metadata(scoped_request->settings));
  EXPECT_FALSE(zsl_helper_->ProcessZslCaptureRequest(
      scoped_request.get(), &output_buffers, &scoped_metadata));
}

// Verifies that the attached output stream can be identified successfully.
// We cannot test |transformed_input| because |ZslHelper| would attempt to
// release/free the input buffer and there isn't a good way to mock it.
TEST_F(ZslHelperTest, ProcessZslCaptureResultTest) {
  std::vector<camera3_stream_buffer_t> attached_output_buffers = {
      {.stream = &preview_stream_, .release_fence = -1},
      {.stream = GetZslBiStream(), .release_fence = -1}};
  camera3_capture_result_t result = {
      .frame_number = 1,
      .result = nullptr,
      .num_output_buffers =
          static_cast<uint32_t>(attached_output_buffers.size()),
      .output_buffers = attached_output_buffers.data(),
      .input_buffer = nullptr,
      .partial_result = 0};
  const camera3_stream_buffer_t* attached_output;
  const camera3_stream_buffer_t* transformed_input;
  zsl_helper_->ProcessZslCaptureResult(&result, &attached_output,
                                       &transformed_input);
  EXPECT_EQ(attached_output, &attached_output_buffers[1]);
}

TEST_F(ZslHelperTest, CanEnableZslTest) {
  std::vector<camera3_stream_t*> streams{&still_capture_stream_};
  EXPECT_TRUE(DoCanEnableZsl(&streams));

  streams = {};
  EXPECT_FALSE(DoCanEnableZsl(&streams))
      << "We can't enable ZSL if there isn't a still capture stream";

  camera3_stream_t zsl_stream = {};
  zsl_stream.stream_type = CAMERA3_STREAM_OUTPUT;
  zsl_stream.format = ZslHelper::kZslPixelFormat;
  zsl_stream.usage = GRALLOC_USAGE_HW_CAMERA_ZSL;
  streams = {&still_capture_stream_, &zsl_stream};
  EXPECT_FALSE(DoCanEnableZsl(&streams))
      << "We can't enable ZSL if there's already a ZSL output stream";

  streams = {&still_capture_stream_};
  std::vector<camera3_stream_t> input_streams(kMaxNumInputStreams);
  for (int i = 0; i < kMaxNumInputStreams; ++i) {
    input_streams[i].stream_type = CAMERA3_STREAM_INPUT;
    streams.push_back(&input_streams[i]);
  }
  EXPECT_FALSE(DoCanEnableZsl(&streams))
      << "We cannot enable ZSL if the max number of input streams is already "
         "reached";
}

TEST_F(ZslHelperTest, IsZslRequestedTest) {
  ScopedCaptureRequest request =
      GetMockCaptureRequest(ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE);
  EXPECT_TRUE(
      DoIsZslRequested(const_cast<camera_metadata_t*>(request->settings)));

  request = GetMockCaptureRequest(ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW);
  EXPECT_FALSE(
      DoIsZslRequested(const_cast<camera_metadata_t*>(request->settings)));
}

TEST_F(ZslHelperTest, Is3AConvergedTest) {
  android::CameraMetadata metadata;

  auto Set3AState = [&](uint32_t tag, uint8_t value) {
    ASSERT_EQ(metadata.update(tag, &value, 1), 0);
  };

  // 3A is expected to be "converged" when the modes are OFF.
  Set3AState(ANDROID_CONTROL_AE_MODE, ANDROID_CONTROL_AE_MODE_OFF);
  Set3AState(ANDROID_CONTROL_AF_MODE, ANDROID_CONTROL_AF_MODE_OFF);
  Set3AState(ANDROID_CONTROL_AWB_MODE, ANDROID_CONTROL_AWB_MODE_OFF);
  EXPECT_TRUE(DoIs3AConverged(metadata))
      << "3A should be converged when all the controls are OFF";

  // Test the condition where all the controls are ON.
  Set3AState(ANDROID_CONTROL_AE_MODE, ANDROID_CONTROL_AE_MODE_ON);
  Set3AState(ANDROID_CONTROL_AF_MODE, ANDROID_CONTROL_AF_MODE_AUTO);
  Set3AState(ANDROID_CONTROL_AWB_MODE, ANDROID_CONTROL_AWB_MODE_AUTO);

  Set3AState(ANDROID_CONTROL_AE_STATE, ANDROID_CONTROL_AE_STATE_CONVERGED);
  Set3AState(ANDROID_CONTROL_AF_STATE,
             ANDROID_CONTROL_AF_STATE_PASSIVE_FOCUSED);
  Set3AState(ANDROID_CONTROL_AWB_STATE, ANDROID_CONTROL_AWB_STATE_CONVERGED);
  EXPECT_TRUE(DoIs3AConverged(metadata));

  Set3AState(ANDROID_CONTROL_AE_STATE, ANDROID_CONTROL_AE_STATE_SEARCHING);
  Set3AState(ANDROID_CONTROL_AF_STATE,
             ANDROID_CONTROL_AF_STATE_PASSIVE_UNFOCUSED);
  Set3AState(ANDROID_CONTROL_AWB_STATE, ANDROID_CONTROL_AWB_STATE_SEARCHING);
  EXPECT_FALSE(DoIs3AConverged(metadata));
}

}  // namespace tests

}  // namespace cros

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
