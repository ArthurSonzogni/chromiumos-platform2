/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/stream_manipulator_helper.h"

#include <memory>
#include <tuple>

#include <camera/camera_metadata.h>
#include <cutils/native_handle.h>
#include <hardware/camera3.h>
#include <hardware/gralloc.h>
#include <system/camera_metadata.h>
#include <system/graphics-base.h>

#include <base/command_line.h>
#include <base/containers/flat_map.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/notreached.h>
#include <base/numerics/safe_conversions.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <base/test/test_timeouts.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/camera_buffer_handle.h"
#include "common/camera_hal3_helpers.h"
#include "common/stream_manipulator.h"
#include "common/test_support/fake_still_capture_processor.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common_types.h"

namespace cros {
namespace {

using ::testing::Address;
using ::testing::Contains;
using ::testing::Not;
using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

using Format = std::tuple<uint32_t /*width*/,
                          uint32_t /*height*/,
                          uint32_t /*format*/,
                          float /*max_fps*/>;

android::CameraMetadata GenerateStaticMetadata(
    const std::vector<Format>& available_formats,
    const Size& active_array_size,
    uint32_t partial_result_count) {
  std::vector<int32_t> stream_configs;
  std::vector<int64_t> min_durations;
  for (auto& [w, h, f, r] : available_formats) {
    stream_configs.push_back(base::checked_cast<int32_t>(f));
    stream_configs.push_back(base::checked_cast<int32_t>(w));
    stream_configs.push_back(base::checked_cast<int32_t>(h));
    stream_configs.push_back(
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT);
    min_durations.push_back(base::checked_cast<int64_t>(f));
    min_durations.push_back(base::checked_cast<int64_t>(w));
    min_durations.push_back(base::checked_cast<int64_t>(h));
    min_durations.push_back(static_cast<int64_t>(1e9f / r));
  }

  android::CameraMetadata static_info;
  CHECK_EQ(static_info.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                              std::move(stream_configs)),
           0);
  CHECK_EQ(static_info.update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                              std::move(min_durations)),
           0);
  CHECK_EQ(static_info.update(
               ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
               std::vector<int32_t>{
                   0, 0, base::checked_cast<int32_t>(active_array_size.width),
                   base::checked_cast<int32_t>(active_array_size.height)}),
           0);
  CHECK_EQ(static_info.update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
                              std::vector<int32_t>{base::checked_cast<int32_t>(
                                  partial_result_count)}),
           0);

  return static_info;
}

class TestStreamManipulator : public StreamManipulator {
 public:
  explicit TestStreamManipulator(StreamManipulatorHelper::Config helper_config)
      : helper_config_(std::move(helper_config)) {}

  TestStreamManipulator(const TestStreamManipulator&) = delete;
  TestStreamManipulator& operator=(const TestStreamManipulator&) = delete;
  ~TestStreamManipulator() override = default;

  void SetBypassProcess(bool bypass_process) {
    bypass_process_ = bypass_process;
  }

  void SetPrivateContextBuilder(
      std::function<std::unique_ptr<StreamManipulatorHelper::PrivateContext>(
          uint32_t)> ctx_builder) {
    ctx_builder_ = std::move(ctx_builder);
  }

  ProcessTask& GetProcessTask(uint32_t frame_number,
                              const camera3_stream_t* stream) const {
    auto it = std::find_if(process_tasks_.begin(), process_tasks_.end(),
                           [&](const auto& task) {
                             return task->frame_number() == frame_number &&
                                    task->input_stream() == stream;
                           });
    CHECK(it != process_tasks_.end());
    return **it;
  }

  void FinishProcessTask(uint32_t frame_number,
                         const camera3_stream_t* stream) {
    auto it = std::find_if(process_tasks_.begin(), process_tasks_.end(),
                           [&](const auto& task) {
                             return task->frame_number() == frame_number &&
                                    task->input_stream() == stream;
                           });
    CHECK(it != process_tasks_.end());
    process_tasks_.erase(it);
    base::RunLoop().RunUntilIdle();
  }

  bool HasCropScaledBuffer(buffer_handle_t input,
                           buffer_handle_t output,
                           const Rect<float>& crop) const {
    for (auto& [i, o, c] : crop_scaled_buffers_) {
      if (i == input && o == output && c == crop) {
        return true;
      }
    }
    return false;
  }

  CaptureResultCallback GetResultCallback() {
    return base::BindRepeating(
        [](TestStreamManipulator* m, Camera3CaptureDescriptor result) {
          CHECK(m->ProcessCaptureResult(std::move(result)));
        },
        base::Unretained(this));
  }

  NotifyCallback GetNotifyCallback() {
    return base::BindRepeating(&TestStreamManipulator::Notify,
                               base::Unretained(this));
  }

  // StreamManipulator methods.
  bool Initialize(const camera_metadata_t* static_info,
                  Callbacks callbacks) override {
    constexpr const char* kFakeCameraModuleName = "Fake camera module";
    helper_ = std::make_unique<StreamManipulatorHelper>(
        std::move(helper_config_), kFakeCameraModuleName, static_info,
        std::move(callbacks),
        base::BindRepeating(&TestStreamManipulator::OnProcessTask,
                            base::Unretained(this)),
        base::BindRepeating(&TestStreamManipulator::CropScaleImage,
                            base::Unretained(this)),
        std::make_unique<tests::FakeStillCaptureProcessor>(),
        base::SequencedTaskRunner::GetCurrentDefault());
    return true;
  }

  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override {
    return helper_->PreConfigure(stream_config);
  }

  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override {
    helper_->PostConfigure(stream_config);
    return true;
  }

  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override {
    return true;
  }

  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override {
    helper_->HandleRequest(request, bypass_process_,
                           ctx_builder_(request->frame_number()));
    return true;
  }

  bool ProcessCaptureResult(Camera3CaptureDescriptor result) override {
    helper_->HandleResult(std::move(result));
    return true;
  }

  void Notify(camera3_notify_msg_t msg) override { helper_->Notify(msg); }

  bool Flush() override { return true; }

 private:
  std::optional<base::ScopedFD> CropScaleImage(
      buffer_handle_t input,
      base::ScopedFD input_release_fence,
      buffer_handle_t output,
      base::ScopedFD output_acquire_fence,
      const Rect<float>& crop) {
    CHECK(!input_release_fence.is_valid());
    CHECK(!output_acquire_fence.is_valid());
    crop_scaled_buffers_.emplace_back(input, output, crop);
    return std::make_optional(base::ScopedFD());
  }

  void OnProcessTask(ScopedProcessTask task) {
    process_tasks_.push_back(std::move(task));
  }

  StreamManipulatorHelper::Config helper_config_;
  std::unique_ptr<StreamManipulatorHelper> helper_;
  bool bypass_process_ = false;
  std::vector<ScopedProcessTask> process_tasks_;
  std::vector<std::tuple<buffer_handle_t /*input*/,
                         buffer_handle_t /*output*/,
                         Rect<float> /*crop*/>>
      crop_scaled_buffers_;
  std::function<std::unique_ptr<StreamManipulatorHelper::PrivateContext>(
      uint32_t)>
      ctx_builder_ = [](uint32_t) { return nullptr; };
};

struct TestCase {
  std::vector<StreamManipulatorHelper::Config> helper_configs;
  std::vector<Format> available_formats;
  Size active_array_size;
  uint32_t partial_result_count = 1;
  std::vector<camera3_stream_t> streams;
  uint32_t max_buffers = 1;
  bool expected_config_success = true;
  std::vector<size_t> expected_configured_stream_indices;
  std::vector<std::tuple<uint32_t /*width*/,
                         uint32_t /*height*/,
                         uint32_t /*format*/,
                         uint32_t /*usage*/>>
      expected_extra_configured_streams;
};

camera3_stream_t* FindStream(base::span<camera3_stream_t* const> streams,
                             uint32_t width,
                             uint32_t height,
                             uint32_t format,
                             uint32_t usage) {
  for (auto* s : streams) {
    if (s->width == width && s->height == height && s->format == format &&
        s->usage == usage) {
      return s;
    }
  }
  return nullptr;
}

class StreamManipulatorHelperTest : public testing::Test {
 protected:
  void TearDown() override {
    for (size_t i = manipulators_.size(); i > 0; --i) {
      manipulators_[i - 1].reset();
    }
    base::RunLoop().RunUntilIdle();
  }

