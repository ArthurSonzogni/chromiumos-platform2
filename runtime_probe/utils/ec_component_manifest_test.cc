// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/ec_component_manifest.h"

#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "runtime_probe/system/context_mock_impl.h"
#include "runtime_probe/utils/file_test_utils.h"

namespace runtime_probe {
namespace {

class EcComponentManifestTest : public BaseFileTest {
 protected:
  ContextMockImpl& mock_context() & { return mock_context_; }

  void SetUpEcComponentManifest(const std::string& image_name,
                                const std::string& case_name) {
    const std::string file_path =
        base::StringPrintf("cme/component_manifest.%s.json", case_name.c_str());
    mock_context().fake_cros_config()->SetString(
        kCrosConfigImageNamePath, kCrosConfigImageNameKey, image_name);
    const base::FilePath manifest_dir =
        Context::Get()->root_dir().Append(kCmePath).Append(image_name);
    ASSERT_TRUE(base::CreateDirectory(manifest_dir));
    // TODO(kevinptt): Move the logic to a helper function in BaseFileTest.
    ASSERT_TRUE(base::CopyFile(GetTestDataPath().Append(file_path),
                               manifest_dir.Append(kEcComponentManifestName)));
  }

 private:
  ::testing::NiceMock<ContextMockImpl> mock_context_;
};

class EcComponentManifestTestBasic : public EcComponentManifestTest {
 protected:
  void SetUp() override { SetUpEcComponentManifest("image1", "basic"); }
};

TEST_F(EcComponentManifestTest, EcComponentManifestReader_ReadEmptySuccess) {
  SetUpEcComponentManifest("image1", "empty");
  const auto manifest = EcComponentManifestReader::Read();
  EXPECT_TRUE(manifest.has_value());
}

TEST_F(EcComponentManifestTest,
       EcComponentManifestReader_ReadWithMissingImageNameFailed) {
  const auto manifest = EcComponentManifestReader::Read();
  EXPECT_FALSE(manifest.has_value());
}

TEST_F(EcComponentManifestTest,
       EcComponentManifestReader_ReadNonexistentManifestFailed) {
  mock_context().fake_cros_config()->SetString(
      kCrosConfigImageNamePath, kCrosConfigImageNameKey, "image1");
  const auto manifest = EcComponentManifestReader::Read();
  EXPECT_FALSE(manifest.has_value());
}

TEST_F(EcComponentManifestTest,
       EcComponentManifestReader_ReadInvalidManifestFailed) {
  SetUpEcComponentManifest("image1", "invalid-1");
  const auto manifest = EcComponentManifestReader::Read();
  EXPECT_FALSE(manifest.has_value());
}

TEST_F(EcComponentManifestTestBasic, EcComponentManifestReader_ReadSuccess) {
  const auto manifest = EcComponentManifestReader::Read();

  EXPECT_TRUE(manifest.has_value());
  EXPECT_EQ(manifest->version, 1);
  EXPECT_EQ(manifest->component_list.size(), 2);
  {
    const EcComponentManifest::Component& comp = manifest->component_list[0];
    EXPECT_EQ(comp.component_type, "base_sensor");
    EXPECT_EQ(comp.component_name, "base_sensor_1");
    EXPECT_EQ(comp.i2c.port, 3);
    EXPECT_EQ(comp.i2c.addr, 0x1);
    EXPECT_EQ(comp.i2c.expect.size(), 2);
    EXPECT_EQ(comp.i2c.expect[0].reg, 0);
    EXPECT_EQ(comp.i2c.expect[0].value, 0);
    EXPECT_EQ(comp.i2c.expect[1].reg, 1);
    EXPECT_EQ(comp.i2c.expect[1].value, 1);
  }
  {
    const EcComponentManifest::Component& comp = manifest->component_list[1];
    EXPECT_EQ(comp.component_type, "base_sensor");
    EXPECT_EQ(comp.component_name, "base_sensor_2");
    EXPECT_EQ(comp.i2c.port, 3);
    EXPECT_EQ(comp.i2c.addr, 0x2);
    EXPECT_EQ(comp.i2c.expect.size(), 0);
  }
}

}  // namespace
}  // namespace runtime_probe
