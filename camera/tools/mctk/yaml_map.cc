/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Specialised YamlNode, representing a YAML mapping.
 *
 * This is equivalent to maps/dictionaries in programming languages.
 *
 * Keys are always strings in this implementation.
 * (YAML itself allows any node to be a key).
 */

#include "tools/mctk/yaml_tree.h"

#include <linux/types.h>
#include <yaml.h>

#include <string>
#include <utility> /* std::pair */
#include <vector>

#include "tools/mctk/debug.h"

YamlMap::~YamlMap() {
  for (YamlMapPair pair : this->map_)
    delete &pair.second;
}

/* Caller is responsible for deleting the event passed in */
YamlMap* YamlMap::FromParserEvent(yaml_parser_t& parser,
                                  yaml_event_t& start_event) {
  MCTK_ASSERT(start_event.type == YAML_MAPPING_START_EVENT);

  YamlMap* new_map = new YamlMap();
  if (!new_map)
    return NULL;

  /* Store YAML formatting style for debugging purposes */
  new_map->implicit_ = start_event.data.mapping_start.implicit;
  new_map->style_ = start_event.data.mapping_start.style;

  while (true) {
    yaml_event_t event;

    if (!yaml_parser_parse(&parser, &event))
      goto error;

    if (event.type == YAML_MAPPING_END_EVENT) {
      yaml_event_delete(&event);
      return new_map;
    }

    /* Part 1: We expect a scalar as a key */
    if (event.type != YAML_SCALAR_EVENT) {
      yaml_event_delete(&event);
      goto error;
    }

    std::string key((const char*)event.data.scalar.value,
                    event.data.scalar.length);

    yaml_event_delete(&event);

    /* Part 2: Parse any node type as a value */
    YamlNode* value = YamlNode::FromParser(parser);
    if (!value)
      goto error;

    new_map->map_.push_back(YamlMapPair(key, *value));
  }

error:
  delete new_map;
  return NULL;
}

bool YamlMap::Emit(yaml_emitter_t& emitter) {
  yaml_event_t event;

  if (!yaml_mapping_start_event_initialize(
          &event, NULL, reinterpret_cast<const yaml_char_t*>(YAML_MAP_TAG),
          implicit_, style_))
    return false;
  if (!yaml_emitter_emit(&emitter, &event))
    return false;

  for (YamlMapPair pair : this->map_) {
    if (!yaml_scalar_event_initialize(
            &event, NULL, NULL,
            reinterpret_cast<const yaml_char_t*>(pair.first.c_str()), -1, 1, 1,
            YAML_ANY_SCALAR_STYLE))
      return false;
    if (!yaml_emitter_emit(&emitter, &event))
      return false;

    pair.second.Emit(emitter);
  }

  if (!yaml_mapping_end_event_initialize(&event))
    return false;
  if (!yaml_emitter_emit(&emitter, &event))
    return false;

  return true;
}
