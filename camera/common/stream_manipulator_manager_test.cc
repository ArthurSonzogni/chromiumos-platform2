/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/stream_manipulator_manager.h"

#include <utility>

#include <base/command_line.h>
#include <base/synchronization/waitable_event.h>

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

class FakeStreamManipulator : public StreamManipulator {
 public:
  explicit FakeStreamManipulator(bool use_thread)
      : use_thread_(use_thread), thread_("StreamManipulatorThread") {
    CHECK(thread_.Start());
  }

  // Implementations of StreamManipulator.

  bool Initialize(const camera_metadata_t* static_info,
                  CaptureResultCallback result_callback) override {
    result_callback_ = std::move(result_callback);
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
    result_callback_.Run(std::move(result));
    return true;
  }

  bool Notify(camera3_notify_msg_t* msg) override { return true; }

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
  base::Thread thread_;
  CaptureResultCallback result_callback_;
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

}  // namespace

TEST(StreamManipulatorManagerTest, NoStreamManipulatorTest) {
  StreamManipulatorManager manager({});

  Camera3CaptureDescriptor returned_result;
  base::WaitableEvent capture_result_returned;
  auto callback =
      CreateCaptureResultCallback(&returned_result, &capture_result_returned);
  android::CameraMetadata metadata;
  manager.Initialize(metadata.getAndLock(), callback);

  Camera3StreamConfiguration stream_config;
  manager.ConfigureStreams(&stream_config);
  manager.OnConfiguredStreams(&stream_config);

  manager.ConstructDefaultRequestSettings(&metadata, 0);

  Camera3CaptureDescriptor request;
  manager.ProcessCaptureRequest(&request);

  manager.ProcessCaptureResult(CreateFakeCaptureResult(/*frame_number=*/1));
  ASSERT_TRUE(capture_result_returned.TimedWait(base::Milliseconds(100)));
  EXPECT_EQ(returned_result.frame_number(), 1);

  camera3_notify_msg_t message;
  manager.Notify(&message);
  manager.Flush();
}

TEST(StreamManipulatorManagerTest, SingleStreamManipulatorTest) {
  std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators;
  stream_manipulators.emplace_back(
      std::make_unique<FakeStreamManipulator>(/*use_thread=*/true));
  auto stream_manipulator =
      static_cast<FakeStreamManipulator*>(stream_manipulators[0].get());
  StreamManipulatorManager manager(std::move(stream_manipulators));

  Camera3CaptureDescriptor returned_result;
  base::WaitableEvent capture_result_returned;
  auto callback =
      CreateCaptureResultCallback(&returned_result, &capture_result_returned);
  android::CameraMetadata metadata;
  manager.Initialize(metadata.getAndLock(), callback);

  Camera3StreamConfiguration stream_config;
  manager.ConfigureStreams(&stream_config);
  manager.OnConfiguredStreams(&stream_config);

  manager.ConstructDefaultRequestSettings(&metadata, 0);

  Camera3CaptureDescriptor request;
  manager.ProcessCaptureRequest(&request);

  manager.ProcessCaptureResult(CreateFakeCaptureResult(/*frame_number=*/1));
  ASSERT_TRUE(capture_result_returned.TimedWait(base::Milliseconds(100)));
  EXPECT_EQ(returned_result.frame_number(), 1);
  EXPECT_EQ(stream_manipulator->process_capture_result_call_counts(), 1);

  camera3_notify_msg_t message;
  manager.Notify(&message);
  manager.Flush();
}

TEST(StreamManipulatorManagerTest, MultipleStreamManipulatorsTest) {
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

  Camera3CaptureDescriptor returned_result;
  base::WaitableEvent capture_result_returned;
  auto callback =
      CreateCaptureResultCallback(&returned_result, &capture_result_returned);
  android::CameraMetadata metadata;
  manager.Initialize(metadata.getAndLock(), callback);

  Camera3StreamConfiguration stream_config;
  manager.ConfigureStreams(&stream_config);
  manager.OnConfiguredStreams(&stream_config);

  manager.ConstructDefaultRequestSettings(&metadata, 0);

  Camera3CaptureDescriptor request;
  manager.ProcessCaptureRequest(&request);

  manager.ProcessCaptureResult(CreateFakeCaptureResult(/*frame_number=*/1));
  ASSERT_TRUE(capture_result_returned.TimedWait(base::Milliseconds(100)));
  EXPECT_EQ(returned_result.frame_number(), 1);
  EXPECT_EQ(stream_manipulator_1->process_capture_result_call_counts(), 1);
  EXPECT_EQ(stream_manipulator_2->process_capture_result_call_counts(), 1);

  camera3_notify_msg_t message;
  manager.Notify(&message);
  manager.Flush();
}

}  // namespace tests

}  // namespace cros

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  base::CommandLine::Init(argc, argv);
  return RUN_ALL_TESTS();
}
