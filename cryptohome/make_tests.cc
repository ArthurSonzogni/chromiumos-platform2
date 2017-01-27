// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Creates credential stores for testing

#include "cryptohome/make_tests.h"

#include <openssl/evp.h>
#include <stdint.h>

#include <algorithm>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/crypto.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mount.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/username_passkey.h"
#include "cryptohome/vault_keyset.h"

using base::FilePath;
using brillo::SecureBlob;
using ::testing::AnyOf;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Mock;
using ::testing::Property;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StartsWith;
using ::testing::_;

namespace cryptohome {

// struct TestUserInfo {
//   const char* username;
//   const char* password;
//   bool create;
// };

const TestUserInfo kDefaultUsers[] = {
  {"testuser0@invalid.domain", "zero", true},
  {"testuser1@invalid.domain", "one", true},
  {"testuser2@invalid.domain", "two", true},
  {"testuser3@invalid.domain", "three", true},
  {"testuser4@invalid.domain", "four", true},
  {"testuser5@invalid.domain", "five", false},
  {"testuser6@invalid.domain", "six", true},
  {"testuser7@invalid.domain", "seven", true},
  {"testuser8@invalid.domain", "eight", true},
  {"testuser9@invalid.domain", "nine", true},
  {"testuser10@invalid.domain", "ten", true},
  {"testuser11@invalid.domain", "eleven", true},
  {"testuser12@invalid.domain", "twelve", false},
  {"testuser13@invalid.domain", "thirteen", true},
};
const size_t kDefaultUserCount = arraysize(kDefaultUsers);

MakeTests::MakeTests() { }

void MakeTests::InitTestData(const FilePath& image_dir,
                             const TestUserInfo* test_users,
                             size_t test_user_count) {
  CHECK(system_salt.size()) << "Call SetUpSystemSalt() first";
  users.clear();
  users.resize(test_user_count);
  const TestUserInfo* user_info = test_users;
  for (size_t id = 0; id < test_user_count; ++id, ++user_info) {
    TestUser* user = &users[id];
    user->FromInfo(user_info, image_dir);
    user->GenerateCredentials();
  }
}

void MakeTests::SetUpSystemSalt() {
  std::string* salt = new std::string(CRYPTOHOME_DEFAULT_SALT_LENGTH, 'A');
  system_salt.resize(salt->size());
  memcpy(&system_salt[0], salt->data(), salt->size());
  brillo::cryptohome::home::SetSystemSalt(salt);
}

void MakeTests::TearDownSystemSalt() {
  std::string* salt = brillo::cryptohome::home::GetSystemSalt();
  brillo::cryptohome::home::SetSystemSalt(NULL);
  delete salt;
}

void MakeTests::InjectSystemSalt(MockPlatform* platform,
                                 const FilePath& path) {
  CHECK(brillo::cryptohome::home::GetSystemSalt());
  EXPECT_CALL(*platform, FileExists(path))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(*platform, GetFileSize(path, _))
    .WillRepeatedly(DoAll(SetArgPointee<1>(system_salt.size()),
                          Return(true)));
  EXPECT_CALL(*platform, ReadFile(path, _))
    .WillRepeatedly(DoAll(SetArgPointee<1>(system_salt), Return(true)));
}

void MakeTests::InjectEphemeralSkeleton(MockPlatform* platform,
                                        const FilePath& root,
                                        bool exists) {
  const FilePath skel = root.Append("skeleton");
  EXPECT_CALL(*platform, CreateDirectory(
        Property(&FilePath::value, StartsWith(skel.value()))))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(*platform,
      SetOwnership(
        Property(&FilePath::value, StartsWith(skel.value())),
        _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(*platform,
      DirectoryExists(
        Property(&FilePath::value, StartsWith(skel.value()))))
    .WillRepeatedly(Return(exists));
  EXPECT_CALL(*platform,
      FileExists(
        Property(&FilePath::value, StartsWith(skel.value()))))
    .WillRepeatedly(Return(exists));
  if (!exists) {
    EXPECT_CALL(*platform,
        SetGroupAccessible(
          Property(&FilePath::value, StartsWith(skel.value())),
          _, _))
      .WillRepeatedly(Return(true));
  }
}


void TestUser::FromInfo(const struct TestUserInfo* info,
                        const FilePath& image_dir) {
  username = info->username;
  password = info->password;
  create = info->create;
  use_key_data = false;
  // Stub system salt must already be in place. See MountTest::SetUp().
  // Sanitized usernames and obfuscated ones differ by case. Accomodate both.
  // TODO(ellyjones) fix this discrepancy!
  sanitized_username = brillo::cryptohome::home::SanitizeUserName(username);
  obfuscated_username = sanitized_username;
  std::transform(obfuscated_username.begin(),
                 obfuscated_username.end(),
                 obfuscated_username.begin(),
                 ::tolower);
  // Both pass this check though.
  DCHECK(brillo::cryptohome::home::IsSanitizedUserName(
           obfuscated_username));
  shadow_root = image_dir;
  skel_dir = image_dir.Append("skel");
  base_path = image_dir.Append(obfuscated_username);
  image_path = base_path.Append("image");
  vault_path = base_path.Append("vault");
  vault_mount_path = base_path.Append("mount");
  root_vault_path = vault_path.Append("root");
  user_vault_path = vault_path.Append("user");
  keyset_path = base_path.Append("master.0");
  salt_path = base_path.Append("master.0.salt");
  user_salt.assign('A', PKCS5_SALT_LEN);
  mount_prefix = brillo::cryptohome::home::GetUserPathPrefix().DirName();
  legacy_user_mount_path = FilePath("/home/chronos/user");
  user_mount_path = brillo::cryptohome::home::GetUserPath(username)
    .StripTrailingSeparators();
  user_mount_prefix = brillo::cryptohome::home::GetUserPathPrefix()
    .StripTrailingSeparators();
  root_mount_path = brillo::cryptohome::home::GetRootPath(username)
    .StripTrailingSeparators();
  root_mount_prefix = brillo::cryptohome::home::GetRootPathPrefix()
    .StripTrailingSeparators();
}

void TestUser::GenerateCredentials() {
  std::string* system_salt = brillo::cryptohome::home::GetSystemSalt();
  brillo::Blob salt;
  salt.resize(system_salt->size());
  memcpy(&salt.at(0), system_salt->c_str(), system_salt->size());
  NiceMock<MockTpm> tpm;
  NiceMock<MockPlatform> platform;
  Crypto crypto(&platform);
  crypto.set_use_tpm(false);
  crypto.set_scrypt_max_encrypt_time(0.001);
  UserOldestActivityTimestampCache timestamp_cache;

  scoped_refptr<Mount> mount = new Mount();
  mount->set_shadow_root(shadow_root);
  mount->set_skel_source(skel_dir);
  mount->set_use_tpm(false);
  NiceMock<policy::MockDevicePolicy>* device_policy =
    new NiceMock<policy::MockDevicePolicy>;
  mount->set_policy_provider(new policy::PolicyProvider(device_policy));
  EXPECT_CALL(*device_policy, LoadPolicy())
    .WillRepeatedly(Return(true));
  FilePath salt_path = shadow_root.Append("salt");
  int64_t salt_size = salt.size();
  EXPECT_CALL(platform, FileExists(salt_path))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform, GetFileSize(salt_path, _))
    .WillRepeatedly(DoAll(SetArgPointee<1>(salt_size), Return(true)));
  EXPECT_CALL(platform, ReadFile(salt_path, _))
    .WillRepeatedly(DoAll(SetArgPointee<1>(salt), Return(true)));
  EXPECT_CALL(platform, DirectoryExists(shadow_root))
    .WillRepeatedly(Return(true));
  mount->Init(&platform, &crypto, &timestamp_cache);

  cryptohome::Crypto::PasswordToPasskey(password,
                                        salt,
                                        &passkey);
  UsernamePasskey up(username, passkey);
  if (use_key_data) {
    up.set_key_data(key_data);
  }
  bool created;
  // NOTE! This code gives us generated credentials for credentials tests NOT
  // NOTE! golden credentials to test against.  This means we won't see problems
  // NOTE! if the credentials generation and checking code break together.
  // TODO(wad,ellyjones) Add golden credential tests too.

  // "Old" image path
  EXPECT_CALL(platform, FileExists(image_path))
    .WillRepeatedly(Return(false));
  // Use 'stat' failures to trigger default-allow the creation of the paths.
  EXPECT_CALL(platform,
      Stat(
        Property(&FilePath::value,
          AnyOf("/home",
                "/home/root",
                brillo::cryptohome::home::GetRootPath(username).value(),
                "/home/user",
                brillo::cryptohome::home::GetUserPath(username).value())),
        _))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(platform,
      Stat(
        Property(&FilePath::value,
          AnyOf("/home/chronos",
                mount->GetNewUserPath(username).value())),
          _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform, DirectoryExists(vault_path))
    .WillOnce(Return(false));
  EXPECT_CALL(platform, DirectoryExists(vault_mount_path))
    .WillOnce(Return(false));
  EXPECT_CALL(platform, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  // Grab the generated credential
  EXPECT_CALL(platform, WriteFileAtomicDurable(keyset_path, _, _))
    .WillOnce(DoAll(SaveArg<1>(&credentials), Return(true)));
  mount->EnsureCryptohome(up, &created);
  DCHECK(created && credentials.size());
}

void TestUser::InjectKeyset(MockPlatform* platform, bool enumerate) {
  // TODO(wad) Update to support multiple keys
  EXPECT_CALL(*platform,
      FileExists(
        Property(&FilePath::value, StartsWith(keyset_path.value()))))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(*platform, ReadFile(keyset_path, _))
    .WillRepeatedly(DoAll(SetArgPointee<1>(credentials),
                          Return(true)));
  if (enumerate) {
    MockFileEnumerator* files = new MockFileEnumerator();
    EXPECT_CALL(*platform, GetFileEnumerator(base_path, false, _))
      .WillOnce(Return(files));
    {
      InSequence s;
      // Single key.
      EXPECT_CALL(*files, Next())
        .WillOnce(Return(keyset_path));
      EXPECT_CALL(*files, Next())
        .WillOnce(Return(FilePath()));
    }
  }
}

void TestUser::InjectUserPaths(MockPlatform* platform,
                               uid_t chronos_uid,
                               gid_t chronos_gid,
                               gid_t chronos_access_gid,
                               gid_t daemon_gid) {
  scoped_refptr<Mount> temp_mount = new Mount();
  EXPECT_CALL(*platform, FileExists(image_path))
    .WillRepeatedly(Return(false));
  struct stat root_dir;
  memset(&root_dir, 0, sizeof(root_dir));
  root_dir.st_mode = S_IFDIR|S_ISVTX;
  EXPECT_CALL(*platform,
      Stat(AnyOf(mount_prefix,
                 root_mount_prefix,
                 user_mount_prefix,
                 root_mount_path,
                 user_vault_path),
           _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(root_dir), Return(true)));
  // Avoid triggering vault migration.  (Is there another test for that?)
  struct stat root_vault_dir;
  memset(&root_vault_dir, 0, sizeof(root_vault_dir));
  root_vault_dir.st_mode = S_IFDIR|S_ISVTX;
  root_vault_dir.st_uid = 0;
  root_vault_dir.st_gid = daemon_gid;
  EXPECT_CALL(*platform, Stat(root_vault_path, _))
    .WillRepeatedly(DoAll(SetArgPointee<1>(root_vault_dir), Return(true)));
  struct stat user_dir;
  memset(&user_dir, 0, sizeof(user_dir));
  user_dir.st_mode = S_IFDIR;
  user_dir.st_uid = chronos_uid;
  user_dir.st_gid = chronos_access_gid;
  EXPECT_CALL(*platform,
      Stat(AnyOf(user_mount_path,
                 temp_mount->GetNewUserPath(username)), _))
    .WillRepeatedly(DoAll(SetArgPointee<1>(user_dir), Return(true)));
  struct stat chronos_dir;
  memset(&chronos_dir, 0, sizeof(chronos_dir));
  chronos_dir.st_mode = S_IFDIR;
  chronos_dir.st_uid = chronos_uid;
  chronos_dir.st_gid = chronos_gid;
  EXPECT_CALL(*platform,
      Stat(FilePath("/home/chronos"), _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(chronos_dir),
                            Return(true)));
  EXPECT_CALL(*platform,
      DirectoryExists(
        Property(&FilePath::value,
          AnyOf(shadow_root.value(),
                mount_prefix.value(),
                StartsWith(legacy_user_mount_path.value()),
                StartsWith(vault_mount_path.value()),
                StartsWith(vault_path.value())))))
    .WillRepeatedly(Return(true));
  // TODO(wad) Bounce this out if needed elsewhere.
  FilePath user_vault_mount = vault_mount_path.Append("user");
  FilePath new_user_path = temp_mount->GetNewUserPath(username);
  EXPECT_CALL(*platform,
      FileExists(
        Property(&FilePath::value,
          AnyOf(StartsWith(legacy_user_mount_path.value()),
                StartsWith(vault_mount_path.value()),
                StartsWith(user_mount_path.value()),
                StartsWith(root_mount_path.value()),
                StartsWith(new_user_path.value()),
                StartsWith(keyset_path.value())))))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(*platform,
      SetGroupAccessible(
        Property(&FilePath::value,
          AnyOf(StartsWith(legacy_user_mount_path.value()),
                StartsWith(user_vault_mount.value()))),
        chronos_access_gid, _))
    .WillRepeatedly(Return(true));
}

}  // namespace cryptohome
