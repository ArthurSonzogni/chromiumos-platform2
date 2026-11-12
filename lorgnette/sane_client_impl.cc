// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_impl.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/sock_diag.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <cstring>
#include <optional>

#include <base/check.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>
#include <re2/re2.h>
#include <sane/saneopts.h>
#include <sane-airscan/airscan.h>

#include "lorgnette/dbus_adaptors/org.chromium.lorgnette.Manager.h"
#include "lorgnette/guess_source.h"
#include "lorgnette/sane_device_impl.h"
#include "lorgnette/scanner_match.h"

static const char* kDbusDomain = brillo::errors::dbus::kDomain;

namespace lorgnette {

namespace {

// Uses Netlink (NETLINK_SOCK_DIAG) to query the kernel for the TCP listener's
// owner UID directly, safely verifying the listener's identity without needing
// to connect to the socket.
bool IsRootOwnedLocalhostListener(uint16_t port) {
  base::ScopedFD fd(
      socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_SOCK_DIAG));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to open NETLINK_SOCK_DIAG socket";
    return false;
  }

  for (uint8_t family : {AF_INET, AF_INET6}) {
    struct {
      struct nlmsghdr nlh;
      struct inet_diag_req_v2 req;
    } request = {};
    request.nlh.nlmsg_len = sizeof(request);
    request.nlh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    request.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.req.sdiag_family = family;
    request.req.sdiag_protocol = IPPROTO_TCP;
    request.req.idiag_states = 1 << TCP_LISTEN;

    if (send(fd.get(), &request, sizeof(request), 0) < 0) {
      continue;
    }

    alignas(struct nlmsghdr) uint8_t buf[8192];
    bool done = false;
    while (!done) {
      ssize_t len = HANDLE_EINTR(recv(fd.get(), buf, sizeof(buf), 0));
      if (len <= 0) {
        break;
      }
      for (struct nlmsghdr* nlh = reinterpret_cast<struct nlmsghdr*>(buf);
           NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
        if (nlh->nlmsg_type == NLMSG_DONE || nlh->nlmsg_type == NLMSG_ERROR) {
          done = true;
          break;
        }
        if (nlh->nlmsg_type == SOCK_DIAG_BY_FAMILY &&
            nlh->nlmsg_len >= NLMSG_LENGTH(sizeof(struct inet_diag_msg))) {
          const auto* diag =
              reinterpret_cast<const struct inet_diag_msg*>(NLMSG_DATA(nlh));
          if (diag->id.idiag_sport == htons(port) && diag->idiag_uid == 0) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

// Returns true if `target` matches an IPv4 address assigned to any local
// network interface on this host. Used to prevent SSRF against local services
// bound to the host's non-loopback interface IP addresses.
bool IsLocalInterfaceIpv4(const struct in_addr& target) {
  struct ifaddrs* ifaddr = nullptr;
  if (getifaddrs(&ifaddr) == -1) {
    PLOG(ERROR) << "getifaddrs failed";
    return false;
  }
  bool found = false;
  for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    const auto* sin =
        reinterpret_cast<const struct sockaddr_in*>(ifa->ifa_addr);
    if (sin->sin_addr.s_addr == target.s_addr) {
      found = true;
      break;
    }
  }
  freeifaddrs(ifaddr);
  return found;
}

// Returns true if `target` matches an IPv6 address assigned to any local
// network interface on this host. Used to prevent SSRF against local services
// bound to the host's non-loopback interface IPv6 addresses.
bool IsLocalInterfaceIpv6(const struct in6_addr& target) {
  struct ifaddrs* ifaddr = nullptr;
  if (getifaddrs(&ifaddr) == -1) {
    PLOG(ERROR) << "getifaddrs failed";
    return false;
  }
  bool found = false;
  for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET6) {
      continue;
    }
    const auto* sin6 =
        reinterpret_cast<const struct sockaddr_in6*>(ifa->ifa_addr);
    if (memcmp(&sin6->sin6_addr, &target, sizeof(struct in6_addr)) == 0) {
      found = true;
      break;
    }
  }
  freeifaddrs(ifaddr);
  return found;
}

// Validates that `addr4` is a remote unicast IPv4 address suitable for a
// network scanner connection. Rejects unspecified (0.0.0.0/8), loopback
// (127.0.0.0/8), multicast (224.0.0.0/4), broadcast (255.255.255.255), and any
// IPv4 address assigned to a local interface on this host.
bool IsValidRemoteIpv4(const struct in_addr& addr4) {
  uint32_t ip = ntohl(addr4.s_addr);
  // Reject 0.0.0.0/8 ("this" network / unspecified) and 127.0.0.0/8 (loopback).
  if ((ip >> 24) == 0 || (ip >> 24) == IN_LOOPBACKNET) {
    return false;
  }
  // Reject multicast (224.0.0.0/4) and broadcast (255.255.255.255).
  if (IN_MULTICAST(ip) || ip == INADDR_BROADCAST) {
    return false;
  }
  return !IsLocalInterfaceIpv4(addr4);
}

// Verifies that `host` is a literal IPv4 or IPv6 address (rejecting DNS
// hostnames that could resolve to localhost) pointing to a remote device.
// Handles bracketed IPv6 literals, optional IPv6 zone IDs (e.g. "%eth0"), and
// IPv4-mapped/compatible IPv6 addresses (e.g. "::ffff:127.0.0.1").
bool IsValidRemoteIpAddress(std::string host) {
  if (host.empty()) {
    return false;
  }
  if (host.front() == '[' && host.back() == ']' && host.size() > 2) {
    host = host.substr(1, host.size() - 2);
  }

  struct in_addr addr4;
  if (inet_pton(AF_INET, host.c_str(), &addr4) == 1) {
    return IsValidRemoteIpv4(addr4);
  }

  // Strip optional IPv6 zone ID (e.g. "%eth0" or "%25eth0").
  std::string ipv6_str = host;
  size_t zone_pos = ipv6_str.find('%');
  if (zone_pos != std::string::npos) {
    if (zone_pos + 1 >= ipv6_str.size()) {
      return false;
    }
    ipv6_str = ipv6_str.substr(0, zone_pos);
  }

  struct in6_addr addr6;
  if (inet_pton(AF_INET6, ipv6_str.c_str(), &addr6) == 1) {
    if (IN6_IS_ADDR_UNSPECIFIED(&addr6) || IN6_IS_ADDR_LOOPBACK(&addr6) ||
        IN6_IS_ADDR_MULTICAST(&addr6)) {
      return false;
    }
    if (IN6_IS_ADDR_V4MAPPED(&addr6) || IN6_IS_ADDR_V4COMPAT(&addr6)) {
      struct in_addr mapped4;
      memcpy(&mapped4.s_addr, &addr6.s6_addr[12], sizeof(mapped4.s_addr));
      return IsValidRemoteIpv4(mapped4);
    }
    return !IsLocalInterfaceIpv6(addr6);
  }

  return false;
}

// Returns true if `port_str` is a valid decimal TCP port number in [1, 65535].
bool IsValidPort(const std::string& port_str) {
  if (port_str.empty() || port_str.size() > 5) {
    return false;
  }
  for (char c : port_str) {
    if (!base::IsAsciiDigit(c)) {
      return false;
    }
  }
  unsigned int port = 0;
  if (!base::StringToUint(port_str, &port)) {
    return false;
  }
  return port >= 1 && port <= 65535;
}

// Validates the `<host>[:<port>]` or `[<ipv6>][:<port>]` authority component of
// an `airscan:` HTTP/HTTPS scanner URL, ensuring the port (if present) is valid
// and the host is a valid remote IP literal.
bool IsValidRemoteHttpAuthority(const std::string& authority) {
  if (authority.empty()) {
    return false;
  }
  std::string host;
  if (authority.front() == '[') {
    size_t close_bracket = authority.find(']');
    if (close_bracket == std::string::npos || close_bracket <= 1) {
      return false;
    }
    host = authority.substr(1, close_bracket - 1);
    if (close_bracket + 1 < authority.size()) {
      if (authority[close_bracket + 1] != ':') {
        return false;
      }
      if (!IsValidPort(authority.substr(close_bracket + 2))) {
        return false;
      }
    }
  } else {
    size_t colon = authority.find(':');
    if (colon != std::string::npos) {
      host = authority.substr(0, colon);
      if (!IsValidPort(authority.substr(colon + 1))) {
        return false;
      }
    } else {
      host = authority;
    }
  }
  return IsValidRemoteIpAddress(host);
}

}  // namespace

// static
std::unique_ptr<SaneClientImpl> SaneClientImpl::Create(
    LibsaneWrapper* libsane) {
  SANE_Status status = libsane->sane_init(nullptr, nullptr);
  if (status != SANE_STATUS_GOOD) {
    LOG(ERROR) << "Unable to initialize SANE";
    return nullptr;
  }

  // Cannot use make_unique() with a private constructor.
  return std::unique_ptr<SaneClientImpl>(new SaneClientImpl(libsane));
}

SaneClientImpl::~SaneClientImpl() {
  libsane_->sane_exit();
}

std::optional<std::vector<ScannerInfo>> SaneClientImpl::ListDevices(
    brillo::ErrorPtr* error, bool local_only) {
  base::AutoLock auto_lock(lock_);

  // Some SANE backends free and rebuild internal state during
  // sane_get_devices(), which can corrupt open SANE_Handles from the same
  // backend.  open_devices_ tracks every handle this SaneClient has handed
  // out (across DeviceTracker::open_scanners_ *and* Manager::active_scans_),
  // so it is the authoritative place to gate enumeration.
  {
    base::AutoLock devices_lock(open_devices_->first);
    if (!open_devices_->second.empty()) {
      LOG(WARNING) << __func__ << ": skipping sane_get_devices() while "
                   << open_devices_->second.size() << " device(s) are open";
      return std::vector<ScannerInfo>();
    }
  }

  const SANE_Device** device_list;
  SANE_Status status = libsane_->sane_get_devices(
      &device_list, local_only ? SANE_TRUE : SANE_FALSE);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Unable to get device list from SANE");
    return std::nullopt;
  }

