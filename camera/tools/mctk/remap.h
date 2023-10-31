/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Entity remapping for merge operations.
 *
 * This allows configurations to be applied in case of entity renumbering.
 * The target entity will be identified by name.
 *
 * In other words, when the configuration file specifies a property to be set
 * on the entity with ID X, then the remapping will replace all occurrences
 * of X with the ID of the entity with name "NAME".
 *
 * To this end, remap keeps a lookup table of tuples of the type:
 *  - (X, "NAME1")
 *  - (Y, "NAME2")
 *  - (Z, "NAME3")
 */

#ifndef CAMERA_TOOLS_MCTK_REMAP_H_
#define CAMERA_TOOLS_MCTK_REMAP_H_

#include <memory>
#include <string>
#include <utility> /* std::pair */
#include <vector>

#include "tools/mctk/mcdev.h"
#include "tools/mctk/remap.h"
#include "tools/mctk/yaml_tree.h"

class V4lMcRemap {
 public:
  /* Public functions */

  __u32 LookupEntityId(__u32 in_entity, V4lMcDev& mc_target);

  static std::unique_ptr<V4lMcRemap> CreateFromYamlNode(YamlNode& node_remap);

 private:
  /* Private variables */

  std::vector<std::pair<__u32, std::string>> remap_list_;
};

#endif /* CAMERA_TOOLS_MCTK_REMAP_H_ */
