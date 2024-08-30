// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

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
#include "secagentd/daemon.h"
#include "secagentd/plugins.h"
#include "secagentd/proto/security_xdr_events.pb.h"
#include "secagentd/test/mock_batch_sender.h"
#include "secagentd/test/mock_bpf_skeleton.h"
#include "secagentd/test/mock_device_user.h"
#include "secagentd/test/mock_image_cache.h"
#include "secagentd/test/mock_message_sender.h"
#include "secagentd/test/mock_platform.h"
#include "secagentd/test/mock_policies_features_broker.h"
#include "secagentd/test/mock_process_cache.h"
#include "secagentd/test/test_utils.h"

namespace secagentd::testing {
namespace pb = cros_xdr::reporting;

using ::testing::_;
using ::testing::AllOf;  // For combining matchers
using ::testing::AnyNumber;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Ref;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

class FilePluginTestFixture : public ::testing::Test {
 protected:
  using BatchSenderType = MockBatchSender<std::string,
                                          pb::XdrFileEvent,
                                          pb::FileEventAtomicVariant>;

  static constexpr uint32_t kBatchIntervalS =
      Daemon::kDefaultPluginBatchIntervalS;
  static constexpr uint32_t kAsyncTimeoutS =
      std::max((kBatchIntervalS / 10), 1u);

  static void SetPluginBatchSenderForTesting(
      PluginInterface* plugin, std::unique_ptr<BatchSenderType> batch_sender) {
    // This downcast here is very unfortunate but it avoids a lot of templating
    // in the plugin interface and the plugin factory. The factory generally
    // requires future cleanup to cleanly accommodate plugin specific dependency
    // injections.
    google::protobuf::internal::DownCast<FilePlugin*>(plugin)
        ->SetBatchSenderForTesting(std::move(batch_sender));
  }

  void SetSHADetails(pb::FileEventAtomicVariant& pb_event,
                     const ImageCacheInterface::HashValue& hash_value) {
    auto variant_type = pb_event.variant_type_case();
    pb::FileImage* image;
    switch (variant_type) {
      case pb::FileEventAtomicVariant::kSensitiveModify:
        image = pb_event.mutable_sensitive_modify()
                    ->mutable_file_modify()
                    ->mutable_image_after();
        break;

      case pb::FileEventAtomicVariant::kSensitiveRead:
        image = pb_event.mutable_sensitive_read()
                    ->mutable_file_read()
                    ->mutable_image();
        break;

      case pb::FileEventAtomicVariant::VARIANT_TYPE_NOT_SET:
        FAIL() << "In SetSHADetails the passed in protobuff did not have a "
                  "variant "
                  "type set.";
        break;
    }
    image->set_partial_sha256(hash_value.sha256_is_partial);
    image->set_sha256(hash_value.sha256);
  }

  void FilePluginCollectEvent(
      std::unique_ptr<FilePlugin::FileEventValue> event) {
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
    image_cache_ = base::MakeRefCounted<MockImageCache>();

    auto batch_sender = std::make_unique<BatchSenderType>();
    batch_sender_ = batch_sender.get();
    plugin_factory_ = std::make_unique<PluginFactory>(skel_factory_);
    device_user_ = base::MakeRefCounted<MockDeviceUser>();

    SetPlatform(std::make_unique<StrictMock<MockPlatform>>());
    platform_ = static_cast<StrictMock<MockPlatform>*>(GetPlatform().get());

    plugin_ = FilePlugin::CreateForTesting(
        skel_factory_, message_sender_, process_cache_, image_cache_,
        policies_features_broker_, device_user_, kBatchIntervalS,
        kAsyncTimeoutS);
    EXPECT_NE(nullptr, plugin_);
    SetPluginBatchSenderForTesting(plugin_.get(), std::move(batch_sender));

    EXPECT_CALL(*skel_factory_,
                Create(Types::BpfSkeleton::kFile, _, kBatchIntervalS))
        .WillOnce(
            DoAll(SaveArg<1>(&cbs_), Return(ByMove(std::move(bpf_skeleton)))));

    EXPECT_CALL(*platform_, OpenDirectory(_)).WillRepeatedly(Return(10));
    EXPECT_CALL(*platform_, CloseDirectory(_)).WillRepeatedly(Return(10));
    EXPECT_CALL(*batch_sender_, Start()).Times(0);
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
        .Times(8)
        .WillRepeatedly([&](int dir_fd, const std::string& path, int flags,
                            unsigned int mask, struct statx* statxbuf) -> int {
          // Modify the statxbuf as needed
          if (statxbuf != nullptr) {
            *statxbuf = expected_statx;
          }
          // Return a specific integer value
          return 0;  // Or any other return value you need
        });
    file_plugin_ = static_cast<FilePlugin*>(plugin_.get());
    EXPECT_TRUE(plugin_->Activate().ok());
  }

