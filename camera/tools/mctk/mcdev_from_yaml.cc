/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Factory for an abstract model of a V4L2 media controller.
 * It will be populated with data from a YAML tree.
 *
 * The YAML tree is no longer needed once this function returns.
 *
 * Returns:
 *  - on success: A pointer to an abstract V4L2 media-ctl.
 *  - on failure: nullptr.
 */

#include "tools/mctk/mcdev.h"

#include <linux/media.h>
#include <linux/types.h>

#include <memory>
#include <utility> /* std::move */
#include <vector>

#include "tools/mctk/debug.h"
#include "tools/mctk/entity.h"
#include "tools/mctk/yaml_tree.h"

/* Load the graph from an in-memory YamlTree and build a class and
 * its child nodes.
 * The resulting mcdev will be no pointers into the YamlTree after
 * this operation, and the YamlTree is safe to delete.
 */
std::unique_ptr<V4lMcDev> V4lMcDev::CreateFromYamlNode(YamlNode& node_mc) {
  auto mcdev = std::make_unique<V4lMcDev>();

  /* Parse info */
  bool ok = true;
  node_mc["info"]["driver"].ReadCString(mcdev->info_.driver, 16, ok);
  node_mc["info"]["model"].ReadCString(mcdev->info_.model, 32, ok);
  node_mc["info"]["serial"].ReadCString(mcdev->info_.serial, 40, ok);
  node_mc["info"]["bus_info"].ReadCString(mcdev->info_.bus_info, 32, ok);
  mcdev->info_.media_version =
      node_mc["info"]["media_version"].ReadInt<__u32>(ok);
  mcdev->info_.hw_revision = node_mc["info"]["hw_revision"].ReadInt<__u32>(ok);
  mcdev->info_.driver_version =
      node_mc["info"]["driver_version"].ReadInt<__u32>(ok);
  if (!ok) {
    MCTK_ERR("Failed parsing: media_ctl > info");
    return nullptr;
  }

  /* Parse entities */
  for (YamlNode* node_ent : node_mc["entities"].ReadSequence()) {
    std::unique_ptr<V4lMcEntity> new_e =
        V4lMcEntity::CreateFromYamlNode(*node_ent);

    if (!new_e) {
      MCTK_ERR("Failed to create entity from YAML node.");
      return nullptr;
    }

    mcdev->entities_.push_back(std::move(new_e));
  }

  /* Sync up all lists and pointers */
  mcdev->BuildCrosslinks();

  return mcdev;
}
