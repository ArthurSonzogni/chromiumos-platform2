// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pwd.h>
#include <sys/types.h>

#include <map>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mojo_service_manager/daemon/service_policy_loader.h"
#include "mojo_service_manager/daemon/service_policy_test_util.h"

namespace chromeos::mojo_service_manager {
namespace {

class FakeDelegate : public LoadServicePolicyDelegate {
 public:
  FakeDelegate();
  FakeDelegate(const FakeDelegate&) = delete;
  FakeDelegate& operator=(const FakeDelegate&) = delete;
  ~FakeDelegate() override;

  // LoadServicePolicyDelegate overrides.
  const struct passwd* GetPWNam(const char* name) const override;

  // Sets this to change `passwd` returned by `GetPWNam`.
  std::map<std::string, struct passwd> passwd_map_;
  // Sets this to raise a EINTR for `GetPWNam`. Use static variable so it can be
  // modified in a const method.
  static bool raise_a_eintr_for_getpwnam_;
};

bool FakeDelegate::raise_a_eintr_for_getpwnam_ = false;

FakeDelegate::FakeDelegate() = default;

FakeDelegate::~FakeDelegate() = default;

const struct passwd* FakeDelegate::GetPWNam(const char* name) const {
  if (raise_a_eintr_for_getpwnam_) {
    raise_a_eintr_for_getpwnam_ = false;
    errno = EINTR;
    return nullptr;
  }
  const auto it = passwd_map_.find(name);
  if (it != passwd_map_.end()) {
    errno = 0;
    return &(it->second);
  }
  errno = ESRCH;
  return nullptr;
}

class ServicePolicyLoaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EXPECT_TRUE(temp_dir_.CreateUniqueTempDir());
    SetLoadServicePolicyDelegateForTest(&fake_delegate_);
  }

  void TearDown() override { SetLoadServicePolicyDelegateForTest(nullptr); }

  const base::FilePath& root_dir() { return temp_dir_.GetPath(); }

  base::FilePath CreateTestFile(const std::string& content) {
    return CreateTestFileInDirectory(root_dir(), content);
  }

  base::FilePath CreateTestFileInDirectory(const base::FilePath& dir,
                                           const std::string& content) {
    base::FilePath file;
    CHECK(CreateDirectory(dir));
    CHECK(CreateTemporaryFileInDir(dir, &file));
    CHECK(base::WriteFile(file, content));
    return file;
  }

  FakeDelegate fake_delegate_;

 private:
  base::ScopedTempDir temp_dir_;
};

TEST_F(ServicePolicyLoaderTest, Parse) {
  fake_delegate_.passwd_map_["user_1"].pw_uid = 1;
  fake_delegate_.passwd_map_["user_2"].pw_uid = 2;

  // Test a general policy file.
  auto policy_map = ParseServicePolicyFromString(R"JSON(
        [
          // Comment
          {
            "user": "user_1",
            "request": [
              "FooService",
              "BarService",
            ]
          },
          {
            "user": "user_2",
            "own": [
              "FooService",
            ],
          }
        ]
      )JSON");
  EXPECT_TRUE(policy_map);
  EXPECT_EQ(policy_map.value(), CreateServicePolicyMapForTest(
                                    {{"FooService", {2, {1}}},
                                     {"BarService", {std::nullopt, {1}}}}));
}

TEST_F(ServicePolicyLoaderTest, ParseEINTR) {
  fake_delegate_.passwd_map_["user_1"].pw_uid = 1;
  FakeDelegate::raise_a_eintr_for_getpwnam_ = true;

  // Test a general policy file.
  auto policy_map = ParseServicePolicyFromString(R"JSON(
        [{"user": "user_1", "request": [ "FooService" ]}]
      )JSON");
  EXPECT_TRUE(policy_map);
  EXPECT_EQ(policy_map.value(), CreateServicePolicyMapForTest(
                                    {{"FooService", {std::nullopt, {1}}}}));
}

