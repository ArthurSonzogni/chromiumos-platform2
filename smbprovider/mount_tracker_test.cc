// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <memory>

#include <base/bind.h>
#include <base/macros.h>
#include <base/test/simple_test_tick_clock.h>
#include <gtest/gtest.h>

#include "smbprovider/fake_samba_interface.h"
#include "smbprovider/fake_samba_proxy.h"
#include "smbprovider/mount_manager.h"
#include "smbprovider/mount_tracker.h"
#include "smbprovider/samba_interface.h"
#include "smbprovider/smb_credential.h"
#include "smbprovider/smbprovider_test_helper.h"
#include "smbprovider/temp_file_manager.h"

namespace smbprovider {

namespace {

std::unique_ptr<SambaInterface> SambaInterfaceFactoryFunction(
    FakeSambaInterface* fake_samba) {
  return std::make_unique<FakeSambaProxy>(fake_samba);
}

}  // namespace

constexpr char kMountRoot[] = "smb://192.168.0.1/test";
constexpr char kWorkgroup[] = "domain";
constexpr char kUsername[] = "user1";
constexpr char kPassword[] = "admin";

class MountTrackerTest : public testing::Test {
 public:
  using SambaInterfaceFactory =
      base::Callback<std::unique_ptr<SambaInterface>()>;

  MountTrackerTest() {
    auto tick_clock = std::make_unique<base::SimpleTestTickClock>();

    auto fake_samba_ = std::make_unique<FakeSambaInterface>();
    samba_interface_factory_ =
        base::Bind(&SambaInterfaceFactoryFunction, fake_samba_.get());

    mount_tracker_ = std::make_unique<MountTracker>(
        std::move(tick_clock), false /* metadata_cache_enabled */);
  }

  ~MountTrackerTest() override = default;

  bool AddMountWithEmptyCredential(const std::string& root_path,
                                   int32_t* mount_id) {
    SmbCredential credential("" /* workgroup */, "" /* username */,
                             GetEmptyPassword());

    return mount_tracker_->AddMount(root_path, std::move(credential),
                                    CreateSambaInterface(), mount_id);
  }

  bool AddMount(const std::string& root_path,
                const std::string& workgroup,
                const std::string& username,
                const std::string& password,
                int32_t* mount_id) {
    SmbCredential credential(workgroup, username, CreatePassword(password));

    return mount_tracker_->AddMount(root_path, std::move(credential),
                                    CreateSambaInterface(), mount_id);
  }

  bool RemountWithEmptyCredential(const std::string& root_path,
                                  int32_t mount_id) {
    base::ScopedFD password_fd =
        smbprovider::WritePasswordToFile(&temp_files_, "" /* password */);

    SmbCredential credential("" /* workgroup */, "" /* username */,
                             GetPassword(password_fd));

    return mount_tracker_->AddMountWithId(root_path, std::move(credential),
                                          CreateSambaInterface(), mount_id);
  }

  bool Remount(const std::string& root_path,
               const std::string& workgroup,
               const std::string& username,
               const std::string& password,
               int32_t mount_id) {
    SmbCredential credential(workgroup, username, CreatePassword(password));

    return mount_tracker_->AddMountWithId(root_path, std::move(credential),
                                          CreateSambaInterface(), mount_id);
  }

  std::unique_ptr<SambaInterface> CreateSambaInterface() {
    return samba_interface_factory_.Run();
  }

  void ExpectCredentialsEqual(int32_t mount_id,
                              const std::string& workgroup,
                              const std::string& username,
                              const std::string& password) {
    const SambaInterface::SambaInterfaceId samba_interface_id =
        GetSambaInterfaceId(mount_id);

    const SmbCredential& cred =
        mount_tracker_->GetCredential(samba_interface_id);

    EXPECT_EQ(workgroup, std::string(cred.workgroup));
    EXPECT_EQ(username, std::string(cred.username));

    if (!password.empty()) {
      EXPECT_EQ(password, std::string(cred.password->GetRaw()));
    } else {
      // Password is empty but check if credential-stored password is empty too.
      EXPECT_TRUE(cred.password.get() == nullptr);
    }
  }

  SambaInterface::SambaInterfaceId GetSambaInterfaceId(const int32_t mount_id) {
    SambaInterface* samba_interface;
    EXPECT_TRUE(mount_tracker_->GetSambaInterface(mount_id, &samba_interface));

    return samba_interface->GetSambaInterfaceId();
  }

