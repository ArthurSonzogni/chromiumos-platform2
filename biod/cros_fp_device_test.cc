// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/ec_usb_endpoint.h>
#include <libec/fingerprint/fp_info_command.h>
#include <libec/fingerprint/fp_template_command.h>
#include <libec/mock_ec_command_factory.h>

#include "biod/cros_fp_device.h"
#include "biod/mock_biod_metrics.h"
#include "biod/mock_cros_fp_device.h"
#include "libec/fingerprint/fp_sensor_errors.h"

using ec::EcCommandFactoryInterface;
using ec::EcCommandInterface;
using ec::FpGetNonceCommand;
using ec::FpInfoCommand;
using ec::FpMode;
using ec::FpReadMatchSecretWithPubkeyCommand;
using ec::FpSensorErrors;
using ec::FpSetNonceContextCommand;
using ec::FpTemplateCommand;
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

class CrosFpDevice_ResetContext : public testing::Test {
 public:
  class MockCrosFpDevice : public CrosFpDevice {
   public:
    MockCrosFpDevice(
        BiodMetricsInterface* biod_metrics,
        std::unique_ptr<EcCommandFactoryInterface> ec_command_factory)
        : CrosFpDevice(biod_metrics, std::move(ec_command_factory)) {}
    MOCK_METHOD(FpMode, GetFpMode, (), (override));
    MOCK_METHOD(bool, SetContext, (std::string user_id), (override));
  };
  class MockFpContextFactory : public ec::MockEcCommandFactory {
   public:
    std::unique_ptr<EcCommandInterface> FpContextCommand(
        ec::CrosFpDeviceInterface* cros_fp,
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
        ec::CrosFpDeviceInterface* cros_fp,
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

  class MockFpInfoCommand : public FpInfoCommand {
   public:
    MockFpInfoCommand() { ON_CALL(*this, Run).WillByDefault(Return(true)); }
    MOCK_METHOD(bool, Run, (int fd), (override));
    MOCK_METHOD(ec_response_fp_info*, Resp, (), (override));
  };

  metrics::MockBiodMetrics mock_biod_metrics_;
  ec::MockEcCommandFactory* mock_ec_command_factory_ = nullptr;
  std::unique_ptr<CrosFpDevice> mock_cros_fp_device_;
};

TEST_F(CrosFpDevice_DeadPixelCount, UnknownCount) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_DEAD_PIXELS_UNKNOWN};
  EXPECT_CALL(*mock_ec_command_factory_, FpInfoCommand).WillOnce([&resp]() {
    auto mock_fp_info_command = std::make_unique<NiceMock<MockFpInfoCommand>>();
    EXPECT_CALL(*mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));
    return mock_fp_info_command;
  });

  EXPECT_EQ(mock_cros_fp_device_->DeadPixelCount(),
            FpInfoCommand::kDeadPixelsUnknown);
}

TEST_F(CrosFpDevice_DeadPixelCount, OneDeadPixel) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_DEAD_PIXELS(1)};
  EXPECT_CALL(*mock_ec_command_factory_, FpInfoCommand).WillOnce([&resp]() {
    auto mock_fp_info_command = std::make_unique<NiceMock<MockFpInfoCommand>>();
    EXPECT_CALL(*mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));
    return mock_fp_info_command;
  });

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
    MockFpTemplateCommand(std::vector<uint8_t> tmpl, uint16_t max_write_size)
        : FpTemplateCommand(tmpl, max_write_size) {
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
      .WillOnce([](std::vector<uint8_t> tmpl, uint16_t max_write_size) {
        return std::make_unique<NiceMock<MockFpTemplateCommand>>(
            tmpl, max_write_size);
      });

  EXPECT_CALL(mock_biod_metrics_, SendUploadTemplateResult(EC_RES_SUCCESS));
  EXPECT_TRUE(mock_cros_fp_device_->UploadTemplate(templ));
}

