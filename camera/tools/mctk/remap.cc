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
 *  - A remap entry if there is one for this ID.
 *  - std::nullopt if there is no remap entry.
 */
std::optional<V4lMcRemapEntry> V4lMcRemap::LookupEntry(__u32 source_id) {
  for (V4lMcRemapEntry& entry : remap_list_)
    if (entry.source_id_ == source_id)
      return entry;

  return std::nullopt;
}

/* Look up a remapped entity ID, or nullopt if something fails.
 *
 * This checks if the input ID is mentioned in the remapping table.
 * If yes, it looks for an entity with the mapped name in the target graph.
 *
 * Returns:
 *  - The found target entity's ID, if both lookups succeed.
 *  - std::nullopt if any step fails.
 */
std::optional<__u32> V4lMcRemap::LookupRemappedId(__u32 source_id,
                                                  V4lMcDev& mc_target) {
  std::optional<V4lMcRemapEntry> entry = this->LookupEntry(source_id);

  if (!entry)
    return std::nullopt;

  if (!entry->target_name_.empty()) {
    V4lMcEntity* te = mc_target.EntityByName(entry->target_name_);

    if (te)
      return te->desc_.id;
  }

  if (!entry->target_name_regex_.empty()) {
    V4lMcEntity* te = mc_target.EntityByNameRegex(entry->target_name_regex_);

    if (te)
      return te->desc_.id;
  }

  /* We tried to look up an entity with a name that doesn't exist
   * in the target media controller.
   * Maybe this remapping is not meant to apply to this target?
   */
  return std::nullopt;
}

/* Look up a remapped entity ID, with a fallback to the input ID.
 *
 * Same as LookupRemappedId(), but with a fallback in case it returns nullopt.
 *
 * This allows using this function safely everywhere, covering both remapped
 * and not remapped entities.
 */
__u32 V4lMcRemap::LookupRemappedIdOrFallback(__u32 source_id,
                                             V4lMcDev& mc_target) {
  std::optional<__u32> target_id = this->LookupRemappedId(source_id, mc_target);

  if (target_id)
    return *target_id;

  return source_id;
}
