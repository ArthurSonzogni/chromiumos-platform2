/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Factory for a list of entity name-to-id mappings.
 * It will be populated with data from a YAML tree.
 *
 * The YAML tree is no longer needed once this function returns.
 *
 * Returns:
 *  - on success: A pointer to a remapping list.
 *  - on failure: nullptr.
 */

#include "tools/mctk/remap.h"

#include <memory>
#include <optional>
#include <string>
#include <utility> /* std::move */
#include <vector>

#include "tools/mctk/debug.h"
#include "tools/mctk/yaml_tree.h"

std::unique_ptr<V4lMcRemap> V4lMcRemap::CreateFromYamlNode(
    YamlNode& node_remap) {
  auto remap = std::make_unique<V4lMcRemap>();

  /* Parse links */
  for (std::unique_ptr<YamlNode>& node_one : node_remap.ReadSequence()) {
    std::optional<__u32> id = (*node_one)["id"].Read<__u32>();
    std::optional<std::string> name = (*node_one)["name"].Read<std::string>();
    std::optional<std::string> name_regex =
        (*node_one)["name_regex"].Read<std::string>();

    /* Is the mapping entry complete? */
    if (!id) {
      MCTK_PANIC("Remap entry without source ID.");
    }

    if (!name && !name_regex) {
      MCTK_PANIC("Remap entry without target name/regex.");
    }

    /* Create the remap entry */
    V4lMcRemapEntry new_entry =
        V4lMcRemapEntry(*id, name.value_or(""), name_regex.value_or(""));
    remap->remap_list_.push_back(std::move(new_entry));
  }

  return remap;

  /* No error case here: We always have a list, even if empty */
}
