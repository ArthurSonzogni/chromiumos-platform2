// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <filesystem>
#include <memory>

#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/common.h"
#include "secagentd/plugins.h"
#include "secagentd/proto/security_xdr_events.pb.h"
#include "secagentd/test/mock_batch_sender.h"
#include "secagentd/test/mock_bpf_skeleton.h"
#include "secagentd/test/mock_device_user.h"
#include "secagentd/test/mock_message_sender.h"
#include "secagentd/test/mock_platform.h"
#include "secagentd/test/mock_policies_features_broker.h"
#include "secagentd/test/mock_process_cache.h"
#include "secagentd/test/test_utils.h"

namespace secagentd::testing {
namespace pb = cros_xdr::reporting;

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Ref;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

class FilePluginTestFixture : public ::testing::Test {
 protected:
  using BatchSenderType = MockBatchSender<std::string,
                                          pb::XdrFileEvent,
                                          pb::FileEventAtomicVariant>;

  static constexpr uint32_t kBatchInterval = 10;
  FilePluginTestFixture()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  static void SetPluginBatchSenderForTesting(
      PluginInterface* plugin, std::unique_ptr<BatchSenderType> batch_sender) {
    // This downcast here is very unfortunate but it avoids a lot of templating
    // in the plugin interface and the plugin factory. The factory generally
    // requires future cleanup to cleanly accommodate plugin specific dependency
    // injections.
    google::protobuf::internal::DownCast<FilePlugin*>(plugin)
        ->SetBatchSenderForTesting(std::move(batch_sender));
  }

  void FilePluginCollectEvent(
      std::unique_ptr<pb::FileEventAtomicVariant> event) {
    google::protobuf::internal::DownCast<FilePlugin*>(plugin_.get())
        ->CollectEvent(std::move(event));
  }

  void SetUp() override {
    // For unit tests run everything on a single thread.
    bpf_skeleton = std::make_unique<MockBpfSkeleton>();
    bpf_skeleton_ = bpf_skeleton.get();
    skel_factory_ = base::MakeRefCounted<MockSkeletonFactory>();
    skel_factory_ref_ = skel_factory_.get();
    message_sender_ = base::MakeRefCounted<MockMessageSender>();
    process_cache_ = base::MakeRefCounted<MockProcessCache>();
    auto batch_sender = std::make_unique<BatchSenderType>();
    batch_sender_ = batch_sender.get();
    plugin_factory_ = std::make_unique<PluginFactory>(skel_factory_);
    device_user_ = base::MakeRefCounted<MockDeviceUser>();

    SetPlatform(std::make_unique<StrictMock<MockPlatform>>());
    platform_ = static_cast<StrictMock<MockPlatform>*>(GetPlatform().get());

    plugin_ = plugin_factory_->Create(Types::Plugin::kFile, message_sender_,
                                      process_cache_, policies_features_broker_,
                                      device_user_, kBatchInterval);
    EXPECT_NE(nullptr, plugin_);
    SetPluginBatchSenderForTesting(plugin_.get(), std::move(batch_sender));

    EXPECT_CALL(*skel_factory_,
                Create(Types::BpfSkeleton::kFile, _, kBatchInterval))
        .WillOnce(
            DoAll(SaveArg<1>(&cbs_), Return(ByMove(std::move(bpf_skeleton)))));

    EXPECT_CALL(*platform_, OpenDirectory(_)).WillRepeatedly(Return(10));
    EXPECT_CALL(*platform_, CloseDirectory(_)).WillRepeatedly(Return(10));
    EXPECT_CALL(*batch_sender_, Start());
    EXPECT_CALL(*platform_, BpfMapFdByName(_, _)).WillRepeatedly(Return(42));
    EXPECT_CALL(*platform_, BpfMapUpdateElementByFd(_, _, _, _))
        .WillRepeatedly(Return(0));

    // Define the expected return value (successful case)
    absl::StatusOr<int> expected_result_bpf_by_name =
        42;  // Replace 42 with your desired value

    EXPECT_CALL(*bpf_skeleton_, FindBpfMapByName(_))
        .WillRepeatedly(Return(expected_result_bpf_by_name));

    EXPECT_CALL(*platform_, FilePathExists(_)).WillRepeatedly(Return(true));

    EXPECT_CALL(*platform_, IsFilePathDirectory(_))
        .WillRepeatedly(Return(true));

    std::vector<std::filesystem::directory_entry> entries;

    // Use std::filesystem::path to construct directory entries
    std::filesystem::path path1("file1.txt");
    std::filesystem::path path2("file2.txt");

    // Create directory entries
    entries.emplace_back(path1);
    entries.emplace_back(path2);

    // Set expectation
    EXPECT_CALL(*platform_, FileSystemDirectoryIterator(_))
        .WillRepeatedly(Return(entries));

    struct statx expected_statx = {};
    expected_statx.stx_mode = S_IFREG | S_IRUSR | S_IWUSR;  // Example mode
    expected_statx.stx_ino = 100;
    expected_statx.stx_dev_major = 10;
    expected_statx.stx_dev_minor = 20;

    // Set up the expectation for Sys_statx
    EXPECT_CALL(*platform_, Sys_statx(_, _, _, _, _))
        .WillRepeatedly([&](int dir_fd, const std::string& path, int flags,
                            unsigned int mask, struct statx* statxbuf) -> int {
          // Modify the statxbuf as needed
          if (statxbuf != nullptr) {
            *statxbuf = expected_statx;
          }
          // Return a specific integer value
          return 0;  // Or any other return value you need
        });
    EXPECT_TRUE(plugin_->Activate().ok());
  }