TEST_F(ServicePolicyLoaderTest, MergeMultiple) {
  fake_delegate_.passwd_map_["user_1"].pw_uid = 1;
  fake_delegate_.passwd_map_["user_2"].pw_uid = 2;

  // Test multiple rules can be merged.
  auto policy_map = ParseServicePolicyFromString(R"JSON(
        [
          {"user": "user_1", "request": [ "FooService" ]},
          {"user": "user_1", "own": [ "FooService" ]},
          {"user": "user_2", "request": [ "FooService" ]},
        ]
      )JSON");
  EXPECT_TRUE(policy_map);
  EXPECT_EQ(policy_map.value(),
            CreateServicePolicyMapForTest({{"FooService", {1, {1, 2}}}}));
}

TEST_F(ServicePolicyLoaderTest, ParseBothUserAndIdentity) {
  fake_delegate_.passwd_map_["user_1"].pw_uid = 1;

  // Test a general policy file.
  auto policy_map = ParseServicePolicyFromString(R"JSON(
        [
          {"identity": "user_a", "request": [ "FooService" ]},
          {"user": "user_1", "own": [ "FooService" ]},
        ]
      )JSON");
  EXPECT_TRUE(policy_map);
  EXPECT_EQ(policy_map.value(),
            CreateServicePolicyMapForTest({{"FooService", {1, {}}}},
                                          {{"FooService", {"", {"user_a"}}}}));
}

TEST_F(ServicePolicyLoaderTest, ParseIdentity) {
  fake_delegate_.passwd_map_["user_1"].pw_uid = 1;

  {
    // Test a general policy file.
    auto policy_map = ParseServicePolicyFromString(R"JSON(
        [
          // Comment
          {
            "identity": "user_a",
            "request": [
              "FooService",
              "BarService",
            ]
          },
          {
            "identity": "user_b",
            "own": [
              "FooService",
            ],
          }
        ]
      )JSON");
    EXPECT_TRUE(policy_map);
    EXPECT_EQ(policy_map.value(), CreateServicePolicyMapForTest(
                                      {{"FooService", {"user_b", {"user_a"}}},
                                       {"BarService", {"", {"user_a"}}}}));
  }
  {
    // Test multiple rules can be merged.
    auto policy_map = ParseServicePolicyFromString(R"JSON(
        [
          {"identity": "user_a", "request": [ "FooService" ]},
          {"identity": "user_a", "own": [ "FooService" ]},
          {"identity": "user_b", "request": [ "FooService" ]},
          {"user": "user_1", "request": [ "FooService" ]},
        ]
      )JSON");
    EXPECT_TRUE(policy_map);
    EXPECT_EQ(policy_map.value(),
              CreateServicePolicyMapForTest(
                  {{"FooService", {std::nullopt, {1}}}},
                  {{"FooService", {"user_a", {"user_a", "user_b"}}}}));
  }
}

TEST_F(ServicePolicyLoaderTest, Invalid) {
  fake_delegate_.passwd_map_["user_1"].pw_uid = 1;
  fake_delegate_.passwd_map_["user_2"].pw_uid = 2;

  // Policy list should be a list, not dict.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      {}
    )JSON"));
  // Policy should be a dict, not int.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [42]
    )JSON"));
  // Found an unexpected field.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"user": "user_1", "own":["ServiceA"], "unexpected":[]}]
    )JSON"));
  // User should be a string, not dict.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"user":{}, "own":["ServiceA"], "request":["ServiceA"]}]
    )JSON"));
  // No user.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"own":["ServiceA"], "request":["ServiceA"]}]
    )JSON"));
  // User not found.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"user": "user_not_found", "own":["ServiceA"], "request":["ServiceA"]}]
    )JSON"));
  // Own/request should be a list, not dict.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"user":"user_1", "own":{}, "request":["ServiceA"]}]
    )JSON"));
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"user":"user_1", "own":["ServiceA"], "request":{}}]
    )JSON"));
  // Service name should be a string, not dict.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"user":"user_1", "own":[{}], "request":["ServiceA"]}]
    )JSON"));
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"user":"user_1", "own":["ServiceA"], "request":[{}]}]
    )JSON"));
  // Cannot own "ServiceA" twice.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"user":"user_1", "own":["ServiceA", "ServiceA"]}]
    )JSON"));
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [
        {"user":"user_1", "own":["ServiceA"]},
        {"user":"user_2", "own":["ServiceA"]}
      ]
    )JSON"));
}

