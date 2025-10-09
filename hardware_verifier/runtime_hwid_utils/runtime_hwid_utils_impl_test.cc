// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/runtime_hwid_utils/runtime_hwid_utils_impl.h"

#include <memory>
#include <optional>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <brillo/file_utils.h>
#include <gtest/gtest.h>
#include <libcrossystem/crossystem_fake.h>

namespace hardware_verifier {

namespace {

constexpr char kCrosSystemHWIDKey[] = "hwid";
constexpr char kRuntimeHWIDFilePath[] =
    "var/cache/hardware_verifier/runtime_hwid";

constexpr char kFactoryHWID[] = "REDRIX-ZZCR D3A-39F-27K-E2A";
constexpr char kRuntimeHWID[] =
    "REDRIX-ZZCR D3A-39E-K6C-E9Z R:1-1-2-6-11-4-5-3-7-8-10-9-1";
constexpr char kRuntimeHWIDChecksum[] =
    "F897B1AD02B472632DB714083BFC70D511B12C0A";

class RuntimeHWIDUtilsImplForTesting : public RuntimeHWIDUtilsImpl {
 public:
  explicit RuntimeHWIDUtilsImplForTesting(
      const base::FilePath& root,
      std::unique_ptr<crossystem::Crossystem> crossystem)
      : RuntimeHWIDUtilsImpl(root, std::move(crossystem)) {}
};

class RuntimeHWIDUtilsImplTest : public ::testing::Test {
 protected:
  void SetUp() override {
    base::ScopedTempDir temp_dir;
    ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
    fake_root_ = temp_dir.GetPath();

    auto crossystem = std::make_unique<crossystem::Crossystem>(
        std::make_unique<crossystem::fake::CrossystemFake>());
    fake_crossystem_ = crossystem.get();

    runtime_hwid_utils_impl_ = std::make_unique<RuntimeHWIDUtilsImplForTesting>(
        fake_root_, std::move(crossystem));
  }

  void CreateRuntimeHWIDFile(const std::string& content) {
    const auto runtime_hwid_path = fake_root_.Append(kRuntimeHWIDFilePath);
    ASSERT_TRUE(brillo::WriteStringToFile(runtime_hwid_path, content));
    ASSERT_TRUE(base::PathExists(runtime_hwid_path));
  }

  base::FilePath fake_root_;
  crossystem::Crossystem* fake_crossystem_;
  std::unique_ptr<RuntimeHWIDUtilsImpl> runtime_hwid_utils_impl_;
};

TEST_F(RuntimeHWIDUtilsImplTest,
       DeleteRuntimeHWIDFromDevice_FileNotExists_Success) {
  const auto runtime_hwid_path = fake_root_.Append(kRuntimeHWIDFilePath);
  ASSERT_FALSE(base::PathExists(runtime_hwid_path));

  EXPECT_TRUE(runtime_hwid_utils_impl_->DeleteRuntimeHWIDFromDevice());
  EXPECT_FALSE(base::PathExists(runtime_hwid_path));
}

TEST_F(RuntimeHWIDUtilsImplTest,
       DeleteRuntimeHWIDFromDevice_FileExists_Success) {
  const auto runtime_hwid_path = fake_root_.Append(kRuntimeHWIDFilePath);
  ASSERT_TRUE(brillo::WriteStringToFile(runtime_hwid_path, ""));
  ASSERT_TRUE(base::PathExists(runtime_hwid_path));

  EXPECT_TRUE(runtime_hwid_utils_impl_->DeleteRuntimeHWIDFromDevice());
  EXPECT_FALSE(base::PathExists(runtime_hwid_path));
}

TEST_F(RuntimeHWIDUtilsImplTest,
       DeleteRuntimeHWIDFromDevice_DeleteFails_Failure) {
  const auto runtime_hwid_path = fake_root_.Append(kRuntimeHWIDFilePath);
  // Make the path a directory to make the file unremovable.
  const auto fake_file_path = runtime_hwid_path.Append("fake-file");
  ASSERT_TRUE(brillo::WriteStringToFile(fake_file_path, ""));
  ASSERT_TRUE(base::DirectoryExists(runtime_hwid_path));

  EXPECT_FALSE(runtime_hwid_utils_impl_->DeleteRuntimeHWIDFromDevice());
  EXPECT_TRUE(base::PathExists(runtime_hwid_path));
}

TEST_F(RuntimeHWIDUtilsImplTest, GetRuntimeHWID) {
  fake_crossystem_->VbSetSystemPropertyString(kCrosSystemHWIDKey, kFactoryHWID);
  const std::string runtime_hwid_file_content =
      base::StringPrintf(R"(%s
%s)",
                         kRuntimeHWID, kRuntimeHWIDChecksum);
  CreateRuntimeHWIDFile(runtime_hwid_file_content);

