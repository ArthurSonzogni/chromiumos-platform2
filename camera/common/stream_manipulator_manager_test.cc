/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/stream_manipulator_manager.h"

#include <cstdint>
#include <list>
#include <optional>
#include <utility>
#include <vector>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/numerics/safe_conversions.h>
#include <base/numerics/safe_math.h>
#include <base/synchronization/waitable_event.h>
#include <base/time/time.h>
#include <mojo/core/embedder/embedder.h>

#if USE_CAMERA_DIAGNOSTICS
#include <mojo/public/cpp/system/buffer.h>

#include "common/camera_diagnostics_client.h"
#include "common/diagnostics_stream_manipulator.h"
#endif

// gtest's internal typedef of None and Bool conflicts with the None and Bool
// macros in X11/X.h (https://github.com/google/googletest/issues/371).
// X11/X.h is pulled in by the GL headers we include.
#pragma push_macro("None")
#pragma push_macro("Bool")
#undef None
#undef Bool
#include <gtest/gtest.h>
#pragma pop_macro("None")
#pragma pop_macro("Bool")

namespace cros {

namespace tests {

namespace {

constexpr int kCaptureRequestTimeoutMs = 1000;

class FakeStreamManipulator : public StreamManipulator {
 public:
  explicit FakeStreamManipulator(bool use_thread,
                                 uint64_t fake_processing_time_us = 0)
      : use_thread_(use_thread),
        fake_processing_time_us_(fake_processing_time_us),
        thread_("StreamManipulatorThread") {
    CHECK(thread_.Start());
  }

  // Implementations of StreamManipulator.

  bool Initialize(const camera_metadata_t* static_info,
                  Callbacks callbacks) override {
    callbacks_ = std::move(callbacks);
    return true;
  }

  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override {
    return true;
  }

  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override {
    return true;
  }

  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override {
    return true;
  }

  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override {
    return true;
  }

  bool ProcessCaptureResult(Camera3CaptureDescriptor result) override {
    EXPECT_EQ(thread_.task_runner()->BelongsToCurrentThread(), use_thread_);
    ++process_capture_result_call_counts_;
    base::PlatformThread::Sleep(base::Microseconds(fake_processing_time_us_));
    callbacks_.result_callback.Run(std::move(result));
    return true;
  }

  void Notify(camera3_notify_msg_t msg) override {
    callbacks_.notify_callback.Run(std::move(msg));
  }

  bool Flush() override { return true; }

  scoped_refptr<base::SingleThreadTaskRunner> GetTaskRunner() override {
    if (use_thread_) {
      return thread_.task_runner();
    } else {
      return nullptr;
    }
  }

  int process_capture_result_call_counts() {
    return process_capture_result_call_counts_;
  }

