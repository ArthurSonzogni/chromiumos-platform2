// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/ec_component.h"

#include <fcntl.h>

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/json/json_reader.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/get_version_command.h>
#include <libec/i2c_passthru_command.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/utils/ec_component_manifest.h"
#include "runtime_probe/utils/function_test_utils.h"
#include "runtime_probe/utils/ish_component_manifest.h"

namespace runtime_probe {
namespace {

using ::testing::_;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;

// Status codes defined in ec/include/ec_commands.h .
constexpr uint32_t kEcResultSuccess = 0;
constexpr uint32_t kEcResultTimeout = 10;
constexpr uint8_t kEcI2cStatusSuccess = 0;

constexpr auto kEcDevPath("dev/cros_ec");
constexpr auto kIshDevPath("dev/cros_ish");

class EcComponentFunctionTest : public BaseFunctionTest {
 protected:
  void SetUp() override {
    // Default to EC device.
    SetUpEcDevice();
  }

  class FakeGetEcVersionCommand : public ec::GetVersionCommand {
    using ec::GetVersionCommand::GetVersionCommand;

   public:
    struct ec_response_get_version* Resp() override { return &resp_.value(); }

    bool EcCommandRun(int fd) override {
      const auto file_path = brillo::GetFDPath(fd);
      if (base::EndsWith(file_path.value(), kEcDevPath)) {
        resp_ = ec_resp_;
      } else if (base::EndsWith(file_path.value(), kIshDevPath)) {
        resp_ = ish_resp_;
      }
      return resp_.has_value();
    }

    std::optional<struct ec_response_get_version> resp_;
    std::optional<struct ec_response_get_version> ec_resp_;
    std::optional<struct ec_response_get_version> ish_resp_;
  };

  class MockI2cPassthruCommand : public ec::I2cPassthruCommand {
   public:
    template <typename T = MockI2cPassthruCommand>
    static std::unique_ptr<T> Create() {
      return ec::I2cPassthruCommand::Create<T>(0, 0, {0}, 1);
    }

    MOCK_METHOD(bool, Run, (int), (override));
    MOCK_METHOD(base::span<const uint8_t>, RespData, (), (const override));
    MOCK_METHOD(uint32_t, Result, (), (const override));
    MOCK_METHOD(uint8_t, I2cStatus, (), (const override));
  };

  class MockEcComponentFunction : public EcComponentFunction {
    using EcComponentFunction::EcComponentFunction;

   public:
    std::unique_ptr<ec::GetVersionCommand> GetGetVersionCommand()
        const override {
      auto cmd = std::make_unique<FakeGetEcVersionCommand>();
      cmd->ec_resp_ = ec_response_get_version_;
      cmd->ish_resp_ = ish_response_get_version_;
      return cmd;
    }

    MOCK_METHOD(std::unique_ptr<ec::I2cPassthruCommand>,
                GetI2cReadCommand,
                (uint8_t port,
                 uint8_t addr7,
                 uint8_t offset,
                 const std::vector<uint8_t>& write_data,
                 uint8_t read_len),
                (const override));

    std::optional<struct ec_response_get_version> ec_response_get_version_{
        {.version_string_ro = "ro_version",
         .version_string_rw = "model-0.0.0-abcdefa",
         .current_image = EC_IMAGE_RW}};
    std::optional<struct ec_response_get_version> ish_response_get_version_{
        {.version_string_ro = "ro_version",
         .version_string_rw = "model-ish-0.0.0-abcdefa",
         .current_image = EC_IMAGE_RW}};
  };

  void SetUpEcDevice() { SetFile(kEcDevPath, ""); }

  void SetUpIshDevice() { SetFile(kIshDevPath, ""); }

  void SetUpEcComponentManifest(const std::string& image_name,
                                const std::string& case_name) {
    const std::string file_path =
        base::StringPrintf("cme/component_manifest.%s.json", case_name.c_str());
    mock_context()->fake_cros_config()->SetString(
        kCrosConfigImageNamePath, kCrosConfigImageNameKey, image_name);
    const base::FilePath manifest_dir =
        base::FilePath{kCmePath}.Append(image_name);
    SetDirectory(manifest_dir);
    ASSERT_TRUE(base::CopyFile(
        GetTestDataPath().Append(file_path),
        GetPathUnderRoot(manifest_dir.Append(kEcComponentManifestName))));
  }