  std::unique_ptr<FilePlugin::FileEventValue> CreateFileEventValue(
      std::unique_ptr<pb::FileEventAtomicVariant> pb,
      std::string name,
      uint32_t tag,
      bool is_no_exec) {
    auto rv = std::make_unique<FilePlugin::FileEventValue>();
    rv->event = std::move(pb);
    rv->meta_data.file_name = name;
    rv->meta_data.mtime.tv_nsec = tag;
    rv->meta_data.mtime.tv_sec = tag * 10;
    rv->meta_data.ctime.tv_nsec = tag * 100;
    rv->meta_data.ctime.tv_sec = tag * 1000;
    rv->meta_data.pid_for_setns = tag * 10000;
    rv->meta_data.is_noexec = is_no_exec;
    return rv;
  }

  ImageCacheInterface::ImageCacheKeyType DeriveImageCacheKeyType(
      const FilePlugin::FileEventValue& fev) {
    auto& meta = fev.meta_data;
    secagentd::ImageCacheInterface::ImageCacheKeyType image_key;
    image_key.mtime.tv_nsec = meta.mtime.tv_nsec;
    image_key.mtime.tv_sec = meta.mtime.tv_sec;

    image_key.ctime.tv_nsec = meta.ctime.tv_nsec;
    image_key.ctime.tv_sec = meta.ctime.tv_sec;

    auto& pb_event = *fev.event;
    const pb::FileImage* image;
    if (pb_event.variant_type_case() ==
        pb::FileEventAtomicVariant::kSensitiveRead) {
      image = &pb_event.sensitive_read().file_read().image();
    } else if (pb_event.variant_type_case() ==
               pb::FileEventAtomicVariant::kSensitiveModify) {
      image = &pb_event.sensitive_modify().file_modify().image_after();
    } else {
      return image_key;
    }
    image_key.inode = image->inode();
    image_key.inode_device_id = image->inode_device_id();
    return image_key;
  }  // DeriveImageCacheKeyType

