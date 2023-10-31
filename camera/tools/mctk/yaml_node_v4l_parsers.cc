/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* V4L specific struct parsers.
 *
 * Returns:
 *  - on success: The parsed struct, inside std::optional.
 *  - on failure: std::nullopt.
 */

#include "tools/mctk/yaml_tree.h"

#include <linux/types.h>
#include <linux/videodev2.h>
#include <yaml.h>

#include <optional>
#include <string>

#include "tools/mctk/debug.h"
#include "tools/mctk/selection.h"

std::optional<struct v4l2_rect> YamlNode::ReadRect() {
  struct v4l2_rect r;
  bool ok = true;

  r.left = (*this)["left"].ReadInt<__s32>(ok);
  r.top = (*this)["top"].ReadInt<__s32>(ok);
  r.width = (*this)["width"].ReadInt<__u32>(ok);
  r.height = (*this)["height"].ReadInt<__u32>(ok);

  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_rect>(r);
}

void YamlNode::ReadSelection(V4lMcSelection& dest) {
  dest.crop_ = (*this)["crop"].ReadRect();
  dest.crop_default_ = (*this)["crop_default"].ReadRect();
  dest.crop_bounds_ = (*this)["crop_bounds"].ReadRect();
  dest.native_size_ = (*this)["native_size"].ReadRect();

  dest.compose_ = (*this)["compose"].ReadRect();
  dest.compose_default_ = (*this)["compose_default"].ReadRect();
  dest.compose_bounds_ = (*this)["compose_bounds"].ReadRect();
  dest.compose_padded_ = (*this)["compose_padded"].ReadRect();
}
