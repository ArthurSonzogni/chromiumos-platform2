// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <netinet/in.h>
#include <sys/socket.h>

#include <cstddef>
#include <memory>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "base/memory/scoped_refptr.h"
#include "gmock/gmock.h"
#include "google/protobuf/message_lite.h"
#include "gtest/gtest.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/plugins.h"
#include "secagentd/proto/security_xdr_events.pb.h"
#include "secagentd/test/mock_batch_sender.h"
#include "secagentd/test/mock_bpf_skeleton.h"
#include "secagentd/test/mock_device_user.h"
#include "secagentd/test/mock_message_sender.h"
#include "secagentd/test/mock_policies_features_broker.h"
#include "secagentd/test/mock_process_cache.h"
#include "secagentd/test/test_utils.h"

namespace secagentd::testing {
namespace pb = cros_xdr::reporting;
namespace {
struct ExpectedProcess {
  uint64_t pid;
  uint64_t uid;
  std::string cmdline;
  uint64_t rel_start_time_s;
};
const uint64_t kDefaultPid{1452};
constexpr bpf::time_ns_t kSpawnStartTime{2222};

const bpf::cros_process_start kDefaultProcessInfo = {
    .task_info = {.pid = 5139,
                  .ppid = 5132,
                  .start_time = 51382,
                  .parent_start_time = 5786,
                  .uid = 382,
                  .gid = 4234},
    .image_info =
        {
            .inode = 24,
            .mode = 123,
        },
    .spawn_namespace = {.cgroup_ns = 54}};

const std::vector<ExpectedProcess> kDefaultProcessHierarchy{
    {.pid = kDefaultPid,
     .uid = 3123,
     .cmdline{"commandline1"},
     .rel_start_time_s = 144234},
    {.pid = 12314,
     .uid = 14123,
     .cmdline{"commandline2"},
     .rel_start_time_s = 51234},
};
bpf::cros_event CreateCrosFlowEvent(const bpf::cros_synthetic_network_flow& f) {
  bpf::cros_event rv = {
      .data.network_event{
          .type = bpf::cros_network_event_type::kSyntheticNetworkFlow},
      .type = bpf::kNetworkEvent};
  memmove(&rv.data.network_event.data.flow, &f, sizeof(f));
  return rv;
}
}  // namespace

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Ref;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;
using ::testing::WithArg;

constexpr char kDeviceUser[] = "deviceUser@email.com";
constexpr char kSanitized[] = "943cebc444e3e19da9a2dbf9c8a473bc7cc16d9d";

class NetworkPluginTestFixture : public ::testing::Test {
 protected:
  using BatchSenderType = MockBatchSender<std::string,
                                          pb::XdrNetworkEvent,
                                          pb::NetworkEventAtomicVariant>;

  static constexpr uint32_t kBatchInterval = 10;

  static void SetPluginBatchSenderForTesting(
      PluginInterface* plugin, std::unique_ptr<BatchSenderType> batch_sender) {
    // This downcast here is very unfortunate but it avoids a lot of templating
    // in the plugin interface and the plugin factory. The factory generally
    // requires future cleanup to cleanly accommodate plugin specific dependency
    // injections.
    google::protobuf::internal::DownCast<NetworkPlugin*>(plugin)
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

    plugin_ = plugin_factory_->Create(Types::Plugin::kNetwork, message_sender_,
                                      process_cache_, policies_features_broker_,
                                      device_user_, kBatchInterval);
    EXPECT_NE(nullptr, plugin_);
    SetPluginBatchSenderForTesting(plugin_.get(), std::move(batch_sender));

    EXPECT_CALL(*skel_factory_,
                Create(Types::BpfSkeleton::kNetwork, _, kBatchInterval))
        .WillOnce(
            DoAll(SaveArg<1>(&cbs_), Return(ByMove(std::move(bpf_skeleton)))));
    EXPECT_CALL(*batch_sender_, Start());
    EXPECT_TRUE(plugin_->Activate().ok());
  }

  void GenerateHierarchyFromExpectedProcesses(
      std::vector<std::unique_ptr<pb::Process>>* hierarchy,
      std::vector<pb::Process>* expected_hierarchy,
      std::vector<ExpectedProcess> expected) {
    for (const auto& p : expected) {
      hierarchy->push_back(std::make_unique<pb::Process>());
      hierarchy->back()->set_canonical_pid(p.pid);
      hierarchy->back()->set_canonical_uid(p.uid);
      hierarchy->back()->set_commandline(p.cmdline);
      hierarchy->back()->set_rel_start_time_s(p.rel_start_time_s);
      expected_hierarchy->emplace_back(*hierarchy->back());
    }
  }

