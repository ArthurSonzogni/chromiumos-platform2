// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/factory_hwid_processor_impl.h"

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/check.h>
#include <brillo/hwid/hwid_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hardware_verifier/encoding_spec_loader.h"
#include "hardware_verifier/test_utils.h"

namespace hardware_verifier {
namespace {

using runtime_probe::ProbeRequest_SupportCategory;
using ::testing::_;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;

class MockEncodingSpecLoader : public EncodingSpecLoader {
 public:
  MOCK_METHOD(std::unique_ptr<EncodingSpec>, Load, (), (const, override));
};

class FactoryHWIDProcessorImplTest : public BaseFileTest {
 protected:
  // Sets the HWID returned by crossystem to the given value.
  void SetFactoryHWID(const std::string hwid) {
    mock_context()->fake_crossystem()->VbSetSystemPropertyString("hwid", hwid);
  }

  std::unique_ptr<FactoryHWIDProcessorImpl> CreateFactoryHWIDProcessor(
      std::unique_ptr<EncodingSpec> spec) {
    NiceMock<MockEncodingSpecLoader> mock_encoding_spec_loader;
    EXPECT_CALL(mock_encoding_spec_loader, Load())
        .WillOnce(Return(ByMove(std::move(spec))));
    auto processor =
        FactoryHWIDProcessorImpl::Create(mock_encoding_spec_loader);
    CHECK(processor != nullptr);
    return processor;
  }

  EncodingPattern CreateEncodingPattern(
      const std::vector<int>& image_ids,
      const std::vector<std::tuple<ProbeRequest_SupportCategory, int, int>>&
          bit_ranges,
      const std::vector<std::pair<ProbeRequest_SupportCategory, int>>&
          zero_bit_positions) {
    EncodingPattern pattern;
    for (const auto image_id : image_ids) {
      pattern.add_image_ids(image_id);
    }
    for (const auto& [category, start, end] : bit_ranges) {
      auto* br = pattern.add_bit_ranges();
      br->set_category(category);
      br->set_start(start);
      br->set_end(end);
    }
    for (const auto& [category, pos] : zero_bit_positions) {
      auto* zb = pattern.add_first_zero_bits();
      zb->set_category(category);
      zb->set_zero_bit_position(pos);
    }
    return pattern;
  }

  EncodedFields CreateEncodedField(
      ProbeRequest_SupportCategory category,
      const std::map<int, std::vector<std::string>>& index_to_comp_names) {
    EncodedFields ef;
    ef.set_category(category);
    for (const auto& [index, comp_names] : index_to_comp_names) {
      auto* ec = ef.add_encoded_components();
      ec->set_index(index);
      for (const auto& name : comp_names) {
        ec->add_component_names(name);
      }
    }
    return ef;
  }

  std::unique_ptr<EncodingSpec> CreateTestEncodingSpec(
      const std::vector<EncodingPattern>& encoding_patterns,
      const std::vector<EncodedFields>& encoded_fields) {
    auto spec = std::make_unique<EncodingSpec>();
    for (const auto& ep : encoding_patterns) {
      spec->add_encoding_patterns()->CopyFrom(ep);
    }
    for (const auto& ef : encoded_fields) {
      spec->add_encoded_fields()->CopyFrom(ef);
    }
    return spec;
  }

