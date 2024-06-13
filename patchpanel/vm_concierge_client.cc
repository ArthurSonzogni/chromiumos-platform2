// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/vm_concierge_client.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <base/functional/callback_forward.h>
#include <base/logging.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>
#include <dbus/vm_concierge/dbus-constants.h>
#include <vm_concierge/concierge_service.pb.h>

namespace {
// Long timeout required as concierge/crosvm may respond slowly during vm boot.
constexpr int kNonBlockingDbusTimeoutMs = 5000;

// Handles the result of an attempt to connect to a D-Bus signal, logging an
// error on failure.
void HandleSignalConnected(const std::string& interface,
                           const std::string& signal,
                           bool success) {
  if (!success) {
    LOG(ERROR) << __func__ << ": failed to connect to signal "
               << interface << "." << signal;
  }
}

std::optional<uint32_t> ReadAttachResponse(dbus::Response* dbus_response) {
  if (dbus_response == nullptr) {
    LOG(ERROR) << __func__ << ": method call failed";
    return std::nullopt;
  }
  dbus::MessageReader reader(dbus_response);
  vm_tools::concierge::AttachNetDeviceResponse attach_response;
  if (!reader.PopArrayOfBytesAsProto(&attach_response)) {
    LOG(ERROR) << __func__ << ": response decode failed";
    return std::nullopt;
  }
  if (!attach_response.success()) {
    LOG(ERROR) << __func__
               << ": remote side fail: " << attach_response.failure_reason();
    return std::nullopt;
  }
  LOG(INFO) << __func__ << ": attach succeeded with device inserted at "
            << attach_response.guest_bus();
  return {attach_response.guest_bus()};
}

bool ReadDetachResponse(dbus::Response* dbus_response) {
  if (dbus_response == nullptr) {
    LOG(ERROR) << __func__ << ": method call failed";
    return false;
  }
  dbus::MessageReader reader(dbus_response);
  vm_tools::concierge::DetachNetDeviceResponse detach_response;
  if (!reader.PopArrayOfBytesAsProto(&detach_response)) {
    LOG(ERROR) << __func__ << ": response decode failed";
    return false;
  }
  if (!detach_response.success()) {
    LOG(ERROR) << __func__
               << ": remote side fail: " << detach_response.failure_reason();
    return false;
  }
  LOG(INFO) << __func__ << ": detach succeeded";
  return true;
}

}  // namespace

