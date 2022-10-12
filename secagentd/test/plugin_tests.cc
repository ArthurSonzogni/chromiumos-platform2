// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "base/memory/scoped_refptr.h"
#include "gmock/gmock-actions.h"
#include "gmock/gmock-more-actions.h"
#include "gtest/gtest.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/plugins.h"
#include "secagentd/test/mock_bpf_skeleton.h"
#include "secagentd/test/mock_message_sender.h"

namespace secagentd {

using ::testing::_;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::TestWithParam;

class BPFPluginTestFixture : public ::testing::TestWithParam<Types::Plugin> {
 public:
  void SetUp() override {
    type_ = GetParam();
    bpf_skeleton_ = std::make_unique<MockBpfSkeleton>();
    bpf_skeleton_ref_ = bpf_skeleton_.get();
    skel_factory_ = base::MakeRefCounted<MockSkeletonFactory>();
    message_sender_ = base::MakeRefCounted<MockMessageSender>();
    plugin_factory_ = std::make_unique<PluginFactory>(skel_factory_);
  }

 protected:
  Types::Plugin type_;
  scoped_refptr<MockSkeletonFactory> skel_factory_;
  scoped_refptr<MockMessageSender> message_sender_;
  std::unique_ptr<PluginFactory> plugin_factory_;
  std::unique_ptr<MockBpfSkeleton> bpf_skeleton_;
  MockBpfSkeleton* bpf_skeleton_ref_;
};

absl::StatusOr<Types::BpfSkeleton> PluginTypeToBpfType(
    const Types::Plugin& type_) {
  static const absl::flat_hash_map<Types::Plugin, Types::BpfSkeleton>
      kPluginToBpfType{{Types::Plugin::kProcess, Types::BpfSkeleton::kProcess}};
  auto bpf_iter = kPluginToBpfType.find(type_);
  if (bpf_iter == kPluginToBpfType.end()) {
    return absl::InternalError(
        absl::StrFormat("plugin_to_bpf_type was unable to map plugin %s to a "
                        "bpf skeleton type.",
                        type_));
  }
  return bpf_iter->second;
}

TEST_P(BPFPluginTestFixture, TestActivationSuccess) {
  auto plugin = plugin_factory_->Create(type_, message_sender_);
  EXPECT_TRUE(plugin);

  auto b = PluginTypeToBpfType(type_);
  ASSERT_OK(b) << b.status().message();
  auto bpf_type = b.value();

  // TODO(b/253640114): When policy checking is in place this test needs to be
  // updated.
  EXPECT_CALL(*skel_factory_, Create(bpf_type, _))
      .WillOnce(Return(ByMove(std::move(bpf_skeleton_))));
  EXPECT_OK(plugin->Activate());
}

TEST_P(BPFPluginTestFixture, TestActivationFailureBadSkeleton) {
  auto plugin = plugin_factory_->Create(type_, message_sender_);
  EXPECT_TRUE(plugin);

  auto b = PluginTypeToBpfType(type_);
  ASSERT_OK(b) << b.status().message();
  auto bpf_type = b.value();

  // TODO(b/253640114): When policy checking is in place this test needs to be
  // updated.
  EXPECT_CALL(*skel_factory_, Create(bpf_type, _))
      .WillOnce(Return(ByMove(nullptr)));
  EXPECT_FALSE(plugin->Activate().ok());
}

TEST_P(BPFPluginTestFixture, TestBPFEventIsAvailable) {
  auto plugin = plugin_factory_->Create(type_, message_sender_);
  EXPECT_TRUE(plugin);

  auto b = PluginTypeToBpfType(type_);
  ASSERT_OK(b) << b.status().message();
  auto bpf_type = b.value();

  // TODO(b/253640114): When policy checking is in place this test needs to be
  // updated.
  BpfCallbacks cbs;
  EXPECT_CALL(*skel_factory_, Create(bpf_type, _))
      .WillOnce(DoAll(::testing::SaveArg<1>(&cbs),
                      Return(ByMove(std::move(bpf_skeleton_)))));
  EXPECT_OK(plugin->Activate());
  EXPECT_CALL(*bpf_skeleton_ref_, ConsumeEvent()).Times(1);
  // Notify the plugin that an event is available.
  cbs.ring_buffer_read_ready_callback.Run();

  // Serve up the event information.
  // TODO(b/241578161): Aashay plans on refactoring message sender so that it
  // accepts a proto. Not going to bother checking the event carefully here
  // since it will be rewritten anyways.
  bpf::cros_event a;
  EXPECT_CALL(*message_sender_, SendMessage(_))
      .WillOnce(Return(absl::OkStatus()));
  cbs.ring_buffer_event_callback.Run(a);
}

INSTANTIATE_TEST_SUITE_P(
    BPFPluginTests,
    BPFPluginTestFixture,
    ::testing::ValuesIn<Types::Plugin>({Types::Plugin::kProcess}),
    [](const ::testing::TestParamInfo<BPFPluginTestFixture::ParamType>& info) {
      return absl::StrFormat("%s", info.param);
    });

}  // namespace secagentd
