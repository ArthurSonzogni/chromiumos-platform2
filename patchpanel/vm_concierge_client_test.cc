// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/vm_concierge_client.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_runner.h>
#include <base/types/expected.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>
#include <dbus/vm_concierge/dbus-constants.h>
#include <gtest/gtest.h>
#include <vm_concierge/concierge_service.pb.h>

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace patchpanel {

namespace {
constexpr uint64_t kMockVmCid = 0xdeadbeef;
constexpr char kMockVmName[] = "MockVm";
constexpr char kMockVmOwner[] = "MockUser";
class CallbackMonitor {
 public:
  CallbackMonitor() = default;
  MOCK_METHOD(void, OnAttachCallback, (std::optional<uint32_t> bus_num), ());
  MOCK_METHOD(void, OnDetachCallback, (bool), ());
  VmConciergeClient::AttachTapCallback CreateAttachTestCallback();
  VmConciergeClient::DetachTapCallback CreateDetachTestCallback();
};

VmConciergeClient::AttachTapCallback
CallbackMonitor::CreateAttachTestCallback() {
  return base::BindOnce(&CallbackMonitor::OnAttachCallback,
                        base::Unretained(this));
}
VmConciergeClient::DetachTapCallback
CallbackMonitor::CreateDetachTestCallback() {
  return base::BindOnce(&CallbackMonitor::OnDetachCallback,
                        base::Unretained(this));
}
class VmConciergeClientTest : public ::testing::Test {
 public:
  static constexpr uint8_t kSlotNum = 1;
  static constexpr char kMockFailReason[] = "mock fail reason";
  VmConciergeClientTest() = default;
  void SetUp() override {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = new dbus::MockBus(options);
    concierge_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), vm_tools::concierge::kVmConciergeServiceName,
        dbus::ObjectPath(vm_tools::concierge::kVmConciergeServicePath));

    // Sets an expectation that VmConciergeClient will get ObjectProxy.
    EXPECT_CALL(
        *mock_bus_.get(),
        GetObjectProxy(
            vm_tools::concierge::kVmConciergeServiceName,
            dbus::ObjectPath(vm_tools::concierge::kVmConciergeServicePath)))
        .WillOnce(Return(concierge_proxy_.get()));
    // Sets an expectation that mock proxy will register to listen to
    // kVmStartedSignal and kVmStoppingSignal from concierge_proxy_.
    EXPECT_CALL(*concierge_proxy_.get(),
                DoConnectToSignal(vm_tools::concierge::kVmConciergeInterface,
                                  vm_tools::concierge::kVmStartedSignal, _, _))
        .WillRepeatedly(Invoke(this, &VmConciergeClientTest::ConnectToSignal));
    EXPECT_CALL(*concierge_proxy_.get(),
                DoConnectToSignal(vm_tools::concierge::kVmConciergeInterface,
                                  vm_tools::concierge::kVmStoppingSignal, _, _))
        .WillRepeatedly(Invoke(this, &VmConciergeClientTest::ConnectToSignal));
    EXPECT_CALL(*mock_bus_, GetDBusTaskRunner())
        .WillRepeatedly(
            Return(base::SingleThreadTaskRunner::GetCurrentDefault().get()));
    client_ = std::make_unique<VmConciergeClient>(mock_bus_);
  }

  void ResolveAttachAsSuccess(testing::Unused,
                              testing::Unused,
                              dbus::ObjectProxy::ResponseCallback* callback) {
    vm_tools::concierge::AttachNetDeviceResponse attach_response;
    attach_response.set_success(true);
    attach_response.set_guest_bus(kSlotNum);
    std::unique_ptr<dbus::Response> dbus_response =
        dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(dbus_response.get());
    writer.AppendProtoAsArrayOfBytes(attach_response);
    std::move(*callback).Run(dbus_response.get());
  }

  void ResolveDetachAsFail(testing::Unused,
                           testing::Unused,
                           dbus::ObjectProxy::ResponseCallback* callback) {
    vm_tools::concierge::DetachNetDeviceResponse detach_response;
    detach_response.set_success(false);
    detach_response.set_failure_reason(kMockFailReason);
    std::unique_ptr<dbus::Response> dbus_response =
        dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(dbus_response.get());
    writer.AppendProtoAsArrayOfBytes(detach_response);
    std::move(*callback).Run(dbus_response.get());
  }

 protected:
  void SendVmStartedSignal() {
    const auto it =
        signal_callbacks_.find(vm_tools::concierge::kVmStartedSignal);
    ASSERT_TRUE(it != signal_callbacks_.end())
        << "Client didn't register for kVmStartedSignal";
    dbus::Signal dbus_signal(vm_tools::concierge::kVmConciergeInterface,
                             vm_tools::concierge::kVmStartedSignal);
    dbus::MessageWriter writer(&dbus_signal);
    vm_tools::concierge::VmStartedSignal vm_started_signal;
    vm_tools::concierge::VmInfo* vm_info = new vm_tools::concierge::VmInfo();
    vm_info->set_cid(kMockVmCid);
    vm_started_signal.set_allocated_vm_info(vm_info);
    vm_started_signal.set_name(kMockVmName);
    vm_started_signal.set_owner_id(kMockVmOwner);
    writer.AppendProtoAsArrayOfBytes(vm_started_signal);
    it->second.Run(&dbus_signal);
  }
  void SendVmStoppingSignal() {
    const auto it =
        signal_callbacks_.find(vm_tools::concierge::kVmStoppingSignal);
    ASSERT_TRUE(it != signal_callbacks_.end())
        << "Client didn't register for kVmStoppingSignal";
    dbus::Signal dbus_signal(vm_tools::concierge::kVmConciergeInterface,
                             vm_tools::concierge::kVmStoppingSignal);
    dbus::MessageWriter writer(&dbus_signal);
    vm_tools::concierge::VmStoppingSignal vm_stopping_signal;
    vm_stopping_signal.set_cid(kMockVmCid);
    vm_stopping_signal.set_name(kMockVmName);
    vm_stopping_signal.set_owner_id(kMockVmOwner);
    writer.AppendProtoAsArrayOfBytes(vm_stopping_signal);
    it->second.Run(&dbus_signal);
  }
  void ConnectToSignal(
      const std::string& interface_name,
      const std::string& signal_name,
      dbus::ObjectProxy::SignalCallback signal_callback,
      dbus::ObjectProxy::OnConnectedCallback* on_connected_callback);

  base::test::TaskEnvironment task_environment_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> concierge_proxy_;
  std::unique_ptr<VmConciergeClient> client_;
  CallbackMonitor monitor_;

 private:
  std::map<std::string, dbus::ObjectProxy::SignalCallback> signal_callbacks_;
};
void VmConciergeClientTest::ConnectToSignal(
    const std::string& interface_name,
    const std::string& signal_name,
    dbus::ObjectProxy::SignalCallback signal_callback,
    dbus::ObjectProxy::OnConnectedCallback* on_connected_callback) {
  EXPECT_EQ(interface_name, vm_tools::concierge::kVmConciergeInterface);
  signal_callbacks_[signal_name] = std::move(signal_callback);
  task_environment_.GetMainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(*on_connected_callback), interface_name,
                     signal_name, true /* success */));
}
}  // namespace