TEST_F(CrosFpDevice_UploadTemplate, RunFailure) {
  std::vector<uint8_t> templ;

  EXPECT_CALL(*mock_ec_command_factory_, FpTemplateCommand)
      .WillOnce([](std::vector<uint8_t> tmpl, uint16_t max_write_size) {
        auto cmd = std::make_unique<NiceMock<MockFpTemplateCommand>>(
            tmpl, max_write_size);
        EXPECT_CALL(*cmd, Run).WillRepeatedly(Return(false));
        return cmd;
      });

  EXPECT_CALL(mock_biod_metrics_,
              SendUploadTemplateResult(metrics::kCmdRunFailure));
  EXPECT_FALSE(mock_cros_fp_device_->UploadTemplate(templ));
}

TEST_F(CrosFpDevice_UploadTemplate, CommandFailure) {
  std::vector<uint8_t> templ;

  EXPECT_CALL(*mock_ec_command_factory_, FpTemplateCommand)
      .WillOnce([](std::vector<uint8_t> tmpl, uint16_t max_write_size) {
        auto cmd = std::make_unique<NiceMock<MockFpTemplateCommand>>(
            tmpl, max_write_size);
        EXPECT_CALL(*cmd, Result).WillRepeatedly(Return(EC_RES_ERROR));
        return cmd;
      });

  EXPECT_CALL(mock_biod_metrics_, SendUploadTemplateResult(EC_RES_ERROR));
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

  class MockFpInfoCommand : public FpInfoCommand {
   public:
    MockFpInfoCommand() { ON_CALL(*this, Run).WillByDefault(Return(true)); }
    MOCK_METHOD(bool, Run, (int fd), (override));
    MOCK_METHOD(ec_response_fp_info*, Resp, (), (override));
  };

  metrics::MockBiodMetrics mock_biod_metrics_;
  ec::MockEcCommandFactory* mock_ec_command_factory_ = nullptr;
  std::unique_ptr<CrosFpDevice> mock_cros_fp_device_;
};

TEST_F(CrosFpDevice_HwErrors, Errors_None) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_DEAD_PIXELS_UNKNOWN};
  EXPECT_CALL(*mock_ec_command_factory_, FpInfoCommand).WillOnce([&resp]() {
    auto mock_fp_info_command = std::make_unique<NiceMock<MockFpInfoCommand>>();
    EXPECT_CALL(*mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));
    return mock_fp_info_command;
  });

  EXPECT_EQ(mock_cros_fp_device_->GetHwErrors(), FpSensorErrors::kNone);
}

TEST_F(CrosFpDevice_HwErrors, Errors_NoIrq) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_NO_IRQ |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN};
  EXPECT_CALL(*mock_ec_command_factory_, FpInfoCommand).WillOnce([&resp]() {
    auto mock_fp_info_command = std::make_unique<NiceMock<MockFpInfoCommand>>();
    EXPECT_CALL(*mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));
    return mock_fp_info_command;
  });

  EXPECT_EQ(mock_cros_fp_device_->GetHwErrors(), FpSensorErrors::kNoIrq);
}

TEST_F(CrosFpDevice_HwErrors, Errors_SpiCommunication) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_SPI_COMM |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN};
  EXPECT_CALL(*mock_ec_command_factory_, FpInfoCommand).WillOnce([&resp]() {
    auto mock_fp_info_command = std::make_unique<NiceMock<MockFpInfoCommand>>();
    EXPECT_CALL(*mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));
    return mock_fp_info_command;
  });

  EXPECT_EQ(mock_cros_fp_device_->GetHwErrors(),
            FpSensorErrors::kSpiCommunication);
}

TEST_F(CrosFpDevice_HwErrors, Errors_BadHardwareID) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_BAD_HWID |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN};
  EXPECT_CALL(*mock_ec_command_factory_, FpInfoCommand).WillOnce([&resp]() {
    auto mock_fp_info_command = std::make_unique<NiceMock<MockFpInfoCommand>>();
    EXPECT_CALL(*mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));
    return mock_fp_info_command;
  });

  EXPECT_EQ(mock_cros_fp_device_->GetHwErrors(),
            FpSensorErrors::kBadHardwareID);
}

