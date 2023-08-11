// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/test/task_environment.h>
#include <gtest/gtest.h>
#include <sys/mman.h>

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/test/bind.h"
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
using ::testing::Pointee;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

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
    fake_network_bpf_.maps.process_map = reinterpret_cast<bpf_map*>(0xDADA);
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
              (const bpf::cros_synthetic_network_flow& flow));
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
}  // namespace secagentd::testing
