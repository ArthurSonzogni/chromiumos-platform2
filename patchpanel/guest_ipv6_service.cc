// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/guest_ipv6_service.h"

#include <net/ethernet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/signal.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>

#include <brillo/process/process.h>

#include "patchpanel/ipc.h"
#include "patchpanel/net_util.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

namespace {

constexpr char kRadvdRunDir[] = "/run/radvd";
constexpr char kRadvdPath[] = "/usr/sbin/radvd";
constexpr char kRadvdConfigFilePrefix[] = "radvd.conf.";
constexpr char kRadvdPidFilePrefix[] = "radvd.pid.";
constexpr base::TimeDelta kTimeoutForSIGTERM = base::Seconds(2);
constexpr base::TimeDelta kTimeoutForSIGKILL = base::Seconds(1);

GuestIPv6Service::ForwardMethod GetForwardMethodByDeviceType(
    ShillClient::Device::Type type) {
  switch (type) {
    case ShillClient::Device::Type::kEthernet:
    case ShillClient::Device::Type::kEthernetEap:
    case ShillClient::Device::Type::kWifi:
      return GuestIPv6Service::ForwardMethod::kMethodNDProxy;
    case ShillClient::Device::Type::kCellular:
      return GuestIPv6Service::ForwardMethod::kMethodRAServer;
    default:
      return GuestIPv6Service::ForwardMethod::kMethodUnknown;
  }
}

bool PrepareRunPath() {
  base::FilePath run_path(kRadvdRunDir);
  if (!base::DirectoryExists(run_path) && !base::CreateDirectory(run_path)) {
    PLOG(ERROR) << "Unable to create configuration directory  " << kRadvdRunDir;
    return false;
  }

  if (chown(kRadvdRunDir, kPatchpaneldUid, kPatchpaneldGid) != 0) {
    PLOG(ERROR) << "Failed to change owner group of configuration directory "
                << kRadvdRunDir;
    base::DeletePathRecursively(run_path);
    return false;
  }

  if (chmod(kRadvdRunDir, S_IRWXU | S_IRGRP | S_IXGRP)) {
    PLOG(ERROR) << "Failed to set permissions on " << kRadvdRunDir;
    base::DeletePathRecursively(run_path);
    return false;
  }
  return true;
}

bool CreateConfigFile(const std::string& ifname, const std::string& prefix) {
  std::vector<std::string> lines;
  lines.push_back(base::StringPrintf("interface %s {", ifname.c_str()));
  lines.push_back("  AdvSendAdvert on;");
  lines.push_back(base::StringPrintf("  prefix %s/64 {", prefix.c_str()));
  lines.push_back("    AdvOnLink off;");
  lines.push_back("    AdvAutonomous on;");
  lines.push_back("  };");
  lines.push_back("};");
  lines.push_back("");
  std::string contents = base::JoinString(lines, "\n");

  const base::FilePath& conf_file_path =
      base::FilePath(kRadvdRunDir)
          .Append(std::string(kRadvdConfigFilePrefix) + ifname);
  if (!base::WriteFile(conf_file_path, contents)) {
    PLOG(ERROR) << "Failed to write config file";
    return false;
  }

  if (chmod(conf_file_path.value().c_str(), S_IRUSR | S_IRGRP)) {
    PLOG(ERROR) << "Failed to set permissions on " << conf_file_path;
    base::DeletePathRecursively(conf_file_path);
    return false;
  }

  if (chown(conf_file_path.value().c_str(), kPatchpaneldUid, kPatchpaneldGid) !=
      0) {
    PLOG(ERROR) << "Failed to change owner group of configuration file "
                << conf_file_path;
    base::DeletePathRecursively(conf_file_path);
    return false;
  }
  return true;
}

}  // namespace

