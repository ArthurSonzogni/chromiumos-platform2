// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <sys/mman.h>

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "dbus/mock_bus.h"
#include "dbus/mock_object_proxy.h"
#include "gmock/gmock.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/test/mock_platform.h"
#include "secagentd/test/mock_shill.h"

namespace secagentd::testing {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;
using ::testing::WithArgs;

bool operator==(const union bpf::cros_ip_addr& lhs,
                const union bpf::cros_ip_addr& rhs) {
  return memcmp(lhs.addr6, rhs.addr6,
                sizeof(lhs.addr6) / sizeof(lhs.addr6[0])) == 0;
}

bool operator==(const bpf::cros_flow_map_key& lhs,
                const bpf::cros_flow_map_key& rhs) {
  const auto& ltuple = lhs.five_tuple;
  const auto& rtuple = rhs.five_tuple;
  return ltuple.family == rtuple.family && ltuple.protocol == rtuple.protocol &&
         ltuple.local_addr == rtuple.local_addr &&
         ltuple.local_port == rtuple.local_port &&
         ltuple.remote_addr == rtuple.remote_addr;
}

bool operator==(const bpf::cros_flow_map_value& lhs,
                const bpf::cros_flow_map_value& rhs) {
  return lhs.direction == rhs.direction && lhs.rx_bytes == rhs.rx_bytes &&
         lhs.tx_bytes == rhs.tx_bytes && lhs.sock_id == rhs.sock_id;
}

bool operator==(const bpf::cros_synthetic_network_flow& lhs,
                const bpf::cros_synthetic_network_flow& rhs) {
  return lhs.flow_map_key == rhs.flow_map_key &&
         lhs.flow_map_value == rhs.flow_map_value;
}
MATCHER_P(SynthFlowEquals, value, "Match by value a synthetic network flow.") {
  return arg == value;
}

MATCHER_P(PointeeUint64, value, "Match the pointee of a void * to a uint64.") {
  return (arg != nullptr && *(static_cast<const uint64_t*>(arg)) == value);
}

MATCHER_P(PointeeFlowKey,
          value,
          "Match the pointee of a void* to a flow map key") {
  return arg != nullptr &&
         *(static_cast<const bpf::cros_flow_map_key*>(arg)) == value;
}

class NetworkBpfTestFixture : public ::testing::Test {
 protected:
  NetworkBpfTestFixture() : weak_ptr_factory_(this) {}
  void SetUp() override {
    mock_bus_ = base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options());
    mock_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        mock_bus_.get(), "org.chromium.flimflam", dbus::ObjectPath("/"));
    EXPECT_CALL(*mock_bus_, GetObjectProxy(_, _))
        .WillRepeatedly(Return(mock_proxy_.get()));
    EXPECT_CALL(*mock_proxy_, SetNameOwnerChangedCallback(_));
    EXPECT_CALL(*mock_proxy_, DoConnectToSignal(_, _, _, _));

    SetPlatform(std::make_unique<StrictMock<MockPlatform>>());
    platform_ = static_cast<StrictMock<MockPlatform>*>(GetPlatform().get());
    auto shill = std::make_unique<StrictMock<MockShill>>(mock_bus_);
    shill_ = shill.get();
    SkeletonCallbacks<network_bpf> cbs{
        .destroy = base::BindRepeating(&NetworkBpfTestFixture::MockDestroy,
                                       weak_ptr_factory_.GetWeakPtr()),
        .open =
            base::BindLambdaForTesting([this]() { return this->MockOpen(); }),
        .open_opts = base::BindLambdaForTesting(
            [this](const struct bpf_object_open_opts* opts) {
              return this->MockOpenWithOpts(opts);
            })};

    bpf_cbs_.ring_buffer_event_callback = base::BindRepeating(
        &NetworkBpfTestFixture::ConsumeEvent, weak_ptr_factory_.GetWeakPtr());
    bpf_cbs_.ring_buffer_read_ready_callback =
        base::BindRepeating(&NetworkBpfTestFixture::MockEventAvailable,
                            weak_ptr_factory_.GetWeakPtr());

