// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/encoding_spec_loader.h"

#include <string>

#include <base/files/file_util.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>

#include "hardware_verifier/test_utils.h"

namespace hardware_verifier {
namespace {

using google::protobuf::util::MessageDifferencer;

class EncodingSpecLoaderTest : public BaseFileTest {
 protected:
  void SetUp() override {
    auto spec_path_a =
        GetTestDataPath().Append("encoding_spec/encoding_spec_a.txtpb");
    std::string spec_content_a;
    ASSERT_TRUE(base::ReadFileToString(spec_path_a, &spec_content_a));
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        spec_content_a, &encoding_spec_a));

    auto spec_path_b =
        GetTestDataPath().Append("encoding_spec/encoding_spec_b.txtpb");
    std::string spec_content_b;
    ASSERT_TRUE(base::ReadFileToString(spec_path_a, &spec_content_b));
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        spec_content_b, &encoding_spec_b));
  }

  // Sets the model name to the given value.
  void SetModelName(const std::string& val) {
    mock_context()->fake_cros_config()->SetString("/", "name", val);
  }

  // Sets the cros_debug flag to the given value.
  void SetCrosDebug(bool is_enabled) {
    mock_context()->fake_crossystem()->VbSetSystemPropertyInt(
        "cros_debug", static_cast<int>(is_enabled));
  }

  EncodingSpec encoding_spec_a;
  EncodingSpec encoding_spec_b;
};

TEST_F(EncodingSpecLoaderTest, Load_RootfsWhenCrosDebugDisabled_Success) {
  constexpr char kModelName[] = "ModelFoo";
  SetCrosDebug(false);
  SetModelName(kModelName);
  SetFile({"usr/local/etc/runtime_probe", kModelName, "encoding_spec.pb"},
          encoding_spec_a.SerializeAsString());
  SetFile({"etc/runtime_probe", kModelName, "encoding_spec.pb"},
          encoding_spec_b.SerializeAsString());

  EncodingSpecLoader loader;
  auto spec = loader.Load();

  EXPECT_NE(spec, nullptr);
  EXPECT_TRUE(MessageDifferencer::Equivalent(*spec, encoding_spec_b));
}

TEST_F(EncodingSpecLoaderTest,
       Load_StatefulPartitionWhenCrosDebugDisabled_Fail) {
  constexpr char kModelName[] = "ModelFoo";
  SetCrosDebug(false);
  SetModelName(kModelName);
  SetFile({"usr/local/etc/runtime_probe", kModelName, "encoding_spec.pb"},
          encoding_spec_a.SerializeAsString());

  EncodingSpecLoader loader;
  auto spec = loader.Load();

  EXPECT_EQ(spec, nullptr);
}

TEST_F(EncodingSpecLoaderTest,
       Load_StatefulPartitionWhenCrosDebugEnabled_Success) {
  constexpr char kModelName[] = "ModelFoo";
  SetCrosDebug(true);
  SetModelName(kModelName);
  SetFile({"usr/local/etc/runtime_probe", kModelName, "encoding_spec.pb"},
          encoding_spec_a.SerializeAsString());
  SetFile({"etc/runtime_probe", kModelName, "encoding_spec.pb"},
          encoding_spec_b.SerializeAsString());

  EncodingSpecLoader loader;
  auto spec = loader.Load();

  EXPECT_NE(spec, nullptr);
  EXPECT_TRUE(MessageDifferencer::Equivalent(*spec, encoding_spec_a));
}

TEST_F(EncodingSpecLoaderTest, Load_RootfsWhenCrosDebugEnabled_Success) {
  constexpr char kModelName[] = "ModelFoo";
  SetCrosDebug(true);
  SetModelName(kModelName);
  SetFile({"etc/runtime_probe", kModelName, "encoding_spec.pb"},
          encoding_spec_b.SerializeAsString());

  EncodingSpecLoader loader;
  auto spec = loader.Load();

  EXPECT_NE(spec, nullptr);
  EXPECT_TRUE(MessageDifferencer::Equivalent(*spec, encoding_spec_b));
}

TEST_F(EncodingSpecLoaderTest, Load_InvalidEncodingSpec_Fail) {
  constexpr char kModelName[] = "ModelFoo";
  SetCrosDebug(true);
  SetModelName(kModelName);
  SetFile({"etc/runtime_probe", kModelName, "encoding_spec.pb"},
          "invalid-content");

  EncodingSpecLoader loader;
  auto spec = loader.Load();

  EXPECT_EQ(spec, nullptr);
}

TEST_F(EncodingSpecLoaderTest, Load_GetModelNameFailed_Fail) {
  constexpr char kModelName[] = "ModelFoo";
  SetCrosDebug(true);
  SetFile({"usr/local/etc/runtime_probe", kModelName, "encoding_spec.pb"},
          encoding_spec_a.SerializeAsString());
  SetFile({"etc/runtime_probe", kModelName, "encoding_spec.pb"},
          encoding_spec_b.SerializeAsString());

  EncodingSpecLoader loader;
  auto spec = loader.Load();

  EXPECT_EQ(spec, nullptr);
}

}  // namespace
}  // namespace hardware_verifier
