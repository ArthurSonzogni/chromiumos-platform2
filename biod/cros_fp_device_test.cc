// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/cros_fp_device.h"

#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/ec_usb_endpoint.h>
#include <libec/fingerprint/fp_info_command.h>
#include <libec/fingerprint/fp_template_command.h>
#include <libec/mock_ec_command_factory.h>

#include "biod/mock_biod_metrics.h"
#include "biod/mock_cros_fp_device.h"
#include "ec/ec_commands.h"
#include "libec/ec_command_version_supported.h"
#include "libec/fingerprint/fp_sensor_errors.h"

using ec::EcCommandFactoryInterface;
using ec::EcCommandInterface;
using ec::FpInfoCommand;
using ec::FpMode;
using ec::FpModeCommand;
using ec::FpSensorErrors;
using ec::FpTemplateCommand;
using ec::GetFpModeCommand;
using testing::An;
using testing::NiceMock;
using testing::Return;

namespace biod {
namespace {

class MockEcCommandInterface : public EcCommandInterface {
 public:
  MOCK_METHOD(bool, Run, (int fd), (override));
  MOCK_METHOD(bool, Run, (ec::EcUsbEndpointInterface & uep), (override));
  MOCK_METHOD(bool,
              RunWithMultipleAttempts,
              (int fd, int num_attempts),
              (override));
  MOCK_METHOD(uint32_t, Version, (), (const, override));
  MOCK_METHOD(uint32_t, Command, (), (const, override));
};

class CrosFpDevice_SetFpMode : public testing::Test {
 public:
  CrosFpDevice_SetFpMode() {
    auto mock_command_factory = std::make_unique<ec::MockEcCommandFactory>();
    mock_ec_command_factory_ = mock_command_factory.get();
    mock_cros_fp_device_ = std::make_unique<MockCrosFpDevice>(
        &mock_biod_metrics_, std::move(mock_command_factory));
  }

 protected:
  class MockCrosFpDevice : public CrosFpDevice {
   public:
    MockCrosFpDevice(
        BiodMetricsInterface* biod_metrics,
        std::unique_ptr<EcCommandFactoryInterface> ec_command_factory)
        : CrosFpDevice(biod_metrics, std::move(ec_command_factory)) {}
    MOCK_METHOD(FpMode, GetFpMode, (), (override));
  };

  class MockFpModeCommand : public FpModeCommand {
   public:
    explicit MockFpModeCommand(const FpMode& mode) : FpModeCommand(mode) {
      ON_CALL(*this, Run).WillByDefault(Return(true));
    }
    MOCK_METHOD(bool, Run, (int fd), (override));
    MOCK_METHOD(ec_response_fp_mode*, Resp, (), (override));
  };

