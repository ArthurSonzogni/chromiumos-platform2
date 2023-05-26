// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/socket.h>
#include <cstddef>
#include <iterator>
#include <memory>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "base/memory/scoped_refptr.h"
#include "gmock/gmock.h"  // IWYU pragma: keep
#include "google/protobuf/message_lite.h"
#include "google/protobuf/stubs/casts.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/plugins.h"
#include "secagentd/policies_features_broker.h"
#include "secagentd/proto/security_xdr_events.pb.h"
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
}  // namespace

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Ref;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

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
    google::protobuf::down_cast<NetworkPlugin*>(plugin)
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
    EXPECT_OK(plugin_->Activate());
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
  EXPECT_CALL(*bpf_skeleton_, ConsumeEvent()).Times(1);
  // Notify the plugin that an event is available.
  cbs_.ring_buffer_read_ready_callback.Run();
  EXPECT_CALL(*message_sender_, SendMessage).Times(AnyNumber());
  cbs_.ring_buffer_event_callback.Run(
      bpf::cros_event{.type = bpf::kNetworkEvent});
}

TEST_F(NetworkPluginTestFixture, TestWrongBPFEvent) {
  EXPECT_CALL(*bpf_skeleton_, ConsumeEvent()).Times(1);
  // Notify the plugin that an event is available.
  cbs_.ring_buffer_read_ready_callback.Run();
  EXPECT_CALL(*message_sender_, SendMessage).Times(0);
  cbs_.ring_buffer_event_callback.Run(
      bpf::cros_event{.type = bpf::kProcessEvent});
}

TEST_F(NetworkPluginTestFixture, TestNetworkPluginListenEvent) {
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
  const bpf::cros_event a = {
      .data.network_event =
          {
              .type = bpf::cros_network_event_type::kNetworkSocketListen,
              .data.socket_listen =
                  {
                      /* 192.168.0.1 */
                      .common = {.family = bpf::CROS_FAMILY_AF_INET,
                                 .protocol = bpf::CROS_PROTOCOL_TCP,
                                 .process{.pid = kDefaultPid,
                                          .start_time = kSpawnStartTime}},
                      .socket_type = SOCK_STREAM,
                      .port = 1234,
                      .ipv4_addr = 0x0100A8C0,
                  },
          },
      .type = bpf::kNetworkEvent,
  };
  const auto& socket_event = a.data.network_event.data.socket_listen;
  EXPECT_CALL(*process_cache_,
              GetProcessHierarchy(socket_event.common.process.pid,
                                  socket_event.common.process.start_time, 2))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

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
                   .common = {.family = bpf::CROS_FAMILY_AF_INET6,
                              .protocol = bpf::CROS_PROTOCOL_TCP,
                              .process{.pid = kDefaultPid,
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
  EXPECT_CALL(*process_cache_,
              GetProcessHierarchy(socket_event.common.process.pid,
                                  socket_event.common.process.start_time, 2))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

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
                  {.common = {.family = bpf::CROS_FAMILY_AF_INET,
                              .protocol = bpf::CROS_PROTOCOL_TCP,
                              .process{.pid = kDefaultPid,
                                       .start_time = kSpawnStartTime}},
                   .socket_type = SOCK_STREAM,
                   .port = 1234,
                   .ipv4_addr = 0x1020304},
          },
      .type = bpf::kNetworkEvent,
  };
  a.data.network_event.data.socket_listen.common.protocol = GetParam();
  pb::NetworkProtocol expected_protocol;
  switch (a.data.network_event.data.socket_listen.common.protocol) {
    case bpf::cros_network_protocol::CROS_PROTOCOL_ICMP:
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
  EXPECT_CALL(*process_cache_,
              GetProcessHierarchy(socket_event.common.process.pid,
                                  socket_event.common.process.start_time, 2))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

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
              .data.socket_listen =
                  {.common = {.family = bpf::CROS_FAMILY_AF_INET,
                              .protocol = bpf::CROS_PROTOCOL_TCP,
                              .process{.pid = kDefaultPid,
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
  EXPECT_CALL(*process_cache_,
              GetProcessHierarchy(socket_event.common.process.pid,
                                  socket_event.common.process.start_time, 2))
      .WillOnce(Return(ByMove(std::move(hierarchy))));

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
}  // namespace secagentd::testing
