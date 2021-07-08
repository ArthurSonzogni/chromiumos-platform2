// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_PROBE_FUNCTION_ARGUMENT_H_
#define RUNTIME_PROBE_PROBE_FUNCTION_ARGUMENT_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/values.h>

namespace runtime_probe {

// To know how to define an argument parser and use it in your probe function,
// please check "functions/shell.h" as an example.  It should be well commented.
//
// Currently, we only supports the following types of arguments:
//   - std::string
//   - int
//   - bool
//   - double
//   - std::vector<std::string>
//   - std::vector<std::unique_ptr<ProbeFunction>>
//
// Arguments can have default value, except for
// std::vector<std::unique_ptr<ProbeFunction>>.

class ProbeFunction;

template <typename T>
bool ParseArgumentInternal(const char* function_name,
                           const char* member_name,
                           T* member,
                           const base::Value& value);

template <typename T>
bool ParseArgument(const char* function_name,
                   const char* member_name,
                   T* member,
                   const base::Value& value) {
  if (value.is_dict()) {
    auto* real_value = value.FindKey(member_name);
    if (!real_value) {
      LOG(ERROR) << function_name << ": `" << member_name << "` not found";
      return false;
    }
    return ParseArgumentInternal(function_name, member_name, member,
                                 *real_value);
  }
  return ParseArgumentInternal(function_name, member_name, member, value);
}

template <typename T>
bool ParseArgument(const char* function_name,
                   const char* member_name,
                   T* member,
                   const base::Value& value,
                   const T&& default_value) {
  CHECK(value.is_dict());
  if (!value.FindKey(member_name)) {
    *member = default_value;
    return true;
  }

  return ParseArgument(function_name, member_name, member, value);
}

template <>
bool ParseArgument<std::vector<std::unique_ptr<ProbeFunction>>>(
    const char* function_name,
    const char* member_name,
    std::vector<std::unique_ptr<ProbeFunction>>* member,
    const base::Value& dict_value,
    const std::vector<std::unique_ptr<ProbeFunction>>&& default_value) = delete;

// These macros are for argument parsing.
//
//  1. Due to the template declaration, the type of default value and member
//  must match exactly.  That is, the default value of a double argument
//  must be double (3.0 instead of 3).  And default value of string argument
//  must be std::string{...}.
//
//  2. Due to the behavior of "&=", all parser will be executed even if some
//  of them failed.
//
// See `functions/shell.h` for example usage.

// Assumes that |T| and |dict_value| are in the scope.
// Define |instance|, |keys| and |result| into the scope.
#define PARSE_BEGIN()                                                 \
  auto instance = std::unique_ptr<T>(new T());                        \
  instance->raw_value_ = base::Value(base::Value::Type::DICTIONARY);  \
  instance->raw_value_->SetKey(T::function_name, dict_value.Clone()); \
  std::set<std::string> keys;                                         \
  bool result = true

// Parses each argument one by one. Stores the value into |instance->arg_name_|.
// If fail, sets |result| to false. Assumes that PARSE_BEGIN is called before
// this.
#define PARSE_ARGUMENT(member_name, ...)                                       \
  result &=                                                                    \
      ParseArgument(T::function_name, #member_name, &instance->member_name##_, \
                    dict_value, ##__VA_ARGS__);                                \
  keys.insert(#member_name)

// Checks |result| and returns |instance| or nullptr. Assumes that PARSE_BEGIN
// is called before this.
// Notes that the return type must be |std::unique_ptr<T>| to support return
// type auto deduction.
#define PARSE_END()                                                    \
  if (!result)                                                         \
    return std::unique_ptr<T>{nullptr};                                \
  for (const auto kv : dict_value.DictItems()) {                       \
    if (keys.find(kv.first) == keys.end()) {                           \
      LOG(ERROR) << T::function_name << " doesn't have \"" << kv.first \
                 << "\" argument.";                                    \
      return std::unique_ptr<T>{nullptr};                              \
    }                                                                  \
  }                                                                    \
  return instance

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_PROBE_FUNCTION_ARGUMENT_H_