 private:
  bool use_thread_;
  uint64_t fake_processing_time_us_;
  base::Thread thread_;
  Callbacks callbacks_;
  int process_capture_result_call_counts_ = 0;
};

Camera3CaptureDescriptor CreateFakeCaptureResult(uint32_t frame_number) {
  camera3_stream_buffer_t stream_buffer;
  return Camera3CaptureDescriptor(
      camera3_capture_result_t{.frame_number = frame_number,
                               .num_output_buffers = 0,
                               .output_buffers = &stream_buffer});
}

StreamManipulator::CaptureResultCallback CreateCaptureResultCallback(
    Camera3CaptureDescriptor* returned_result,
    base::WaitableEvent* capture_result_returned) {
  return base::BindRepeating(
      [](Camera3CaptureDescriptor* returned_result,
         base::WaitableEvent* capture_result_returned,
         Camera3CaptureDescriptor result) {
        *returned_result = std::move(result);
        capture_result_returned->Signal();
      },
      returned_result, capture_result_returned);
}

StreamManipulator::NotifyCallback CreateNotifyCallback(
    camera3_notify_msg_t* returned_msg, base::WaitableEvent* notify_returned) {
  return base::BindRepeating(
      [](camera3_notify_msg_t* returned_msg,
         base::WaitableEvent* notify_returned, camera3_notify_msg_t msg) {
        *returned_msg = msg;
        notify_returned->Signal();
      },
      returned_msg, notify_returned);
}

StreamManipulator::Callbacks CreateCallbacks(
    Camera3CaptureDescriptor* returned_result,
    base::WaitableEvent* capture_result_returned,
    camera3_notify_msg_t* returned_msg,
    base::WaitableEvent* notify_returned) {
  return StreamManipulator::Callbacks{
      .result_callback =
          CreateCaptureResultCallback(returned_result, capture_result_returned),
      .notify_callback = CreateNotifyCallback(returned_msg, notify_returned)};
}

}  // namespace

TEST(StreamManipulatorManagerTest, NoStreamManipulatorTest) {
  // Construct these objects before StreamManipulatorManager so that they
  // outlive StreamManipulatorManager.
  Camera3CaptureDescriptor returned_result;
  base::WaitableEvent capture_result_returned;
  camera3_notify_msg_t returned_msg;
  base::WaitableEvent notify_returned;

  StreamManipulatorManager manager({});

  android::CameraMetadata metadata;
  manager.Initialize(metadata.getAndLock(),
                     CreateCallbacks(&returned_result, &capture_result_returned,
                                     &returned_msg, &notify_returned));

  Camera3StreamConfiguration stream_config;
  manager.ConfigureStreams(&stream_config);
  manager.OnConfiguredStreams(&stream_config);

  manager.ConstructDefaultRequestSettings(&metadata, 0);

  Camera3CaptureDescriptor request;
  manager.ProcessCaptureRequest(&request);

  manager.ProcessCaptureResult(CreateFakeCaptureResult(/*frame_number=*/1));
  ASSERT_TRUE(capture_result_returned.TimedWait(
      base::Milliseconds(kCaptureRequestTimeoutMs)));
  EXPECT_EQ(returned_result.frame_number(), 1);

  manager.Notify(camera3_notify_msg_t{.type = CAMERA3_MSG_SHUTTER});
  EXPECT_EQ(returned_msg.type, CAMERA3_MSG_SHUTTER);

  manager.Flush();
}

TEST(StreamManipulatorManagerTest, SingleStreamManipulatorTest) {
  // Construct these objects before StreamManipulatorManager so that they
  // outlive StreamManipulatorManager.
  Camera3CaptureDescriptor returned_result;
  base::WaitableEvent capture_result_returned;
  camera3_notify_msg_t returned_msg;
  base::WaitableEvent notify_returned;

  std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators;
  stream_manipulators.emplace_back(
      std::make_unique<FakeStreamManipulator>(/*use_thread=*/true));
  auto stream_manipulator =
      static_cast<FakeStreamManipulator*>(stream_manipulators[0].get());
  StreamManipulatorManager manager(std::move(stream_manipulators));

  android::CameraMetadata metadata;
  manager.Initialize(metadata.getAndLock(),
                     CreateCallbacks(&returned_result, &capture_result_returned,
                                     &returned_msg, &notify_returned));

  Camera3StreamConfiguration stream_config;
  manager.ConfigureStreams(&stream_config);
  manager.OnConfiguredStreams(&stream_config);

  manager.ConstructDefaultRequestSettings(&metadata, 0);

  Camera3CaptureDescriptor request;
  manager.ProcessCaptureRequest(&request);

  manager.ProcessCaptureResult(CreateFakeCaptureResult(/*frame_number=*/1));
  ASSERT_TRUE(capture_result_returned.TimedWait(
      base::Milliseconds(kCaptureRequestTimeoutMs)));
  EXPECT_EQ(returned_result.frame_number(), 1);
  EXPECT_EQ(stream_manipulator->process_capture_result_call_counts(), 1);

  manager.Notify(camera3_notify_msg_t{.type = CAMERA3_MSG_SHUTTER});
  ASSERT_TRUE(
      notify_returned.TimedWait(base::Milliseconds(kCaptureRequestTimeoutMs)));
  EXPECT_EQ(returned_msg.type, CAMERA3_MSG_SHUTTER);

  manager.Flush();
}

TEST(StreamManipulatorManagerTest, MultipleStreamManipulatorsTest) {
  // Construct these objects before StreamManipulatorManager so that they
  // outlive StreamManipulatorManager.
  Camera3CaptureDescriptor returned_result;
  base::WaitableEvent capture_result_returned;
  camera3_notify_msg_t returned_msg;
  base::WaitableEvent notify_returned;

  std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators;
  stream_manipulators.emplace_back(
      std::make_unique<FakeStreamManipulator>(/*use_thread=*/true));
  stream_manipulators.emplace_back(
      std::make_unique<FakeStreamManipulator>(/*use_thread=*/false));
  auto stream_manipulator_1 =
      static_cast<FakeStreamManipulator*>(stream_manipulators[0].get());
  auto stream_manipulator_2 =
      static_cast<FakeStreamManipulator*>(stream_manipulators[1].get());
  StreamManipulatorManager manager(std::move(stream_manipulators));

  android::CameraMetadata metadata;
  manager.Initialize(metadata.getAndLock(),
                     CreateCallbacks(&returned_result, &capture_result_returned,
                                     &returned_msg, &notify_returned));

  Camera3StreamConfiguration stream_config;
  manager.ConfigureStreams(&stream_config);
  manager.OnConfiguredStreams(&stream_config);

  manager.ConstructDefaultRequestSettings(&metadata, 0);

  Camera3CaptureDescriptor request;
  manager.ProcessCaptureRequest(&request);

  manager.ProcessCaptureResult(CreateFakeCaptureResult(/*frame_number=*/1));
  ASSERT_TRUE(capture_result_returned.TimedWait(
      base::Milliseconds(kCaptureRequestTimeoutMs)));
  EXPECT_EQ(returned_result.frame_number(), 1);
  EXPECT_EQ(stream_manipulator_1->process_capture_result_call_counts(), 1);
  EXPECT_EQ(stream_manipulator_2->process_capture_result_call_counts(), 1);

  manager.Notify(camera3_notify_msg_t{.type = CAMERA3_MSG_SHUTTER});
  ASSERT_TRUE(
      notify_returned.TimedWait(base::Milliseconds(kCaptureRequestTimeoutMs)));
  EXPECT_EQ(returned_msg.type, CAMERA3_MSG_SHUTTER);

  manager.Flush();
}

TEST(StreamManipulatorManagerTest, SynchronizationTest) {
  // Construct these objects before StreamManipulatorManager so that they
  // outlive StreamManipulatorManager.
  Camera3CaptureDescriptor returned_result;
  base::WaitableEvent capture_result_returned;
  camera3_notify_msg_t returned_msg;
  base::WaitableEvent notify_returned;

  std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators;
  stream_manipulators.emplace_back(std::make_unique<FakeStreamManipulator>(
      /*use_thread=*/true, /*fake_processing_time_us_=*/1000));
  stream_manipulators.emplace_back(std::make_unique<FakeStreamManipulator>(
      /*use_thread=*/true, /*fake_processing_time_us_=*/1000));
  stream_manipulators.emplace_back(std::make_unique<FakeStreamManipulator>(
      /*use_thread=*/true, /*fake_processing_time_us_=*/1000));
  stream_manipulators.emplace_back(std::make_unique<FakeStreamManipulator>(
      /*use_thread=*/true, /*fake_processing_time_us_=*/1000));
  auto stream_manipulator_1 =
      static_cast<FakeStreamManipulator*>(stream_manipulators[0].get());
  auto stream_manipulator_2 =
      static_cast<FakeStreamManipulator*>(stream_manipulators[1].get());
  auto stream_manipulator_3 =
      static_cast<FakeStreamManipulator*>(stream_manipulators[2].get());
  auto stream_manipulator_4 =
      static_cast<FakeStreamManipulator*>(stream_manipulators[3].get());
  StreamManipulatorManager manager(std::move(stream_manipulators));

  android::CameraMetadata metadata;
  manager.Initialize(metadata.getAndLock(),
                     CreateCallbacks(&returned_result, &capture_result_returned,
                                     &returned_msg, &notify_returned));

  Camera3StreamConfiguration stream_config;
  manager.ConfigureStreams(&stream_config);
  manager.OnConfiguredStreams(&stream_config);

  manager.ConstructDefaultRequestSettings(&metadata, 0);

  Camera3CaptureDescriptor request;
  manager.ProcessCaptureRequest(&request);

  manager.ProcessCaptureResult(CreateFakeCaptureResult(/*frame_number=*/1));
  manager.Notify(camera3_notify_msg_t{.type = CAMERA3_MSG_ERROR});

  // When Notify is returned, capture_result must already be returned
  ASSERT_TRUE(
      notify_returned.TimedWait(base::Milliseconds(kCaptureRequestTimeoutMs)));
  ASSERT_TRUE(capture_result_returned.IsSignaled());
  EXPECT_EQ(returned_result.frame_number(), 1);
  EXPECT_EQ(stream_manipulator_1->process_capture_result_call_counts(), 1);
  EXPECT_EQ(stream_manipulator_2->process_capture_result_call_counts(), 1);
  EXPECT_EQ(stream_manipulator_3->process_capture_result_call_counts(), 1);
  EXPECT_EQ(stream_manipulator_4->process_capture_result_call_counts(), 1);

  EXPECT_EQ(returned_msg.type, CAMERA3_MSG_ERROR);

  manager.Flush();
}

#if USE_CAMERA_DIAGNOSTICS

namespace {

class FakeCameraDiagnosticsClient : public CameraDiagnosticsClient {
 public:
  bool IsFrameAnalysisEnabled() override { return frame_analysis_enabled_; }

