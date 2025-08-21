// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_dlc_platform_info_impl.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <string_view>

#include <base/byte_count.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <rootdev/rootdev.h>

namespace login_manager {

std::optional<std::string> ArcDlcPlatformInfoImpl::GetRootDeviceName() {
  char dev_path_cstr[4096];

  // Get physical root device without partition
  int ret = rootdev(dev_path_cstr, sizeof(dev_path_cstr),
                    true /* resolve to physical */, true /* strip partition */);
  if (ret != 0) {
    LOG(ERROR) << "Failed to retrieve root device";
    return std::nullopt;
  }

  std::string_view dev_path_view(dev_path_cstr);
  constexpr char kDevPrefix[] = "/dev/";

  if (!dev_path_view.starts_with(kDevPrefix)) {
    LOG(ERROR) << "Unexpected root device format " << dev_path_view;
    return std::nullopt;
  }

  // Added a check to ensure the remaining part of the path is valid.
  std::string_view name_part =
      dev_path_view.substr(std::string_view(kDevPrefix).length());
  if (name_part.find('/') != std::string_view::npos) {
    LOG(ERROR) << "Root device name should not contain '/'";
    return std::nullopt;
  }

  return std::string(name_part);
}

std::optional<base::ByteCount> ArcDlcPlatformInfoImpl::GetDeviceSize(
    const base::FilePath& dev_path) {
  base::ScopedFD fd(HANDLE_EINTR(
      open(dev_path.value().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)));
  if (!fd.is_valid()) {
    LOG(ERROR) << "Failed to open " << dev_path.value();
    return std::nullopt;
  }

  uint64_t size;
  auto ret = ioctl(fd.get(), BLKGETSIZE64, &size);
  if (ret != 0) {
    LOG(ERROR) << "Failed to query size of " << dev_path.value();
    return std::nullopt;
  }

  return base::ByteCount(size);
}
}  // namespace login_manager