  metrics::MockBiodMetrics mock_biod_metrics_;
  ec::MockEcCommandFactory* mock_ec_command_factory_ = nullptr;
  std::unique_ptr<MockCrosFpDevice> mock_cros_fp_device_;
};

TEST_F(CrosFpDevice_SetFpMode, Success) {
  const FpMode mode = FpMode(FpMode::Mode::kMatch);
  EXPECT_CALL(*mock_ec_command_factory_, FpModeCommand(mode))
      .WillOnce([&mode]() {
        auto mock_fp_mode_command =
            std::make_unique<NiceMock<MockFpModeCommand>>(mode);
        EXPECT_CALL(*mock_fp_mode_command, Run).WillRepeatedly(Return(true));
        return mock_fp_mode_command;
      });

  EXPECT_TRUE(mock_cros_fp_device_->SetFpMode(mode));
}

TEST_F(CrosFpDevice_SetFpMode, FailureCurrentModeInvalid) {
  const FpMode mode = FpMode(FpMode::Mode::kMatch);
  EXPECT_CALL(*mock_ec_command_factory_, FpModeCommand(mode))
      .WillOnce([&mode, this]() {
        auto mock_fp_mode_command =
            std::make_unique<NiceMock<MockFpModeCommand>>(mode);
        EXPECT_CALL(*mock_fp_mode_command, Run).WillRepeatedly(Return(false));
        EXPECT_CALL(*mock_cros_fp_device_, GetFpMode)
            .WillRepeatedly(Return(FpMode(FpMode::Mode::kModeInvalid)));
        return mock_fp_mode_command;
      });

  EXPECT_FALSE(mock_cros_fp_device_->SetFpMode(mode));
}

TEST_F(CrosFpDevice_SetFpMode, RunFailsButCurrentModeMatchesRequestedMode) {
  const FpMode mode = FpMode(FpMode::Mode::kMatch);
  EXPECT_CALL(*mock_ec_command_factory_, FpModeCommand(mode))
      .WillOnce([&mode, this]() {
        auto mock_fp_mode_command =
            std::make_unique<NiceMock<MockFpModeCommand>>(mode);
        EXPECT_CALL(*mock_fp_mode_command, Run).WillRepeatedly(Return(false));
        EXPECT_CALL(*mock_cros_fp_device_, GetFpMode)
            .WillRepeatedly(Return(mode));
        return mock_fp_mode_command;
      });

  EXPECT_TRUE(mock_cros_fp_device_->SetFpMode(mode));
}

TEST_F(CrosFpDevice_SetFpMode, FailureCurrentModeDifferentFromInputMode) {
  const FpMode mode1 = FpMode(FpMode::Mode::kMatch);
  const FpMode mode2 = FpMode(FpMode::Mode::kCapture);
  EXPECT_CALL(*mock_ec_command_factory_, FpModeCommand(mode1))
      .WillOnce([&mode1, this, mode2]() {
        auto mock_fp_mode_command =
            std::make_unique<NiceMock<MockFpModeCommand>>(mode1);
        EXPECT_CALL(*mock_fp_mode_command, Run).WillRepeatedly(Return(false));
        EXPECT_CALL(*mock_cros_fp_device_, GetFpMode)
            .WillRepeatedly(Return(mode2));
        return mock_fp_mode_command;
      });

  EXPECT_FALSE(mock_cros_fp_device_->SetFpMode(mode1));
}

class CrosFpDevice_GetFpMode : public testing::Test {
 public:
  CrosFpDevice_GetFpMode() {
    auto mock_command_factory = std::make_unique<ec::MockEcCommandFactory>();
    mock_ec_command_factory_ = mock_command_factory.get();
    mock_cros_fp_device_ = std::make_unique<MockCrosFpDevice>(
        &mock_biod_metrics_, std::move(mock_command_factory));
  }

 protected:
  class MockCrosFpDevice : public CrosFpDevice {
   public:
    MockCrosFpDevice(
        BiodMetricsInterface* biod_metrics,
        std::unique_ptr<EcCommandFactoryInterface> ec_command_factory)
        : CrosFpDevice(biod_metrics, std::move(ec_command_factory)) {}
  };

  class MockGetFpModeCommand : public GetFpModeCommand {
   public:
    MockGetFpModeCommand() { ON_CALL(*this, Run).WillByDefault(Return(true)); }
    MOCK_METHOD(bool, Run, (int fd), (override));
    MOCK_METHOD(const struct ec_response_fp_mode*, Resp, (), (const, override));
  };