  scoped_refptr<MockSkeletonFactory> skel_factory_;
  MockSkeletonFactory* skel_factory_ref_;
  scoped_refptr<MockMessageSender> message_sender_;
  scoped_refptr<MockProcessCache> process_cache_;
  scoped_refptr<MockImageCache> image_cache_;
  scoped_refptr<MockDeviceUser> device_user_;
  scoped_refptr<MockPoliciesFeaturesBroker> policies_features_broker_;
  BatchSenderType* batch_sender_;
  std::unique_ptr<PluginFactory> plugin_factory_;
  std::unique_ptr<MockBpfSkeleton> bpf_skeleton;
  MockBpfSkeleton* bpf_skeleton_;
  std::unique_ptr<PluginInterface> plugin_;
  FilePlugin* file_plugin_;
  StrictMock<MockPlatform>* platform_;
  BpfCallbacks cbs_;
  // Needed because FilePlugin creates a new sequenced task.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(FilePluginTestFixture, TestGetName) {
  EXPECT_EQ("File", plugin_->GetName());
}

TEST_F(FilePluginTestFixture, TestActivationFailureBadSkeleton) {
  auto plugin = plugin_factory_->Create(
      Types::Plugin::kFile, message_sender_, process_cache_,
      policies_features_broker_, device_user_, kBatchIntervalS);
  EXPECT_TRUE(plugin);
  SetPluginBatchSenderForTesting(plugin.get(),
                                 std::make_unique<BatchSenderType>());

  // Set up expectations.
  EXPECT_CALL(*skel_factory_,
              Create(Types::BpfSkeleton::kFile, _, kBatchIntervalS))
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
}

TEST_F(FilePluginTestFixture, TestWrongBPFEvent) {
  EXPECT_CALL(*bpf_skeleton_, ConsumeEvent()).Times(1);
  // Notify the plugin that an event is available.
  cbs_.ring_buffer_read_ready_callback.Run();
  EXPECT_CALL(*message_sender_, SendMessage).Times(0);
  cbs_.ring_buffer_event_callback.Run(
      bpf::cros_event{.type = bpf::kProcessEvent});
  task_environment_.AdvanceClock(
      base::Seconds(kBatchIntervalS + kAsyncTimeoutS));
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
  file_image->set_sha256("expected_modify");
  file_image->set_partial_sha256(true);

  file_image = file_modify->mutable_attributes_before();
  file_image->set_mode(321);
  // Done setting up expected modify

  // expected coalesced read (based off the expected modify).
  pb::FileEventAtomicVariant expected_read;
  pb::FileReadEvent* file_read_event = expected_read.mutable_sensitive_read();
  file_read_event->mutable_process()->CopyFrom(file_modify_event->process());
  auto* mutable_read_image =
      file_read_event->mutable_file_read()->mutable_image();
  mutable_read_image->CopyFrom(file_modify->image_after());
  mutable_read_image->set_sha256("expected_read");
  mutable_read_image->set_partial_sha256(false);

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
  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "modify0", 1, true));

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

  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "modify2", 2, true));

  // a change attribute event with matching before attributes and matching
  // attributes on the after image.
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_modify);
  event->mutable_sensitive_modify()->mutable_file_modify()->set_modify_type(
      pb::FileModify_ModifyType_MODIFY_ATTRIBUTE);
  auto fev = CreateFileEventValue(std::move(event), "modify3", 3, true);
  auto image_key = DeriveImageCacheKeyType(*fev);
  EXPECT_CALL(*image_cache_,
              InclusiveGetImage(image_key, false, fev->meta_data.pid_for_setns,
                                base::FilePath(fev->meta_data.file_name)))
      .WillOnce(Return(ImageCacheInterface::HashValue{
          .sha256 = "expected_modify", .sha256_is_partial = true}));

  FilePluginCollectEvent(std::move(fev));
  // read event with differing attributes on the image.
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_read);
  file_image =
      event->mutable_sensitive_read()->mutable_file_read()->mutable_image();
  file_image->set_mode(456);
  file_image->set_canonical_gid(314);
  file_image->set_canonical_uid(654);
  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "read1", 1, true));

  // read event with expected attributes.
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_read);
  fev = CreateFileEventValue(std::move(event), "read2", 2, true);
  image_key = DeriveImageCacheKeyType(*fev);
  EXPECT_CALL(*image_cache_,
              InclusiveGetImage(image_key, false, fev->meta_data.pid_for_setns,
                                base::FilePath(fev->meta_data.file_name)))
      .WillOnce(Return(ImageCacheInterface::HashValue{
          .sha256 = "expected_read", .sha256_is_partial = false}));
  FilePluginCollectEvent(std::move(fev));
  // read1 and read2 are expected to be coalesced.

  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_modify))));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_read))));
  task_environment_.FastForwardBy(
      base::Seconds(kBatchIntervalS + kAsyncTimeoutS));
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
  file_image->set_sha256("test");
  auto event = std::make_unique<pb::FileEventAtomicVariant>(expected_modify1);

  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "modify1", 1, true));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_modify1))));
  // Done setting up expected modify

  pb::FileEventAtomicVariant expected_modify2(expected_modify1);
  expected_modify2.mutable_sensitive_modify()
      ->mutable_process()
      ->set_process_uuid("modified_process");
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_modify2);
  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "modify2", 2, true));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_modify2))));

  pb::FileEventAtomicVariant expected_modify3(expected_modify1);
  expected_modify3.mutable_sensitive_modify()
      ->mutable_file_modify()
      ->mutable_image_after()
      ->set_inode(65);
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_modify3);
  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "modify3", 3, true));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_modify3))));

  pb::FileEventAtomicVariant expected_modify4(expected_modify1);
  expected_modify4.mutable_sensitive_modify()
      ->mutable_file_modify()
      ->mutable_image_after()
      ->set_inode_device_id(165);
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_modify4);
  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "modify4", 4, true));
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
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_read1);
  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "read1", 1, true));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_read1))));

  pb::FileEventAtomicVariant expected_read2(expected_read1);
  expected_read2.mutable_sensitive_read()->mutable_process()->set_process_uuid(
      "modified_process");
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_read2);
  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "read2", 2, true));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_read2))));

  pb::FileEventAtomicVariant expected_read3(expected_read1);
  expected_read3.mutable_sensitive_read()
      ->mutable_file_read()
      ->mutable_image()
      ->set_inode(65);
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_read3);
  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "read3", 3, true));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_read3))));

  pb::FileEventAtomicVariant expected_read4(expected_read1);
  expected_read4.mutable_sensitive_read()
      ->mutable_file_read()
      ->mutable_image()
      ->set_inode_device_id(165);
  event = std::make_unique<pb::FileEventAtomicVariant>(expected_read4);
  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "read4", 4, true));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_read4))));
  task_environment_.FastForwardBy(
      base::Seconds(kBatchIntervalS + kAsyncTimeoutS));
}

