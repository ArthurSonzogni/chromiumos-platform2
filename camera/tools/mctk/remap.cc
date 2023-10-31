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

#include <utility> /* std::pair */
#include <vector>

#include "tools/mctk/debug.h"
#include "tools/mctk/mcdev.h"

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