  metrics::MockBiodMetrics mock_biod_metrics_;
  ec::MockEcCommandFactory* mock_ec_command_factory_ = nullptr;
  std::unique_ptr<MockCrosFpDevice> mock_cros_fp_device_;
};

TEST_F(CrosFpDevice_GetFpMode, Success) {
  struct ec_response_fp_mode resp = {.mode = FP_MODE_DEEPSLEEP};
  EXPECT_CALL(*mock_ec_command_factory_, GetFpModeCommand())
      .WillOnce([&resp]() {
        auto mock_get_fp_mode_command =
            std::make_unique<NiceMock<MockGetFpModeCommand>>();
        EXPECT_CALL(*mock_get_fp_mode_command, Run)
            .WillRepeatedly(Return(true));
        EXPECT_CALL(*mock_get_fp_mode_command, Resp)
            .WillRepeatedly(Return(&resp));
        return mock_get_fp_mode_command;
      });

  EXPECT_EQ(mock_cros_fp_device_->GetFpMode(),
            FpMode(FpMode::Mode::kDeepsleep));
}

TEST_F(CrosFpDevice_GetFpMode, FailureReturnsInvalidMode) {
  EXPECT_CALL(*mock_ec_command_factory_, GetFpModeCommand()).WillOnce([]() {
    auto mock_get_fp_mode_command =
        std::make_unique<NiceMock<MockGetFpModeCommand>>();
    EXPECT_CALL(*mock_get_fp_mode_command, Run).WillRepeatedly(Return(false));
    return mock_get_fp_mode_command;
  });

  EXPECT_EQ(mock_cros_fp_device_->GetFpMode(),
            FpMode(FpMode::Mode::kModeInvalid));
}

class CrosFpDevice_ResetContext : public testing::Test {
 public:
  class MockCrosFpDevice : public CrosFpDevice {
   public:
    MockCrosFpDevice(
        BiodMetricsInterface* biod_metrics,
        std::unique_ptr<EcCommandFactoryInterface> ec_command_factory)
        : CrosFpDevice(biod_metrics, std::move(ec_command_factory)) {}
    MOCK_METHOD(FpMode, GetFpMode, (), (override));
    MOCK_METHOD(bool, SetFpMode, (const FpMode&), (override));
    MOCK_METHOD(bool, SetContext, (std::string user_id), (override));
  };
  class MockFpContextFactory : public ec::MockEcCommandFactory {
   public:
    std::unique_ptr<EcCommandInterface> FpContextCommand(
        ec::EcCommandVersionSupportedInterface* ec_cmd_ver_suported,
        const std::string& user_id) override {
      auto cmd = std::make_unique<MockEcCommandInterface>();
      EXPECT_CALL(*cmd, Run(An<int>())).WillOnce(testing::Return(true));
      return cmd;
    }
  };
  metrics::MockBiodMetrics mock_biod_metrics;
  MockCrosFpDevice mock_cros_fp_device{
      &mock_biod_metrics, std::make_unique<MockFpContextFactory>()};
};

TEST_F(CrosFpDevice_ResetContext, Success) {
  EXPECT_CALL(mock_cros_fp_device, GetFpMode).Times(1).WillOnce([]() {
    return FpMode(FpMode::Mode::kNone);
  });
  EXPECT_CALL(mock_cros_fp_device, SetContext(std::string())).Times(1);
  EXPECT_CALL(mock_biod_metrics,
              SendResetContextMode(FpMode(FpMode::Mode::kNone)));

  mock_cros_fp_device.ResetContext();
}

TEST_F(CrosFpDevice_ResetContext, WrongMode) {
  EXPECT_CALL(mock_cros_fp_device, GetFpMode).Times(1).WillOnce([]() {
    return FpMode(FpMode::Mode::kMatch);
  });
  EXPECT_CALL(mock_cros_fp_device, SetFpMode(FpMode(FpMode::Mode::kNone)))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_cros_fp_device, SetContext(std::string())).Times(1);
  EXPECT_CALL(mock_biod_metrics,
              SendResetContextMode(FpMode(FpMode::Mode::kMatch)));

