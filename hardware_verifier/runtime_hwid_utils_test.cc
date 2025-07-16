// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/runtime_hwid_utils.h"

#include <base/files/file_util.h>
#include <gtest/gtest.h>

#include "hardware_verifier/runtime_hwid_generator.h"
#include "hardware_verifier/test_utils.h"

namespace hardware_verifier {
namespace {

class RuntimeHWIDUtilsTest : public BaseFileTest {};

TEST_F(RuntimeHWIDUtilsTest,
       DeleteRuntimeHWIDFromDevice_FileNotExists_Success) {
  const auto runtime_hwid_path = GetPathUnderRoot(kRuntimeHWIDFilePath);
  ASSERT_FALSE(base::PathExists(runtime_hwid_path));

  EXPECT_TRUE(DeleteRuntimeHWIDFromDevice());
  EXPECT_FALSE(base::PathExists(runtime_hwid_path));
}

TEST_F(RuntimeHWIDUtilsTest, DeleteRuntimeHWIDFromDevice_FileExists_Success) {
  SetFile(kRuntimeHWIDFilePath, "");
  const auto runtime_hwid_path = GetPathUnderRoot(kRuntimeHWIDFilePath);
  ASSERT_TRUE(base::PathExists(runtime_hwid_path));

  EXPECT_TRUE(DeleteRuntimeHWIDFromDevice());
  EXPECT_FALSE(base::PathExists(runtime_hwid_path));
}

TEST_F(RuntimeHWIDUtilsTest, DeleteRuntimeHWIDFromDevice_DeleteFails_Failure) {
  // Make the path a directory to make the file unremovable.
  SetFile({kRuntimeHWIDFilePath, "fake-file"}, "");
  const auto runtime_hwid_path = GetPathUnderRoot(kRuntimeHWIDFilePath);
  ASSERT_TRUE(base::DirectoryExists(runtime_hwid_path));

  EXPECT_FALSE(DeleteRuntimeHWIDFromDevice());
  EXPECT_TRUE(base::PathExists(runtime_hwid_path));
}

}  // namespace
}  // namespace hardware_verifier
