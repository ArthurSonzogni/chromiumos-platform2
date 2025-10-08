// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/runtime_hwid_utils/runtime_hwid_utils_impl.h"

#include <memory>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/file_utils.h>
#include <gtest/gtest.h>
#include <libcrossystem/crossystem_fake.h>

namespace hardware_verifier {

namespace {

constexpr char kRuntimeHWIDFilePath[] =
    "var/cache/hardware_verifier/runtime_hwid";

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

}  // namespace

}  // namespace hardware_verifier
