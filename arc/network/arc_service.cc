// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/arc_service.h"

#include <linux/rtnetlink.h>
#include <net/if.h>

#include <utility>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/key_value_store.h>
#include <chromeos/constants/vm_tools.h>
#include <shill/net/rtnl_message.h>

#include "arc/network/datapath.h"
#include "arc/network/ipc.pb.h"
#include "arc/network/mac_address_generator.h"
#include "arc/network/minijailed_process_runner.h"
#include "arc/network/net_util.h"
#include "arc/network/scoped_ns.h"

namespace arc_networkd {
namespace {
constexpr pid_t kInvalidPID = -1;
constexpr pid_t kTestPID = -2;
constexpr int32_t kInvalidCID = -1;
constexpr int kInvalidTableID = -1;
constexpr int kMaxTableRetries = 10;  // Based on 1 second delay.
constexpr base::TimeDelta kTableRetryDelay = base::TimeDelta::FromSeconds(1);
// Android adds a constant to the interface index to derive the table id.
// This is defined in system/netd/server/RouteController.h
constexpr int kRouteControllerRouteTableOffsetFromIndex = 1000;

// This wrapper is required since the base class is a singleton that hides its
// constructor. It is necessary here because the message loop thread has to be
// reassociated to the container's network namespace; and since the container
// can be repeatedly created and destroyed, the handler must be as well.
class RTNetlinkHandler : public shill::RTNLHandler {
 public:
  RTNetlinkHandler() = default;
  ~RTNetlinkHandler() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(RTNetlinkHandler);
};

int GetAndroidRoutingTableId(const std::string& ifname, pid_t pid) {
  base::FilePath ifindex_path(base::StringPrintf(
      "/proc/%d/root/sys/class/net/%s/ifindex", pid, ifname.c_str()));
  std::string contents;
  if (!base::ReadFileToString(ifindex_path, &contents)) {
    PLOG(WARNING) << "Could not read " << ifindex_path.value();
    return kInvalidTableID;
  }

  base::TrimWhitespaceASCII(contents, base::TRIM_TRAILING, &contents);
  int table_id = kInvalidTableID;
  if (!base::StringToInt(contents, &table_id)) {
    LOG(ERROR) << "Could not parse ifindex from " << ifindex_path.value()
               << ": " << contents;
    return kInvalidTableID;
  }
  table_id += kRouteControllerRouteTableOffsetFromIndex;

  LOG(INFO) << "Found table id " << table_id << " for container interface "
            << ifname;
  return table_id;
}

bool ShouldEnableMultinet() {
  static const char kLsbReleasePath[] = "/etc/lsb-release";
  static int kMinAndroidSdkVersion = 28;  // P

  brillo::KeyValueStore store;
  if (!store.Load(base::FilePath(kLsbReleasePath))) {
    LOG(ERROR) << "Could not read lsb-release";
    return false;
  }

  std::string value;
  if (!store.GetString("CHROMEOS_ARC_ANDROID_SDK_VERSION", &value)) {
    LOG(ERROR) << "ARC multi-networking disabled - cannot determine Android "
                  "SDK version";
    return false;
  }
  int ver = 0;
  if (!base::StringToInt(value.c_str(), &ver)) {
    LOG(ERROR) << "ARC multi-networking disabled - invalid Android SDK version";
    return false;
  }
  if (ver < kMinAndroidSdkVersion) {
    LOG(INFO) << "ARC multi-networking disabled for Android SDK " << value;
    return false;
  }
  return true;
}

// Load networking modules needed by Android that are not compiled in the
// kernel. Android does not allow auto-loading of kernel modules.
void LoadModules(const Datapath& datapath) {
  static bool done = false;
  if (done)
    return;

  // These must succeed.
  if (datapath.runner().ModprobeAll({
          // The netfilter modules needed by netd for iptables commands.
          "ip6table_filter",
          "ip6t_ipv6header",
          "ip6t_REJECT",
          // The xfrm modules needed for Android's ipsec APIs.
          "xfrm4_mode_transport",
          "xfrm4_mode_tunnel",
          "xfrm6_mode_transport",
          "xfrm6_mode_tunnel",
          // The ipsec modules for AH and ESP encryption for ipv6.
          "ah6",
          "esp6",
      }) != 0) {
    LOG(ERROR) << "One or more required kernel modules failed to load."
               << " Some Android functionality may be broken.";
  }
  // Optional modules.
  if (datapath.runner().ModprobeAll({
          // This module is not available in kernels < 3.18
          "nf_reject_ipv6",
          // These modules are needed for supporting Chrome traffic on Android
          // VPN which uses Android's NAT feature. Android NAT sets up
          // iptables
          // rules that use these conntrack modules for FTP/TFTP.
          "nf_nat_ftp",
          "nf_nat_tftp",
      }) != 0) {
    LOG(WARNING) << "One or more optional kernel modules failed to load.";
  }

  done = true;
}

// TODO(garrick): Remove this workaround ASAP.
int GetContainerPID() {
  const base::FilePath path("/run/containers/android-run_oci/container.pid");
  std::string pid_str;
  if (!base::ReadFileToStringWithMaxSize(path, &pid_str, 16 /* max size */)) {
    LOG(ERROR) << "Failed to read pid file";
    return kInvalidPID;
  }
  int pid;
  if (!base::StringToInt(base::TrimWhitespaceASCII(pid_str, base::TRIM_ALL),
                         &pid)) {
    LOG(ERROR) << "Failed to convert container pid string";
    return kInvalidPID;
  }
  LOG(INFO) << "Read container pid as " << pid;
  return pid;
}

bool IsArcVm() {
  const base::FilePath path("/run/chrome/is_arcvm");
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    PLOG(ERROR) << "Could not read " << path.value();
  }
  return contents == "1";
}

GuestMessage::GuestType ArcGuest(bool* is_legacy_override_for_testing) {
  if (is_legacy_override_for_testing)
    return *is_legacy_override_for_testing ? GuestMessage::ARC_LEGACY
                                           : GuestMessage::ARC;

  return IsArcVm() ? GuestMessage::ARC_VM
                   : ShouldEnableMultinet() ? GuestMessage::ARC
                                            : GuestMessage::ARC_LEGACY;
}

}  // namespace