  uint32_t frame_interval() const override { return frame_interval_; }

  std::optional<camera_diag::mojom::CameraFramePtr> RequestEmptyFrame()
      override {
    ++request_empty_frame_calls_;
    if (empty_frames_.empty()) {
      return std::nullopt;
    }
    auto frame = std::move(empty_frames_.front());
    empty_frames_.pop_front();
    return frame;
  }

  void SendFrame(camera_diag::mojom::CameraFramePtr frame) override {
    sent_frames_.push_back(std::move(frame));
  }

  bool AddCameraSession(const Size& stream_size) override {
    if (session_stream_size_.has_value()) {
      return false;
    }
    base::CheckedNumeric<uint32_t> nv12_size = stream_size.width;
    nv12_size *= stream_size.height;
    nv12_size = nv12_size * 3 / 2;
    if (!stream_size.is_valid() || stream_size.width % 2 != 0 ||
        stream_size.height % 2 != 0 || !nv12_size.IsValid() ||
        !base::IsValueInRangeForNumericType<int>(stream_size.width) ||
        !base::IsValueInRangeForNumericType<int>(stream_size.height)) {
      return false;
    }
    session_stream_size_ = stream_size;
    return true;
  }

  void RemoveCameraSession() override {
    ++remove_session_calls_;
    session_stream_size_.reset();
  }

