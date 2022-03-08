// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>
#include <string>
#include <vector>

#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/executor/utils/sandboxed_process.h"

namespace diagnostics {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class MockSandboxedProcess : public SandboxedProcess {
 public:
  using SandboxedProcess::SandboxedProcess;

  MOCK_METHOD(void, BrilloProcessAddArg, (const std::string&), (override));
  MOCK_METHOD(bool, BrilloProcessStart, (), (override));
  MOCK_METHOD(bool, IsPathExists, (const base::FilePath&), (const override));
};

constexpr char kTestSeccompName[] = "test_seccomp.policy";
constexpr char kTestUser[] = "foo_user";
constexpr uint64_t kTestCapabilitiesMask = 0xa42;
constexpr char kTestCapabilitiesMaskHex[] = "0xa42";
constexpr char kTestReadOnlyFile[] = "/some/readonly/file";
constexpr char kTestReadOnlyFileNotExist[] = "/some/not/exist/readonly/file";
constexpr char kTestWritableFile[] = "/some/writable/file";
constexpr char kTestWritableFileMountFlag[] =
    "/some/writable/file,/some/writable/file,1";

TEST(SandboxedProcessTest, Default) {
  std::vector<std::string> expected_cmd{"ls", "-al"};
  MockSandboxedProcess process{
      /*command=*/expected_cmd,
      /*seccomp_filename=*/kTestSeccompName,
      /*user=*/kTestUser,
      /*capabilities_mask=*/kTestCapabilitiesMask,
      /*readonly_mount_points=*/
      {base::FilePath{kTestReadOnlyFile},
       base::FilePath{kTestReadOnlyFileNotExist}},
      /*writable_mount_points=*/{base::FilePath{kTestWritableFile}},
  };

  EXPECT_CALL(process, BrilloProcessStart()).WillOnce(Return(true));
  EXPECT_CALL(process, IsPathExists(base::FilePath{kTestReadOnlyFile}))
      .WillOnce(Return(true));
  EXPECT_CALL(process, IsPathExists(base::FilePath{kTestReadOnlyFileNotExist}))
      .WillOnce(Return(false));

  // These are minijail flags with string argument.
  const std::set<std::string> minijail_string_arg_flags{"-u", "-g", "-c", "-S",
                                                        "-b"};
  bool has_minijail_bin = false;
  std::vector<std::string> minijail_args;
  std::set<std::vector<std::string>> minijail_args_set;
  bool has_minijail_finish_flag = false;
  std::vector<std::string> cmd;
  EXPECT_CALL(process, BrilloProcessAddArg(_))
      .WillRepeatedly(Invoke([&](const std::string& arg) {
        if (!has_minijail_bin) {
          EXPECT_EQ(arg, kMinijailBinary);
          has_minijail_bin = true;
          return;
        }
        if (!has_minijail_finish_flag) {
          if (arg == "--") {
            has_minijail_finish_flag = true;
            return;
          }
          minijail_args.push_back(arg);
          if (!minijail_string_arg_flags.count(minijail_args[0]) ||
              minijail_args.size() == 2) {
            auto [unused, success] = minijail_args_set.insert(minijail_args);
            EXPECT_TRUE(success) << "Duplicated argument: "
                                 << base::JoinString(minijail_args, " ");
            minijail_args.clear();
          }
          return;
        }
        cmd.push_back(arg);
      }));
  EXPECT_TRUE(process.Start());
  EXPECT_EQ(cmd, expected_cmd);
  EXPECT_EQ(minijail_args_set,
            (std::set<std::vector<std::string>>{
                {"-v"},
                {"-r"},
                {"-l"},
                {"-e"},
                {"--uts"},
                {"-u", kTestUser},
                {"-g", kTestUser},
                {"-G"},
                {"-c", kTestCapabilitiesMaskHex},
                {"-S", base::FilePath{kSeccompPolicyDirectory}
                           .Append(kTestSeccompName)
                           .value()},
                {"-n"},
                {"-b", kTestReadOnlyFile},
                {"-b", kTestWritableFileMountFlag},
            }));
}

}  // namespace
}  // namespace diagnostics