  void SetUpWithTestCase(const TestCase& test_case) {
    partial_result_count_ = test_case.partial_result_count;
    max_buffers_ = test_case.max_buffers;
    streams_ = test_case.streams;

    Initialize(test_case.helper_configs, test_case.available_formats,
               test_case.active_array_size, partial_result_count_);

    std::vector<camera3_stream_t*> stream_ptrs;
    for (auto& s : streams_) {
      stream_ptrs.push_back(&s);
    }
    Camera3StreamConfiguration stream_config(
        camera3_stream_configuration_t{
            .num_streams = base::checked_cast<uint32_t>(stream_ptrs.size()),
            .streams = stream_ptrs.data(),
            .operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE,
        },
        /*stream_effects_map=*/nullptr);
    EXPECT_EQ(PreConfigure(&stream_config), test_case.expected_config_success);
    if (!test_case.expected_config_success) {
      return;
    }

    EXPECT_EQ(stream_config.num_streams(),
              test_case.expected_configured_stream_indices.size() +
                  test_case.expected_extra_configured_streams.size());
    for (size_t i : test_case.expected_configured_stream_indices) {
      EXPECT_THAT(stream_config.GetStreams(), Contains(stream_ptrs[i]))
          << "stream (" << GetDebugString(stream_ptrs[i]) << ") not found";
      configured_streams_.push_back(stream_ptrs[i]);
    }
    for (auto& [w, h, f, u] : test_case.expected_extra_configured_streams) {
      camera3_stream_t* stream =
          FindStream(stream_config.GetStreams(), w, h, f, u);
      EXPECT_NE(stream, nullptr)
          << "extra stream of size " << Size(w, h).ToString() << " not found";
      EXPECT_THAT(streams_, Not(Contains(Address(stream))));
      extra_configured_streams_.push_back(stream);
    }

    for (auto* s : stream_config.GetStreams()) {
      s->max_buffers = max_buffers_;
    }
    PostConfigure(&stream_config);

    EXPECT_THAT(stream_config.GetStreams(),
                UnorderedElementsAreArray(stream_ptrs));
    for (auto* s : stream_config.GetStreams()) {
      if (s->format != HAL_PIXEL_FORMAT_BLOB) {
        EXPECT_TRUE(s->usage | kProcessStreamUsageFlags)
            << "usage not configured for stream " << GetDebugString(s);
      }
      EXPECT_EQ(s->max_buffers, max_buffers_)
          << "max_buffers not configured for stream " << GetDebugString(s);
    }
  }

  void AllocateRequestBuffers() {
    for (auto& s : streams_) {
      for (uint32_t i = 0; i < max_buffers_; ++i) {
        request_buffers_[&s].emplace_back(
            CameraBufferManager::AllocateScopedBuffer(s.width, s.height,
                                                      s.format, s.usage));
      }
    }
  }

  void Initialize(std::vector<StreamManipulatorHelper::Config> helper_configs,
                  const std::vector<Format>& available_formats,
                  const Size& active_array_size,
                  uint32_t partial_result_count) {
    ASSERT_NE(helper_configs.size(), 0);
    for (auto& c : helper_configs) {
      manipulators_.push_back(
          std::make_unique<TestStreamManipulator>(std::move(c)));
    }
    static_info_ = GenerateStaticMetadata(available_formats, active_array_size,
                                          partial_result_count);
    const camera_metadata_t* locked_static_info = static_info_.getAndLock();
    manipulators_[0]->Initialize(
        locked_static_info,
        StreamManipulator::Callbacks{
            .result_callback = base::BindRepeating(
                &StreamManipulatorHelperTest::ResultCallback,
                base::Unretained(this)),
            .notify_callback = base::BindRepeating(
                &StreamManipulatorHelperTest::NotifyCallback,
                base::Unretained(this)),
        });
    for (size_t i = 1; i < manipulators_.size(); ++i) {
      ASSERT_TRUE(manipulators_[i]->Initialize(
          locked_static_info,
          StreamManipulator::Callbacks{
              .result_callback = manipulators_[i - 1]->GetResultCallback(),
              .notify_callback = manipulators_[i - 1]->GetNotifyCallback(),
          }));
    }
  }

  bool PreConfigure(Camera3StreamConfiguration* stream_config) {
    bool ok = true;
    for (auto& m : manipulators_) {
      ok = m->ConfigureStreams(stream_config) && ok;
    }
    return ok;
  }

  void PostConfigure(Camera3StreamConfiguration* stream_config) {
    for (size_t i = manipulators_.size(); i > 0; --i) {
      ASSERT_TRUE(manipulators_[i - 1]->OnConfiguredStreams(stream_config));
    }
  }

  void SendRequest(Camera3CaptureDescriptor* request) {
    for (auto& m : manipulators_) {
      ASSERT_TRUE(m->ProcessCaptureRequest(request));
    }
  }

  void SendResult(Camera3CaptureDescriptor result) {
    ASSERT_TRUE(manipulators_.back()->ProcessCaptureResult(std::move(result)));
    base::RunLoop().RunUntilIdle();
  }

  void Notify(camera3_notify_msg_t msg) {
    manipulators_.back()->Notify(msg);
    base::RunLoop().RunUntilIdle();
  }

  Camera3CaptureDescriptor TakeLastReturnedResult() {
    CHECK(!returned_results_.empty());
    Camera3CaptureDescriptor result = std::move(returned_results_.back());
    returned_results_.pop_back();
    return result;
  }

  std::vector<camera3_notify_msg_t> TakeNotifiedMessages() {
    return std::move(notified_messages_);
  }

  TestStreamManipulator& manipulator(size_t index) const {
    CHECK_LT(index, manipulators_.size());
    return *manipulators_[index];
  }

  // For configuration with test case.
  uint32_t partial_result_count_ = 0;
  uint32_t max_buffers_ = 0;
  std::vector<camera3_stream_t> streams_;
  std::vector<camera3_stream_t*> configured_streams_;
  std::vector<camera3_stream_t*> extra_configured_streams_;
  base::flat_map<const camera3_stream_t*, std::vector<ScopedBufferHandle>>
      request_buffers_;

 private:
  void ResultCallback(Camera3CaptureDescriptor result) {
    CHECK(!result.is_empty());
    returned_results_.push_back(std::move(result));
  }

  void NotifyCallback(camera3_notify_msg_t msg) {
    notified_messages_.push_back(msg);
  }

  android::CameraMetadata static_info_;
  std::vector<std::unique_ptr<TestStreamManipulator>> manipulators_;
  std::vector<Camera3CaptureDescriptor> returned_results_;
  std::vector<camera3_notify_msg_t> notified_messages_;

  base::test::TaskEnvironment task_environment_;
};

class StreamManipulatorHelperTestWithCase
    : public StreamManipulatorHelperTest,
      public testing::WithParamInterface<TestCase> {};

std::pair<buffer_handle_t, int /*status*/> FindBuffer(
    const Camera3CaptureDescriptor& desc, const camera3_stream_t* stream) {
  for (auto& b : desc.GetOutputBuffers()) {
    if (b.stream() == stream) {
      return std::make_pair(*b.buffer(), b.status());
    }
  }
  return std::make_pair(nullptr, CAMERA3_BUFFER_STATUS_ERROR);
}

Camera3CaptureDescriptor MakeRequest(
    uint32_t frame_number,
    const std::vector<std::pair<camera3_stream_t*, buffer_handle_t*>>&
        stream_buffers) {
  Camera3CaptureDescriptor request(
      camera3_capture_request_t{.frame_number = frame_number});
  for (auto& [s, b] : stream_buffers) {
    request.AppendOutputBuffer(
        Camera3StreamBuffer::MakeRequestOutput(camera3_stream_buffer_t{
            .stream = s,
            .buffer = b,
            .status = CAMERA3_BUFFER_STATUS_OK,
            .acquire_fence = -1,
            .release_fence = -1,
        }));
  }
  return request;
}

Camera3CaptureDescriptor MakeResult(
    uint32_t frame_number,
    const std::vector<std::pair<camera3_stream_t*, buffer_handle_t*>>&
        stream_buffers,
    uint32_t partial_result,
    int status = CAMERA3_BUFFER_STATUS_OK) {
  Camera3CaptureDescriptor result(camera3_capture_result_t{
      .frame_number = frame_number, .partial_result = partial_result});
  for (auto& [s, b] : stream_buffers) {
    result.AppendOutputBuffer(
        Camera3StreamBuffer::MakeResultOutput(camera3_stream_buffer_t{
            .stream = s,
            .buffer = b,
            .status = status,
            .acquire_fence = -1,
            .release_fence = -1,
        }));
  }
  return result;
}