  absl::Status fillTuple(const char local_addr[],
                         uint16_t local_port,
                         const char remote_addr[],
                         uint16_t remote_port,
                         bpf::cros_network_protocol protocol,
                         bpf::cros_synthetic_network_flow* flow) {
    auto& tuple = flow->flow_map_key.five_tuple;
    if (inet_pton(AF_INET, local_addr, &tuple.local_addr.addr4) == 1 &&
        inet_pton(AF_INET, remote_addr, &tuple.remote_addr.addr4) == 1) {
      tuple.local_port = local_port;
      tuple.remote_port = remote_port;
      tuple.family = bpf::CROS_FAMILY_AF_INET;
      tuple.protocol = protocol;
      return absl::OkStatus();
    } else if (inet_pton(AF_INET6, local_addr, tuple.local_addr.addr6) == 1 &&
               inet_pton(AF_INET6, remote_addr, tuple.remote_addr.addr6) == 1) {
      tuple.local_port = local_port;
      tuple.remote_port = remote_port;
      tuple.family = bpf::CROS_FAMILY_AF_INET6;
      tuple.protocol = protocol;
      return absl::OkStatus();
    }
    return absl::InternalError("invalid format for ip addresses");
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

TEST_F(NetworkPluginTestFixture, TestActivationFailureBadSkeleton) {
  auto plugin = plugin_factory_->Create(
      Types::Plugin::kNetwork, message_sender_, process_cache_,
      policies_features_broker_, device_user_, kBatchInterval);
  EXPECT_TRUE(plugin);
  SetPluginBatchSenderForTesting(plugin.get(),
                                 std::make_unique<BatchSenderType>());

  EXPECT_CALL(*skel_factory_,
              Create(Types::BpfSkeleton::kNetwork, _, kBatchInterval))
      .WillOnce(Return(ByMove(nullptr)));
  EXPECT_FALSE(plugin->Activate().ok());
}

TEST_F(NetworkPluginTestFixture, TestGetName) {
  ASSERT_EQ("Network", plugin_->GetName());
}
TEST_F(NetworkPluginTestFixture, TestBPFEventIsAvailable) {
  const bpf::cros_event socket_listen_event = {
      .data.network_event =
          {
              .type = bpf::cros_network_event_type::kNetworkSocketListen,
              .data.socket_listen =
                  {
                      /* 192.168.0.1 */
                      .family = bpf::CROS_FAMILY_AF_INET,
                      .protocol = bpf::CROS_PROTOCOL_TCP,
                      .process_info = {.task_info{
                          .pid = kDefaultPid, .start_time = kSpawnStartTime}},
                      .socket_type = SOCK_STREAM,
                      .port = 1234,
                      .ipv4_addr = 0x0100A8C0,
                  },
          },
      .type = bpf::kNetworkEvent,
  };
  EXPECT_CALL(*bpf_skeleton_, ConsumeEvent()).Times(1);
  // Notify the plugin that an event is available.
  cbs_.ring_buffer_read_ready_callback.Run();
  EXPECT_CALL(*message_sender_, SendMessage).Times(AnyNumber());
  cbs_.ring_buffer_event_callback.Run(socket_listen_event);
}

TEST_F(NetworkPluginTestFixture, TestWrongBPFEvent) {
  EXPECT_CALL(*bpf_skeleton_, ConsumeEvent()).Times(1);
  // Notify the plugin that an event is available.
  cbs_.ring_buffer_read_ready_callback.Run();
  EXPECT_CALL(*message_sender_, SendMessage).Times(0);
  cbs_.ring_buffer_event_callback.Run(
      bpf::cros_event{.type = bpf::kProcessEvent});
}

TEST_F(NetworkPluginTestFixture, TestSyntheticIpv4FlowEvent) {
  bpf::cros_synthetic_network_flow flow;
  constexpr char remote_addr[] = "168.152.10.1";
  constexpr char local_addr[] = "192.168.0.1";
  constexpr uint16_t local_port{4591};
  constexpr uint16_t remote_port{5231};
  ASSERT_EQ(fillTuple(local_addr, local_port, remote_addr, remote_port,
                      bpf::CROS_PROTOCOL_TCP, &flow),
            absl::OkStatus());

  auto& val = flow.flow_map_value;
  val.direction = bpf::CROS_SOCKET_DIRECTION_OUT;
  val.garbage_collect_me = false;
  constexpr uint32_t rx_bytes{1456};
  constexpr uint32_t tx_bytes{2563};
  constexpr uint32_t rx_bytes2{rx_bytes + 100};
  constexpr uint32_t tx_bytes2{tx_bytes + 124};
  val.rx_bytes = rx_bytes;
  val.tx_bytes = tx_bytes;

  flow.flow_map_value = val;
  flow.flow_map_value.has_full_process_info = false;
  flow.flow_map_value.process_info = kDefaultProcessInfo;

  auto flow1 = CreateCrosFlowEvent(flow);
  std::vector<std::unique_ptr<pb::Process>> hierarchy, hierarchy2;
  std::vector<pb::Process> expected_hierarchy;

  GenerateHierarchyFromExpectedProcesses(&hierarchy, &expected_hierarchy,
                                         kDefaultProcessHierarchy);
  GenerateHierarchyFromExpectedProcesses(&hierarchy2, &expected_hierarchy,
                                         kDefaultProcessHierarchy);

  auto& process =
      flow1.data.network_event.data.flow.flow_map_value.process_info.task_info;
  /* Three flows will be generated but only two events are expected.
  The second flow has the same tx/rx byte count so it should be ignored.*/
  EXPECT_CALL(*process_cache_,
              GetProcessHierarchy(process.pid, process.start_time, 2))
      .Times(2)
      .WillOnce(Return(ByMove(std::move(hierarchy))))
      .WillOnce(Return(ByMove(std::move(hierarchy2))));

  std::unique_ptr<pb::NetworkEventAtomicVariant> actual_sent_event;
  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .Times(2)
      .WillRepeatedly(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user,
                                     const std::string& sanitized_uname)> cb) {
            std::move(cb).Run(kDeviceUser, kSanitized);
          })));

  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(2)
      .WillRepeatedly([&actual_sent_event](
                          std::unique_ptr<pb::NetworkEventAtomicVariant> e) {
        actual_sent_event = std::move(e);
      });

  cbs_.ring_buffer_event_callback.Run(flow1);
  EXPECT_THAT(expected_hierarchy[0],
              EqualsProto(actual_sent_event->network_flow().process()));
  EXPECT_THAT(expected_hierarchy[1],
              EqualsProto(actual_sent_event->network_flow().parent_process()));
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().local_ip(),
            local_addr);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().local_port(),
            local_port);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().remote_ip(),
            remote_addr);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().remote_port(),
            remote_port);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().protocol(),
            pb::TCP);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().direction(),
            pb::NetworkFlow_Direction_OUTGOING);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().rx_bytes(),
            rx_bytes);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().tx_bytes(),
            tx_bytes);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().community_id_v1(),
            "1:xQuGZjr6e08tldWqhl7702m03YU=");

  // Identical event so no event should be generated.
  cbs_.ring_buffer_event_callback.Run(flow1);

  // a bit more traffic.
  val.rx_bytes = rx_bytes2;
  val.tx_bytes = tx_bytes2;
  auto flow2 = CreateCrosFlowEvent(flow);
  cbs_.ring_buffer_event_callback.Run(flow2);
  EXPECT_THAT(expected_hierarchy[0],
              EqualsProto(actual_sent_event->network_flow().process()));
  EXPECT_THAT(expected_hierarchy[1],
              EqualsProto(actual_sent_event->network_flow().parent_process()));
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().local_ip(),
            local_addr);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().local_port(),
            local_port);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().remote_ip(),
            remote_addr);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().remote_port(),
            remote_port);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().protocol(),
            pb::TCP);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().direction(),
            pb::NetworkFlow_Direction_OUTGOING);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().rx_bytes(),
            rx_bytes2 - rx_bytes);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().tx_bytes(),
            tx_bytes2 - tx_bytes);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().community_id_v1(),
            "1:xQuGZjr6e08tldWqhl7702m03YU=");
}

