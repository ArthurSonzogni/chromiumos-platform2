// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/crostini_service.h"

#include <memory>
#include <optional>
#include <ostream>
#include <utility>

#include "base/task/single_thread_task_runner.h"
#include <base/check.h>
#include <base/logging.h>
#include <chromeos/constants/vm_tools.h>
#include <chromeos/dbus/service_constants.h>
// Ignore Wconversion warnings in dbus headers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>
#pragma GCC diagnostic pop
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/adb_proxy.h"
#include "patchpanel/address_manager.h"
#include "patchpanel/device.h"
#include "patchpanel/ipc.h"
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

AutoDNATTarget GetAutoDNATTarget(CrostiniService::VMType guest_type) {
  switch (guest_type) {
    case CrostiniService::VMType::kTermina:
      return AutoDNATTarget::kCrostini;
    case CrostiniService::VMType::kParallels:
      return AutoDNATTarget::kParallels;
  }
}
}  // namespace

CrostiniService::CrostiniDevice::CrostiniDevice(VMType type,
                                                std::unique_ptr<Device> device)
    : type_(type), device_(std::move(device)) {}

CrostiniService::CrostiniDevice::~CrostiniDevice() {}

const std::string& CrostiniService::CrostiniDevice::tap_device_ifname() const {
  return device_->host_ifname();
}

const net_base::IPv4CIDR CrostiniService::CrostiniDevice::vm_ipv4_subnet()
    const {
  return device_->config().ipv4_subnet()->base_cidr();
}

const net_base::IPv4Address CrostiniService::CrostiniDevice::vm_ipv4_address()
    const {
  return device_->config().guest_ipv4_addr();
}

const net_base::IPv4Address
CrostiniService::CrostiniDevice::gateway_ipv4_address() const {
  return device_->config().host_ipv4_addr();
}

std::optional<net_base::IPv4CIDR>
CrostiniService::CrostiniDevice::lxd_ipv4_subnet() const {
  if (!device_->config().lxd_ipv4_subnet()) {
    return std::nullopt;
  }
  return device_->config().lxd_ipv4_subnet()->base_cidr();
}

std::optional<net_base::IPv4Address>
CrostiniService::CrostiniDevice::lxd_ipv4_address() const {
  if (!device_->config().lxd_ipv4_subnet()) {
    return std::nullopt;
  }
  auto cidr = device_->config().lxd_ipv4_subnet()->CIDRAtOffset(
      kTerminaContainerAddressOffset);
  if (!cidr) {
    return std::nullopt;
  }
  return cidr->address();
}

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
  switch (type()) {
    case VMType::kTermina:
      output->set_guest_type(NetworkDevice::TERMINA_VM);
      break;
    case VMType::kParallels:
      output->set_guest_type(NetworkDevice::PARALLELS_VM);
      break;
  }
  FillSubnetProto(vm_ipv4_subnet(), output->mutable_ipv4_subnet());
  // Do no copy LXD container subnet data: patchpanel_service.proto's
  // NetworkDevice does not have a field for the LXD container IPv4 allocation.
}

CrostiniService::CrostiniService(
    AddressManager* addr_mgr,
    Datapath* datapath,
    CrostiniService::CrostiniDeviceEventHandler event_handler)
    : addr_mgr_(addr_mgr),
      datapath_(datapath),
      event_handler_(event_handler),
      adb_sideloading_enabled_(false) {
  DCHECK(addr_mgr_);
  DCHECK(datapath_);

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;

  bus_ = new dbus::Bus(options);
  if (!bus_->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
  } else {
    CheckAdbSideloadingStatus();
  }
}

CrostiniService::~CrostiniService() {
  if (bus_)
    bus_->ShutdownAndBlock();
}