    network_bpf_ =
        std::make_unique<NetworkBpfSkeleton>(10, std::move(shill), cbs);
    fake_network_bpf_.maps.cros_network_external_interfaces =
        reinterpret_cast<bpf_map*>(0xFADE);
    fake_network_bpf_.maps.cros_network_flow_map =
        reinterpret_cast<bpf_map*>(0xDEDE);
    fake_network_bpf_.maps.active_socket_map =
        reinterpret_cast<bpf_map*>(0xDEED);
    fake_network_bpf_.maps.rb = reinterpret_cast<bpf_map*>(0xCADE);
  }

  /* This is to make matching easier.*/
  void ConsumeEvent(const bpf::cros_event& event) {
    if (event.type != bpf::cros_event_type::kNetworkEvent) {
      MockConsumeNonNetworkEvent();
    }
    const auto& net_event = event.data.network_event;
    if (net_event.type == bpf::cros_network_event_type::kNetworkSocketListen) {
      MockConsumeListen(net_event.data.socket_listen);
    } else if (net_event.type ==
               bpf::cros_network_event_type::kSyntheticNetworkFlow) {
      MockConsumeFlowEvent(net_event.data.flow);
    }
  }
  MOCK_METHOD(void,
              MockConsumeFlowEvent,
              (bpf::cros_synthetic_network_flow flow));
  MOCK_METHOD(void,
              MockConsumeListen,
              (const bpf::cros_network_socket_listen& listen));
  MOCK_METHOD(void, MockConsumeNonNetworkEvent, ());

  MOCK_METHOD(void, MockEventAvailable, ());
  MOCK_METHOD(network_bpf*, MockOpen, ());
  MOCK_METHOD(void, MockDestroy, (network_bpf*));

  MOCK_METHOD(network_bpf*,
              MockOpenWithOpts,
              (const struct bpf_object_open_opts* opts));

  std::pair<absl::Status, metrics::BpfAttachResult> LoadAndAttach() {
    return network_bpf_->LoadAndAttach();
  }

  void ScanFlowMap() { network_bpf_->ScanFlowMap(); }

  void RegisterCallbacks(const BpfCallbacks& bpf_cb) {
    network_bpf_->RegisterCallbacks(bpf_cbs_);
  }

  void InstallSuccessfulLoadExpectations() {
    EXPECT_CALL(*this, MockOpenWithOpts(_))
        .WillOnce(Return(&fake_network_bpf_));
    EXPECT_CALL(*platform_, LibbpfSetStrictMode(_)).WillOnce(Return(0));
    EXPECT_CALL(*platform_, BpfObjectLoadSkeleton(_)).WillOnce(Return(0));
    EXPECT_CALL(*platform_, BpfObjectAttachSkeleton(_)).WillOnce(Return(0));
    EXPECT_CALL(*platform_, BpfMapFd(_)).WillOnce(Return(bpf_map_fd_));
    EXPECT_CALL(*platform_, RingBufferNew(bpf_map_fd_, _, _, _))
        .WillOnce(Return(bpf_rb_));
    EXPECT_CALL(*platform_, RingBufferEpollFd(Eq(bpf_rb_)))
        .WillOnce(Return(bpf_epoll_fd_));
    EXPECT_CALL(*platform_, WatchReadable(bpf_epoll_fd_, _))
        .WillOnce(Return(nullptr));
  }
  int bpf_map_fd_{0xDEAD};
  ring_buffer* bpf_rb_{reinterpret_cast<ring_buffer*>(0xBEEF)};
  int bpf_epoll_fd_{0xFEED};
  BpfCallbacks bpf_cbs_;
  network_bpf fake_network_bpf_;
  // Network BPF uses a timer.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;
  StrictMock<MockShill>* shill_;
  StrictMock<MockPlatform>* platform_;
  std::unique_ptr<NetworkBpfSkeleton> network_bpf_;
  base::WeakPtrFactory<NetworkBpfTestFixture> weak_ptr_factory_;
};

TEST_F(NetworkBpfTestFixture, ExternalDeviceList) {
  base::OnceCallback<void(bool)> on_avail;
  base::RepeatingCallback<void(bool)> proc_changed;
  base::RepeatingCallback<void(const shill::Client::Device* const)>
      device_added;
  std::map<std::string, std::pair<shill::Client::Device, int>> devices{
      {"dev0", std::make_pair(shill::Client::Device{.ifname = "dev0"}, 1)},
      {"dev1", std::make_pair(shill::Client::Device{.ifname = "dev1"}, 4)}};

  EXPECT_CALL(*platform_, IfNameToIndex("dev0"))
      .WillOnce(Return(devices.at("dev0").second));
  EXPECT_CALL(*platform_, IfNameToIndex("dev1"))
      .WillOnce(Return(devices.at("dev1").second));
  EXPECT_CALL(*shill_, RegisterOnAvailableCallback)
      .WillOnce([&on_avail](base::OnceCallback<void(bool)> handler) {
        on_avail = std::move(handler);
      });
  EXPECT_CALL(*shill_, RegisterProcessChangedHandler)
      .WillOnce(SaveArg<0>(&proc_changed));
  EXPECT_CALL(*shill_, RegisterDeviceAddedHandler)
      .WillOnce(SaveArg<0>(&device_added));
  EXPECT_CALL(*shill_, RegisterDeviceRemovedHandler(_));
  InstallSuccessfulLoadExpectations();
  // Activate the Network BPF.
  RegisterCallbacks(bpf_cbs_);
  ASSERT_TRUE(LoadAndAttach().first.ok());
  // Signal that shill is now available.
  std::move(on_avail).Run(true);
  EXPECT_CALL(
      *platform_,
      BpfMapUpdateElem(
          /*map*/ fake_network_bpf_.maps.cros_network_external_interfaces,
          /*key*/ ::testing::Truly([&devices](const void* p) {
            uint64_t in_val = *(reinterpret_cast<const uint64_t*>(p));
            uint64_t expected = devices.at("dev0").second;
            bool rv = in_val == expected;
            return rv;
          }),
          /*key size*/ sizeof(uint64_t), _, _, _))
      .WillOnce(Return(0));
  EXPECT_CALL(
      *platform_,
      BpfMapUpdateElem(
          /*map*/ fake_network_bpf_.maps.cros_network_external_interfaces,
          /*key*/ ::testing::Truly([&devices](const void* p) {
            uint64_t in_val = *(reinterpret_cast<const uint64_t*>(p));
            uint64_t expected = devices.at("dev1").second;
            bool rv = in_val == expected;
            return rv;
          }),
          /*key size*/ sizeof(uint64_t), _, _, _))
      .WillOnce(Return(0));
  device_added.Run(&devices.at("dev0").first);
  device_added.Run(&devices.at("dev1").first);
  EXPECT_CALL(*platform_, RingBufferFree(bpf_rb_));
}

