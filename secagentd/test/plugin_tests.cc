// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "base/memory/scoped_refptr.h"
#include "gmock/gmock.h"  // IWYU pragma: keep
#include "google/protobuf/message_lite.h"
#include "google/protobuf/stubs/casts.h"
#include "gtest/gtest.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "secagentd/bpf/process.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/plugins.h"
#include "secagentd/test/mock_bpf_skeleton.h"
#include "secagentd/test/mock_message_sender.h"
#include "secagentd/test/mock_process_cache.h"

namespace secagentd::testing {

namespace pb = cros_xdr::reporting;

namespace {
absl::StatusOr<Types::BpfSkeleton> PluginTypeToBpfType(
    const Types::Plugin& type) {
  static const absl::flat_hash_map<Types::Plugin, Types::BpfSkeleton>
      kPluginToBpfType{{Types::Plugin::kProcess, Types::BpfSkeleton::kProcess}};
  auto bpf_iter = kPluginToBpfType.find(type);
  if (bpf_iter == kPluginToBpfType.end()) {
    return absl::InternalError(
        absl::StrFormat("plugin_to_bpf_type was unable to map plugin %s to a "
                        "bpf skeleton type.",
                        type));
  }
  return bpf_iter->second;
}
}  // namespace

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Ref;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::TestWithParam;

class BPFPluginTestFixture : public ::testing::TestWithParam<Types::Plugin> {
 protected:
  void SetUp() override {
    bpf_skeleton_ = std::make_unique<MockBpfSkeleton>();
    bpf_skeleton_ref_ = bpf_skeleton_.get();
    skel_factory_ = base::MakeRefCounted<MockSkeletonFactory>();
    message_sender_ = base::MakeRefCounted<MockMessageSender>();
    process_cache_ = base::MakeRefCounted<MockProcessCache>();
    plugin_factory_ = std::make_unique<PluginFactory>(skel_factory_);
  }

  void CreateActivatedPlugin(Types::Plugin type) {
    plugin_ = plugin_factory_->Create(type, message_sender_, process_cache_);
    EXPECT_TRUE(plugin_);

    auto b = PluginTypeToBpfType(type);
    ASSERT_OK(b) << b.status().message();
    auto bpf_type = b.value();

    // TODO(b/253640114): When policy checking is in place this test needs to be
    // updated.
    EXPECT_CALL(*skel_factory_, Create(bpf_type, _))
        .WillOnce(DoAll(::testing::SaveArg<1>(&cbs_),
                        Return(ByMove(std::move(bpf_skeleton_)))));
    EXPECT_OK(plugin_->Activate());
  }

