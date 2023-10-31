/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_TOOLS_MCTK_CONTROL_HELPERS_H_
#define CAMERA_TOOLS_MCTK_CONTROL_HELPERS_H_

#include <linux/media.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <stddef.h> /* size_t */

size_t ControlHelperElemSize(__u32 type_u32);
bool ControlHelperDescLooksOK(struct v4l2_query_ext_ctrl& desc);

#endif /* CAMERA_TOOLS_MCTK_CONTROL_HELPERS_H_ */