  mock_cros_fp_device.ResetContext();
}

TEST_F(CrosFpDevice_ResetContext, Failure) {
  EXPECT_CALL(mock_cros_fp_device, GetFpMode).Times(1).WillOnce([]() {
    return FpMode(FpMode::Mode::kModeInvalid);
  });
  EXPECT_CALL(mock_cros_fp_device, SetContext(std::string())).Times(1);
  EXPECT_CALL(mock_biod_metrics,
              SendResetContextMode(FpMode(FpMode::Mode::kModeInvalid)));

  mock_cros_fp_device.ResetContext();
}

class CrosFpDevice_SetContext : public testing::Test {
 public:
  class MockCrosFpDevice : public CrosFpDevice {
   public:
    MockCrosFpDevice(
        BiodMetricsInterface* biod_metrics,
        std::unique_ptr<EcCommandFactoryInterface> ec_command_factory)
        : CrosFpDevice(biod_metrics, std::move(ec_command_factory)) {}
    MOCK_METHOD(FpMode, GetFpMode, (), (override));
    MOCK_METHOD(bool, SetFpMode, (const FpMode& mode), (override));
  };
  class MockFpContextFactory : public ec::MockEcCommandFactory {
   public:
    std::unique_ptr<EcCommandInterface> FpContextCommand(
        ec::EcCommandVersionSupportedInterface* ec_cmd_ver_supported,
        const std::string& user_id) override {
      auto cmd = std::make_unique<MockEcCommandInterface>();
      EXPECT_CALL(*cmd, Run(An<int>())).WillOnce(testing::Return(true));
      return cmd;
    }
  };
  metrics::MockBiodMetrics mock_biod_metrics;
  MockCrosFpDevice mock_cros_fp_device{
      &mock_biod_metrics, std::make_unique<MockFpContextFactory>()};
};

// Test that if FPMCU is in match mode, setting context will trigger a call to
// set FPMCU to none mode then another call to set it back to match mode, and
// will send the original mode to UMA.
TEST_F(CrosFpDevice_SetContext, MatchMode) {
  {
    testing::InSequence s;
    EXPECT_CALL(mock_cros_fp_device, GetFpMode).WillOnce([]() {
      return FpMode(FpMode::Mode::kMatch);
    });
    EXPECT_CALL(mock_cros_fp_device, SetFpMode(FpMode(FpMode::Mode::kNone)))
        .WillOnce(Return(true));
    EXPECT_CALL(mock_biod_metrics,
                SendSetContextMode(FpMode(FpMode::Mode::kMatch)));
    EXPECT_CALL(mock_cros_fp_device, SetFpMode(FpMode(FpMode::Mode::kMatch)))
        .WillOnce(Return(true));
    EXPECT_CALL(mock_biod_metrics, SendSetContextSuccess(true));
  }

  mock_cros_fp_device.SetContext("beef");
}

// Test that failure to get FPMCU mode in setting context will cause the
// failure to be sent to UMA.
TEST_F(CrosFpDevice_SetContext, SendMetricsOnFailingToGetMode) {
  EXPECT_CALL(mock_cros_fp_device, GetFpMode)
      .WillOnce(Return(FpMode(FpMode::Mode::kModeInvalid)));
  EXPECT_CALL(mock_biod_metrics, SendSetContextSuccess(false));

  mock_cros_fp_device.SetContext("beef");
}

// Test that failure to set FPMCU mode in setting context will cause the
// failure to be sent to UMA.
TEST_F(CrosFpDevice_SetContext, SendMetricsOnFailingToSetMode) {
  EXPECT_CALL(mock_cros_fp_device, GetFpMode).WillOnce([]() {
    return FpMode(FpMode::Mode::kMatch);
  });
  EXPECT_CALL(mock_cros_fp_device, SetFpMode).WillRepeatedly(Return(false));
  EXPECT_CALL(mock_biod_metrics, SendSetContextSuccess(false));

  mock_cros_fp_device.SetContext("beef");
}

class CrosFpDevice_DeadPixelCount : public testing::Test {
 public:
  CrosFpDevice_DeadPixelCount() {
    auto mock_command_factory = std::make_unique<ec::MockEcCommandFactory>();
    mock_ec_command_factory_ = mock_command_factory.get();
    mock_cros_fp_device_ = std::make_unique<MockCrosFpDevice>(
        &mock_biod_metrics_, std::move(mock_command_factory));
  }

 protected:
  class MockCrosFpDevice : public CrosFpDevice {
   public:
    MockCrosFpDevice(
        BiodMetricsInterface* biod_metrics,
        std::unique_ptr<EcCommandFactoryInterface> ec_command_factory)
        : CrosFpDevice(biod_metrics, std::move(ec_command_factory)) {}
  };

  class MockFpInfoCommand : public ec::FpInfoCommand {
   public:
    MockFpInfoCommand() : ec::FpInfoCommand(/*version=*/2) {
      ON_CALL(*this, Run).WillByDefault(Return(true));
    }

    MOCK_METHOD(bool, Run, (int fd), (override));
    MOCK_METHOD(int, NumDeadPixels, (), (override));
  };

