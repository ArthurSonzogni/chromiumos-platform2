// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/crostini_service.h"

#include <memory>
#include <optional>
#include <ostream>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>
#include <chromeos/constants/vm_tools.h>
#include <chromeos/dbus/service_constants.h>
// Ignore Wconversion warnings in dbus headers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>
#pragma GCC diagnostic pop
#include <chromeos/net-base/mac_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/net_util.h"
#include "patchpanel/proto_utils.h"

namespace patchpanel {
namespace {
constexpr int32_t kInvalidID = 0;
constexpr int kDbusTimeoutMs = 200;
// The maximum number of ADB sideloading query failures before stopping.
constexpr int kAdbSideloadMaxTry = 5;
constexpr base::TimeDelta kAdbSideloadUpdateDelay = base::Milliseconds(5000);

std::ostream& operator<<(
    std::ostream& stream,
    const std::pair<uint64_t, CrostiniService::VMType>& vm_info) {
  return stream << "{id: " << vm_info.first << ", vm_type: " << vm_info.second
                << "}";
}

std::optional<AutoDNATTarget> GetAutoDNATTarget(
    CrostiniService::VMType guest_type) {
  switch (guest_type) {
    case CrostiniService::VMType::kTermina:
      return AutoDNATTarget::kCrostini;
    case CrostiniService::VMType::kParallels:
      return AutoDNATTarget::kParallels;
    case CrostiniService::VMType::kBruschetta:
    case CrostiniService::VMType::kBorealis:
      // There should not be attempt of auto DNAT into Bruschetta and Borealis
      return std::nullopt;
  }
}

NetworkDevice::GuestType ProtoDeviceTypeFromVMType(
    CrostiniService::VMType vm_type) {
  switch (vm_type) {
    case CrostiniService::VMType::kTermina:
      return NetworkDevice::TERMINA_VM;
      break;
    case CrostiniService::VMType::kParallels:
      return NetworkDevice::PARALLELS_VM;
      break;
    case CrostiniService::VMType::kBruschetta:
    case CrostiniService::VMType::kBorealis:
      // TODO(b/279994478): Clarify whether Bruschetta and Borealis need to
      // differentiate themselves from Crostini devices on dbus.
      return NetworkDevice::TERMINA_VM;
  }
}

}  // namespace

CrostiniService::CrostiniDevice::CrostiniDevice(
    VMType type,
    std::string_view tap_device_ifname,
    std::unique_ptr<Subnet> vm_ipv4_subnet,
    std::unique_ptr<Subnet> lxd_ipv4_subnet)
    : type_(type),
      tap_device_ifname_(tap_device_ifname),
      vm_ipv4_subnet_(std::move(vm_ipv4_subnet)),
      lxd_ipv4_subnet_(std::move(lxd_ipv4_subnet)) {
  DCHECK(vm_ipv4_subnet_);
  gateway_ipv4_address_ = vm_ipv4_subnet_->CIDRAtOffset(1)->address();
  vm_ipv4_address_ = vm_ipv4_subnet_->CIDRAtOffset(2)->address();
  lxd_ipv4_address_ = std::nullopt;
  if (lxd_ipv4_subnet_) {
    lxd_ipv4_address_ =
        lxd_ipv4_subnet_->CIDRAtOffset(kTerminaContainerAddressOffset)
            ->address();
  }
}

CrostiniService::CrostiniDevice::~CrostiniDevice() = default;

void CrostiniService::CrostiniDevice::ConvertToProto(
    NetworkDevice* output) const {
  output->set_ifname(tap_device_ifname());
  // Legacy compatibility: fill |phys_ifname| with the tap device interface
  // name.
  output->set_phys_ifname(tap_device_ifname());
  // For non-ARC VMs, the guest virtio interface name is not known.
  output->set_phys_ifname(tap_device_ifname());
  output->set_guest_ifname("");
  output->set_ipv4_addr(vm_ipv4_address().ToInAddr().s_addr);
  output->set_host_ipv4_addr(gateway_ipv4_address().ToInAddr().s_addr);
  output->set_guest_type(ProtoDeviceTypeFromVMType(type()));

  FillSubnetProto(vm_ipv4_subnet(), output->mutable_ipv4_subnet());
  // Do no copy LXD container subnet data: patchpanel_service.proto's
  // NetworkDevice does not have a field for the LXD container IPv4 allocation.
}

CrostiniService::CrostiniService(const scoped_refptr<dbus::Bus>& bus,
                                 AddressManager* addr_mgr,
                                 Datapath* datapath,
                                 ForwardingService* forwarding_service,
                                 DbusClientNotifier* dbus_client_notifier)
    : bus_(bus),
      addr_mgr_(addr_mgr),
      datapath_(datapath),
      forwarding_service_(forwarding_service),
      dbus_client_notifier_(dbus_client_notifier),
      adb_sideloading_enabled_(false) {
  DCHECK(addr_mgr_);
  DCHECK(datapath_);
  DCHECK(forwarding_service_);
  CheckAdbSideloadingStatus();
}

CrostiniService::~CrostiniService() = default;

const CrostiniService::CrostiniDevice* CrostiniService::Start(
    uint64_t vm_id, CrostiniService::VMType vm_type, uint32_t subnet_index) {
  const auto vm_info = std::make_pair(vm_id, vm_type);
  if (vm_id == kInvalidID) {
    LOG(ERROR) << __func__ << " " << vm_info << ": Invalid VM id";
    return nullptr;
  }

  if (devices_.find(vm_id) != devices_.end()) {
    LOG(INFO) << __func__ << " " << vm_info
              << ": Datapath was already started. Restarting...";
    Stop(vm_id);
  }

  auto dev = AddTAP(vm_type, subnet_index);
  if (!dev) {
    LOG(ERROR) << __func__ << " " << vm_info << ": Failed to create TAP device";
    return nullptr;
  }

  datapath_->StartRoutingDeviceAsUser(dev->tap_device_ifname(),
                                      TrafficSourceFromVMType(vm_type),
                                      dev->vm_ipv4_address());
  if (default_logical_device_) {
    forwarding_service_->StartIPv6NDPForwarding(*default_logical_device_,
                                                dev->tap_device_ifname());
    forwarding_service_->StartMulticastForwarding(*default_logical_device_,
                                                  dev->tap_device_ifname());
    forwarding_service_->StartBroadcastForwarding(*default_logical_device_,
                                                  dev->tap_device_ifname());
  }
  if (adb_sideloading_enabled_) {
    StartAdbPortForwarding(dev->tap_device_ifname());
  }
  if (vm_type == VMType::kParallels) {
    StartAutoDNAT(dev.get());
  }

  LOG(INFO) << __func__ << " " << vm_info << ": VM network service started on "
            << dev->tap_device_ifname()
            << " with VM subnet: " << dev->vm_ipv4_subnet() << ", LXD subnet: "
            << (dev->lxd_ipv4_subnet() ? dev->lxd_ipv4_subnet()->ToString()
                                       : "none");
  auto signal_device = std::make_unique<NetworkDevice>();
  dev->ConvertToProto(signal_device.get());
  dbus_client_notifier_->OnNetworkDeviceChanged(
      std::move(signal_device), NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto [it, _] = devices_.emplace(vm_id, std::move(dev));
  return it->second.get();
}

void CrostiniService::Stop(uint64_t vm_id) {
  const auto it = devices_.find(vm_id);
  if (it == devices_.end()) {
    LOG(WARNING) << __func__ << " {id: " << vm_id << "}: Unknown VM";
    return;
  }

  auto vm_type = it->second->type();
  const auto vm_info = std::make_pair(vm_id, vm_type);

  auto signal_device = std::make_unique<NetworkDevice>();
  it->second->ConvertToProto(signal_device.get());
  dbus_client_notifier_->OnNetworkDeviceChanged(
      std::move(signal_device), NetworkDeviceChangedSignal::DEVICE_REMOVED);
  const std::string tap_ifname = it->second->tap_device_ifname();
  if (default_logical_device_) {
    forwarding_service_->StopIPv6NDPForwarding(*default_logical_device_,
                                               tap_ifname);
    forwarding_service_->StopMulticastForwarding(*default_logical_device_,
                                                 tap_ifname);
    forwarding_service_->StopBroadcastForwarding(*default_logical_device_,
                                                 tap_ifname);
  }
  datapath_->StopRoutingDevice(tap_ifname, TrafficSourceFromVMType(vm_type));
  if (adb_sideloading_enabled_) {
    StopAdbPortForwarding(tap_ifname);
  }
  if (vm_type == VMType::kParallels) {
    StopAutoDNAT(it->second.get());
  }
  datapath_->RemoveInterface(tap_ifname);
  devices_.erase(it);

  LOG(INFO) << __func__ << " " << vm_info << ": VM network service stopped on "
            << tap_ifname;
}

const CrostiniService::CrostiniDevice* const CrostiniService::GetDevice(
    uint64_t vm_id) const {
  const auto it = devices_.find(vm_id);
  if (it == devices_.end()) {
    return nullptr;
  }
  return it->second.get();
}

std::vector<const CrostiniService::CrostiniDevice*>
CrostiniService::GetDevices() const {
  std::vector<const CrostiniDevice*> devices;
  for (const auto& [_, dev] : devices_) {
    devices.push_back(dev.get());
  }
  return devices;
}

std::unique_ptr<CrostiniService::CrostiniDevice> CrostiniService::AddTAP(
    CrostiniService::VMType vm_type, uint32_t subnet_index) {
  auto address_type = AddressManagingTypeFromVMType(vm_type);
  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(address_type, subnet_index);
  if (!ipv4_subnet) {
    LOG(ERROR) << __func__ << ": Subnet " << subnet_index
               << " already in use or unavailable.";
    return nullptr;
  }
  // Verify addresses can be allocated in the VM IPv4 subnet.
  auto gateway_ipv4_cidr = ipv4_subnet->CIDRAtOffset(1);
  if (!gateway_ipv4_cidr) {
    LOG(ERROR) << __func__
               << ": Gateway address already in use or unavailable.";
    return nullptr;
  }
  auto vm_ipv4_cidr = ipv4_subnet->CIDRAtOffset(2);
  if (!vm_ipv4_cidr) {
    LOG(ERROR) << __func__ << ": VM address already in use or unavailable.";
    return nullptr;
  }
  std::unique_ptr<Subnet> lxd_subnet;
  if (vm_type == VMType::kTermina) {
    lxd_subnet =
        addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kLXDContainer);
    if (!lxd_subnet) {
      LOG(ERROR) << __func__ << ": LXD subnet already in use or unavailable.";
      return nullptr;
    }
    // Verify the LXD address can be allocated in the VM IPv4 subnet.
    if (!lxd_subnet->CIDRAtOffset(kTerminaContainerAddressOffset)) {
      LOG(ERROR) << __func__ << ": LXD address already in use or unavailable.";
      return nullptr;
    }
  }
  // Name is autogenerated.
  const std::string tap = datapath_->AddTunTap(
      /*name=*/"",
      net_base::MacAddress(addr_mgr_->GenerateMacAddress(subnet_index)),
      gateway_ipv4_cidr, vm_tools::kCrosVmUser, DeviceMode::kTap);
  if (tap.empty()) {
    LOG(ERROR) << __func__ << ": Failed to create TAP device.";
    return nullptr;
  }
  if (lxd_subnet) {
    // Setup route to the LXD the container using the VM as a gateway into the
    // LXD container.
    const auto lxd_cidr =
        lxd_subnet->CIDRAtOffset(kTerminaContainerAddressOffset);
    if (!datapath_->AddIPv4Route(vm_ipv4_cidr->address(), *lxd_cidr)) {
      LOG(ERROR) << __func__
                 << ": Failed to setup route to the Termina LXD container";
      return nullptr;
    }
  }
  return std::make_unique<CrostiniDevice>(vm_type, tap, std::move(ipv4_subnet),
                                          std::move(lxd_subnet));
}

void CrostiniService::StartAdbPortForwarding(std::string_view ifname) {
  if (!datapath_->AddAdbPortForwardRule(ifname)) {
    LOG(ERROR) << __func__ << ": Error adding ADB port forwarding rule for "
               << ifname;
    return;
  }

  if (!datapath_->AddAdbPortAccessRule(ifname)) {
    datapath_->DeleteAdbPortForwardRule(ifname);
    LOG(ERROR) << __func__ << ": Error adding ADB port access rule for "
               << ifname;
    return;
  }

  if (!datapath_->SetRouteLocalnet(ifname, true)) {
    LOG(ERROR) << __func__ << ": Failed to set up route localnet for "
               << ifname;
    return;
  }
}

void CrostiniService::StopAdbPortForwarding(std::string_view ifname) {
  datapath_->DeleteAdbPortForwardRule(ifname);
  datapath_->DeleteAdbPortAccessRule(ifname);
  datapath_->SetRouteLocalnet(ifname, false);
}

void CrostiniService::CheckAdbSideloadingStatus() {
  static int num_try = 0;
  if (num_try >= kAdbSideloadMaxTry) {
    LOG(WARNING) << __func__
                 << ": Failed getting feature enablement status after "
                 << num_try << " tries.";
    return;
  }

  dbus::ObjectProxy* proxy = bus_->GetObjectProxy(
      login_manager::kSessionManagerServiceName,
      dbus::ObjectPath(login_manager::kSessionManagerServicePath));
  dbus::MethodCall method_call(login_manager::kSessionManagerInterface,
                               login_manager::kSessionManagerQueryAdbSideload);
  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> dbus_response =
      proxy->CallMethodAndBlock(&method_call, kDbusTimeoutMs);

  if (!dbus_response.has_value() || !dbus_response.value()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&CrostiniService::CheckAdbSideloadingStatus,
                       weak_factory_.GetWeakPtr()),
        kAdbSideloadUpdateDelay);
    num_try++;
    return;
  }

  dbus::MessageReader reader(dbus_response.value().get());
  reader.PopBool(&adb_sideloading_enabled_);
  if (!adb_sideloading_enabled_) {
    return;
  }

  // If ADB sideloading is enabled, start ADB forwarding on all configured
  // Crostini's TAP interfaces.
  for (const auto& [_, dev] : devices_) {
    StartAdbPortForwarding(dev->tap_device_ifname());
  }
}