TEST_F(CrosFpDevice_HwErrors, Errors_InitializationFailure) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_INIT_FAIL |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN};
  EXPECT_CALL(*mock_ec_command_factory_, FpInfoCommand).WillOnce([&resp]() {
    auto mock_fp_info_command = std::make_unique<NiceMock<MockFpInfoCommand>>();
    EXPECT_CALL(*mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));
    return mock_fp_info_command;
  });

  EXPECT_EQ(mock_cros_fp_device_->GetHwErrors(),
            FpSensorErrors::kInitializationFailure);
}

TEST_F(CrosFpDevice_HwErrors, Errors_InitializationFailureOrBadHardwareID) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_INIT_FAIL |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN |
                                               FP_ERROR_BAD_HWID};
  EXPECT_CALL(*mock_ec_command_factory_, FpInfoCommand).WillOnce([&resp]() {
    auto mock_fp_info_command = std::make_unique<NiceMock<MockFpInfoCommand>>();
    EXPECT_CALL(*mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));
    return mock_fp_info_command;
  });

  EXPECT_EQ(
      mock_cros_fp_device_->GetHwErrors(),
      FpSensorErrors::kInitializationFailure | FpSensorErrors::kBadHardwareID);
}

class CrosFpDevice_GetNonce : public testing::Test {
 public:
  CrosFpDevice_GetNonce() {
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

  class MockFpGetNonceCommand : public FpGetNonceCommand {
   public:
    MockFpGetNonceCommand() { ON_CALL(*this, Run).WillByDefault(Return(true)); }
    MOCK_METHOD(bool, Run, (int fd), (override));
    MOCK_METHOD(brillo::Blob, Nonce, (), (override, const));
  };

  metrics::MockBiodMetrics mock_biod_metrics_;
  ec::MockEcCommandFactory* mock_ec_command_factory_ = nullptr;
  std::unique_ptr<CrosFpDevice> mock_cros_fp_device_;
};

TEST_F(CrosFpDevice_GetNonce, Success) {
  const brillo::Blob kNonce(32, 1);

  EXPECT_CALL(*mock_ec_command_factory_, FpGetNonceCommand).WillOnce([&]() {
    auto cmd = std::make_unique<NiceMock<MockFpGetNonceCommand>>();
    EXPECT_CALL(*cmd, Nonce).WillRepeatedly(Return(kNonce));
    return cmd;
  });

  std::optional<brillo::Blob> nonce = mock_cros_fp_device_->GetNonce();
  ASSERT_TRUE(nonce.has_value());
  EXPECT_EQ(nonce, kNonce);
}

TEST_F(CrosFpDevice_GetNonce, RunFailure) {
  EXPECT_CALL(*mock_ec_command_factory_, FpGetNonceCommand).WillOnce([]() {
    auto cmd = std::make_unique<NiceMock<MockFpGetNonceCommand>>();
    EXPECT_CALL(*cmd, Run).WillRepeatedly(Return(false));
    return cmd;
  });

  std::optional<brillo::Blob> nonce = mock_cros_fp_device_->GetNonce();
  EXPECT_FALSE(nonce.has_value());
}

class CrosFpDevice_SetNonceContext : public testing::Test {
 public:
  CrosFpDevice_SetNonceContext() {
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

  class MockFpSetNonceContextCommand : public FpSetNonceContextCommand {
   public:
    using FpSetNonceContextCommand::FpSetNonceContextCommand;
    MOCK_METHOD(bool, Run, (int fd), (override));
  };

  metrics::MockBiodMetrics mock_biod_metrics_;
  ec::MockEcCommandFactory* mock_ec_command_factory_ = nullptr;
  std::unique_ptr<CrosFpDevice> mock_cros_fp_device_;
};

TEST_F(CrosFpDevice_SetNonceContext, Success) {
  const brillo::Blob kNonce(32, 1);
  const brillo::Blob kUserId(32, 2);
  const brillo::Blob kIv(16, 3);

  EXPECT_CALL(*mock_ec_command_factory_, FpSetNonceContextCommand)
      .WillOnce([](const brillo::Blob& nonce,
                   const brillo::Blob& encrypted_user_id,
                   const brillo::Blob& iv)
                    -> std::unique_ptr<FpSetNonceContextCommand> {
        auto cmd =
            FpSetNonceContextCommand::Create<MockFpSetNonceContextCommand>(
                nonce, encrypted_user_id, iv);
        if (cmd) {
          EXPECT_CALL(*cmd, Run).WillRepeatedly(Return(true));
        }
        return cmd;
      });

  EXPECT_TRUE(mock_cros_fp_device_->SetNonceContext(kNonce, kUserId, kIv));
}

TEST_F(CrosFpDevice_SetNonceContext, InvalidParams) {
  // Incorrect size.
  const brillo::Blob kNonce(33, 1);
  const brillo::Blob kUserId(32, 2);
  const brillo::Blob kIv(16, 3);

  EXPECT_CALL(*mock_ec_command_factory_, FpSetNonceContextCommand)
      .WillOnce([](const brillo::Blob& nonce,
                   const brillo::Blob& encrypted_user_id,
                   const brillo::Blob& iv)
                    -> std::unique_ptr<FpSetNonceContextCommand> {
        auto cmd =
            FpSetNonceContextCommand::Create<MockFpSetNonceContextCommand>(
                nonce, encrypted_user_id, iv);
        EXPECT_EQ(cmd, nullptr);
        return cmd;
      });

  EXPECT_FALSE(mock_cros_fp_device_->SetNonceContext(kNonce, kUserId, kIv));
}

TEST_F(CrosFpDevice_SetNonceContext, RunFailure) {
  const brillo::Blob kNonce(32, 1);
  const brillo::Blob kUserId(32, 2);
  const brillo::Blob kIv(16, 3);

  EXPECT_CALL(*mock_ec_command_factory_, FpSetNonceContextCommand)
      .WillOnce([](const brillo::Blob& nonce,
                   const brillo::Blob& encrypted_user_id,
                   const brillo::Blob& iv)
                    -> std::unique_ptr<FpSetNonceContextCommand> {
        auto cmd =
            FpSetNonceContextCommand::Create<MockFpSetNonceContextCommand>(
                nonce, encrypted_user_id, iv);
        if (cmd) {
          EXPECT_CALL(*cmd, Run).WillRepeatedly(Return(false));
        }
        return cmd;
      });

  EXPECT_FALSE(mock_cros_fp_device_->SetNonceContext(kNonce, kUserId, kIv));
}

class CrosFpDevice_GetPositiveMatchSecretWithPubkey : public testing::Test {
 public:
  CrosFpDevice_GetPositiveMatchSecretWithPubkey() {
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

  class MockFpReadMatchSecretWithPubkeyCommand
      : public FpReadMatchSecretWithPubkeyCommand {
   public:
    using FpReadMatchSecretWithPubkeyCommand::
        FpReadMatchSecretWithPubkeyCommand;
    MOCK_METHOD(bool, Run, (int fd), (override));
    MOCK_METHOD(brillo::Blob, EncryptedSecret, (), (override, const));
    MOCK_METHOD(brillo::Blob, Iv, (), (override, const));
    MOCK_METHOD(brillo::Blob, PkOutX, (), (override, const));
    MOCK_METHOD(brillo::Blob, PkOutY, (), (override, const));
  };

  metrics::MockBiodMetrics mock_biod_metrics_;
  ec::MockEcCommandFactory* mock_ec_command_factory_ = nullptr;
  std::unique_ptr<CrosFpDevice> mock_cros_fp_device_;
};

TEST_F(CrosFpDevice_GetPositiveMatchSecretWithPubkey, Success) {
  const uint16_t kIndex = 0;
  const brillo::Blob kPkInX(32, 1);
  const brillo::Blob kPkInY(32, 2);
  const brillo::Blob kEncryptedSecret(32, 3);
  const brillo::Blob kIv(16, 4);
  const brillo::Blob kPkOutX(32, 5);
  const brillo::Blob kPkOutY(32, 6);

  EXPECT_CALL(*mock_ec_command_factory_, FpReadMatchSecretWithPubkeyCommand)
      .WillOnce([&](uint16_t index, const brillo::Blob& pk_in_x,
                    const brillo::Blob& pk_in_y)
                    -> std::unique_ptr<FpReadMatchSecretWithPubkeyCommand> {
        auto cmd = FpReadMatchSecretWithPubkeyCommand::Create<
            MockFpReadMatchSecretWithPubkeyCommand>(index, pk_in_x, pk_in_y);
        if (cmd) {
          EXPECT_CALL(*cmd, Run).WillRepeatedly(Return(true));
          EXPECT_CALL(*cmd, EncryptedSecret)
              .WillRepeatedly(Return(kEncryptedSecret));
          EXPECT_CALL(*cmd, Iv).WillRepeatedly(Return(kIv));
          EXPECT_CALL(*cmd, PkOutX).WillRepeatedly(Return(kPkOutX));
          EXPECT_CALL(*cmd, PkOutY).WillRepeatedly(Return(kPkOutY));
        }
        return cmd;
      });

  std::optional<ec::CrosFpDeviceInterface::GetSecretReply> reply =
      mock_cros_fp_device_->GetPositiveMatchSecretWithPubkey(kIndex, kPkInX,
                                                             kPkInY);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->encrypted_secret, kEncryptedSecret);
  EXPECT_EQ(reply->iv, kIv);
  EXPECT_EQ(reply->pk_out_x, kPkOutX);
  EXPECT_EQ(reply->pk_out_y, kPkOutY);
}

TEST_F(CrosFpDevice_GetPositiveMatchSecretWithPubkey, InvalidParams) {
  const uint16_t kIndex = 0;
  // Incorrect size.
  const brillo::Blob kPkInX(33, 1);
  const brillo::Blob kPkInY(32, 2);

  EXPECT_CALL(*mock_ec_command_factory_, FpReadMatchSecretWithPubkeyCommand)
      .WillOnce([&](uint16_t index, const brillo::Blob& pk_in_x,
                    const brillo::Blob& pk_in_y)
                    -> std::unique_ptr<FpReadMatchSecretWithPubkeyCommand> {
        auto cmd = FpReadMatchSecretWithPubkeyCommand::Create<
            MockFpReadMatchSecretWithPubkeyCommand>(index, pk_in_x, pk_in_y);
        EXPECT_EQ(cmd, nullptr);
        return cmd;
      });

  std::optional<ec::CrosFpDeviceInterface::GetSecretReply> reply =
      mock_cros_fp_device_->GetPositiveMatchSecretWithPubkey(kIndex, kPkInX,
                                                             kPkInY);
  EXPECT_FALSE(reply.has_value());
}

TEST_F(CrosFpDevice_GetPositiveMatchSecretWithPubkey, RunFailure) {
  const uint16_t kIndex = 0;
  const brillo::Blob kPkInX(32, 1);
  const brillo::Blob kPkInY(32, 2);

  EXPECT_CALL(*mock_ec_command_factory_, FpReadMatchSecretWithPubkeyCommand)
      .WillOnce([&](uint16_t index, const brillo::Blob& pk_in_x,
                    const brillo::Blob& pk_in_y)
                    -> std::unique_ptr<FpReadMatchSecretWithPubkeyCommand> {
        auto cmd = FpReadMatchSecretWithPubkeyCommand::Create<
            MockFpReadMatchSecretWithPubkeyCommand>(index, pk_in_x, pk_in_y);
        if (cmd) {
          EXPECT_CALL(*cmd, Run).WillRepeatedly(Return(false));
        }
        return cmd;
      });

  std::optional<ec::CrosFpDeviceInterface::GetSecretReply> reply =
      mock_cros_fp_device_->GetPositiveMatchSecretWithPubkey(kIndex, kPkInX,
                                                             kPkInY);
  EXPECT_FALSE(reply.has_value());
}

}  // namespace
}  // namespace biod
