/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/mctk/yaml_tree.h"

#include <linux/types.h>
#include <stdlib.h>
#include <yaml.h>

#include <string>
#include <vector>

#include "tools/mctk/debug.h"

YamlScalar* YamlScalar::FromEvent(yaml_event_t& event) {
  MCTK_ASSERT(event.type == YAML_SCALAR_EVENT);

  YamlScalar* new_scalar = new YamlScalar(std::string(
      (const char*)event.data.scalar.value, event.data.scalar.length));
  return new_scalar;
}

bool YamlScalar::Emit(yaml_emitter_t& emitter) {
  yaml_event_t event;

  if (!yaml_scalar_event_initialize(&event, NULL, NULL,
                                    (const yaml_char_t*)(value_.c_str()), -1, 1,
                                    1, YAML_ANY_SCALAR_STYLE))
    return false;
  if (!yaml_emitter_emit(&emitter, &event))
    return false;

  return true;
}

template <>
std::optional<__u64> YamlScalar::Read() {
  // NOLINTNEXTLINE(runtime/int)
  unsigned long long temp = strtoull(this->value_.c_str(), NULL, 0);

  // NOLINTNEXTLINE(readability/todo)
  // TODO, optional: Check for conversion errors, out of bounds, etc.

  return std::optional<__u64>(temp);
}

template <>
std::optional<__s64> YamlScalar::Read() {
  // NOLINTNEXTLINE(runtime/int)
  unsigned long long temp = strtoull(this->value_.c_str(), NULL, 0);

  // NOLINTNEXTLINE(readability/todo)
  // TODO, optional: Check for conversion errors, out of bounds, etc.

  return std::optional<__s64>(temp);
}

template <>
std::optional<__u32> YamlScalar::Read() {
  // NOLINTNEXTLINE(runtime/int)
  unsigned long temp = strtoul(this->value_.c_str(), NULL, 0);

  // NOLINTNEXTLINE(readability/todo)
  // TODO, optional: Check for conversion errors, out of bounds, etc.

  return std::optional<__u32>(temp);
}

template <>
std::optional<__s32> YamlScalar::Read() {
  // NOLINTNEXTLINE(runtime/int)
  unsigned long temp = strtol(this->value_.c_str(), NULL, 0);

  // NOLINTNEXTLINE(readability/todo)
  // TODO, optional: Check for conversion errors, out of bounds, etc.

  return std::optional<__s32>(temp);
}

template <>
std::optional<__u16> YamlScalar::Read() {
  // NOLINTNEXTLINE(runtime/int)
  unsigned long temp = strtoul(this->value_.c_str(), NULL, 0);

  // NOLINTNEXTLINE(readability/todo)
  // TODO, optional: Check for conversion errors, out of bounds, etc.

  return std::optional<__u16>(temp);
}

template <>
std::optional<__u8> YamlScalar::Read() {
  // NOLINTNEXTLINE(runtime/int)
  unsigned long temp = strtoul(this->value_.c_str(), NULL, 0);

  // NOLINTNEXTLINE(readability/todo)
  // TODO, optional: Check for conversion errors, out of bounds, etc.

  return std::optional<__u8>(temp);
}

template <>
std::optional<std::string> YamlScalar::Read() {
  return std::optional<std::string>(this->value_);
}