  metrics::MockBiodMetrics mock_biod_metrics_;
  ec::MockEcCommandFactory* mock_ec_command_factory_ = nullptr;
  std::unique_ptr<CrosFpDevice> mock_cros_fp_device_;
};

TEST_F(CrosFpDevice_DeadPixelCount, UnknownCount) {
  auto mock_fp_info_command = std::make_unique<MockFpInfoCommand>();
  EXPECT_CALL(*mock_fp_info_command, NumDeadPixels)
      .WillOnce(Return(FpInfoCommand::kDeadPixelsUnknown));
  EXPECT_CALL(*mock_ec_command_factory_,
              FpInfoCommand(mock_cros_fp_device_.get()))
      .WillOnce(Return(std::move(mock_fp_info_command)));

  EXPECT_EQ(mock_cros_fp_device_->DeadPixelCount(),
            FpInfoCommand::kDeadPixelsUnknown);
}

TEST_F(CrosFpDevice_DeadPixelCount, OneDeadPixel) {
  auto mock_fp_info_command = std::make_unique<MockFpInfoCommand>();
  EXPECT_CALL(*mock_fp_info_command, NumDeadPixels).WillOnce(Return(1));
  EXPECT_CALL(*mock_ec_command_factory_,
              FpInfoCommand(mock_cros_fp_device_.get()))
      .WillOnce(Return(std::move(mock_fp_info_command)));

  EXPECT_EQ(mock_cros_fp_device_->DeadPixelCount(), 1);
}

class CrosFpDevice_ReadVersion : public testing::Test {
 public:
  static inline const std::string kValidVersionStr =
      "1.0.0\n"
      "bloonchipper_v2.0.4277-9f652bb3\n"
      "bloonchipper_v2.0.4277-9f652bb3\n"
      "read-write\n";

  class MockCrosFpDevice : public CrosFpDevice {
   public:
    MockCrosFpDevice(
        BiodMetricsInterface* biod_metrics,
        std::unique_ptr<EcCommandFactoryInterface> ec_command_factory)
        : CrosFpDevice(biod_metrics, std::move(ec_command_factory)) {}
    MOCK_METHOD(int, read, (int, void*, size_t), (override));
    using CrosFpDevice::ReadVersion;
  };

 protected:
  metrics::MockBiodMetrics mock_biod_metrics_;
  MockCrosFpDevice mock_cros_fp_device_{
      &mock_biod_metrics_, std::make_unique<ec::MockEcCommandFactory>()};
};

// Test reading the version string where the string returned by the driver is
// not NUL terminated. The driver won't do this unless the "count" requested
// by userspace in the "read" system call does not have enough space. This test
// doesn't exactly replicate that condition exactly since we don't change
// "count".
TEST_F(CrosFpDevice_ReadVersion, ValidVersionStringNotNulTerminated) {
  EXPECT_EQ(kValidVersionStr.size(), 81);

  EXPECT_CALL(mock_cros_fp_device_, read)
      .WillOnce([](int, void* buf, size_t count) {
        // Copy string, excluding terminating NUL.
        int num_bytes_to_copy = kValidVersionStr.size();
        EXPECT_GE(count, num_bytes_to_copy);
        uint8_t* buffer = static_cast<uint8_t*>(buf);
        std::memcpy(buffer, kValidVersionStr.data(), num_bytes_to_copy);
        return num_bytes_to_copy;
      });
  std::optional<std::string> version = mock_cros_fp_device_.ReadVersion();
  EXPECT_TRUE(version.has_value());
  EXPECT_EQ(*version, std::string("1.0.0"));
}

TEST_F(CrosFpDevice_ReadVersion, ValidVersionStringNulTerminated) {
  EXPECT_EQ(kValidVersionStr.size(), 81);

  EXPECT_CALL(mock_cros_fp_device_, read)
      .WillOnce([](int, void* buf, size_t count) {
        // Copy entire string, including terminating NUL.
        int num_bytes_to_copy = kValidVersionStr.size() + 1;
        EXPECT_EQ(num_bytes_to_copy, 82);
        EXPECT_GE(count, num_bytes_to_copy);
        uint8_t* buffer = static_cast<uint8_t*>(buf);
        std::memcpy(buffer, kValidVersionStr.data(), num_bytes_to_copy);
        return num_bytes_to_copy;
      });
  std::optional<std::string> version = mock_cros_fp_device_.ReadVersion();
  EXPECT_TRUE(version.has_value());
  EXPECT_EQ(*version, std::string("1.0.0"));
}

TEST_F(CrosFpDevice_ReadVersion, InvalidVersionStringNoNewline) {
  const std::string kInvalidVersionStr = "1.0.0";

  EXPECT_CALL(mock_cros_fp_device_, read)
      .WillOnce([kInvalidVersionStr](int, void* buf, size_t count) {
        // Copy string, including terminating NUL.
        int num_bytes_to_copy = kInvalidVersionStr.size() + 1;
        EXPECT_GE(count, num_bytes_to_copy);
        uint8_t* buffer = static_cast<uint8_t*>(buf);
        std::memcpy(buffer, kInvalidVersionStr.data(), num_bytes_to_copy);
        return num_bytes_to_copy;
      });
  std::optional<std::string> version = mock_cros_fp_device_.ReadVersion();
  EXPECT_FALSE(version.has_value());
}

class CrosFpDevice_UploadTemplate : public testing::Test {
 public:
  CrosFpDevice_UploadTemplate() {
    auto mock_command_factory = std::make_unique<ec::MockEcCommandFactory>();
    mock_ec_command_factory_ = mock_command_factory.get();
    mock_cros_fp_device_ = std::make_unique<MockCrosFpDevice>(
        &mock_biod_metrics_, std::move(mock_command_factory));
  }