void CrostiniService::OnShillDefaultLogicalDeviceChanged(
    const ShillClient::Device* new_device,
    const ShillClient::Device* prev_device) {
  // When the default logical network changes, Crostini's tap devices must leave
  // their current forwarding group for multicast and IPv6 ndproxy and join the
  // forwarding group of the new logical default network.
  for (const auto& [_, dev] : devices_) {
    if (prev_device) {
      forwarding_service_->StopIPv6NDPForwarding(*prev_device,
                                                 dev->tap_device_ifname());
      forwarding_service_->StopMulticastForwarding(*prev_device,
                                                   dev->tap_device_ifname());
      forwarding_service_->StopBroadcastForwarding(*prev_device,
                                                   dev->tap_device_ifname());
    }
    if (new_device) {
      forwarding_service_->StartIPv6NDPForwarding(*new_device,
                                                  dev->tap_device_ifname());
      forwarding_service_->StartMulticastForwarding(*new_device,
                                                    dev->tap_device_ifname());
      forwarding_service_->StartBroadcastForwarding(*new_device,
                                                    dev->tap_device_ifname());
    }
  }

  // b/197930417: Update Auto DNAT rules if a Parallels VM is running.
  const CrostiniDevice* parallels_device = nullptr;
  for (const auto& [_, dev] : devices_) {
    if (dev->type() == VMType::kParallels) {
      parallels_device = dev.get();
      break;
    }
  }

  if (parallels_device) {
    StopAutoDNAT(parallels_device);
  }
  if (new_device) {
    default_logical_device_ = *new_device;
  } else {
    default_logical_device_ = std::nullopt;
  }
  if (parallels_device) {
    StartAutoDNAT(parallels_device);
  }
}