// TODO(b/228585272): Support prefix larger than /64
// static
std::string GuestIPv6Service::IPAddressTo64BitPrefix(
    const std::string addr_str) {
  if (addr_str.empty()) {
    return "";
  }
  in6_addr addr = StringToIPv6Address(addr_str);
  memset(&addr.s6_addr[8], 0, 8);
  return IPv6AddressToString(addr);
}

GuestIPv6Service::GuestIPv6Service(SubprocessController* nd_proxy,
                                   Datapath* datapath,
                                   ShillClient* shill_client,
                                   System* system)
    : nd_proxy_(nd_proxy),
      datapath_(datapath),
      shill_client_(shill_client),
      system_(system) {}

void GuestIPv6Service::Start() {
  nd_proxy_->RegisterFeedbackMessageHandler(base::BindRepeating(
      &GuestIPv6Service::OnNDProxyMessage, weak_factory_.GetWeakPtr()));
  nd_proxy_->Listen();
}

void GuestIPv6Service::StartForwarding(const std::string& ifname_uplink,
                                       const std::string& ifname_downlink,
                                       bool downlink_is_tethering) {
  LOG(INFO) << "Starting IPv6 forwarding between uplink: " << ifname_uplink
            << ", downlink: " << ifname_downlink;
  int if_id_uplink = system_->IfNametoindex(ifname_uplink);
  if (if_id_uplink == 0) {
    PLOG(ERROR) << "Get interface index failed on " << ifname_uplink;
    return;
  }
  if_cache_[ifname_uplink] = if_id_uplink;
  int if_id_downlink = system_->IfNametoindex(ifname_downlink);
  if (if_id_downlink == 0) {
    PLOG(ERROR) << "Get interface index failed on " << ifname_downlink;
    return;
  }
  if_cache_[ifname_downlink] = if_id_downlink;

  // Lookup ForwardEntry for the specified uplink. If it does not exist, create
  // a new one based on its device type.
  ForwardMethod forward_method;
  std::vector<ForwardEntry>::iterator it;
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == ifname_uplink)
      break;
  }
  if (it != forward_record_.end()) {
    forward_method = it->method;
    it->downstream_ifnames.insert(ifname_downlink);
  } else if (forward_method_override_.find(ifname_uplink) !=
             forward_method_override_.end()) {
    forward_method = forward_method_override_[ifname_uplink];
    forward_record_.push_back(ForwardEntry{
        forward_method, ifname_uplink, std::set<std::string>{ifname_downlink}});
  } else {
    ShillClient::Device upstream_shill_device;
    shill_client_->GetDeviceProperties(ifname_uplink, &upstream_shill_device);
    forward_method = GetForwardMethodByDeviceType(upstream_shill_device.type);

    if (forward_method == ForwardMethod::kMethodUnknown) {
      LOG(INFO) << "IPv6 forwarding not supported on device type of "
                << ifname_uplink << ", skipped";
      return;
    }
    forward_record_.push_back(ForwardEntry{
        forward_method, ifname_uplink, std::set<std::string>{ifname_downlink}});
  }

  if (!datapath_->MaskInterfaceFlags(ifname_uplink, IFF_ALLMULTI)) {
    LOG(WARNING) << "Failed to setup all multicast mode for interface "
                 << ifname_uplink;
  }
  if (!datapath_->MaskInterfaceFlags(ifname_downlink, IFF_ALLMULTI)) {
    LOG(WARNING) << "Failed to setup all multicast mode for interface "
                 << ifname_downlink;
  }

  switch (forward_method) {
    case ForwardMethod::kMethodNDProxy:
      SendNDProxyControl(NDProxyControlMessage::START_NS_NA_RS_RA, if_id_uplink,
                         if_id_downlink);
      break;
    case ForwardMethod::kMethodNDProxyInjectingRA:
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS,
          if_id_uplink, if_id_downlink);
      break;
    case ForwardMethod::kMethodRAServer:
      // No need of proxying between downlink and uplink for RA server.
      SendNDProxyControl(NDProxyControlMessage::START_NEIGHBOR_MONITOR,
                         if_id_downlink, 0);
      break;
    case ForwardMethod::kMethodUnknown:
      NOTREACHED();
  }

  // Start NA proxying between the new downlink and existing downlinks, if any.
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == ifname_uplink)
      break;
  }
  CHECK(it != forward_record_.end());
  for (const auto& another_downlink : it->downstream_ifnames) {
    if (another_downlink != ifname_downlink) {
      int32_t if_id_downlink2 = if_cache_[another_downlink];
      SendNDProxyControl(NDProxyControlMessage::START_NS_NA, if_id_downlink,
                         if_id_downlink2);
    }
  }

  const std::string& uplink_ip = uplink_ips_[ifname_uplink];
  if (!uplink_ip.empty()) {
    // Allow IPv6 address on uplink to be resolvable on the downlink
    if (!datapath_->AddIPv6NeighborProxy(ifname_downlink, uplink_ip)) {
      LOG(WARNING) << "Failed to setup the IPv6 neighbor: " << uplink_ip
                   << " proxy on dev " << ifname_downlink;
    }

    if (forward_method == ForwardMethod::kMethodRAServer) {
      if (!StartRAServer(ifname_downlink, IPAddressTo64BitPrefix(uplink_ip))) {
        LOG(WARNING) << "Failed to start RA server on downlink "
                     << ifname_downlink << " with uplink " << ifname_uplink
                     << " ip " << uplink_ip;
      }
    }
  }
}

