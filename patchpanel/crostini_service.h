// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_CROSTINI_SERVICE_H_
#define PATCHPANEL_CROSTINI_SERVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <net-base/ipv4_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/ipc.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/subnet.h"

namespace patchpanel {

// Crostini networking service handling address allocation, TAP device creation,
// and patchpanel Device management for Crostini VMs (Termina VMs, Parallels
// VMs). CrostiniService currently only supports one TAP device per VM instance.
class CrostiniService {
 public:
  // Offset for allocating the LXD container address in the LXD subnet assigned
  // to a Termina VM.
  static constexpr uint32_t kTerminaContainerAddressOffset = 1;

  // All types of VM supported by CrostiniService.
  enum class VMType {
    // Crostini Linux VM with a user LXD container.
    kTermina,
    kParallels,
    kBruschetta,
    kBorealis,
  };

  // Represents the virtual interface setup created for a VM connected to the
  // network with CrostiniService. The crosvm virtual interface setup user for
  // non-ARC crosvm guest are L3 tap devices without any bridge setup. They are
  // not associated to any specific physical network and instead follows the
  // current default logical network, therefore |phys_ifname| is undefined.
  // |guest_ifname| is not used inside the crosvm guest and is left empty.
  class CrostiniDevice {
   public:
    CrostiniDevice(VMType type,
                   std::string_view tap_device_ifname,
                   const MacAddress& mac_address,
                   std::unique_ptr<Subnet> vm_ipv4_subnet,
                   std::unique_ptr<Subnet> lxd_ipv4_subnet);
    CrostiniDevice(const CrostiniDevice&) = delete;
    CrostiniDevice& operator=(const CrostiniDevice&) = delete;
    ~CrostiniDevice();

    VMType type() const { return type_; }
    const std::string& tap_device_ifname() const { return tap_device_ifname_; }
    const MacAddress& mac_address() const { return mac_address_; }
    const net_base::IPv4CIDR& vm_ipv4_subnet() const {
      return vm_ipv4_subnet_->base_cidr();
    }
    const net_base::IPv4Address& vm_ipv4_address() const {
      return vm_ipv4_address_;
    }
    const net_base::IPv4Address& gateway_ipv4_address() const {
      return gateway_ipv4_address_;
    }
    std::optional<net_base::IPv4CIDR> lxd_ipv4_subnet() const {
      if (!lxd_ipv4_subnet_) {
        return std::nullopt;
      }
      return lxd_ipv4_subnet_->base_cidr();
    }
    std::optional<net_base::IPv4Address> lxd_ipv4_address() const {
      return lxd_ipv4_address_;
    }
    // Converts this CrostiniDevice to a patchpanel proto NetworkDevice object
    // passed as a pointer. It is necessary to support externally allocated
    // objects to work well with probotuf repeated embedded message fields.
    void ConvertToProto(NetworkDevice* output) const;

   private:
    // The type of the VM associated with this virtual device.
    VMType type_;
    // The interface name of the tap device created for the VM.
    std::string tap_device_ifname_;
    // A random MAC address assigned to the tap device created for the VM.
    MacAddress mac_address_;
    // The static IPv4 subnet allocated for the VM.
    std::unique_ptr<Subnet> vm_ipv4_subnet_;
    // The static IPv4 address allocated to the VM and assigned on the virtual
    // interface created inside the VM and connected on the host to the tap
    // device created for the VM.
    net_base::IPv4Address vm_ipv4_address_;
    // The static IPv4 address used as a next hop by the VM (and its internal
    // containers if any) for the default IPv4 route and assigned to the tap
    // device created for the VM.
    net_base::IPv4Address gateway_ipv4_address_;
    // For Termina VMs only, an additional static IPv4 subnet allocated for the
    // LXD container running inside the Termina VM.
    std::unique_ptr<Subnet> lxd_ipv4_subnet_;
    // For Termina VMs only, the address of the network interface assigned to
    // the LXD container inside the Termina VM.
    std::optional<net_base::IPv4Address> lxd_ipv4_address_;
  };

  enum class CrostiniDeviceEvent {
    kAdded,
    kRemoved,
  };

  using CrostiniDeviceEventHandler =
      base::RepeatingCallback<void(const CrostiniDevice&, CrostiniDeviceEvent)>;

  static TrafficSource TrafficSourceFromVMType(VMType vm_type);
  // Converts VMType to an internal IPC GuestMessage::GuestType value. This type
  // is needed by Manager for IPCs to patchpanel subprocesses.
  static GuestMessage::GuestType GuestMessageTypeFromVMType(VMType vm_type);
  // Converts VMType to an internal AddressManager::GuestType enum value. This
  // type is needed for allocating static IPv4 subnets.
  static AddressManager::GuestType AddressManagingTypeFromVMType(
      VMType vm_type);

  // All pointers are required and must not be null, and are owned by the
  // caller.
  CrostiniService(AddressManager* addr_mgr,
                  Datapath* datapath,
                  CrostiniDeviceEventHandler event_handler);
  CrostiniService(const CrostiniService&) = delete;
  CrostiniService& operator=(const CrostiniService&) = delete;

  ~CrostiniService();

  const CrostiniDevice* Start(uint64_t vm_id,
                              VMType vm_type,
                              uint32_t subnet_index);
  void Stop(uint64_t vm_id);

  // Returns a single CrostiniDevice pointer created for the VM with id |vm_id|.
  const CrostiniDevice* const GetDevice(uint64_t vm_id) const;

  // Returns a list of all CrostiniDevices currently managed by this service.
  std::vector<const CrostiniDevice*> GetDevices() const;

  // Notifies CrostiniService about a change in the default logical shill
  // Device.
  void OnShillDefaultLogicalDeviceChanged(
      const ShillClient::Device* new_device,
      const ShillClient::Device* prev_device);

 private:
  std::unique_ptr<CrostiniDevice> AddTAP(VMType vm_type, uint32_t subnet_index);

  // Checks ADB sideloading status and set it to |adb_sideloading_enabled_|.
  // This function will call itself again if ADB sideloading status is not
  // known yet. Otherwise, it will process all currently running Crostini VMs.
  void CheckAdbSideloadingStatus();

  // Start and stop ADB traffic forwarding from Crostini's TAP device
  // patchpanel's adb-proxy. |ifname| is the Crostini's TAP interface that
  // will be forwarded. These methods call permission broker DBUS APIs to port
  // forward and accept traffic.
  void StartAdbPortForwarding(const std::string& ifname);
  void StopAdbPortForwarding(const std::string& ifname);

  // Starts and stop automatic DNAT forwarding of inbound traffic into a
  // Crostini virtual device. |crostini_device| must not be null.
  void StartAutoDNAT(const CrostiniDevice* crostini_device);
  void StopAutoDNAT(const CrostiniDevice* crostini_device);

  AddressManager* addr_mgr_;
  Datapath* datapath_;
  std::optional<ShillClient::Device> default_logical_device_;
  CrostiniDeviceEventHandler event_handler_;

  // Mapping of VM IDs to TAP devices
  std::map<uint64_t, std::unique_ptr<CrostiniDevice>> devices_;

  bool adb_sideloading_enabled_;
  scoped_refptr<dbus::Bus> bus_;

  base::WeakPtrFactory<CrostiniService> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream,
                         const CrostiniService::VMType vm_type);

}  // namespace patchpanel

#endif  // PATCHPANEL_CROSTINI_SERVICE_H_
