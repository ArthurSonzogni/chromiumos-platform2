// Copyright 2022 The ChromiumOS Authors
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

constexpr char kDefaultRunAs[] = "runtime_probe";

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
    "func1": {
      "other_args": ["-A", "-B", "-C", "C_arg", "args"]
    }
  })";
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  std::string user, group;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &user,
                                                   &group, &args));
  EXPECT_EQ(user, kDefaultRunAs);
  EXPECT_EQ(group, kDefaultRunAs);
  EXPECT_EQ(args.size(), 5);
  EXPECT_EQ(args[0], "-A");
  EXPECT_EQ(args[1], "-B");
  EXPECT_EQ(args[2], "-C");
  EXPECT_EQ(args[3], "C_arg");
  EXPECT_EQ(args[4], "args");
}

TEST(ProbeToolTest, GetValidMinijailArguments_Failure) {
  auto json_str = R"({
    "func1": {
      "other_args": ["-A", "-B", "-C", "C_arg", "args"]
    }
  })";
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  std::string user, group;
  EXPECT_FALSE(probe_tool.GetValidMinijailArguments(nullptr, "func2", &user,
                                                    &group, &args));
  EXPECT_TRUE(user.empty());
  EXPECT_TRUE(group.empty());
  EXPECT_EQ(args.size(), 0);
}

TEST(ProbeToolTest, GetValidMinijailArguments_BindDirectoryExists) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  auto dir = temp_dir.GetPath().Append("dir");
  ASSERT_TRUE(base::CreateDirectory(dir));
  auto json_str = base::StringPrintf(
      R"({
        "func1": {
          "binds": ["%s"],
          "other_args": ["-A"]
        }
      })",
      dir.value().c_str());
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  std::string user, group;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &user,
                                                   &group, &args));
  EXPECT_EQ(user, kDefaultRunAs);
  EXPECT_EQ(group, kDefaultRunAs);
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
        "func1": {
          "binds": ["%s"],
          "other_args": ["-A"]
        }
      })",
      not_exist_dir.value().c_str());
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  std::string user, group;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &user,
                                                   &group, &args));
  EXPECT_EQ(user, kDefaultRunAs);
  EXPECT_EQ(group, kDefaultRunAs);
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
        "func1": {
          "binds": ["%s"],
          "other_args": ["-A"]
        }
      })",
      symlink_dir.value().c_str());
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  std::string user, group;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &user,
                                                   &group, &args));
  EXPECT_EQ(user, kDefaultRunAs);
  EXPECT_EQ(group, kDefaultRunAs);
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
        "func1": {
          "binds": ["%s"],
          "other_args": ["-A"]
        }
      })",
      file.value().c_str());
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  std::string user, group;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &user,
                                                   &group, &args));
  EXPECT_EQ(user, kDefaultRunAs);
  EXPECT_EQ(group, kDefaultRunAs);
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
        "func1": {
          "binds": ["%s,,1"],
          "other_args": ["-A"]
        }
      })",
      dir.value().c_str());
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  std::string user, group;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &user,
                                                   &group, &args));
  EXPECT_EQ(user, kDefaultRunAs);
  EXPECT_EQ(group, kDefaultRunAs);
  EXPECT_EQ(args.size(), 3);
  EXPECT_EQ(args[0], "-A");
  EXPECT_EQ(args[1], "-b");
  EXPECT_EQ(args[2], base::StringPrintf("%s,,1", dir.value().c_str()));
}

TEST(ProbeToolTest, GetValidMinijailArguments_SpecifyUser) {
  auto json_str = R"({
    "func1": {
      "user": "abc",
      "other_args": ["-A", "-B", "args"]
    }
  })";
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  std::string user, group;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &user,
                                                   &group, &args));
  EXPECT_EQ(user, "abc");
  EXPECT_EQ(group, kDefaultRunAs);
  EXPECT_EQ(args.size(), 3);
  EXPECT_EQ(args[0], "-A");
  EXPECT_EQ(args[1], "-B");
  EXPECT_EQ(args[2], "args");
}

TEST(ProbeToolTest, GetValidMinijailArguments_SpecifyGroup) {
  auto json_str = R"({
    "func1": {
      "group": "abc",
      "other_args": ["-A", "-B", "args"]
    }
  })";
  ProbeToolForTesting probe_tool(json_str);
  std::vector<std::string> args;
  std::string user, group;
  EXPECT_TRUE(probe_tool.GetValidMinijailArguments(nullptr, "func1", &user,
                                                   &group, &args));
  EXPECT_EQ(user, kDefaultRunAs);
  EXPECT_EQ(group, "abc");
  EXPECT_EQ(args.size(), 3);
  EXPECT_EQ(args[0], "-A");
  EXPECT_EQ(args[1], "-B");
  EXPECT_EQ(args[2], "args");
}

}  // namespace debugd