const TestCase g_test_cases[] =
    {
        // No stream manipulation.
        [0] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kBypass},
                    },
                .available_formats =
                    {
                        {1920, 1080, HAL_PIXEL_FORMAT_BLOB, 30.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_BLOB},
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0, 1},
                .expected_extra_configured_streams = {},
            },
        // Adding processing streams.
        [1] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kStillProcess},
                    },
                .available_formats =
                    {
                        {1920, 1080, HAL_PIXEL_FORMAT_BLOB, 30.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_BLOB},
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0, 1},
                .expected_extra_configured_streams =
                    {
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags | kStillCaptureUsageFlag},
                    },
            },
        [2] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess},
                    },
                .available_formats =
                    {
                        {1920, 1080, HAL_PIXEL_FORMAT_BLOB, 30.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 360, HAL_PIXEL_FORMAT_YCBCR_420_888, 60.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_BLOB},
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                        {.width = 640,
                         .height = 360,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0, 1, 2},
                .expected_extra_configured_streams =
                    {
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags | kStillCaptureUsageFlag},
                    },
            },
        [3] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .prefer_large_source = true},
                    },
                .available_formats =
                    {
                        {2592, 1944, HAL_PIXEL_FORMAT_BLOB, 15.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_BLOB, 30.0f},
                        {2592, 1944, HAL_PIXEL_FORMAT_YCBCR_420_888, 15.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 360, HAL_PIXEL_FORMAT_YCBCR_420_888, 60.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_BLOB},
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0, 1},
                .expected_extra_configured_streams =
                    {
                        {2592, 1944, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags | kStillCaptureUsageFlag},
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags},
                    },
            },
        // Reusing still YUV stream.
        [4] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kStillProcess},
                        {.process_mode = ProcessMode::kVideoAndStillProcess},
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .prefer_large_source = true},
                    },
                .available_formats =
                    {
                        {1920, 1080, HAL_PIXEL_FORMAT_BLOB, 30.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_BLOB},
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
                         .usage = kStillCaptureUsageFlag},
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0, 1, 2},
                .expected_extra_configured_streams = {},
            },
        // Replacing still YUV stream.
        [5] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kStillProcess},
                        {.process_mode = ProcessMode::kVideoAndStillProcess},
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .prefer_large_source = true},
                    },
                .available_formats =
                    {
                        {2592, 1944, HAL_PIXEL_FORMAT_BLOB, 15.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_BLOB, 30.0f},
                        {2592, 1944, HAL_PIXEL_FORMAT_YCBCR_420_888, 15.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_BLOB},
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
                         .usage = kStillCaptureUsageFlag},
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0, 2},
                .expected_extra_configured_streams =
                    {
                        {2592, 1944, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags | kStillCaptureUsageFlag},
                    },
            },
        // Different aspect ratios.
        [6] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess},
                    },
                .available_formats =
                    {
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1600, 1200, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 960, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 480, HAL_PIXEL_FORMAT_YCBCR_420_888, 60.0f},
                        {640, 360, HAL_PIXEL_FORMAT_YCBCR_420_888, 60.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                        {.width = 640,
                         .height = 480,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0, 1},
                .expected_extra_configured_streams =
                    {
                        {1280, 960, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags},
                    },
            },
        [7] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .prefer_large_source = true},
                    },
                .available_formats =
                    {
                        {2592, 1944, HAL_PIXEL_FORMAT_BLOB, 15.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_BLOB, 30.0f},
                        {2592, 1944, HAL_PIXEL_FORMAT_YCBCR_420_888, 15.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1600, 1200, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 960, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 480, HAL_PIXEL_FORMAT_YCBCR_420_888, 60.0f},
                        {640, 360, HAL_PIXEL_FORMAT_YCBCR_420_888, 60.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_BLOB},
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                        {.width = 640,
                         .height = 480,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0, 1, 2},
                .expected_extra_configured_streams =
                    {
                        {2592, 1944, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags | kStillCaptureUsageFlag},
                        {1600, 1200, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags},
                    },
            },
        // Still capture only.
        [8] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .prefer_large_source = true},
                        {.process_mode = ProcessMode::kVideoAndStillProcess},
                        {.process_mode = ProcessMode::kStillProcess},
                    },
                .available_formats =
                    {
                        {2592, 1944, HAL_PIXEL_FORMAT_BLOB, 15.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_BLOB, 30.0f},
                        {2592, 1944, HAL_PIXEL_FORMAT_YCBCR_420_888, 15.0f},
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_BLOB},
                    },
                .expected_configured_stream_indices = {0},
                .expected_extra_configured_streams =
                    {
                        {2592, 1944, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags | kStillCaptureUsageFlag},
                    },
            },
        // Upscaling.
        [9] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess},
                    },
                .available_formats =
                    {
                        {1920, 1080, HAL_PIXEL_FORMAT_BLOB, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_BLOB},
                    },
                .expected_configured_stream_indices = {0},
                .expected_extra_configured_streams =
                    {
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags | kStillCaptureUsageFlag},
                    },
            },
        [10] =
            {
                .helper_configs = {{.process_mode =
                                        ProcessMode::kVideoAndStillProcess,
                                    .preserve_client_video_streams = false}},
                .available_formats = {{1920, 1080,
                                       HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                                      {1280, 960,
                                       HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f}},
                .active_array_size = Size(2592, 1944),
                .streams = {{.width = 1920,
                             .height = 1080,
                             .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                            {.width = 1280,
                             .height = 960,
                             .format = HAL_PIXEL_FORMAT_YCBCR_420_888}},
                .expected_configured_stream_indices = {1},
                .expected_extra_configured_streams = {},
            },
        // Limiting max video source size.
        [11] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .prefer_large_source = true,
                         .max_enlarged_video_source_width = 1600,
                         .max_enlarged_video_source_height = 1080},
                    },
                .available_formats =
                    {
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1600, 1200, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 360, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 640,
                         .height = 360,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0},
                .expected_extra_configured_streams =
                    {
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags},
                    },
            },
        [12] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .prefer_large_source = true,
                         .max_enlarged_video_source_width = 1600,
                         .max_enlarged_video_source_height = 1080,
                         .preserve_client_video_streams = false},
                    },
                .available_formats =
                    {
                        {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1600, 1200, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 360, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1920,
                         .height = 1080,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                        {.width = 640,
                         .height = 360,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0},
                .expected_extra_configured_streams = {},
            },
        // Removing generated video streams.
        [13] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .preserve_client_video_streams = false},
                    },
                .available_formats =
                    {
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 360, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                        {.width = 640,
                         .height = 360,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0},
                .expected_extra_configured_streams = {},
            },
        [14] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .preserve_client_video_streams = false},
                    },
                .available_formats =
                    {
                        {1280, 960, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 480, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                        {.width = 640,
                         .height = 480,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {},
                .expected_extra_configured_streams =
                    {
                        {1280, 960, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags},
                    },
            },
        // Carry HW composer flag to processing stream.
        [15] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .preserve_client_video_streams = false},
                    },
                .available_formats =
                    {
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 360, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
                         .usage = GRALLOC_USAGE_HW_COMPOSER},
                    },
                .expected_configured_stream_indices = {1},
                .expected_extra_configured_streams = {},
            },
        [16] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .preserve_client_video_streams = false},
                    },
                .available_formats =
                    {
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 360, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                        {.width = 640,
                         .height = 360,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
                         .usage = GRALLOC_USAGE_HW_COMPOSER},
                    },
                .expected_configured_stream_indices = {},
                .expected_extra_configured_streams =
                    {
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags | GRALLOC_USAGE_HW_COMPOSER},
                    },
            },
        // Config to skip processing on multiple aspect ratios.
        [17] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .skip_on_multiple_aspect_ratios = true},
                    },
                .available_formats =
                    {
                        {1280, 960, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 480, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 1280,
                         .height = 720,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                        {.width = 640,
                         .height = 480,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
                         .usage = GRALLOC_USAGE_HW_COMPOSER},
                    },
                .expected_config_success = false,
            },
        // Limiting min video source size.
        [18] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .min_video_source_width = 640},
                    },
                .available_formats =
                    {
                        {1280, 960, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 480, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {320, 240, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 320,
                         .height = 240,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0},
                .expected_extra_configured_streams =
                    {
                        {640, 480, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags},
                    },
            },
        [19] =
            {
                .helper_configs =
                    {
                        {.process_mode = ProcessMode::kVideoAndStillProcess,
                         .min_video_source_height = 480},
                    },
                .available_formats =
                    {
                        {1280, 960, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {640, 480, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                        {320, 240, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
                    },
                .active_array_size = Size(2592, 1944),
                .streams =
                    {
                        {.width = 320,
                         .height = 240,
                         .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
                    },
                .expected_configured_stream_indices = {0},
                .expected_extra_configured_streams =
                    {
                        {640, 480, HAL_PIXEL_FORMAT_YCBCR_420_888,
                         kProcessStreamUsageFlags},
                    },
            },
};

INSTANTIATE_TEST_SUITE_P(,
                         StreamManipulatorHelperTestWithCase,
                         testing::ValuesIn(g_test_cases));

TEST_P(StreamManipulatorHelperTestWithCase, StreamConfig) {
  SetUpWithTestCase(GetParam());
}

const TestCase g_simple_test_case = {
    .helper_configs =
        {
            {.process_mode = ProcessMode::kVideoAndStillProcess,
             .preserve_client_video_streams = false,
             .result_metadata_tags_to_inspect = {ANDROID_SENSOR_TIMESTAMP}},
        },
    .available_formats =
        {
            {1920, 1080, HAL_PIXEL_FORMAT_BLOB, 30.0f},
            {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
            {1280, 720, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
        },
    .active_array_size = Size(2592, 1944),
    .partial_result_count = 10,
    .streams =
        {
            {.width = 1920, .height = 1080, .format = HAL_PIXEL_FORMAT_BLOB},
            {.width = 1280,
             .height = 720,
             .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
        },
    .max_buffers = 10,
    .expected_configured_stream_indices = {0, 1},
    .expected_extra_configured_streams =
        {
            {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888,
             kProcessStreamUsageFlags | kStillCaptureUsageFlag},
        },
};

const TestCase g_complex_test_case = {
    .helper_configs =
        {
            {.process_mode = ProcessMode::kVideoAndStillProcess,
             .prefer_large_source = true,
             .preserve_client_video_streams = false,
             .result_metadata_tags_to_inspect = {ANDROID_SENSOR_TIMESTAMP},
             .enable_debug_logs = true},
        },
    .available_formats =
        {
            {2592, 1944, HAL_PIXEL_FORMAT_BLOB, 15.0f},
            {1920, 1080, HAL_PIXEL_FORMAT_BLOB, 30.0f},
            {2592, 1944, HAL_PIXEL_FORMAT_YCBCR_420_888, 15.0f},
            {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
            {1600, 1200, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
            {640, 480, HAL_PIXEL_FORMAT_YCBCR_420_888, 60.0f},
            {640, 360, HAL_PIXEL_FORMAT_YCBCR_420_888, 60.0f},
        },
    .active_array_size = Size(2592, 1944),
    .partial_result_count = 10,
    .streams =
        {
            {.width = 1920, .height = 1080, .format = HAL_PIXEL_FORMAT_BLOB},
            {.width = 1920,
             .height = 1080,
             .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
             .usage = kStillCaptureUsageFlag},
            {.width = 640,
             .height = 480,
             .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
            {.width = 640,
             .height = 360,
             .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
        },
    .max_buffers = 10,
    .expected_configured_stream_indices = {0},
    .expected_extra_configured_streams =
        {
            {2592, 1944, HAL_PIXEL_FORMAT_YCBCR_420_888,
             kProcessStreamUsageFlags | kStillCaptureUsageFlag},
            {1600, 1200, HAL_PIXEL_FORMAT_YCBCR_420_888,
             kProcessStreamUsageFlags},
        },
};

const TestCase g_upscaling_test_case = {
    .helper_configs =
        {
            {.process_mode = ProcessMode::kVideoAndStillProcess,
             .preserve_client_video_streams = false},
        },
    .available_formats =
        {
            {2592, 1944, HAL_PIXEL_FORMAT_BLOB, 15.0f},
            {1920, 1080, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
            {1600, 1200, HAL_PIXEL_FORMAT_YCBCR_420_888, 30.0f},
            {640, 480, HAL_PIXEL_FORMAT_YCBCR_420_888, 60.0f},
            {640, 360, HAL_PIXEL_FORMAT_YCBCR_420_888, 60.0f},
        },
    .active_array_size = Size(2592, 1944),
    .partial_result_count = 10,
    .streams =
        {
            {.width = 2592, .height = 1944, .format = HAL_PIXEL_FORMAT_BLOB},
            {.width = 1920,
             .height = 1080,
             .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
            {.width = 640,
             .height = 480,
             .format = HAL_PIXEL_FORMAT_YCBCR_420_888},
        },
    .max_buffers = 10,
    .expected_configured_stream_indices = {0},
    .expected_extra_configured_streams =
        {
            {1600, 1200, HAL_PIXEL_FORMAT_YCBCR_420_888,
             kProcessStreamUsageFlags | kStillCaptureUsageFlag},
            {1600, 1200, HAL_PIXEL_FORMAT_YCBCR_420_888,
             kProcessStreamUsageFlags},
        },
};

void ValidateResult(
    const Camera3CaptureDescriptor& result,
    uint32_t expected_frame_number,
    const std::vector<
        std::tuple<const camera3_stream_t*, buffer_handle_t, int /*status*/>>
        expected_stream_buffers) {
  EXPECT_EQ(result.frame_number(), expected_frame_number);
  EXPECT_EQ(result.num_output_buffers(), expected_stream_buffers.size());
  for (auto& [s, b, e] : expected_stream_buffers) {
    EXPECT_THAT(FindBuffer(result, s), Pair(b, e))
        << "result validation failed on frame " << expected_frame_number
        << ", stream " << GetDebugString(s);
  }
}

const Rect<float> kCropFull(0.0f, 0.0, 1.0f, 1.0f);
const Rect<float> kCrop4x3To16x9(0.0f, 0.125f, 1.0f, 0.75f);

TEST_F(StreamManipulatorHelperTest, SimpleProcessing) {
  SetUpWithTestCase(g_simple_test_case);
  camera3_stream_t* blob_stream = configured_streams_[0];
  camera3_stream_t* video_stream = configured_streams_[1];
  camera3_stream_t* still_stream = extra_configured_streams_[0];

  AllocateRequestBuffers();
  const ScopedBufferHandle& blob = request_buffers_[blob_stream][0];
  const ScopedBufferHandle& video_output = request_buffers_[video_stream][0];

  // Process video.
  {
    const uint32_t fn = 1;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{video_stream, video_output.get()}});
    SendRequest(&request);

    EXPECT_EQ(request.num_output_buffers(), 1);
    buffer_handle_t video_input = FindBuffer(request, video_stream).first;
    ASSERT_NE(video_input, nullptr);
    EXPECT_NE(video_input, *video_output);

    SendResult(
        MakeResult(fn, {{video_stream, &video_input}}, partial_result_count_));

    const ProcessTask& task = manipulator(0).GetProcessTask(fn, video_stream);
    EXPECT_EQ(task.input_buffer(), video_input);
    EXPECT_EQ(task.output_buffer(), *video_output);

    manipulator(0).FinishProcessTask(fn, video_stream);
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{video_stream, *video_output, CAMERA3_BUFFER_STATUS_OK}});
  }

  // Process still capture.
  {
    const uint32_t fn = 2;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{blob_stream, blob.get()}});
    SendRequest(&request);

    EXPECT_EQ(request.num_output_buffers(), 2);
    EXPECT_EQ(FindBuffer(request, blob_stream).first, *blob);
    buffer_handle_t still_input = FindBuffer(request, still_stream).first;
    ASSERT_NE(still_input, nullptr);

    SendResult(MakeResult(
        fn, {{blob_stream, blob.get()}, {still_stream, &still_input}},
        partial_result_count_));

    const ProcessTask& task = manipulator(0).GetProcessTask(fn, still_stream);
    EXPECT_EQ(task.input_buffer(), still_input);

    manipulator(0).FinishProcessTask(fn, still_stream);
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{blob_stream, *blob, CAMERA3_BUFFER_STATUS_OK}});
  }

  // Process video and still capture in one request.
  {
    const uint32_t fn = 3;
    Camera3CaptureDescriptor request = MakeRequest(
        fn, {{blob_stream, blob.get()}, {video_stream, video_output.get()}});
    SendRequest(&request);

    EXPECT_EQ(request.num_output_buffers(), 3);
    EXPECT_EQ(FindBuffer(request, blob_stream).first, *blob);
    buffer_handle_t still_input = FindBuffer(request, still_stream).first;
    buffer_handle_t video_input = FindBuffer(request, video_stream).first;
    ASSERT_NE(still_input, nullptr);
    ASSERT_NE(video_input, nullptr);

    SendResult(MakeResult(fn,
                          {{blob_stream, blob.get()},
                           {still_stream, &still_input},
                           {video_stream, &video_input}},
                          partial_result_count_));

    const ProcessTask& still_task =
        manipulator(0).GetProcessTask(fn, still_stream);
    EXPECT_EQ(still_task.input_buffer(), still_input);

    const ProcessTask& video_task =
        manipulator(0).GetProcessTask(fn, video_stream);
    EXPECT_EQ(video_task.input_buffer(), video_input);
    EXPECT_EQ(video_task.output_buffer(), *video_output);

    manipulator(0).FinishProcessTask(fn, video_stream);
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{video_stream, *video_output, CAMERA3_BUFFER_STATUS_OK}});

    manipulator(0).FinishProcessTask(fn, still_stream);
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{blob_stream, *blob, CAMERA3_BUFFER_STATUS_OK}});
  }
}

TEST_F(StreamManipulatorHelperTest, ProcessOnLargerSourceStream) {
  SetUpWithTestCase(g_complex_test_case);
  camera3_stream_t* blob_stream = &streams_[0];
  camera3_stream_t* still_output_stream = &streams_[1];
  camera3_stream_t* video_output_streams[] = {&streams_[2], &streams_[3]};
  camera3_stream_t* still_input_stream = extra_configured_streams_[0];
  camera3_stream_t* video_input_stream = extra_configured_streams_[1];

  AllocateRequestBuffers();
  const ScopedBufferHandle& blob = request_buffers_[blob_stream][0];
  const ScopedBufferHandle& still_output =
      request_buffers_[still_output_stream][0];
  const ScopedBufferHandle& video0_output =
      request_buffers_[video_output_streams[0]][0];
  const ScopedBufferHandle& video1_output =
      request_buffers_[video_output_streams[1]][0];

  // Process video
  {
    const uint32_t fn = 1;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{video_output_streams[0], video0_output.get()},
                         {video_output_streams[1], video1_output.get()}});
    SendRequest(&request);
    EXPECT_EQ(request.num_output_buffers(), 1);
    buffer_handle_t video_input = FindBuffer(request, video_input_stream).first;
    EXPECT_NE(video_input, nullptr);

    SendResult(MakeResult(fn, {{video_input_stream, &video_input}},
                          partial_result_count_));

    const ProcessTask& task =
        manipulator(0).GetProcessTask(fn, video_input_stream);
    EXPECT_EQ(task.input_buffer(), video_input);
    EXPECT_EQ(task.output_buffer(), *video0_output);

    manipulator(0).FinishProcessTask(fn, video_input_stream);
    ValidateResult(
        TakeLastReturnedResult(), fn,
        {{video_output_streams[0], *video0_output, CAMERA3_BUFFER_STATUS_OK},
         {video_output_streams[1], *video1_output, CAMERA3_BUFFER_STATUS_OK}});
    EXPECT_TRUE(manipulator(0).HasCropScaledBuffer(
        *video0_output, *video1_output, kCrop4x3To16x9));
  }

  // Process still capture.
  {
    const uint32_t fn = 2;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{blob_stream, blob.get()}});
    SendRequest(&request);
    EXPECT_EQ(request.num_output_buffers(), 2);
    EXPECT_EQ(FindBuffer(request, blob_stream).first, *blob);
    buffer_handle_t still_input = FindBuffer(request, still_input_stream).first;
    EXPECT_NE(still_input, nullptr);

    SendResult(MakeResult(
        fn, {{blob_stream, blob.get()}, {still_input_stream, &still_input}},
        partial_result_count_));

    const ProcessTask& task =
        manipulator(0).GetProcessTask(fn, still_input_stream);
    EXPECT_EQ(task.input_buffer(), still_input);
    EXPECT_EQ(CameraBufferManager::GetWidth(task.output_buffer()),
              still_output_stream->width);
    EXPECT_EQ(CameraBufferManager::GetHeight(task.output_buffer()),
              still_output_stream->height);

    manipulator(0).FinishProcessTask(fn, still_input_stream);
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{blob_stream, *blob, CAMERA3_BUFFER_STATUS_OK}});
  }

  // Process still capture with still YUV output.
  {
    const uint32_t fn = 3;
    Camera3CaptureDescriptor request = MakeRequest(
        fn,
        {{blob_stream, blob.get()}, {still_output_stream, still_output.get()}});
    SendRequest(&request);
    EXPECT_EQ(request.num_output_buffers(), 2);
    EXPECT_EQ(FindBuffer(request, blob_stream).first, *blob);
    buffer_handle_t still_input = FindBuffer(request, still_input_stream).first;
    EXPECT_NE(still_input, nullptr);

    SendResult(MakeResult(
        fn, {{blob_stream, blob.get()}, {still_input_stream, &still_input}},
        partial_result_count_));

    const ProcessTask& task =
        manipulator(0).GetProcessTask(fn, still_input_stream);
    EXPECT_EQ(task.input_buffer(), still_input);
    EXPECT_EQ(task.output_buffer(), *still_output);

    manipulator(0).FinishProcessTask(fn, still_input_stream);
    ValidateResult(
        TakeLastReturnedResult(), fn,
        {{still_output_stream, *still_output, CAMERA3_BUFFER_STATUS_OK},
         {blob_stream, *blob, CAMERA3_BUFFER_STATUS_OK}});
  }
}

