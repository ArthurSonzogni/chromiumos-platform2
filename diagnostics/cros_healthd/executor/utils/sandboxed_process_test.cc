// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/sandboxed_process.h"

#include <set>
#include <string>
#include <vector>

#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace diagnostics {
namespace {

using ::testing::_;
using ::testing::Contains;
using ::testing::Not;
using ::testing::Return;

class MockSandboxedProcess : public SandboxedProcess {
 public:
  using SandboxedProcess::SandboxedProcess;

  MOCK_METHOD(void, BrilloProcessAddArg, (const std::string&), (override));
  MOCK_METHOD(bool, BrilloProcessStart, (), (override));
  MOCK_METHOD(bool, IsPathExists, (const base::FilePath&), (const override));
  MOCK_METHOD(bool, IsDevMode, (), (const override));
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

class SandboxedProcessTest : public testing::Test {
 public:
  SandboxedProcessTest(const SandboxedProcessTest&) = delete;
  SandboxedProcessTest& operator=(const SandboxedProcessTest&) = delete;

 protected:
  SandboxedProcessTest() = default;

  // Sets up the expect calls needed to correctly get minijail arguments and
  // commands.
  void SetUpExpectCallForMinijailParsing(MockSandboxedProcess& process) {
    has_minijail_bin_ = false;
    has_minijail_finish_flag_ = false;
    minijail_args_set_ = std::set<std::vector<std::string>>{};
    minijail_args_ = std::vector<std::string>{};
    cmd_ = std::vector<std::string>{};
    EXPECT_CALL(process, BrilloProcessStart()).WillOnce(Return(true));
    ON_CALL(process, IsPathExists(base::FilePath{kTestReadOnlyFile}))
        .WillByDefault(Return(true));
    ON_CALL(process, IsPathExists(base::FilePath{kTestReadOnlyFileNotExist}))
        .WillByDefault(Return(false));
    ON_CALL(process, IsPathExists(base::FilePath{kTestWritableFile}))
        .WillByDefault(Return(true));
    EXPECT_CALL(process, BrilloProcessAddArg(_))
        .WillRepeatedly([&](const std::string& arg) {
          // These are minijail flags with string argument.
          const std::set<std::string> kMinijailStringArgFlags{
              "-u", "-g", "-c",           "-S",          "-b",
              "-P", "-k", "--fs-path-rw", "--fs-path-ro"};
          if (!has_minijail_bin_) {
            EXPECT_EQ(arg, kMinijailBinary);
            has_minijail_bin_ = true;
            return;
          }
          if (!has_minijail_finish_flag_) {
            if (arg == "--") {
              has_minijail_finish_flag_ = true;
              return;
            }
            minijail_args_.push_back(arg);
            if (!kMinijailStringArgFlags.contains(minijail_args_[0]) ||
                minijail_args_.size() == 2) {
              auto [unused, success] =
                  minijail_args_set_.insert(minijail_args_);
              EXPECT_TRUE(success) << "Duplicated argument: "
                                   << base::JoinString(minijail_args_, " ");
              minijail_args_.clear();
            }
            return;
          }
          cmd_.push_back(arg);
        });
  }

 protected:
  std::set<std::vector<std::string>> minijail_args_set_;
  std::vector<std::string> cmd_;

