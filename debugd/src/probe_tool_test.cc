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

TEST(ProbeToolTest, GetValidMinijailArguments_BindDirectoryExists) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  auto dir = temp_dir.GetPath().Append("dir");
  ASSERT_TRUE(base::CreateDirectory(dir));
  auto json_str = base::StringPrintf(
      R"({
        "func1": [ "-A", "-b", "%s" ]
      })",
      dir.value().c_str());
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &args));
  EXPECT_EQ(args.size(), 3);
  EXPECT_EQ(args[0], "-A");
  EXPECT_EQ(args[1], "-b");
  EXPECT_EQ(args[2], dir.value());
}

TEST(ProbeToolTest, GetValidMinijailArguments_SkipBindingDirectoryNotExist) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  auto not_exist_dir = temp_dir.GetPath().Append("not_exist_dir");
  auto json_str = base::StringPrintf(
      R"({
        "func1": [ "-A", "-b", "%s" ]
      })",
      not_exist_dir.value().c_str());
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &args));
  EXPECT_EQ(args.size(), 1);
  EXPECT_EQ(args[0], "-A");
}

TEST(ProbeToolTest, GetValidMinijailArguments_BindSymbolicLink) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  auto dir = temp_dir.GetPath().Append("dir");
  ASSERT_TRUE(base::CreateDirectory(dir));
  auto symlink_dir = temp_dir.GetPath().Append("symlink_dir");
  ASSERT_TRUE(base::CreateSymbolicLink(dir, symlink_dir));
  auto json_str = base::StringPrintf(
      R"({
        "func1": [ "-A", "-b", "%s" ]
      })",
      symlink_dir.value().c_str());
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &args));
  EXPECT_EQ(args.size(), 3);
  EXPECT_EQ(args[0], "-A");
  EXPECT_EQ(args[1], "-b");
  EXPECT_EQ(args[2], symlink_dir.value());
}

TEST(ProbeToolTest, GetValidMinijailArguments_BindNormalFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  auto file = temp_dir.GetPath().Append("file");
  ASSERT_EQ(base::WriteFile(file, "", 0), 0);
  auto json_str = base::StringPrintf(
      R"({
        "func1": [ "-A", "-b", "%s" ]
      })",
      file.value().c_str());
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &args));
  EXPECT_EQ(args.size(), 3);
  EXPECT_EQ(args[0], "-A");
  EXPECT_EQ(args[1], "-b");
  EXPECT_EQ(args[2], file.value());
}

TEST(ProbeToolTest, GetValidMinijailArguments_BindWithArguments) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  auto dir = temp_dir.GetPath().Append("dir");
  ASSERT_TRUE(base::CreateDirectory(dir));
  // Writeable binding.
  auto json_str = base::StringPrintf(
      R"({
        "func1": [ "-A", "-b", "%s,,1" ]
      })",
      dir.value().c_str());
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &args));
  EXPECT_EQ(args.size(), 3);
  EXPECT_EQ(args[0], "-A");
  EXPECT_EQ(args[1], "-b");
  EXPECT_EQ(args[2], base::StringPrintf("%s,,1", dir.value().c_str()));
}

}  // namespace debugd
