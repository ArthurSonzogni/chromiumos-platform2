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

// NOLINTNEXTLINE(build/include)
#include "tools/mctk/remap.h"

#include <linux/media.h>
#include <linux/types.h>

#include <optional>
#include <string>
#include <utility> /* std::pair */
#include <vector>

#include "tools/mctk/debug.h"
#include "tools/mctk/mcdev.h"

/* Look up a remapped entity ID's name.
 *
 * Returns:
 *  - An entity name if there is a remap entry for this ID.
 *  - std::nullopt if there is no remap entry.
 */
std::optional<std::string> V4lMcRemap::LookupEntityName(__u32 in_entity) {
  for (std::pair<__u32, std::string> entry : remap_list_)
    if (entry.first == in_entity)
      return std::optional<std::string>(entry.second);

  return std::nullopt;
}

/* Look up a remapped entity ID, with a fallback to the input ID.
 *
 * This checks if the input ID is mentioned in the remapping table.
 * If yes, it looks for an entity with the mapped name in the target graph.
 *
 * Returns:
 *  - The found target entity's ID, if both lookups succeed.
 *  - The input ID if any step fails.
 *
 * This allows using this function safely everywhere, covering both remapped
 * and not remapped entities.
 */
__u32 V4lMcRemap::LookupEntityId(__u32 in_entity, V4lMcDev& mc_target) {
  for (std::pair<__u32, std::string> entry : remap_list_) {
    if (entry.first == in_entity) {
      auto te = mc_target.EntityByName(entry.second);

      if (!te) {
        /* We tried to look up an entity that doesn't exist.
         * This is indicative of a mismatch between the remapping
         * and the target media-ctl.
         */
        MCTK_ERR("Entity named " + entry.second +
                 " not found. Proceeding without remapping.");
        return in_entity;
      }

      return te->desc_.id;
    }
  }

  return in_entity;
}