namespace patchpanel {

VmConciergeClientImpl::VmConciergeClientImpl(scoped_refptr<dbus::Bus> bus)
    : bus_(bus) {
  dbus::Bus::Options options;
  concierge_proxy_ = bus_->GetObjectProxy(
      vm_tools::concierge::kVmConciergeServiceName,
      dbus::ObjectPath(vm_tools::concierge::kVmConciergeServicePath));
  concierge_proxy_->ConnectToSignal(
      vm_tools::concierge::kVmConciergeServiceName,
      vm_tools::concierge::kVmStartedSignal,
      base::BindRepeating(&VmConciergeClientImpl::OnVmStarted,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&HandleSignalConnected));
  concierge_proxy_->ConnectToSignal(
      vm_tools::concierge::kVmConciergeServiceName,
      vm_tools::concierge::kVmStoppingSignal,
      base::BindRepeating(&VmConciergeClientImpl::OnVmStopping,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&HandleSignalConnected));
}

bool VmConciergeClientImpl::RegisterVm(int64_t vm_cid) {
  return cid_vmid_map_.insert({vm_cid, std::nullopt}).second;
}

void VmConciergeClientImpl::DoAttachTapDevice(const std::string& tap_name,
                                              AttachTapCallback callback,
                                              const VmId& vm_id) {
  dbus::MethodCall method_call(vm_tools::concierge::kVmConciergeInterface,
                               vm_tools::concierge::kAttachNetDeviceMethod);
  vm_tools::concierge::AttachNetDeviceRequest attach_request_proto;
  dbus::MessageWriter writer(&method_call);
  attach_request_proto.set_vm_name(vm_id.vm_name);
  attach_request_proto.set_owner_id(vm_id.owner_id);
  attach_request_proto.set_tap_name(tap_name);
  if (!writer.AppendProtoAsArrayOfBytes(attach_request_proto)) {
    LOG(ERROR) << __func__ << ": request encode failed";
    return;
  }
  base::OnceCallback<void(dbus::Response*)> dbus_callback =
      base::BindOnce(&ReadAttachResponse).Then(std::move(callback));
  concierge_proxy_->CallMethod(&method_call, kNonBlockingDbusTimeoutMs,
                               std::move(dbus_callback));
}
bool VmConciergeClientImpl::AttachTapDevice(int64_t vm_cid,
                                            const std::string& tap_name,
                                            AttachTapCallback callback) {
  const auto itr = cid_vmid_map_.find(vm_cid);
  if (itr == cid_vmid_map_.end()) {
    LOG(ERROR) << __func__ << ": VM " << vm_cid << " is not registered.";
    return false;
  }
  if (itr->second.has_value()) {
    DoAttachTapDevice(std::string(tap_name), std::move(callback), *itr->second);
  } else {
    // Queue requests since VM is not ready.
    DeferredRequest request = base::BindOnce(
        &VmConciergeClientImpl::DoAttachTapDevice,
        weak_ptr_factory_.GetWeakPtr(), tap_name, std::move(callback));
    auto [q_itr, _] =
        cid_requestq_map_.insert({vm_cid, std::queue<DeferredRequest>()});
    q_itr->second.push(std::move(request));
  }
  return true;
}

void VmConciergeClientImpl::DoDetachTapDevice(uint32_t bus_num,
                                              DetachTapCallback callback,
                                              const VmId& vm_id) {
  dbus::MethodCall method_call(vm_tools::concierge::kVmConciergeInterface,
                               vm_tools::concierge::kDetachNetDeviceMethod);
  vm_tools::concierge::DetachNetDeviceRequest detach_request_proto;
  dbus::MessageWriter writer(&method_call);
  detach_request_proto.set_vm_name(vm_id.vm_name);
  detach_request_proto.set_owner_id(vm_id.owner_id);
  detach_request_proto.set_guest_bus(bus_num);
  if (!writer.AppendProtoAsArrayOfBytes(detach_request_proto)) {
    LOG(ERROR) << __func__ << ": request encode failed";
    return;
  }
  base::OnceCallback<void(dbus::Response*)> dbus_callback =
      base::BindOnce(&ReadDetachResponse).Then(std::move(callback));
  concierge_proxy_->CallMethod(&method_call, kNonBlockingDbusTimeoutMs,
                               std::move(dbus_callback));
}

bool VmConciergeClientImpl::DetachTapDevice(int64_t vm_cid,
                                            uint32_t bus_num,
                                            DetachTapCallback callback) {
  const auto itr = cid_vmid_map_.find(vm_cid);
  if (itr == cid_vmid_map_.end()) {
    // VM may already be shutdown, treat removal of device as successful.
    std::move(callback).Run(true);
    return true;
  }
  if (!itr->second.has_value()) {
    // Queue requests since VM is not ready.
    DeferredRequest request = base::BindOnce(
        &VmConciergeClientImpl::DoDetachTapDevice,
        weak_ptr_factory_.GetWeakPtr(), bus_num, std::move(callback));
    auto [q_itr, _] =
        cid_requestq_map_.insert({vm_cid, std::queue<DeferredRequest>()});
    q_itr->second.push(std::move(request));
  } else {
    DoDetachTapDevice(bus_num, std::move(callback), *itr->second);
  }
  return true;
}

void VmConciergeClientImpl::OnVmStarted(dbus::Signal* signal) {
  dbus::MessageReader reader(signal);
  vm_tools::concierge::VmStartedSignal started_signal;
  if (!reader.PopArrayOfBytesAsProto(&started_signal)) {
    LOG(ERROR) << __func__ << ": failed to parse "
               << vm_tools::concierge::kVmStartedSignal;
    return;
  }
  const int64_t cid = started_signal.vm_info().cid();
  auto itr = cid_vmid_map_.find(cid);
  if (itr == cid_vmid_map_.end()) {
    return;
  }
  VmId vm_id{started_signal.owner_id(), started_signal.name()};
  LOG(INFO) << __func__ << ": VM " << cid << " has started with VmId " << vm_id;
  itr->second.emplace(vm_id);
  // Handles pending tasks:
  auto q_itr = cid_requestq_map_.find(cid);
  if (q_itr != cid_requestq_map_.end()) {
    while (!q_itr->second.empty()) {
      DeferredRequest request(std::move(q_itr->second.front()));
      q_itr->second.pop();
      std::move(request).Run(vm_id);
    }
    cid_requestq_map_.erase(q_itr);
  }
}

void VmConciergeClientImpl::OnVmStopping(dbus::Signal* signal) {
  dbus::MessageReader reader(signal);
  vm_tools::concierge::VmStoppingSignal stopping_signal;
  if (!reader.PopArrayOfBytesAsProto(&stopping_signal)) {
    LOG(ERROR) << __func__ << ": failed to parse "
               << vm_tools::concierge::kVmStoppingSignal;
    return;
  }
  const int64_t cid = stopping_signal.cid();
  if (cid_vmid_map_.erase(cid) > 0) {
    // Removes pending tasks:
    cid_requestq_map_.erase(cid);
    LOG(INFO) << __func__ << ": VM " << cid
              << " is removed from VmConciergeClientImpl.";
  }
}

std::unique_ptr<VmConciergeClientImpl>
VmConciergeClientImpl::CreateClientWithNewBus() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;

  scoped_refptr<dbus::Bus> bus = new dbus::Bus(options);
  if (!bus->Connect()) {
    LOG(ERROR) << __func__ << "Failed to connect to system bus";
  }

  return std::make_unique<VmConciergeClientImpl>(std::move(bus));
}

std::ostream& operator<<(std::ostream& os,
                         const VmConciergeClient::VmId& vm_id) {
  os << "name: " << vm_id.vm_name << ", owner_id: " << vm_id.owner_id;
  return os;
}

}  // namespace patchpanel