TEST_F(NetworkPluginTestFixture, TestSyntheticIpv6FlowEvent) {
  bpf::cros_synthetic_network_flow flow;
  constexpr char remote_addr[] = "fd00::65:4cc4:d4ff:fe18:d7b9";
  constexpr char local_addr[] = "fd00::65:fb92:6a08:5c09:b81";
  constexpr uint16_t local_port{4591};
  constexpr uint16_t remote_port{5231};
  ASSERT_EQ(fillTuple(local_addr, local_port, remote_addr, remote_port,
                      bpf::CROS_PROTOCOL_TCP, &flow),
            absl::OkStatus());

  auto& val = flow.flow_map_value;
  val.direction = bpf::CROS_SOCKET_DIRECTION_OUT;
  val.garbage_collect_me = false;

  constexpr uint32_t rx_bytes{1456};
  constexpr uint32_t tx_bytes{2563};
  val.rx_bytes = rx_bytes;
  val.tx_bytes = tx_bytes;

  flow.flow_map_value.has_full_process_info = false;
  flow.flow_map_value.process_info = kDefaultProcessInfo;

  auto flowEvent = CreateCrosFlowEvent(flow);
  std::vector<std::unique_ptr<pb::Process>> hierarchy;
  std::vector<pb::Process> expected_hierarchy;
  for (const auto& p : kDefaultProcessHierarchy) {
    hierarchy.push_back(std::make_unique<pb::Process>());
    hierarchy.back()->set_canonical_pid(p.pid);
    hierarchy.back()->set_canonical_uid(p.uid);
    hierarchy.back()->set_commandline(p.cmdline);
    hierarchy.back()->set_rel_start_time_s(p.rel_start_time_s);
    expected_hierarchy.emplace_back(*hierarchy.back());
  }
  auto& process = flowEvent.data.network_event.data.flow.flow_map_value
                      .process_info.task_info;
  EXPECT_CALL(*process_cache_,
              GetProcessHierarchy(process.pid, process.start_time, 2))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

  std::unique_ptr<pb::NetworkEventAtomicVariant> actual_sent_event;
  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .WillOnce(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user,
                                     const std::string& sanitized_uname)> cb) {
            std::move(cb).Run(kDeviceUser, kSanitized);
          })));

  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(1)
      .WillOnce([&actual_sent_event](
                    std::unique_ptr<pb::NetworkEventAtomicVariant> e) {
        actual_sent_event = std::move(e);
      });

  cbs_.ring_buffer_event_callback.Run(flowEvent);
  EXPECT_THAT(expected_hierarchy[0],
              EqualsProto(actual_sent_event->network_flow().process()));
  EXPECT_THAT(expected_hierarchy[1],
              EqualsProto(actual_sent_event->network_flow().parent_process()));
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().local_ip(),
            local_addr);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().local_port(),
            local_port);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().remote_ip(),
            remote_addr);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().remote_port(),
            remote_port);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().protocol(),
            pb::TCP);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().direction(),
            pb::NetworkFlow_Direction_OUTGOING);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().rx_bytes(),
            rx_bytes);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().tx_bytes(),
            tx_bytes);
  EXPECT_EQ(actual_sent_event->network_flow().network_flow().community_id_v1(),
            "1:Ri1ArKrJ+g/QrTLp8fPFFQd3jcw=");
}

