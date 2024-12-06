// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/policy_store.h"

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>

#include "login_manager/fake_system_utils.h"

namespace em = enterprise_management;

namespace login_manager {

class PolicyStoreTest : public ::testing::Test {
 public:
  PolicyStoreTest() = default;
  PolicyStoreTest(const PolicyStoreTest&) = delete;
  PolicyStoreTest& operator=(const PolicyStoreTest&) = delete;
  ~PolicyStoreTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(system_utils_.CreateDir(tmpfile_.DirName()));
  }

  void CheckExpectedPolicy(PolicyStore* store,
                           const em::PolicyFetchResponse& policy) {
    std::string serialized;
    ASSERT_TRUE(policy.SerializeToString(&serialized));
    std::string serialized_from;
    ASSERT_TRUE(store->Get().SerializeToString(&serialized_from));
    EXPECT_EQ(serialized, serialized_from);
  }

  base::FilePath tmpfile_{"/tmp/foo/bar"};
  FakeSystemUtils system_utils_;
};

TEST_F(PolicyStoreTest, InitialEmptyStore) {
  PolicyStore store(&system_utils_, tmpfile_);
  CheckExpectedPolicy(&store, em::PolicyFetchResponse());
}

TEST_F(PolicyStoreTest, CreateEmptyStore) {
  PolicyStore store(&system_utils_, tmpfile_);
  ASSERT_TRUE(store.EnsureLoadedOrCreated());  // Should create an empty policy.
  CheckExpectedPolicy(&store, em::PolicyFetchResponse());
}

TEST_F(PolicyStoreTest, FailBrokenStore) {
  // Create a bad file.
  ASSERT_TRUE(system_utils_.WriteStringToFile(tmpfile_, ""));
  PolicyStore store(&system_utils_, tmpfile_);
  ASSERT_FALSE(store.EnsureLoadedOrCreated());
}

TEST_F(PolicyStoreTest, VerifyPolicyStorage) {
  enterprise_management::PolicyFetchResponse policy;
  policy.set_error_message("policy");
  PolicyStore store(&system_utils_, tmpfile_);
  store.Set(policy);
  CheckExpectedPolicy(&store, policy);
}

TEST_F(PolicyStoreTest, VerifyPolicyUpdate) {
  PolicyStore store(&system_utils_, tmpfile_);
  enterprise_management::PolicyFetchResponse policy;
  policy.set_error_message("policy");
  store.Set(policy);
  CheckExpectedPolicy(&store, policy);

  enterprise_management::PolicyFetchResponse new_policy;
  new_policy.set_error_message("new policy");
  store.Set(new_policy);
  CheckExpectedPolicy(&store, new_policy);
}

TEST_F(PolicyStoreTest, LoadStoreFromDisk) {
  PolicyStore store(&system_utils_, tmpfile_);
  enterprise_management::PolicyFetchResponse policy;
  policy.set_error_message("policy");
  store.Set(policy);
  ASSERT_TRUE(store.Persist());
  CheckExpectedPolicy(&store, policy);

  PolicyStore store2(&system_utils_, tmpfile_);
  ASSERT_TRUE(store2.EnsureLoadedOrCreated());
  CheckExpectedPolicy(&store2, policy);
}

}  // namespace login_manager