ArcService::ArcService(DeviceManagerBase* dev_mgr,
                       Datapath* datapath,
                       bool* is_legacy)
    : GuestService(ArcGuest(is_legacy), dev_mgr), datapath_(datapath) {
  DCHECK(datapath_);

  if (guest_ == GuestMessage::ARC_VM)
    impl_ = std::make_unique<VmImpl>(dev_mgr, datapath);
  else
    impl_ = std::make_unique<ContainerImpl>(dev_mgr, datapath, guest_);
}

bool ArcService::Start(int32_t id) {
  int32_t prev_id;
  if (impl_->IsStarted(&prev_id)) {
    LOG(WARNING) << "Already running - did something crash?"
                 << " Stopping and restarting...";
    Stop(prev_id);
  }

  if (!impl_->Start(id))
    return false;

  // Start known host devices, any new ones will be setup in the process.
  dev_mgr_->ProcessDevices(
      base::Bind(&ArcService::StartDevice, weak_factory_.GetWeakPtr()));

  // If this is the first time the service is starting this will create the
  // Android bridge device; otherwise it does nothing (this is a workaround for
  // the bug in Shill that casues a Bus crash when it sees the ARC bridge a
  // second time). Do this after processing the existing devices so it doesn't
  // get started twice.
  std::string arc;
  switch (const auto guest = impl_->guest()) {
    case GuestMessage::ARC:
      arc = kAndroidDevice;
      break;
    case GuestMessage::ARC_LEGACY:
      arc = kAndroidLegacyDevice;
      break;
    case GuestMessage::ARC_VM:
      arc = kAndroidVmDevice;
      break;
    default:
      LOG(DFATAL) << "Unexpected guest: " << guest;
      return false;
  }
  dev_mgr_->Add(arc);

  // Finally, call the base implementation.
  return GuestService::Start(id);
}

void ArcService::Stop(int32_t id) {
  // Call the base implementation.
  GuestService::Stop(id);

  // Stop known host devices. Note that this does not teardown any existing
  // devices.
  dev_mgr_->ProcessDevices(
      base::Bind(&ArcService::StopDevice, weak_factory_.GetWeakPtr()));

  impl_->Stop(id);
}

