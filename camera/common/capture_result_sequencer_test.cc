/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/capture_result_sequencer.h"

#include <hardware/camera3.h>
#include <system/camera_metadata.h>

#include <map>
#include <set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "common/camera_hal3_helpers.h"
#include "common/stream_manipulator.h"

namespace cros {
namespace {

class CaptureResultSequencerTest : public testing::Test {
 protected:
  CaptureResultSequencerTest()
      : sequencer_(StreamManipulator::Callbacks{
            .result_callback =
                base::BindRepeating(&CaptureResultSequencerTest::ResultCallback,
                                    base::Unretained(this)),
            .notify_callback =
                base::BindRepeating(&CaptureResultSequencerTest::NotifyCallback,
                                    base::Unretained(this)),
        }) {}

  void AddRequest(const Camera3CaptureDescriptor& request) {
    sequencer_.AddRequest(request);
  }

  void AddResult(Camera3CaptureDescriptor result) {
    sequencer_.AddResult(std::move(result));
  }

  void NotifyDeviceError() {
    sequencer_.Notify(camera3_notify_msg_t{
        .type = CAMERA3_MSG_ERROR,
        .message =
            {
                .error =
                    camera3_error_msg_t{
                        .error_code = CAMERA3_MSG_ERROR_DEVICE,
                    },
            },
    });
  }

  void NotifyRequestError(uint32_t frame_number) {
    sequencer_.Notify(camera3_notify_msg_t{
        .type = CAMERA3_MSG_ERROR,
        .message =
            {
                .error =
                    camera3_error_msg_t{
                        .frame_number = frame_number,
                        .error_code = CAMERA3_MSG_ERROR_REQUEST,
                    },
            },
    });
  }

  void NotifyBufferError(uint32_t frame_number, size_t stream_index) {
    sequencer_.Notify(camera3_notify_msg_t{
        .type = CAMERA3_MSG_ERROR,
        .message =
            {
                .error =
                    camera3_error_msg_t{
                        .frame_number = frame_number,
                        .error_stream = stream(stream_index),
                        .error_code = CAMERA3_MSG_ERROR_BUFFER,
                    },
            },
    });
  }

  void Reset() {
    sequencer_.Reset();

    returned_results_.clear();
    notified_messages_.clear();
  }

  void ValidateReturnedResults(
      const std::map<uint32_t /*frame_number*/,
                     std::set<size_t> /*stream_indices*/>& expected_results)
      const {
    std::map<const camera3_stream_t*, std::vector<uint32_t>> frame_numbers;
    std::map<uint32_t, std::set<size_t>> stream_indices;
    for (auto& result : returned_results_) {
      for (auto& b : result.GetOutputBuffers()) {
        frame_numbers[b.stream()].push_back(result.frame_number());
        stream_indices[result.frame_number()].insert(stream_index(b.stream()));
      }
    }
    EXPECT_TRUE(
        std::all_of(frame_numbers.begin(), frame_numbers.end(), [](auto& item) {
          return std::is_sorted(item.second.begin(), item.second.end());
        }));
    EXPECT_EQ(stream_indices, expected_results);
  }

  Camera3CaptureDescriptor MakeRequest(
      uint32_t frame_number, const std::vector<size_t>& stream_indices) {
    Camera3CaptureDescriptor request(
        camera3_capture_result_t{.frame_number = frame_number});
    for (size_t i : stream_indices) {
      request.AppendOutputBuffer(Camera3StreamBuffer::MakeResultOutput(
          camera3_stream_buffer_t{.stream = stream(i)}));
    }
    return request;
  }

  Camera3CaptureDescriptor MakeResult(uint32_t frame_number,
                                      const std::vector<size_t> stream_indices,
                                      uint32_t partial_result = 0) {
    Camera3CaptureDescriptor result(camera3_capture_result_t{
        .frame_number = frame_number,
        .partial_result = partial_result,
    });
    for (size_t i : stream_indices) {
      result.AppendOutputBuffer(Camera3StreamBuffer::MakeResultOutput(
          camera3_stream_buffer_t{.stream = stream(i)}));
    }
    return result;
  }

  const Camera3CaptureDescriptor& LastReturnedResult() const {
    CHECK(!returned_results_.empty());
    return returned_results_.back();
  }

  const camera3_notify_msg_t& LastNotifiedMessage() const {
    CHECK(!notified_messages_.empty());
    return notified_messages_.back();
  }

 private:
  void ResultCallback(Camera3CaptureDescriptor result) {
    returned_results_.push_back(std::move(result));
  }

  void NotifyCallback(camera3_notify_msg_t msg) {
    notified_messages_.push_back(msg);
  }

  camera3_stream_t* stream(size_t index) { return &mock_streams_[index]; }

  size_t stream_index(const camera3_stream_t* stream) const {
    return std::distance(mock_streams_.begin(), stream);
  }

  static constexpr const size_t kMaxNumStreams = 10;

