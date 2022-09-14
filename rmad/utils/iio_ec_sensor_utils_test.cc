// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/iio_ec_sensor_utils_impl.h"

#include <array>
#include <memory>
#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <gtest/gtest.h>

#include "rmad/utils/mock_cmd_utils.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace {

constexpr char kTestIioSysfsPrefix[] = "iio_test_";
constexpr int kNumberOfIioEntries = 19;

constexpr char kTestFreqAvailableLocation[] = "test_freq_available_location";
constexpr char kTestScaleLocation[] = "test_scale_location";
constexpr char kTestSysValueLocation[] = "test_sys_value_location";

constexpr char kTestIntName[] = "test_int";
constexpr char kTestFloatName[] = "test_float";
constexpr char kTestFloatRangeName[] = "test_float_range";
constexpr char kTestDiscreteSetName[] = "test_discrete_set";
constexpr char kTestTrailingSpaceName[] = "test_trailing_space";
constexpr char kTestInvalidName[] = "test_invalid";
constexpr char kTestNotAvailableName[] = "test_not_available";
constexpr char kTestInitFailedName[] = "test_init_failed";

// Used for test cases at initialization.
constexpr std::array<const char*, 7> kIioEcProperties = {
    "location",
    "name",
    "sampling_frequency_available",
    "scale",
    "test_sys_entry1",
    "test_sys_entry2",
    "test_sys_entry3"};
constexpr std::array<std::array<const char*, 7>, kNumberOfIioEntries>
    kIioEcEntries = {{
        {"", kTestIntName, "0 13 208", "1.0"},
        {kTestFreqAvailableLocation, "", "0 13 208", "1.0"},
        {kTestFreqAvailableLocation, kTestIntName, "208", "1.0"},
        {kTestFreqAvailableLocation, kTestFloatName, "208.0", "1.0"},
        {kTestFreqAvailableLocation, kTestFloatRangeName, "0.0 13.0 208.0",
         "1.0"},
        {kTestFreqAvailableLocation, kTestDiscreteSetName, "0.0 13.0 26.0 52.0",
         "1.0"},
        {kTestFreqAvailableLocation, kTestTrailingSpaceName,
         "0.0 13.0 26.0 52.0   ", "1.0"},
        {kTestFreqAvailableLocation, kTestInvalidName, "123 abc", "1.0"},
        {kTestFreqAvailableLocation, kTestNotAvailableName, "", "1.0"},
        {kTestScaleLocation, kTestIntName, "0.0 13.0 208.0", "1"},
        {kTestScaleLocation, kTestFloatName, "0.0 13.0 208.0", "1.0"},
        {kTestScaleLocation, kTestTrailingSpaceName, "0.0 13.0 208.0", "1.0 "},
        {kTestScaleLocation, kTestInvalidName, "0.0 13.0 208.0", "1.0 abc"},
        {kTestScaleLocation, kTestNotAvailableName, "0.0 13.0 208.0", ""},
        {kTestSysValueLocation, kTestIntName, "0.0 13.0 208.0", "1.0", "1", "2",
         "3"},
        {kTestSysValueLocation, kTestFloatName, "0.0 13.0 208.0", "1.0", "1.0",
         "2.0", "3.0"},
        {kTestSysValueLocation, kTestInitFailedName, "0.0 13.0 208.0",
         "1.0 abc", "1.0", "2.0", "3.0"},
        {kTestSysValueLocation, kTestNotAvailableName, "0.0 13.0 208.0", "1.0",
         "1.0", "2.0", ""},
        {kTestSysValueLocation, kTestInvalidName, "0.0 13.0 208.0", "1.0",
         "1.0", "2.0", ""},
    }};

const std::vector<std::string> kTestSysEntries = {
    "test_sys_entry1", "test_sys_entry2", "test_sys_entry3"};
constexpr std::array<double, 3> kTestSysValues = {1.0, 2.0, 3.0};

const std::vector<std::string> kTestChannels = {"channel1", "channel2",
                                                "channel3"};
constexpr int kTestSamples = 3;
constexpr int kNumberFirstReadsDiscarded = 10;
constexpr char kInvalidData[] = R"(
channel1: 12345
channel2: 12345
channel3: 12345
)";
constexpr char kTestSensorData[] = R"(
channel1: 111
channel2: 222
channel3: 333
channel1: 110
channel2: 221
channel3: 332
channel1: 112
channel2: 223
channel3: 334
)";
constexpr std::array<double, 3> kTestAvgData = {111, 222, 333};
constexpr std::array<double, 3> kTestVariance = {1, 1, 1};

}  // namespace

