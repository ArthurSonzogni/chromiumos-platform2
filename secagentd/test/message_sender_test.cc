// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/message_sender.h"

#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "gmock/gmock.h"  // IWYU pragma: keep
#include "gtest/gtest.h"
#include "missive/client/mock_report_queue.h"
#include "missive/client/mock_report_queue_provider.h"
#include "missive/client/report_queue.h"
#include "missive/client/report_queue_provider_test_helper.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"
#include "secagentd/proto/security_xdr_events.pb.h"
#include "secagentd/test/mock_message_sender.h"

namespace secagentd::testing {

namespace pb = cros_xdr::reporting;
using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::WithArg;
using ::testing::WithArgs;

class MessageSenderTestFixture : public ::testing::Test {
 protected:
  MessageSenderTestFixture()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  void SetUp() override {
    ASSERT_TRUE(fake_root_.CreateUniqueTempDir());
    const base::FilePath timezone_dir =
        fake_root_.GetPath().Append("var/lib/timezone");
    ASSERT_TRUE(base::CreateDirectory(timezone_dir));
    timezone_symlink_ = timezone_dir.Append("localtime");
    zoneinfo_dir_ = fake_root_.GetPath().Append("usr/share/zoneinfo");
    ASSERT_TRUE(base::CreateDirectory(zoneinfo_dir_));

    message_sender_ = MessageSender::CreateForTesting(fake_root_.GetPath());

    provider_ =
        std::make_unique<NiceMock<reporting::MockReportQueueProvider>>();
    reporting::report_queue_provider_test_helper::SetForTesting(
        provider_.get());
    provider_->ExpectCreateNewSpeculativeQueueAndReturnNewMockQueue(3);
    EXPECT_EQ(message_sender_->InitializeQueues(), absl::OkStatus());
    for (auto destination : kDestinations) {
      auto it = message_sender_->queue_map_.find(destination);
      EXPECT_NE(it, message_sender_->queue_map_.end());
      mock_queue_map_.insert(std::make_pair(
          destination, google::protobuf::down_cast<reporting::MockReportQueue*>(
                           it->second.get())));
    }
  }

