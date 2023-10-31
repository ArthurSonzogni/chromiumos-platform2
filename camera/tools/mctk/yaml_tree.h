/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This is a thin "composer" helper layer in the sense of the
 * YAML specification: https://yaml.org/spec/1.1/current.html
 *
 * It is given a pre-initialised libYAML parser, and generates a
 * representation graph from the event stream.
 *
 * There are multiple restrictions. Examples:
 *  - Not all event types are handled - example: YAML_ALIAS_EVENT
 *  - Map keys must be scalars
 */

#ifndef CAMERA_TOOLS_MCTK_YAML_TREE_H_
#define CAMERA_TOOLS_MCTK_YAML_TREE_H_

#include <assert.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <stddef.h> /* size_t */
#include <stdio.h>
#include <yaml.h>

#include <optional>
#include <string>
#include <utility> /* std::pair */
#include <vector>

#include "tools/mctk/selection.h"

class YamlNode;
class YamlEmpty;
class YamlScalar;
class YamlSequence;
class YamlMap;

class YamlNode {
 public:
  virtual ~YamlNode() {}

  /* Recursively convert an input to a YAML tree, depending on what data
   * is at hand:
   *  - a FILE (feeding it into libyaml),
   *  - a libyaml parser, or
   *  - a libyaml parser and the last read event (used internally).
   */
  static YamlNode* FromFile(FILE& file);
  static YamlNode* FromParser(yaml_parser_t& parser);
  static YamlNode* FromParserEvent(yaml_parser_t& parser, yaml_event_t& event);

  /* Recursively convert a YAML tree to a FILE. */
  bool ToFile(FILE& file);
  virtual bool Emit(yaml_emitter_t& emitter) = 0;

  /* Check if this is a YamlEmpty node */
  bool IsEmpty();

  /* Syntactic sugar for lookups.
   *
   * If the node is a YamlSequence, integer lookups returns the n-th node.
   * Otherwise a YamlEmpty node.
   *
   * If the node is a YamlMap, string lookups return the node with that key.
   * Otherwise a YamlEmpty node.
   */
  YamlNode& operator[](size_t index);
  YamlNode& operator[](std::string key);

  /* Getters for basic types in std::optional form.
   * Returns std:nullopt if trying to read from an empty node.
   */
  template <typename T>
  std::optional<T> Read();

  /* Parse a whole array of the same basic type, but only if the
   * array size matches the expected number of elements.
   */
  template <typename T>
  std::optional<std::vector<T>> ReadArray(size_t expected_count);

  /* Getters for batch parsing.
   * These handle bare values, instead of encapsulating them in std::optional.
   * The passed reference to a bool is not touched if the read succeeds.
   * On failure, the bool is set to false, and an undefined value is returned.
   *
   * ReadInt() is a template, as it specialises on uint64_t, int32_t, and more.
   */
  template <typename T>
  T ReadInt(bool& ok);

  template <typename T>
  void ReadCArray(T* dest, size_t expected_count, bool& ok);

  /* destlen is the maximum destination buffer length.
   * It will always be NUL-terminated.
   */
  void ReadCString(char* dest, size_t destlen, bool& ok);

  /* Return a vector of nodes contained in a sequence node.
   * If it is not a sequence, an empty vector is returned.
   */
  std::vector<YamlNode*>& ReadSequence();

  /* Getters for common composite V4L data types */
  std::optional<struct v4l2_rect> ReadRect();
  void ReadSelection(V4lMcSelection& dest);
};

/*
 * The empty YAML node is merely implementation specific syntactical sugar.
 * It allows full-path lookups to fail gracefully if an intermediary node
 * does not exist, enabling batch parsing:
 *
 * std::optional<__u32> value = root["key1"][42]["key2"].Read<__u32>();
 */
class YamlEmpty : public YamlNode {
 public:
  bool Emit(yaml_emitter_t& emitter);
};

/*
 * YAML scalars are leaf nodes containing an actual value
 */
class YamlScalar : public YamlNode {
 public:
  static YamlScalar* FromEvent(yaml_event_t& event);

  bool Emit(yaml_emitter_t& emitter);

  template <typename T>
  std::optional<T> Read();

  /* The actual value stored in this node */
  const std::string value_;

 private:
  explicit YamlScalar(std::string value) : value_(value) {}
};

/*
 * YAML sequences act like vectors/lists
 */
class YamlSequence : public YamlNode {
 public:
  ~YamlSequence();
  static YamlSequence* FromParserEvent(yaml_parser_t& parser,
                                       yaml_event_t& start_event);

  bool Emit(yaml_emitter_t& emitter);

  template <typename T>
  std::optional<std::vector<T>> ReadArray(size_t expected_count);

  /* The actual list of nodes */
  std::vector<YamlNode*> list_;

 private:
  bool ParseOneListElement(yaml_parser_t& parser);

  int implicit_;
  yaml_sequence_style_t style_;
};

/*
 * YAML mappings act like maps/dictionaries
 */
typedef std::pair<std::string, YamlNode&> YamlMapPair;

class YamlMap : public YamlNode {
 public:
  ~YamlMap();
  static YamlMap* FromParserEvent(yaml_parser_t& parser,
                                  yaml_event_t& start_event);

  bool Emit(yaml_emitter_t& emitter);

  /* The actual map of nodes */
  std::vector<YamlMapPair> map_;

 private:
  int implicit_;
  yaml_mapping_style_t style_;
};

#endif /* CAMERA_TOOLS_MCTK_YAML_TREE_H_ */