const CrostiniService::CrostiniDevice* CrostiniService::Start(
    uint64_t vm_id, CrostiniService::VMType vm_type, uint32_t subnet_index) {
  const auto vm_info = std::make_pair(vm_id, vm_type);
  if (vm_id == kInvalidID) {
    LOG(ERROR) << __func__ << " " << vm_info << ": Invalid VM id";
    return nullptr;
  }

  if (devices_.find(vm_id) != devices_.end()) {
    LOG(WARNING) << __func__ << " " << vm_info << ": Datapath already started";
    return nullptr;
  }

  auto dev = AddTAP(vm_type, subnet_index);
  if (!dev) {
    LOG(ERROR) << __func__ << " " << vm_info << ": Failed to create TAP device";
    return nullptr;
  }

  datapath_->StartRoutingDeviceAsUser(dev->tap_device_ifname(),
                                      dev->vm_ipv4_address(),
                                      TrafficSourceFromVMType(vm_type));
  if (adb_sideloading_enabled_) {
    StartAdbPortForwarding(dev->tap_device_ifname());
  }
  if (vm_type == VMType::kParallels) {
    StartAutoDNAT(dev.get());
  }

  LOG(INFO) << __func__ << " " << vm_info
            << ": Crostini network service started on "
            << dev->tap_device_ifname();
  event_handler_.Run(*dev, CrostiniDeviceEvent::kAdded);
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

  event_handler_.Run(*it->second, CrostiniDeviceEvent::kRemoved);
  const std::string tap_ifname = it->second->tap_device_ifname();
  datapath_->StopRoutingDevice(tap_ifname);
  if (adb_sideloading_enabled_) {
    StopAdbPortForwarding(tap_ifname);
  }
  if (vm_type == VMType::kParallels) {
    StopAutoDNAT(it->second.get());
  }
  datapath_->RemoveInterface(tap_ifname);
  devices_.erase(vm_id);

  LOG(INFO) << __func__ << " " << vm_info
            << ": Crostini network service stopped on " << tap_ifname;
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
  auto guest_type = GuestTypeFromVMType(vm_type);
  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(guest_type, subnet_index);
  if (!ipv4_subnet) {
    LOG(ERROR) << "Subnet already in use or unavailable.";
    return nullptr;
  }
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  if (!host_ipv4_addr) {
    LOG(ERROR) << "Host address already in use or unavailable.";
    return nullptr;
  }
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(2);
  if (!guest_ipv4_addr) {
    LOG(ERROR) << "VM address already in use or unavailable.";
    return nullptr;
  }
  std::unique_ptr<Subnet> lxd_subnet;
  if (vm_type == VMType::kTermina) {
    lxd_subnet =
        addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kLXDContainer);
    if (!lxd_subnet) {
      LOG(ERROR) << "lxd subnet already in use or unavailable.";
      return nullptr;
    }
  }

  const auto mac_addr = addr_mgr_->GenerateMacAddress(subnet_index);
  // Name is autogenerated.
  const std::string tap = datapath_->AddTAP(
      /*name=*/"", &mac_addr, &(host_ipv4_addr->cidr()), vm_tools::kCrosVmUser);
  if (tap.empty()) {
    LOG(ERROR) << "Failed to create TAP device.";
    return nullptr;
  }

  if (lxd_subnet) {
    // Setup lxd route for the container using the VM as a gateway.
    const auto gateway_cidr = ipv4_subnet->CIDRAtOffset(2);
    const auto lxd_subnet_cidr =
        lxd_subnet->CIDRAtOffset(kTerminaContainerAddressOffset);
    if (!gateway_cidr || !lxd_subnet_cidr) {
      LOG(ERROR) << "Failed to get CIDR from Subnet, ipv4_subnet="
                 << ipv4_subnet->base_cidr()
                 << ", lxd_subnet=" << lxd_subnet->base_cidr();
      return nullptr;
    }
    if (!datapath_->AddIPv4Route(gateway_cidr->address(), *lxd_subnet_cidr)) {
      LOG(ERROR) << "Failed to setup lxd route";
      return nullptr;
    }
  }

  auto config = std::make_unique<Device::Config>(
      mac_addr, std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr), std::move(lxd_subnet));

  // The crosvm virtual devices used for non ARC guests are isolated TAP
  // devices without any bridge setup. They are not associated to any specific
  // physical network and instead follows the current default logical network,
  // therefore |phys_ifname| is undefined. |guest_ifname| is
  // not used inside the crosvm guest and is left empty.
  auto device =
      std::make_unique<Device>(VirtualDeviceTypeFromVMType(vm_type),
                               /*shill_device=*/std::nullopt, tap,
                               /*guest_ifname=*/"", std::move(config));
  return std::make_unique<CrostiniDevice>(vm_type, std::move(device));
}