namespace rmad {

class IioEcSensorUtilsImplTest : public testing::Test {
 public:
  IioEcSensorUtilsImplTest() {}

  std::unique_ptr<IioEcSensorUtilsImpl> CreateIioEcSensorUtils(
      const std::string& location,
      const std::string& name,
      const std::vector<std::pair<bool, std::string>> cmd_outputs = {}) {
    auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
    for (auto [return_value, output] : cmd_outputs) {
      EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _))
          .WillRepeatedly(
              DoAll(SetArgPointee<1>(output), Return(return_value)));
    }

    return std::make_unique<IioEcSensorUtilsImpl>(
        location, name,
        temp_dir_.GetPath().Append(kTestIioSysfsPrefix).MaybeAsASCII(),
        std::move(mock_cmd_utils));
  }

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base::FilePath base_path = temp_dir_.GetPath();
    for (int i = 0; i < kNumberOfIioEntries; i++) {
      std::string dirname = kTestIioSysfsPrefix + base::NumberToString(i);
      base::FilePath dir_path = base_path.AppendASCII(dirname);
      EXPECT_TRUE(base::CreateDirectory(dir_path));

      for (int j = 0; j < kIioEcProperties.size(); j++) {
        if (kIioEcEntries[i][j]) {
          base::FilePath file_path = dir_path.AppendASCII(kIioEcProperties[j]);
          base::WriteFile(file_path, kIioEcEntries[i][j],
                          strlen(kIioEcEntries[i][j]));
        }
      }
    }
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(IioEcSensorUtilsImplTest, Initialize_FreqAvailableInt_Success) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestFreqAvailableLocation, kTestIntName);

  EXPECT_TRUE(iio_ec_sensor_utils->IsInitialized());
  EXPECT_EQ(iio_ec_sensor_utils->GetLocation(), kTestFreqAvailableLocation);
  EXPECT_EQ(iio_ec_sensor_utils->GetName(), kTestIntName);
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_FreqAvailableFloat_Success) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestFreqAvailableLocation, kTestFloatName);

  EXPECT_TRUE(iio_ec_sensor_utils->IsInitialized());
  EXPECT_EQ(iio_ec_sensor_utils->GetLocation(), kTestFreqAvailableLocation);
  EXPECT_EQ(iio_ec_sensor_utils->GetName(), kTestFloatName);
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_FreqAvailableFloatRange_Success) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestFreqAvailableLocation, kTestFloatRangeName);

  EXPECT_TRUE(iio_ec_sensor_utils->IsInitialized());
  EXPECT_EQ(iio_ec_sensor_utils->GetLocation(), kTestFreqAvailableLocation);
  EXPECT_EQ(iio_ec_sensor_utils->GetName(), kTestFloatRangeName);
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_FreqAvailableDiscreteSet_Success) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestFreqAvailableLocation, kTestDiscreteSetName);

  EXPECT_TRUE(iio_ec_sensor_utils->IsInitialized());
  EXPECT_EQ(iio_ec_sensor_utils->GetLocation(), kTestFreqAvailableLocation);
  EXPECT_EQ(iio_ec_sensor_utils->GetName(), kTestDiscreteSetName);
}

TEST_F(IioEcSensorUtilsImplTest,
       Initialize_FreqAvailableTrailingSpace_Success) {
  auto iio_ec_sensor_utils = CreateIioEcSensorUtils(kTestFreqAvailableLocation,
                                                    kTestTrailingSpaceName);

  EXPECT_TRUE(iio_ec_sensor_utils->IsInitialized());
  EXPECT_EQ(iio_ec_sensor_utils->GetLocation(), kTestFreqAvailableLocation);
  EXPECT_EQ(iio_ec_sensor_utils->GetName(), kTestTrailingSpaceName);
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_FreqAvailableInvalid_Failed) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestFreqAvailableLocation, kTestInvalidName);

  EXPECT_FALSE(iio_ec_sensor_utils->IsInitialized());
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_FreqAvailableNotAvailable_Failed) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestFreqAvailableLocation, kTestNotAvailableName);

  EXPECT_FALSE(iio_ec_sensor_utils->IsInitialized());
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_ScaleInt_Success) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestScaleLocation, kTestIntName);

  EXPECT_TRUE(iio_ec_sensor_utils->IsInitialized());
  EXPECT_EQ(iio_ec_sensor_utils->GetLocation(), kTestScaleLocation);
  EXPECT_EQ(iio_ec_sensor_utils->GetName(), kTestIntName);
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_ScaleFloat_Success) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestScaleLocation, kTestFloatName);

  EXPECT_TRUE(iio_ec_sensor_utils->IsInitialized());
  EXPECT_EQ(iio_ec_sensor_utils->GetLocation(), kTestScaleLocation);
  EXPECT_EQ(iio_ec_sensor_utils->GetName(), kTestFloatName);
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_ScaleTrailingSpace_Success) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestScaleLocation, kTestTrailingSpaceName);

  EXPECT_TRUE(iio_ec_sensor_utils->IsInitialized());
  EXPECT_EQ(iio_ec_sensor_utils->GetLocation(), kTestScaleLocation);
  EXPECT_EQ(iio_ec_sensor_utils->GetName(), kTestTrailingSpaceName);
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_ScaleInvalid_Failed) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestScaleLocation, kTestInvalidName);

  EXPECT_FALSE(iio_ec_sensor_utils->IsInitialized());
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_ScaleNotAvailable_Failed) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestScaleLocation, kTestNotAvailableName);

  EXPECT_FALSE(iio_ec_sensor_utils->IsInitialized());
}

