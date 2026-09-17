#include <base/posix/eintr_wrapper.h>
// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_impl.h"

#include <arpa/inet.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/sock_diag.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <optional>

#include <base/check.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
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

bool SaneClientImpl::IsDeviceKnown(const std::string& device_name) {
  if (device_name == "test" || base::StartsWith(device_name, "test:")) {
    return true;
  }
  // Rewritten IPP-USB devices (airscan:...:unix://<vid>-<pid>.sock/...) were
  // already verified by BackendForDevice() in SaneClient::ConnectToDevice().
  if (base::StartsWith(device_name, "airscan:") &&
      base::ToLowerASCII(device_name).find(":unix://") != std::string::npos) {
    return true;
  }
  // Tast virtual network scanner (ippusb_bridge -p 60000) runs as root (uid 0).
  if (device_name == "airscan:escl:TestScanner:http://localhost:60000/eSCL" &&
      IsRootOwnedLocalhostListener(60000)) {
    return true;
  }
  base::AutoLock auto_lock(known_devices_lock_);
  return known_devices_.count(device_name) > 0;
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
