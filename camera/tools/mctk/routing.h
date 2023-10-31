/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NOTE: This code merely serves as an example usage of the class hierarchy,
 *       and has not been optimised for style or quality.
 *
 * For each sensor on a media controller, attempt to find and configure
 * a route to a /dev/videoX device using a depth-first search.
 *
 * This assumes that any free links can be used equally well, and hence
 * works best on homogeneous devices like IPU6.
 *
 * This is a remnant of the v1 tool:
 * https://chromium-review.googlesource.com/c/chromiumos/platform2/+/4055245
 */

#ifndef CAMERA_TOOLS_MCTK_ROUTING_H_
#define CAMERA_TOOLS_MCTK_ROUTING_H_

#include "tools/mctk/mcdev.h"

void V4lMcRouteSensors(V4lMcDev& mcdev);

#endif /* CAMERA_TOOLS_MCTK_ROUTING_H_ */