TEST_F(IioEcSensorUtilsImplTest, GetAvgData_Success) {
  std::string cmd_outputs;
  // Remove first few invalid reads as a workaround of crrev/c/1423123.
  // This would be fixed by FW update later.
  // TODO(genechang): Remove this workaround when new firmware is released.
  for (int i = 0; i < kNumberFirstReadsDiscarded; i++) {
    cmd_outputs += kInvalidData;
  }
  cmd_outputs += kTestSensorData;
  auto iio_ec_sensor_utils = CreateIioEcSensorUtils(
      kTestScaleLocation, kTestFloatName, {{true, cmd_outputs}});

  std::vector<double> avg_data;
  std::vector<double> variance;
  EXPECT_TRUE(iio_ec_sensor_utils->GetAvgData(kTestChannels, kTestSamples,
                                              &avg_data, &variance));

  EXPECT_EQ(avg_data.size(), kTestChannels.size());
  EXPECT_EQ(variance.size(), kTestChannels.size());

  for (int i = 0; i < kTestChannels.size(); i++) {
    EXPECT_DOUBLE_EQ(avg_data[i], kTestAvgData[i]);
    EXPECT_DOUBLE_EQ(variance[i], kTestVariance[i]);
  }
}

TEST_F(IioEcSensorUtilsImplTest, GetAvgData_OneSample_Failed) {
  std::string cmd_outputs;
  // Remove first few invalid reads as a workaround of crrev/c/1423123.
  // This would be fixed by FW update later.
  // TODO(genechang): Remove this workaround when new firmware is released.
  for (int i = 0; i < kNumberFirstReadsDiscarded; i++) {
    cmd_outputs += kInvalidData;
  }
  // One sample case, we need at least 2 samples to calculate variance.
  cmd_outputs += "channel1: 111 channel2: 222 channel3: 333";
  auto iio_ec_sensor_utils = CreateIioEcSensorUtils(
      kTestScaleLocation, kTestFloatName, {{true, cmd_outputs}});

  std::vector<double> avg_data;
  std::vector<double> variance;
  EXPECT_FALSE(
      iio_ec_sensor_utils->GetAvgData(kTestChannels, 1, &avg_data, &variance));
}

TEST_F(IioEcSensorUtilsImplTest, GetAvgData_NoVariance_Success) {
  std::string cmd_outputs;
  // Remove first few invalid reads as a workaround of crrev/c/1423123.
  // This would be fixed by FW update later.
  // TODO(genechang): Remove this workaround when new firmware is released.
  for (int i = 0; i < kNumberFirstReadsDiscarded; i++) {
    cmd_outputs += kInvalidData;
  }
  cmd_outputs += kTestSensorData;
  auto iio_ec_sensor_utils = CreateIioEcSensorUtils(
      kTestScaleLocation, kTestFloatName, {{true, cmd_outputs}});

  std::vector<double> avg_data;
  EXPECT_TRUE(
      iio_ec_sensor_utils->GetAvgData(kTestChannels, kTestSamples, &avg_data));

  EXPECT_EQ(avg_data.size(), kTestChannels.size());

  for (int i = 0; i < kTestChannels.size(); i++) {
    EXPECT_DOUBLE_EQ(avg_data[i], kTestAvgData[i]);
  }
}