TEST_F(StreamManipulatorHelperTest, UpscalingProcessedStream) {
  SetUpWithTestCase(g_upscaling_test_case);
  camera3_stream_t* blob_stream = &streams_[0];
  camera3_stream_t* video_output_streams[] = {&streams_[1], &streams_[2]};
  camera3_stream_t* still_input_stream = extra_configured_streams_[0];
  camera3_stream_t* video_input_stream = extra_configured_streams_[1];

  AllocateRequestBuffers();
  const ScopedBufferHandle& blob = request_buffers_[blob_stream][0];
  const ScopedBufferHandle& video0_output =
      request_buffers_[video_output_streams[0]][0];
  const ScopedBufferHandle& video1_output =
      request_buffers_[video_output_streams[1]][0];

  // Process video
  {
    const uint32_t fn = 1;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{video_output_streams[0], video0_output.get()},
                         {video_output_streams[1], video1_output.get()}});
    SendRequest(&request);
    EXPECT_EQ(request.num_output_buffers(), 1);
    buffer_handle_t video_input = FindBuffer(request, video_input_stream).first;
    EXPECT_NE(video_input, nullptr);

    SendResult(MakeResult(fn, {{video_input_stream, &video_input}},
                          partial_result_count_));

    const ProcessTask& task =
        manipulator(0).GetProcessTask(fn, video_input_stream);
    EXPECT_EQ(task.input_buffer(), video_input);
    buffer_handle_t task_output = task.output_buffer();
    EXPECT_NE(task_output, *video0_output);
    EXPECT_EQ(CameraBufferManager::GetWidth(task_output),
              video_input_stream->width);
    EXPECT_EQ(CameraBufferManager::GetHeight(task_output),
              video_input_stream->height);

    manipulator(0).FinishProcessTask(fn, video_input_stream);
    ValidateResult(
        TakeLastReturnedResult(), fn,
        {{video_output_streams[0], *video0_output, CAMERA3_BUFFER_STATUS_OK},
         {video_output_streams[1], *video1_output, CAMERA3_BUFFER_STATUS_OK}});
    EXPECT_TRUE(manipulator(0).HasCropScaledBuffer(task_output, *video0_output,
                                                   kCrop4x3To16x9));
    EXPECT_TRUE(manipulator(0).HasCropScaledBuffer(task_output, *video1_output,
                                                   kCropFull));
  }

  // Process still capture.
  {
    const uint32_t fn = 2;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{blob_stream, blob.get()}});
    SendRequest(&request);
    EXPECT_EQ(request.num_output_buffers(), 2);
    EXPECT_EQ(FindBuffer(request, blob_stream).first, *blob);
    buffer_handle_t still_input = FindBuffer(request, still_input_stream).first;
    EXPECT_NE(still_input, nullptr);

    SendResult(MakeResult(
        fn, {{blob_stream, blob.get()}, {still_input_stream, &still_input}},
        partial_result_count_));

    const ProcessTask& task =
        manipulator(0).GetProcessTask(fn, still_input_stream);
    EXPECT_EQ(task.input_buffer(), still_input);
    EXPECT_EQ(CameraBufferManager::GetWidth(task.output_buffer()),
              still_input_stream->width);
    EXPECT_EQ(CameraBufferManager::GetHeight(task.output_buffer()),
              still_input_stream->height);

    manipulator(0).FinishProcessTask(fn, still_input_stream);
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{blob_stream, *blob, CAMERA3_BUFFER_STATUS_OK}});
  }
}

