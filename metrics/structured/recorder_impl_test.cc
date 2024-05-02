// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/structured/recorder_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/flat_set.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/raw_ptr.h>
#include <base/run_loop.h>
#include <base/strings/strcat.h>
#include <base/task/sequenced_task_runner.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <base/time/time.h>
#include <base/test/task_environment.h>
#include <base/values.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>
#include <metrics/fake_metrics_library.h>

#include "metrics/structured/lib/proto/key.pb.h"
#include "metrics/structured/proto/storage.pb.h"
#include "metrics/structured/recorder_singleton.h"
#include "metrics/structured/structured_events.h"

namespace metrics::structured {

namespace {

// These project, event, and metric names are used for testing.
// - project: TestProjectOne
//   - event: TestEventOne
//     - metric: TestMetricOne
//     - metric: TestMetricTwo
//     - metric: TestMetricThree

// The name hash of "TestProjectOne".
constexpr uint64_t kProjectOneHash = UINT64_C(16881314472396226433);
// The name hash of "TestEventOne".
constexpr uint64_t kEventOneHash = UINT64_C(16542188217976373364);
// The name hash of "TestMetricOne".
constexpr uint64_t kMetricOneHash = UINT64_C(637929385654885975);
// The name hash of "TestMetricTwo".
constexpr uint64_t kMetricTwoHash = UINT64_C(14083999144141567134);
// The name hash of "TestMetricThree".
constexpr uint64_t kMetricThreeHash = UINT64_C(13469300759843809564);

// Name hash of "TestEventThree".
constexpr uint64_t kEventTwoHash = UINT64_C(18051195235939111613);

}  // namespace

// TODO(b/338458899): Investigate why calling DestroyRecorderForTest() in
// TearDown() causes coverage builds to fail the test.
class RecorderTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(base::CreateDirectory(GetEventPath()));

    // Create and set a test recorder.
    std::unique_ptr<RecorderImpl> test_recorder =
        std::make_unique<RecorderImpl>(
            GetEventPath().value(), GetKeyPath().value(), GetResetCounterPath(),
            std::make_unique<FakeMetricsLibrary>());
    RecorderSingleton::GetInstance()->SetRecorderForTest(
        std::move(test_recorder));
  }

  base::FilePath GetKeyPath() { return temp_dir_.GetPath().Append("keys"); }
  base::FilePath GetEventPath() { return temp_dir_.GetPath().Append("events"); }
  base::FilePath GetResetCounterPath() {
    return temp_dir_.GetPath().Append("reset_counter");
  }

  // Read the on-disk file and return the information about the key for
  // |project_name_hash|. Fails if a key does not exist.
  KeyProto GetKey(const uint64_t project_name_hash) {
    std::string proto_str;
    // CHECK(base::PathIsReadable(GetKeyPath()));
    CHECK(base::PathIsWritable(GetKeyPath()));
    CHECK(base::PathExists(GetKeyPath()));
    CHECK(base::ReadFileToString(GetKeyPath(), &proto_str));
    KeyDataProto proto;
    CHECK(proto.ParseFromString(proto_str));

    const auto it = proto.keys().find(project_name_hash);
    CHECK(it != proto.keys().end());
    return it->second;
  }

  // Reads from disk all events persisted.
  std::vector<StructuredEventProto> GetEvents() {
    std::vector<StructuredEventProto> events;
    base::FileEnumerator file_iter(GetEventPath(), false,
                                   base::FileEnumerator::FILES);
    for (base::FilePath path = file_iter.Next(); !path.empty();
         path = file_iter.Next()) {
      std::string proto_str;
      CHECK(base::ReadFileToString(path, &proto_str));
      EventsProto proto;
      CHECK(proto.ParseFromString(proto_str));

      for (const StructuredEventProto& event_proto : proto.non_uma_events()) {
        events.emplace_back(event_proto);
      }
    }
    return events;
  }

  void PopulateResetCounterFile(std::string reset_counter) {
    EXPECT_TRUE(base::WriteFile(GetResetCounterPath(), reset_counter));
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(RecorderTest, WriteEvent) {
  events::test_project_one::TestEventOne event1;
  event1.SetTestMetricOne("test").SetTestMetricTwo(1).SetTestMetricThree(2.0);

  EXPECT_TRUE(RecorderSingleton::GetInstance()->GetRecorder()->Record(event1));

  // Check that the project key exists and contains something.
  KeyProto key = GetKey(kProjectOneHash);
  EXPECT_TRUE(key.has_key());

  // There should only be one recorded event.
  EXPECT_EQ(GetEvents().size(), 1);
  StructuredEventProto event = GetEvents().front();
  EXPECT_EQ(event.event_name_hash(), kEventOneHash);

  // Ensure that recorded event is the same.
  StructuredEventProto_Metric metric1 = event.metrics()[0];
  EXPECT_EQ(metric1.name_hash(), kMetricOneHash);
  StructuredEventProto_Metric metric2 = event.metrics()[1];
  EXPECT_EQ(metric2.name_hash(), kMetricTwoHash);
  EXPECT_EQ(metric2.value_int64(), 1);
  StructuredEventProto_Metric metric3 = event.metrics()[2];
  EXPECT_EQ(metric3.name_hash(), kMetricThreeHash);
  EXPECT_EQ(metric3.value_double(), 2.0);

  // Ensure that no sequence metadata has been attached.
  EXPECT_FALSE(event.has_event_sequence_metadata());
}

TEST_F(RecorderTest, SequenceMetadataAttached) {
  PopulateResetCounterFile("5");

  events::test_project_two::TestEventThree event3;
  event3.SetTestMetricFour("abcd");
  EXPECT_TRUE(RecorderSingleton::GetInstance()->GetRecorder()->Record(event3));

  // There should only be one recorded event.
  EXPECT_EQ(GetEvents().size(), 1);
  StructuredEventProto event = GetEvents().front();
  EXPECT_EQ(event.event_name_hash(), kEventTwoHash);

  // Ensure that sequence metadata is correctly attached.
  EXPECT_EQ(event.event_sequence_metadata().reset_counter(), 5);
  EXPECT_NE(event.event_sequence_metadata().system_uptime(),
            kCounterFileUnread);
}

}  // namespace metrics::structured
