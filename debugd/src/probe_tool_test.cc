// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_string_value_serializer.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "debugd/src/probe_tool.h"

namespace debugd {
namespace {
class ProbeToolForTesting : public ProbeTool {
 public:
  explicit ProbeToolForTesting(const std::string& minijail_args_json) {
    JSONStringValueDeserializer deserializer(minijail_args_json);
    auto dict = deserializer.Deserialize(nullptr, nullptr);
    SetMinijailArgumentsForTesting(std::move(dict));
  }
};

}  // namespace

TEST(ProbeToolTest, GetValidMinijailArguments_Success) {
  auto json_str = R"({
  "func1": [ "-A", "-B", "-C", "C_arg", "args" ]
})";
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &args));
  EXPECT_EQ(args.size(), 5);
  EXPECT_EQ(args[0], "-A");
  EXPECT_EQ(args[1], "-B");
  EXPECT_EQ(args[2], "-C");
  EXPECT_EQ(args[3], "C_arg");
  EXPECT_EQ(args[4], "args");
}

TEST(ProbeToolTest, GetValidMinijailArguments_Failure) {
  auto json_str = R"({
  "func1": [ "-A", "-B", "-C", "C_arg", "args" ]
})";
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  EXPECT_FALSE(probe_tool.GetValidMinijailArguments(nullptr, "func2", &args));
  EXPECT_EQ(args.size(), 0);
}
}  // namespace debugd
