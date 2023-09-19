// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/json/json_reader.h>
#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/i2c_read_command.h>

#include "runtime_probe/functions/ec_component.h"
#include "runtime_probe/utils/ec_component_manifest.h"
#include "runtime_probe/utils/function_test_utils.h"

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

class EcComponentFunctionTest : public BaseFunctionTest {
 protected:
  class MockI2cReadCommand : public ec::I2cReadCommand {
   public:
    template <typename T = MockI2cReadCommand>
    static std::unique_ptr<T> Create() {
      return ec::I2cReadCommand::Create<T>(0, 0, 0, 1);
    }

    MOCK_METHOD(bool, Run, (int), (override));
    MOCK_METHOD(uint32_t, Data, (), (const override));
    MOCK_METHOD(uint32_t, Result, (), (const override));
    MOCK_METHOD(uint8_t, I2cStatus, (), (const override));
  };

  class MockEcComponentFunction : public EcComponentFunction {
    using EcComponentFunction::EcComponentFunction;

   public:
    base::ScopedFD GetEcDevice() const override { return base::ScopedFD{}; }
    MOCK_METHOD(std::unique_ptr<ec::I2cReadCommand>,
                GetI2cReadCommand,
                (uint8_t port, uint8_t addr8, uint8_t offset, uint8_t read_len),
                (const override));
  };

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

  void ExpectI2cReadSuccess(MockEcComponentFunction* probe_function,
                            uint8_t port,
                            uint8_t addr8) const {
    auto cmd = MockI2cReadCommand::Create<NiceMock<MockI2cReadCommand>>();
    ON_CALL(*cmd, Run).WillByDefault(Return(true));
    ON_CALL(*cmd, Result).WillByDefault(Return(kEcResultSuccess));
    ON_CALL(*cmd, I2cStatus).WillByDefault(Return(kEcI2cStatusSuccess));
    ON_CALL(*cmd, Data).WillByDefault(Return(0x00));
    EXPECT_CALL(*probe_function, GetI2cReadCommand(port, addr8, _, _))
        .WillOnce(Return(ByMove(std::move(cmd))));
  }

  void ExpectI2cReadSuccessWithResult(MockEcComponentFunction* probe_function,
                                      uint8_t port,
                                      uint8_t addr8,
                                      uint8_t offset,
                                      uint8_t len,
                                      uint32_t return_value) const {
    auto cmd = MockI2cReadCommand::Create<NiceMock<MockI2cReadCommand>>();
    ON_CALL(*cmd, Run).WillByDefault(Return(true));
    ON_CALL(*cmd, Result).WillByDefault(Return(kEcResultSuccess));
    ON_CALL(*cmd, I2cStatus).WillByDefault(Return(kEcI2cStatusSuccess));
    ON_CALL(*cmd, Data).WillByDefault(Return(return_value));
    EXPECT_CALL(*probe_function, GetI2cReadCommand(port, addr8, offset, len))
        .WillOnce(Return(ByMove(std::move(cmd))));
  }

  void ExpectI2cReadFailed(MockEcComponentFunction* probe_function,
                           uint8_t port,
                           uint8_t addr8) const {
    auto cmd = MockI2cReadCommand::Create<NiceMock<MockI2cReadCommand>>();
    ON_CALL(*cmd, Run).WillByDefault(Return(false));
    ON_CALL(*cmd, Result).WillByDefault(Return(kEcResultTimeout));
    ON_CALL(*cmd, I2cStatus).WillByDefault(Return(kEcI2cStatusSuccess));
    ON_CALL(*cmd, Data).WillByDefault(Return(0x00));
    EXPECT_CALL(*probe_function, GetI2cReadCommand(port, addr8, _, _))
        .WillOnce(Return(ByMove(std::move(cmd))));
  }

 private:
};

class EcComponentFunctionTestNoExpect : public EcComponentFunctionTest {
 protected:
  void SetUp() override { SetUpEcComponentManifest("image1", "no_expect"); }
};

class EcComponentFunctionTestWithExpect : public EcComponentFunctionTest {
 protected:
  void SetUp() override { SetUpEcComponentManifest("image1", "with_expect"); }
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
      "type": "bc12"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  // bc12_1
  ExpectI2cReadSuccess(probe_function.get(), 2, 0x5f);
  // bc12_2
  ExpectI2cReadSuccess(probe_function.get(), 3, 0x5f);
  ExpectUnorderedListEqual(EvalProbeFunction(probe_function.get()),
                           CreateProbeResultFromJson(R"JSON(
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
  ExpectI2cReadSuccess(probe_function.get(), 2, 0x5f);
  ExpectUnorderedListEqual(EvalProbeFunction(probe_function.get()),
                           CreateProbeResultFromJson(R"JSON(
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
  ExpectI2cReadSuccess(probe_function.get(), 2, 0x5f);
  ExpectUnorderedListEqual(EvalProbeFunction(probe_function.get()),
                           CreateProbeResultFromJson(R"JSON(
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
  ExpectI2cReadFailed(probe_function.get(), 2, 0x5f);
  ExpectUnorderedListEqual(EvalProbeFunction(probe_function.get()),
                           CreateProbeResultFromJson("[]"));
}

TEST_F(EcComponentFunctionTestWithExpect, ProbeI2cValueMatch) {
  constexpr auto kMatchValue = 0x01;
  constexpr auto kMismatchValue = 0xff;
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "base_sensor",
      "name": "base_sensor_1"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  ExpectI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x00, 1,
                                 kMismatchValue);
  // base_sensor_1
  ExpectI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x01, 1,
                                 kMatchValue);
  ExpectUnorderedListEqual(EvalProbeFunction(probe_function.get()),
                           CreateProbeResultFromJson(R"JSON(
    [
      {
        "component_type": "base_sensor",
        "component_name": "base_sensor_1"
      }
    ]
  )JSON"));
}

TEST_F(EcComponentFunctionTestWithExpect, ProbeI2cValueMismatch) {
  constexpr auto kMismatchValue = 0xff;
  auto arguments = base::JSONReader::Read(R"JSON(
    {
      "type": "base_sensor",
      "name": "base_sensor_1"
    }
  )JSON");
  auto probe_function =
      CreateProbeFunction<MockEcComponentFunction>(arguments->GetDict());

  ExpectI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x00, 1,
                                 kMismatchValue);
  ExpectI2cReadSuccessWithResult(probe_function.get(), 3, 0x01, 0x01, 1,
                                 kMismatchValue);
  ExpectUnorderedListEqual(EvalProbeFunction(probe_function.get()),
                           CreateProbeResultFromJson("[]"));
}

}  // namespace
}  // namespace runtime_probe