  scoped_refptr<MockSkeletonFactory> skel_factory_;
  scoped_refptr<MockMessageSender> message_sender_;
  scoped_refptr<MockProcessCache> process_cache_;
  std::unique_ptr<PluginFactory> plugin_factory_;
  std::unique_ptr<MockBpfSkeleton> bpf_skeleton_;
  MockBpfSkeleton* bpf_skeleton_ref_;
  std::unique_ptr<PluginInterface> plugin_;
  BpfCallbacks cbs_;
};

TEST_P(BPFPluginTestFixture, TestActivationSuccess) {
  CreateActivatedPlugin(GetParam());
  EXPECT_NE(nullptr, plugin_);
}

TEST_P(BPFPluginTestFixture, TestActivationFailureBadSkeleton) {
  auto type = GetParam();
  auto plugin = plugin_factory_->Create(type, message_sender_, process_cache_);
  EXPECT_TRUE(plugin);

  auto b = PluginTypeToBpfType(type);
  ASSERT_OK(b) << b.status().message();
  auto bpf_type = b.value();

  // TODO(b/253640114): When policy checking is in place this test needs to be
  // updated.
  EXPECT_CALL(*skel_factory_, Create(bpf_type, _))
      .WillOnce(Return(ByMove(nullptr)));
  EXPECT_FALSE(plugin->Activate().ok());
}

TEST_P(BPFPluginTestFixture, TestBPFEventIsAvailable) {
  CreateActivatedPlugin(GetParam());
  EXPECT_NE(nullptr, plugin_);
  EXPECT_CALL(*bpf_skeleton_ref_, ConsumeEvent()).Times(1);
  // Notify the plugin that an event is available.
  cbs_.ring_buffer_read_ready_callback.Run();

  // Maybe serve up the event information.
  bpf::cros_event a;
  EXPECT_CALL(*message_sender_, SendMessage)
      .Times(AnyNumber())
      .WillRepeatedly(Return(absl::OkStatus()));
  cbs_.ring_buffer_event_callback.Run(a);
}

TEST_F(BPFPluginTestFixture, TestProcessPluginExecEvent) {
  CreateActivatedPlugin(Types::Plugin::kProcess);
  EXPECT_NE(nullptr, plugin_);

  constexpr bpf::time_ns_t kSpawnStartTime = 2222;
  // Descending order in time starting from the youngest.
  constexpr uint64_t kPids[] = {3, 2, 1};
  std::vector<std::unique_ptr<pb::Process>> hierarchy;
  for (int i = 0; i < std::size(kPids); ++i) {
    hierarchy.push_back(std::make_unique<pb::Process>());
    // Just some basic verification to make sure we consume the protos in the
    // expected order. The process cache unit test should cover the remaining
    // fields.
    hierarchy[i]->set_canonical_pid(kPids[i]);
  }

  const bpf::cros_event a = {
      .data.process_event = {.type = bpf::process_start_type,
                             .data.process_start = {.task_info =
                                                        {
                                                            .pid = kPids[0],
                                                            .start_time =
                                                                kSpawnStartTime,
                                                        },
                                                    .spawn_namespace =
                                                        {
                                                            .cgroup_ns = 1,
                                                            .pid_ns = 2,
                                                            .user_ns = 3,
                                                            .uts_ns = 4,
                                                            .mnt_ns = 5,
                                                            .net_ns = 6,
                                                            .ipc_ns = 7,
                                                        }}},
      .type = bpf::process_type,
  };
  EXPECT_CALL(*process_cache_,
              PutFromBpfExec(Ref(a.data.process_event.data.process_start)));
  EXPECT_CALL(*process_cache_,
              GetProcessHierarchy(kPids[0], kSpawnStartTime, 3))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

  std::unique_ptr<google::protobuf::MessageLite> actual_sent_message;
  pb::CommonEventDataFields* actual_mutable_common = nullptr;
  EXPECT_CALL(
      *message_sender_,
      SendMessage(Eq(reporting::Destination::CROS_SECURITY_PROCESS), _, _))
      .WillOnce([&actual_sent_message, &actual_mutable_common](
                    auto d, pb::CommonEventDataFields* c,
                    std::unique_ptr<google::protobuf::MessageLite> m) {
        // SaveArgByMove unfortunately doesn't exist.
        actual_sent_message = std::move(m);
        actual_mutable_common = c;
        return absl::OkStatus();
      });

  cbs_.ring_buffer_event_callback.Run(a);

  pb::XdrProcessEvent* actual_process_event =
      google::protobuf::down_cast<pb::XdrProcessEvent*>(
          actual_sent_message.get());
  EXPECT_EQ(actual_process_event->mutable_common(), actual_mutable_common);
  EXPECT_EQ(
      kPids[0],
      actual_process_event->process_exec().spawn_process().canonical_pid());
  EXPECT_EQ(kPids[1],
            actual_process_event->process_exec().process().canonical_pid());
  EXPECT_EQ(
      kPids[2],
      actual_process_event->process_exec().parent_process().canonical_pid());
  auto& ns = a.data.process_event.data.process_start.spawn_namespace;
  EXPECT_EQ(
      ns.cgroup_ns,
      actual_process_event->process_exec().spawn_namespaces().cgroup_ns());
  EXPECT_EQ(ns.pid_ns,
            actual_process_event->process_exec().spawn_namespaces().pid_ns());
  EXPECT_EQ(ns.user_ns,
            actual_process_event->process_exec().spawn_namespaces().user_ns());
  EXPECT_EQ(ns.uts_ns,
            actual_process_event->process_exec().spawn_namespaces().uts_ns());
  EXPECT_EQ(ns.mnt_ns,
            actual_process_event->process_exec().spawn_namespaces().mnt_ns());
  EXPECT_EQ(ns.net_ns,
            actual_process_event->process_exec().spawn_namespaces().net_ns());
  EXPECT_EQ(ns.ipc_ns,
            actual_process_event->process_exec().spawn_namespaces().ipc_ns());
}

TEST_F(BPFPluginTestFixture, TestProcessPluginExecEventPartialHierarchy) {
  CreateActivatedPlugin(Types::Plugin::kProcess);
  EXPECT_NE(nullptr, plugin_);

  constexpr bpf::time_ns_t kSpawnStartTime = 2222;
  // Populate just the spawned process and its parent. I.e one fewer that what
  // we'll be asked to return.
  constexpr uint64_t kPids[] = {3, 2};
  std::vector<std::unique_ptr<pb::Process>> hierarchy;
  for (int i = 0; i < std::size(kPids); ++i) {
    hierarchy.push_back(std::make_unique<pb::Process>());
    hierarchy[i]->set_canonical_pid(kPids[i]);
  }

  const bpf::cros_event a = {
      .data.process_event = {.type = bpf::process_start_type,
                             .data.process_start.task_info =
                                 {
                                     .pid = kPids[0],
                                     .start_time = kSpawnStartTime,
                                 }},
      .type = bpf::process_type,
  };
  EXPECT_CALL(*process_cache_,
              PutFromBpfExec(Ref(a.data.process_event.data.process_start)));
  EXPECT_CALL(*process_cache_,
              GetProcessHierarchy(kPids[0], kSpawnStartTime, 3))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

  std::unique_ptr<google::protobuf::MessageLite> actual_sent_message;
  EXPECT_CALL(
      *message_sender_,
      SendMessage(Eq(reporting::Destination::CROS_SECURITY_PROCESS), _, _))
      .WillOnce([&actual_sent_message](
                    auto d, auto c,
                    std::unique_ptr<google::protobuf::MessageLite> m) {
        actual_sent_message = std::move(m);
        return absl::OkStatus();
      });

  cbs_.ring_buffer_event_callback.Run(a);

  pb::XdrProcessEvent* actual_process_event =
      google::protobuf::down_cast<pb::XdrProcessEvent*>(
          actual_sent_message.get());
  EXPECT_EQ(
      kPids[0],
      actual_process_event->process_exec().spawn_process().canonical_pid());
  EXPECT_EQ(kPids[1],
            actual_process_event->process_exec().process().canonical_pid());
  EXPECT_FALSE(actual_process_event->process_exec().has_parent_process());
}

TEST_F(BPFPluginTestFixture, TestProcessPluginExitEventCacheHit) {
  CreateActivatedPlugin(Types::Plugin::kProcess);
  EXPECT_NE(nullptr, plugin_);

  constexpr bpf::time_ns_t kStartTime = 2222;
  constexpr uint64_t kPids[] = {2, 1};
  std::vector<std::unique_ptr<pb::Process>> hierarchy;
  for (int i = 0; i < std::size(kPids); ++i) {
    hierarchy.push_back(std::make_unique<pb::Process>());
    hierarchy[i]->set_canonical_pid(kPids[i]);
  }

  const bpf::cros_event a = {
      .data.process_event = {.type = bpf::process_exit_type,
                             .data.process_exit =
                                 {
                                     .task_info =
                                         {
                                             .pid = kPids[0],
                                             .start_time = kStartTime,
                                         },
                                     .is_leaf = true,
                                 }},
      .type = bpf::process_type,
  };
  EXPECT_CALL(*process_cache_, GetProcessHierarchy(kPids[0], kStartTime, 2))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

  std::unique_ptr<google::protobuf::MessageLite> actual_sent_message;
  pb::CommonEventDataFields* actual_mutable_common = nullptr;
  EXPECT_CALL(
      *message_sender_,
      SendMessage(Eq(reporting::Destination::CROS_SECURITY_PROCESS), _, _))
      .WillOnce([&actual_sent_message, &actual_mutable_common](
                    auto d, pb::CommonEventDataFields* c,
                    std::unique_ptr<google::protobuf::MessageLite> m) {
        actual_sent_message = std::move(m);
        actual_mutable_common = c;
        return absl::OkStatus();
      });

  EXPECT_CALL(*process_cache_, Erase(kPids[0], kStartTime));

  cbs_.ring_buffer_event_callback.Run(a);

  pb::XdrProcessEvent* actual_process_event =
      google::protobuf::down_cast<pb::XdrProcessEvent*>(
          actual_sent_message.get());
  EXPECT_EQ(actual_process_event->mutable_common(), actual_mutable_common);
  EXPECT_EQ(
      kPids[0],
      actual_process_event->process_terminate().process().canonical_pid());
  EXPECT_EQ(kPids[1], actual_process_event->process_terminate()
                          .parent_process()
                          .canonical_pid());
}

TEST_F(BPFPluginTestFixture, TestProcessPluginExitEventCacheMiss) {
  CreateActivatedPlugin(Types::Plugin::kProcess);
  EXPECT_NE(nullptr, plugin_);

  constexpr bpf::time_ns_t kStartTimes[] = {2222, 1111};
  constexpr uint64_t kPids[] = {2, 1};
  constexpr char kParentImage[] = "/bin/bash";

  // The exiting process wasn't found in the cache.
  std::vector<std::unique_ptr<pb::Process>> hierarchy;

  // The parent, however, was found in procfs.
  std::vector<std::unique_ptr<pb::Process>> parent_hierarchy;
  parent_hierarchy.push_back(std::make_unique<pb::Process>());
  parent_hierarchy[0]->set_canonical_pid(kPids[1]);
  parent_hierarchy[0]->mutable_image()->set_pathname(kParentImage);

  const bpf::cros_event a = {
      .data.process_event = {.type = bpf::process_exit_type,
                             .data.process_exit =
                                 {
                                     .task_info =
                                         {
                                             .pid = kPids[0],
                                             .ppid = kPids[1],
                                             .start_time = kStartTimes[0],
                                             .parent_start_time =
                                                 kStartTimes[1],
                                         },
                                     .is_leaf = false,
                                 }},
      .type = bpf::process_type,
  };
  EXPECT_CALL(*process_cache_, GetProcessHierarchy(kPids[0], kStartTimes[0], 2))
      .WillOnce(Return(ByMove(std::move(hierarchy))));
  EXPECT_CALL(*process_cache_, GetProcessHierarchy(kPids[1], kStartTimes[1], 1))
      .WillOnce(Return(ByMove(std::move(parent_hierarchy))));

  std::unique_ptr<google::protobuf::MessageLite> actual_sent_message;
  pb::CommonEventDataFields* actual_mutable_common = nullptr;
  EXPECT_CALL(
      *message_sender_,
      SendMessage(Eq(reporting::Destination::CROS_SECURITY_PROCESS), _, _))
      .WillOnce([&actual_sent_message, &actual_mutable_common](
                    auto d, pb::CommonEventDataFields* c,
                    std::unique_ptr<google::protobuf::MessageLite> m) {
        actual_sent_message = std::move(m);
        actual_mutable_common = c;
        return absl::OkStatus();
      });

  EXPECT_CALL(*process_cache_, Erase(_, _)).Times(0);

  cbs_.ring_buffer_event_callback.Run(a);

  pb::XdrProcessEvent* actual_process_event =
      google::protobuf::down_cast<pb::XdrProcessEvent*>(
          actual_sent_message.get());
  EXPECT_EQ(actual_process_event->mutable_common(), actual_mutable_common);
  // Expect some process information to be filled in from the BPF event despite
  // the cache miss.
  EXPECT_TRUE(
      actual_process_event->process_terminate().process().has_process_uuid());
  EXPECT_EQ(
      kPids[0],
      actual_process_event->process_terminate().process().canonical_pid());
  EXPECT_EQ(kPids[1], actual_process_event->process_terminate()
                          .parent_process()
                          .canonical_pid());
  // Expect richer information about the parent due to the cache hit on the
  // parent.
  EXPECT_EQ(kParentImage, actual_process_event->process_terminate()
                              .parent_process()
                              .image()
                              .pathname());
}

INSTANTIATE_TEST_SUITE_P(
    BPFPluginTests,
    BPFPluginTestFixture,
    ::testing::ValuesIn<Types::Plugin>({Types::Plugin::kProcess}),
    [](const ::testing::TestParamInfo<BPFPluginTestFixture::ParamType>& info) {
      return absl::StrFormat("%s", info.param);
    });

}  // namespace secagentd::testing