void CrostiniService::StartAdbPortForwarding(const std::string& ifname) {
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

void CrostiniService::StopAdbPortForwarding(const std::string& ifname) {
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
  std::unique_ptr<dbus::Response> dbus_response =
      proxy->CallMethodAndBlock(&method_call, kDbusTimeoutMs);

  if (!dbus_response) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&CrostiniService::CheckAdbSideloadingStatus,
                       weak_factory_.GetWeakPtr()),
        kAdbSideloadUpdateDelay);
    num_try++;
    return;
  }

  dbus::MessageReader reader(dbus_response.get());
  reader.PopBool(&adb_sideloading_enabled_);
  if (!adb_sideloading_enabled_)
    return;

  // If ADB sideloading is enabled, start ADB forwarding on all configured
  // Crostini's TAP interfaces.
  for (const auto& [_, dev] : devices_) {
    StartAdbPortForwarding(dev->tap_device_ifname());
  }
}

void CrostiniService::OnShillDefaultLogicalDeviceChanged(
    const ShillClient::Device* new_device,
    const ShillClient::Device* prev_device) {
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
  datapath_->AddInboundIPv4DNAT(GetAutoDNATTarget(virtual_device->type()),
                                *default_logical_device_,
                                virtual_device->vm_ipv4_address());
}

void CrostiniService::StopAutoDNAT(
    const CrostiniService::CrostiniDevice* virtual_device) {
  if (!default_logical_device_) {
    return;
  }
  datapath_->RemoveInboundIPv4DNAT(GetAutoDNATTarget(virtual_device->type()),
                                   *default_logical_device_,
                                   virtual_device->vm_ipv4_address());
}

// static
std::optional<CrostiniService::VMType> CrostiniService::VMTypeFromDeviceType(
    Device::Type device_type) {
  switch (device_type) {
    case Device::Type::kTerminaVM:
      return VMType::kTermina;
    case Device::Type::kParallelsVM:
      return VMType::kParallels;
    default:
      return std::nullopt;
  }
}

// static
std::optional<CrostiniService::VMType>
CrostiniService::VMTypeFromProtoGuestType(NetworkDevice::GuestType guest_type) {
  switch (guest_type) {
    case NetworkDevice::TERMINA_VM:
      return VMType::kTermina;
    case NetworkDevice::PARALLELS_VM:
      return VMType::kParallels;
    default:
      return std::nullopt;
  }
}

// static
TrafficSource CrostiniService::TrafficSourceFromVMType(
    CrostiniService::VMType vm_type) {
  switch (vm_type) {
    case VMType::kTermina:
      return TrafficSource::kCrosVM;
    case VMType::kParallels:
      return TrafficSource::kParallelsVM;
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
  }
}

// static
AddressManager::GuestType CrostiniService::GuestTypeFromVMType(
    CrostiniService::VMType vm_type) {
  switch (vm_type) {
    case VMType::kTermina:
      return AddressManager::GuestType::kTerminaVM;
    case VMType::kParallels:
      return AddressManager::GuestType::kParallelsVM;
  }
}

// static
Device::Type CrostiniService::VirtualDeviceTypeFromVMType(
    CrostiniService::VMType vm_type) {
  switch (vm_type) {
    case VMType::kTermina:
      return Device::Type::kTerminaVM;
    case VMType::kParallels:
      return Device::Type::kParallelsVM;
  }
}

std::ostream& operator<<(std::ostream& stream,
                         const CrostiniService::VMType vm_type) {
  switch (vm_type) {
    case CrostiniService::VMType::kTermina:
      return stream << "Termina";
    case CrostiniService::VMType::kParallels:
      return stream << "Parallels";
  }
}

}  // namespace patchpanel