TEST_F(NetworkBpfTestFixture, FlowCleanUp) {
  struct bpf::cros_synthetic_network_flow f0 = {
      .flow_map_key = {.five_tuple = {.family = bpf::CROS_FAMILY_AF_INET,
                                      .protocol = bpf::CROS_PROTOCOL_TCP,
                                      .local_addr.addr4 = 1234,
                                      .local_port = 95,
                                      .remote_addr.addr4 = 4321,
                                      .remote_port = 123},
                       .sock_id = 0},
      .flow_map_value = {.direction = bpf::CROS_SOCKET_DIRECTION_OUT,
                         .tx_bytes = 512,
                         .rx_bytes = 254,
                         .sock_id = 10}};
  struct bpf::cros_synthetic_network_flow f1 = {
      .flow_map_key = {.five_tuple = {.family = bpf::CROS_FAMILY_AF_INET,
                                      .protocol = bpf::CROS_PROTOCOL_UDP,
                                      .local_addr.addr4 = 1234 * 2,
                                      .local_port = 95 * 2,
                                      .remote_addr.addr4 = 4321 * 2,
                                      .remote_port = 123 * 2},
                       .sock_id = 0},
      .flow_map_value = {.direction = bpf::CROS_SOCKET_DIRECTION_OUT,
                         .tx_bytes = 512 * 2,
                         .rx_bytes = 254 * 2,
                         .sock_id = 10 * 2}};
  struct bpf::cros_synthetic_network_flow f2 = {
      .flow_map_key = {.five_tuple = {.family = bpf::CROS_FAMILY_AF_INET,
                                      .protocol = bpf::CROS_PROTOCOL_ICMP,
                                      .local_addr.addr4 = 1234 * 2,
                                      .local_port = 0,
                                      .remote_addr.addr4 = 4321 * 2,
                                      .remote_port = 0},
                       .sock_id = 10 * 3},
      .flow_map_value = {.direction = bpf::CROS_SOCKET_DIRECTION_OUT,
                         .tx_bytes = 512 * 3,
                         .rx_bytes = 254 * 3,
                         .sock_id = 10 * 3}};

  /* Expect the retrieval of active sockets, make it so that the socket
    associated with f2 is considered inactive.
  */
  {
    InSequence s1;
    EXPECT_CALL(*platform_,
                BpfMapGetNextKey(fake_network_bpf_.maps.active_socket_map,
                                 nullptr, _, _))
        .WillOnce(WithArgs<2>(Invoke([&f0](void* ptr) {
          *static_cast<uint64_t*>(ptr) = f0.flow_map_value.sock_id;
          return 0;
        })));
    EXPECT_CALL(*platform_,
                BpfMapGetNextKey(fake_network_bpf_.maps.active_socket_map,
                                 PointeeUint64(f0.flow_map_value.sock_id), _,
                                 sizeof(f0.flow_map_value.sock_id)))
        .WillOnce(WithArgs<2>(Invoke([&f2](void* ptr) {
          *static_cast<uint64_t*>(ptr) = f2.flow_map_value.sock_id;
          return -ENOENT;  // last value in the map.
        })));
  }
  // Expect retrieval of all flow keys from the map.
  {
    InSequence s2;
    EXPECT_CALL(*platform_,
                BpfMapGetNextKey(fake_network_bpf_.maps.cros_network_flow_map,
                                 nullptr, _, sizeof(f0.flow_map_key)))
        .WillOnce(WithArgs<2>(Invoke([&f0](void* ptr) {
          *static_cast<bpf::cros_flow_map_key*>(ptr) = f0.flow_map_key;
          return 0;
        })));
    EXPECT_CALL(*platform_,
                BpfMapGetNextKey(fake_network_bpf_.maps.cros_network_flow_map,
                                 PointeeFlowKey(f0.flow_map_key), _,
                                 sizeof(f0.flow_map_key)))
        .WillOnce(WithArgs<2>(Invoke([&f1](void* ptr) {
          *static_cast<bpf::cros_flow_map_key*>(ptr) = f1.flow_map_key;
          return 0;
        })));
    EXPECT_CALL(*platform_,
                BpfMapGetNextKey(fake_network_bpf_.maps.cros_network_flow_map,
                                 PointeeFlowKey(f1.flow_map_key), _,
                                 sizeof(f1.flow_map_key)))
        .WillOnce(WithArgs<2>(Invoke([&f2](void* ptr) {
          *static_cast<bpf::cros_flow_map_key*>(ptr) = f2.flow_map_key;
          return -ENOENT;  // last value in the map.
        })));
  }

  /* Expect that flow values are retrieved at least once.*/
  EXPECT_CALL(
      *platform_,
      BpfMapLookupElem(fake_network_bpf_.maps.cros_network_flow_map,
                       PointeeFlowKey(f0.flow_map_key), sizeof(f0.flow_map_key),
                       NotNull(), sizeof(f0.flow_map_value), _))
      .WillRepeatedly(WithArgs<3>(Invoke([&f0](void* ptr) {
        *static_cast<bpf::cros_flow_map_value*>(ptr) = f0.flow_map_value;
        return 0;
      })));

  EXPECT_CALL(
      *platform_,
      BpfMapLookupElem(fake_network_bpf_.maps.cros_network_flow_map,
                       PointeeFlowKey(f1.flow_map_key), sizeof(f1.flow_map_key),
                       NotNull(), sizeof(f1.flow_map_value), _))
      .WillRepeatedly(WithArgs<3>(Invoke([&f1](void* ptr) {
        *static_cast<bpf::cros_flow_map_value*>(ptr) = f1.flow_map_value;
        return 0;
      })));
  EXPECT_CALL(
      *platform_,
      BpfMapLookupElem(fake_network_bpf_.maps.cros_network_flow_map,
                       PointeeFlowKey(f2.flow_map_key), sizeof(f2.flow_map_key),
                       NotNull(), sizeof(f2.flow_map_value), _))
      .WillRepeatedly(WithArgs<3>(Invoke([&f2](void* ptr) {
        *static_cast<bpf::cros_flow_map_value*>(ptr) = f2.flow_map_value;
        return 0;
      })));

  /* the active sock map keys we returned earlier indicate that the f1 socket is
  not active. Expect a deletion for flows associated with this socket, flows
  associated with the other sockets should not be deleted.
  */
  EXPECT_CALL(*platform_,
              BpfMapDeleteElem(fake_network_bpf_.maps.cros_network_flow_map,
                               PointeeFlowKey(f1.flow_map_key),
                               sizeof(f1.flow_map_key), _))
      .WillOnce(Return(0));
  EXPECT_CALL(*platform_,
              BpfMapDeleteElem(fake_network_bpf_.maps.cros_network_flow_map,
                               PointeeFlowKey(f0.flow_map_key),
                               sizeof(f0.flow_map_key), _))
      .Times(0);
  EXPECT_CALL(*platform_,
              BpfMapDeleteElem(fake_network_bpf_.maps.cros_network_flow_map,
                               PointeeFlowKey(f2.flow_map_key),
                               sizeof(f2.flow_map_key), _))
      .Times(0);

  EXPECT_CALL(*platform_, RingBufferFree(bpf_rb_)).Times(1);
  EXPECT_CALL(*this, MockConsumeFlowEvent(SynthFlowEquals(f0))).Times(1);
  EXPECT_CALL(*this, MockConsumeFlowEvent(SynthFlowEquals(f1))).Times(1);
  EXPECT_CALL(*this, MockConsumeFlowEvent(SynthFlowEquals(f2))).Times(1);

  base::OnceCallback<void(bool)> on_avail;
  EXPECT_CALL(*shill_, RegisterOnAvailableCallback)
      .WillOnce([&on_avail](base::OnceCallback<void(bool)> handler) {
        on_avail = std::move(handler);
      });
  InstallSuccessfulLoadExpectations();
  // Activate the Network BPF.
  RegisterCallbacks(bpf_cbs_);
  ASSERT_TRUE(LoadAndAttach().first.ok());
  ScanFlowMap();
}
}  // namespace secagentd::testing