TEST_F(StreamManipulatorHelperTest, RuntimeBypassWithCopy) {
  SetUpWithTestCase(g_complex_test_case);
  camera3_stream_t* blob_stream = &streams_[0];
  camera3_stream_t* still_output_stream = &streams_[1];
  camera3_stream_t* video_output_streams[] = {&streams_[2], &streams_[3]};
  camera3_stream_t* still_input_stream = extra_configured_streams_[0];
  camera3_stream_t* video_input_stream = extra_configured_streams_[1];

  AllocateRequestBuffers();
  const ScopedBufferHandle& blob = request_buffers_[blob_stream][0];
  const ScopedBufferHandle& still_output =
      request_buffers_[still_output_stream][0];
  const ScopedBufferHandle& video_output =
      request_buffers_[video_output_streams[0]][0];

  manipulator(0).SetBypassProcess(true);

  // Bypass video stream.
  {
    const uint32_t fn = 1;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{video_output_streams[0], video_output.get()}});
    SendRequest(&request);
    EXPECT_EQ(request.num_output_buffers(), 1);
    buffer_handle_t video_input = FindBuffer(request, video_input_stream).first;
    EXPECT_NE(video_input, nullptr);

    SendResult(MakeResult(fn, {{video_input_stream, &video_input}},
                          partial_result_count_));
    ValidateResult(
        TakeLastReturnedResult(), fn,
        {{video_output_streams[0], *video_output, CAMERA3_BUFFER_STATUS_OK}});
    EXPECT_TRUE(manipulator(0).HasCropScaledBuffer(video_input, *video_output,
                                                   kCropFull));
  }

  // Bypass BLOB and replace still YUV stream without processing.
  {
    const uint32_t fn = 2;
    Camera3CaptureDescriptor request = MakeRequest(
        fn,
        {{still_output_stream, still_output.get()}, {blob_stream, blob.get()}});
    SendRequest(&request);
    EXPECT_EQ(request.num_output_buffers(), 2);
    EXPECT_EQ(FindBuffer(request, blob_stream).first, *blob);
    buffer_handle_t still_input = FindBuffer(request, still_input_stream).first;
    EXPECT_NE(still_input, nullptr);

    SendResult(MakeResult(
        fn, {{still_input_stream, &still_input}, {blob_stream, blob.get()}},
        partial_result_count_));
    ValidateResult(
        TakeLastReturnedResult(), fn,
        {{still_output_stream, *still_output, CAMERA3_BUFFER_STATUS_OK},
         {blob_stream, *blob, CAMERA3_BUFFER_STATUS_OK}});
    EXPECT_TRUE(manipulator(0).HasCropScaledBuffer(still_input, *still_output,
                                                   kCrop4x3To16x9));
  }
}

