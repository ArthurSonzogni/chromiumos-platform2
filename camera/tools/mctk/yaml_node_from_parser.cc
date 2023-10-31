/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Parser for a YAML node, starting from a libyaml yaml_parser_t
 * that has already scanned past YAML_STREAM_START_EVENT and
 * YAML_DOCUMENT_START_EVENT, or anywhere in the middle of a
 * document, as long as a complete scalar/sequence/mapping follows.
 *
 * Returns:
 *  - on success: A YamlNode* representing the next YAML node.
 *  - on failure: nullptr.
 */

#include "tools/mctk/yaml_tree.h"

#include <yaml.h>

#include "tools/mctk/debug.h"

extern YamlEmpty empty_node;

/* Caller is responsible for deleting the event passed in */
YamlNode* YamlNode::FromParserEvent(yaml_parser_t& parser,
                                    yaml_event_t& event) {
  YamlNode* new_node = NULL;

  switch (event.type) {
    case YAML_SCALAR_EVENT:
      new_node = YamlScalar::FromEvent(event);
      break;

    case YAML_SEQUENCE_START_EVENT:
      new_node = YamlSequence::FromParserEvent(parser, event);
      break;

    case YAML_MAPPING_START_EVENT:
      new_node = YamlMap::FromParserEvent(parser, event);
      break;

    /* This is never used */
    case YAML_NO_EVENT:
      MCTK_PANIC("Encountered YAML_NO_EVENT");

    /* We should never see these events within the tree of a node */
    case YAML_STREAM_START_EVENT:
      MCTK_PANIC("Encountered YAML_STREAM_START_EVENT");
    case YAML_STREAM_END_EVENT:
      MCTK_PANIC("Encountered YAML_STREAM_END_EVENT");
    case YAML_DOCUMENT_START_EVENT:
      MCTK_PANIC("Encountered YAML_DOCUMENT_START_EVENT");
    case YAML_DOCUMENT_END_EVENT:
      MCTK_PANIC("Encountered YAML_DOCUMENT_END_EVENT");

    /* These events are handled by subclasses */
    case YAML_SEQUENCE_END_EVENT:
      MCTK_PANIC("Encountered YAML_SEQUENCE_END_EVENT");
    case YAML_MAPPING_END_EVENT:
      MCTK_PANIC("Encountered YAML_MAPPING_END_EVENT");

    /* We only parse documents without aliases */
    case YAML_ALIAS_EVENT:
      MCTK_PANIC("Encountered YAML_ALIAS_EVENT");
  }

  return new_node;
}

/* Fetch a new event, parse it, and then delete it */
YamlNode* YamlNode::FromParser(yaml_parser_t& parser) {
  YamlNode* new_node = NULL;
  yaml_event_t event;

  if (!yaml_parser_parse(&parser, &event))
    return NULL;

  new_node = YamlNode::FromParserEvent(parser, event);

  yaml_event_delete(&event);
  return new_node;
}