TEST_F(NetworkPluginTestFixture, TestSyntheticFlowEventWithFullProcessInfo) {
  bpf::cros_synthetic_network_flow flow;

  constexpr char remote_addr[] = "168.152.10.1";
  constexpr char local_addr[] = "192.168.0.1";
  constexpr uint16_t local_port{4591};
  constexpr uint16_t remote_port{5231};
  ASSERT_EQ(fillTuple(local_addr, local_port, remote_addr, remote_port,
                      bpf::CROS_PROTOCOL_TCP, &flow),
            absl::OkStatus());

  auto& val = flow.flow_map_value;
  val.direction = bpf::CROS_SOCKET_DIRECTION_OUT;
  val.garbage_collect_me = false;
  constexpr uint32_t rx_bytes{1456};
  constexpr uint32_t tx_bytes{2563};
  val.tx_bytes = tx_bytes;
  val.rx_bytes = rx_bytes;

  flow.flow_map_value.has_full_process_info = true;
  flow.flow_map_value.process_info = kDefaultProcessInfo;

  auto event = CreateCrosFlowEvent(flow);
  std::vector<std::unique_ptr<pb::Process>> hierarchy;
  hierarchy.push_back(std::make_unique<pb::Process>());
  hierarchy.back()->set_canonical_pid(kDefaultProcessHierarchy[1].pid);
  hierarchy.back()->set_canonical_uid(kDefaultProcessHierarchy[1].uid);
  hierarchy.back()->set_commandline(kDefaultProcessHierarchy[1].cmdline);
  hierarchy.back()->set_rel_start_time_s(
      kDefaultProcessHierarchy[1].rel_start_time_s);
  std::vector<pb::Process> expected_hierarchy;
  expected_hierarchy.emplace_back(*hierarchy.back());

  auto& process =
      event.data.network_event.data.flow.flow_map_value.process_info.task_info;

  std::list<std::string> redacted_usernames = {"username"};
  EXPECT_CALL(*device_user_, GetUsernamesForRedaction)
      .WillOnce(Return(redacted_usernames));
  auto& process_info = flow.flow_map_value.process_info;
  EXPECT_CALL(*process_cache_, FillProcessFromBpf(_, _, _, redacted_usernames))
      .WillOnce(WithArg<2>(
          Invoke([&process_info](cros_xdr::reporting::Process* process_proto) {
            process_proto->set_canonical_pid(process_info.task_info.pid);
            process_proto->set_canonical_uid(process_info.task_info.uid);
            process_proto->set_rel_start_time_s(
                process_info.task_info.start_time);
            process_proto->mutable_image()->set_inode(
                process_info.image_info.inode);
            process_proto->mutable_image()->set_mode(
                process_info.image_info.mode);
            process_proto->set_meta_first_appearance(true);
          })));
  // Expect an attempt to use cache to retrieve parent.
  EXPECT_CALL(*process_cache_,
              GetProcessHierarchy(process.ppid, process.parent_start_time, 1))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .WillOnce(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user,
                                     const std::string& sanitized_uname)> cb) {
            std::move(cb).Run(kDeviceUser, kSanitized);
          })));

  pb::NetworkEventAtomicVariant actual_sent_event;
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(1)
      .WillOnce([&actual_sent_event](
                    std::unique_ptr<pb::NetworkEventAtomicVariant> e) {
        actual_sent_event = *e;
      });
  cbs_.ring_buffer_event_callback.Run(event);
  auto& actual_process = actual_sent_event.network_flow().process();
  /* Expect that the process proto is filled by the info in the bpf event.*/
  EXPECT_THAT(actual_process.canonical_pid(), process_info.task_info.pid);
  EXPECT_THAT(actual_process.canonical_uid(), process_info.task_info.uid);
  EXPECT_THAT(actual_process.rel_start_time_s(),
              process_info.task_info.start_time);

  EXPECT_THAT(actual_process.image().inode(), process_info.image_info.inode);
  EXPECT_THAT(actual_process.image().mode(), process_info.image_info.mode);
  EXPECT_THAT(actual_process.meta_first_appearance(), true);

  EXPECT_THAT(expected_hierarchy[0],
              EqualsProto(actual_sent_event.network_flow().parent_process()));
  EXPECT_EQ(actual_sent_event.network_flow().network_flow().local_ip(),
            local_addr);
  EXPECT_EQ(actual_sent_event.network_flow().network_flow().local_port(),
            local_port);
  EXPECT_EQ(actual_sent_event.network_flow().network_flow().remote_ip(),
            remote_addr);
  EXPECT_EQ(actual_sent_event.network_flow().network_flow().remote_port(),
            remote_port);
  EXPECT_EQ(actual_sent_event.network_flow().network_flow().protocol(),
            pb::TCP);
  EXPECT_EQ(actual_sent_event.network_flow().network_flow().direction(),
            pb::NetworkFlow_Direction_OUTGOING);
  EXPECT_EQ(actual_sent_event.network_flow().network_flow().rx_bytes(),
            rx_bytes);
  EXPECT_EQ(actual_sent_event.network_flow().network_flow().tx_bytes(),
            tx_bytes);
  EXPECT_EQ(actual_sent_event.network_flow().network_flow().community_id_v1(),
            "1:xQuGZjr6e08tldWqhl7702m03YU=");
}

TEST_F(NetworkPluginTestFixture, TestSSDPFiltering) {
  // UUT should ignore SSDP broadcast traffic from patchpanel.
  const uint32_t kPatchPanelPid{0xDEADBEEF};
  const uint32_t kPatchPanelPpid{0xFEED};
  const uint32_t kPatchPanelStartTime{123098};

  std::vector<std::unique_ptr<pb::Process>> patch_panel_hierarchy;
  patch_panel_hierarchy.push_back(std::make_unique<pb::Process>());
  patch_panel_hierarchy.back()->set_canonical_pid(kPatchPanelPid);
  pb::FileImage patch_panel_image = pb::FileImage();
  patch_panel_image.set_pathname("/usr/bin/patchpaneld");
  *patch_panel_hierarchy.back()->mutable_image() = patch_panel_image;

  patch_panel_hierarchy.push_back(std::make_unique<pb::Process>());
  patch_panel_hierarchy.back()->set_canonical_pid(kPatchPanelPpid);
  EXPECT_CALL(*process_cache_,
              GetProcessHierarchy(kPatchPanelPid, kPatchPanelStartTime, 2))
      .WillOnce(Return(ByMove(std::move(patch_panel_hierarchy))));

  EXPECT_CALL(*batch_sender_, Enqueue(_)).Times(0);

  bpf::cros_synthetic_network_flow patchpaneld_flow;

  constexpr char remote_addr[] = "10.10.10.10";
  constexpr char local_addr[] = "239.255.255.250";
  constexpr uint16_t local_port{1900};
  constexpr uint16_t remote_port{2500};
  ASSERT_EQ(fillTuple(local_addr, local_port, remote_addr, remote_port,
                      bpf::CROS_PROTOCOL_UDP, &patchpaneld_flow),
            absl::OkStatus());
  auto& patchpaneld_value = patchpaneld_flow.flow_map_value;
  constexpr uint32_t rx_bytes{24};
  constexpr uint32_t tx_bytes{48};
  patchpaneld_value.rx_bytes = rx_bytes;
  patchpaneld_value.tx_bytes = tx_bytes;

  patchpaneld_value.direction = bpf::CROS_SOCKET_DIRECTION_OUT;
  patchpaneld_value.process_info.task_info.pid = kPatchPanelPid;
  patchpaneld_value.process_info.task_info.start_time = kPatchPanelStartTime;
  patchpaneld_flow.flow_map_value.process_info.task_info.pid = kPatchPanelPid;
  patchpaneld_flow.flow_map_value.has_full_process_info = false;
  auto flow_event = CreateCrosFlowEvent(patchpaneld_flow);
  cbs_.ring_buffer_event_callback.Run(flow_event);
}

