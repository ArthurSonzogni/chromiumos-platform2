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
#include <optional>
#include <string>
#include <vector>

#include "tools/mctk/mcdev.h"
#include "tools/mctk/remap.h"
#include "tools/mctk/yaml_tree.h"

class V4lMcRemapEntry {
 public:
  explicit V4lMcRemapEntry(__u32 source_id, std::string target_name)
      : source_id_(source_id), target_name_(target_name) {}

  __u32 source_id_;

  std::string target_name_;
};

class V4lMcRemap {
 public:
  /* Public functions */

  static std::unique_ptr<V4lMcRemap> CreateFromYamlNode(YamlNode& node_remap);

  std::optional<V4lMcRemapEntry> LookupEntry(__u32 source_id);
  std::optional<__u32> LookupRemappedId(__u32 source_id, V4lMcDev& mc_target);
  __u32 LookupRemappedIdOrFallback(__u32 source_id, V4lMcDev& mc_target);

 private:
  /* Private variables */

  std::vector<V4lMcRemapEntry> remap_list_;
};

#endif /* CAMERA_TOOLS_MCTK_REMAP_H_ */
