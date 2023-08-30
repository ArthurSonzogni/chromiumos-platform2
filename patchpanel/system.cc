// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/system.h"

#include <fcntl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

namespace patchpanel {

namespace {

// /proc/sys/ paths used for System::SysNetSet() and System::SysNetGet().
// Defines the local port range that is used by TCP and UDP traffic to choose
// the local port (IPv4 and IPv6).
constexpr char kSysNetIPLocalPortRangePath[] =
    "/proc/sys/net/ipv4/ip_local_port_range";
// Enables/Disables IPv4 forwarding between interfaces.
constexpr char kSysNetIPv4ForwardingPath[] = "/proc/sys/net/ipv4/ip_forward";
// /proc/sys path for controlling connection tracking helper modules
constexpr char kSysNetConntrackHelperPath[] =
    "/proc/sys/net/netfilter/nf_conntrack_helper";
// Enables/Disables IPv6.
constexpr char kSysNetDisableIPv6Path[] =
    "/proc/sys/net/ipv6/conf/all/disable_ipv6";
// Allow localhost as a source or destination when routing IPv4.
constexpr char kSysNetIPv4RouteLocalnetPath[] =
    "/proc/sys/net/ipv4/conf/%s/route_localnet";
// Enables/Disables IPv6 forwarding between interfaces.
constexpr char kSysNetIPv6ForwardingPath[] =
    "/proc/sys/net/ipv6/conf/all/forwarding";
// Accept Router Advertisements on an interface and autoconfiguring it with IPv6
// parameters.
constexpr char kSysNetIPv6AcceptRaPath[] =
    "/proc/sys/net/ipv6/conf/%s/accept_ra";
// Enables/Disables IPv6 cross-inteface NDP response.
constexpr char kSysNetIPv6ProxyNDPPath[] =
    "/proc/sys/net/ipv6/conf/all/proxy_ndp";
constexpr char kSysNetIPv6HopLimitPath[] =
    "/proc/sys/net/ipv6/conf/%s/hop_limit";

constexpr char kTunDev[] = "/dev/net/tun";
}  // namespace

int System::Ioctl(int fd, ioctl_req_t request, const char* argp) {
  return ioctl(fd, request, argp);
}

int System::Ioctl(int fd, ioctl_req_t request, uint64_t arg) {
  return ioctl(fd, request, arg);
}

int System::Ioctl(int fd, ioctl_req_t request, struct ifreq* ifr) {
  return ioctl(fd, request, ifr);
}

int System::Ioctl(int fd, ioctl_req_t request, struct rtentry* route) {
  return ioctl(fd, request, route);
}

int System::Ioctl(int fd, ioctl_req_t request, struct in6_rtmsg* route) {
  return ioctl(fd, request, route);
}

int System::SocketPair(int domain, int type, int protocol, int sv[2]) {
  return socketpair(domain, type, protocol, sv);
}

pid_t System::WaitPid(pid_t pid, int* wstatus, int options) {
  return waitpid(pid, wstatus, options);
}

base::ScopedFD System::OpenTunDev() {
  return base::ScopedFD(open(kTunDev, O_RDWR | O_NONBLOCK));
}

bool System::SysNetSet(SysNet target,
                       std::string_view content,
                       std::string_view iface) {
  const std::string path = SysNetPath(target, iface);
  if (path.empty()) {
    return false;
  }

  return Write(path, content);
}

std::string System::SysNetGet(SysNet target, std::string_view iface) const {
  const base::FilePath path(SysNetPath(target, iface));
  if (path.empty()) {
    return "";
  }

  std::string content;
  if (!base::ReadFileToString(path, &content)) {
    LOG(ERROR) << "Failed to read the content from " << path;
    return "";
  }

  // The content may contain '\n' character at the end. Remove it.
  content =
      std::string(base::TrimWhitespaceASCII(content, base::TRIM_TRAILING));
  return content;
}

std::string System::SysNetPath(SysNet target, std::string_view iface) const {
  switch (target) {
    case SysNet::kIPv4Forward:
      return kSysNetIPv4ForwardingPath;
    case SysNet::kIPLocalPortRange:
      return kSysNetIPLocalPortRangePath;
    case SysNet::kIPv4RouteLocalnet:
      if (iface.empty()) {
        LOG(ERROR) << "IPv4LocalPortRange requires a valid interface";
        return "";
      }
      return base::StringPrintf(kSysNetIPv4RouteLocalnetPath, iface.data());
    case SysNet::kIPv6Forward:
      return kSysNetIPv6ForwardingPath;
    case SysNet::kIPv6AcceptRA:
      if (iface.empty()) {
        LOG(ERROR) << "IPv6AcceptRA requires a valid interface";
        return "";
      }
      return base::StringPrintf(kSysNetIPv6AcceptRaPath, iface.data());
    case SysNet::kConntrackHelper:
      return kSysNetConntrackHelperPath;
    case SysNet::kIPv6Disable:
      return kSysNetDisableIPv6Path;
    case SysNet::kIPv6ProxyNDP:
      return kSysNetIPv6ProxyNDPPath;
    case SysNet::kIPv6HopLimit:
      if (iface.empty()) {
        LOG(ERROR) << "IPv6HopLimit requires a valid interface";
        return "";
      }
      return base::StringPrintf(kSysNetIPv6HopLimitPath, iface.data());
  }
}

int System::IfNametoindex(const char* ifname) {
  uint32_t ifindex = if_nametoindex(ifname);
  if (ifindex > INT_MAX) {
    errno = EINVAL;
    return 0;
  }
  return static_cast<int>(ifindex);
}

int System::IfNametoindex(std::string_view ifname) {
  return IfNametoindex(ifname.data());
}

char* System::IfIndextoname(int ifindex, char* ifname) {
  if (ifindex < 0) {
    errno = EINVAL;
    return nullptr;
  }
  return if_indextoname(static_cast<uint32_t>(ifindex), ifname);
}

std::string System::IfIndextoname(int ifindex) {
  char ifname[IFNAMSIZ] = {};
  IfIndextoname(ifindex, ifname);
  return ifname;
}

// static
bool System::Write(std::string_view path, std::string_view content) {
  base::ScopedFD fd(open(path.data(), O_WRONLY | O_TRUNC | O_CLOEXEC));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to open " << path;
    return false;
  }

  if (write(fd.get(), content.data(), content.size()) != content.size()) {
    PLOG(ERROR) << "Failed to write \"" << content << "\" to " << path;
    return false;
  }

  return true;
}

}  // namespace patchpanel
