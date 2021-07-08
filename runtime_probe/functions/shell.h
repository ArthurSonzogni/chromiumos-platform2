// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_SHELL_H_
#define RUNTIME_PROBE_FUNCTIONS_SHELL_H_

#include <memory>
#include <string>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

class ShellFunction : public ProbeFunction {
 public:
  // The identifier / function name of this probe function.
  //
  // It will be used for both parsing and logging.
  NAME_PROBE_FUNCTION("shell");

  // Define a parser for this function.
  //
  // This function takes the arguments in `const base::Value&` type.
  // This function should parse the `dict_value`, if the `dict_value` has
  // correct format, this function should return a new instance of
  // `ShellFunction` whose members are decided by `dict_value`.
  //
  // @args dict_value: a JSON dictionary to parse arguments from.
  //
  // @return pointer to new `ShellFunction` instance on success, nullptr
  //   otherwise.
  template <typename T>
  static auto FromKwargsValue(const base::Value& dict_value) {
    PARSE_BEGIN();
    PARSE_ARGUMENT(command);
    PARSE_ARGUMENT(key, std::string{"shell_raw"});
    PARSE_ARGUMENT(split_line, false);
    PARSE_END();
  }

 private:
  // Override `EvalImpl` function, which should return a list of Value.
  DataType EvalImpl() const override {
    VLOG(1) << "command: " << command_;
    VLOG(1) << "split_line: " << split_line_;
    // TODO(stimim): implement this

    return DataType{};
  }

  // Declare function arguments
  std::string command_;
  std::string key_;
  bool split_line_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_SHELL_H_