void GuestIPv6Service::StopForwarding(const std::string& ifname_uplink,
                                      const std::string& ifname_downlink) {
  LOG(INFO) << "Stopping IPv6 forwarding between uplink: " << ifname_uplink
            << ", downlink: " << ifname_downlink;

  std::vector<ForwardEntry>::iterator it;
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == ifname_uplink)
      break;
  }
  if (it == forward_record_.end()) {
    return;
  }
  if (it->downstream_ifnames.find(ifname_downlink) ==
      it->downstream_ifnames.end()) {
    return;
  }

  if (it->method != ForwardMethod::kMethodRAServer) {
    SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                       if_cache_[ifname_uplink], if_cache_[ifname_downlink]);
  }

  // Remove proxying between specified downlink and all other downlinks in the
  // same group.
  for (const auto& another_downlink : it->downstream_ifnames) {
    if (another_downlink != ifname_downlink) {
      SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                         if_cache_[ifname_downlink],
                         if_cache_[another_downlink]);
    }
  }

  // Remove ip neigh proxy entry
  if (uplink_ips_[ifname_uplink] != "") {
    datapath_->RemoveIPv6NeighborProxy(ifname_downlink,
                                       uplink_ips_[ifname_uplink]);
  }
  // Remove downlink /128 routes
  for (const auto& neighbor_ip : downstream_neighbors_[ifname_downlink]) {
    datapath_->RemoveIPv6HostRoute(neighbor_ip, 128);
  }
  downstream_neighbors_[ifname_downlink].clear();

  if (it->method == ForwardMethod::kMethodRAServer) {
    SendNDProxyControl(NDProxyControlMessage::STOP_NEIGHBOR_MONITOR,
                       if_cache_[ifname_downlink], 0);
    if (uplink_ips_[ifname_uplink] != "") {
      StopRAServer(ifname_downlink);
    }
  }

  it->downstream_ifnames.erase(ifname_downlink);
  if (it->downstream_ifnames.empty()) {
    forward_record_.erase(it);
  }
}

