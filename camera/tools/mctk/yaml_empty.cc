/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* The empty YAML node is merely implementation specific syntactical sugar.
 * It allows full-path lookups to fail gracefully if an intermediary node
 * does not exist, enabling batch parsing:
 *
 * std::optional<__u32> value = root["key1"][42]["key2"].Read<__u32>();
 */

#include "tools/mctk/yaml_tree.h"

#include <vector>

#include "tools/mctk/debug.h"

static YamlEmpty empty_node;

bool YamlEmpty::Emit(yaml_emitter_t& emitter) {
  (void)emitter;

  /* If an empty YAML node is being emitted, then a logical error in
   * the program has likely corrupted the YAML tree.
   */
  MCTK_PANIC("Attempted to emit an empty YAML node.");

  return true;
}
