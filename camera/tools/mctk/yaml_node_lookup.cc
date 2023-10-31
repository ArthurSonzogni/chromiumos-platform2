/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* operator[] overloads for easy YAML tree traversal.
 *
 * Returns:
 *  - on success: The desired node.
 *  - on failure: A YamlEmpty node.
 */

#include "tools/mctk/yaml_tree.h"

#include <stddef.h> /* size_t */

#include <string>
#include <vector>

#include "tools/mctk/debug.h"

static YamlEmpty empty_node;

/* Look up a node by position in YAML sequences. */
YamlNode& YamlNode::operator[](size_t index) {
  YamlSequence* sequence = dynamic_cast<YamlSequence*>(this);
  if (!sequence)
    return empty_node;

  if (sequence->list_.size() <= index)
    return empty_node;

  return *(sequence->list_[index]);
}

/* Look up a node by key in YAML mappings. */
YamlNode& YamlNode::operator[](std::string key) {
  YamlMap* map = dynamic_cast<YamlMap*>(this);
  if (!map)
    return empty_node;

  for (YamlMapPair pair : map->map_)
    if (pair.first == key)
      return pair.second;

  return empty_node;
}
