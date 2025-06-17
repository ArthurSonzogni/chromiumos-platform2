// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/ish_component_manifest.h"

#include <string>

#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "runtime_probe/system/context_mock_impl.h"
#include "runtime_probe/utils/ec_component_manifest.h"
#include "runtime_probe/utils/file_test_utils.h"

namespace runtime_probe {
namespace {

class IshComponentManifestTest : public BaseFileTest {
 protected:
  ContextMockImpl& mock_context() & { return mock_context_; }

  void SetUpIshComponentManifest(const std::string& ish_project_name,
                                 const std::string& case_name) {
    const std::string file_path =
        base::StringPrintf("cme/component_manifest.%s.json", case_name.c_str());
    const base::FilePath manifest_dir =
        Context::Get()->root_dir().Append(kCmePath).Append(ish_project_name);
    ASSERT_TRUE(base::CreateDirectory(manifest_dir));
    ASSERT_TRUE(base::CopyFile(GetTestDataPath().Append(file_path),
                               manifest_dir.Append(kEcComponentManifestName)));
  }

 private:
  ::testing::NiceMock<ContextMockImpl> mock_context_;
};

TEST_F(IshComponentManifestTest,
       IshComponentManifestReader_IshProjectNameWithoutSuffix_Success) {
  SetUpIshComponentManifest("model", "basic");
  const auto reader = IshComponentManifestReader("model-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_TRUE(manifest.has_value());
}

TEST_F(IshComponentManifestTest,
       IshComponentManifestReader_IshProjectNameWithSuffix_Success) {
  SetUpIshComponentManifest("model-ish", "basic.ish");
  const auto reader = IshComponentManifestReader("model-ish-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_TRUE(manifest.has_value());
}

TEST_F(IshComponentManifestTest,
       IshComponentManifestReader_IshProjectNameWithoutSuffixMismatch_Failed) {
  SetUpIshComponentManifest("model-ish", "basic");
  const auto reader = IshComponentManifestReader("model-ish-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_FALSE(manifest.has_value());
}

TEST_F(IshComponentManifestTest,
       IshComponentManifestReader_IshProjectNameWithSuffixMismatch_Failed) {
  SetUpIshComponentManifest("model", "basic.ish");
  const auto reader = IshComponentManifestReader("model-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_FALSE(manifest.has_value());
}

TEST_F(IshComponentManifestTest,
       IshComponentManifestReader_NoIshProjectNameInEcVersion_Failed) {
  SetUpIshComponentManifest("model-ish", "basic.ish");
  const auto reader = IshComponentManifestReader("invalid_ec_version");

  const auto manifest = reader.Read();

  EXPECT_FALSE(manifest.has_value());
}

}  // namespace
}  // namespace runtime_probe
