// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Creates credential stores for testing.  This class is only used in prepping
// the test data for unit tests.

#ifndef CRYPTOHOME_MAKE_TESTS_H_
#define CRYPTOHOME_MAKE_TESTS_H_

#include <string>
#include <vector>

#include <base/macros.h>

#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/make_tests.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/username_passkey.h"
#include "cryptohome/vault_keyset.h"

#include "vault_keyset.pb.h"  // NOLINT(build/include)

namespace cryptohome {

struct TestUserInfo {
  const char* username;
  const char* password;
  bool create;
};

extern const TestUserInfo kDefaultUsers[];
extern const size_t kDefaultUserCount;
class TestUser;

class MakeTests {
 public:
  MakeTests();

  virtual ~MakeTests() { }

  void SetUpSystemSalt();
  void InitTestData(const std::string& image_dir,
                    const TestUserInfo* test_users,
                    size_t test_user_count);
  void InjectSystemSalt(MockPlatform* platform, const std::string& path);
  // Inject mocks needed for skeleton population.
  void InjectEphemeralSkeleton(MockPlatform* platform,
                               const std::string& root,
                               bool exists);
  void TearDownSystemSalt();

  std::vector<TestUser> users;
  brillo::Blob system_salt;
 private:
  DISALLOW_COPY_AND_ASSIGN(MakeTests);
};

class TestUser {
 public:
  TestUser() { }
  virtual ~TestUser() { }
  // Populate from struct TestUserInfo.
  void FromInfo(const struct TestUserInfo* info, const char* image_dir);
  // Generate a valid vault keyset using scrypt.
  void GenerateCredentials();
  // Inject the keyset so it can be accessed via platform.
  void InjectKeyset(MockPlatform* platform, bool enumerate = true);
  // Inject all the paths for a vault to exist.
  void InjectUserPaths(MockPlatform* platform,
                       uid_t chronos_uid,
                       gid_t chronos_gid,
                       gid_t chronos_access_gid,
                       gid_t daemon_gid);
  // Completely public accessors like the TestUserInfo struct.
  const char* username;
  const char* password;
  bool create;
  std::string shadow_root;
  std::string skel_dir;
  std::string obfuscated_username;
  std::string sanitized_username;
  std::string base_path;
  std::string image_path;
  std::string vault_path;
  std::string vault_mount_path;
  std::string user_vault_path;
  std::string root_vault_path;
  std::string keyset_path;
  std::string salt_path;
  std::string mount_prefix;
  std::string legacy_user_mount_path;
  std::string user_mount_path;
  std::string root_mount_path;
  std::string user_mount_prefix;
  std::string root_mount_prefix;
  brillo::Blob credentials;
  brillo::Blob user_salt;
  brillo::SecureBlob passkey;
  bool use_key_data;
  KeyData key_data;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MAKE_TESTS_H_
