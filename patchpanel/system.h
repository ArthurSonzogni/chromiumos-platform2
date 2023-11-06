// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_SYSTEM_H_
#define PATCHPANEL_SYSTEM_H_

#include <net/if.h>
#include <net/route.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <string>
#include <string_view>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>

namespace patchpanel {

// cros lint will yell to force using int16/int64 instead of long here, however
// note that unsigned long IS the correct signature for ioctl in Linux kernel -
// it's 32 bits on 32-bit platform and 64 bits on 64-bit one.
using ioctl_req_t = unsigned long;  // NOLINT(runtime/int)

// User/Group ID of patchpaneld.
constexpr uid_t kPatchpaneldUid = 284;
constexpr gid_t kPatchpaneldGid = 284;
constexpr char kPatchpaneldUser[] = "patchpaneld";
constexpr char kPatchpaneldGroup[] = "patchpaneld";

// Class used for:
//  - holding all utility functions with side effects on the environment.
//  - wrapping commonly used system calls.
// This class facilitates mocking these functions in unit tests.
class System {
 public:
  // Enum used for restricting the possible paths that SysNetSet can write to.
  enum class SysNet {
    // Used for modifying "net.ipv4.ip_forward"
    kIPv4Forward = 1,
    // Used for modifying "net.ipv4.ip_local_port_range"
    kIPLocalPortRange,
    // Used for modifying "net.ipv4.conf.%s.route_localnet", requires an
    // interface
    // argument
    kIPv4RouteLocalnet,
    // Used for modifying "net.ipv6.conf.%s.accept_ra", requires an interface
    // argument
    kIPv6AcceptRA,
    // Used for modifying "net.ipv6.conf.all.forwarding"
    kIPv6Forward,
    // Used for enabling netfilter connection tracking helper modules.
    kConntrackHelper,
    // Used for modifying "net.ipv6.conf.all.disable_ipv6"
    kIPv6Disable,
    // Used for modifying "net.ipv6.conf.all.proxy_ndp"
    kIPv6ProxyNDP,
    // Used for modifying "net.ipv6.conf.%s.hop_limit", requires an interface
    // argument
    kIPv6HopLimit,
  };

  System() = default;
  System(const System&) = delete;
  System& operator=(const System&) = delete;
  virtual ~System() = default;

  virtual base::ScopedFD OpenTunDev();

  // Writes |content| to a "/proc/sys/net/" path as specified by |target|.
  virtual bool SysNetSet(SysNet target,
                         std::string_view content,
                         std::string_view iface = {});

  // Reads the content from the "/proc/sys/net/" path as specified by |target|.
  virtual std::string SysNetGet(SysNet target,
                                std::string_view iface = {}) const;

  // Returns the "/proc/sys/net/" path as specified by |target|,
  std::string SysNetPath(SysNet target, std::string_view iface = {}) const;

  virtual int Ioctl(int fd, ioctl_req_t request, const char* argp);
  virtual int Ioctl(int fd, ioctl_req_t request, uint64_t arg);
  virtual int Ioctl(int fd, ioctl_req_t request, struct ifreq* ifr);
  virtual int Ioctl(int fd, ioctl_req_t request, struct rtentry* route);
  virtual int Ioctl(int fd, ioctl_req_t request, struct in6_rtmsg* route);

  virtual int SocketPair(int domain, int type, int protocol, int sv[2]);

  virtual pid_t WaitPid(pid_t pid, int* wstatus, int options = 0);

  // Simple wrappers around if_nametoindex which returns a signed int instead
  // of an unsigned int to avoid static casts.
  virtual int IfNametoindex(const char* ifname);

  // Overload that takes a constant reference to a c++ string.
  virtual int IfNametoindex(std::string_view ifname);

  // Simple wrapper around if_indextoname which takes as an argument a signed
  // int instead of an unsigned int to avoid static casts.
  virtual char* IfIndextoname(int ifindex, char* ifname);

  // Overload that directly returns a c++ string. Returns an empty string if an
  // error happens.
  virtual std::string IfIndextoname(int ifindex);

  static bool Write(std::string_view path, std::string_view content);

  // Writes |contents| into |path| and changes the owner and group to
  // `patchpaneld:patchpaneld`. Returns true if all the steps succeed.
  virtual bool WriteConfigFile(base::FilePath path, std::string_view contents);

  // Returns if the eBPF is enabled on the system.
  virtual bool IsEbpfEnabled() const;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_SYSTEM_H_