void GuestIPv6Service::StopUplink(const std::string& ifname_uplink) {
  LOG(INFO) << "Stopping all IPv6 forwarding with uplink: " << ifname_uplink;

  std::vector<ForwardEntry>::iterator it;
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == ifname_uplink)
      break;
  }
  if (it == forward_record_.end())
    return;

  // Remove proxying between specified uplink and all downlinks.
  if (it->method != ForwardMethod::kMethodRAServer) {
    for (const auto& ifname_downlink : it->downstream_ifnames) {
      SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                         if_cache_[ifname_uplink], if_cache_[ifname_downlink]);
    }
  }

  // Remove proxying between all downlink pairs in the forward group.
  const auto& downlinks = it->downstream_ifnames;
  for (auto it1 = downlinks.begin(); it1 != downlinks.end(); it1++) {
    for (auto it2 = std::next(it1); it2 != downlinks.end(); it2++) {
      SendNDProxyControl(NDProxyControlMessage::STOP_PROXY,
                         if_cache_[it1->c_str()], if_cache_[it2->c_str()]);
    }
  }

  for (const auto& ifname_downlink : it->downstream_ifnames) {
    // Remove ip neigh proxy entry
    if (uplink_ips_[ifname_uplink] != "") {
      datapath_->RemoveIPv6NeighborProxy(ifname_downlink,
                                         uplink_ips_[ifname_uplink]);
    }
    // Remove downlink /128 routes
    for (const auto& neighbor_ip : downstream_neighbors_[ifname_downlink]) {
      datapath_->RemoveIPv6HostRoute(neighbor_ip, 128);
    }
    downstream_neighbors_[ifname_downlink].clear();
  }

  if (it->method == ForwardMethod::kMethodRAServer) {
    for (const auto& ifname_downlink : it->downstream_ifnames) {
      SendNDProxyControl(NDProxyControlMessage::STOP_NEIGHBOR_MONITOR,
                         if_cache_[ifname_downlink], 0);
      if (uplink_ips_[ifname_uplink] != "") {
        StopRAServer(ifname_downlink);
      }
    }
  }

  forward_record_.erase(it);
}

void GuestIPv6Service::OnUplinkIPv6Changed(const std::string& ifname,
                                           const std::string& uplink_ip) {
  VLOG(1) << "OnUplinkIPv6Changed: " << ifname << ", {" << uplink_ips_[ifname]
          << "} to {" << uplink_ip << "}";
  if (uplink_ips_[ifname] == uplink_ip) {
    return;
  }

  std::vector<ForwardEntry>::iterator it;
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == ifname)
      break;
  }
  if (it != forward_record_.end()) {
    // Note that the order of StartForwarding() and OnUplinkIPv6Changed() is not
    // certain so the `ip neigh proxy` and /128 route changes need to be handled
    // in both code paths. When an uplink is newly connected to,
    // StartForwarding() get called first and then we received
    // OnUplinkIPv6Changed() when uplink get an IPv6 address. When default
    // network switches to an existing uplink, StartForwarding() is after
    // OnUplinkIPv6Changed() (which was already called when it was not default
    // yet).
    for (const auto& ifname_downlink : it->downstream_ifnames) {
      // Update ip neigh proxy entries
      if (uplink_ips_[ifname] != "") {
        datapath_->RemoveIPv6NeighborProxy(ifname_downlink,
                                           uplink_ips_[ifname]);
      }
      if (uplink_ip != "") {
        if (!datapath_->AddIPv6NeighborProxy(ifname_downlink, uplink_ip)) {
          LOG(WARNING) << "Failed to setup the IPv6 neighbor: " << uplink_ip
                       << " proxy on dev " << ifname_downlink;
        }
      }

      // Update downlink /128 routes source IP. Note AddIPv6HostRoute uses `ip
      // route replace` so we don't need to remove the old one first.
      for (const auto& neighbor_ip : downstream_neighbors_[ifname_downlink]) {
        if (!datapath_->AddIPv6HostRoute(ifname, neighbor_ip, 128, uplink_ip)) {
          LOG(WARNING) << "Failed to setup the IPv6 route: " << neighbor_ip
                       << " dev " << ifname << " src " << uplink_ip;
        }
      }

      if (it->method == ForwardMethod::kMethodRAServer) {
        auto old_prefix = IPAddressTo64BitPrefix(uplink_ips_[ifname]);
        auto new_prefix = IPAddressTo64BitPrefix(uplink_ip);
        if (old_prefix == new_prefix) {
          continue;
        }
        if (!old_prefix.empty()) {
          StopRAServer(ifname_downlink);
        }
        if (!new_prefix.empty()) {
          if (!StartRAServer(ifname_downlink, new_prefix)) {
            LOG(WARNING) << "Failed to start RA server on downlink "
                         << ifname_downlink << " with uplink " << ifname
                         << " ip " << uplink_ip;
          }
        }
      }
    }
  }

  uplink_ips_[ifname] = uplink_ip;
}