 protected:
  std::unique_ptr<MountTracker> mount_tracker_;
  TempFileManager temp_files_;
  SambaInterfaceFactory samba_interface_factory_;

 private:
  base::ScopedFD WriteEmptyPasswordToFile() {
    return smbprovider::WritePasswordToFile(&temp_files_, "" /* password */);
  }

  base::ScopedFD WritePasswordToFile(const std::string& password) {
    return smbprovider::WritePasswordToFile(&temp_files_, password);
  }

  std::unique_ptr<password_provider::Password> GetEmptyPassword() {
    return GetPassword(WriteEmptyPasswordToFile());
  }

  std::unique_ptr<password_provider::Password> CreatePassword(
      const std::string& password) {
    return GetPassword(WritePasswordToFile(password));
  }

  DISALLOW_COPY_AND_ASSIGN(MountTrackerTest);
};

TEST_F(MountTrackerTest, TestNegativeMounts) {
  const std::string root_path = "smb://server/share";
  const int32_t mount_id = 1;

  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(root_path));
  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(mount_id));
}

TEST_F(MountTrackerTest, TestAddMount) {
  const std::string root_path = "smb://server/share";

  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(root_path));
  int32_t mount_id;
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());

  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(root_path));
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));
}

TEST_F(MountTrackerTest, TestAddSameMount) {
  const std::string root_path = "smb://server/share";

  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(root_path));
  int32_t mount_id;
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id));

  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(root_path));
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));
  EXPECT_EQ(1, mount_tracker_->MountCount());

  // Ensure IsAlreadyMounted is working after adding a mount.
  const std::string root_path2 = "smb://server/share2";
  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(root_path2));

  const int32_t mount_id2 = 9;
  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(mount_id2));

  EXPECT_FALSE(AddMountWithEmptyCredential(root_path, &mount_id));

  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(root_path));
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));

  EXPECT_FALSE(AddMountWithEmptyCredential(root_path, &mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
}

TEST_F(MountTrackerTest, TestMountCount) {
  const std::string root_path = "smb://server/share1";
  const std::string root_path2 = "smb://server/share2";

  EXPECT_EQ(0, mount_tracker_->MountCount());

  int32_t mount_id1;
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id1));

  EXPECT_EQ(1, mount_tracker_->MountCount());

  int32_t mount_id2;
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path2, &mount_id2));

  EXPECT_EQ(2, mount_tracker_->MountCount());
  EXPECT_NE(mount_id1, mount_id2);
}

TEST_F(MountTrackerTest, TestAddMultipleDifferentMountId) {
  const std::string root_path1 = "smb://server/share1";
  int32_t mount_id1;
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path1, &mount_id1));

  const std::string root_path2 = "smb://server/share2";
  int32_t mount_id2;
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path2, &mount_id2));

  EXPECT_GE(mount_id1, 0);
  EXPECT_GE(mount_id2, 0);
  EXPECT_NE(mount_id1, mount_id2);
}

TEST_F(MountTrackerTest, TestRemountSucceeds) {
  const std::string root_path = "smb://server/share1";
  const int32_t mount_id = 9;

  EXPECT_TRUE(RemountWithEmptyCredential(root_path, mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));
}

TEST_F(MountTrackerTest, TestRemountFailsWithSameMountPath) {
  const std::string root_path = "smb://server/share1";
  const int32_t mount_id = 9;

  EXPECT_TRUE(RemountWithEmptyCredential(root_path, mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));

  const int32_t mount_id2 = 10;
  // Should be false since the same path cannot be mounted twice.
  EXPECT_FALSE(RemountWithEmptyCredential(root_path, mount_id2));
}

TEST_F(MountTrackerTest, TestRemountFailsWithSameMountId) {
  const std::string root_path = "smb://server/share1";
  const int32_t mount_id = 9;

  EXPECT_TRUE(RemountWithEmptyCredential(root_path, mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));

  const std::string root_path2 = "smb://server/share2";
  const int32_t mount_id2 = 9;
  // Should be false since the same id cannot be mounted twice.
  EXPECT_FALSE(RemountWithEmptyCredential(root_path2, mount_id2));
}