  const auto runtime_hwid = runtime_hwid_utils_impl_->GetRuntimeHWID();

  EXPECT_EQ(runtime_hwid, kRuntimeHWID);
}

TEST_F(RuntimeHWIDUtilsImplTest, GetRuntimeHWID_EmptyRuntimeHWIDFile) {
  fake_crossystem_->VbSetSystemPropertyString(kCrosSystemHWIDKey, kFactoryHWID);
  CreateRuntimeHWIDFile("");

  const auto runtime_hwid = runtime_hwid_utils_impl_->GetRuntimeHWID();

  EXPECT_EQ(runtime_hwid, kFactoryHWID);
}

TEST_F(RuntimeHWIDUtilsImplTest, GetRuntimeHWID_MalformedRuntimeHWIDFile) {
  fake_crossystem_->VbSetSystemPropertyString(kCrosSystemHWIDKey, kFactoryHWID);
  // File has only one line.
  const std::string runtime_hwid_file_content =
      base::StringPrintf(R"(%s
%s
invalid-line)",
                         kRuntimeHWID, kRuntimeHWIDChecksum);
  CreateRuntimeHWIDFile(runtime_hwid_file_content);

  const auto runtime_hwid = runtime_hwid_utils_impl_->GetRuntimeHWID();

  EXPECT_EQ(runtime_hwid, kFactoryHWID);
}

TEST_F(RuntimeHWIDUtilsImplTest, GetRuntimeHWID_InvalidChecksum) {
  fake_crossystem_->VbSetSystemPropertyString(kCrosSystemHWIDKey, kFactoryHWID);
  const std::string runtime_hwid_file_content =
      base::StringPrintf(R"(%s
invalid-checksum)",
                         kRuntimeHWID);
  CreateRuntimeHWIDFile(runtime_hwid_file_content);

  const auto runtime_hwid = runtime_hwid_utils_impl_->GetRuntimeHWID();

  EXPECT_EQ(runtime_hwid, kFactoryHWID);
}

TEST_F(RuntimeHWIDUtilsImplTest, GetRuntimeHWID_MismatchedModelRLZ) {
  fake_crossystem_->VbSetSystemPropertyString(kCrosSystemHWIDKey, kFactoryHWID);
  const std::string runtime_hwid_file_content =
      R"(MODEL-CODE A1B-C2D-E2J R:1-1-2-6-11-4-5-3-7-8-10-9-1
1B2927AE670B279CE1096C21AD05C27CE60A03E5)";
  CreateRuntimeHWIDFile(runtime_hwid_file_content);

  const auto runtime_hwid = runtime_hwid_utils_impl_->GetRuntimeHWID();

  EXPECT_EQ(runtime_hwid, kFactoryHWID);
}

TEST_F(RuntimeHWIDUtilsImplTest, GetRuntimeHWID_NoFactoryHWID) {
  EXPECT_EQ(runtime_hwid_utils_impl_->GetRuntimeHWID(), std::nullopt);
}

TEST_F(RuntimeHWIDUtilsImplTest, GetRuntimeHWID_NoRuntimeHWIDFile) {
  fake_crossystem_->VbSetSystemPropertyString(kCrosSystemHWIDKey, kFactoryHWID);

  const auto runtime_hwid = runtime_hwid_utils_impl_->GetRuntimeHWID();

  EXPECT_EQ(runtime_hwid, kFactoryHWID);
}

}  // namespace

}  // namespace hardware_verifier