void GuestIPv6Service::StartLocalHotspot(
    const std::string& ifname_hotspot_link,
    const std::string& prefix,
    const std::vector<std::string>& rdnss,
    const std::vector<std::string>& dnssl) {
  NOTIMPLEMENTED();
}

void GuestIPv6Service::StopLocalHotspot(
    const std::string& ifname_hotspot_link) {
  NOTIMPLEMENTED();
}

void GuestIPv6Service::SetForwardMethod(const std::string& ifname_uplink,
                                        ForwardMethod method) {
  forward_method_override_[ifname_uplink] = method;

  std::vector<ForwardEntry>::iterator it;
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == ifname_uplink)
      break;
  }
  if (it != forward_record_.end()) {
    // Need a copy here since StopUplink() will modify the record
    auto downlinks = it->downstream_ifnames;
    StopUplink(ifname_uplink);
    for (const auto& downlink : downlinks) {
      StartForwarding(ifname_uplink, downlink);
    }
  }
}

void GuestIPv6Service::SendNDProxyControl(
    NDProxyControlMessage::NDProxyRequestType type,
    int32_t if_id_primary,
    int32_t if_id_secondary) {
  VLOG(4) << "Sending NDProxyControlMessage: " << type << ": " << if_id_primary
          << "<->" << if_id_secondary;
  NDProxyControlMessage msg;
  msg.set_type(type);
  msg.set_if_id_primary(if_id_primary);
  msg.set_if_id_secondary(if_id_secondary);
  ControlMessage cm;
  *cm.mutable_ndproxy_control() = msg;
  nd_proxy_->SendControlMessage(cm);
}

void GuestIPv6Service::OnNDProxyMessage(const FeedbackMessage& fm) {
  if (!fm.has_ndproxy_signal()) {
    LOG(ERROR) << "Unexpected feedback message type";
    return;
  }

  const NDProxySignalMessage& msg = fm.ndproxy_signal();
  if (msg.has_neighbor_detected_signal()) {
    const auto& inner_msg = msg.neighbor_detected_signal();
    in6_addr ip;
    memcpy(&ip, inner_msg.ip().data(), sizeof(in6_addr));
    std::string ip6_str = IPv6AddressToString(ip);
    std::string ifname = system_->IfIndextoname(inner_msg.if_id());

    RegisterDownstreamNeighborIP(ifname, ip6_str);
    return;
  }

  if (msg.has_router_detected_signal()) {
    // This event is currently not used.
    return;
  }

  LOG(ERROR) << "Unknown NDProxy event ";
  NOTREACHED();
}

void GuestIPv6Service::RegisterDownstreamNeighborIP(
    const std::string& ifname_downlink, const std::string& ip) {
  downstream_neighbors_[ifname_downlink].insert(ip);

  const auto& uplink = DownlinkToUplink(ifname_downlink);
  if (!uplink) {
    LOG(WARNING) << __func__ << ": " << ifname_downlink << ", neighbor IP "
                 << ip << ", no corresponding uplink";
    return;
  }

  LOG(INFO) << __func__ << ": " << ifname_downlink << ", neighbor IP " << ip
            << ", corresponding uplink " << uplink.value() << "["
            << uplink_ips_[uplink.value()] << "]";
  if (!datapath_->AddIPv6HostRoute(ifname_downlink, ip, 128,
                                   uplink_ips_[uplink.value()])) {
    LOG(WARNING) << "Failed to setup the IPv6 route: " << ip << " dev "
                 << ifname_downlink << " src " << uplink_ips_[uplink.value()];
  }
}