 private:
  bool has_minijail_bin_;
  bool has_minijail_finish_flag_;
  std::vector<std::string> minijail_args_;
};

TEST_F(SandboxedProcessTest, Default) {
  std::vector<std::string> expected_cmd{"ls", "-al"};
  MockSandboxedProcess process{
      /*command=*/expected_cmd,
      /*seccomp_filename=*/kTestSeccompName,
      SandboxedProcess::Options{
          .user = kTestUser,
          .capabilities_mask = kTestCapabilitiesMask,
          .mount_points = {SandboxedProcess::MountPoint{
                               .path = base::FilePath{kTestReadOnlyFile}},
                           SandboxedProcess::MountPoint{
                               .path =
                                   base::FilePath{kTestReadOnlyFileNotExist}},
                           SandboxedProcess::MountPoint{
                               .path = base::FilePath{kTestWritableFile},
                               .writable = true,
                           }},
      }};

  SetUpExpectCallForMinijailParsing(process);

  auto expected_minijail_arg_set = std::set<std::vector<std::string>>{
      {"-P", "/mnt/empty"},
      {"-v"},
      {"-Kslave"},
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
      {"-b", "/"},
      {"-b", "/dev/log"},
      {"-d"},
      {"-k", "tmpfs,/tmp,tmpfs"},
      {"-k", "tmpfs,/proc,tmpfs"},
      {"-k", "tmpfs,/run,tmpfs"},
      {"-k", "tmpfs,/sys,tmpfs"},
      {"-k", "tmpfs,/var,tmpfs"},
      {"--fs-default-paths"},
      {"--fs-path-ro", kTestReadOnlyFile},
      {"--fs-path-rw", kTestWritableFile},
  };
  for (const auto& mount_dev_node : kMountDevNodes) {
    expected_minijail_arg_set.insert(
        {"--fs-path-rw", std::string(mount_dev_node)});
  }

  EXPECT_TRUE(process.Start());
  EXPECT_EQ(cmd_, expected_cmd);
  EXPECT_EQ(minijail_args_set_, expected_minijail_arg_set);
}

TEST_F(SandboxedProcessTest, NoNetworkNamespace) {
  std::vector<std::string> expected_cmd{"ls", "-al"};
  MockSandboxedProcess process{
      /*command=*/expected_cmd,
      /*seccomp_filename=*/kTestSeccompName,
      SandboxedProcess::Options{
          .user = kTestUser,
          .capabilities_mask = kTestCapabilitiesMask,
          .mount_points = {SandboxedProcess::MountPoint{
                               .path = base::FilePath{kTestReadOnlyFile}},
                           SandboxedProcess::MountPoint{
                               .path =
                                   base::FilePath{kTestReadOnlyFileNotExist}},
                           SandboxedProcess::MountPoint{
                               .path = base::FilePath{kTestWritableFile},
                               .writable = true,
                           }},
          .enter_network_namespace = false,
      }};

  SetUpExpectCallForMinijailParsing(process);

  EXPECT_TRUE(process.Start());
  EXPECT_EQ(cmd_, expected_cmd);
  EXPECT_THAT(minijail_args_set_,
              Not(Contains(std::vector<std::string>{"-e"})));
}

TEST_F(SandboxedProcessTest, MOUNT_DLC) {
  std::vector<std::string> expected_cmd{"ls", "-al"};
  MockSandboxedProcess process{
      /*command=*/expected_cmd,
      /*seccomp_filename=*/kTestSeccompName,
      SandboxedProcess::Options{
          .user = kTestUser,
          .capabilities_mask = kTestCapabilitiesMask,
          .mount_points = {SandboxedProcess::MountPoint{
                               .path = base::FilePath{kTestReadOnlyFile}},
                           SandboxedProcess::MountPoint{
                               .path =
                                   base::FilePath{kTestReadOnlyFileNotExist}},
                           SandboxedProcess::MountPoint{
                               .path = base::FilePath{kTestWritableFile},
                               .writable = true,
                           }},
          .mount_dlc = true,
      }};

  SetUpExpectCallForMinijailParsing(process);

  EXPECT_TRUE(process.Start());
  EXPECT_EQ(cmd_, expected_cmd);
  EXPECT_THAT(
      minijail_args_set_,
      Contains(std::vector<std::string>{
          "-k", "/run/imageloader,/run/imageloader,none,MS_BIND|MS_REC"}));
}

TEST_F(SandboxedProcessTest, SkipSandbox) {
  std::vector<std::string> expected_cmd{"ls", "-al"};
  MockSandboxedProcess process{/*command=*/expected_cmd,
                               /*seccomp_filename=*/kTestSeccompName,
                               /*options=*/
                               SandboxedProcess::Options{
                                   .skip_sandbox = true,
                               }};  // namespace

  EXPECT_CALL(process, IsDevMode()).WillRepeatedly(Return(true));
  EXPECT_CALL(process, BrilloProcessStart()).WillRepeatedly(Return(true));
  EXPECT_CALL(process, BrilloProcessAddArg(_))
      .WillRepeatedly([&](const std::string& arg) { cmd_.push_back(arg); });
  EXPECT_TRUE(process.Start());
  EXPECT_EQ(cmd_, expected_cmd);
  EXPECT_EQ(minijail_args_set_.size(), 0);
}

TEST_F(SandboxedProcessTest, SkipSandboxInNormalMode) {
  std::vector<std::string> expected_cmd{"ls", "-al"};
  MockSandboxedProcess process{/*command=*/expected_cmd,
                               /*seccomp_filename=*/kTestSeccompName,
                               /*options=*/
                               SandboxedProcess::Options{
                                   .skip_sandbox = true,
                               }};

  EXPECT_CALL(process, IsDevMode()).WillRepeatedly(Return(false));
  EXPECT_FALSE(process.Start());
}

TEST_F(SandboxedProcessTest, FailToStartOnMissingRequiredMountPoint) {
  std::vector<std::string> expected_cmd{"ls", "-al"};
  MockSandboxedProcess process{
      /*command=*/expected_cmd,
      /*seccomp_filename=*/kTestSeccompName,
      SandboxedProcess::Options{
          .user = kTestUser,
          .capabilities_mask = kTestCapabilitiesMask,
          .mount_points = {SandboxedProcess::MountPoint{
              .path = base::FilePath{kTestReadOnlyFileNotExist},
              .writable = false,
              .is_required = true,
          }}}};

  ON_CALL(process, IsPathExists(base::FilePath{kTestReadOnlyFileNotExist}))
      .WillByDefault(Return(false));

  EXPECT_FALSE(process.Start());
}

// Landlock is not applied if .enable_landlock is false.
TEST_F(SandboxedProcessTest, DisableLandlockFeature) {
  std::vector<std::string> expected_cmd{"ls", "-al"};
  MockSandboxedProcess process{/*command=*/expected_cmd,
                               /*seccomp_filename=*/kTestSeccompName,
                               /*options=*/
                               SandboxedProcess::Options{
                                   .enable_landlock = false,
                               }};

  SetUpExpectCallForMinijailParsing(process);

  EXPECT_TRUE(process.Start());
  EXPECT_EQ(cmd_, expected_cmd);
  EXPECT_THAT(minijail_args_set_,
              Not(Contains(std::vector<std::string>{"--fs-default-paths"})));
}

// Landlock is applied if .enable_landlock is true.
TEST_F(SandboxedProcessTest, EnableLandlockFeature) {
  std::vector<std::string> expected_cmd{"ls", "-al"};
  MockSandboxedProcess process{/*command=*/expected_cmd,
                               /*seccomp_filename=*/kTestSeccompName,
                               /*options=*/
                               SandboxedProcess::Options{
                                   .enable_landlock = true,
                               }};

  SetUpExpectCallForMinijailParsing(process);

  EXPECT_TRUE(process.Start());
  EXPECT_EQ(cmd_, expected_cmd);
  EXPECT_THAT(minijail_args_set_,
              Contains(std::vector<std::string>{"--fs-default-paths"}));
}

}  // namespace
}  // namespace diagnostics