  std::optional<std::vector<ScannerInfo>> scanners =
      DeviceListToScannerInfo(device_list);
  if (scanners.has_value()) {
    base::AutoLock known_lock(known_devices_lock_);
    // Note: `known_devices_` is not cleared on each call because a
    // `local_only=true` enumeration would otherwise drop previously discovered
    // network scanners, and lorgnette exits after a short idle timeout.
    for (const ScannerInfo& scanner : scanners.value()) {
      known_devices_.insert(scanner.name());
      // DeviceTracker::TryEpsondsInstead() probes "epson2:..." network
      // scanners by replacing the "epson2:" prefix with "epsonds:" to check if
      // the scanner requires the epsonds backend.
      if (base::StartsWith(scanner.name(), "epson2:")) {
        known_devices_.insert("epsonds:" + scanner.name().substr(7));
      }
    }
  }
  return scanners;
}

// static
std::optional<std::vector<ScannerInfo>> SaneClientImpl::DeviceListToScannerInfo(
    const SANE_Device** device_list) {
  if (!device_list) {
    LOG(ERROR) << "'device_list' cannot be NULL";
    return std::nullopt;
  }

  std::unordered_set<std::string> names;
  std::vector<ScannerInfo> scanners;
  for (int i = 0; device_list[i]; i++) {
    const SANE_Device* dev = device_list[i];
    if (!dev->name || strcmp(dev->name, "") == 0) {
      continue;
    }

    if (names.count(dev->name) != 0) {
      LOG(ERROR) << "Duplicate device name: " << dev->name;
      return std::nullopt;
    }
    names.insert(dev->name);

    ScannerInfo info;
    info.set_name(dev->name);
    info.set_manufacturer(dev->vendor ? dev->vendor : "");
    info.set_model(dev->model ? dev->model : "");
    info.set_type(dev->type ? dev->type : "");
    info.set_connection_type(ConnectionTypeForScanner(info));
    info.set_secure(info.connection_type() == lorgnette::CONNECTION_USB);
    info.set_protocol_type(ProtocolTypeForScanner(info));
    info.set_display_name(DisplayNameForScanner(info));
    scanners.push_back(info);
  }
  return scanners;
}

