/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/mctk/yaml_tree.h"

#include <linux/types.h>
#include <stddef.h> /* size_t */

#include <string>
#include <vector>

#include "tools/mctk/debug.h"

extern YamlEmpty empty_node;

static std::vector<YamlNode*> empty_sequence_vector;

/* Getters for basic types in std::optional form.
 * Returns std:nullopt if trying to read from an empty node.
 */
template <typename T>
std::optional<T> YamlNode::Read() {
  YamlScalar* scalar = dynamic_cast<YamlScalar*>(this);
  if (!scalar)
    return std::nullopt;

  return scalar->Read<T>();
}

/* Parse a whole array of the same basic type, but only if the
 * array size matches the expected number of elements.
 */
template <typename T>
std::optional<std::vector<T>> YamlNode::ReadArray(size_t expected_count) {
  YamlSequence* sequence = dynamic_cast<YamlSequence*>(this);
  if (!sequence)
    return std::nullopt;

  return sequence->ReadArray<T>(expected_count);
}

/* Encapsulate Read() in a batchable form:
 *
 * bool ok = true;
 * struct.a = node["a"].ReadInt(ok);
 * struct.b = node["b"].ReadInt(ok);
 * if (!ok)
 *   error("One of the parser steps failed");
 */
template <typename T>
T YamlNode::ReadInt(bool& ok) {
  std::optional<T> val = this->Read<T>();
  if (val)
    return *val;

  ok = false;
  return 0;
}

template __u64 YamlNode::ReadInt<__u64>(bool& ok);
template __s64 YamlNode::ReadInt<__s64>(bool& ok);
template __u32 YamlNode::ReadInt<__u32>(bool& ok);
template __s32 YamlNode::ReadInt<__s32>(bool& ok);
template __u16 YamlNode::ReadInt<__u16>(bool& ok);
template __u8 YamlNode::ReadInt<__u8>(bool& ok);

/* Parse an entire array like ReadInt(), but also fail if the YAML sequence's
 * length does not match the expected array length.
 */
template <typename T>
void YamlNode::ReadCArray(T* dest, size_t expected_count, bool& ok) {
  std::optional<std::vector<T>> temp = this->ReadArray<T>(expected_count);

  if (!temp)
    ok = false;
  else
    memcpy(dest, temp->data(), expected_count * sizeof(T));
}

template void YamlNode::ReadCArray<__u32>(__u32* dest,
                                          size_t expected_count,
                                          bool& ok);
template void YamlNode::ReadCArray<__u8>(__u8* dest,
                                         size_t expected_count,
                                         bool& ok);

/* Parse a YAML scalar into a C string.
 * Fails if the destination buffer is too small.
 * The destination will always be NUL-terminated on success.
 */
void YamlNode::ReadCString(char* dest, size_t destlen, bool& ok) {
  std::optional<std::string> val = this->Read<std::string>();
  if (!val) {
    ok = false;
    return;
  }

  if (val->size() > (destlen - 1)) {
    ok = false;
    return;
  }

  strncpy(dest, val->c_str(), destlen);
}

/* Return a vector of nodes contained in a sequence node.
 * If it is not a sequence, an empty vector is returned.
 */
std::vector<YamlNode*>& YamlNode::ReadSequence() {
  YamlSequence* sequence = dynamic_cast<YamlSequence*>(this);
  if (!sequence)
    return empty_sequence_vector;

  return sequence->list_;
}