 protected:
  class MockCrosFpDevice : public CrosFpDevice {
   public:
    MockCrosFpDevice(
        BiodMetricsInterface* biod_metrics,
        std::unique_ptr<EcCommandFactoryInterface> ec_command_factory)
        : CrosFpDevice(biod_metrics, std::move(ec_command_factory)) {}
  };

  class MockFpTemplateCommand : public FpTemplateCommand {
   public:
    MockFpTemplateCommand(std::vector<uint8_t> tmpl,
                          uint16_t max_write_size,
                          bool commit)
        : FpTemplateCommand(tmpl, max_write_size, commit) {
      ON_CALL(*this, Run).WillByDefault(Return(true));
      ON_CALL(*this, Result).WillByDefault(Return(EC_RES_SUCCESS));
    }
    MOCK_METHOD(bool, Run, (int fd), (override));
    MOCK_METHOD(uint32_t, Result, (), (override, const));
  };

  metrics::MockBiodMetrics mock_biod_metrics_;
  ec::MockEcCommandFactory* mock_ec_command_factory_ = nullptr;
  std::unique_ptr<CrosFpDevice> mock_cros_fp_device_;
};

TEST_F(CrosFpDevice_UploadTemplate, Success) {
  std::vector<uint8_t> templ;

  EXPECT_CALL(*mock_ec_command_factory_, FpTemplateCommand)
      .WillOnce(
          [](std::vector<uint8_t> tmpl, uint16_t max_write_size, bool commit) {
            return std::make_unique<NiceMock<MockFpTemplateCommand>>(
                tmpl, max_write_size, commit);
          });

  EXPECT_CALL(mock_biod_metrics_, SendUploadTemplateResult(EC_RES_SUCCESS));
  EXPECT_TRUE(mock_cros_fp_device_->UploadTemplate(templ));
}

TEST_F(CrosFpDevice_UploadTemplate, RunFailure) {
  std::vector<uint8_t> templ;

  EXPECT_CALL(*mock_ec_command_factory_, FpTemplateCommand)
      .WillOnce(
          [](std::vector<uint8_t> tmpl, uint16_t max_write_size, bool commit) {
            auto cmd = std::make_unique<NiceMock<MockFpTemplateCommand>>(
                tmpl, max_write_size, commit);
            EXPECT_CALL(*cmd, Run).WillRepeatedly(Return(false));
            return cmd;
          });

  EXPECT_CALL(mock_biod_metrics_,
              SendUploadTemplateResult(metrics::kCmdRunFailure));
  EXPECT_FALSE(mock_cros_fp_device_->UploadTemplate(templ));
}

class CrosFpDevice_HwErrors : public testing::Test {
 public:
  CrosFpDevice_HwErrors() {
    auto mock_command_factory = std::make_unique<ec::MockEcCommandFactory>();
    mock_ec_command_factory_ = mock_command_factory.get();
    mock_cros_fp_device_ = std::make_unique<MockCrosFpDevice>(
        &mock_biod_metrics_, std::move(mock_command_factory));
  }

 protected:
  class MockCrosFpDevice : public CrosFpDevice {
   public:
    MockCrosFpDevice(
        BiodMetricsInterface* biod_metrics,
        std::unique_ptr<EcCommandFactoryInterface> ec_command_factory)
        : CrosFpDevice(biod_metrics, std::move(ec_command_factory)) {}
  };

  class MockFpInfoCommand : public ec::FpInfoCommand {
   public:
    MockFpInfoCommand() : ec::FpInfoCommand(/*version=*/2) {
      ON_CALL(*this, Run).WillByDefault(Return(true));
    }

    MOCK_METHOD(bool, Run, (int fd), (override));
    MOCK_METHOD(FpSensorErrors, GetFpSensorErrors, (), (override));
  };