TEST_F(NetworkPluginTestFixture, TestAvahiScriptFiltering) {
  // UUT should ignore spammy network traffic from avahi bash script.
  const uint32_t kAvahiPid{0xFADE};
  const uint32_t kAvahiPpid{0xEDAF};
  const uint32_t kAvahiStartTime{9876};

  std::vector<std::unique_ptr<pb::Process>> avahi_hierarchy;
  avahi_hierarchy.push_back(std::make_unique<pb::Process>());
  avahi_hierarchy.back()->set_canonical_pid(kAvahiPid);
  avahi_hierarchy.back()->set_commandline("avahi-daemon: running");
  avahi_hierarchy.push_back(std::make_unique<pb::Process>());
  avahi_hierarchy.back()->set_canonical_pid(kAvahiPpid);
  EXPECT_CALL(*process_cache_,
              GetProcessHierarchy(kAvahiPid, kAvahiStartTime, 2))
      .WillOnce(Return(ByMove(std::move(avahi_hierarchy))));
  EXPECT_CALL(*batch_sender_, Enqueue(_)).Times(0);

  bpf::cros_synthetic_network_flow avahi_flow;
  auto& avahi_tuple = avahi_flow.flow_map_key.five_tuple;
  auto& avahi_value = avahi_flow.flow_map_value;
  avahi_value.rx_bytes = 24;
  avahi_value.tx_bytes = 48;
  inet_pton(AF_INET, "10.10.10.10", &avahi_tuple.remote_addr.addr4);
  inet_pton(AF_INET, "239.255.255.250", &avahi_tuple.local_addr.addr4);
  avahi_tuple.local_port = 12;
  avahi_tuple.protocol = bpf::CROS_PROTOCOL_TCP;
  avahi_value.process_info.task_info.pid = kAvahiPid;
  avahi_value.process_info.task_info.start_time = kAvahiStartTime;
  avahi_value.has_full_process_info = false;
  auto avahi_event = CreateCrosFlowEvent(avahi_flow);
  cbs_.ring_buffer_event_callback.Run(avahi_event);
}

TEST_F(NetworkPluginTestFixture, TestNetworkPluginListenEvent) {
  // Descending order in time starting from the youngest.
  std::vector<std::unique_ptr<pb::Process>> hierarchy;
  std::vector<pb::Process> expected_hierarchy;
  for (const auto& p : kDefaultProcessHierarchy) {
    hierarchy.push_back(std::make_unique<pb::Process>());
    hierarchy.back()->set_canonical_pid(p.pid);
    hierarchy.back()->set_canonical_uid(p.uid);
    hierarchy.back()->set_commandline(p.cmdline);
    hierarchy.back()->set_rel_start_time_s(p.rel_start_time_s);
    expected_hierarchy.emplace_back(*hierarchy.back());
  }
  const bpf::cros_event a = {
      .data.network_event =
          {
              .type = bpf::cros_network_event_type::kNetworkSocketListen,
              .data.socket_listen =
                  {
                      /* 192.168.0.1 */
                      .family = bpf::CROS_FAMILY_AF_INET,
                      .protocol = bpf::CROS_PROTOCOL_TCP,
                      .process_info{.task_info{.pid = kDefaultPid,
                                               .start_time = kSpawnStartTime}},
                      .socket_type = SOCK_STREAM,
                      .port = 1234,
                      .ipv4_addr = 0x0100A8C0,
                  },
          },
      .type = bpf::kNetworkEvent,
  };
  const auto& socket_event = a.data.network_event.data.socket_listen;
  EXPECT_CALL(
      *process_cache_,
      GetProcessHierarchy(socket_event.process_info.task_info.pid,
                          socket_event.process_info.task_info.start_time, 2))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .WillOnce(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user,
                                     const std::string& sanitized_uname)> cb) {
            std::move(cb).Run(kDeviceUser, kSanitized);
          })));

  std::unique_ptr<pb::NetworkEventAtomicVariant> actual_sent_event;
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(1)
      .WillOnce([&actual_sent_event](
                    std::unique_ptr<pb::NetworkEventAtomicVariant> e) {
        actual_sent_event = std::move(e);
      });

  cbs_.ring_buffer_event_callback.Run(a);
  EXPECT_THAT(
      expected_hierarchy[0],
      EqualsProto(actual_sent_event->network_socket_listen().process()));
  EXPECT_THAT(
      expected_hierarchy[1],
      EqualsProto(actual_sent_event->network_socket_listen().parent_process()));
  EXPECT_EQ(actual_sent_event->network_socket_listen().socket().bind_addr(),
            "192.168.0.1");
  EXPECT_EQ(actual_sent_event->network_socket_listen().socket().bind_port(),
            socket_event.port);
  EXPECT_EQ(actual_sent_event->network_socket_listen().socket().protocol(),
            pb::NetworkProtocol::TCP);
}
using IPv6TestParam = std::pair<std::array<uint8_t, 16>, std::string>;
class IPv6VariationsTestFixture
    : public NetworkPluginTestFixture,
      public ::testing::WithParamInterface<IPv6TestParam> {};

/* Make sure that the compressed formatting of IPv6 is correct.*/
INSTANTIATE_TEST_SUITE_P(
    TestIPv6AddressFormatting,
    IPv6VariationsTestFixture,
    ::testing::Values(
        IPv6TestParam{{0xb4, 0x75, 0x34, 0x24, 0xde, 0x03, 0xa0, 0x90, 0xa0,
                       0x86, 0xb5, 0xff, 0x3c, 0x12, 0xb4, 0x56},
                      "b475:3424:de03:a090:a086:b5ff:3c12:b456"},
        /* 0: Test correct IPv6 compression of stripping leading zeroes.*/
        IPv6TestParam{{0xb4, 0x75, 00, 0x24, 0xde, 0x03, 0xa0, 0x90, 0xa0, 0x86,
                       0x0, 0xff, 0x3c, 0x12, 0xb4, 0x56},
                      "b475:24:de03:a090:a086:ff:3c12:b456"},
        /* 1: Test that a single group of 0's is not fully compressed. */
        IPv6TestParam{{0xb4, 0x75, 0x34, 0x24, 0x0, 0x0, 0xa0, 0x90, 0xa0, 0x86,
                       0xb5, 0xff, 0x3c, 0x12, 0xb4, 0x56},
                      "b475:3424:0:a090:a086:b5ff:3c12:b456"},
        /* 2: Test that multiple groups of 0s are compressed into :: */
        IPv6TestParam{{0xb4, 0x75, 0x34, 0x24, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                       0xb5, 0xff, 0x3c, 0x12, 0xb4, 0x56},
                      "b475:3424::b5ff:3c12:b456"},
        /* 3:Test that only the left most groups of 0's are compressed into ::*/
        IPv6TestParam{{0xb4, 0x75, 0x34, 0x24, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                       0xb5, 0xff, 0x0, 0x0, 0x0, 0x0},
                      "b475:3424::b5ff:0:0"}),
    [](::testing::TestParamInfo<IPv6TestParam> p) -> std::string {
      switch (p.index) {
        case 0:
          return "StripLeadingZeroes";
        case 1:
          return "Single0GroupNotCompressed";
        case 2:
          return "Multiple0GroupsCompressed";
        case 3:
          return "LeftMost0GroupsCompressed";
        default:
          return absl::StrFormat("MysteryTestCase%d", p.index);
      }
    });