TEST_F(MountTrackerTest, TestMountAfterRemounts) {
  const std::string root_path_1 = "smb://server/share1";
  const int32_t mount_id_1 = 9;

  const std::string root_path_2 = "smb://server/share2";
  const int32_t mount_id_2 = 4;

  const std::string new_root_path = "smb://server/share3";

  EXPECT_TRUE(RemountWithEmptyCredential(root_path_1, mount_id_1));
  EXPECT_TRUE(RemountWithEmptyCredential(root_path_2, mount_id_2));

  EXPECT_EQ(2, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id_1));
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id_2));

  int32_t mount_id_3;
  EXPECT_TRUE(AddMountWithEmptyCredential(new_root_path, &mount_id_3));

  EXPECT_EQ(3, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id_3));
  EXPECT_GT(mount_id_3, mount_id_1);
}

TEST_F(MountTrackerTest, TestAddRemoveMount) {
  // Add a new mount.
  const std::string root_path = "smb://server/share";
  int32_t mount_id;

  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(root_path));

  // Verify the mount can be removed.
  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id));
  EXPECT_EQ(0, mount_tracker_->MountCount());

  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(mount_id));
  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(root_path));
}

TEST_F(MountTrackerTest, TestAddThenRemoveWrongMount) {
  // Add a new mount.
  const std::string root_path = "smb://server/share";
  int32_t mount_id;

  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id));

  // Verify RemoveMount fails with an invalid id and nothing is removed.
  const int32_t invalid_mount_id = mount_id + 1;
  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(invalid_mount_id));

  EXPECT_FALSE(mount_tracker_->RemoveMount(invalid_mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());

  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(root_path));

  // Verify the valid id can still be removed.
  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id));

  EXPECT_EQ(0, mount_tracker_->MountCount());

  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(mount_id));
  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(root_path));
}

TEST_F(MountTrackerTest, TestAddRemoveMultipleMounts) {
  const std::string root_path1 = "smb://server/share1";
  const std::string root_path2 = "smb://server/share2";

  // Add two mounts and verify they were both added.
  int32_t mount_id_1;
  int32_t mount_id_2;

  EXPECT_TRUE(AddMountWithEmptyCredential(root_path1, &mount_id_1));
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path2, &mount_id_2));

  EXPECT_EQ(2, mount_tracker_->MountCount());

  // Remove the second id, verify it is removed, and the first remains.
  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id_2));

  EXPECT_EQ(1, mount_tracker_->MountCount());

  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(mount_id_2));
  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(root_path2));

  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id_1));
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(root_path1));

  // Remove the first id and verify it is also removed.
  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id_1));

  EXPECT_EQ(0, mount_tracker_->MountCount());

  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(mount_id_1));
  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(root_path1));
}

TEST_F(MountTrackerTest, TestRemovedMountCanBeRemounted) {
  const std::string root_path = "smb://server/share1";

  int32_t mount_id;
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id));

  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id));

  EXPECT_EQ(0, mount_tracker_->MountCount());

  // Should be able to be remounted again.
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
}

TEST_F(MountTrackerTest, TestRemoveInvalidMountId) {
  const int32_t mount_id = 5;

  EXPECT_FALSE(mount_tracker_->RemoveMount(mount_id));

  // Ensure AddMount still works.
  const std::string root_path = "smb://server/share";

  int32_t mount_id1;
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id1));

  EXPECT_EQ(1, mount_tracker_->MountCount());

  // Ensure RemoveMount still works.
  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id1));

  EXPECT_EQ(0, mount_tracker_->MountCount());
}

TEST_F(MountTrackerTest, TestGetFullPath) {
  // Add a new mount.
  const std::string root_path = "smb://server/share";
  int32_t mount_id;

  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id));

  // Verify the full path is as expected.
  const std::string entry_path = "/foo/bar";
  const std::string expected_full_path = root_path + entry_path;

  std::string actual_full_path;
  EXPECT_TRUE(
      mount_tracker_->GetFullPath(mount_id, entry_path, &actual_full_path));

  EXPECT_EQ(expected_full_path, actual_full_path);
}

TEST_F(MountTrackerTest, TestGetFullPathWithInvalidId) {
  // Add a new mount.
  const std::string root_path = "smb://server/share";
  int32_t mount_id;

  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id));

  // Verify calling GetFullPath() with an invalid id fails.
  const int32_t invalid_mount_id = mount_id + 1;

  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(invalid_mount_id));
  std::string full_path;
  EXPECT_FALSE(
      mount_tracker_->GetFullPath(invalid_mount_id, "/foo/bar", &full_path));
}