TEST_F(FilePluginTestFixture, TestMultipleBatches) {
  pb::FileEventAtomicVariant expected_modify1;
  pb::FileModifyEvent* file_modify_event =
      expected_modify1.mutable_sensitive_modify();
  file_modify_event->mutable_process()->set_process_uuid("process1");
  pb::FileImage* file_image =
      file_modify_event->mutable_file_modify()->mutable_image_after();
  file_image->set_inode(64);
  file_image->set_inode_device_id(164);
  file_image->set_pathname("filename1");
  file_image->set_sha256("test");

  auto event = std::make_unique<pb::FileEventAtomicVariant>(expected_modify1);
  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "modify1", 1, true));
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(expected_modify1))))
      .Times(2);
  task_environment_.FastForwardBy(
      base::Seconds(kBatchIntervalS + kAsyncTimeoutS));

  event = std::make_unique<pb::FileEventAtomicVariant>(expected_modify1);
  FilePluginCollectEvent(
      CreateFileEventValue(std::move(event), "modify1", 1, true));
  task_environment_.FastForwardBy(
      base::Seconds(kBatchIntervalS + kAsyncTimeoutS));
}

// Test that SHAs will not be computed for events on a particular file
// if a write event for that file follows.
TEST_F(FilePluginTestFixture, TestWriteInvalidatesAllPreviousSHAs) {
  // File will be written to by process A, then read by process B
  // and then written to by process C.
  // Show that only process C has SHA256 populated.
  // Processes are intentionally different to prevent FilePlugin from
  // coalescing events.
  pb::FileImage file;
  file.set_inode(64);
  file.set_inode_device_id(640);
  file.set_pathname("file1");

  pb::FileEventAtomicVariant write1;
  write1.mutable_sensitive_modify()->mutable_process()->set_process_uuid(
      "process_a");
  write1.mutable_sensitive_modify()
      ->mutable_file_modify()
      ->mutable_image_after()
      ->CopyFrom(file);
  write1.mutable_sensitive_modify()->mutable_file_modify()->set_modify_type(
      pb::FileModify_ModifyType_WRITE);

  pb::FileEventAtomicVariant read;
  read.mutable_sensitive_read()->mutable_process()->set_process_uuid(
      "process_b");
  read.mutable_sensitive_read()->mutable_file_read()->mutable_image()->CopyFrom(
      file);

  pb::FileEventAtomicVariant write2;
  write2.mutable_sensitive_modify()->mutable_process()->set_process_uuid(
      "process_c");
  write2.mutable_sensitive_modify()
      ->mutable_file_modify()
      ->mutable_image_after()
      ->CopyFrom(file);
  write2.mutable_sensitive_modify()->mutable_file_modify()->set_modify_type(
      pb::FileModify_ModifyType_WRITE);

  auto fev = CreateFileEventValue(
      std::make_unique<pb::FileEventAtomicVariant>(write1), "write1", 1, true);
  // SHA256 computation should not happened for first write since the SHA
  // operation is aborted by write2.
  EXPECT_CALL(*image_cache_,
              InclusiveGetImage(DeriveImageCacheKeyType(*fev), false,
                                fev->meta_data.pid_for_setns,
                                base::FilePath(fev->meta_data.file_name)))
      .Times(0);
  EXPECT_CALL(*batch_sender_, Enqueue(::testing::Pointee(EqualsProto(write1))));
  FilePluginCollectEvent(std::move(fev));

  fev = CreateFileEventValue(std::make_unique<pb::FileEventAtomicVariant>(read),
                             "read", 2, true);
  // SHA256 computation should not happened for first write since the SHA
  // operation is aborted by write2.
  EXPECT_CALL(*image_cache_,
              InclusiveGetImage(DeriveImageCacheKeyType(*fev), false,
                                fev->meta_data.pid_for_setns,
                                base::FilePath(fev->meta_data.file_name)))
      .Times(0);
  EXPECT_CALL(*batch_sender_, Enqueue(::testing::Pointee(EqualsProto(read))));
  FilePluginCollectEvent(std::move(fev));

  fev = CreateFileEventValue(
      std::make_unique<pb::FileEventAtomicVariant>(write2), "write2", 3, true);
  // SHA256 computation should not happened for first write since the SHA
  // operation is aborted by write2.
  ImageCacheInterface::HashValue hash_val{.sha256 = "sha_write2",
                                          .sha256_is_partial = false};
  EXPECT_CALL(*image_cache_,
              InclusiveGetImage(DeriveImageCacheKeyType(*fev), false,
                                fev->meta_data.pid_for_setns,
                                base::FilePath(fev->meta_data.file_name)))
      .WillOnce(Return(hash_val));

  write2.mutable_sensitive_modify()
      ->mutable_file_modify()
      ->mutable_image_after()
      ->set_partial_sha256(hash_val.sha256_is_partial);
  write2.mutable_sensitive_modify()
      ->mutable_file_modify()
      ->mutable_image_after()
      ->set_sha256(hash_val.sha256);
  EXPECT_CALL(*batch_sender_, Enqueue(::testing::Pointee(EqualsProto(write2))));
  EXPECT_CALL(*batch_sender_, Flush()).Times(1);
  FilePluginCollectEvent(std::move(fev));
  task_environment_.FastForwardBy(
      base::Seconds(kBatchIntervalS + kAsyncTimeoutS));
}

