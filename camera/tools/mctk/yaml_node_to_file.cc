/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/mctk/yaml_tree.h"

#include <stdio.h>
#include <yaml.h>

#include "tools/mctk/debug.h"

/* Dump a YAML node and its children to a stdio FILE. */
bool YamlNode::ToFile(FILE& file) {
  yaml_emitter_t emitter;
  yaml_event_t event;

  yaml_emitter_initialize(&emitter);
  yaml_emitter_set_output_file(&emitter, &file);

  MCTK_ASSERT(yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING));
  MCTK_ASSERT(yaml_emitter_emit(&emitter, &event));

  MCTK_ASSERT(
      yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1));
  MCTK_ASSERT(yaml_emitter_emit(&emitter, &event));

  this->Emit(emitter);

  MCTK_ASSERT(yaml_document_end_event_initialize(&event, 1));
  MCTK_ASSERT(yaml_emitter_emit(&emitter, &event));

  MCTK_ASSERT(yaml_stream_end_event_initialize(&event));
  MCTK_ASSERT(yaml_emitter_emit(&emitter, &event));

  yaml_emitter_delete(&emitter);

  return true;
}