TEST_F(MountTrackerTest, TestGetFullPathMultipleMounts) {
  // Add two mounts with different roots.
  const std::string root_path_1 = "smb://server/share1";
  const std::string root_path_2 = "smb://server/share2";

  ASSERT_NE(root_path_1, root_path_2);

  int32_t mount_id_1;
  int32_t mount_id_2;

  EXPECT_TRUE(AddMountWithEmptyCredential(root_path_1, &mount_id_1));
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path_2, &mount_id_2));

  // Verify correct ids map to the correct paths.
  std::string actual_full_path;
  const std::string entry_path = "/foo/bar";
  const std::string expected_full_path_1 = root_path_1 + entry_path;
  const std::string expected_full_path_2 = root_path_2 + entry_path;

  EXPECT_TRUE(
      mount_tracker_->GetFullPath(mount_id_1, entry_path, &actual_full_path));

  EXPECT_EQ(expected_full_path_1, actual_full_path);

  EXPECT_TRUE(
      mount_tracker_->GetFullPath(mount_id_2, entry_path, &actual_full_path));

  EXPECT_EQ(expected_full_path_2, actual_full_path);
}

TEST_F(MountTrackerTest, TestGetRelativePath) {
  const std::string root_path = "smb://server/share1";
  const std::string expected_relative_path = "/animals/dog.jpg";
  const std::string full_path = root_path + expected_relative_path;

  int32_t mount_id;
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id));

  EXPECT_EQ(expected_relative_path,
            mount_tracker_->GetRelativePath(mount_id, full_path));
}

TEST_F(MountTrackerTest, TestGetRelativePathOnRoot) {
  const std::string root_path = "smb://server/share1";
  const std::string expected_relative_path = "/";
  const std::string full_path = root_path + expected_relative_path;

  int32_t mount_id;
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id));

  EXPECT_EQ(expected_relative_path,
            mount_tracker_->GetRelativePath(mount_id, full_path));
}

TEST_F(MountTrackerTest, TestGetEmptyCredential) {
  const std::string root_path = "smb://server/share";

  int32_t mount_id;
  EXPECT_TRUE(AddMountWithEmptyCredential(root_path, &mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));

  ExpectCredentialsEqual(mount_id, "" /* Workgroup */, "" /* Username */,
                         "" /* Password */);
}

TEST_F(MountTrackerTest, TestAddMountWithGetCredential) {
  int32_t mount_id;
  EXPECT_TRUE(
      AddMount(kMountRoot, kWorkgroup, kUsername, kPassword, &mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));

  ExpectCredentialsEqual(mount_id, kWorkgroup, kUsername, kPassword);
}

TEST_F(MountTrackerTest, TestAddMountWithEmptyPassword) {
  const std::string password = "";

  int32_t mount_id;
  EXPECT_TRUE(AddMount(kMountRoot, kWorkgroup, kUsername, password, &mount_id));

  EXPECT_GE(mount_id, 0);
  EXPECT_EQ(1, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));

  ExpectCredentialsEqual(mount_id, kWorkgroup, kUsername, password);
}

TEST_F(MountTrackerTest, TestAddingRemovingMultipleCredentials) {
  const std::string mount_root2 = "smb://192.168.0.1/share";
  const std::string workgroup2 = "workgroup2";
  const std::string username2 = "user2";
  const std::string password2 = "root";

  int32_t mount_id1;
  EXPECT_TRUE(
      AddMount(kMountRoot, kWorkgroup, kUsername, kPassword, &mount_id1));

  int32_t mount_id2;
  EXPECT_TRUE(
      AddMount(mount_root2, workgroup2, username2, password2, &mount_id2));

  EXPECT_EQ(2, mount_tracker_->MountCount());

  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(kMountRoot));
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_root2));

  ExpectCredentialsEqual(mount_id1, kWorkgroup, kUsername, kPassword);

  ExpectCredentialsEqual(mount_id2, workgroup2, username2, password2);

  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id1));
  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id2));
}

