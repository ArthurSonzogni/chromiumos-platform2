// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <memory>

#include "base/memory/scoped_refptr.h"
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
#include "secagentd/test/mock_policies_features_broker.h"
#include "secagentd/test/mock_process_cache.h"

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

  static void SetPluginBatchSenderForTesting(
      PluginInterface* plugin, std::unique_ptr<BatchSenderType> batch_sender) {
    // This downcast here is very unfortunate but it avoids a lot of templating
    // in the plugin interface and the plugin factory. The factory generally
    // requires future cleanup to cleanly accommodate plugin specific dependency
    // injections.
    google::protobuf::internal::DownCast<FilePlugin*>(plugin)
        ->SetBatchSenderForTesting(std::move(batch_sender));
  }

  void SetUp() override {
    bpf_skeleton = std::make_unique<MockBpfSkeleton>();
    bpf_skeleton_ = bpf_skeleton.get();
    skel_factory_ = base::MakeRefCounted<MockSkeletonFactory>();
    message_sender_ = base::MakeRefCounted<MockMessageSender>();
    process_cache_ = base::MakeRefCounted<MockProcessCache>();
    auto batch_sender = std::make_unique<BatchSenderType>();
    batch_sender_ = batch_sender.get();
    plugin_factory_ = std::make_unique<PluginFactory>(skel_factory_);
    device_user_ = base::MakeRefCounted<MockDeviceUser>();

    plugin_ = plugin_factory_->Create(Types::Plugin::kFile, message_sender_,
                                      process_cache_, policies_features_broker_,
                                      device_user_, kBatchInterval);
    EXPECT_NE(nullptr, plugin_);
    SetPluginBatchSenderForTesting(plugin_.get(), std::move(batch_sender));

    EXPECT_CALL(*skel_factory_,
                Create(Types::BpfSkeleton::kFile, _, kBatchInterval))
        .WillOnce(
            DoAll(SaveArg<1>(&cbs_), Return(ByMove(std::move(bpf_skeleton)))));
    EXPECT_CALL(*batch_sender_, Start());
    EXPECT_TRUE(plugin_->Activate().ok());
  }

  scoped_refptr<MockSkeletonFactory> skel_factory_;
  scoped_refptr<MockMessageSender> message_sender_;
  scoped_refptr<MockProcessCache> process_cache_;
  scoped_refptr<MockDeviceUser> device_user_;
  scoped_refptr<MockPoliciesFeaturesBroker> policies_features_broker_;
  BatchSenderType* batch_sender_;
  std::unique_ptr<PluginFactory> plugin_factory_;
  std::unique_ptr<MockBpfSkeleton> bpf_skeleton;
  MockBpfSkeleton* bpf_skeleton_;
  std::unique_ptr<PluginInterface> plugin_;
  BpfCallbacks cbs_;
};

TEST_F(FilePluginTestFixture, TestActivationFailureBadSkeleton) {
  auto plugin = plugin_factory_->Create(
      Types::Plugin::kFile, message_sender_, process_cache_,
      policies_features_broker_, device_user_, kBatchInterval);
  EXPECT_TRUE(plugin);
  SetPluginBatchSenderForTesting(plugin.get(),
                                 std::make_unique<BatchSenderType>());

  EXPECT_CALL(*skel_factory_,
              Create(Types::BpfSkeleton::kFile, _, kBatchInterval))
      .WillOnce(Return(ByMove(nullptr)));
  EXPECT_FALSE(plugin->Activate().ok());
}

TEST_F(FilePluginTestFixture, TestGetName) {
  ASSERT_EQ("File", plugin_->GetName());
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
}
}  // namespace secagentd::testing