TEST_F(StreamManipulatorHelperTest, RuntimeBypassWithoutCopy) {
  TestCase test_case = g_complex_test_case;
  test_case.helper_configs[0].preserve_client_video_streams = true;
  test_case.expected_configured_stream_indices = {0, 2, 3};
  SetUpWithTestCase(test_case);
  camera3_stream_t* video_streams[] = {&streams_[2], &streams_[3]};

  AllocateRequestBuffers();
  const ScopedBufferHandle& video_output =
      request_buffers_[video_streams[0]][0];

  manipulator(0).SetBypassProcess(true);

  {
    const uint32_t fn = 1;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{video_streams[0], video_output.get()}});
    SendRequest(&request);
    EXPECT_EQ(request.num_output_buffers(), 1);
    buffer_handle_t video_input = FindBuffer(request, video_streams[0]).first;
    EXPECT_EQ(video_input, *video_output);

    SendResult(MakeResult(fn, {{video_streams[0], &video_input}},
                          partial_result_count_));
    ValidateResult(
        TakeLastReturnedResult(), fn,
        {{video_streams[0], *video_output, CAMERA3_BUFFER_STATUS_OK}});
  }
}

class InstanceCounter : public StreamManipulatorHelper::PrivateContext {
 public:
  explicit InstanceCounter(int& count) : count_(count) { ++count_; }
  InstanceCounter(const InstanceCounter&) = delete;
  InstanceCounter& operator=(const InstanceCounter&) = delete;
  ~InstanceCounter() override { --count_; }

 private:
  int& count_;
};

TEST_F(StreamManipulatorHelperTest, CaptureContextLifetime) {
  SetUpWithTestCase(g_simple_test_case);
  camera3_stream_t* blob_stream = configured_streams_[0];
  camera3_stream_t* video_stream = configured_streams_[1];
  camera3_stream_t* still_stream = extra_configured_streams_[0];
  ASSERT_GE(partial_result_count_, 2);
  ASSERT_GE(max_buffers_, 10);

  AllocateRequestBuffers();
  const std::vector<ScopedBufferHandle>& blobs = request_buffers_[blob_stream];
  const std::vector<ScopedBufferHandle>& video_outputs =
      request_buffers_[video_stream];

  int ctx_count = 0;
  manipulator(0).SetPrivateContextBuilder(
      [&](uint32_t) { return std::make_unique<InstanceCounter>(ctx_count); });

  std::vector<buffer_handle_t> still_inputs;
  std::vector<buffer_handle_t> video_inputs;
  for (uint32_t fn = 1; fn <= 10; ++fn) {
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{blob_stream, blobs.back().get()},
                         {video_stream, video_outputs.back().get()}});
    SendRequest(&request);
    still_inputs.push_back(FindBuffer(request, still_stream).first);
    video_inputs.push_back(FindBuffer(request, video_stream).first);
  }
  EXPECT_EQ(ctx_count, 10);

  // Context removal by finishing process task.
  {
    const uint32_t fn = 1;
    Camera3CaptureDescriptor result =
        MakeResult(fn,
                   {{video_stream, &video_inputs[fn - 1]},
                    {still_stream, &still_inputs[fn - 1]},
                    {blob_stream, blobs[fn - 1].get()}},
                   partial_result_count_);
    EXPECT_TRUE(result.UpdateMetadata<int64_t>(ANDROID_SENSOR_TIMESTAMP,
                                               std::array<int64_t, 1>{111}));
    SendResult(std::move(result));
    manipulator(0).FinishProcessTask(fn, still_stream);
    EXPECT_EQ(ctx_count, 10);
    manipulator(0).FinishProcessTask(fn, video_stream);
    EXPECT_EQ(ctx_count, 9);
  }

  // Context removal by receiving the last metadata.
  {
    const uint32_t fn = 2;
    {
      Camera3CaptureDescriptor result =
          MakeResult(fn,
                     {{video_stream, &video_inputs[fn - 1]},
                      {still_stream, &still_inputs[fn - 1]},
                      {blob_stream, blobs[fn - 1].get()}},
                     1);
      EXPECT_TRUE(result.UpdateMetadata<int64_t>(ANDROID_SENSOR_TIMESTAMP,
                                                 std::array<int64_t, 1>{222}));
      SendResult(std::move(result));
    }
    manipulator(0).FinishProcessTask(fn, video_stream);
    manipulator(0).FinishProcessTask(fn, still_stream);
    EXPECT_EQ(ctx_count, 9);
    {
      Camera3CaptureDescriptor result =
          MakeResult(fn, {}, partial_result_count_);
      EXPECT_TRUE(result.UpdateMetadata<float>(ANDROID_LENS_APERTURE,
                                               std::array<float, 1>{0.02f}));
      SendResult(std::move(result));
    }
    EXPECT_EQ(ctx_count, 8);
  }

  // Context removal by finishing still capture.
  {
    const uint32_t fn = 3;
    {
      Camera3CaptureDescriptor result =
          MakeResult(fn,
                     {{video_stream, &video_inputs[fn - 1]},
                      {still_stream, &still_inputs[fn - 1]}},
                     partial_result_count_);
      EXPECT_TRUE(result.UpdateMetadata<int64_t>(ANDROID_SENSOR_TIMESTAMP,
                                                 std::array<int64_t, 1>{333}));
      SendResult(std::move(result));
    }
    manipulator(0).FinishProcessTask(fn, video_stream);
    {
      Camera3CaptureDescriptor result =
          MakeResult(fn, {{blob_stream, blobs[fn - 1].get()}}, 0);
      EXPECT_TRUE(result.UpdateMetadata<float>(ANDROID_LENS_APERTURE,
                                               std::array<float, 1>{0.02f}));
      SendResult(std::move(result));
    }
    EXPECT_EQ(ctx_count, 8);
    manipulator(0).FinishProcessTask(fn, still_stream);
    EXPECT_EQ(ctx_count, 7);
  }
}