TEST_F(MountTrackerTest, TestRemoveCredentialFromMultiple) {
  const std::string mount_root2 = "smb://192.168.0.1/share";
  const std::string workgroup2 = "workgroup2";
  const std::string username2 = "user2";
  const std::string password2 = "root";

  int32_t mount_id1;
  EXPECT_TRUE(
      AddMount(kMountRoot, kWorkgroup, kUsername, kPassword, &mount_id1));

  int32_t mount_id2;
  EXPECT_TRUE(
      AddMount(mount_root2, workgroup2, username2, password2, &mount_id2));

  EXPECT_EQ(2, mount_tracker_->MountCount());

  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id1));

  EXPECT_EQ(1, mount_tracker_->MountCount());

  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(kMountRoot));
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_root2));

  ExpectCredentialsEqual(mount_id2, workgroup2, username2, password2);

  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id2));
  EXPECT_EQ(0, mount_tracker_->MountCount());
}

TEST_F(MountTrackerTest, TestRemountWithCredential) {
  int32_t mount_id = 9;
  EXPECT_TRUE(Remount(kMountRoot, kWorkgroup, kUsername, kPassword, mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));

  ExpectCredentialsEqual(mount_id, kWorkgroup, kUsername, kPassword);
}

TEST_F(MountTrackerTest, TestAddRemoveRemountWithCredential) {
  int32_t mount_id;
  EXPECT_TRUE(
      AddMount(kMountRoot, kWorkgroup, kUsername, kPassword, &mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));

  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id));

  EXPECT_EQ(0, mount_tracker_->MountCount());

  EXPECT_TRUE(Remount(kMountRoot, kWorkgroup, kUsername, kPassword, mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));

  ExpectCredentialsEqual(mount_id, kWorkgroup, kUsername, kPassword);
}

TEST_F(MountTrackerTest, TestIsSambaInterfaceIdMounted) {
  int32_t mount_id;
  EXPECT_TRUE(
      AddMount(kMountRoot, kWorkgroup, kUsername, kPassword, &mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());

  SambaInterface::SambaInterfaceId samba_interface_id =
      GetSambaInterfaceId(mount_id);

  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(samba_interface_id));
}

TEST_F(MountTrackerTest, TestAddRemoveSambaInterfaceId) {
  int32_t mount_id;
  EXPECT_TRUE(
      AddMount(kMountRoot, kWorkgroup, kUsername, kPassword, &mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());

  SambaInterface::SambaInterfaceId samba_interface_id =
      GetSambaInterfaceId(mount_id);

  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(samba_interface_id));

  EXPECT_TRUE(mount_tracker_->RemoveMount(mount_id));

  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(samba_interface_id));
}

TEST_F(MountTrackerTest, TestIsSambaInterfaceIdMountedWithRemount) {
  int32_t mount_id = 9;
  EXPECT_TRUE(Remount(kMountRoot, kWorkgroup, kUsername, kPassword, mount_id));

  EXPECT_EQ(1, mount_tracker_->MountCount());
  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(mount_id));

  SambaInterface::SambaInterfaceId samba_interface_id =
      GetSambaInterfaceId(mount_id);

  EXPECT_TRUE(mount_tracker_->IsAlreadyMounted(samba_interface_id));
}

TEST_F(MountTrackerTest, TestNonExistantSambaInterfaceId) {
  uintptr_t samba_interface_id = 1;
  SambaInterface::SambaInterfaceId non_existent_id =
      reinterpret_cast<SambaInterface::SambaInterfaceId>(samba_interface_id);

  EXPECT_FALSE(mount_tracker_->IsAlreadyMounted(non_existent_id));
}

TEST_F(MountTrackerTest, TestGetCacheNoMounts) {
  MetadataCache* cache = nullptr;

  EXPECT_FALSE(mount_tracker_->GetMetadataCache(0, &cache));
}

TEST_F(MountTrackerTest, TestGetCache) {
  int32_t mount_id;
  EXPECT_TRUE(AddMountWithEmptyCredential("smb://server/share", &mount_id));

  MetadataCache* cache = nullptr;
  EXPECT_TRUE(mount_tracker_->GetMetadataCache(mount_id, &cache));
  EXPECT_NE(nullptr, cache);
}

TEST_F(MountTrackerTest, TestGetCacheForInvalidMount) {
  int32_t mount_id;
  EXPECT_TRUE(AddMountWithEmptyCredential("smb://server/share", &mount_id));

  // mount_id + 1 does not exist.
  MetadataCache* cache = nullptr;
  EXPECT_FALSE(mount_tracker_->GetMetadataCache(mount_id + 1, &cache));
}

}  // namespace smbprovider
