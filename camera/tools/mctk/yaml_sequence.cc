/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Specialised YamlNode, representing a YAML sequence.
 *
 * This is equivalent to vectors/lists in programming languages.
 */

#include "tools/mctk/yaml_tree.h"

#include <stddef.h> /* size_t */
#include <yaml.h>

#include <vector>

#include "tools/mctk/debug.h"

YamlSequence::~YamlSequence() {
  for (YamlNode* node : this->list_)
    delete node;
}

bool YamlSequence::ParseOneListElement(yaml_parser_t& parser) {
  yaml_event_t event;

  if (!yaml_parser_parse(&parser, &event))
    return false;

  if (event.type == YAML_SEQUENCE_END_EVENT) {
    yaml_event_delete(&event);
    return true;
  }

  YamlNode* new_node = YamlNode::FromParserEvent(parser, event);
  yaml_event_delete(&event);
  if (!new_node)
    return false;

  this->list_.push_back(new_node);
  return this->ParseOneListElement(parser);
}

/* Caller is responsible for deleting the event passed in */
YamlSequence* YamlSequence::FromParserEvent(yaml_parser_t& parser,
                                            yaml_event_t& start_event) {
  MCTK_ASSERT(start_event.type == YAML_SEQUENCE_START_EVENT);

  YamlSequence* new_seq = new YamlSequence();
  if (!new_seq)
    return NULL;

  /* Store YAML formatting style for debugging purposes */
  new_seq->implicit_ = start_event.data.sequence_start.implicit;
  new_seq->style_ = start_event.data.sequence_start.style;

  if (new_seq->ParseOneListElement(parser))
    return new_seq;

  delete new_seq;
  return NULL;
}

bool YamlSequence::Emit(yaml_emitter_t& emitter) {
  yaml_event_t event;

  if (!yaml_sequence_start_event_initialize(
          &event, NULL, reinterpret_cast<const yaml_char_t*>(YAML_SEQ_TAG),
          implicit_, style_))
    return false;
  if (!yaml_emitter_emit(&emitter, &event))
    return false;

  for (YamlNode* node : this->list_) {
    node->Emit(emitter);
  }

  if (!yaml_sequence_end_event_initialize(&event))
    return false;
  if (!yaml_emitter_emit(&emitter, &event))
    return false;

  return true;
}

/* Parse a whole array of the same basic type, but only if the
 * array size matches the expected number of elements.
 */
template <typename T>
std::optional<std::vector<T>> YamlSequence::ReadArray(size_t expected_count) {
  if (list_.size() != expected_count)
    return std::nullopt;

  std::vector<T> out_vec;

  for (YamlNode* node : list_) {
    std::optional<T> temp = node->Read<T>();
    if (!temp)
      return std::nullopt;

    out_vec.push_back(*temp);
  }

  return std::optional<std::vector<T>>(out_vec);
}

template std::optional<std::vector<__u32>> YamlSequence::ReadArray(
    size_t expected_count);
template std::optional<std::vector<__u8>> YamlSequence::ReadArray(
    size_t expected_count);
