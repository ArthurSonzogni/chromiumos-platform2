// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_function_argument.h"
#include "runtime_probe/probe_function_argument_legacy.h"

#include <base/logging.h>

namespace runtime_probe {
namespace internal {

#define _DEFINE_PARSE_ARGUMENT(type, GetType)                       \
  template <>                                                       \
  bool ParseArgumentImpl<type>(const base::Value& value, type& out, \
                               std::string& err) {                  \
    if (!value.is_##type()) {                                       \
      std::stringstream ss;                                         \
      ss << "expected " << #type << " but got: " << value;          \
      err = ss.str();                                               \
      return false;                                                 \
    }                                                               \
    out = value.GetType();                                          \
    return true;                                                    \
  }

using std::string;
_DEFINE_PARSE_ARGUMENT(string, GetString);
_DEFINE_PARSE_ARGUMENT(bool, GetBool);
_DEFINE_PARSE_ARGUMENT(double, GetDouble);
_DEFINE_PARSE_ARGUMENT(int, GetInt);

#undef _DEFINE_PARSE_ARGUMENT

template <>
bool ParseArgumentImpl<std::unique_ptr<ProbeFunction>>(
    const base::Value& value,
    std::unique_ptr<ProbeFunction>& out,
    std::string& err) {
  out = ProbeFunction::FromValue(value);
  if (out) {
    return true;
  }
  std::stringstream ss;
  ss << "failed to parse probe function from: " << value;
  err = ss.str();
  return false;
}

}  // namespace internal

#define _DEFINE_PARSE_ARGUMENT(type, GetType)                             \
  template <>                                                             \
  bool ParseArgumentInternal<type>(const char* function_name,             \
                                   const char* member_name, type* member, \
                                   const base::Value& value) {            \
    if (!value.is_##type()) {                                             \
      LOG(ERROR) << function_name << ": `" << member_name                 \
                 << "` should be " #type;                                 \
      return false;                                                       \
    }                                                                     \
    *member = value.GetType();                                            \
    return true;                                                          \
  }

using std::string;
_DEFINE_PARSE_ARGUMENT(string, GetString);
_DEFINE_PARSE_ARGUMENT(bool, GetBool);
_DEFINE_PARSE_ARGUMENT(double, GetDouble);
_DEFINE_PARSE_ARGUMENT(int, GetInt);

template <>
bool ParseArgumentInternal<std::vector<std::string>>(
    const char* function_name,
    const char* member_name,
    std::vector<std::string>* member,
    const base::Value& value) {
  if (!value.is_list()) {
    LOG(ERROR) << "failed to parse " << value << " as a list of string.";
    return false;
  }

  std::vector<std::string> tmp;

  for (const auto& v : value.GetList()) {
    if (!v.is_string()) {
      LOG(ERROR) << "failed to parse " << value << " as a list of string.";
      return false;
    }

    tmp.push_back(v.GetString());
  }
  member->swap(tmp);
  return true;
}

template <>
bool ParseArgumentInternal<std::vector<std::unique_ptr<ProbeFunction>>>(
    const char* function_name,
    const char* member_name,
    std::vector<std::unique_ptr<ProbeFunction>>* member,
    const base::Value& value) {
  if (!value.is_list()) {
    LOG(ERROR) << "failed to parse " << value
               << " as a list of probe functions.";
    return false;
  }

  std::vector<std::unique_ptr<ProbeFunction>> tmp;

  for (const auto& v : value.GetList()) {
    auto ptr = runtime_probe::ProbeFunction::FromValue(v);
    if (!ptr) {
      LOG(ERROR) << "failed to parse " << value
                 << " as a list of probe functions.";
      return false;
    }

    tmp.push_back(std::move(ptr));
  }

  member->swap(tmp);
  return true;
}

}  // namespace runtime_probe