  std::array<camera3_stream_t, kMaxNumStreams> mock_streams_ = {};
  CaptureResultSequencer sequencer_;
  std::vector<Camera3CaptureDescriptor> returned_results_;
  std::vector<camera3_notify_msg_t> notified_messages_;
};

TEST_F(CaptureResultSequencerTest, OutOfOrderBuffers) {
  AddRequest(MakeRequest(1, {0, 1}));
  AddRequest(MakeRequest(2, {0}));
  AddRequest(MakeRequest(3, {0, 1}));
  AddRequest(MakeRequest(4, {1}));
  AddRequest(MakeRequest(5, {0, 1}));

  AddResult(MakeResult(1, {0}));
  AddResult(MakeResult(3, {0, 1}));
  AddResult(MakeResult(1, {1}));
  AddResult(MakeResult(2, {0}));
  AddResult(MakeResult(5, {0}));
  AddResult(MakeResult(5, {1}));

  ValidateReturnedResults({
      {1, {0, 1}},
      {2, {0}},
      {3, {0, 1}},
      {5, {0}},
  });
}

TEST_F(CaptureResultSequencerTest, NotifyDeviceError) {
  AddRequest(MakeRequest(1, {0, 1}));
  AddRequest(MakeRequest(2, {0, 1}));

  AddResult(MakeResult(1, {0}));
  AddResult(MakeResult(2, {0}));
  AddResult(MakeResult(1, {1}));
  NotifyDeviceError();

  ValidateReturnedResults({
      {1, {0, 1}},
      {2, {0}},
  });

  const camera3_notify_msg_t& last_msg = LastNotifiedMessage();
  ASSERT_EQ(last_msg.type, CAMERA3_MSG_ERROR);
  ASSERT_EQ(last_msg.message.error.error_code, CAMERA3_MSG_ERROR_DEVICE);
}

TEST_F(CaptureResultSequencerTest, NotifyRequestError) {
  AddRequest(MakeRequest(1, {0, 1}));
  AddRequest(MakeRequest(2, {0, 1}));
  AddRequest(MakeRequest(3, {0, 1}));

  AddResult(MakeResult(1, {0}));
  AddResult(MakeResult(3, {0}));
  NotifyRequestError(2);
  AddResult(MakeResult(3, {1}));

  ValidateReturnedResults({
      {1, {0}},
      {3, {0}},
  });

  const camera3_notify_msg_t& last_msg = LastNotifiedMessage();
  ASSERT_EQ(last_msg.type, CAMERA3_MSG_ERROR);
  ASSERT_EQ(last_msg.message.error.error_code, CAMERA3_MSG_ERROR_REQUEST);
  EXPECT_EQ(last_msg.message.error.frame_number, 2);
}

TEST_F(CaptureResultSequencerTest, NotifyBufferError) {
  AddRequest(MakeRequest(1, {0, 1}));
  AddRequest(MakeRequest(2, {0, 1}));
  AddRequest(MakeRequest(3, {0, 1}));

  AddResult(MakeResult(1, {0}));
  AddResult(MakeResult(1, {1}));
  NotifyBufferError(2, 0);
  AddResult(MakeResult(2, {1}));
  AddResult(MakeResult(3, {0}));

  ValidateReturnedResults({
      {1, {0, 1}},
      {2, {1}},
      {3, {0}},
  });

  const camera3_notify_msg_t& last_msg = LastNotifiedMessage();
  ASSERT_EQ(last_msg.type, CAMERA3_MSG_ERROR);
  ASSERT_EQ(last_msg.message.error.error_code, CAMERA3_MSG_ERROR_BUFFER);
  EXPECT_EQ(last_msg.message.error.frame_number, 2);
}

TEST_F(CaptureResultSequencerTest, BypassMetadata) {
  AddRequest(MakeRequest(1, {0, 1}));
  AddRequest(MakeRequest(2, {0, 1}));
  AddRequest(MakeRequest(3, {0, 1}));

  AddResult(MakeResult(1, {0, 1}));
  {
    Camera3CaptureDescriptor result = MakeResult(3, {0}, /*partial_result=*/1);
    result.UpdateMetadata<int64_t>(ANDROID_SENSOR_TIMESTAMP,
                                   std::array<int64_t, 1>{1'234'567});
    AddResult(std::move(result));
  }

  ValidateReturnedResults({
      {1, {0, 1}},
  });

  const Camera3CaptureDescriptor& last_result = LastReturnedResult();
  EXPECT_EQ(last_result.frame_number(), 3);
  EXPECT_TRUE(last_result.HasMetadata(ANDROID_SENSOR_TIMESTAMP));
}

TEST_F(CaptureResultSequencerTest, UnexpectedResult) {
  AddRequest(MakeRequest(1, {0, 1}));

  AddResult(MakeResult(1, {0}));
  EXPECT_DEATH(AddResult(MakeResult(2, {1})), "");
}

TEST_F(CaptureResultSequencerTest, Reset) {
  AddRequest(MakeRequest(1, {0, 1}));
  AddRequest(MakeRequest(2, {0}));
  AddRequest(MakeRequest(3, {0}));

  AddResult(MakeResult(1, {0}));
  AddResult(MakeResult(2, {0}));
  AddResult(MakeResult(1, {1}));

  ValidateReturnedResults({
      {1, {0, 1}},
      {2, {0}},
  });

  Reset();

  AddRequest(MakeRequest(1, {0}));
  AddRequest(MakeRequest(2, {0, 1}));

  AddResult(MakeResult(1, {0}));
  AddResult(MakeResult(2, {0}));

  ValidateReturnedResults({
      {1, {0}},
      {2, {0}},
  });
}

}  // namespace
}  // namespace cros

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