void CrostiniService::StartAutoDNAT(
    const CrostiniService::CrostiniDevice* virtual_device) {
  if (!default_logical_device_) {
    return;
  }
  auto dnat_target = GetAutoDNATTarget(virtual_device->type());
  if (!dnat_target) {
    LOG(ERROR) << __func__ << ": " << virtual_device->type()
               << " is not a valid auto DNAT target.";
    return;
  }
  datapath_->AddInboundIPv4DNAT(*dnat_target, *default_logical_device_,
                                virtual_device->vm_ipv4_address());
}

void CrostiniService::StopAutoDNAT(
    const CrostiniService::CrostiniDevice* virtual_device) {
  if (!default_logical_device_) {
    return;
  }
  auto dnat_target = GetAutoDNATTarget(virtual_device->type());
  if (!dnat_target) {
    LOG(ERROR) << __func__ << ": " << virtual_device->type()
               << " is not a valid auto DNAT target.";
    return;
  }
  datapath_->RemoveInboundIPv4DNAT(*dnat_target, *default_logical_device_,
                                   virtual_device->vm_ipv4_address());
}

// static
TrafficSource CrostiniService::TrafficSourceFromVMType(
    CrostiniService::VMType vm_type) {
  switch (vm_type) {
    case VMType::kTermina:
      return TrafficSource::kCrostiniVM;
    case VMType::kParallels:
      return TrafficSource::kParallelsVM;
    case VMType::kBruschetta:
      return TrafficSource::kBruschettaVM;
    case VMType::kBorealis:
      return TrafficSource::kBorealisVM;
  }
}