  void SetFrameAnalysisEnabled(bool enabled) {
    frame_analysis_enabled_ = enabled;
  }

  void QueueEmptyFrame(camera_diag::mojom::CameraFramePtr frame) {
    empty_frames_.push_back(std::move(frame));
  }

  const std::optional<Size>& session_stream_size() const {
    return session_stream_size_;
  }

  int request_empty_frame_calls() const { return request_empty_frame_calls_; }

  int remove_session_calls() const { return remove_session_calls_; }

  const std::vector<camera_diag::mojom::CameraFramePtr>& sent_frames() const {
    return sent_frames_;
  }

 private:
  bool frame_analysis_enabled_ = true;
  uint32_t frame_interval_ = 1;
  std::optional<Size> session_stream_size_;
  std::list<camera_diag::mojom::CameraFramePtr> empty_frames_;
  std::vector<camera_diag::mojom::CameraFramePtr> sent_frames_;
  int request_empty_frame_calls_ = 0;
  int remove_session_calls_ = 0;
};

}  // namespace

TEST(DiagnosticsStreamManipulatorTest, RejectsOverflowingStreamSize) {
  FakeCameraDiagnosticsClient diag_client;
  DiagnosticsStreamManipulator manipulator(&diag_client);

  Camera3CaptureDescriptor returned_result;
  base::WaitableEvent capture_result_returned;
  camera3_notify_msg_t returned_msg;
  base::WaitableEvent notify_returned;

  android::CameraMetadata metadata;
  ASSERT_TRUE(manipulator.Initialize(
      metadata.getAndLock(),
      CreateCallbacks(&returned_result, &capture_result_returned, &returned_msg,
                      &notify_returned)));

  // 151358 * 113518 = 17,181,857,444 = 4 * 2^32 + 1,988,260 (wraps 32-bit
  // multiplication into [kMinPixelCount, kMaxPixelCount]).
  camera3_stream_t overflow_stream = {
      .stream_type = CAMERA3_STREAM_OUTPUT,
      .width = 151358,
      .height = 113518,
      .format = HAL_PIXEL_FORMAT_YCbCr_420_888,
      .usage = 0,
  };
  std::vector<camera3_stream_t*> streams = {&overflow_stream};
  Camera3StreamConfiguration stream_config(camera3_stream_configuration_t{
      .num_streams = static_cast<uint32_t>(streams.size()),
      .streams = streams.data(),
      .operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE,
  });

  ASSERT_TRUE(manipulator.ConfigureStreams(&stream_config));
  EXPECT_FALSE(diag_client.session_stream_size().has_value());
}

TEST(DiagnosticsStreamManipulatorTest,
     RejectsStreamNotInAvailableStreamConfigurations) {
  FakeCameraDiagnosticsClient diag_client;
  DiagnosticsStreamManipulator manipulator(&diag_client);

  Camera3CaptureDescriptor returned_result;
  base::WaitableEvent capture_result_returned;
  camera3_notify_msg_t returned_msg;
  base::WaitableEvent notify_returned;

  android::CameraMetadata metadata;
  int32_t available_configs[] = {
      HAL_PIXEL_FORMAT_YCbCr_420_888,
      1280,
      720,
      ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
  };
  ASSERT_EQ(metadata.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                            available_configs, std::size(available_configs)),
            0);

