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

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/files/file_util.h>

#include "patchpanel/bpf/constants.h"

namespace patchpanel {

namespace {

// /proc/sys/ paths used for System::SysNetSet() and System::SysNetGet().
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
// Controls the default TTL value for IPv4.
constexpr char kSysNetIPv4DefaultTTL[] = "/proc/sys/net/ipv4/ip_default_ttl";

constexpr char kTunDev[] = "/dev/net/tun";

class ScopedNSImpl : public System::ScopedNS {
 public:
  ScopedNSImpl(int nstype,
               const std::string& current_ns_path,
               const std::string& target_ns_path);
  ~ScopedNSImpl() override;

  ScopedNSImpl(const ScopedNSImpl&) = delete;
  ScopedNSImpl& operator=(const ScopedNSImpl&) = delete;

  bool valid() const { return valid_; }

 private:
  int nstype_;
  bool valid_;
  base::ScopedFD ns_fd_;
  base::ScopedFD self_fd_;
};

ScopedNSImpl::ScopedNSImpl(int nstype,
                           const std::string& current_ns_path,
                           const std::string& target_ns_path)
    : nstype_(nstype), valid_(false) {
  ns_fd_.reset(open(target_ns_path.c_str(), O_RDONLY | O_CLOEXEC));
  if (!ns_fd_.is_valid()) {
    PLOG(ERROR) << "Could not open namespace " << target_ns_path;
    return;
  }
  self_fd_.reset(open(current_ns_path.c_str(), O_RDONLY | O_CLOEXEC));
  if (!self_fd_.is_valid()) {
    PLOG(ERROR) << "Could not open host namespace " << current_ns_path;
    return;
  }
  if (setns(ns_fd_.get(), nstype_) != 0) {
    PLOG(ERROR) << "Could not enter namespace " << target_ns_path;
    return;
  }
  valid_ = true;
}

ScopedNSImpl::~ScopedNSImpl() {
  if (valid_) {
    if (setns(self_fd_.get(), nstype_) != 0) {
      PLOG(FATAL) << "Could not re-enter host namespace type " << nstype_;
    }
  }
}

}  // namespace

System::ScopedNS::~ScopedNS() = default;

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

int System::Chown(const char* pathname, uid_t owner, gid_t group) {
  return chown(pathname, owner, group);
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
    case SysNet::kIPv4RouteLocalnet:
      if (iface.empty()) {
        LOG(ERROR) << "IPv4RouteLocalnet requires a valid interface";
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
    case SysNet::kIPv4DefaultTTL:
      return kSysNetIPv4DefaultTTL;
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

bool System::WriteConfigFile(base::FilePath path, std::string_view contents) {
  if (!base::WriteFile(path, contents)) {
    PLOG(ERROR) << "Failed to write config file to " << path;
    return false;
  }

  if (chmod(path.value().c_str(), S_IRUSR | S_IRGRP | S_IWUSR)) {
    PLOG(ERROR) << "Failed to set permissions on " << path;
    brillo::DeletePathRecursively(path);
    return false;
  }

  if (chown(path.value().c_str(), kPatchpaneldUid, kPatchpaneldGid) != 0) {
    PLOG(ERROR) << "Failed to change owner group of " << path;
    brillo::DeletePathRecursively(path);
    return false;
  }
  return true;
}

bool System::IsEbpfEnabled() const {
  return base::PathExists(base::FilePath(kBPFPath));
}

std::unique_ptr<System::ScopedNS> System::EnterMountNS(pid_t pid) {
  int nstype = CLONE_NEWNS;
  const std::string current_path = "/proc/self/ns/mnt";
  const std::string target_path = "/proc/" + std::to_string(pid) + "/ns/mnt";
  auto ns =
      base::WrapUnique(new ScopedNSImpl(nstype, current_path, target_path));
  return ns->valid() ? std::move(ns) : nullptr;
}

std::unique_ptr<System::ScopedNS> System::EnterNetworkNS(pid_t pid) {
  int nstype = CLONE_NEWNET;
  const std::string current_path = "/proc/self/ns/net";
  const std::string target_path = "/proc/" + std::to_string(pid) + "/ns/net";
  auto ns =
      base::WrapUnique(new ScopedNSImpl(nstype, current_path, target_path));
  return ns->valid() ? std::move(ns) : nullptr;
}

std::unique_ptr<System::ScopedNS> System::EnterNetworkNS(
    std::string_view netns_name) {
  int nstype = CLONE_NEWNET;
  const std::string current_path = "/proc/self/ns/net";
  const std::string target_path = base::StrCat({"/run/netns/", netns_name});
  auto ns =
      base::WrapUnique(new ScopedNSImpl(nstype, current_path, target_path));
  return ns->valid() ? std::move(ns) : nullptr;
}

}  // namespace patchpanel