  void SetUpIshComponentManifest(const std::string& ish_project_name,
                                 const std::string& case_name) {
    const std::string file_path =
        base::StringPrintf("cme/component_manifest.%s.json", case_name.c_str());
    const base::FilePath manifest_dir =
        base::FilePath{kIshCmePath}.Append(ish_project_name);
    SetDirectory(manifest_dir);
    ASSERT_TRUE(base::CopyFile(
        GetTestDataPath().Append(file_path),
        GetPathUnderRoot(manifest_dir.Append(kEcComponentManifestName))));
  }

  void SetFakeEcComponentManifest(const std::string& content) {
    const std::string image_name = "fake_image";
    mock_context()->fake_cros_config()->SetString(
        kCrosConfigImageNamePath, kCrosConfigImageNameKey, image_name);
    SetFile({kCmePath, image_name, kEcComponentManifestName}, content);
  }

  void SetI2cReadSuccess(MockEcComponentFunction* probe_function,
                         uint8_t port,
                         uint8_t addr7) const {
    constexpr uint8_t kRturnValue[] = {0x00};
    auto cmd =
        MockI2cPassthruCommand::Create<NiceMock<MockI2cPassthruCommand>>();
    ON_CALL(*cmd, Run).WillByDefault(Return(true));
    ON_CALL(*cmd, Result).WillByDefault(Return(kEcResultSuccess));
    ON_CALL(*cmd, I2cStatus).WillByDefault(Return(kEcI2cStatusSuccess));
    ON_CALL(*cmd, RespData)
        .WillByDefault(Return(base::span<const uint8_t>{kRturnValue}));
    ON_CALL(*probe_function, GetI2cReadCommand(port, addr7, _, _, _))
        .WillByDefault(Return(ByMove(std::move(cmd))));
  }

  void ExpectI2cReadSuccess(MockEcComponentFunction* probe_function,
                            uint8_t port,
                            uint8_t addr7) const {
    constexpr uint8_t kRturnValue[] = {0x00};
    auto cmd =
        MockI2cPassthruCommand::Create<NiceMock<MockI2cPassthruCommand>>();
    ON_CALL(*cmd, Run).WillByDefault(Return(true));
    ON_CALL(*cmd, Result).WillByDefault(Return(kEcResultSuccess));
    ON_CALL(*cmd, I2cStatus).WillByDefault(Return(kEcI2cStatusSuccess));
    ON_CALL(*cmd, RespData)
        .WillByDefault(Return(base::span<const uint8_t>{kRturnValue}));
    EXPECT_CALL(*probe_function, GetI2cReadCommand(port, addr7, _, _, _))
        .WillOnce(Return(ByMove(std::move(cmd))));
  }

  void SetI2cReadSuccessWithResult(
      MockEcComponentFunction* probe_function,
      uint8_t port,
      uint8_t addr7,
      uint8_t offset,
      const std::vector<uint8_t>& write_data,
      uint8_t len,
      base::span<const uint8_t> return_value) const {
    auto cmd =
        MockI2cPassthruCommand::Create<NiceMock<MockI2cPassthruCommand>>();
    ON_CALL(*cmd, Run).WillByDefault(Return(true));
    ON_CALL(*cmd, Result).WillByDefault(Return(kEcResultSuccess));
    ON_CALL(*cmd, I2cStatus).WillByDefault(Return(kEcI2cStatusSuccess));
    ON_CALL(*cmd, RespData).WillByDefault(Return(return_value));
    ON_CALL(*probe_function,
            GetI2cReadCommand(port, addr7, offset, write_data, len))
        .WillByDefault(Return(ByMove(std::move(cmd))));
  }