  pb::CommonEventDataFields* GetCommon() { return &message_sender_->common_; }
  void CallInitializeDeviceBtime() { message_sender_->InitializeDeviceBtime(); }
  void CallUpdateDeviceTz() {
    message_sender_->UpdateDeviceTz(timezone_symlink_, false);
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir fake_root_;
  scoped_refptr<MessageSender> message_sender_;
  base::FilePath timezone_symlink_;
  base::FilePath zoneinfo_dir_;
  std::unique_ptr<NiceMock<reporting::MockReportQueueProvider>> provider_;
  std::unordered_map<reporting::Destination, reporting::MockReportQueue*>
      mock_queue_map_;
  static const reporting::Priority kPriority_ = reporting::SLOW_BATCH;
  constexpr static const reporting::Destination kDestinations[3] = {
      reporting::Destination::CROS_SECURITY_NETWORK,
      reporting::CROS_SECURITY_PROCESS, reporting::CROS_SECURITY_AGENT};
};

TEST_F(MessageSenderTestFixture, TestInitializeBtime) {
  const std::string kStatContents =
      "cpu  331574 58430 92503 1962802 6568 24763 7752 0 0 0\n"
      "cpu0 18478 11108 17247 350739 777 8197 4561 0 0 0\n"
      "cpu1 22345 8002 13230 364796 1006 3470 961 0 0 0\n"
      "cpu2 23079 8248 12590 365637 1163 2955 737 0 0 0\n"
      "cpu3 23019 8297 12074 366703 1085 2756 630 0 0 0\n"
      "cpu4 108517 11661 18315 272063 1037 3519 442 0 0 0\n"
      "cpu5 136133 11112 19045 242863 1498 3863 419 0 0 0\n"
      "intr 17153789 0 1877556 2940893 0 0 22514 424451 0 0 0 0 0 0 0 0 0 0 0 "
      "0 0 0 0 0 9546173 0 756967 263 1557 1 0 0 0 288285 62 0 158 0 0 12282 "
      "128 56 82 44 15 22533 0 192916 1 17569 519 6 0 0 0 0 0 0 0 221447 0 977 "
      "0 0 0 0 10765 0 0 0 214680 14 263403 0 0 0 0 0 1 1 0 0 0 284203 14 2 1 "
      "51429 0 2 0 0 0 0 1819\n"
      "ctxt 15507989\n"
      "btime 1667427768\n"
      "processes 20013\n"
      "procs_running 1\n"
      "procs_blocked 0\n"
      "softirq 5429921 130273 509093 53702 235430 109885 0 433061 1603480 2368 "
      "2352629";
  const base::FilePath proc_dir = fake_root_.GetPath().Append("proc");
  ASSERT_TRUE(base::CreateDirectory(proc_dir));
  ASSERT_TRUE(base::WriteFile(proc_dir.Append("stat"), kStatContents));
  CallInitializeDeviceBtime();
  EXPECT_EQ(1667427768, GetCommon()->device_boot_time());
}

TEST_F(MessageSenderTestFixture, TestTzUpdateWithPrefix) {
  const base::FilePath us_dir = zoneinfo_dir_.Append("US");
  ASSERT_TRUE(base::CreateDirectory(us_dir));
  const base::FilePath pacific = us_dir.Append("Pacific");
  ASSERT_TRUE(base::WriteFile(pacific, ""));

  ASSERT_TRUE(base::CreateSymbolicLink(pacific, timezone_symlink_));
  CallUpdateDeviceTz();
  EXPECT_EQ("US/Pacific", GetCommon()->local_timezone());
}

TEST_F(MessageSenderTestFixture, TestTzUpdateWithoutPrefix) {
  // Zulu doesn't have a prefix. Probably will never happen but supported
  // nonetheless.
  const base::FilePath zulu = zoneinfo_dir_.Append("Zulu");
  ASSERT_TRUE(base::WriteFile(zulu, ""));

  ASSERT_TRUE(base::CreateSymbolicLink(zulu, timezone_symlink_));
  CallUpdateDeviceTz();
  EXPECT_EQ("Zulu", GetCommon()->local_timezone());
}

TEST_F(MessageSenderTestFixture, TestTzUpdateNotInZoneInfo) {
  const base::FilePath bad = fake_root_.GetPath().Append("IAmError");
  ASSERT_TRUE(base::WriteFile(bad, ""));

  ASSERT_TRUE(base::CreateSymbolicLink(bad, timezone_symlink_));
  CallUpdateDeviceTz();
  // Timezone isn't updated.
  EXPECT_EQ("", GetCommon()->local_timezone());
}

TEST_F(MessageSenderTestFixture, TestSendMessageValidDestination) {
  auto common = GetCommon();
  common->set_device_boot_time(100);
  common->set_local_timezone("US/Pacific");
  std::string proto_string;

  // Process Event.
  EXPECT_CALL(*(mock_queue_map_.find(reporting::CROS_SECURITY_PROCESS)->second),
              AddProducedRecord(_, kPriority_, _))
      .WillOnce(WithArgs<0, 2>(Invoke(
          [&proto_string](
              base::OnceCallback<reporting::StatusOr<std::string>()> record_cb,
              base::OnceCallback<void(reporting::Status)> status_cb) {
            auto serialized = std::move(record_cb).Run();
            proto_string = serialized.ValueOrDie();

            std::move(status_cb).Run(reporting::Status::StatusOK());
          })));
  auto process_message =
      std::make_unique<cros_xdr::reporting::XdrProcessEvent>();
  auto mutable_common = process_message->mutable_common();
  reporting::Destination destination =
      reporting::Destination::CROS_SECURITY_PROCESS;

  message_sender_->SendMessage(destination, mutable_common,
                               std::move(process_message), std::nullopt);
  auto process_deserialized =
      std::make_unique<cros_xdr::reporting::XdrProcessEvent>();
  process_deserialized->ParseFromString(proto_string);
  EXPECT_EQ(common->device_boot_time(),
            process_deserialized->common().device_boot_time());
  EXPECT_EQ(common->local_timezone(),
            process_deserialized->common().local_timezone());

  // Agent Event.
  EXPECT_CALL(*(mock_queue_map_.find(reporting::CROS_SECURITY_AGENT)->second),
              AddProducedRecord(_, kPriority_, _))
      .WillOnce(WithArgs<0, 2>(Invoke(
          [&proto_string](
              base::OnceCallback<reporting::StatusOr<std::string>()> record_cb,
              base::OnceCallback<void(reporting::Status)> status_cb) {
            auto serialized = std::move(record_cb).Run();
            proto_string = serialized.ValueOrDie();

            std::move(status_cb).Run(reporting::Status::StatusOK());
          })));
  auto agent_message = std::make_unique<cros_xdr::reporting::XdrAgentEvent>();
  mutable_common = agent_message->mutable_common();
  destination = reporting::Destination::CROS_SECURITY_AGENT;
  message_sender_->SendMessage(destination, mutable_common,
                               std::move(agent_message), std::nullopt);
  auto agent_deserialized =
      std::make_unique<cros_xdr::reporting::XdrAgentEvent>();
  agent_deserialized->ParseFromString(proto_string);
  EXPECT_EQ(common->device_boot_time(),
            agent_deserialized->common().device_boot_time());
  EXPECT_EQ(common->local_timezone(),
            agent_deserialized->common().local_timezone());
}

TEST_F(MessageSenderTestFixture, TestSendMessageInvalidDestination) {
  auto message = std::make_unique<cros_xdr::reporting::XdrProcessEvent>();
  auto mutable_common = message->mutable_common();
  const reporting::Destination destination = reporting::Destination(-1);

  EXPECT_DEATH(
      {
        message_sender_->SendMessage(destination, mutable_common,
                                     std::move(message), std::nullopt);
      },
      ".*FATAL secagentd_testrunner:.*Check failed: it != queue_map_\\.end.*");
}

TEST_F(MessageSenderTestFixture, TestSendMessageWithCallback) {
  auto message = std::make_unique<cros_xdr::reporting::XdrProcessEvent>();
  auto mutable_common = message->mutable_common();
  const reporting::Destination destination =
      reporting::Destination::CROS_SECURITY_PROCESS;

  EXPECT_CALL(*(mock_queue_map_.find(reporting::CROS_SECURITY_PROCESS)->second),
              AddProducedRecord(_, kPriority_, _))
      .WillOnce(WithArg<2>(
          Invoke([](base::OnceCallback<void(reporting::Status)> status_cb) {
            std::move(status_cb).Run(reporting::Status::StatusOK());
          })));

  base::RunLoop run_loop;
  message_sender_->SendMessage(
      destination, mutable_common, std::move(message),
      base::BindOnce(
          [](base::RunLoop* run_loop, reporting::Status status) {
            EXPECT_OK(status);
            run_loop->Quit();
          },
          &run_loop));
  run_loop.Run();
}

class BatchSenderTestFixture : public ::testing::Test {
 protected:
  // KeyType type.
  using KT = std::string;
  // XdrMessage type.
  using XM = pb::XdrProcessEvent;
  // AtomicVariantMessage type.
  using AVM = pb::ProcessEventAtomicVariant;
  using BatchSenderType = BatchSender<KT, XM, AVM>;

