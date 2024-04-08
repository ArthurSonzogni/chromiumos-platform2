// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/structured/batch_event_storage.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "metrics/structured/proto/storage.pb.h"

namespace metrics::structured {

class BatchEventStorageTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(base::CreateDirectory(GetEventsPath()));
  }

  base::FilePath GetEventsPath() {
    return temp_dir_.GetPath().Append("events");
  }

  StructuredEventProto CreateTestPayload() {
    StructuredEventProto test_payload;

    test_payload.set_event_name_hash(12345L);
    test_payload.set_project_name_hash(6789L);
    return test_payload;
  }

  // Reads from disk all events persisted.
  std::vector<StructuredEventProto> GetEvents() {
    std::vector<StructuredEventProto> events;
    base::FileEnumerator file_iter(GetEventsPath(), false,
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

 private:
  base::ScopedTempDir temp_dir_;
};

TEST_F(BatchEventStorageTest, FlushesAfterMaxEventBytes) {
  BatchEventStorage::StorageParams params;
  // Set max time elapsed to something arbitrarily large.
  params.flush_time_limit = base::Seconds(1000000);

  // Set max event bytes to something slightly larger.
  params.max_event_bytes_size = CreateTestPayload().ByteSizeLong() + 10;
  auto event_storage =
      std::make_unique<BatchEventStorage>(GetEventsPath(), params);
  event_storage->SetUptimeForTesting(base::Seconds(10), base::Seconds(9));

  // Adding the event to the batch event storage should not trigger a flush
  // since max byte size is TestPayload().ByteSizeLong() + 10.
  event_storage->AddEvent(CreateTestPayload());

  // Should contain zero elements.
  std::vector<StructuredEventProto> events = GetEvents();
  EXPECT_EQ(events.size(), 0);

  // Add another event.
  event_storage->AddEvent(CreateTestPayload());

  // Should contain two events.
  events = GetEvents();
  EXPECT_EQ(events.size(), 2);
  EXPECT_EQ(event_storage->GetInMemoryEventCountForTesting(), 0);
}

TEST_F(BatchEventStorageTest, FlushesAfterTimeElapsed) {
  BatchEventStorage::StorageParams params;
  // Set max time elapsed to something arbitrarily small.
  params.flush_time_limit = base::Seconds(1);

  // Set max event bytes to something arbitrarily large.
  params.max_event_bytes_size = 1000000000;
  auto event_storage =
      std::make_unique<BatchEventStorage>(GetEventsPath(), params);
  event_storage->SetUptimeForTesting(/*curr_uptime=*/base::Seconds(11),
                                     /*last_write_uptime=*/base::Seconds(9));

  // Adding the event to the batch event storage should trigger a flush.
  event_storage->AddEvent(CreateTestPayload());

  // Should contain one element.
  std::vector<StructuredEventProto> events = GetEvents();
  EXPECT_EQ(events.size(), 1);
  EXPECT_EQ(event_storage->GetInMemoryEventCountForTesting(), 0);
}

TEST_F(BatchEventStorageTest, DoesNotFlushIfConditionsNotMet) {
  BatchEventStorage::StorageParams params;
  // Set max time elapsed to something arbitrarily large.
  params.flush_time_limit = base::Seconds(100000);

  // Set max event bytes to something arbitrarily large.
  params.max_event_bytes_size = 1000000000;
  auto event_storage =
      std::make_unique<BatchEventStorage>(GetEventsPath(), params);
  event_storage->SetUptimeForTesting(base::Seconds(10), base::Seconds(9));

  // Adding the event to the batch event storage should not trigger a flush.
  event_storage->AddEvent(CreateTestPayload());

  // Should contain zero elements since a flush has not triggered.
  std::vector<StructuredEventProto> events = GetEvents();
  EXPECT_EQ(events.size(), 0);
  EXPECT_EQ(event_storage->GetInMemoryEventCountForTesting(), 1);
}

}  // namespace metrics::structured