  metrics::MockBiodMetrics mock_biod_metrics_;
  ec::MockEcCommandFactory* mock_ec_command_factory_ = nullptr;
  std::unique_ptr<CrosFpDevice> mock_cros_fp_device_;
};

TEST_F(CrosFpDevice_HwErrors, Errors_None) {
  auto mock_fp_info_command = std::make_unique<MockFpInfoCommand>();
  EXPECT_CALL(*mock_fp_info_command, GetFpSensorErrors)
      .WillOnce(Return(FpSensorErrors::kNone));
  EXPECT_CALL(*mock_ec_command_factory_,
              FpInfoCommand(mock_cros_fp_device_.get()))
      .WillOnce(Return(std::move(mock_fp_info_command)));

  EXPECT_EQ(mock_cros_fp_device_->GetHwErrors(), FpSensorErrors::kNone);
}

TEST_F(CrosFpDevice_HwErrors, Errors_NoIrq) {
  auto mock_fp_info_command = std::make_unique<MockFpInfoCommand>();
  EXPECT_CALL(*mock_fp_info_command, GetFpSensorErrors)
      .WillOnce(Return(FpSensorErrors::kNoIrq));
  EXPECT_CALL(*mock_ec_command_factory_,
              FpInfoCommand(mock_cros_fp_device_.get()))
      .WillOnce(Return(std::move(mock_fp_info_command)));

  EXPECT_EQ(mock_cros_fp_device_->GetHwErrors(), FpSensorErrors::kNoIrq);
}

TEST_F(CrosFpDevice_HwErrors, Errors_SpiCommunication) {
  auto mock_fp_info_command = std::make_unique<MockFpInfoCommand>();
  EXPECT_CALL(*mock_fp_info_command, GetFpSensorErrors)
      .WillOnce(Return(FpSensorErrors::kSpiCommunication));
  EXPECT_CALL(*mock_ec_command_factory_,
              FpInfoCommand(mock_cros_fp_device_.get()))
      .WillOnce(Return(std::move(mock_fp_info_command)));

  EXPECT_EQ(mock_cros_fp_device_->GetHwErrors(),
            FpSensorErrors::kSpiCommunication);
}

TEST_F(CrosFpDevice_HwErrors, Errors_BadHardwareID) {
  auto mock_fp_info_command = std::make_unique<MockFpInfoCommand>();
  EXPECT_CALL(*mock_fp_info_command, GetFpSensorErrors)
      .WillOnce(Return(FpSensorErrors::kBadHardwareID));
  EXPECT_CALL(*mock_ec_command_factory_,
              FpInfoCommand(mock_cros_fp_device_.get()))
      .WillOnce(Return(std::move(mock_fp_info_command)));

  EXPECT_EQ(mock_cros_fp_device_->GetHwErrors(),
            FpSensorErrors::kBadHardwareID);
}

TEST_F(CrosFpDevice_HwErrors, Errors_InitializationFailure) {
  auto mock_fp_info_command = std::make_unique<MockFpInfoCommand>();
  EXPECT_CALL(*mock_fp_info_command, GetFpSensorErrors)
      .WillOnce(Return(FpSensorErrors::kInitializationFailure));
  EXPECT_CALL(*mock_ec_command_factory_,
              FpInfoCommand(mock_cros_fp_device_.get()))
      .WillOnce(Return(std::move(mock_fp_info_command)));

  EXPECT_EQ(mock_cros_fp_device_->GetHwErrors(),
            FpSensorErrors::kInitializationFailure);
}

TEST_F(CrosFpDevice_HwErrors, Errors_InitializationFailureOrBadHardwareID) {
  auto mock_fp_info_command = std::make_unique<MockFpInfoCommand>();
  EXPECT_CALL(*mock_fp_info_command, GetFpSensorErrors)
      .WillOnce(Return(FpSensorErrors::kInitializationFailure |
                       FpSensorErrors::kBadHardwareID));
  EXPECT_CALL(*mock_ec_command_factory_,
              FpInfoCommand(mock_cros_fp_device_.get()))
      .WillOnce(Return(std::move(mock_fp_info_command)));

  EXPECT_EQ(
      mock_cros_fp_device_->GetHwErrors(),
      FpSensorErrors::kInitializationFailure | FpSensorErrors::kBadHardwareID);
}

}  // namespace
}  // namespace biod
