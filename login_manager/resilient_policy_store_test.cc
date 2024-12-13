// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/resilient_policy_store.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>
#include <policy/device_policy_impl.h>

#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "login_manager/fake_system_utils.h"
#include "login_manager/mock_metrics.h"

namespace em = enterprise_management;

namespace login_manager {

class ResilientPolicyStoreTest : public ::testing::Test {
 public:
  ResilientPolicyStoreTest() = default;
  ResilientPolicyStoreTest(const ResilientPolicyStoreTest&) = delete;
  ResilientPolicyStoreTest& operator=(const ResilientPolicyStoreTest&) = delete;

  ~ResilientPolicyStoreTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(system_utils_.CreateDir(tmpfile_.DirName()));
    EXPECT_FALSE(system_utils_.Exists(tmpfile_));
  }

  void CheckExpectedPolicy(PolicyStore* store,
                           const em::PolicyFetchResponse& policy) {
    std::string serialized;
    ASSERT_TRUE(policy.SerializeToString(&serialized));
    std::string serialized_from;
    ASSERT_TRUE(store->Get().SerializeToString(&serialized_from));
    EXPECT_EQ(serialized, serialized_from);
  }

  const base::FilePath tmpfile_{policy::DevicePolicyImpl::kPolicyPath};
  FakeSystemUtils system_utils_;
};

TEST_F(ResilientPolicyStoreTest, LoadResilientMissingPolicy) {
  MockMetrics metrics;
  ResilientPolicyStore store(&system_utils_, tmpfile_, &metrics);
  ASSERT_TRUE(store.EnsureLoadedOrCreated());
}

TEST_F(ResilientPolicyStoreTest, CheckDeleteAtLoadResilient) {
  MockMetrics metrics;
  ResilientPolicyStore store(&system_utils_, tmpfile_, &metrics);

  enterprise_management::PolicyFetchResponse policy;
  enterprise_management::PolicyData policy_data;
  enterprise_management::ChromeDeviceSettingsProto settings;
  policy_data.set_username("test_user");
  policy_data.set_request_token("secret_token");
  std::string settings_str;
  settings.SerializeToString(&settings_str);
  policy_data.set_policy_value(settings_str);
  policy.set_policy_data(policy_data.SerializeAsString());

  store.Set(policy);

  ASSERT_TRUE(store.Persist());
  CheckExpectedPolicy(&store, policy);

  // Create the file with next index, containing some invalid data.
  base::FilePath policy_path2 = base::FilePath(tmpfile_.value() + ".2");
  ASSERT_TRUE(system_utils_.WriteStringToFile(policy_path2, "invalid_data"));
  ASSERT_TRUE(system_utils_.Exists(policy_path2));

  // Check that LoadResilient succeeds and ignores the last file.
  ASSERT_TRUE(store.EnsureLoadedOrCreated());
  CheckExpectedPolicy(&store, policy);

  // Check that the last file was deleted.
  ASSERT_FALSE(system_utils_.Exists(policy_path2));
}

TEST_F(ResilientPolicyStoreTest, CheckCleanupFromPersistResilient) {
  MockMetrics metrics;
  ResilientPolicyStore store(&system_utils_, tmpfile_, &metrics);
  enterprise_management::PolicyFetchResponse policy;
  policy.set_error_message("foo");
  store.Set(policy);

  base::FilePath policy_path1(tmpfile_.value() + ".1");
  base::FilePath policy_path2(tmpfile_.value() + ".2");
  base::FilePath policy_path3(tmpfile_.value() + ".3");
  base::FilePath policy_path4(tmpfile_.value() + ".4");

  ASSERT_TRUE(store.Persist());
  CheckExpectedPolicy(&store, policy);
  ASSERT_TRUE(system_utils_.Exists(policy_path1));

  // Emulate restart of the device by cleaning up /run/session_manager.
  ASSERT_TRUE(system_utils_.ClearDirectoryContents(
      base::FilePath("/run/session_manager")));
  policy.set_error_message("foo2");
  store.Set(policy);
  ASSERT_TRUE(store.Persist());
  ASSERT_TRUE(system_utils_.Exists(policy_path2));

  // Create the file with next index, containing some invalid data.
  ASSERT_TRUE(system_utils_.WriteStringToFile(policy_path3, "invalid_data"));

  // Change the policy data and store again, having a new file.
  ASSERT_TRUE(system_utils_.ClearDirectoryContents(
      base::FilePath("/run/session_manager")));
  policy.set_error_message("foo");
  store.Set(policy);
  ASSERT_TRUE(store.Persist());
  ASSERT_TRUE(system_utils_.Exists(policy_path4));

  // The last Persist resilient should have done the cleanup and removed the
  // oldest file since the limit was reached.
  ASSERT_FALSE(system_utils_.Exists(policy_path1));
  ASSERT_TRUE(system_utils_.Exists(policy_path2));
  ASSERT_TRUE(system_utils_.Exists(policy_path3));
}

}  // namespace login_manager