SaneClientImpl::SaneClientImpl(LibsaneWrapper* libsane)
    : libsane_(libsane), open_devices_(std::make_shared<DeviceSet>()) {}

// Validates `device_name` before passing it to `sane_open()` to prevent SSRF
// and confused-deputy attacks via arbitrary network/socket SANE strings while
// supporting scanners discovered outside `sane_get_devices()` and across
// lorgnette's 2-second idle shutdown timeout.
bool SaneClientImpl::IsDeviceKnown(const std::string& device_name) {
  // Allow SANE's built-in virtual test backend.
  if (device_name == "test" || base::StartsWith(device_name, "test:")) {
    return true;
  }

  if (base::StartsWith(device_name, "airscan:")) {
    // Rewritten IPP-USB devices (airscan:...:unix://<vid>-<pid>.sock/...) were
    // already verified by BackendForDevice() in SaneClient::ConnectToDevice(),
    // which rejects direct caller-supplied `airscan:...:unix://` strings.
    if (base::ToLowerASCII(device_name).find(":unix://") != std::string::npos) {
      return true;
    }
    // Tast virtual network scanner (ippusb_bridge -p 60000) runs as root (uid
    // 0).
    if (device_name == "airscan:escl:TestScanner:http://localhost:60000/eSCL" &&
        IsRootOwnedLocalhostListener(60000)) {
      return true;
    }
    // Network AirScan scanners are discovered via mDNS in Chrome
    // (ZeroconfScannerDetector) or airscan-discover in lorgnette_cli rather
    // than sane_get_devices() (since /etc/sane.d/airscan.conf sets
    // `discovery = disable`). Allow them if they target a valid remote IP.
    std::string authority;
    if (RE2::FullMatch(device_name,
                       "airscan:(?:escl|wsd):[^:]+:https?://([^/?#]+)(?:/.*)?",
                       &authority)) {
      return IsValidRemoteHttpAuthority(authority);
    }
    return false;
  }

  // Epson network scanners may be discovered via epson_probe outside
  // sane_get_devices() or opened after lorgnette's idle shutdown timeout.
  // Allow them if they target a valid remote IP address without a custom port.
  if (base::StartsWith(device_name, "epson2:net:")) {
    return IsValidRemoteIpAddress(device_name.substr(11));
  }
  if (base::StartsWith(device_name, "epsonds:net:")) {
    return IsValidRemoteIpAddress(device_name.substr(12));
  }

  // Allow any other network scanner backend (e.g. kodakaio, pixma, xerox_mfp)
  // that was explicitly enumerated by `sane_get_devices()` during ListDevices()
  // in the current daemon lifetime.
  {
    base::AutoLock auto_lock(known_devices_lock_);
    if (known_devices_.count(device_name) > 0) {
      return true;
    }
  }

  // Reject any other network or URI-based SANE device strings that were not
  // discovered by ListDevices().
  std::string lower_name = base::ToLowerASCII(device_name);
  if (base::StartsWith(lower_name, "net:") ||
      base::StartsWith(lower_name, "escl:") ||
      base::StartsWith(lower_name, "dell1600n_net:") ||
      base::StartsWith(lower_name, "magicolor:") ||
      base::StartsWith(lower_name, "hpaio:") ||
      lower_name.find("://") != std::string::npos ||
      lower_name.find(":net:") != std::string::npos ||
      lower_name.find(":tcp") != std::string::npos) {
    return false;
  }

  // Allow local USB SANE backends (e.g. pixma, fujitsu, genesys, epson2:libusb)
  // across lorgnette idle restarts without requiring a prior ListDevices() call.
  return true;
}