TEST_P(IPv6VariationsTestFixture, TestSocketListenIPv6) {
  constexpr bpf::time_ns_t kSpawnStartTime = 2222;
  // Descending order in time starting from the youngest.
  std::vector<std::unique_ptr<pb::Process>> hierarchy;
  std::vector<pb::Process> expected_hierarchy;
  for (const auto& p : kDefaultProcessHierarchy) {
    hierarchy.push_back(std::make_unique<pb::Process>());
    hierarchy.back()->set_canonical_pid(p.pid);
    hierarchy.back()->set_canonical_uid(p.uid);
    hierarchy.back()->set_commandline(p.cmdline);
    hierarchy.back()->set_rel_start_time_s(p.rel_start_time_s);
    expected_hierarchy.emplace_back(*hierarchy.back());
  }
  bpf::cros_event a = {
      .data.network_event =
          {
              .type = bpf::cros_network_event_type::kNetworkSocketListen,
              .data.socket_listen =
                  {/* 192.168.0.1 */
                   .family = bpf::CROS_FAMILY_AF_INET6,
                   .protocol = bpf::CROS_PROTOCOL_TCP,
                   .process_info{.task_info{.pid = kDefaultPid,
                                            .start_time = kSpawnStartTime}},
                   .socket_type = 0,
                   .port = 1234},
          },
      .type = bpf::kNetworkEvent,
  };
  auto& ipv6_field = a.data.network_event.data.socket_listen.ipv6_addr;
  memmove(ipv6_field, GetParam().first.data(), sizeof(ipv6_field));
  auto expected_ipaddr = GetParam().second;
  const auto& socket_event = a.data.network_event.data.socket_listen;
  EXPECT_CALL(
      *process_cache_,
      GetProcessHierarchy(socket_event.process_info.task_info.pid,
                          socket_event.process_info.task_info.start_time, 2))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .WillOnce(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user,
                                     const std::string& sanitized_uname)> cb) {
            std::move(cb).Run(kDeviceUser, kSanitized);
          })));

  std::unique_ptr<pb::NetworkEventAtomicVariant> actual_sent_event;
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(1)
      .WillOnce([&actual_sent_event](
                    std::unique_ptr<pb::NetworkEventAtomicVariant> e) {
        actual_sent_event = std::move(e);
      });

  cbs_.ring_buffer_event_callback.Run(a);
  EXPECT_EQ(actual_sent_event->network_socket_listen().socket().bind_addr(),
            expected_ipaddr);
}

class ProtocolVariationsTestFixture
    : public NetworkPluginTestFixture,
      public ::testing::WithParamInterface<bpf::cros_network_protocol> {};
using SocketTypeTestParam = std::pair<int, pb::SocketType>;
class SocketTypeVariationsTestFixture
    : public NetworkPluginTestFixture,
      public ::testing::WithParamInterface<SocketTypeTestParam> {};

/* Test all possible network protocols. */
INSTANTIATE_TEST_SUITE_P(
    TestDifferentProtocols,
    ProtocolVariationsTestFixture,
    ::testing::Values(bpf::cros_network_protocol::CROS_PROTOCOL_ICMP,
                      bpf::cros_network_protocol::CROS_PROTOCOL_RAW,
                      bpf::cros_network_protocol::CROS_PROTOCOL_TCP,
                      bpf::cros_network_protocol::CROS_PROTOCOL_UDP,
                      bpf::cros_network_protocol::CROS_PROTOCOL_UNKNOWN),
    [](::testing::TestParamInfo<bpf::cros_network_protocol> p) -> std::string {
      switch (p.param) {
        case bpf::cros_network_protocol::CROS_PROTOCOL_ICMP:
        case bpf::cros_network_protocol::CROS_PROTOCOL_ICMP6:
          return "ICMP";
        case bpf::cros_network_protocol::CROS_PROTOCOL_RAW:
          return "RAW";
        case bpf::cros_network_protocol::CROS_PROTOCOL_TCP:
          return "TCP";
        case bpf::cros_network_protocol::CROS_PROTOCOL_UDP:
          return "UDP";
        case bpf::cros_network_protocol::CROS_PROTOCOL_UNKNOWN:
          return "UnknownProtocol";
      }
    });