  ASSERT_TRUE(manipulator.Initialize(
      metadata.getAndLock(),
      CreateCallbacks(&returned_result, &capture_result_returned, &returned_msg,
                      &notify_returned)));

  camera3_stream_t unsupported_stream = {
      .stream_type = CAMERA3_STREAM_OUTPUT,
      .width = 1920,
      .height = 1080,
      .format = HAL_PIXEL_FORMAT_YCbCr_420_888,
      .usage = 0,
  };
  std::vector<camera3_stream_t*> unsupported_streams = {&unsupported_stream};
  Camera3StreamConfiguration unsupported_config(camera3_stream_configuration_t{
      .num_streams = static_cast<uint32_t>(unsupported_streams.size()),
      .streams = unsupported_streams.data(),
      .operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE,
  });

  ASSERT_TRUE(manipulator.ConfigureStreams(&unsupported_config));
  EXPECT_FALSE(diag_client.session_stream_size().has_value());

  camera3_stream_t supported_stream = {
      .stream_type = CAMERA3_STREAM_OUTPUT,
      .width = 1280,
      .height = 720,
      .format = HAL_PIXEL_FORMAT_YCbCr_420_888,
      .usage = 0,
  };
  std::vector<camera3_stream_t*> supported_streams = {&supported_stream};
  Camera3StreamConfiguration supported_config(camera3_stream_configuration_t{
      .num_streams = static_cast<uint32_t>(supported_streams.size()),
      .streams = supported_streams.data(),
      .operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE,
  });