  void SetI2cReadFailed(MockEcComponentFunction* probe_function,
                        uint8_t port,
                        uint8_t addr7) const {
    constexpr uint8_t kRturnValue[] = {0x00};
    auto cmd =
        MockI2cPassthruCommand::Create<NiceMock<MockI2cPassthruCommand>>();
    ON_CALL(*cmd, Run).WillByDefault(Return(false));
    ON_CALL(*cmd, Result).WillByDefault(Return(kEcResultTimeout));
    ON_CALL(*cmd, I2cStatus).WillByDefault(Return(kEcI2cStatusSuccess));
    ON_CALL(*cmd, RespData)
        .WillByDefault(Return(base::span<const uint8_t>{kRturnValue}));
    ON_CALL(*probe_function, GetI2cReadCommand(port, addr7, _, _, _))
        .WillByDefault(Return(ByMove(std::move(cmd))));
  }
};

class EcComponentFunctionTestNoExpect : public EcComponentFunctionTest {
 protected:
  void SetUp() override {
    EcComponentFunctionTest::SetUp();
    SetUpEcComponentManifest("image1", "no_expect");
  }
};

class EcComponentFunctionTestWithExpect : public EcComponentFunctionTest {
 protected:
  void SetUp() override {
    EcComponentFunctionTest::SetUp();
    SetUpEcComponentManifest("image1", "with_expect");
  }
};

class EcComponentFunctionTestWithIsh : public EcComponentFunctionTestNoExpect {
 protected:
  void SetUp() override {
    EcComponentFunctionTestNoExpect::SetUp();
    SetUpIshDevice();
    SetUpIshComponentManifest("model-ish", "no_expect_ish");
  }
};

TEST_F(EcComponentFunctionTest, ProbeWithInvalidManifestFailed) {
  auto arguments = base::JSONReader::Read("{}");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());
  ExpectUnorderedListEqual(EvalProbeFunction(probe_function.get()),
                           CreateProbeResultFromJson("[]"));
}

TEST_F(EcComponentFunctionTestNoExpect, ProbeWithTypeSucceed) {
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "bc12",
      "name": null
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  // bc12_1
  SetI2cReadSuccess(probe_function.get(), 2, 0x5f);
  // bc12_2
  SetI2cReadSuccess(probe_function.get(), 3, 0x5f);

  auto actual = EvalProbeFunction(probe_function.get());

  ExpectUnorderedListEqual(actual, CreateProbeResultFromJson(R"JSON(
    [
      {
        "component_type": "bc12",
        "component_name": "bc12_1"
      },
      {
        "component_type": "bc12",
        "component_name": "bc12_2"
      }
    ]
  )JSON"));
}

TEST_F(EcComponentFunctionTestNoExpect, ProbeWithNameSucceed) {
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "name": "bc12_1"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  // bc12_1
  SetI2cReadSuccess(probe_function.get(), 2, 0x5f);

  auto actual = EvalProbeFunction(probe_function.get());

  ExpectUnorderedListEqual(actual, CreateProbeResultFromJson(R"JSON(
    [
      {
        "component_type": "bc12",
        "component_name": "bc12_1"
      }
    ]
  )JSON"));
}

TEST_F(EcComponentFunctionTestNoExpect, ProbeWithTypeAndNameSucceed) {
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "bc12",
      "name": "bc12_1"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  // bc12_1
  SetI2cReadSuccess(probe_function.get(), 2, 0x5f);

  auto actual = EvalProbeFunction(probe_function.get());

  ExpectUnorderedListEqual(actual, CreateProbeResultFromJson(R"JSON(
    [
      {
        "component_type": "bc12",
        "component_name": "bc12_1"
      }
    ]
  )JSON"));
}

TEST_F(EcComponentFunctionTestNoExpect, ProbeI2cFailed) {
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "bc12",
      "name": "bc12_1"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  // bc12_1
  SetI2cReadFailed(probe_function.get(), 2, 0x5f);

  auto actual = EvalProbeFunction(probe_function.get());

  ExpectUnorderedListEqual(actual, CreateProbeResultFromJson("[]"));
}

TEST_F(EcComponentFunctionTestWithExpect, ProbeI2cValueMatch) {
  constexpr uint8_t kMatchValueForReg0[] = {0x00};
  constexpr uint8_t kMatchValueForReg1[] = {0x01};
  constexpr uint8_t kMatchValueForReg2[] = {0x02};
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "base_sensor",
      "name": "base_sensor_1"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());
  // base_sensor_1 without a mask
  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x00,
                              std::vector<uint8_t>{}, 1, kMatchValueForReg0);
  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x01,
                              std::vector<uint8_t>{}, 1, kMatchValueForReg1);
  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x02,
                              std::vector<uint8_t>{}, 1, kMatchValueForReg2);

  auto actual = EvalProbeFunction(probe_function.get());

