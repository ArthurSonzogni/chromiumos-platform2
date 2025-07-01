// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_MOCK_SYSTEM_UTILS_H_
#define LOGIN_MANAGER_MOCK_SYSTEM_UTILS_H_

#include <stdint.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <policy/device_policy_impl.h>

#include "login_manager/system_utils.h"

namespace login_manager {

class MockSystemUtils : public SystemUtils {
 public:
  MockSystemUtils();
  MockSystemUtils(const MockSystemUtils&) = delete;
  MockSystemUtils& operator=(const MockSystemUtils&) = delete;

  ~MockSystemUtils() override;

  MOCK_METHOD(int, kill, (pid_t, uid_t, int), (override));
  MOCK_METHOD(time_t, time, (time_t*), (override));  // NOLINT
  MOCK_METHOD(pid_t, fork, (), (override));
  MOCK_METHOD(int, close, (int), (override));
  MOCK_METHOD(int, chdir, (const base::FilePath&), (override));
  MOCK_METHOD(pid_t, setsid, (), (override));
  MOCK_METHOD(int,
              execve,
              (const base::FilePath&, const char* const[], const char* const[]),
              (override));
  MOCK_METHOD(bool, EnterNewMountNamespace, (), (override));
  MOCK_METHOD(bool,
              GetAppOutput,
              (const std::vector<std::string>&, std::string*),
              (override));
  MOCK_METHOD(DevModeState, GetDevModeState, (), (override));
  MOCK_METHOD(VmState, GetVmState, (), (override));
  MOCK_METHOD(bool, ProcessGroupIsGone, (pid_t, base::TimeDelta), (override));
  MOCK_METHOD(bool, ProcessIsGone, (pid_t, base::TimeDelta), (override));
  MOCK_METHOD(pid_t, Wait, (pid_t, base::TimeDelta, int*), (override));
  MOCK_METHOD(std::optional<int64_t>,
              GetFileSize,
              (const base::FilePath&),
              (override));
  MOCK_METHOD(bool, Exists, (const base::FilePath&), (override));
  MOCK_METHOD(bool, DirectoryExists, (const base::FilePath&), (override));
  MOCK_METHOD(bool, CreateDir, (const base::FilePath&), (override));
  MOCK_METHOD(bool,
              EnumerateFiles,
              (const base::FilePath&, int, std::vector<base::FilePath>*),
              (override));
  MOCK_METHOD(bool,
              GetUniqueFilenameInWriteOnlyTempDir,
              (base::FilePath*),
              (override));
  MOCK_METHOD(bool, RemoveFile, (const base::FilePath&), (override));
  MOCK_METHOD(int64_t,
              AmountOfFreeDiskSpace,
              (const base::FilePath&),
              (override));
  MOCK_METHOD(bool,
              GetGidAndGroups,
              (uid_t, gid_t*, std::vector<gid_t>*),
              (override));

  MOCK_METHOD(std::optional<std::vector<uint8_t>>,
              ReadFileToBytes,
              (const base::FilePath&),
              (override));
  MOCK_METHOD(bool,
              ReadFileToString,
              (const base::FilePath&, std::string*),
              (override));
  MOCK_METHOD(bool,
              WriteStringToFile,
              (const base::FilePath&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              WriteFileAtomically,
              (const base::FilePath&,
               base::span<const uint8_t>,
               mode_t,
               brillo::WriteFileOptions),
              (override));

  MOCK_METHOD(policy::LoadPolicyResult,
              LoadPolicyFromPath,
              (const base::FilePath&,
               std::string*,
               enterprise_management::PolicyFetchResponse*),
              (override));
  MOCK_METHOD(std::unique_ptr<policy::DevicePolicyImpl>,
              CreateDevicePolicy,
              (),
              (override));
  MOCK_METHOD((std::map<int, base::FilePath>),
              GetSortedResilientPolicyFilePaths,
              (const base::FilePath&),
              (override));

  MOCK_METHOD(bool,
              ChangeBlockedSignals,
              (int, const std::vector<int>&),
              (override));

  MOCK_METHOD(bool,
              LaunchAndWait,
              (const std::vector<std::string>&, int*),
              (override));
  MOCK_METHOD(bool,
              RunInMinijail,
              (const ScopedMinijail& jail,
               const std::vector<std::string>&,
               const std::vector<std::string>&,
               pid_t*),
              (override));
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_SYSTEM_UTILS_H_
