// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/ec_component_manifest.h"

#include <optional>

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

  void SetUpEcComponentManifestWithContents(const std::string& image_name,
                                            const std::string& contents) {
    mock_context().fake_cros_config()->SetString(
        kCrosConfigImageNamePath, kCrosConfigImageNameKey, image_name);
    const base::FilePath manifest_dir =
        Context::Get()->root_dir().Append(kCmePath).Append(image_name);
    ASSERT_TRUE(base::CreateDirectory(manifest_dir));
    ASSERT_TRUE(base::WriteFile(manifest_dir.Append(kEcComponentManifestName),
                                contents));
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
  const auto reader = EcComponentManifestReader("model-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_TRUE(manifest.has_value());
}

TEST_F(EcComponentManifestTest,
       EcComponentManifestReader_ReadWithMissingImageNameFailed) {
  const auto reader = EcComponentManifestReader("model-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_FALSE(manifest.has_value());
}

TEST_F(EcComponentManifestTest,
       EcComponentManifestReader_ReadNonexistentManifestFailed) {
  mock_context().fake_cros_config()->SetString(
      kCrosConfigImageNamePath, kCrosConfigImageNameKey, "image1");
  const auto reader = EcComponentManifestReader("model-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_FALSE(manifest.has_value());
}

TEST_F(EcComponentManifestTest,
       EcComponentManifestReader_ManifestWithMissingField_ReadFailed) {
  SetUpEcComponentManifest("image1", "invalid.missing-field");
  const auto reader = EcComponentManifestReader("model-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_FALSE(manifest.has_value());
}

TEST_F(EcComponentManifestTest,
       EcComponentManifestReader_ManifestWithMaskLengthMismatch_ReadFailed) {
  SetUpEcComponentManifest("image1", "invalid.mask-length-mismatch");
  const auto reader = EcComponentManifestReader("model-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_FALSE(manifest.has_value());
}

TEST_F(EcComponentManifestTest,
       EcComponentManifestReader_ManifestWithByteLengthMismatch_ReadFailed) {
  SetUpEcComponentManifest("image1", "invalid.byte-length-mismatch");
  const auto reader = EcComponentManifestReader("model-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_FALSE(manifest.has_value());
}

TEST_F(EcComponentManifestTest,
       EcComponentManifestReader_ManifestWithConflictValue_ReadFailed) {
  SetUpEcComponentManifest("image1", "invalid.conflict-value");
  const auto reader = EcComponentManifestReader("model-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_FALSE(manifest.has_value());
}

TEST_F(EcComponentManifestTest,
       EcComponentManifestReader_ManifestWithConflictMask_ReadFailed) {
  SetUpEcComponentManifest("image1", "invalid.conflict-mask");
  const auto reader = EcComponentManifestReader("model-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_FALSE(manifest.has_value());
}

TEST_F(EcComponentManifestTestBasic,
       EcComponentManifestReader_EcVersionMismatch_ReadFailed) {
  const auto reader = EcComponentManifestReader("mismatch-version");

  const auto manifest = reader.Read();

  EXPECT_FALSE(manifest.has_value());
}

TEST_F(EcComponentManifestTestBasic, EcComponentManifestReader_ReadSuccess) {
  const auto reader = EcComponentManifestReader("model-0.0.0-abcdefa");

  const auto manifest = reader.Read();

  EXPECT_TRUE(manifest.has_value());
  EXPECT_EQ(manifest->manifest_version, 1);
  EXPECT_EQ(manifest->ec_version, "model-0.0.0-abcdefa");
  EXPECT_EQ(manifest->component_list.size(), 2);
  {
    const EcComponentManifest::Component& comp = manifest->component_list[0];
    EXPECT_EQ(comp.component_type, "base_sensor");
    EXPECT_EQ(comp.component_name, "base_sensor_1");
    EXPECT_EQ(comp.i2c.port, 3);
    EXPECT_EQ(comp.i2c.addr, 0x1);
    EXPECT_EQ(comp.i2c.expect.size(), 4);
    EXPECT_EQ(comp.i2c.expect[0].reg, 0);
    EXPECT_EQ(comp.i2c.expect[0].write_data,
              (std::vector<uint8_t>{0xaa, 0xbb, 0xcc}));
    EXPECT_EQ(comp.i2c.expect[0].value, std::vector<uint8_t>{0x00});
    EXPECT_EQ(comp.i2c.expect[0].mask, std::vector<uint8_t>{0xff});
    EXPECT_EQ(comp.i2c.expect[0].override_addr, std::nullopt);
    EXPECT_EQ(comp.i2c.expect[0].bytes, 1);
    EXPECT_EQ(comp.i2c.expect[1].reg, 1);
    EXPECT_EQ(comp.i2c.expect[1].write_data, std::vector<uint8_t>{});
    EXPECT_EQ(comp.i2c.expect[1].value, std::vector<uint8_t>{0x01});
    EXPECT_EQ(comp.i2c.expect[1].mask, std::vector<uint8_t>{0xff});
    EXPECT_EQ(comp.i2c.expect[1].override_addr, std::nullopt);
    EXPECT_EQ(comp.i2c.expect[1].bytes, 1);
    EXPECT_EQ(comp.i2c.expect[2].reg, 2);
    EXPECT_EQ(comp.i2c.expect[2].write_data, std::vector<uint8_t>{});
    EXPECT_EQ(comp.i2c.expect[2].value,
              (std::vector<uint8_t>{0x00, 0x00, 0x42, 0x00}));
    EXPECT_EQ(comp.i2c.expect[2].mask,
              (std::vector<uint8_t>{0x00, 0x00, 0xff, 0x00}));
    EXPECT_EQ(comp.i2c.expect[2].override_addr, std::nullopt);
    EXPECT_EQ(comp.i2c.expect[2].bytes, 4);
    EXPECT_EQ(comp.i2c.expect[3].reg, 3);
    EXPECT_EQ(comp.i2c.expect[3].write_data, std::vector<uint8_t>{});
    EXPECT_EQ(comp.i2c.expect[3].value, std::vector<uint8_t>{0x03});
    EXPECT_EQ(comp.i2c.expect[3].mask, std::vector<uint8_t>{0xff});
    EXPECT_EQ(comp.i2c.expect[3].override_addr, 0x2);
    EXPECT_EQ(comp.i2c.expect[3].bytes, 1);
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

namespace {

std::string FindAndReplace(const std::string& text,
                           const std::string& from,
                           const std::string& to) {
  std::string result = text;
  result.replace(text.find(from), from.length(), to);
  return result;
}

}  // namespace

TEST_F(EcComponentManifestTestBasic, EcComponentManifestReader_InvalidReg) {
  // Arrange, prepare a manifest file that contains only one invalid part ---
  // "reg" out of range.
  std::string valid_manifest_content = R"JSON(
      {
        "manifest_version": 1,
        "ec_version": "model-0.0.0-abcdefa",
        "component_list": [{
          "component_type": "the_comp_type",
          "component_name": "the_comp_name",
          "i2c": {
            "port": 3,
            "addr": "0x12",
            "expect": [{
              "reg": "0xab",
              "bytes": 0
            }]
          }
        }]
      })JSON";
  SetUpEcComponentManifestWithContents("the_unused_image_name",
                                       valid_manifest_content);
  ASSERT_TRUE(
      EcComponentManifestReader("model-0.0.0-abcdefa").Read().has_value());
  std::string invalid_manifest_content = FindAndReplace(
      valid_manifest_content, "\"reg\": \"0xab\"", "\"reg\": \"0xabcd\"");
  SetUpEcComponentManifestWithContents("the_unused_image_name",
                                       invalid_manifest_content);

  // Act, read the manifest
  const auto invalid_manifest =
      EcComponentManifestReader("model-0.0.0-abcdefa").Read();

  // Assert, check if the manifest file is indeed failed to read.
  EXPECT_FALSE(invalid_manifest.has_value());
}

}  // namespace
}  // namespace runtime_probe