  static constexpr auto kDestination =
      reporting::Destination::CROS_SECURITY_PROCESS;
  static constexpr uint32_t kBatchInterval = 10;

  static std::string GetProcessEventKey(
      const pb::ProcessEventAtomicVariant& process_event) {
    switch (process_event.variant_type_case()) {
      case AVM::kProcessExec:
        return process_event.process_exec().spawn_process().process_uuid();
      case AVM::kProcessTerminate:
        return process_event.process_terminate().process().process_uuid();
      case AVM::VARIANT_TYPE_NOT_SET:
        CHECK(false);
        return "";
    }
  }

  BatchSenderTestFixture()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME),
        message_sender_(base::MakeRefCounted<StrictMock<MockMessageSender>>()) {
  }

  void SetUp() override {
    batch_sender_ = std::make_unique<BatchSenderType>(
        base::BindRepeating(&GetProcessEventKey), message_sender_, kDestination,
        kBatchInterval);
    batch_sender_->Start();
    expected_process_exec_1_.mutable_process_exec()
        ->mutable_spawn_process()
        ->set_process_uuid("uuid1");
    expected_process_exec_2_.mutable_process_exec()
        ->mutable_spawn_process()
        ->set_process_uuid("uuid2");
    expected_process_term_1_.mutable_process_terminate()
        ->mutable_process()
        ->set_process_uuid("uuid1");
  }