  scoped_refptr<MockSkeletonFactory> skel_factory_;
  MockSkeletonFactory* skel_factory_ref_;
  scoped_refptr<MockMessageSender> message_sender_;
  scoped_refptr<MockProcessCache> process_cache_;
  scoped_refptr<MockDeviceUser> device_user_;
  scoped_refptr<MockPoliciesFeaturesBroker> policies_features_broker_;
  BatchSenderType* batch_sender_;
  std::unique_ptr<PluginFactory> plugin_factory_;
  std::unique_ptr<MockBpfSkeleton> bpf_skeleton;
  MockBpfSkeleton* bpf_skeleton_;
  std::unique_ptr<PluginInterface> plugin_;
  StrictMock<MockPlatform>* platform_;
  BpfCallbacks cbs_;
  // Needed because FilePlugin creates a new sequenced task.
  base::test::TaskEnvironment task_environment_;
};

TEST_F(FilePluginTestFixture, TestGetName) {
  EXPECT_EQ("File", plugin_->GetName());
}

TEST_F(FilePluginTestFixture, TestActivationFailureBadSkeleton) {
  auto plugin = plugin_factory_->Create(
      Types::Plugin::kFile, message_sender_, process_cache_,
      policies_features_broker_, device_user_, kBatchInterval);
  EXPECT_TRUE(plugin);
  SetPluginBatchSenderForTesting(plugin.get(),
                                 std::make_unique<BatchSenderType>());

  // Set up expectations.
  EXPECT_CALL(*skel_factory_,
              Create(Types::BpfSkeleton::kFile, _, kBatchInterval))
      .WillOnce(Return(ByMove(nullptr)));
  EXPECT_FALSE(plugin->Activate().ok());
}

TEST_F(FilePluginTestFixture, TestBPFEventIsAvailable) {
  const bpf::cros_event file_close_event = {
      .data.file_event =
          {
              .type = bpf::cros_file_event_type::kFileCloseEvent,
              .data.file_detailed_event = {},
          },
      .type = bpf::kFileEvent,
  };
  EXPECT_CALL(*bpf_skeleton_, ConsumeEvent()).Times(1);
  // Notify the plugin that an event is available.
  cbs_.ring_buffer_read_ready_callback.Run();
  EXPECT_CALL(*message_sender_, SendMessage).Times(AnyNumber());
  cbs_.ring_buffer_event_callback.Run(file_close_event);
  task_environment_.AdvanceClock(base::Seconds(kBatchInterval));
  task_environment_.RunUntilIdle();
}

TEST_F(FilePluginTestFixture, TestWrongBPFEvent) {
  EXPECT_CALL(*bpf_skeleton_, ConsumeEvent()).Times(1);
  // Notify the plugin that an event is available.
  cbs_.ring_buffer_read_ready_callback.Run();
  EXPECT_CALL(*message_sender_, SendMessage).Times(0);
  cbs_.ring_buffer_event_callback.Run(
      bpf::cros_event{.type = bpf::kProcessEvent});
  task_environment_.AdvanceClock(base::Seconds(kBatchInterval));
  task_environment_.RunUntilIdle();
}

TEST_F(FilePluginTestFixture, TestReadWriteCoalescing) {
  // events will be a write, modify, modify, read, read
  // all from the same process and all affecting the same file.
  std::string process_uuid{"process1"};

  // create the expected coalesced modify.
  pb::FileEventAtomicVariant expected_modify;
  pb::FileModifyEvent* file_modify_event =
      expected_modify.mutable_sensitive_modify();
  file_modify_event->mutable_process()->set_process_uuid(process_uuid);
  pb::FileModify* file_modify = file_modify_event->mutable_file_modify();
  file_modify->set_modify_type(
      pb::FileModify_ModifyType_WRITE_AND_MODIFY_ATTRIBUTE);
  pb::FileImage* file_image = file_modify->mutable_image_after();
  file_image->set_inode(64);
  file_image->set_inode_device_id(164);
  file_image->set_pathname("filename");
  file_image->set_canonical_gid(45);
  file_image->set_canonical_uid(76);
  file_image->set_mode(123);
  file_image = file_modify->mutable_attributes_before();
  file_image->set_mode(321);
  // Done setting up expected modify

  // expected coalesced read (based off the expected modify).
  pb::FileEventAtomicVariant expected_read;
  pb::FileReadEvent* file_read_event = expected_read.mutable_sensitive_read();
  file_read_event->mutable_process()->CopyFrom(file_modify_event->process());
  file_read_event->mutable_file_read()->mutable_image()->CopyFrom(
      file_modify->image_after());

  // a write event with differing attributes on the after image.
  std::unique_ptr<pb::FileEventAtomicVariant> event =
      std::make_unique<pb::FileEventAtomicVariant>(expected_modify);
  file_modify = event->mutable_sensitive_modify()->mutable_file_modify();
  file_modify->set_modify_type(pb::FileModify_ModifyType_WRITE);
  file_modify->clear_attributes_before();
  file_image = file_modify->mutable_image_after();
  file_image->set_mode(001);
  file_image->set_canonical_uid(999);
  file_image->set_canonical_uid(456);

  FilePluginCollectEvent(std::move(event));

  // a change attribute event with differing before attributes and differing
  // attributes on the after image.
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_modify);
  event->mutable_sensitive_modify()->mutable_file_modify()->set_modify_type(
      pb::FileModify_ModifyType_MODIFY_ATTRIBUTE);
  file_image = event->mutable_sensitive_modify()
                   ->mutable_file_modify()
                   ->mutable_image_after();
  file_image->set_mode(002);
  file_image->set_canonical_uid(888);
  file_image->set_canonical_uid(789);