TEST_P(ProtocolVariationsTestFixture, TestSocketListenProtocols) {
  constexpr bpf::time_ns_t kSpawnStartTime = 2222;
  // Descending order in time starting from the youngest.
  std::vector<std::unique_ptr<pb::Process>> hierarchy;
  std::vector<pb::Process> expected_hierarchy;
  for (const auto& p : kDefaultProcessHierarchy) {
    hierarchy.push_back(std::make_unique<pb::Process>());
    hierarchy.back()->set_canonical_pid(p.pid);
    hierarchy.back()->set_canonical_uid(p.uid);
    hierarchy.back()->set_commandline(p.cmdline);
    hierarchy.back()->set_rel_start_time_s(p.rel_start_time_s);
    expected_hierarchy.emplace_back(*hierarchy.back());
  }
  bpf::cros_event a = {
      .data.network_event =
          {
              .type = bpf::cros_network_event_type::kNetworkSocketListen,
              .data.socket_listen =
                  {.family = bpf::CROS_FAMILY_AF_INET,
                   .protocol = bpf::CROS_PROTOCOL_TCP,
                   .process_info{.task_info = {.pid = kDefaultPid,
                                               .start_time = kSpawnStartTime}},
                   .socket_type = SOCK_STREAM,
                   .port = 1234,
                   .ipv4_addr = 0x1020304},
          },
      .type = bpf::kNetworkEvent,
  };
  a.data.network_event.data.socket_listen.protocol = GetParam();
  pb::NetworkProtocol expected_protocol;
  switch (a.data.network_event.data.socket_listen.protocol) {
    case bpf::cros_network_protocol::CROS_PROTOCOL_ICMP:
    case bpf::cros_network_protocol::CROS_PROTOCOL_ICMP6:
      expected_protocol = pb::NetworkProtocol::ICMP;
      break;
    case bpf::cros_network_protocol::CROS_PROTOCOL_RAW:
      expected_protocol = pb::NetworkProtocol::RAW;
      break;
    case bpf::cros_network_protocol::CROS_PROTOCOL_TCP:
      expected_protocol = pb::NetworkProtocol::TCP;
      break;
    case bpf::cros_network_protocol::CROS_PROTOCOL_UDP:
      expected_protocol = pb::NetworkProtocol::UDP;
      break;
    case bpf::cros_network_protocol::CROS_PROTOCOL_UNKNOWN:
      expected_protocol = pb::NetworkProtocol::NETWORK_PROTOCOL_UNKNOWN;
      break;
  }
  const auto& socket_event = a.data.network_event.data.socket_listen;
  EXPECT_CALL(
      *process_cache_,
      GetProcessHierarchy(socket_event.process_info.task_info.pid,
                          socket_event.process_info.task_info.start_time, 2))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .WillOnce(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user,
                                     const std::string& sanitized_uname)> cb) {
            std::move(cb).Run(kDeviceUser, kSanitized);
          })));

  std::unique_ptr<pb::NetworkEventAtomicVariant> actual_sent_event;
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(1)
      .WillOnce([&actual_sent_event](
                    std::unique_ptr<pb::NetworkEventAtomicVariant> e) {
        actual_sent_event = std::move(e);
      });

  cbs_.ring_buffer_event_callback.Run(a);
  EXPECT_EQ(actual_sent_event->network_socket_listen().socket().protocol(),
            expected_protocol);
}

/* Test all possible socket types. */
INSTANTIATE_TEST_SUITE_P(
    TestDifferentSocketTypes,
    SocketTypeVariationsTestFixture,
    ::testing::Values(
        SocketTypeTestParam{__socket_type::SOCK_STREAM,
                            pb::SocketType::SOCK_STREAM},
        SocketTypeTestParam{__socket_type::SOCK_DGRAM,
                            pb::SocketType::SOCK_DGRAM},
        SocketTypeTestParam{__socket_type::SOCK_RAW, pb::SocketType::SOCK_RAW},
        SocketTypeTestParam{__socket_type::SOCK_RDM, pb::SocketType::SOCK_RDM},
        SocketTypeTestParam{__socket_type::SOCK_PACKET,
                            pb::SocketType::SOCK_PACKET},
        SocketTypeTestParam{__socket_type::SOCK_SEQPACKET,
                            pb::SocketType::SOCK_SEQPACKET}),
    [](::testing::TestParamInfo<SocketTypeTestParam> p) -> std::string {
      switch (p.param.first) {
        case __socket_type::SOCK_STREAM:
          return "STREAM";
        case __socket_type::SOCK_RAW:
          return "RAW";
        case __socket_type::SOCK_DGRAM:
          return "DATAGRAM";
        case __socket_type::SOCK_RDM:
          return "RDM";
        case __socket_type::SOCK_PACKET:
          return "PACKET";
        case __socket_type::SOCK_SEQPACKET:
          return "SEQPACKET";
        default:
          return "UNKNOWN";
      }
    });

TEST_P(SocketTypeVariationsTestFixture, TestSocketListenSocketTypes) {
  constexpr bpf::time_ns_t kSpawnStartTime = 2222;
  // Descending order in time starting from the youngest.
  std::vector<std::unique_ptr<pb::Process>> hierarchy;
  std::vector<pb::Process> expected_hierarchy;
  for (const auto& p : kDefaultProcessHierarchy) {
    hierarchy.push_back(std::make_unique<pb::Process>());
    hierarchy.back()->set_canonical_pid(p.pid);
    hierarchy.back()->set_canonical_uid(p.uid);
    hierarchy.back()->set_commandline(p.cmdline);
    hierarchy.back()->set_rel_start_time_s(p.rel_start_time_s);
    expected_hierarchy.emplace_back(*hierarchy.back());
  }
  bpf::cros_event a = {
      .data.network_event =
          {
              .type = bpf::cros_network_event_type::kNetworkSocketListen,
              .data.socket_listen = {.family = bpf::CROS_FAMILY_AF_INET,
                                     .protocol = bpf::CROS_PROTOCOL_TCP,
                                     .process_info{.task_info{
                                         .pid = kDefaultPid,
                                         .start_time = kSpawnStartTime}},
                                     .socket_type = SOCK_STREAM,
                                     .port = 1234,
                                     .ipv4_addr = 0x1020304},
          },
      .type = bpf::kNetworkEvent,
  };
  a.data.network_event.data.socket_listen.socket_type = GetParam().first;
  auto expected_socket_type = GetParam().second;
  const auto& socket_event = a.data.network_event.data.socket_listen;
  EXPECT_CALL(
      *process_cache_,
      GetProcessHierarchy(socket_event.process_info.task_info.pid,
                          socket_event.process_info.task_info.start_time, 2))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

  EXPECT_CALL(*device_user_, GetDeviceUserAsync)
      .WillOnce(WithArg<0>(Invoke(
          [](base::OnceCallback<void(const std::string& device_user,
                                     const std::string& sanitized_uname)> cb) {
            std::move(cb).Run(kDeviceUser, kSanitized);
          })));

  std::unique_ptr<pb::NetworkEventAtomicVariant> actual_sent_event;
  EXPECT_CALL(*batch_sender_, Enqueue(_))
      .Times(1)
      .WillOnce([&actual_sent_event](
                    std::unique_ptr<pb::NetworkEventAtomicVariant> e) {
        actual_sent_event = std::move(e);
      });

  cbs_.ring_buffer_event_callback.Run(a);
  EXPECT_EQ(actual_sent_event->network_socket_listen().socket().socket_type(),
            expected_socket_type);
}