std::unique_ptr<SaneDevice> SaneClientImpl::ConnectToDeviceInternal(
    brillo::ErrorPtr* error,
    SANE_Status* sane_status,
    const std::string& device_name) {
  if (!IsDeviceKnown(device_name)) {
    LOG(ERROR) << "Refusing to open undiscovered SANE device: " << device_name;
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "Device '%s' was not found among discovered scanners",
        device_name.c_str());
    if (sane_status) {
      *sane_status = SANE_STATUS_INVAL;
    }
    return nullptr;
  }

  LOG(INFO) << "Creating connection to device: " << device_name;
  base::AutoLock auto_lock(lock_);
  SANE_Handle handle;
  {
    base::AutoLock auto_lock(open_devices_->first);
    if (open_devices_->second.count(device_name) != 0) {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, kDbusDomain, kManagerServiceError,
          "Device '%s' is currently in-use", device_name.c_str());

      if (sane_status) {
        *sane_status = SANE_STATUS_DEVICE_BUSY;
      }
      return nullptr;
    }

    SANE_Status status = libsane_->sane_open(device_name.c_str(), &handle);
    if (sane_status) {
      *sane_status = status;
    }

    if (status != SANE_STATUS_GOOD) {
      brillo::Error::AddToPrintf(error, FROM_HERE, kDbusDomain,
                                 kManagerServiceError,
                                 "Unable to open device '%s': %s",
                                 device_name.c_str(), sane_strstatus(status));

      return nullptr;
    }

    open_devices_->second.insert(device_name);
  }

  // Cannot use make_unique() with a private constructor.
  auto device = std::unique_ptr<SaneDeviceImpl>(
      new SaneDeviceImpl(libsane_, handle, device_name, open_devices_));
  device->LoadOptions(error);
  return device;
}

}  // namespace lorgnette