// static
GuestMessage::GuestType CrostiniService::GuestMessageTypeFromVMType(
    CrostiniService::VMType vm_type) {
  switch (vm_type) {
    case VMType::kTermina:
      return GuestMessage::TERMINA_VM;
    case VMType::kParallels:
      return GuestMessage::PARALLELS_VM;
    case VMType::kBruschetta:
      return GuestMessage::BRUSCHETTA_VM;
    case VMType::kBorealis:
      return GuestMessage::BOREALIS_VM;
  }
}

// static
AddressManager::GuestType CrostiniService::AddressManagingTypeFromVMType(
    CrostiniService::VMType vm_type) {
  switch (vm_type) {
    case VMType::kTermina:
    case VMType::kBruschetta:
    case VMType::kBorealis:
      // Bruschetta and Borealis share the same address pool with Crostini.
      return AddressManager::GuestType::kTerminaVM;
    case VMType::kParallels:
      return AddressManager::GuestType::kParallelsVM;
  }
}

std::ostream& operator<<(std::ostream& stream,
                         const CrostiniService::VMType vm_type) {
  switch (vm_type) {
    case CrostiniService::VMType::kTermina:
      return stream << "Termina";
    case CrostiniService::VMType::kParallels:
      return stream << "Parallels";
    case CrostiniService::VMType::kBruschetta:
      return stream << "Bruschetta";
    case CrostiniService::VMType::kBorealis:
      return stream << "Borealis";
  }
}

}  // namespace patchpanel