struct CommunityHashTestParam {
  std::string source_address;
  std::string dest_address;
  uint16_t source_port;
  uint16_t dest_port;
  bpf::cros_network_protocol protocol;
  std::string expected;
};
class CommunityHashingTestFixture
    : public ::testing::Test,
      public ::testing::WithParamInterface<CommunityHashTestParam> {
 public:
  absl::StatusOr<std::array<uint8_t, 16>> TryIPv6StringToNBOBuffer(
      std::string_view in) {
    struct in6_addr addr;
    if (inet_pton(AF_INET6, in.data(), &addr) != 1) {
      return absl::InvalidArgumentError(
          absl::StrFormat("%s is not a valid ipv6 address.", in));
    }
    std::array<uint8_t, sizeof(addr.__in6_u.__u6_addr8)> rv;
    memmove(rv.data(), &addr.__in6_u.__u6_addr8[0], rv.size());
    return rv;
  }

  absl::StatusOr<std::array<uint8_t, 4>> TryIPv4StringToNBOBuffer(
      std::string_view in) {
    struct in_addr addr;
    if (inet_pton(AF_INET, in.data(), &addr) != 1) {
      return absl::InvalidArgumentError(
          absl::StrFormat("%s is not a valid ipv4 address.", in));
    }
    std::array<uint8_t, 4> rv;
    memmove(rv.data(), &addr.s_addr, rv.size());
    return rv;
  }
};
INSTANTIATE_TEST_SUITE_P(
    CommunityIDHashing,
    CommunityHashingTestFixture,
    ::testing::Values(
        // Same ip addr but different port.
        CommunityHashTestParam{
            // idx 0.
            .source_address = "b475:3424:de03:a090:a086:b5ff:3c12:b456",
            .dest_address = "b475:3424:de03:a090:a086:b5ff:3c12:b456",
            .source_port = 456,
            .dest_port = 457,
            .protocol = bpf::CROS_PROTOCOL_TCP,
            .expected = "1:9nlcNcNqbWThbbrqcZ653+nS/Ig="},

        // Same port but source address has a smaller IP address.
        CommunityHashTestParam{
            // idx 1.
            .source_address = "b475:3424:de03:a090:a086:b5ff:3c12:b453",
            .dest_address = "b475:3424:de03:a090:a086:b5ff:3c12:b456",
            .source_port = 457,
            .dest_port = 457,
            .protocol = bpf::CROS_PROTOCOL_UDP,
            .expected = "1:0bk6xBJMSDtsXhLKWuSD1waPfOg="},
        // Same port but dest address has a smaller IP address.
        CommunityHashTestParam{
            // idx 2.
            .source_address = "b475:3424:de03:a090:a086:b5ff:3c12:b456",
            .dest_address = "b475:3424:de03:a090:a086:b5ff:3c12:b453",
            .source_port = 457,
            .dest_port = 457,
            .protocol = bpf::CROS_PROTOCOL_UDP,
            .expected = "1:0bk6xBJMSDtsXhLKWuSD1waPfOg="},
        // Same ip addr but different port.
        CommunityHashTestParam{// idx 3.
                               .source_address = "192.168.0.1",
                               .dest_address = "192.168.0.1",
                               .source_port = 456,
                               .dest_port = 457,
                               .protocol = bpf::CROS_PROTOCOL_TCP,
                               .expected = "1:wtrJ3294c/p34IEHKppjTVgTvmY="},
        // Same port but source address has a smaller IP address.
        CommunityHashTestParam{// idx 4.
                               .source_address = "192.168.0.0",
                               .dest_address = "192.168.0.1",
                               .source_port = 457,
                               .dest_port = 457,
                               .protocol = bpf::CROS_PROTOCOL_TCP,
                               .expected = "1:fxjiNC2ogHm2gNZIiJssJkyUiGE="},
        // Same port but dest address has a smaller IP address.
        CommunityHashTestParam{// idx 5.
                               .source_address = "192.168.0.1",
                               .dest_address = "192.168.0.0",
                               .source_port = 457,
                               .dest_port = 457,
                               .protocol = bpf::CROS_PROTOCOL_TCP,
                               .expected = "1:fxjiNC2ogHm2gNZIiJssJkyUiGE="}),
    [](::testing::TestParamInfo<CommunityHashTestParam> p) -> std::string {
      switch (p.index) {
        case 0:
          return "IPv6SameAddrDifferentPorts";
        case 1:
          return "IPv6SourceAddressSmaller";
        case 2:
          return "IPv6DestAddrSmaller";
        case 3:
          return "IPv4SameAddrDifferentPorts";
        case 4:
          return "IPv4SourceAddressSmaller";
        case 5:
          return "IPv4DestAddrSmaller";
        default:
          return absl::StrFormat("MysteryTestCase%d", p.index);
      }
    });

TEST_P(CommunityHashingTestFixture, CommunityFlowIDHash) {
  auto i = GetParam();
  auto ipv4_source = TryIPv4StringToNBOBuffer(i.source_address);
  auto ipv4_dest = TryIPv4StringToNBOBuffer(i.dest_address);
  auto ipv6_source = TryIPv6StringToNBOBuffer(i.source_address);
  auto ipv6_dest = TryIPv6StringToNBOBuffer(i.dest_address);
  absl::Span<const uint8_t> source, dest;
  if (ipv4_source.ok()) {
    source = absl::MakeSpan(ipv4_source.value());
  } else if (ipv6_source.ok()) {
    source = absl::MakeSpan(ipv6_source.value());
  }

  if (ipv4_dest.ok()) {
    dest = absl::MakeSpan(ipv4_dest.value());
  } else if (ipv6_dest.ok()) {
    dest = absl::MakeSpan(ipv6_dest.value());
  }
  auto result = NetworkPlugin::ComputeCommunityHashv1(
      source, dest, i.source_port, i.dest_port, i.protocol);
  EXPECT_EQ(result, i.expected);
}
}  // namespace secagentd::testing