  ASSERT_TRUE(manipulator.ConfigureStreams(&supported_config));
  ASSERT_TRUE(diag_client.session_stream_size().has_value());
  EXPECT_EQ(diag_client.session_stream_size().value(), Size(1280, 720));
}

TEST(DiagnosticsStreamManipulatorTest,
     CrossDeviceSessionIsolationAndOverflowFrameRejection) {
  FakeCameraDiagnosticsClient diag_client;
  DiagnosticsStreamManipulator cam0_manipulator(&diag_client);

  Camera3CaptureDescriptor returned_result;
  base::WaitableEvent capture_result_returned;
  camera3_notify_msg_t returned_msg;
  base::WaitableEvent notify_returned;

  android::CameraMetadata metadata;
  ASSERT_TRUE(cam0_manipulator.Initialize(
      metadata.getAndLock(),
      CreateCallbacks(&returned_result, &capture_result_returned, &returned_msg,
                      &notify_returned)));

  camera3_stream_t cam0_stream = {
      .stream_type = CAMERA3_STREAM_OUTPUT,
      .width = 1600,
      .height = 1200,
      .format = HAL_PIXEL_FORMAT_YCbCr_420_888,
      .usage = 0,
  };
  std::vector<camera3_stream_t*> cam0_streams = {&cam0_stream};
  Camera3StreamConfiguration cam0_config(camera3_stream_configuration_t{
      .num_streams = static_cast<uint32_t>(cam0_streams.size()),
      .streams = cam0_streams.data(),
      .operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE,
  });
  ASSERT_TRUE(cam0_manipulator.ConfigureStreams(&cam0_config));
  ASSERT_TRUE(diag_client.session_stream_size().has_value());
  EXPECT_EQ(diag_client.session_stream_size().value(), Size(1600, 1200));

  // A second camera device configures streams while cam0 owns the session.
  // Its manipulator must not claim selected_stream_, steal frames, or remove
  // cam0's session when destroyed.
  {
    DiagnosticsStreamManipulator cam1_manipulator(&diag_client);
    ASSERT_TRUE(cam1_manipulator.Initialize(
        metadata.getAndLock(),
        CreateCallbacks(&returned_result, &capture_result_returned,
                        &returned_msg, &notify_returned)));

    camera3_stream_t cam1_stream = {
        .stream_type = CAMERA3_STREAM_OUTPUT,
        .width = 640,
        .height = 480,
        .format = HAL_PIXEL_FORMAT_YCbCr_420_888,
        .usage = 0,
    };
    std::vector<camera3_stream_t*> cam1_streams = {&cam1_stream};
    Camera3StreamConfiguration cam1_config(camera3_stream_configuration_t{
        .num_streams = static_cast<uint32_t>(cam1_streams.size()),
        .streams = cam1_streams.data(),
        .operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE,
    });
    ASSERT_TRUE(cam1_manipulator.ConfigureStreams(&cam1_config));

    camera3_stream_buffer_t cam1_buf = {
        .stream = &cam1_stream,
        .status = CAMERA3_BUFFER_STATUS_OK,
        .acquire_fence = -1,
        .release_fence = -1,
    };
    Camera3CaptureDescriptor cam1_result(camera3_capture_result_t{
        .frame_number = 1,
        .num_output_buffers = 1,
        .output_buffers = &cam1_buf,
    });
    ASSERT_TRUE(cam1_manipulator.ProcessCaptureResult(std::move(cam1_result)));
    EXPECT_EQ(diag_client.request_empty_frame_calls(), 0);
  }
  EXPECT_EQ(diag_client.remove_session_calls(), 0);
  ASSERT_TRUE(diag_client.session_stream_size().has_value());
  EXPECT_EQ(diag_client.session_stream_size().value(), Size(1600, 1200));

  // Queue an empty frame with overflowing dimensions (151358x113518) and
  // wrapped 32-bit NV12 buffer size (2,982,390 bytes) and verify cam0 rejects
  // it in ValidateDiagnosticsFrame without attempting FillDiagnosticsBuffer.
  auto bad_frame = camera_diag::mojom::CameraFrame::New();
  bad_frame->stream = camera_diag::mojom::CameraStream::New(
      151358, 113518, camera_diag::mojom::PixelFormat::kYuv420);
  bad_frame->is_empty = true;
  bad_frame->buffer = camera_diag::mojom::CameraFrameBuffer::New();
  bad_frame->buffer->size = 2982390;
  bad_frame->buffer->shm_handle = mojo::SharedBufferHandle::Create(4096);
  ASSERT_TRUE(bad_frame->buffer->shm_handle.is_valid());
  diag_client.QueueEmptyFrame(std::move(bad_frame));

  camera3_stream_buffer_t cam0_buf = {
      .stream = &cam0_stream,
      .status = CAMERA3_BUFFER_STATUS_OK,
      .acquire_fence = -1,
      .release_fence = -1,
  };
  Camera3CaptureDescriptor cam0_result(camera3_capture_result_t{
      .frame_number = 1,
      .num_output_buffers = 1,
      .output_buffers = &cam0_buf,
  });
  ASSERT_TRUE(cam0_manipulator.ProcessCaptureResult(std::move(cam0_result)));
  EXPECT_EQ(diag_client.request_empty_frame_calls(), 1);
  ASSERT_EQ(diag_client.sent_frames().size(), 1u);
  EXPECT_TRUE(diag_client.sent_frames()[0]->is_empty);
}

#endif  // USE_CAMERA_DIAGNOSTICS

}  // namespace tests

}  // namespace cros

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  ::testing::InitGoogleTest(&argc, argv);
  base::CommandLine::Init(argc, argv);
  mojo::core::Init(mojo::core::Configuration{.is_broker_process = true});
  return RUN_ALL_TESTS();
}