TEST_F(StreamManipulatorHelperTest, ProcessFail) {
  SetUpWithTestCase(g_simple_test_case);
  camera3_stream_t* blob_stream = configured_streams_[0];
  camera3_stream_t* video_stream = configured_streams_[1];
  camera3_stream_t* still_stream = extra_configured_streams_[0];

  AllocateRequestBuffers();
  const ScopedBufferHandle& blob = request_buffers_[blob_stream][0];
  const ScopedBufferHandle& video_output = request_buffers_[video_stream][0];

  // Video processing fails.
  {
    const uint32_t fn = 1;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{video_stream, video_output.get()}});
    SendRequest(&request);
    buffer_handle_t video_input = FindBuffer(request, video_stream).first;

    SendResult(
        MakeResult(fn, {{video_stream, &video_input}}, partial_result_count_));
    manipulator(0).GetProcessTask(fn, video_stream).Fail();
    manipulator(0).FinishProcessTask(fn, video_stream);
    ValidateResult(
        TakeLastReturnedResult(), fn,
        {{video_stream, *video_output, CAMERA3_BUFFER_STATUS_ERROR}});
  }

  // Still processing fails.
  {
    const uint32_t fn = 2;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{blob_stream, blob.get()}});
    SendRequest(&request);
    buffer_handle_t still_input = FindBuffer(request, still_stream).first;

    SendResult(MakeResult(
        fn, {{blob_stream, blob.get()}, {still_stream, &still_input}},
        partial_result_count_));
    manipulator(0).GetProcessTask(fn, still_stream).Fail();
    manipulator(0).FinishProcessTask(fn, still_stream);
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{blob_stream, *blob, CAMERA3_BUFFER_STATUS_ERROR}});
  }
}

TEST_F(StreamManipulatorHelperTest, PropagateBufferErrors) {
  SetUpWithTestCase(g_complex_test_case);
  camera3_stream_t* blob_stream = &streams_[0];
  camera3_stream_t* still_output_stream = &streams_[1];
  camera3_stream_t* video_output_streams[] = {&streams_[2], &streams_[3]};
  camera3_stream_t* still_input_stream = extra_configured_streams_[0];
  camera3_stream_t* video_input_stream = extra_configured_streams_[1];

  AllocateRequestBuffers();
  const ScopedBufferHandle& blob = request_buffers_[blob_stream][0];
  const ScopedBufferHandle& still_output =
      request_buffers_[still_output_stream][0];
  const ScopedBufferHandle& video_output =
      request_buffers_[video_output_streams[0]][0];

  // Buffer error on video processing stream.
  {
    const uint32_t fn = 1;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{video_output_streams[0], video_output.get()}});
    SendRequest(&request);
    buffer_handle_t video_input = FindBuffer(request, video_input_stream).first;

    SendResult(MakeResult(fn, {{video_input_stream, &video_input}},
                          partial_result_count_, CAMERA3_BUFFER_STATUS_ERROR));
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{video_output_streams[0], *video_output,
                     CAMERA3_BUFFER_STATUS_ERROR}});
  }

  // Buffer error on still processing stream.
  {
    const uint32_t fn = 2;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{blob_stream, blob.get()}});
    SendRequest(&request);
    buffer_handle_t still_input = FindBuffer(request, still_input_stream).first;

    SendResult(MakeResult(fn, {{blob_stream, blob.get()}}, 0));
    SendResult(MakeResult(fn, {{still_input_stream, &still_input}},
                          partial_result_count_, CAMERA3_BUFFER_STATUS_ERROR));
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{blob_stream, *blob, CAMERA3_BUFFER_STATUS_ERROR}});
  }
  {
    const uint32_t fn = 3;
    Camera3CaptureDescriptor request = MakeRequest(
        fn,
        {{blob_stream, blob.get()}, {still_output_stream, still_output.get()}});
    SendRequest(&request);
    buffer_handle_t still_input = FindBuffer(request, still_input_stream).first;

    SendResult(MakeResult(fn, {{still_input_stream, &still_input}}, 0,
                          CAMERA3_BUFFER_STATUS_ERROR));
    ValidateResult(
        TakeLastReturnedResult(), fn,
        {{still_output_stream, *still_output, CAMERA3_BUFFER_STATUS_ERROR}});

    SendResult(
        MakeResult(fn, {{blob_stream, blob.get()}}, partial_result_count_));
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{blob_stream, *blob, CAMERA3_BUFFER_STATUS_ERROR}});
  }

  // Buffer error on BLOB stream.
  {
    const uint32_t fn = 4;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{blob_stream, blob.get()}});
    SendRequest(&request);
    buffer_handle_t still_input = FindBuffer(request, still_input_stream).first;

    SendResult(MakeResult(fn, {{blob_stream, blob.get()}}, 0,
                          CAMERA3_BUFFER_STATUS_ERROR));
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{blob_stream, *blob, CAMERA3_BUFFER_STATUS_ERROR}});

    SendResult(MakeResult(fn, {{still_input_stream, &still_input}},
                          partial_result_count_));
    manipulator(0).FinishProcessTask(fn, still_input_stream);
    ValidateResult(TakeLastReturnedResult(), fn, {});
  }
  {
    const uint32_t fn = 5;
    Camera3CaptureDescriptor request = MakeRequest(
        fn,
        {{blob_stream, blob.get()}, {still_output_stream, still_output.get()}});
    SendRequest(&request);
    buffer_handle_t still_input = FindBuffer(request, still_input_stream).first;

    SendResult(MakeResult(fn, {{still_input_stream, &still_input}},
                          partial_result_count_));
    manipulator(0).FinishProcessTask(fn, still_input_stream);
    SendResult(MakeResult(fn, {{blob_stream, blob.get()}}, 0,
                          CAMERA3_BUFFER_STATUS_ERROR));
    ValidateResult(
        TakeLastReturnedResult(), fn,
        {{still_output_stream, *still_output, CAMERA3_BUFFER_STATUS_OK},
         {blob_stream, *blob, CAMERA3_BUFFER_STATUS_ERROR}});
  }
}

void ValidateMessages(
    base::span<const camera3_notify_msg_t> messages,
    const std::vector<std::tuple<int /*error_code*/,
                                 uint32_t /*frame_number*/,
                                 const camera3_stream_t* /*error_stream*/>>&
        expected_messages) {
  std::vector<std::tuple<int, uint32_t, const camera3_stream_t*>> result;
  for (auto& m : messages) {
    ASSERT_EQ(m.type, CAMERA3_MSG_ERROR);
    const camera3_error_msg_t& err = m.message.error;
    result.push_back(
        std::make_tuple(err.error_code, err.frame_number, err.error_stream));
  }
  EXPECT_THAT(result, UnorderedElementsAreArray(expected_messages));
}