// Test that reads do not invalidate SHAs.
TEST_F(FilePluginTestFixture, TestReadsDoNotInvalidateSHA) {
  // File will be read twice by two different processes.
  pb::FileImage file;
  file.set_inode(64);
  file.set_inode_device_id(640);
  file.set_pathname("file1");

  pb::FileEventAtomicVariant read1;
  read1.mutable_sensitive_read()->mutable_process()->set_process_uuid(
      "process_a");
  read1.mutable_sensitive_read()
      ->mutable_file_read()
      ->mutable_image()
      ->CopyFrom(file);

  auto fev_read1 = CreateFileEventValue(
      std::make_unique<pb::FileEventAtomicVariant>(read1), "read1", 1, true);
  // SHA256 computation should not happened for first write since the SHA
  // operation is aborted by write2.
  ImageCacheInterface::HashValue read1_hash{.sha256 = "sha_read1",
                                            .sha256_is_partial = false};
  EXPECT_CALL(*image_cache_,
              InclusiveGetImage(DeriveImageCacheKeyType(*fev_read1), false,
                                fev_read1->meta_data.pid_for_setns,
                                base::FilePath(fev_read1->meta_data.file_name)))
      .WillOnce(Return(read1_hash));
  SetSHADetails(read1, read1_hash);
  EXPECT_CALL(*batch_sender_, Enqueue(::testing::Pointee(EqualsProto(read1))));
  FilePluginCollectEvent(std::move(fev_read1));

  pb::FileEventAtomicVariant read2;
  read2.mutable_sensitive_read()->mutable_process()->set_process_uuid(
      "process_b");
  read2.mutable_sensitive_read()
      ->mutable_file_read()
      ->mutable_image()
      ->CopyFrom(file);
  auto fev_read2 = CreateFileEventValue(
      std::make_unique<pb::FileEventAtomicVariant>(read2), "read2", 2, true);
  // SHA256 computation should not happened for first write since the SHA
  // operation is aborted by write2.
  ImageCacheInterface::HashValue read2_hash{.sha256 = "sha_read2",
                                            .sha256_is_partial = true};
  EXPECT_CALL(*image_cache_,
              InclusiveGetImage(DeriveImageCacheKeyType(*fev_read2), false,
                                fev_read2->meta_data.pid_for_setns,
                                base::FilePath(fev_read2->meta_data.file_name)))
      .WillOnce(Return(read2_hash));
  FilePluginCollectEvent(std::move(fev_read2));
  SetSHADetails(read2, read2_hash);
  EXPECT_CALL(*batch_sender_, Enqueue(::testing::Pointee(EqualsProto(read2))));

  EXPECT_CALL(*batch_sender_, Flush()).Times(1);
  task_environment_.FastForwardBy(
      base::Seconds(kBatchIntervalS + kAsyncTimeoutS));
}

