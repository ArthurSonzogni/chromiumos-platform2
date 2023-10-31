/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Factory for an abstract model of a V4L2 link.
 * It will be populated with data from a YAML tree.
 *
 * The YAML tree is no longer needed once this function returns.
 *
 * Returns:
 *  - on success: A pointer to an abstract V4L2 link.
 *  - on failure: nullptr.
 */

#include "tools/mctk/link.h"

#include <linux/media.h>
#include <linux/types.h>

#include <memory>

#include "tools/mctk/debug.h"
#include "tools/mctk/pad.h"
#include "tools/mctk/yaml_tree.h"

std::unique_ptr<V4lMcLink> V4lMcLink::CreateFromYamlNode(YamlNode& node_link,
                                                         V4lMcPad& src_pad) {
  auto link = std::make_unique<V4lMcLink>();

  /* Fill source */
  link->desc_.source = src_pad.desc_;

  /* Parse desc and sink */
  bool ok = true;
  link->desc_.sink.entity = node_link["sink"]["entity"].ReadInt<__u32>(ok);
  link->desc_.sink.index = node_link["sink"]["index"].ReadInt<__u16>(ok);
  link->desc_.sink.flags = node_link["sink"]["flags"].ReadInt<__u32>(ok);
  link->desc_.flags = node_link["flags"].ReadInt<__u32>(ok);
  if (!ok) {
    MCTK_PANIC("Failed parsing: link");
    return nullptr;
  }

  return link;
}
