/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Parser for a YAML file, starting from a stdio FILE pointer.
 *
 * Returns:
 *  - on success: A YamlNode* representing the YAML document's contents.
 *  - on failure: nullptr.
 */

#include "tools/mctk/yaml_tree.h"

#include <assert.h>
#include <stdio.h>
#include <yaml.h>

#include "tools/mctk/debug.h"

YamlNode* YamlNode::FromFile(FILE& file) {
  YamlNode* root = nullptr;
  yaml_parser_t parser;
  yaml_event_t event;

  rewind(&file);

  yaml_parser_initialize(&parser);
  yaml_parser_set_input_file(&parser, &file);

  /* Assert that we're at the start of a file */
  if (!yaml_parser_parse(&parser, &event))
    goto del_parser;
  if (event.type != YAML_STREAM_START_EVENT) {
    yaml_event_delete(&event);
    goto del_parser;
  }
  yaml_event_delete(&event);

  /* Assert that we're at the start of a YAML document */
  if (!yaml_parser_parse(&parser, &event))
    goto del_parser;
  if (event.type != YAML_DOCUMENT_START_EVENT) {
    yaml_event_delete(&event);
    goto del_parser;
  }
  yaml_event_delete(&event);

  root = YamlNode::FromParser(parser);

  if (!root)
    goto del_parser;

  /* The next event should be a YAML_DOCUMENT_END_EVENT,
   * since we've just parsed the root node and all of its children.
   */
  if (yaml_parser_parse(&parser, &event)) {
    MCTK_ASSERT(event.type == YAML_DOCUMENT_END_EVENT);
    yaml_event_delete(&event);
  }

  /* We don't parse any further documents in the stream. */

del_parser:
  yaml_parser_delete(&parser);
  return root;
}