  ExpectUnorderedListEqual(actual, CreateProbeResultFromJson(R"JSON(
    [
      {
        "component_type": "base_sensor",
        "component_name": "base_sensor_1"
      }
    ]
  )JSON"));
}

TEST_F(EcComponentFunctionTestWithExpect, ProbeI2cMultiBytesValueMatch) {
  constexpr uint8_t kMatchValueForReg3[] = {0x01, 0x02, 0x03, 0x04};
  constexpr uint8_t kMatchValuesForReg4[][4] = {{0x00, 0x11, 0x00, 0x22},
                                                {0x01, 0x11, 0x10, 0x22},
                                                {0x20, 0x11, 0x02, 0x22},
                                                {0xaa, 0x11, 0xbb, 0x22}};
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "base_sensor",
      "name": "base_sensor_3"
    }
  )JSON");

  for (const auto& match_value_for_reg4 : kMatchValuesForReg4) {
    auto probe_function =
        CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());
    SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x03,
                                std::vector<uint8_t>{}, 4, kMatchValueForReg3);
    // base_sensor_3 with mask 0x00ff00ff.
    SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x04,
                                std::vector<uint8_t>{}, 4,
                                match_value_for_reg4);

    auto actual = EvalProbeFunction(probe_function.get());

    ExpectUnorderedListEqual(actual, CreateProbeResultFromJson(R"JSON(
      [
        {
          "component_type": "base_sensor",
          "component_name": "base_sensor_3"
        }
      ]
    )JSON"));
  }
}

TEST_F(EcComponentFunctionTestWithExpect, ProbeI2cValueMismatch) {
  constexpr uint8_t kMismatchValueForReg0[] = {0xff};
  constexpr uint8_t kMatchValueForReg1[] = {0x01};
  constexpr uint8_t kMatchValueForReg2[] = {0x02};
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "base_sensor",
      "name": "base_sensor_1"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x00,
                              std::vector<uint8_t>{}, 1, kMismatchValueForReg0);
  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x01,
                              std::vector<uint8_t>{}, 1, kMatchValueForReg1);
  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x02,
                              std::vector<uint8_t>{}, 1, kMatchValueForReg2);

  ExpectUnorderedListEqual(EvalProbeFunction(probe_function.get()),
                           CreateProbeResultFromJson("[]"));
}

TEST_F(EcComponentFunctionTestWithExpect, ProbeI2cValueLengthMismatch) {
  constexpr uint8_t kMatchValueForReg0[] = {0x00};
  constexpr uint8_t kMismatchValueForReg1[] = {0x01, 0x12};
  constexpr uint8_t kMatchValueForReg2[] = {0x02};
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "base_sensor",
      "name": "base_sensor_1"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x00,
                              std::vector<uint8_t>{}, 1, kMatchValueForReg0);
  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x01,
                              std::vector<uint8_t>{}, 1, kMismatchValueForReg1);
  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x02,
                              std::vector<uint8_t>{}, 1, kMatchValueForReg2);

  ExpectUnorderedListEqual(EvalProbeFunction(probe_function.get()),
                           CreateProbeResultFromJson("[]"));
}

TEST_F(EcComponentFunctionTestWithExpect, ProbeI2cOnlyOneValueMismatch) {
  constexpr uint8_t kMismatchValue[] = {0xff};
  constexpr uint8_t kMatchedValueForReg0[] = {0x00};
  constexpr uint8_t kMatchedValueForReg3[] = {0x22};
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "base_sensor",
      "name": "base_sensor_1"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x00,
                              std::vector<uint8_t>{}, 1, kMatchedValueForReg0);
  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x01,
                              std::vector<uint8_t>{}, 1, kMismatchValue);
  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x02,
                              std::vector<uint8_t>{}, 1, kMatchedValueForReg3);

  ExpectUnorderedListEqual(EvalProbeFunction(probe_function.get()),
                           CreateProbeResultFromJson("[]"));
}

TEST_F(EcComponentFunctionTestWithExpect, ProbeI2cOptionalValue) {
  constexpr uint8_t kMismatchValue[] = {0xff};
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "base_sensor",
      "name": "base_sensor_2"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  // base_sensor_2
  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x02,
                              std::vector<uint8_t>{}, 1, kMismatchValue);

  auto actual = EvalProbeFunction(probe_function.get());

  ExpectUnorderedListEqual(actual, CreateProbeResultFromJson(R"JSON(
    [
      {
        "component_type": "base_sensor",
        "component_name": "base_sensor_2"
      }
    ]
  )JSON"));
}