  std::string ConstructHWID(const std::string& prefix,
                            const std::string& expected_decoded_bits) {
    auto encoded_hwid = brillo::hwid::EncodeHWID(prefix, expected_decoded_bits);
    CHECK(encoded_hwid.has_value());
    return encoded_hwid.value();
  }
};

TEST_F(FactoryHWIDProcessorImplTest, Create_Success) {
  std::string decoded_bits = "0000000";
  SetFactoryHWID(ConstructHWID("TESTMODEL", decoded_bits));
  std::vector<EncodingPattern> encoding_patterns = {
      CreateEncodingPattern({0}, {}, {})};
  auto spec = CreateTestEncodingSpec(encoding_patterns, {});
  NiceMock<MockEncodingSpecLoader> mock_loader;
  EXPECT_CALL(mock_loader, Load()).WillOnce(Return(ByMove(std::move(spec))));

  auto processor = FactoryHWIDProcessorImpl::Create(mock_loader);

  EXPECT_NE(processor, nullptr);
}

TEST_F(FactoryHWIDProcessorImplTest, Create_LoadFailed_Failure) {
  NiceMock<MockEncodingSpecLoader> mock_loader;
  EXPECT_CALL(mock_loader, Load()).WillOnce(Return(nullptr));

  auto processor = FactoryHWIDProcessorImpl::Create(mock_loader);

  EXPECT_EQ(processor, nullptr);
}

TEST_F(FactoryHWIDProcessorImplTest, Create_CrossystemFailed_Failure) {
  std::vector<EncodingPattern> encoding_patterns = {
      CreateEncodingPattern({0}, {}, {})};
  auto spec = CreateTestEncodingSpec(encoding_patterns, {});
  NiceMock<MockEncodingSpecLoader> mock_loader;
  EXPECT_CALL(mock_loader, Load()).WillOnce(Return(ByMove(std::move(spec))));

  auto processor = FactoryHWIDProcessorImpl::Create(mock_loader);

  EXPECT_EQ(processor, nullptr);
}

TEST_F(FactoryHWIDProcessorImplTest, Create_DecodeHWIDFailed_Failure) {
  SetFactoryHWID("INVALID HWID");
  std::vector<EncodingPattern> encoding_patterns = {
      CreateEncodingPattern({0}, {}, {})};
  auto spec = CreateTestEncodingSpec(encoding_patterns, {});
  NiceMock<MockEncodingSpecLoader> mock_loader;
  EXPECT_CALL(mock_loader, Load()).WillOnce(Return(ByMove(std::move(spec))));

  auto processor = FactoryHWIDProcessorImpl::Create(mock_loader);

  EXPECT_EQ(processor, nullptr);
}

TEST_F(FactoryHWIDProcessorImplTest, Create_HWIDTooShortFailed_Failure) {
  SetFactoryHWID(ConstructHWID("TESTMODEL", "000"));
  std::vector<EncodingPattern> encoding_patterns = {
      CreateEncodingPattern({0}, {}, {})};
  auto spec = CreateTestEncodingSpec(encoding_patterns, {});
  NiceMock<MockEncodingSpecLoader> mock_loader;
  EXPECT_CALL(mock_loader, Load()).WillOnce(Return(ByMove(std::move(spec))));

  auto processor = FactoryHWIDProcessorImpl::Create(mock_loader);

  EXPECT_EQ(processor, nullptr);
}

TEST_F(FactoryHWIDProcessorImplTest, Create_NoMatchingEncodingPattern_Failure) {
  // Image ID = 0000 = 0. No pattern with image ID = 0.
  std::string decoded_bits = "0000000";
  SetFactoryHWID(ConstructHWID("TESTMODEL", decoded_bits));
  std::vector<EncodingPattern> encoding_patterns = {
      CreateEncodingPattern({1}, {}, {})};
  auto spec = CreateTestEncodingSpec(encoding_patterns, {});
  NiceMock<MockEncodingSpecLoader> mock_loader;
  EXPECT_CALL(mock_loader, Load()).WillOnce(Return(ByMove(std::move(spec))));

  auto processor = FactoryHWIDProcessorImpl::Create(mock_loader);

  EXPECT_EQ(processor, nullptr);
}

TEST_F(FactoryHWIDProcessorImplTest, DecodeFactoryHWID_WithoutZeroBits) {
  // 0 0001(image ID) 010 0(camera) 101 01(battery) 11 01(camera)
  // battery: 01 = index 1
  // camera: 010 = index 2
  std::string decoded_bits = "000010100101011101";
  SetFactoryHWID(ConstructHWID("TESTMODEL", decoded_bits));
  std::vector<EncodingPattern> encoding_patterns = {
      CreateEncodingPattern(
          {0, 1},
          {{runtime_probe::ProbeRequest_SupportCategory_camera, 3, 3},
           {runtime_probe::ProbeRequest_SupportCategory_battery, 7, 8},
           {runtime_probe::ProbeRequest_SupportCategory_camera, 11, 12},
           {runtime_probe::ProbeRequest_SupportCategory_wireless, 100, 100}},
          {}),
      CreateEncodingPattern({2}, {}, {})};
  std::vector<EncodedFields> encoded_fields = {
      CreateEncodedField(runtime_probe::ProbeRequest_SupportCategory_camera,
                         {{0, {"camera_0_0"}},
                          {1, {"camera_1_1"}},
                          {2, {"camera_2_2", "camera_2_2#2"}}}),
      CreateEncodedField(
          runtime_probe::ProbeRequest_SupportCategory_battery,
          {{0, {"battery_3_3"}}, {1, {"battery_4_4"}}, {2, {"battery_5_5"}}}),
      CreateEncodedField(
          runtime_probe::ProbeRequest_SupportCategory_touchscreen,
          {{0, {"touchscreen_6_6"}}}),
      CreateEncodedField(runtime_probe::ProbeRequest_SupportCategory_wireless,
                         {{0, {"wireless_7_7"}}})};
  auto spec = CreateTestEncodingSpec(encoding_patterns, encoded_fields);
  auto processor = CreateFactoryHWIDProcessor(std::move(spec));

  auto result = processor->DecodeFactoryHWID();

  ASSERT_TRUE(result.has_value());
  const auto mapping = result.value();
  EXPECT_EQ(mapping.size(), 2);
  EXPECT_THAT(mapping.at(runtime_probe::ProbeRequest_SupportCategory_battery),
              testing::UnorderedElementsAre("battery_4_4"));
  EXPECT_THAT(mapping.at(runtime_probe::ProbeRequest_SupportCategory_camera),
              testing::UnorderedElementsAre("camera_2_2", "camera_2_2#2"));
}

TEST_F(FactoryHWIDProcessorImplTest, DecodeFactoryHWID_WithZeroBits) {
  // 0 0001(image ID) 010 0(camera) 101 01(battery) 11 01(camera)
  // battery: 01 = index 1
  // camera: 010 = index 2
  std::string decoded_bits = "000010100101011101";
  SetFactoryHWID(ConstructHWID("TESTMODEL", decoded_bits));
  std::vector<EncodingPattern> encoding_patterns = {
      CreateEncodingPattern(
          {0, 1},
          {{runtime_probe::ProbeRequest_SupportCategory_camera, 3, 3},
           {runtime_probe::ProbeRequest_SupportCategory_battery, 7, 8},
           {runtime_probe::ProbeRequest_SupportCategory_camera, 11, 12},
           {runtime_probe::ProbeRequest_SupportCategory_wireless, 100, 100}},
          {{runtime_probe::ProbeRequest_SupportCategory_camera, 5},
           {runtime_probe::ProbeRequest_SupportCategory_touchscreen, 12},
           {runtime_probe::ProbeRequest_SupportCategory_wireless, 13}}),
      CreateEncodingPattern({2}, {}, {})};
  std::vector<EncodedFields> encoded_fields = {
      CreateEncodedField(runtime_probe::ProbeRequest_SupportCategory_camera,
                         {{0, {"camera_0_0"}},
                          {1, {"camera_1_1"}},
                          {2, {"camera_2_2", "camera_2_2#2"}}}),
      CreateEncodedField(
          runtime_probe::ProbeRequest_SupportCategory_battery,
          {{0, {"battery_3_3"}}, {1, {"battery_4_4"}}, {2, {"battery_5_5"}}}),
      CreateEncodedField(
          runtime_probe::ProbeRequest_SupportCategory_touchscreen,
          {{0, {"touchscreen_6_6"}}}),
      CreateEncodedField(runtime_probe::ProbeRequest_SupportCategory_wireless,
                         {{0, {"wireless_7_7"}}})};
  auto spec = CreateTestEncodingSpec(encoding_patterns, encoded_fields);
  auto processor = CreateFactoryHWIDProcessor(std::move(spec));

  auto result = processor->DecodeFactoryHWID();

  ASSERT_TRUE(result.has_value());
  const auto mapping = result.value();
  EXPECT_EQ(mapping.size(), 3);
  EXPECT_THAT(mapping.at(runtime_probe::ProbeRequest_SupportCategory_battery),
              testing::UnorderedElementsAre("battery_4_4"));
  EXPECT_THAT(mapping.at(runtime_probe::ProbeRequest_SupportCategory_camera),
              testing::UnorderedElementsAre("camera_2_2", "camera_2_2#2"));
  EXPECT_THAT(
      mapping.at(runtime_probe::ProbeRequest_SupportCategory_touchscreen),
      testing::UnorderedElementsAre("touchscreen_6_6"));
}

TEST_F(FactoryHWIDProcessorImplTest,
       DecodeFactoryHWID_CategoryNotInEncodedFields) {
  // 0 0001(image ID) 0(camera)
  std::string decoded_bits = "000010";
  SetFactoryHWID(ConstructHWID("TESTMODEL", decoded_bits));
  std::vector<EncodingPattern> encoding_patterns = {CreateEncodingPattern(
      {0, 1}, {{runtime_probe::ProbeRequest_SupportCategory_camera, 0, 0}},
      {})};
  std::vector<EncodedFields> encoded_fields = {CreateEncodedField(
      runtime_probe::ProbeRequest_SupportCategory_battery,
      {{0, {"battery_3_3"}}, {1, {"battery_4_4"}}, {2, {"battery_5_5"}}})};
  auto spec = CreateTestEncodingSpec(encoding_patterns, encoded_fields);
  auto processor = CreateFactoryHWIDProcessor(std::move(spec));

  auto result = processor->DecodeFactoryHWID();

  EXPECT_EQ(result, std::nullopt);
}

TEST_F(FactoryHWIDProcessorImplTest,
       DecodeFactoryHWID_ComponentIndexNotInEncodedFields) {
  // 0 0001(image ID) 0(camera)
  std::string decoded_bits = "000010";
  SetFactoryHWID(ConstructHWID("TESTMODEL", decoded_bits));
  std::vector<EncodingPattern> encoding_patterns = {CreateEncodingPattern(
      {0, 1}, {{runtime_probe::ProbeRequest_SupportCategory_camera, 0, 0}},
      {})};
  std::vector<EncodedFields> encoded_fields = {CreateEncodedField(
      runtime_probe::ProbeRequest_SupportCategory_camera,
      {{1, {"camera_1_1"}}, {2, {"camera_2_2", "camera_2_2#2"}}})};
  auto spec = CreateTestEncodingSpec(encoding_patterns, encoded_fields);
  auto processor = CreateFactoryHWIDProcessor(std::move(spec));

  auto result = processor->DecodeFactoryHWID();

  EXPECT_EQ(result, std::nullopt);
}

}  // namespace
}  // namespace hardware_verifier