// Tests that attempt to use client with unknown VM cid is rejected.
TEST_F(VmConciergeClientTest, RejectUnregisteredVm) {
  SendVmStartedSignal();
  base::RunLoop().RunUntilIdle();
  // AttachTapDevice fails since kMockVmCid is not registered with client.
  EXPECT_FALSE(client_->AttachTapDevice(kMockVmCid, "test-tap",
                                        monitor_.CreateAttachTestCallback()));
}

// Basic test that VmConciergeClient can send request and process response after
// VmStartedSignal is emitted.
TEST_F(VmConciergeClientTest, DetachAfterVmStartedSignal) {
  client_->RegisterVm(kMockVmCid);
  EXPECT_CALL(*concierge_proxy_.get(), DoCallMethod)
      .WillOnce(Invoke(this, &VmConciergeClientTest::ResolveDetachAsFail));
  SendVmStartedSignal();
  EXPECT_CALL(monitor_, OnDetachCallback(false)).Times(1);
  EXPECT_TRUE(client_->DetachTapDevice(kMockVmCid, kSlotNum,
                                       monitor_.CreateDetachTestCallback()));
  base::RunLoop().RunUntilIdle();
}

// Tests that VmConciergeClient defers attach requests until it receives
// VmStartedSignal.
TEST_F(VmConciergeClientTest, AttachBeforeVmStartedSignal) {
  client_->RegisterVm(kMockVmCid);
  EXPECT_CALL(*concierge_proxy_.get(), DoCallMethod)
      .WillOnce(Invoke(this, &VmConciergeClientTest::ResolveAttachAsSuccess));
  // expect attach requests are accepted since the VM is registered.
  EXPECT_TRUE(client_->AttachTapDevice(kMockVmCid, "test-tap",
                                       monitor_.CreateAttachTestCallback()));
  // expect callback not invoked since VmStartedSignal is not emitted.
  EXPECT_CALL(monitor_, OnAttachCallback).Times(0);
  base::RunLoop().RunUntilIdle();
  // expect callback after signal emitted.
  EXPECT_CALL(monitor_, OnAttachCallback({kSlotNum})).Times(1);
  SendVmStartedSignal();
  base::RunLoop().RunUntilIdle();
}

// Tests that VmConciergeClient rejects requests after a VM is shutting down.
TEST_F(VmConciergeClientTest, RejectAfterVmStoppingSignal) {
  client_->RegisterVm(kMockVmCid);
  SendVmStartedSignal();
  SendVmStoppingSignal();
  base::RunLoop().RunUntilIdle();
  // AttachTapDevice fails early because Stopping signal is received.
  EXPECT_FALSE(client_->AttachTapDevice(kMockVmCid, "test-tap",
                                        monitor_.CreateAttachTestCallback()));
}

}  // namespace patchpanel