// Test that modify attributes does not invalidate SHAs.
TEST_F(FilePluginTestFixture, TestChangeAttributesDoNotInvalidateSHA) {
  // File will be read then have its attributes modified by two different
  // processes.
  pb::FileImage file;
  file.set_inode(64);
  file.set_inode_device_id(640);
  file.set_pathname("file1");

  pb::FileEventAtomicVariant read;
  read.mutable_sensitive_read()->mutable_process()->set_process_uuid(
      "process_a");
  read.mutable_sensitive_read()->mutable_file_read()->mutable_image()->CopyFrom(
      file);

  auto fev_read = CreateFileEventValue(
      std::make_unique<pb::FileEventAtomicVariant>(read), "read", 1, true);
  // SHA256 computation should not happened for first write since the SHA
  // operation is aborted by write2.
  ImageCacheInterface::HashValue read_hash{.sha256 = "sha_read",
                                           .sha256_is_partial = false};
  EXPECT_CALL(*image_cache_,
              InclusiveGetImage(DeriveImageCacheKeyType(*fev_read), false,
                                fev_read->meta_data.pid_for_setns,
                                base::FilePath(fev_read->meta_data.file_name)))
      .WillOnce(Return(read_hash));
  SetSHADetails(read, read_hash);
  EXPECT_CALL(*batch_sender_, Enqueue(::testing::Pointee(EqualsProto(read))));
  FilePluginCollectEvent(std::move(fev_read));

  pb::FileEventAtomicVariant modify_attribute;
  modify_attribute.mutable_sensitive_modify()
      ->mutable_process()
      ->set_process_uuid("process_b");
  modify_attribute.mutable_sensitive_modify()
      ->mutable_file_modify()
      ->set_modify_type(pb::FileModify_ModifyType_MODIFY_ATTRIBUTE);
  modify_attribute.mutable_sensitive_modify()
      ->mutable_file_modify()
      ->mutable_image_after()
      ->CopyFrom(file);
  auto fev_modify_attribute = CreateFileEventValue(
      std::make_unique<pb::FileEventAtomicVariant>(modify_attribute),
      "modify_attribute", 2, true);
  // SHA256 computation should not happened for first write since the SHA
  // operation is aborted by write2.
  ImageCacheInterface::HashValue modify_attribute_hash{
      .sha256 = "sha_modify_attribute", .sha256_is_partial = true};
  EXPECT_CALL(*image_cache_,
              InclusiveGetImage(
                  DeriveImageCacheKeyType(*fev_modify_attribute), false,
                  fev_modify_attribute->meta_data.pid_for_setns,
                  base::FilePath(fev_modify_attribute->meta_data.file_name)))
      .WillOnce(Return(modify_attribute_hash));
  FilePluginCollectEvent(std::move(fev_modify_attribute));
  SetSHADetails(modify_attribute, modify_attribute_hash);
  EXPECT_CALL(*batch_sender_,
              Enqueue(::testing::Pointee(EqualsProto(modify_attribute))));

  EXPECT_CALL(*batch_sender_, Flush()).Times(1);
  task_environment_.FastForwardBy(
      base::Seconds(kBatchIntervalS + kAsyncTimeoutS));
}