std::optional<std::string> GuestIPv6Service::DownlinkToUplink(
    const std::string& downlink) {
  std::vector<ForwardEntry>::iterator it;
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->downstream_ifnames.find(downlink) != it->downstream_ifnames.end())
      return it->upstream_ifname;
  }
  return std::nullopt;
}

const std::set<std::string>& GuestIPv6Service::UplinkToDownlinks(
    const std::string& uplink) {
  static std::set<std::string> empty_set;

  std::vector<ForwardEntry>::iterator it;
  for (it = forward_record_.begin(); it != forward_record_.end(); it++) {
    if (it->upstream_ifname == uplink)
      return it->downstream_ifnames;
  }
  return empty_set;
}

bool GuestIPv6Service::StartRAServer(const std::string& ifname,
                                     const std::string& prefix) {
  return PrepareRunPath() && CreateConfigFile(ifname, prefix) &&
         StartRadvd(ifname);
}

bool GuestIPv6Service::StopRAServer(const std::string& ifname) {
  const base::FilePath& pid_file_path =
      base::FilePath(kRadvdRunDir)
          .Append(std::string(kRadvdPidFilePrefix) + ifname);

  std::string pid_str;
  pid_t pid;
  if (!base::ReadFileToString(pid_file_path, &pid_str) ||
      !base::TrimString(pid_str, "\n", &pid_str) ||
      !base::StringToInt(pid_str, &pid)) {
    LOG(WARNING) << "Invalid radvd pid file " << pid_file_path;
    return false;
  }

  if (!brillo::Process::ProcessExists(pid)) {
    LOG(WARNING) << "radvd[" << pid << "] already stopped for interface "
                 << ifname;
    return true;
  }
  brillo::ProcessImpl process;
  process.Reset(pid);
  if (process.Kill(SIGTERM, kTimeoutForSIGTERM.InSeconds())) {
    base::DeleteFile(pid_file_path);
    return true;
  }
  LOG(WARNING) << "Not able to gracefully stop radvd[" << pid
               << "] for interface " << ifname << ", trying to force stop";
  if (process.Kill(SIGKILL, kTimeoutForSIGKILL.InSeconds())) {
    base::DeleteFile(pid_file_path);
    return true;
  }
  LOG(ERROR) << "Cannot stop radvd[" << pid << "] for interface " << ifname;
  return false;
}

bool GuestIPv6Service::StartRadvd(const std::string& ifname) {
  const base::FilePath& conf_file_path =
      base::FilePath(kRadvdRunDir)
          .Append(std::string(kRadvdConfigFilePrefix) + ifname);
  const base::FilePath& pid_file_path =
      base::FilePath(kRadvdRunDir)
          .Append(std::string(kRadvdPidFilePrefix) + ifname);

  std::vector<std::string> argv = {
      kRadvdPath, "-n",
      "-C",       conf_file_path.value(),
      "-p",       pid_file_path.value(),
      "-m",       "syslog",
  };

  auto mj = brillo::Minijail::GetInstance();
  minijail* jail = mj->New();
  mj->DropRoot(jail, kPatchpaneldUid, kPatchpaneldGid);
  constexpr uint64_t kNetRawCapMask = CAP_TO_MASK(CAP_NET_RAW);
  mj->UseCapabilities(jail, kNetRawCapMask);

  std::vector<char*> args;
  for (const auto& arg : argv) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  pid_t pid;
  bool ran = mj->RunAndDestroy(jail, args, &pid);

  return ran;
}

}  // namespace patchpanel