TEST_F(EcComponentFunctionTestWithExpect, ProbeI2cWithWriteData) {
  constexpr uint8_t kUnusedI2cResponseValue[] = {0xff};
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "base_sensor",
      "name": "base_sensor_4"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  // base_sensor_2
  SetI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x03,
                              std::vector<uint8_t>{0xaa, 0xbb, 0xcc}, 1,
                              kUnusedI2cResponseValue);

  auto actual = EvalProbeFunction(probe_function.get());

  ExpectUnorderedListEqual(actual, CreateProbeResultFromJson(R"JSON(
    [
      {
        "component_type": "base_sensor",
        "component_name": "base_sensor_4"
      }
    ]
  )JSON"));
}

TEST_F(EcComponentFunctionTestWithIsh, ProbeWithIshComponentsSucceed) {
  auto arguments = base::JSONReader::Read("{}");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  // bc12_1 in the EC component manifest.
  SetI2cReadSuccess(probe_function.get(), 2, 0x5f);
  // charger_1 in the ISH component manifest.
  SetI2cReadSuccess(probe_function.get(), 4, 0x9);

  auto actual = EvalProbeFunction(probe_function.get());

  ExpectUnorderedListEqual(actual, CreateProbeResultFromJson(R"JSON(
    [
      {
        "component_type": "bc12",
        "component_name": "bc12_1"
      },
      {
        "component_type": "charger",
        "component_name": "charger_1"
      }
    ]
  )JSON"));
}

TEST_F(EcComponentFunctionTestWithIsh, ProbeWithoutIshDevFailed) {
  UnsetPath(kIshDevPath);
  auto arguments = base::JSONReader::Read("{}");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  // bc12_1 in the EC component manifest.
  SetI2cReadSuccess(probe_function.get(), 2, 0x5f);
  // charger_1 in the ISH component manifest.
  SetI2cReadSuccess(probe_function.get(), 4, 0x9);

  auto actual = EvalProbeFunction(probe_function.get());

  ExpectUnorderedListEqual(actual, CreateProbeResultFromJson(R"JSON(
    [
      {
        "component_type": "bc12",
        "component_name": "bc12_1"
      }
    ]
  )JSON"));
}

class EcComponentFunctionTestECVersion : public EcComponentFunctionTest {
 protected:
  void SetUp() override {
    EcComponentFunctionTest::SetUp();
    SetFakeEcComponentManifest(R"JSON(
      {
        "manifest_version": 1,
        "ec_version": "model-0.0.0-abcdefa",
        "component_list": [
          {
            "component_type": "base_sensor",
            "component_name": "base_sensor_2",
            "i2c": {
              "port": 3,
              "addr": "0x01"
            }
          }
        ]
      }
    )JSON");
  }

  void ExpectI2cRead(MockEcComponentFunction* probe_function) {
    // Expect read the only component in fake manifest above.
    ExpectI2cReadSuccess(probe_function, 3, 0x01);
  }

  void ExpectNoI2cRead(MockEcComponentFunction* probe_function) {
    EXPECT_CALL(*probe_function, GetI2cReadCommand(_, _, _, _, _)).Times(0);
  }

  std::unique_ptr<MockEcComponentFunction> probe_function_{
      CreateProbeFunction<MockEcComponentFunction>(base::Value::Dict{})};
};

TEST_F(EcComponentFunctionTestECVersion, MatchRO) {
  probe_function_->ec_response_get_version_ = {
      .version_string_ro = "model-0.0.0-abcdefa",
      .version_string_rw = "rw_version",
      .current_image = EC_IMAGE_RO};
  ExpectI2cRead(probe_function_.get());
  EvalProbeFunction(probe_function_.get());
}

TEST_F(EcComponentFunctionTestECVersion, MatchROB) {
  probe_function_->ec_response_get_version_ = {
      .version_string_ro = "model-0.0.0-abcdefa",
      .version_string_rw = "rw_version",
      .current_image = EC_IMAGE_RO_B};
  ExpectI2cRead(probe_function_.get());
  EvalProbeFunction(probe_function_.get());
}

TEST_F(EcComponentFunctionTestECVersion, MatchRW) {
  probe_function_->ec_response_get_version_ = {
      .version_string_ro = "ro_version",
      .version_string_rw = "model-0.0.0-abcdefa",
      .current_image = EC_IMAGE_RW};
  ExpectI2cRead(probe_function_.get());
  EvalProbeFunction(probe_function_.get());
}

TEST_F(EcComponentFunctionTestECVersion, MatchRWB) {
  probe_function_->ec_response_get_version_ = {
      .version_string_ro = "ro_version",
      .version_string_rw = "model-0.0.0-abcdefa",
      .current_image = EC_IMAGE_RW_B};
  ExpectI2cRead(probe_function_.get());
  EvalProbeFunction(probe_function_.get());
}

TEST_F(EcComponentFunctionTestECVersion, NotMatchRO) {
  probe_function_->ec_response_get_version_ = {
      .version_string_ro = "ro_version",
      .version_string_rw = "rw_version",
      .current_image = EC_IMAGE_RO};
  ExpectNoI2cRead(probe_function_.get());
  EvalProbeFunction(probe_function_.get());
}

TEST_F(EcComponentFunctionTestECVersion, NotMatchRW) {
  probe_function_->ec_response_get_version_ = {
      .version_string_ro = "ro_version",
      .version_string_rw = "rw_version",
      .current_image = EC_IMAGE_RW};
  ExpectNoI2cRead(probe_function_.get());
  EvalProbeFunction(probe_function_.get());
}

TEST_F(EcComponentFunctionTestECVersion, Unknown) {
  probe_function_->ec_response_get_version_ = {
      .version_string_ro = "model-0.0.0-abcdefa",
      .version_string_rw = "model-0.0.0-abcdefa",
      .current_image = EC_IMAGE_UNKNOWN};
  ExpectNoI2cRead(probe_function_.get());
  EvalProbeFunction(probe_function_.get());
}

TEST_F(EcComponentFunctionTestECVersion, GetECVersionFailed) {
  probe_function_->ec_response_get_version_ = std::nullopt;
  ExpectNoI2cRead(probe_function_.get());
  EvalProbeFunction(probe_function_.get());
}

TEST_F(EcComponentFunctionTest, ProbeWithManifestPathSuccess) {
  mock_context()->SetFactoryMode(true);
  auto manifest_path = "/a/fake/path/manifest.json";
  SetFile(manifest_path, R"JSON(
      {
        "manifest_version": 1,
        "ec_version": "model-0.0.0-abcdefa",
        "component_list": [
          {
            "component_type": "base_sensor",
            "component_name": "base_sensor_2",
            "i2c": {
              "port": 3,
              "addr": "0x01"
            }
          }
        ]
      }
    )JSON");
  base::Value::Dict argument;
  argument.Set("manifest_path", GetPathUnderRoot(manifest_path).value());

  auto probe_function = CreateProbeFunction<MockEcComponentFunction>(argument);
  ExpectI2cReadSuccess(probe_function.get(), 3, 0x01);
  EvalProbeFunction(probe_function.get());
}

TEST_F(EcComponentFunctionTest, ProbeWithManifestPathNonFactoryMode) {
  mock_context()->SetFactoryMode(false);
  base::Value::Dict argument;
  argument.Set("manifest_path", "/a/fake/path/manifest.json");
  ASSERT_FALSE(CreateProbeFunction<MockEcComponentFunction>(argument));
}

TEST_F(EcComponentFunctionTest, ProbeWithIshManifestPathSuccess) {
  mock_context()->SetFactoryMode(true);
  SetUpIshDevice();
  auto manifest_path = "/a/fake/path/manifest.json";
  SetFile(manifest_path, R"JSON(
      {
        "manifest_version": 1,
        "ec_version": "model-ish-0.0.0-abcdefa",
        "component_list": [
          {
            "component_type": "base_sensor",
            "component_name": "base_sensor_2",
            "i2c": {
              "port": 3,
              "addr": "0x01"
            }
          }
        ]
      }
    )JSON");
  base::Value::Dict argument;
  argument.Set("ish_manifest_path", GetPathUnderRoot(manifest_path).value());

  auto probe_function = CreateProbeFunction<MockEcComponentFunction>(argument);
  ExpectI2cReadSuccess(probe_function.get(), 3, 0x01);
  EvalProbeFunction(probe_function.get());
}

TEST_F(EcComponentFunctionTest, ProbeWithIshManifestPathNonFactoryMode) {
  mock_context()->SetFactoryMode(false);
  SetUpIshDevice();
  base::Value::Dict argument;
  argument.Set("ish_manifest_path", "/a/fake/path/manifest.json");
  ASSERT_FALSE(CreateProbeFunction<MockEcComponentFunction>(argument));
}

}  // namespace
}  // namespace runtime_probe
