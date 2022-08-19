/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string>
#include <vector>

#include <base/strings/string_util.h>
#include <base/strings/string_number_conversions.h>

#include "cros-camera/common.h"
#include "hal/fake/value_util.h"

namespace cros {

DottedPath DottedPath::extend(const std::string& p) const {
  DottedPath ret = *this;
  ret.segments.push_back(p);
  return ret;
}

std::ostream& operator<<(std::ostream& s, const DottedPath& path) {
  s << "$";
  for (const auto& p : path.segments) {
    s << "." << p;
  }
  return s;
}

const ListWithPath::Iterator& ListWithPath::Iterator::operator++() {
  idx++;
  return *this;
}

ValueWithPath ListWithPath::Iterator::operator*() const {
  return ValueWithPath{&(*value)[idx], path.extend(base::NumberToString(idx))};
}

bool ListWithPath::Iterator::operator!=(const Iterator& o) const {
  return idx != o.idx;
}

ListWithPath::Iterator ListWithPath::begin() const {
  return Iterator{path, value, 0};
}

ListWithPath::Iterator ListWithPath::end() const {
  return Iterator{path, value, value->size()};
}

#define WARN_MALFORMED(path, type, value)                        \
  LOGF(WARNING) << "malformed entry at " << path << ": " << type \
                << " expected, got:\n"                           \
                << value
#define WARN_MISSING(path) LOGF(WARNING) << "missing required key at " << path
#define WARN_MISSING_WITH_TYPE(path, type) \
  WARN_MISSING(path) << ": " << type << "expected"

std::optional<DictWithPath> GetIfDict(const ValueWithPath& v) {
  auto ret = v->GetIfDict();
  if (ret == nullptr) {
    WARN_MALFORMED(v.path, "dictionary", *v.value);
    return {};
  }
  return DictWithPath{ret, v.path};
}

std::optional<ListWithPath> GetIfList(const ValueWithPath& v) {
  auto ret = v->GetIfList();
  if (ret == nullptr) {
    WARN_MALFORMED(v.path, "list", *v.value);
    return {};
  }
  return ListWithPath{ret, v.path};
}

template <>
std::optional<ValueWithPath> GetValue<ValueWithPath>(const DictWithPath& dict,
                                                     base::StringPiece key) {
  auto child = dict->Find(key);
  if (child == nullptr) {
    WARN_MISSING(dict.path << "." << key);
    return {};
  }
  return ValueWithPath{child, dict.path.extend(std::string(key))};
}

template <>
std::optional<ListWithPath> GetValue<ListWithPath>(const DictWithPath& dict,
                                                   base::StringPiece key) {
  auto child = dict->Find(key);
  if (child == nullptr) {
    WARN_MISSING_WITH_TYPE(dict.path << "." << key, "list");
    return {};
  }
  auto ret = child->GetIfList();
  if (!ret) {
    WARN_MALFORMED(dict.path << "." << key, "list", *child);
    return {};
  }
  return ListWithPath{ret, dict.path.extend(std::string(key))};
}

template <>
std::optional<DictWithPath> GetValue(const DictWithPath& dict,
                                     base::StringPiece key) {
  auto child = dict->Find(key);
  if (child == nullptr) {
    WARN_MISSING_WITH_TYPE(dict.path << "." << key, "dict");
    return {};
  }
  auto ret = child->GetIfDict();
  if (!ret) {
    WARN_MALFORMED(dict.path << "." << key, "dict", *child);
    return {};
  }
  return DictWithPath{ret, dict.path.extend(std::string(key))};
}

#define GENERATE_TYPED_GETTER(c_type, value_type, type_name)               \
  template <>                                                              \
  std::optional<c_type> GetValue<c_type>(const DictWithPath& dict,         \
                                         base::StringPiece key) {          \
    auto child = dict->Find(key);                                          \
    if (child == nullptr) {                                                \
      WARN_MISSING_WITH_TYPE(dict.path << "." << key, #type_name);         \
      return {};                                                           \
    }                                                                      \
    auto ret = child->GetIf##value_type();                                 \
    if (!ret) {                                                            \
      WARN_MALFORMED(dict.path << "." << key, #type_name, *child);         \
      return {};                                                           \
    }                                                                      \
    return *ret;                                                           \
  }                                                                        \
                                                                           \
  template <>                                                              \
  c_type GetValue<c_type>(const DictWithPath& dict, base::StringPiece key, \
                          const c_type& default_value) {                   \
    auto child = dict->Find(key);                                          \
    if (child == nullptr) {                                                \
      return default_value;                                                \
    }                                                                      \
    auto ret = child->GetIf##value_type();                                 \
    if (!ret) {                                                            \
      WARN_MALFORMED(dict.path << "." << key, #type_name, *child)          \
          << ", returning default value";                                  \
      return default_value;                                                \
    }                                                                      \
    return *ret;                                                           \
  }

GENERATE_TYPED_GETTER(int, Int, integer);
GENERATE_TYPED_GETTER(bool, Bool, boolean);
GENERATE_TYPED_GETTER(double, Double, number);
GENERATE_TYPED_GETTER(std::string, String, string);

#undef GENERATE_TYPED_GETTER
#undef WARN_MISSING_WITH_TYPE
#undef WARN_MISSING
#undef WARN_MALFORMED
}  // namespace cros