bool ArcService::AllowDevice(Device* device) const {
  if (!device->options().is_arc)
    return false;

  // ARC P+ is multi-network enabled and should process all devices.
  if (guest_ == GuestMessage::ARC)
    return true;

  // ARC N and ARCVM (for now) are both single-network - meaning they only use
  // the "default" device which uses the default interface from shill.
  return device->UsesDefaultInterface();
}

void ArcService::OnDeviceAdded(Device* device) {
  if (!AllowDevice(device))
    return;

  const auto& config = device->config();

  LOG(INFO) << "Adding device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname();

  // Create the bridge.
  if (!datapath_->AddBridge(config.host_ifname(),
                            IPv4AddressToString(config.host_ipv4_addr()))) {
    LOG(ERROR) << "Failed to setup arc bridge: " << config.host_ifname();
    return;
  }

  // Setup the iptables.
  if (device->UsesDefaultInterface()) {
    if (!datapath_->AddLegacyIPv4DNAT(
            IPv4AddressToString(config.guest_ipv4_addr())))
      LOG(ERROR) << "Failed to configure ARC traffic rules";

    if (!datapath_->AddOutboundIPv4(config.host_ifname()))
      LOG(ERROR) << "Failed to configure egress traffic rules";
  } else if (!device->IsAndroid()) {
    if (!datapath_->AddInboundIPv4DNAT(
            device->ifname(), IPv4AddressToString(config.guest_ipv4_addr())))
      LOG(ERROR) << "Failed to configure ingress traffic rules for "
                 << device->ifname();

    if (!datapath_->AddOutboundIPv4(config.host_ifname()))
      LOG(ERROR) << "Failed to configure egress traffic rules";
  }

  device->set_context(guest_, std::make_unique<Context>());

  StartDevice(device);
}

void ArcService::StartDevice(Device* device) {
  if (!AllowDevice(device))
    return;

  // This can happen if OnDeviceAdded is invoked when the container is down.
  if (!impl_->IsStarted())
    return;

  // If there is no context, then this is a new device and it needs to run
  // through the full setup process.
  Context* ctx = dynamic_cast<Context*>(device->context(guest_));
  if (!ctx)
    return OnDeviceAdded(device);

  if (ctx->IsStarted()) {
    LOG(ERROR) << "Attempt to restart device " << device->ifname();
    return;
  }

  if (!impl_->OnStartDevice(device)) {
    LOG(ERROR) << "Failed to start device " << device->ifname();
    return;
  }

  ctx->Start();
}

void ArcService::OnDeviceRemoved(Device* device) {
  if (!AllowDevice(device))
    return;

  // If the container is down, this call does nothing.
  StopDevice(device);

  const auto& config = device->config();

  LOG(INFO) << "Removing device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname();

  device->Disable();
  if (device->UsesDefaultInterface()) {
    datapath_->RemoveOutboundIPv4(config.host_ifname());
    datapath_->RemoveLegacyIPv4DNAT();
  } else if (!device->IsAndroid()) {
    datapath_->RemoveOutboundIPv4(config.host_ifname());
    datapath_->RemoveInboundIPv4DNAT(
        device->ifname(), IPv4AddressToString(config.guest_ipv4_addr()));
  }

  datapath_->RemoveBridge(config.host_ifname());

  device->set_context(guest_, nullptr);
}

void ArcService::StopDevice(Device* device) {
  if (!AllowDevice(device))
    return;

  // This can happen if the device if OnDeviceRemoved is invoked when the
  // container is down.
  if (!impl_->IsStarted())
    return;

  Context* ctx = dynamic_cast<Context*>(device->context(guest_));
  if (!ctx) {
    LOG(ERROR) << "Attempt to stop removed device " << device->ifname();
    return;
  }

  if (!ctx->IsStarted()) {
    LOG(ERROR) << "Attempt to re-stop device " << device->ifname();
    return;
  }

  impl_->OnStopDevice(device);

  ctx->Stop();
}

void ArcService::OnDefaultInterfaceChanged(const std::string& ifname) {
  impl_->OnDefaultInterfaceChanged(ifname);
}

// Context

ArcService::Context::Context() : Device::Context() {
  Stop();
}

void ArcService::Context::Start() {
  Stop();
  started_ = true;
}

void ArcService::Context::Stop() {
  started_ = false;
  link_up_ = false;
  routing_table_id_ = kInvalidTableID;
  routing_table_attempts_ = 0;
}