  FilePluginCollectEvent(std::move(event));

  // a change attribute event with matching before attributes and matching
  // attributes on the after image.
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_modify);
  event->mutable_sensitive_modify()->mutable_file_modify()->set_modify_type(
      pb::FileModify_ModifyType_MODIFY_ATTRIBUTE);

  FilePluginCollectEvent(std::move(event));
  // read event with differing attributes on the image.
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_read);
  file_image =
      event->mutable_sensitive_read()->mutable_file_read()->mutable_image();
  file_image->set_mode(456);
  file_image->set_canonical_gid(314);
  file_image->set_canonical_uid(654);

  FilePluginCollectEvent(std::move(event));

  // read event with expected attributes.
  FilePluginCollectEvent(
      std::make_unique<pb::FileEventAtomicVariant>(expected_read));

  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_modify))));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_read))));
  task_environment_.AdvanceClock(base::Seconds(kBatchInterval));
  task_environment_.RunUntilIdle();
}

TEST_F(FilePluginTestFixture, TestNoCoalescing) {
  // Make sure that coalescing does not happen for events that have differing
  // process uuid, inode, inode device id or are different event types
  // e.g read/write.

  // create a set of expected modifies which vary from the base modify by
  // process uuid, inode or inode device id.
  pb::FileEventAtomicVariant expected_modify1;
  pb::FileModifyEvent* file_modify_event =
      expected_modify1.mutable_sensitive_modify();
  file_modify_event->mutable_process()->set_process_uuid("process1");
  pb::FileImage* file_image =
      file_modify_event->mutable_file_modify()->mutable_image_after();
  file_image->set_inode(64);
  file_image->set_inode_device_id(164);
  file_image->set_pathname("filename1");
  FilePluginCollectEvent(
      std::make_unique<pb::FileEventAtomicVariant>(expected_modify1));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_modify1))));
  // Done setting up expected modify

  pb::FileEventAtomicVariant expected_modify2(expected_modify1);
  expected_modify2.mutable_sensitive_modify()
      ->mutable_process()
      ->set_process_uuid("modified_process");
  FilePluginCollectEvent(
      std::make_unique<pb::FileEventAtomicVariant>(expected_modify2));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_modify2))));

  pb::FileEventAtomicVariant expected_modify3(expected_modify1);
  expected_modify3.mutable_sensitive_modify()
      ->mutable_file_modify()
      ->mutable_image_after()
      ->set_inode(65);
  FilePluginCollectEvent(
      std::make_unique<pb::FileEventAtomicVariant>(expected_modify3));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_modify3))));

  pb::FileEventAtomicVariant expected_modify4(expected_modify1);
  expected_modify4.mutable_sensitive_modify()
      ->mutable_file_modify()
      ->mutable_image_after()
      ->set_inode_device_id(165);
  FilePluginCollectEvent(
      std::make_unique<pb::FileEventAtomicVariant>(expected_modify4));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_modify4))));

  // create a set of expected reads. Each expected varies from the base expected
  // by process uuid, inode or inode device id.
  pb::FileEventAtomicVariant expected_read1;
  pb::FileReadEvent* file_read_event = expected_read1.mutable_sensitive_read();
  file_read_event->mutable_process()->CopyFrom(
      expected_modify1.sensitive_modify().process());
  file_read_event->mutable_file_read()->mutable_image()->CopyFrom(
      expected_modify1.sensitive_modify().file_modify().image_after());
  FilePluginCollectEvent(
      std::make_unique<pb::FileEventAtomicVariant>(expected_read1));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_read1))));

  pb::FileEventAtomicVariant expected_read2(expected_read1);
  expected_read2.mutable_sensitive_read()->mutable_process()->set_process_uuid(
      "modified_process");

  pb::FileEventAtomicVariant expected_read3(expected_read1);
  expected_read3.mutable_sensitive_read()
      ->mutable_file_read()
      ->mutable_image()
      ->set_inode(65);
  FilePluginCollectEvent(
      std::make_unique<pb::FileEventAtomicVariant>(expected_read3));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_read3))));

  pb::FileEventAtomicVariant expected_read4(expected_read1);
  expected_read4.mutable_sensitive_read()
      ->mutable_file_read()
      ->mutable_image()
      ->set_inode_device_id(165);
  FilePluginCollectEvent(
      std::make_unique<pb::FileEventAtomicVariant>(expected_read4));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_read4))));
  task_environment_.AdvanceClock(base::Seconds(kBatchInterval));
  task_environment_.RunUntilIdle();
}

}  // namespace secagentd::testing