TEST_F(IioEcSensorUtilsImplTest, GetAvgData_OneSampleNoVariance_Success) {
  std::string cmd_outputs;
  // Remove first few invalid reads as a workaround of crrev/c/1423123.
  // This would be fixed by FW update later.
  // TODO(genechang): Remove this workaround when new firmware is released.
  for (int i = 0; i < kNumberFirstReadsDiscarded; i++) {
    cmd_outputs += kInvalidData;
  }
  // One sample case, if we don't ask for it, util won't calculate the variance,
  // so one sample will still get the data successfully.
  cmd_outputs += "channel1: 111 channel2: 222 channel3: 333";
  auto iio_ec_sensor_utils = CreateIioEcSensorUtils(
      kTestScaleLocation, kTestFloatName, {{true, cmd_outputs}});

  std::vector<double> avg_data;
  EXPECT_TRUE(iio_ec_sensor_utils->GetAvgData(kTestChannels, 1, &avg_data));

  EXPECT_EQ(avg_data.size(), kTestChannels.size());

  for (int i = 0; i < kTestChannels.size(); i++) {
    EXPECT_DOUBLE_EQ(avg_data[i], kTestAvgData[i]);
  }
}

TEST_F(IioEcSensorUtilsImplTest, GetAvgData_NotInitialized_Failed) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestScaleLocation, kTestInvalidName);

  std::vector<double> avg_data;
  EXPECT_FALSE(
      iio_ec_sensor_utils->GetAvgData(kTestChannels, kTestSamples, &avg_data));
}

TEST_F(IioEcSensorUtilsImplTest, GetAvgData_NoOuput_Failed) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestScaleLocation, kTestFloatName, {{false, ""}});

  std::vector<double> avg_data;
  EXPECT_FALSE(
      iio_ec_sensor_utils->GetAvgData(kTestChannels, kTestSamples, &avg_data));
}

TEST_F(IioEcSensorUtilsImplTest, GetAvgData_WrongSize_Failed) {
  std::string cmd_outputs;
  // Remove first few invalid reads as a workaround of crrev/c/1423123.
  // This would be fixed by FW update later.
  // TODO(genechang): Remove this workaround when new firmware is released.
  for (int i = 0; i < kNumberFirstReadsDiscarded; i++) {
    cmd_outputs += kInvalidData;
  }
  cmd_outputs += kTestSensorData;
  // Add more data, which means the size will be larger than expected.
  cmd_outputs += kTestSensorData;

  auto iio_ec_sensor_utils = CreateIioEcSensorUtils(
      kTestScaleLocation, kTestFloatName, {{true, cmd_outputs}});

  std::vector<double> avg_data;
  EXPECT_FALSE(
      iio_ec_sensor_utils->GetAvgData(kTestChannels, kTestSamples, &avg_data));
}

TEST_F(IioEcSensorUtilsImplTest, GetSysValue_Int_Success) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestSysValueLocation, kTestIntName);

  std::vector<double> values;
  EXPECT_TRUE(iio_ec_sensor_utils->GetSysValues(kTestSysEntries, &values));
  EXPECT_EQ(values.size(), kTestSysEntries.size());
  for (int i = 0; i < kTestSysEntries.size(); i++) {
    EXPECT_DOUBLE_EQ(values[i], kTestSysValues[i]);
  }
}

TEST_F(IioEcSensorUtilsImplTest, GetSysValue_Float_Success) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestSysValueLocation, kTestFloatName);

  std::vector<double> values;
  EXPECT_TRUE(iio_ec_sensor_utils->GetSysValues(kTestSysEntries, &values));
  EXPECT_EQ(values.size(), kTestSysEntries.size());
  for (int i = 0; i < kTestSysEntries.size(); i++) {
    EXPECT_DOUBLE_EQ(values[i], kTestSysValues[i]);
  }
}

TEST_F(IioEcSensorUtilsImplTest, GetSysValue_NotInitialized_Failed) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestSysValueLocation, kTestInitFailedName);

  std::vector<double> values;
  EXPECT_FALSE(iio_ec_sensor_utils->GetSysValues(kTestSysEntries, &values));
}

TEST_F(IioEcSensorUtilsImplTest, GetSysValue_EntryNotAvailable_Failed) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestSysValueLocation, kTestNotAvailableName);

  std::vector<double> values;
  EXPECT_FALSE(iio_ec_sensor_utils->GetSysValues(kTestSysEntries, &values));
}

TEST_F(IioEcSensorUtilsImplTest, GetSysValue_InvalidValue_Failed) {
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(kTestSysValueLocation, kTestInvalidName);

  std::vector<double> values;
  EXPECT_FALSE(iio_ec_sensor_utils->GetSysValues(kTestSysEntries, &values));
}

}  // namespace rmad