bool ArcService::Context::IsStarted() const {
  return started_;
}

bool ArcService::Context::IsLinkUp() const {
  return link_up_;
}

bool ArcService::Context::SetLinkUp(bool link_up) {
  if (link_up == link_up_)
    return false;

  link_up_ = link_up;
  return true;
}

bool ArcService::Context::HasIPv6() const {
  return routing_table_id_ != kInvalidTableID;
}

bool ArcService::Context::SetHasIPv6(int routing_table_id) {
  if (routing_table_id <= kRouteControllerRouteTableOffsetFromIndex)
    return false;

  routing_table_id_ = routing_table_id;
  return true;
}

void ArcService::Context::ClearIPv6() {
  routing_table_id_ = kInvalidTableID;
  routing_table_attempts_ = 0;
}

int ArcService::Context::RoutingTableID() const {
  return routing_table_id_;
}

int ArcService::Context::RoutingTableAttempts() {
  return routing_table_attempts_++;
}

const std::string& ArcService::Context::TAP() const {
  return tap_;
}

void ArcService::Context::SetTAP(const std::string& tap) {
  tap_ = tap;
}

// ARC++ specific functions.

ArcService::ContainerImpl::ContainerImpl(DeviceManagerBase* dev_mgr,
                                         Datapath* datapath,
                                         GuestMessage::GuestType guest)
    : pid_(kInvalidPID), dev_mgr_(dev_mgr), datapath_(datapath), guest_(guest) {
  LoadModules(*datapath_);
}

GuestMessage::GuestType ArcService::ContainerImpl::guest() const {
  return guest_;
}

bool ArcService::ContainerImpl::Start(int32_t pid) {
  // This could happen if something crashes and the stop signal is not sent.
  // It can probably be addressed by stopping and restarting the service.
  if (pid_ != kInvalidPID)
    return false;

  // TODO(garrick): Remove this test hack.
  if (pid == kTestPID) {
    LOG(WARNING) << "Running with test PID";
    pid_ = pid;
    return true;
  }
  pid_ = GetContainerPID();
  if (pid_ == kInvalidPID) {
    LOG(ERROR) << "Cannot start service - invalid container PID";
    return false;
  }

  // Start listening for RTNetlink messages in the container's net namespace
  // to be notified whenever it brings up an interface.
  {
    ScopedNS ns(pid_);
    if (ns.IsValid()) {
      rtnl_handler_ = std::make_unique<RTNetlinkHandler>();
      rtnl_handler_->Start(RTMGRP_LINK);
      link_listener_ = std::make_unique<shill::RTNLListener>(
          shill::RTNLHandler::kRequestLink,
          Bind(&ArcService::ContainerImpl::LinkMsgHandler,
               weak_factory_.GetWeakPtr()),
          rtnl_handler_.get());
    } else {
      // This is bad - it means we won't ever be able to tell when the container
      // brings up an interface.
      LOG(ERROR)
          << "Cannot start netlink listener - invalid container namespace?";
      return false;
    }
  }

  dev_mgr_->RegisterDeviceIPv6AddressFoundHandler(
      guest(), base::Bind(&ArcService::ContainerImpl::SetupIPv6,
                          weak_factory_.GetWeakPtr()));

  LOG(INFO) << "ARC++ network service started {pid: " << pid_ << "}";
  return true;
}

void ArcService::ContainerImpl::Stop(int32_t /*pid*/) {
  rtnl_handler_->RemoveListener(link_listener_.get());
  link_listener_.reset();
  rtnl_handler_.reset();

  LOG(INFO) << "ARC++ network service stopped {pid: " << pid_ << "}";
  pid_ = kInvalidPID;
}

bool ArcService::ContainerImpl::IsStarted(int32_t* pid) const {
  if (pid)
    *pid = pid_;

  return pid_ != kInvalidPID;
}

