/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Factory for an abstract model of a V4L2 media-ctl pad.
 * It will be populated with data from a YAML tree.
 *
 * The YAML tree is no longer needed once this function returns.
 *
 * Returns:
 *  - on success: A pointer to an abstract V4L2 pad.
 *  - on failure: nullptr.
 */

#include "tools/mctk/yaml_tree.h"

#include <linux/media.h>
#include <linux/types.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>

#include <memory>
#include <optional>
#include <utility> /* std::move */
#include <vector>

#include "tools/mctk/debug.h"
#include "tools/mctk/entity.h"
#include "tools/mctk/link.h"
#include "tools/mctk/pad.h"

std::optional<struct v4l2_mbus_framefmt> ParseSubdevFmt(YamlNode& map) {
  struct v4l2_mbus_framefmt fmt = {};
  bool ok = true;

  fmt.width = map["width"].ReadInt<__u32>(ok);
  fmt.height = map["height"].ReadInt<__u32>(ok);
  fmt.code = map["code"].ReadInt<__u32>(ok);
  fmt.field = map["field"].ReadInt<__u32>(ok);
  fmt.colorspace = map["colorspace"].ReadInt<__u32>(ok);
  fmt.ycbcr_enc = map["ycbcr_enc"].ReadInt<__u16>(ok);
  fmt.quantization = map["quantization"].ReadInt<__u16>(ok);
  fmt.xfer_func = map["xfer_func"].ReadInt<__u16>(ok);
#ifdef V4L2_MBUS_FRAMEFMT_SET_CSC
  fmt.flags = map["flags"].ReadInt<__u16>(ok);
#endif /* V4L2_MBUS_FRAMEFMT_SET_CSC */
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_mbus_framefmt>(fmt);
}

std::optional<struct v4l2_fract> ParseFrameInterval(YamlNode& map) {
  struct v4l2_fract fract;
  bool ok = true;

  fract.numerator = map["numerator"].ReadInt<__u32>(ok);
  fract.denominator = map["denominator"].ReadInt<__u32>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_fract>(fract);
}

std::unique_ptr<V4lMcPad> V4lMcPad::CreateFromYamlNode(YamlNode& node_pad,
                                                       V4lMcEntity& entity) {
  auto pad = std::make_unique<V4lMcPad>(entity, std::nullopt);

  /* Parse desc */
  bool ok = true;
  pad->desc_.entity = node_pad["desc"]["entity"].ReadInt<__u32>(ok);
  pad->desc_.index = node_pad["desc"]["index"].ReadInt<__u16>(ok);
  pad->desc_.flags = node_pad["desc"]["flags"].ReadInt<__u32>(ok);
  if (!ok) {
    MCTK_ERR("Pad description doesn't look right, aborting.");
    return nullptr;
  }

  /* Parse subdev properties */
  pad->subdev_.crop = node_pad["subdev_properties"]["crop"].ReadRect();
  pad->subdev_.fmt = ParseSubdevFmt(node_pad["subdev_properties"]["fmt"]);
  pad->subdev_.frame_interval =
      ParseFrameInterval(node_pad["subdev_properties"]["frame_interval"]);
  node_pad["subdev_properties"]["selection"].ReadSelection(
      pad->subdev_.selection);

  /* Parse links */
  for (YamlNode* node_link : node_pad["links"].ReadSequence()) {
    std::unique_ptr<V4lMcLink> new_link =
        V4lMcLink::CreateFromYamlNode(*node_link, *pad);

    if (!new_link) {
      MCTK_ERR("Failed to create link from YAML node.");
      return nullptr;
    }

    /* NOTE:
     * Links belong:
     *  - to the pad in YAML,
     *  - to the entity in the model (like in the kernel's V4L API).
     */
    entity.links_.push_back(std::move(new_link));
  }

  return pad;
}
