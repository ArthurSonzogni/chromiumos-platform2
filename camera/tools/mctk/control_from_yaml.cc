/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Factory for an abstract model of a V4L2 control.
 * It will be populated with data from a YAML tree.
 *
 * The YAML tree is no longer needed once this function returns.
 *
 * Returns:
 *  - on success: A pointer to an abstract V4L2 control.
 *  - on failure: nullptr.
 */

#include "tools/mctk/control.h"

#include <linux/media.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>

#include <memory>
#include <optional>
#include <string>

#include "tools/mctk/control_helpers.h"
#include "tools/mctk/debug.h"
#include "tools/mctk/yaml_tree.h"

namespace {

bool ParsePayloadValue(V4lMcControl& control, YamlNode& node) {
  /* v4l2_ctrl_new() should ensure this - see linux/.../v4l2-ctrls-core.c */
  if (control.desc_.type != V4L2_CTRL_TYPE_STRING &&
      control.desc_.elem_size != ControlHelperElemSize(control.desc_.type)) {
    MCTK_ERR("Payload element size does not match type.");
    return false;
  }

  switch (control.desc_.type) {
    case V4L2_CTRL_TYPE_INTEGER:
      /* fall-through */
    case V4L2_CTRL_TYPE_BOOLEAN:
      /* fall-through */
    case V4L2_CTRL_TYPE_MENU:
      /* fall-through */
    case V4L2_CTRL_TYPE_BUTTON:
    case V4L2_CTRL_TYPE_BITMASK:
    case V4L2_CTRL_TYPE_INTEGER_MENU: {
      std::optional<__s32> tmp = node.Read<__s32>();
      if (!tmp)
        return false;
      control.values_s32_.push_back(*tmp);
      return true;
    }
    case V4L2_CTRL_TYPE_INTEGER64: {
      std::optional<__s64> tmp = node.Read<__s64>();
      if (!tmp)
        return false;
      control.values_s64_.push_back(*tmp);
      return true;
    }
    case V4L2_CTRL_TYPE_CTRL_CLASS:
      /* This should never happen:
       * We process controls, not control classes.
       */
      MCTK_ASSERT(0);
      return false;
    case V4L2_CTRL_TYPE_STRING: {
      /* Read<std::string>() always returns a value */
      std::string str = *(node.Read<std::string>());

      if (str.length() > (__u64)control.desc_.maximum) {
        MCTK_ERR("Value for string control longer than elem_size");
        return false;
      }

      control.values_string_.push_back(str);
      return true;
    }
    case V4L2_CTRL_TYPE_U8: {
      std::optional<__u8> tmp = node.Read<__u8>();
      if (!tmp)
        return false;
      control.values_u8_.push_back(*tmp);
      return true;
    }
    case V4L2_CTRL_TYPE_U16: {
      std::optional<__u16> tmp = node.Read<__u16>();
      if (!tmp)
        return false;
      control.values_u16_.push_back(*tmp);
      return true;
    }
    case V4L2_CTRL_TYPE_U32: {
      std::optional<__u32> tmp = node.Read<__u32>();
      if (!tmp)
        return false;
      control.values_u32_.push_back(*tmp);
      return true;
    }
#ifdef V4L2_CTRL_TYPE_AREA
    case V4L2_CTRL_TYPE_AREA: {
      bool ok = true;
      struct v4l2_area tmp;
      tmp.width = node["width"].ReadInt<__u32>(ok);
      tmp.height = node["height"].ReadInt<__u32>(ok);
      if (!ok)
        return false;
      control.values_area_.push_back(tmp);
      return true;
    }
#endif /* V4L2_CTRL_TYPE_AREA */
    default:
      MCTK_PANIC("Unknown control type found in YAML file");
      return false;
  }
}

}  // namespace

std::unique_ptr<V4lMcControl> V4lMcControl::CreateFromYamlNode(
    YamlNode& node_ctl) {
  auto control = std::make_unique<V4lMcControl>();

  std::vector<YamlNode*>& nodes_values = node_ctl["values"].ReadSequence();

  /* Parse desc */
  bool ok = true;
  control->desc_.id = node_ctl["desc"]["id"].ReadInt<__u32>(ok);
  control->desc_.type = node_ctl["desc"]["type"].ReadInt<__u32>(ok);
  node_ctl["desc"]["name"].ReadCString(control->desc_.name, 32, ok);
  control->desc_.minimum = node_ctl["desc"]["minimum"].ReadInt<__s64>(ok);
  control->desc_.maximum = node_ctl["desc"]["maximum"].ReadInt<__s64>(ok);
  control->desc_.step = node_ctl["desc"]["step"].ReadInt<__u64>(ok);
  control->desc_.default_value =
      node_ctl["desc"]["default_value"].ReadInt<__s64>(ok);
  control->desc_.flags = node_ctl["desc"]["flags"].ReadInt<__u32>(ok);
  control->desc_.elem_size = node_ctl["desc"]["elem_size"].ReadInt<__u32>(ok);
  /* elems is implicit in the YAML format */
  control->desc_.elems = nodes_values.size();
  /* dims is optional in the YAML format */
  if (node_ctl["desc"]["dims"].IsEmpty()) {
    control->desc_.nr_of_dims = 0;
  } else {
    /* nr_of_dims is implicit in the YAML format */
    control->desc_.nr_of_dims = node_ctl["desc"]["dims"].ReadSequence().size();
    node_ctl["desc"]["dims"].ReadCArray<__u32>(control->desc_.dims,
                                               control->desc_.nr_of_dims, ok);
  }
  if (!ok) {
    MCTK_ERR("Parsing control description failed.");
    return nullptr;
  }

  /* A control that was serialised has at least one value */
  if (!control->desc_.elems)
    return nullptr;

  /* Consistency checks */
  MCTK_ASSERT(nodes_values.size() == control->desc_.elems);
  if (!ControlHelperDescLooksOK(control->desc_)) {
    MCTK_ERR("Control description doesn't look right, aborting.");
    return nullptr;
  }

  for (auto* val : nodes_values) {
    if (!ParsePayloadValue(*control, *val)) {
      MCTK_ERR("Failed to parse control value/payload, aborting.");
      return nullptr;
    }
  }

  return control;
}