bool ArcService::ContainerImpl::OnStartDevice(Device* device) {
  const auto& config = device->config();

  LOG(INFO) << "Starting device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname() << " pid: " << pid_;

  std::string veth_ifname = datapath_->AddVirtualBridgedInterface(
      device->ifname(), MacAddressToString(config.guest_mac_addr()),
      config.host_ifname());
  if (veth_ifname.empty()) {
    LOG(ERROR) << "Failed to create virtual interface for container";
    return false;
  }

  if (!datapath_->AddInterfaceToContainer(
          pid_, veth_ifname, config.guest_ifname(),
          IPv4AddressToString(config.guest_ipv4_addr()),
          device->options().fwd_multicast)) {
    LOG(ERROR) << "Failed to create container interface.";
    datapath_->RemoveInterface(veth_ifname);
    datapath_->RemoveBridge(config.host_ifname());
    return false;
  }

  // Signal the container that the network device is ready.
  if (device->IsAndroid()) {
    datapath_->runner().WriteSentinelToContainer(base::IntToString(pid_));
  }

  return true;
}

void ArcService::ContainerImpl::OnStopDevice(Device* device) {
  const auto& config = device->config();

  LOG(INFO) << "Stopping device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname() << " pid: " << pid_;

  device->Disable();
  if (!device->IsAndroid()) {
    datapath_->RemoveInterface(ArcVethHostName(device->ifname()));
  }
}

void ArcService::ContainerImpl::OnDefaultInterfaceChanged(
    const std::string& ifname) {
  if (!IsStarted())
    return;

  // For ARC N, we must always be able to find the arc0 device and, at a
  // minimum, disable it.
  if (guest() == GuestMessage::ARC_LEGACY) {
    datapath_->RemoveLegacyIPv4InboundDNAT();
    auto* device = dev_mgr_->FindByGuestInterface("arc0");
    if (!device) {
      LOG(DFATAL) << "Expected legacy Android device missing";
      return;
    }
    device->Disable();

    // If a new default interface was given, then re-enable with that.
    if (!ifname.empty()) {
      datapath_->AddLegacyIPv4InboundDNAT(ifname);
      device->Enable(ifname);
    }
    return;
  }

  // For ARC P and later, we're only concerned with resetting the device when it
  // becomes the default (again) in order to ensure any previous configuration.
  // is cleared.
  if (ifname.empty())
    return;

  auto* device = dev_mgr_->FindByGuestInterface(ifname);
  if (!device) {
    LOG(ERROR) << "Expected default device missing: " << ifname;
    return;
  }
  device->StopIPv6RoutingLegacy();
  device->StartIPv6RoutingLegacy(ifname);
}

void ArcService::ContainerImpl::LinkMsgHandler(const shill::RTNLMessage& msg) {
  if (!msg.HasAttribute(IFLA_IFNAME)) {
    LOG(ERROR) << "Link event message does not have IFLA_IFNAME";
    return;
  }
  bool link_up = msg.link_status().flags & IFF_UP;
  shill::ByteString b(msg.GetAttribute(IFLA_IFNAME));
  std::string ifname(reinterpret_cast<const char*>(
      b.GetSubstring(0, IFNAMSIZ).GetConstData()));

  auto* device = dev_mgr_->FindByGuestInterface(ifname);
  if (!device)
    return;

  Context* ctx = dynamic_cast<Context*>(device->context(guest()));
  if (!ctx) {
    LOG(DFATAL) << "Context missing";
    return;
  }

  // If the link status is unchanged, there is nothing to do.
  if (!ctx->SetLinkUp(link_up))
    return;

  if (!link_up) {
    LOG(INFO) << ifname << " is now down";
    return;
  }
  LOG(INFO) << ifname << " is now up";

  if (device->UsesDefaultInterface()) {
    OnDefaultInterfaceChanged(dev_mgr_->DefaultInterface());
    return;
  }

  if (device->IsAndroid())
    return;

  device->Enable(ifname);
}