TEST_F(FilePluginTestFixture, TestOnDeviceMountMedia) {
  const bpf::mount_data mount_data = {.dest_device_path =
                                          "/media/removable/usb1"};

  struct statx expected_statx = {};
  expected_statx.stx_mode = S_IFREG | S_IRUSR | S_IWUSR;
  expected_statx.stx_ino = 100;
  expected_statx.stx_dev_major = 10;
  expected_statx.stx_dev_minor = 20;

  EXPECT_CALL(*platform_,
              Sys_statx(10, Eq("/media/removable/usb1"), AT_STATX_DONT_SYNC,
                        STATX_INO | STATX_BASIC_STATS, _))
      .Times(2)
      .WillRepeatedly([&](int dir_fd, const std::string& path, int flags,
                          unsigned int mask, struct statx* statxbuf) -> int {
        if (statxbuf != nullptr) {
          *statxbuf = expected_statx;
        }
        return 0;
      });

  //   Expected value from UserspaceToKernelDeviceId().
  dev_t expected_kernel_dev = static_cast<dev_t>((10 << 20) | 20);
  struct bpf::inode_dev_map_key expected_bpfMapKey = {
      .inode_id = expected_statx.stx_ino, .dev_id = expected_kernel_dev};

  struct bpf::device_file_monitoring_settings expected_bpfSettings = {
      .device_monitoring_type =
          bpf::device_monitoring_type::MONITOR_SPECIFIC_FILES,
      .file_monitoring_mode = bpf::file_monitoring_mode::READ_WRITE_ONLY,
      .sensitive_file_type =
          cros_xdr::reporting::SensitiveFileType::USB_MASS_STORAGE,
  };

  absl::StatusOr<int> expected_result_nodes = 123245;
  EXPECT_CALL(*bpf_skeleton_, FindBpfMapByName("predefined_allowed_inodes"))
      .WillRepeatedly(Return(expected_result_nodes));

  absl::StatusOr<int> expected_result_device = 4354;
  EXPECT_CALL(*bpf_skeleton_, FindBpfMapByName("device_monitoring_allowlist"))
      .WillRepeatedly(Return(expected_result_device));

  EXPECT_CALL(
      *platform_,
      BpfMapUpdateElementByFd(
          expected_result_nodes.value(),
          ::testing::Truly([&expected_bpfMapKey](const void* arg) -> bool {
            auto* key = static_cast<const bpf::inode_dev_map_key*>(arg);
            return key->inode_id == expected_bpfMapKey.inode_id &&
                   key->dev_id == expected_bpfMapKey.dev_id;
          }),
          ::testing::Truly([&expected_bpfSettings](const void* arg) -> bool {
            auto* settings =
                static_cast<const bpf::file_monitoring_settings*>(arg);
            return settings->file_monitoring_mode ==
                       expected_bpfSettings.file_monitoring_mode &&
                   settings->sensitive_file_type ==
                       expected_bpfSettings.sensitive_file_type;
          }),
          0))
      .WillOnce(Return(0));

  EXPECT_CALL(
      *platform_,
      BpfMapUpdateElementByFd(
          expected_result_device.value(),
          ::testing::Truly([&expected_bpfMapKey](const void* arg) -> bool {
            const auto* dev_id_arg = static_cast<const dev_t*>(arg);
            return *dev_id_arg == expected_bpfMapKey.dev_id;
          }),
          ::testing::Truly([&expected_bpfSettings](const void* arg) -> bool {
            auto* settings =
                static_cast<const bpf::device_file_monitoring_settings*>(arg);
            return settings->device_monitoring_type ==
                       expected_bpfSettings.device_monitoring_type &&
                   settings->file_monitoring_mode ==
                       expected_bpfSettings.file_monitoring_mode &&
                   settings->sensitive_file_type ==
                       expected_bpfSettings.sensitive_file_type;
          }),
          0))
      .WillOnce(Return(0));
  bpf::cros_event bpf_event;
  bpf_event.type = bpf::kFileEvent;
  bpf_event.data.file_event.type = bpf::cros_file_event_type::kFileMountEvent;
  bpf_event.data.file_event.data.mount_event = mount_data;
  bpf_event.data.file_event.mod_type = bpf::FMOD_MOUNT;

  cbs_.ring_buffer_event_callback.Run(bpf_event);
}