TEST_F(StreamManipulatorHelperTest, NotifyRequestError) {
  SetUpWithTestCase(g_complex_test_case);
  camera3_stream_t* blob_stream = &streams_[0];
  camera3_stream_t* video_output_streams[] = {&streams_[2], &streams_[3]};
  camera3_stream_t* still_input_stream = extra_configured_streams_[0];
  camera3_stream_t* video_input_stream = extra_configured_streams_[1];

  AllocateRequestBuffers();
  const ScopedBufferHandle& blob = request_buffers_[blob_stream][0];
  const ScopedBufferHandle& video0_output =
      request_buffers_[video_output_streams[0]][0];
  const ScopedBufferHandle& video1_output =
      request_buffers_[video_output_streams[1]][0];

  // Request error with video streams.
  {
    const uint32_t fn = 1;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{video_output_streams[0], video0_output.get()},
                         {video_output_streams[1], video1_output.get()}});
    SendRequest(&request);
    buffer_handle_t video_input = FindBuffer(request, video_input_stream).first;

    Notify(camera3_notify_msg_t{
        .type = CAMERA3_MSG_ERROR,
        .message = {.error = {.frame_number = fn,
                              .error_code = CAMERA3_MSG_ERROR_REQUEST}}});
    ValidateMessages(TakeNotifiedMessages(),
                     {{CAMERA3_MSG_ERROR_REQUEST, fn, nullptr}});

    // Check buffers with error status can still be sent.
    SendResult(MakeResult(fn, {{video_input_stream, &video_input}},
                          partial_result_count_, CAMERA3_BUFFER_STATUS_ERROR));
  }

  // Request error with still capture.
  {
    const uint32_t fn = 2;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{blob_stream, blob.get()}});
    SendRequest(&request);
    buffer_handle_t still_input = FindBuffer(request, still_input_stream).first;

    Notify(camera3_notify_msg_t{
        .type = CAMERA3_MSG_ERROR,
        .message = {.error = {.frame_number = fn,
                              .error_code = CAMERA3_MSG_ERROR_REQUEST}}});
    ValidateMessages(TakeNotifiedMessages(),
                     {{CAMERA3_MSG_ERROR_REQUEST, fn, nullptr}});

    SendResult(MakeResult(
        fn, {{blob_stream, blob.get()}, {still_input_stream, &still_input}},
        partial_result_count_, CAMERA3_BUFFER_STATUS_ERROR));
  }
}

TEST_F(StreamManipulatorHelperTest, NotifyResultError) {
  SetUpWithTestCase(g_simple_test_case);
  camera3_stream_t* video_stream = configured_streams_[1];

  AllocateRequestBuffers();
  const ScopedBufferHandle& video_output = request_buffers_[video_stream][0];

  {
    const uint32_t fn = 1;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{video_stream, video_output.get()}});
    SendRequest(&request);
    buffer_handle_t video_input = FindBuffer(request, video_stream).first;

    Notify(camera3_notify_msg_t{
        .type = CAMERA3_MSG_ERROR,
        .message = {.error = {.frame_number = fn,
                              .error_code = CAMERA3_MSG_ERROR_RESULT}}});
    ValidateMessages(TakeNotifiedMessages(),
                     {{CAMERA3_MSG_ERROR_RESULT, fn, nullptr}});

    // Check process task is still sent without the required metadata.
    SendResult(MakeResult(fn, {{video_stream, &video_input}}, 0));
    manipulator(0).FinishProcessTask(fn, video_stream);
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{video_stream, *video_output, CAMERA3_BUFFER_STATUS_OK}});
  }
}

TEST_F(StreamManipulatorHelperTest, NotifyBufferError) {
  SetUpWithTestCase(g_complex_test_case);
  camera3_stream_t* blob_stream = &streams_[0];
  camera3_stream_t* still_output_stream = &streams_[1];
  camera3_stream_t* video_output_streams[] = {&streams_[2], &streams_[3]};
  camera3_stream_t* still_input_stream = extra_configured_streams_[0];
  camera3_stream_t* video_input_stream = extra_configured_streams_[1];

  AllocateRequestBuffers();
  const ScopedBufferHandle& blob = request_buffers_[blob_stream][0];
  const ScopedBufferHandle& still_output =
      request_buffers_[still_output_stream][0];
  const ScopedBufferHandle& video0_output =
      request_buffers_[video_output_streams[0]][0];
  const ScopedBufferHandle& video1_output =
      request_buffers_[video_output_streams[1]][0];

  // Notify buffer error on video processing stream.
  {
    const uint32_t fn = 1;
    Camera3CaptureDescriptor request =
        MakeRequest(fn, {{video_output_streams[0], video0_output.get()},
                         {video_output_streams[1], video1_output.get()}});
    SendRequest(&request);
    buffer_handle_t video_input = FindBuffer(request, video_input_stream).first;

    Notify(camera3_notify_msg_t{
        .type = CAMERA3_MSG_ERROR,
        .message = {.error = {.frame_number = fn,
                              .error_stream = video_input_stream,
                              .error_code = CAMERA3_MSG_ERROR_BUFFER}}});
    ValidateMessages(TakeNotifiedMessages(),
                     {{CAMERA3_MSG_ERROR_BUFFER, fn, video_output_streams[0]},
                      {CAMERA3_MSG_ERROR_BUFFER, fn, video_output_streams[1]}});

    // Check buffers with error status can still be sent.
    SendResult(MakeResult(fn, {{video_input_stream, &video_input}},
                          partial_result_count_, CAMERA3_BUFFER_STATUS_ERROR));
  }

  // Notify buffer error on still processing stream.
  {
    const uint32_t fn = 2;
    Camera3CaptureDescriptor request = MakeRequest(
        fn,
        {{blob_stream, blob.get()}, {still_output_stream, still_output.get()}});
    SendRequest(&request);
    buffer_handle_t still_input = FindBuffer(request, still_input_stream).first;

    Notify(camera3_notify_msg_t{
        .type = CAMERA3_MSG_ERROR,
        .message = {.error = {.frame_number = fn,
                              .error_stream = still_input_stream,
                              .error_code = CAMERA3_MSG_ERROR_BUFFER}}});
    ValidateMessages(TakeNotifiedMessages(),
                     {{CAMERA3_MSG_ERROR_BUFFER, fn, still_output_stream}});

    SendResult(
        MakeResult(fn, {{blob_stream, blob.get()}}, partial_result_count_));
    ValidateResult(TakeLastReturnedResult(), fn,
                   {{blob_stream, *blob, CAMERA3_BUFFER_STATUS_ERROR}});

    SendResult(MakeResult(fn, {{still_input_stream, &still_input}}, 0,
                          CAMERA3_BUFFER_STATUS_ERROR));
  }

  // Notify buffer error on BLOB stream.
  {
    const uint32_t fn = 3;
    Camera3CaptureDescriptor request = MakeRequest(
        fn,
        {{blob_stream, blob.get()}, {still_output_stream, still_output.get()}});
    SendRequest(&request);
    buffer_handle_t still_input = FindBuffer(request, still_input_stream).first;

    Notify(camera3_notify_msg_t{
        .type = CAMERA3_MSG_ERROR,
        .message = {.error = {.frame_number = fn,
                              .error_stream = blob_stream,
                              .error_code = CAMERA3_MSG_ERROR_BUFFER}}});
    ValidateMessages(TakeNotifiedMessages(),
                     {{CAMERA3_MSG_ERROR_BUFFER, fn, blob_stream}});

    SendResult(MakeResult(fn, {{still_input_stream, &still_input}},
                          partial_result_count_));
    manipulator(0).FinishProcessTask(fn, still_input_stream);
    ValidateResult(
        TakeLastReturnedResult(), fn,
        {{still_output_stream, *still_output, CAMERA3_BUFFER_STATUS_OK}});

    SendResult(MakeResult(fn, {{blob_stream, blob.get()}}, 0));
  }
}

}  // namespace

scoped_refptr<base::SingleThreadTaskRunner> StreamManipulator::GetTaskRunner() {
  NOTREACHED();
  return nullptr;
}

ScopedMapping::~ScopedMapping() {
  NOTREACHED();
}

// Fake buffer implementation.
ScopedBufferHandle CameraBufferManager::AllocateScopedBuffer(size_t width,
                                                             size_t height,
                                                             uint32_t format,
                                                             uint32_t usage) {
  auto handle = new camera_buffer_handle_t();
  handle->magic = kCameraBufferMagic;
  handle->width = width;
  handle->height = height;
  handle->hal_pixel_format = format;
  handle->hal_usage_flags = usage;

  return ScopedBufferHandle(new buffer_handle_t{&handle->base});
}

void BufferHandleDeleter::operator()(buffer_handle_t* handle) {
  if (handle != nullptr) {
    CHECK_NE(*handle, nullptr);
    delete *handle;
    delete handle;
  }
}

uint32_t CameraBufferManager::GetWidth(buffer_handle_t buffer) {
  return camera_buffer_handle_t::FromBufferHandle(buffer)->width;
}

uint32_t CameraBufferManager::GetHeight(buffer_handle_t buffer) {
  return camera_buffer_handle_t::FromBufferHandle(buffer)->height;
}

}  // namespace cros

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  base::CommandLine::Init(argc, argv);
  TestTimeouts::Initialize();

  return RUN_ALL_TESTS();
}