void ArcService::ContainerImpl::SetupIPv6(Device* device) {
  device->RegisterIPv6TeardownHandler(base::Bind(
      &ArcService::ContainerImpl::TeardownIPv6, weak_factory_.GetWeakPtr()));

  auto& ipv6_config = device->ipv6_config();
  if (ipv6_config.ifname.empty())
    return;

  Context* ctx = dynamic_cast<Context*>(device->context(guest()));
  if (!ctx) {
    LOG(DFATAL) << "Context missing";
    return;
  }
  if (ctx->HasIPv6())
    return;

  LOG(INFO) << "Setting up IPv6 for " << ipv6_config.ifname;

  int table_id =
      GetAndroidRoutingTableId(device->config().guest_ifname(), pid_);
  if (table_id == kInvalidTableID) {
    if (ctx->RoutingTableAttempts() < kMaxTableRetries) {
      LOG(INFO) << "Could not look up routing table ID for container interface "
                << device->config().guest_ifname() << " - trying again...";
      base::MessageLoop::current()->task_runner()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&ArcService::ContainerImpl::SetupIPv6,
                     weak_factory_.GetWeakPtr(), device),
          kTableRetryDelay);
    } else {
      LOG(DFATAL)
          << "Could not look up routing table ID for container interface "
          << device->config().guest_ifname();
    }
    return;
  }

  LOG(INFO) << "Setting IPv6 address " << ipv6_config.addr
            << "/128, gateway=" << ipv6_config.router << " on "
            << ipv6_config.ifname;

  char buf[INET6_ADDRSTRLEN] = {0};
  if (!inet_ntop(AF_INET6, &ipv6_config.addr, buf, sizeof(buf))) {
    LOG(DFATAL) << "Invalid address: " << ipv6_config.addr;
    return;
  }
  std::string addr = buf;

  if (!inet_ntop(AF_INET6, &ipv6_config.router, buf, sizeof(buf))) {
    LOG(DFATAL) << "Invalid router address: " << ipv6_config.router;
    return;
  }
  std::string router = buf;

  const auto& config = device->config();
  {
    ScopedNS ns(pid_);
    if (!ns.IsValid()) {
      LOG(ERROR) << "Invalid container namespace (" << pid_
                 << ") - cannot configure IPv6.";
      return;
    }
    // Tag the interface so that ARC can detect this manual configuration and
    // skip disabling and re-enabling IPv6 (b/144545910).
    if (!datapath_->SetInterfaceFlag(config.guest_ifname(), IFF_DEBUG)) {
      LOG(ERROR) << "Failed to mark IPv6 manual config flag on interface";
    }
    if (!datapath_->AddIPv6GatewayRoutes(config.guest_ifname(), addr, router,
                                         ipv6_config.prefix_len, table_id)) {
      LOG(ERROR) << "Failed to setup IPv6 routes in the container";
      return;
    }
  }

  if (!datapath_->AddIPv6HostRoute(config.host_ifname(), addr,
                                   ipv6_config.prefix_len)) {
    LOG(ERROR) << "Failed to setup the IPv6 route for interface "
               << config.host_ifname();
    return;
  }

  if (!datapath_->AddIPv6Neighbor(ipv6_config.ifname, addr)) {
    LOG(ERROR) << "Failed to setup the IPv6 neighbor proxy";
    datapath_->RemoveIPv6HostRoute(config.host_ifname(), addr,
                                   ipv6_config.prefix_len);
    return;
  }

  if (!datapath_->AddIPv6Forwarding(ipv6_config.ifname,
                                    device->config().host_ifname())) {
    LOG(ERROR) << "Failed to setup iptables for IPv6";
    datapath_->RemoveIPv6Neighbor(ipv6_config.ifname, addr);
    datapath_->RemoveIPv6HostRoute(config.host_ifname(), addr,
                                   ipv6_config.prefix_len);
    return;
  }

  ctx->SetHasIPv6(table_id);
}

void ArcService::ContainerImpl::TeardownIPv6(Device* device) {
  Context* ctx = dynamic_cast<Context*>(device->context(guest()));
  if (!ctx || !ctx->HasIPv6())
    return;

  auto& ipv6_config = device->ipv6_config();
  LOG(INFO) << "Clearing IPv6 for " << ipv6_config.ifname;
  int table_id = ctx->RoutingTableID();
  ctx->ClearIPv6();

  char buf[INET6_ADDRSTRLEN] = {0};
  if (!inet_ntop(AF_INET6, &ipv6_config.addr, buf, sizeof(buf))) {
    LOG(DFATAL) << "Invalid address: " << ipv6_config.addr;
    return;
  }
  std::string addr = buf;

  if (!inet_ntop(AF_INET6, &ipv6_config.router, buf, sizeof(buf))) {
    LOG(DFATAL) << "Invalid router address: " << ipv6_config.router;
    return;
  }
  std::string router = buf;

  const auto& config = device->config();
  datapath_->RemoveIPv6Forwarding(ipv6_config.ifname, config.host_ifname());
  datapath_->RemoveIPv6Neighbor(ipv6_config.ifname, addr);
  datapath_->RemoveIPv6HostRoute(config.host_ifname(), addr,
                                 ipv6_config.prefix_len);

  ScopedNS ns(pid_);
  if (ns.IsValid()) {
    datapath_->RemoveIPv6GatewayRoutes(config.guest_ifname(), addr, router,
                                       ipv6_config.prefix_len, table_id);
  }
}