TEST_F(ServicePolicyLoaderTest, InvalidIdentity) {
  // Identity should be a string, not dict.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"identity":{}, "own":["ServiceA"], "request":["ServiceA"]}]
    )JSON"));
  // No identity.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"own":["ServiceA"], "request":["ServiceA"]}]
    )JSON"));
  // Cannot have both user and identity.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
      [{"user": "user_1", "identity": "user_a", "own":["ServiceA"]}]
    )JSON"));
  // Cannot own "ServiceA" twice.
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
        [
          {"identity": "user_a", "own": [ "ServiceA" ]},
          {"identity": "user_b", "own": [ "ServiceA" ]},
        ]
      )JSON"));
  // Cannot own "ServiceA" twice (user and identity field).
  EXPECT_FALSE(ParseServicePolicyFromString(R"JSON(
        [
          {"identity": "user_a", "own": [ "ServiceA" ]},
          {"user": "user_1", "own": [ "ServiceA" ]},
        ]
      )JSON"));
}

TEST_F(ServicePolicyLoaderTest, LoadFile) {
  fake_delegate_.passwd_map_["user_1"].pw_uid = 1;
  fake_delegate_.passwd_map_["user_2"].pw_uid = 2;
  fake_delegate_.passwd_map_["user_3"].pw_uid = 3;
  fake_delegate_.passwd_map_["user_4"].pw_uid = 4;

  // The last rule should fail(no user field) and the whole file should not
  // be loaded.
  EXPECT_FALSE(LoadServicePolicyFile(CreateTestFile(R"JSON(
      [
        {"user":"user_1","own":["ServiceA"],"request":["ServiceA"]},
        {"user":"user_2","own":["ServiceB"],"request":["ServiceB"]},
        {}
      ]
    )JSON")));
  auto policy_map = LoadServicePolicyFile(CreateTestFile(R"JSON(
      [
        {"user":"user_3","own":["ServiceC"],"request":["ServiceC"]},
        {"user":"user_4","own":["ServiceD"],"request":["ServiceD"]},
      ]
    )JSON"));
  EXPECT_TRUE(policy_map);
  EXPECT_EQ(policy_map.value(), CreateServicePolicyMapForTest({
                                    {"ServiceC", {3, {3}}},
                                    {"ServiceD", {4, {4}}},
                                }));
}

TEST_F(ServicePolicyLoaderTest, LoadDirectory) {
  fake_delegate_.passwd_map_["user_1"].pw_uid = 1;
  fake_delegate_.passwd_map_["user_2"].pw_uid = 2;
  fake_delegate_.passwd_map_["user_3"].pw_uid = 3;
  fake_delegate_.passwd_map_["user_4"].pw_uid = 4;
  fake_delegate_.passwd_map_["user_5"].pw_uid = 5;

  CreateTestFile(R"JSON(
      [
        {"user":"user_1","own":["ServiceA"],"request":["ServiceA"]},
        {"user":"user_2","own":["ServiceB"],"request":["ServiceB"]},
      ]
    )JSON");
  CreateTestFile(R"JSON(
      [
        {"user":"user_3","request":["ServiceA"]},
        {"user":"user_2","own":["ServiceC"],"request":["ServiceC"]},
      ]
    )JSON");

  ServicePolicyMap policy_map;
  EXPECT_TRUE(LoadAllServicePolicyFileFromDirectory(root_dir(), &policy_map));
  EXPECT_EQ(policy_map, CreateServicePolicyMapForTest({
                            {"ServiceA", {1, {1, 3}}},
                            {"ServiceB", {2, {2}}},
                            {"ServiceC", {2, {2}}},
                        }));

  // Won't be loaded because the last rule doesn't have user field.
  CreateTestFile(R"JSON(
      [
        {"user":"user_4","own":["ServiceD"],"request":["ServiceD"]},
        {}
      ]
    )JSON");
  // False because a file cannot be loaded.
  EXPECT_FALSE(LoadAllServicePolicyFileFromDirectory(root_dir(), &policy_map));
  EXPECT_EQ(policy_map, CreateServicePolicyMapForTest({
                            {"ServiceA", {1, {1, 3}}},
                            {"ServiceB", {2, {2}}},
                            {"ServiceC", {2, {2}}},
                        }));

  // Test load one more file and merge into current policy map.
  CreateTestFile(R"JSON(
      [
        {"user":"user_5","own":["ServiceE"],"request":["ServiceE"]},
      ]
    )JSON");
  // False because a file cannot be loaded.
  EXPECT_FALSE(LoadAllServicePolicyFileFromDirectory(root_dir(), &policy_map));
  EXPECT_EQ(policy_map, CreateServicePolicyMapForTest({
                            {"ServiceA", {1, {1, 3}}},
                            {"ServiceB", {2, {2}}},
                            {"ServiceC", {2, {2}}},
                            {"ServiceE", {5, {5}}},
                        }));
}

TEST_F(ServicePolicyLoaderTest, LoadDirectoryMergeFail) {
  fake_delegate_.passwd_map_["user_1"].pw_uid = 1;

  // Load will fail because "ServiceA" is owned twice.
  CreateTestFile(R"JSON(
      [
        {"user":"user_1","own":["ServiceA"],"request":["ServiceA"]},
      ]
    )JSON");
  CreateTestFile(R"JSON(
      [
        {"user":"user_1","own":["ServiceA"],"request":["ServiceA"]},
      ]
    )JSON");

  ServicePolicyMap policy_map;
  EXPECT_FALSE(LoadAllServicePolicyFileFromDirectory(root_dir(), &policy_map));
}

TEST_F(ServicePolicyLoaderTest, LoadDirectories) {
  fake_delegate_.passwd_map_["user_1"].pw_uid = 1;
  fake_delegate_.passwd_map_["user_2"].pw_uid = 2;

  const auto dir_a = root_dir().Append("a");
  const auto dir_b = root_dir().Append("b");
  CreateTestFileInDirectory(dir_a, R"JSON(
      [
        {"user":"user_1","own":["ServiceA"],"request":["ServiceA"]},
      ]
    )JSON");
  CreateTestFileInDirectory(dir_b, R"JSON(
      [
        {"user":"user_2","own":["ServiceB"],"request":["ServiceB"]},
      ]
    )JSON");
  ServicePolicyMap policy_map;
  EXPECT_TRUE(
      LoadAllServicePolicyFileFromDirectories({dir_a, dir_b}, &policy_map));
  EXPECT_EQ(policy_map, CreateServicePolicyMapForTest({
                            {"ServiceA", {1, {1}}},
                            {"ServiceB", {2, {2}}},
                        }));
}

TEST_F(ServicePolicyLoaderTest, LoadDirectoriesMergeFail) {
  fake_delegate_.passwd_map_["user_1"].pw_uid = 1;
  fake_delegate_.passwd_map_["user_2"].pw_uid = 2;

  // Load will fail because "ServiceA" is owned twice.
  const auto dir_a = root_dir().Append("a");
  const auto dir_b = root_dir().Append("b");
  CreateTestFileInDirectory(dir_a, R"JSON(
      [
        {"user":"user_1","own":["ServiceA"],"request":["ServiceA"]},
      ]
    )JSON");
  CreateTestFileInDirectory(dir_b, R"JSON(
      [
        {"user":"user_2","own":["ServiceA"],"request":["ServiceA"]},
      ]
    )JSON");

  ServicePolicyMap policy_map;
  EXPECT_FALSE(
      LoadAllServicePolicyFileFromDirectories({dir_a, dir_b}, &policy_map));
}

TEST_F(ServicePolicyLoaderTest, LoadDirectoriesKeepLoadingWhenFail) {
  fake_delegate_.passwd_map_["user_2"].pw_uid = 2;

  const auto dir_a = root_dir().Append("a");
  const auto dir_b = root_dir().Append("b");
  // Will fail because it is not a list.
  CreateTestFileInDirectory(dir_a, R"JSON(
      {}
    )JSON");
  // Loads "b" even if fails to load "a".
  CreateTestFileInDirectory(dir_b, R"JSON(
      [
        {"user":"user_2","own":["ServiceB"],"request":["ServiceB"]},
      ]
    )JSON");

  ServicePolicyMap policy_map;
  EXPECT_FALSE(
      LoadAllServicePolicyFileFromDirectories({dir_a, dir_b}, &policy_map));
  EXPECT_EQ(policy_map, CreateServicePolicyMapForTest({
                            {"ServiceB", {2, {2}}},
                        }));
}

}  // namespace
}  // namespace chromeos::mojo_service_manager