TEST_F(FilePluginTestFixture, TestOnDeviceMountUnknown) {
  const bpf::mount_data mount_data = {.dest_device_path =
                                          "/mnt/removable/usb1"};

  struct statx expected_statx = {};
  expected_statx.stx_mode = S_IFREG | S_IRUSR | S_IWUSR;
  expected_statx.stx_ino = 100;
  expected_statx.stx_dev_major = 10;
  expected_statx.stx_dev_minor = 20;

  EXPECT_CALL(*platform_, Sys_statx(_, Eq("/mnt/removable/usb1"), _, _, _))
      .Times(0);

  absl::StatusOr<int> expected_result_nodes = 123245;
  EXPECT_CALL(*bpf_skeleton_, FindBpfMapByName("predefined_allowed_inodes"))
      .WillRepeatedly(Return(expected_result_nodes));

  absl::StatusOr<int> expected_result_device = 4354;
  EXPECT_CALL(*bpf_skeleton_, FindBpfMapByName("device_monitoring_allowlist"))
      .WillRepeatedly(Return(expected_result_device));

  EXPECT_CALL(*platform_,
              BpfMapUpdateElementByFd(expected_result_nodes.value(), _, _, _))
      .Times(0);

  EXPECT_CALL(*platform_,
              BpfMapUpdateElementByFd(expected_result_device.value(), _, _, _))
      .Times(0);
  bpf::cros_event bpf_event;
  bpf_event.type = bpf::kFileEvent;
  bpf_event.data.file_event.type = bpf::cros_file_event_type::kFileMountEvent;
  bpf_event.data.file_event.data.mount_event = mount_data;
  bpf_event.data.file_event.mod_type = bpf::FMOD_MOUNT;

  cbs_.ring_buffer_event_callback.Run(bpf_event);
}

}  // namespace secagentd::testing