// VM specific functions

ArcService::VmImpl::VmImpl(DeviceManagerBase* dev_mgr, Datapath* datapath)
    : cid_(kInvalidCID), dev_mgr_(dev_mgr), datapath_(datapath) {}

GuestMessage::GuestType ArcService::VmImpl::guest() const {
  return GuestMessage::ARC_VM;
}

bool ArcService::VmImpl::Start(int32_t cid) {
  // This can happen if concierge crashes and doesn't send the vm down RPC.
  // It can probably be addressed by stopping and restarting the service.
  if (cid_ != kInvalidCID)
    return false;

  if (cid <= kInvalidCID) {
    LOG(ERROR) << "Invalid VM cid " << cid;
    return false;
  }

  cid_ = cid;
  LOG(INFO) << "ARCVM network service started {cid: " << cid_ << "}";

  return true;
}

void ArcService::VmImpl::Stop(int32_t cid) {
  if (cid_ != cid) {
    LOG(ERROR) << "Mismatched ARCVM CIDs " << cid_ << " != " << cid;
    return;
  }

  LOG(INFO) << "ARCVM network service stopped {cid: " << cid_ << "}";
  cid_ = kInvalidCID;
}

bool ArcService::VmImpl::IsStarted(int32_t* cid) const {
  if (cid)
    *cid = cid_;

  return cid_ > kInvalidCID;
}

bool ArcService::VmImpl::OnStartDevice(Device* device) {
  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations.
  if (!device->UsesDefaultInterface())
    return false;

  const auto& config = device->config();

  LOG(INFO) << "Starting device " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname() << " cid: " << cid_;

  Context* ctx = dynamic_cast<Context*>(device->context(guest()));
  if (!ctx) {
    LOG(ERROR) << "Context missing";
    return false;
  }

  // Since the interface will be added to the bridge, no address configuration
  // should be provided here.
  std::string tap =
      datapath_->AddTAP("" /* auto-generate name */, nullptr /* no mac addr */,
                        nullptr /* no ipv4 subnet */, vm_tools::kCrosVmUser);
  if (tap.empty()) {
    LOG(ERROR) << "Failed to create TAP device for VM";
    return false;
  }

  if (!datapath_->AddToBridge(config.host_ifname(), tap)) {
    LOG(ERROR) << "Failed to bridge TAP device " << tap;
    datapath_->RemoveInterface(tap);
    return false;
  }

  ctx->SetTAP(tap);
  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations; but for now ARCVM needs to be treated like ARC++ N.
  OnDefaultInterfaceChanged(dev_mgr_->DefaultInterface());
  return true;
}

void ArcService::VmImpl::OnStopDevice(Device* device) {
  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations.
  if (!device->UsesDefaultInterface())
    return;

  const auto& config = device->config();

  LOG(INFO) << "Stopping " << device->ifname()
            << " bridge: " << config.host_ifname()
            << " guest_iface: " << config.guest_ifname() << " cid: " << cid_;

  Context* ctx = dynamic_cast<Context*>(device->context(guest()));
  if (!ctx) {
    LOG(ERROR) << "Context missing";
    return;
  }

  device->Disable();
  datapath_->RemoveInterface(ctx->TAP());
}

void ArcService::VmImpl::OnDefaultInterfaceChanged(const std::string& ifname) {
  if (!IsStarted())
    return;

  // TODO(garrick): Remove this once ARCVM supports ad hoc interface
  // configurations; but for now ARCVM needs to be treated like ARC++ N.
  datapath_->RemoveLegacyIPv4InboundDNAT();
  auto* device = dev_mgr_->FindByGuestInterface("arc0");
  if (!device) {
    LOG(DFATAL) << "Expected Android device missing";
    return;
  }
  device->Disable();

  // If a new default interface was given, then re-enable with that.
  if (!ifname.empty()) {
    datapath_->AddLegacyIPv4InboundDNAT(ifname);
    device->Enable(ifname);
  }
}

}  // namespace arc_networkd
