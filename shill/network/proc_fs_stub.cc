// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/proc_fs_stub.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_piece.h>
#include <base/strings/stringprintf.h>

namespace shill {

namespace {
constexpr char kIPFlagTemplate[] = "/proc/sys/net/%s/conf/%s/%s";
constexpr char kIPFlagVersion4[] = "ipv4";
constexpr char kIPFlagVersion6[] = "ipv6";
constexpr std::array<base::StringPiece, 2> kRouteFlushPaths = {
    "/proc/sys/net/ipv4/route/flush",
    "/proc/sys/net/ipv6/route/flush",
};
}  // namespace

ProcFsStub::ProcFsStub(const std::string& interface_name)
    : interface_name_(interface_name) {}

bool ProcFsStub::SetIPFlag(net_base::IPFamily family,
                           const std::string& flag,
                           const std::string& value) {
  const std::string ip_version =
      (family == net_base::IPFamily::kIPv4) ? kIPFlagVersion4 : kIPFlagVersion6;
  base::FilePath flag_file(
      base::StringPrintf(kIPFlagTemplate, ip_version.c_str(),
                         interface_name_.c_str(), flag.c_str()));
  if (!base::PathExists(flag_file.DirName())) {
    // If the directory containing the flag file does not exist it means the
    // interface is already removed. Returning silently without an ERROR log.
    return false;
  }
  if (base::WriteFile(flag_file, value.c_str(), value.length()) !=
      static_cast<int>(value.length())) {
    LOG(ERROR) << "IP flag write failed: " << value << " to "
               << flag_file.value();
    return false;
  }

  return true;
}

bool ProcFsStub::FlushRoutingCache() {
  bool ret = true;
  for (auto path : kRouteFlushPaths) {
    if (!base::WriteFile(base::FilePath(path), "-1")) {
      PLOG(ERROR) << "Cannot write to " << path;
      ret = false;
    }
  }

  return ret;
}
}  // namespace shill
