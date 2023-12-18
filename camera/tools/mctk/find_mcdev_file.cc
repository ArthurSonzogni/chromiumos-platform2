/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helper(s) to find a /dev/mediaX device file by more permanent attributes.
 *
 * Unless otherwise noted, there are NO guarantees about the order in which
 * devices are probed, or which device is returned if multiple devices fulfill
 * the search criteria.
 */

#include "tools/mctk/find_mcdev_file.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/media.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "tools/mctk/debug.h"

/* Helper to find a /dev/mediaX device file by its bus_info attribute.
 *
 * For example, given the following device in the system:
 *
 * $ media-ctl -p -d /dev/media0
 * Media controller API version 6.5.6
 *
 * Media device information
 * ------------------------
 * driver          uvcvideo
 * model           Chromebox VP8 Camera: Chromebox
 * serial
 * bus info        usb-0000:04:00.3-1
 * hw revision     0x2105
 * driver version  6.5.6
 *
 * In this case, looking up by bus_info "usb-0000:04:00.3-1" would return the
 * device file path "/dev/media0".
 *
 * This function works by enumerating all /dev/media* files, opening them
 * and querying their bus_info property.
 */
std::optional<std::string> MctkFindMcDevByBusInfo(std::string_view bus_info) {
  for (const auto& entry : std::filesystem::directory_iterator("/dev/")) {
    const std::string path_string = entry.path().string();

    if (!path_string.starts_with("/dev/media"))
      continue;

    if (!entry.is_character_file())
      continue;

    int fd = open(path_string.c_str(), O_RDWR);
    if (fd < 0) {
      MCTK_PERROR("Failed to probe media controller device " + path_string);
      continue;
    }

    struct media_device_info info = {};

    /* Get the device name, etc. */
    int ret = ioctl(fd, MEDIA_IOC_DEVICE_INFO, &info);
    close(fd);

    if (ret < 0) {
      MCTK_PERROR("MEDIA_IOC_DEVICE_INFO on " + path_string);
      continue;
    }

    if (std::string(info.bus_info) == bus_info)
      return path_string;
  }

  return std::nullopt;
}
