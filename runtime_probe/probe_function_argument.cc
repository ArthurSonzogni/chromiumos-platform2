/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string>
#include <utility>
#include <vector>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_function_argument.h"

namespace runtime_probe {

#define _DEFINE_PARSE_ARGUMENT(type, GetType)                                  \
  template <>                                                                  \
  bool ParseArgument<type>(const char* function_name, const char* member_name, \
                           type* member, const base::Value& value) {           \
    if (!value.is_##type()) {                                                  \
      LOG(ERROR) << function_name << ": `" << member_name                      \
                 << "` should be " #type;                                      \
      return false;                                                            \
    }                                                                          \
    *member = value.GetType();                                                 \
    return true;                                                               \
  }

using std::string;
_DEFINE_PARSE_ARGUMENT(string, GetString);
_DEFINE_PARSE_ARGUMENT(bool, GetBool);
_DEFINE_PARSE_ARGUMENT(double, GetDouble);
_DEFINE_PARSE_ARGUMENT(int, GetInt);

template <>
bool ParseArgument<std::vector<std::string>>(const char* function_name,
                                             const char* member_name,
                                             std::vector<std::string>* member,
                                             const base::Value& value) {
  if (!value.is_list()) {
    LOG(ERROR) << "failed to parse " << value << " as a list of string.";
    return false;
  }

  std::vector<std::string> tmp{};
  const base::ListValue* list_value;
  value.GetAsList(&list_value);

  for (auto it = list_value->begin(); it != list_value->end(); it++) {
    if (!(*it)->is_string()) {
      LOG(ERROR) << "failed to parse " << value << " as a list of string.";
      return false;
    }

    tmp.emplace_back();
    (*it)->GetAsString(&tmp.back());
  }
  member->swap(tmp);
  return true;
}

template <>
bool ParseArgument<std::vector<std::unique_ptr<ProbeFunction>>>(
    const char* function_name,
    const char* member_name,
    std::vector<std::unique_ptr<ProbeFunction>>* member,
    const base::Value& value) {
  if (!value.is_list()) {
    LOG(ERROR) << "failed to parse " << value
               << " as a list of probe functions.";
    return false;
  }

  std::vector<std::unique_ptr<ProbeFunction>> tmp{};

  const base::ListValue* list_value;
  value.GetAsList(&list_value);

  for (auto it = list_value->begin(); it != list_value->end(); it++) {
// TODO(crbug.com/909719): remove this after libchrome uprev
#if BASE_VER < 576279
    auto ptr = runtime_probe::ProbeFunction::FromValue(**it);
#else
    auto ptr = runtime_probe::ProbeFunction::FromValue(*it);
#endif
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
