/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helper(s) to find a /dev/mediaX device file by more permanent attributes.
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
 * Unless otherwise noted, there are NO guarantees about the order in which
 * devices are probed, or which device is returned if multiple devices fulfill
 * the search criteria.
 */

#ifndef CAMERA_TOOLS_MCTK_FIND_MCDEV_FILE_H_
#define CAMERA_TOOLS_MCTK_FIND_MCDEV_FILE_H_

#include <optional>
#include <string>
#include <string_view>

std::optional<std::string> MctkFindMcDevByBusInfo(std::string_view bus_info);

#endif /* CAMERA_TOOLS_MCTK_FIND_MCDEV_FILE_H_ */