  base::test::TaskEnvironment task_environment_;
  scoped_refptr<StrictMock<MockMessageSender>> message_sender_;
  std::unique_ptr<BatchSenderType> batch_sender_;
  AVM expected_process_exec_1_;
  AVM expected_process_exec_2_;
  AVM expected_process_term_1_;
};

TEST_F(BatchSenderTestFixture, TestSimpleBatchingPeriodicFlush) {
  std::unique_ptr<google::protobuf::MessageLite> actual_sent_message;
  pb::CommonEventDataFields* actual_mutable_common = nullptr;
  EXPECT_CALL(*message_sender_,
              SendMessage(Eq(BatchSenderTestFixture::kDestination), _, _, _))
      .WillRepeatedly(
          [&actual_sent_message, &actual_mutable_common](
              auto d, pb::CommonEventDataFields* c,
              std::unique_ptr<google::protobuf::MessageLite> m,
              std::optional<reporting::ReportQueue::EnqueueCallback> cb) {
            // SaveArgByMove unfortunately doesn't exist.
            actual_sent_message = std::move(m);
            actual_mutable_common = c;
            return absl::OkStatus();
          });

  auto process_event_1 = std::make_unique<BatchSenderTestFixture::AVM>();
  process_event_1->CopyFrom(expected_process_exec_1_);
  batch_sender_->Enqueue(std::move(process_event_1));

  auto process_event_2 = std::make_unique<BatchSenderTestFixture::AVM>();
  process_event_2->CopyFrom(expected_process_exec_2_);
  batch_sender_->Enqueue(std::move(process_event_2));

  task_environment_.AdvanceClock(base::Seconds(kBatchInterval));
  task_environment_.RunUntilIdle();

  BatchSenderTestFixture::XM* actual_process_event =
      google::protobuf::down_cast<pb::XdrProcessEvent*>(
          actual_sent_message.get());
  EXPECT_EQ(actual_process_event->mutable_common(), actual_mutable_common);
  ASSERT_EQ(2, actual_process_event->batched_events_size());
  EXPECT_TRUE(actual_process_event->batched_events(0)
                  .common()
                  .has_create_timestamp_us());
  EXPECT_EQ(
      expected_process_exec_1_.process_exec().spawn_process().process_uuid(),
      actual_process_event->batched_events(0)
          .process_exec()
          .spawn_process()
          .process_uuid());
  EXPECT_TRUE(actual_process_event->batched_events(1)
                  .common()
                  .has_create_timestamp_us());
  EXPECT_EQ(
      expected_process_exec_2_.process_exec().spawn_process().process_uuid(),
      actual_process_event->batched_events(1)
          .process_exec()
          .spawn_process()
          .process_uuid());

  auto process_event_3 = std::make_unique<BatchSenderTestFixture::AVM>();
  process_event_3->CopyFrom(expected_process_term_1_);
  batch_sender_->Enqueue(std::move(process_event_3));

  task_environment_.AdvanceClock(base::Seconds(kBatchInterval));
  task_environment_.RunUntilIdle();

  actual_process_event = google::protobuf::down_cast<pb::XdrProcessEvent*>(
      actual_sent_message.get());
  ASSERT_EQ(1, actual_process_event->batched_events_size());
  EXPECT_EQ(
      expected_process_term_1_.process_exec().spawn_process().process_uuid(),
      actual_process_event->batched_events(0)
          .process_exec()
          .spawn_process()
          .process_uuid());
}

TEST_F(BatchSenderTestFixture, TestBatchingSizeLimit) {
  std::vector<std::unique_ptr<google::protobuf::MessageLite>>
      actual_sent_messages;
  std::vector<pb::CommonEventDataFields*> actual_mutable_commons;
  EXPECT_CALL(*message_sender_,
              SendMessage(Eq(BatchSenderTestFixture::kDestination), _, _, _))
      .WillRepeatedly(
          [&actual_sent_messages, &actual_mutable_commons](
              auto d, pb::CommonEventDataFields* c,
              std::unique_ptr<google::protobuf::MessageLite> m,
              std::optional<reporting::ReportQueue::EnqueueCallback> cb) {
            // SaveArgByMove unfortunately doesn't exist.
            actual_sent_messages.emplace_back(std::move(m));
            actual_mutable_commons.push_back(c);
            return absl::OkStatus();
          });

  size_t est_batch_size = 0;
  int sent_events = 0;
  // Enqueue more than enough for the batches to be split.
  while (est_batch_size < BatchSenderType::kMaxMessageSizeBytes * 2) {
    auto process_event = std::make_unique<BatchSenderTestFixture::AVM>();
    process_event->CopyFrom(expected_process_exec_1_);
    process_event->mutable_process_exec()
        ->mutable_spawn_process()
        ->set_process_uuid(base::StringPrintf("%s_%d",
                                              process_event->process_exec()
                                                  .spawn_process()
                                                  .process_uuid()
                                                  .c_str(),
                                              sent_events++));
    est_batch_size += process_event->ByteSizeLong();
    batch_sender_->Enqueue(std::move(process_event));
  }

  task_environment_.AdvanceClock(base::Seconds(kBatchInterval));
  task_environment_.RunUntilIdle();

  // Our math here is not perfect so tolerate a minor deviation. What we
  // actually care about is that the batches were split at least once and that
  // there weren't hundreds of batches created due to some internal glitch.
  EXPECT_LE(2, actual_sent_messages.size());
  EXPECT_GE(5, actual_sent_messages.size());
  // Verify that all the sent messages disjointly account for all of the
  // enqueued events.
  std::set<std::string> sent_ids;
  for (const auto& message : actual_sent_messages) {
    EXPECT_GE(BatchSenderType::kMaxMessageSizeBytes, message->ByteSizeLong());
    auto actual_process_event =
        google::protobuf::down_cast<pb::XdrProcessEvent*>(message.get());
    for (int i = 0; i < actual_process_event->batched_events_size(); ++i) {
      auto id = GetProcessEventKey(actual_process_event->batched_events(i));
      CHECK_EQ(0, sent_ids.count(id)) << "Found dupe id " << id;
      sent_ids.insert(id);
    }
  }
  EXPECT_EQ(sent_events, sent_ids.size());
}

TEST_F(BatchSenderTestFixture, TestVisit) {
  auto process_event_1 = std::make_unique<BatchSenderTestFixture::AVM>();
  process_event_1->CopyFrom(expected_process_exec_1_);
  batch_sender_->Enqueue(std::move(process_event_1));

  auto process_event_2 = std::make_unique<BatchSenderTestFixture::AVM>();
  process_event_2->CopyFrom(expected_process_exec_2_);
  batch_sender_->Enqueue(std::move(process_event_2));

  auto process_event_3 = std::make_unique<BatchSenderTestFixture::AVM>();
  process_event_3->CopyFrom(expected_process_term_1_);
  batch_sender_->Enqueue(std::move(process_event_3));

  ASSERT_EQ(
      expected_process_exec_1_.process_exec().spawn_process().process_uuid(),
      expected_process_term_1_.process_terminate().process().process_uuid());
  const auto& key =
      expected_process_term_1_.process_terminate().process().process_uuid();
  bool cb1_run = false;
  auto cb1 = base::BindLambdaForTesting([key, &cb1_run](AVM* process_event) {
    EXPECT_TRUE(process_event->has_process_terminate());
    EXPECT_EQ(key, process_event->process_terminate().process().process_uuid());
    cb1_run = true;
  });
  // Ask specifically for a terminate event and verify that Visit ignores the
  // exec event with the same key.
  EXPECT_TRUE(
      batch_sender_->Visit(AVM::kProcessTerminate, key, std::move(cb1)));
  EXPECT_TRUE(cb1_run);

  bool cb2_run = false;
  auto cb2 = base::BindLambdaForTesting(
      [&cb2_run](AVM* process_event) { cb2_run = true; });
  EXPECT_FALSE(batch_sender_->Visit(AVM::kProcessTerminate,
                                    "Key does not exist", std::move(cb2)));
  EXPECT_FALSE(cb2_run);
}

}  // namespace secagentd::testing
