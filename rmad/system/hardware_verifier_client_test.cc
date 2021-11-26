// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/fake_hardware_verifier_client.h"
#include "rmad/system/hardware_verifier_client_impl.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/hardware_verifier/dbus-constants.h>
#include <gmock/gmock.h>
#include <google/protobuf/text_format.h>
#include <gtest/gtest.h>
#include <hardware_verifier/hardware_verifier.pb.h>
#include <rmad/proto_bindings/rmad.pb.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "rmad/constants.h"

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace {

const char kVerifyComponentsReplyCompliant[] = R"(
  error: ERROR_OK
  hw_verification_report: {
    is_compliant: true
    found_component_infos: [
      {
        component_category: audio_codec
        qualification_status: QUALIFIED
        component_fields: {
          audio_codec: {
            name: "Audio1"
          }
        }
      }
    ]
  }
)";

const char kVerifyComponentsReplyNotCompliant[] = R"(
  error: ERROR_OK
  hw_verification_report: {
    is_compliant: false
    found_component_infos: [
      {
        component_category: audio_codec
        qualification_status: QUALIFIED
        component_fields: {
          audio_codec: {
            name: "Audio1"
          }
        }
      },
      {
        component_category: battery
        qualification_status: UNQUALIFIED
        component_fields: {
          battery: {
            manufacturer: "ABC"
            model_name: "abc"
          }
        }
      }
    ]
  }
)";

const char kVerifyComponentsReplyError[] = R"(
  error: ERROR_OTHER_ERROR
)";

const char kVerifyComponentsReplyAllComponents[] = R"(
  error: ERROR_OK
  hw_verification_report: {
    is_compliant: false
    found_component_infos: [
      {
        component_category: audio_codec
        qualification_status: UNQUALIFIED
        component_fields: {
          audio_codec: {
            name: "AudioName"
          }
        }
      },
      {
        component_category: battery
        qualification_status: UNQUALIFIED
        component_fields: {
          battery: {
            manufacturer: "BatteryManufacturer"
            model_name: "BatteryModelName"
          }
        }
      },
      {
        component_category: storage
        qualification_status: UNQUALIFIED
        component_fields: {
          storage: {
            type: "MMC",
            mmc_manfid: 10
            mmc_name: "MmcName"
          }
        }
      },
      {
        component_category: storage
        qualification_status: UNQUALIFIED
        component_fields: {
          storage: {
            type: "NVMe",
            pci_vendor: 10
            pci_device: 11
          }
        }
      },
      {
        component_category: storage
        qualification_status: UNQUALIFIED
        component_fields: {
          storage: {
            type: "ATA",
            ata_vendor: "AtaVendor"
            ata_model: "AtaModel"
          }
        }
      },
      {
        component_category: storage
        qualification_status: UNQUALIFIED
        component_fields: {
          storage: {
            type: "*(&]",
          }
        }
      },
      {
        component_category: camera
        qualification_status: UNQUALIFIED
        component_fields: {
          camera: {
            usb_vendor_id: 10
            usb_product_id: 11
          }
        }
      },
      {
        component_category: stylus
        qualification_status: UNQUALIFIED
        component_fields: {
          stylus: {
            vendor: 10
            product: 11
          }
        }
      },
      {
        component_category: touchpad
        qualification_status: UNQUALIFIED
        component_fields: {
          touchpad: {
            vendor: 10
            product: 11
          }
        }
      },
      {
        component_category: touchscreen
        qualification_status: UNQUALIFIED
        component_fields: {
          touchscreen: {
            vendor: 10
            product: 11
          }
        }
      },
      {
        component_category: dram
        qualification_status: UNQUALIFIED
        component_fields: {
          dram: {
            part: "DramPart"
          }
        }
      },
      {
        component_category: display_panel
        qualification_status: UNQUALIFIED
        component_fields: {
          display_panel: {
            vendor: "DisplayVendor"
            product_id: 10
          }
        }
      },
      {
        component_category: cellular
        qualification_status: UNQUALIFIED
        component_fields: {
          cellular: {
            type: "cellular"
            bus_type: "pci"
            pci_vendor_id: 10
            pci_device_id: 11
          }
        }
      },
      {
        component_category: ethernet
        qualification_status: UNQUALIFIED
        component_fields: {
          cellular: {
            type: "ethernet"
            bus_type: "usb"
            usb_vendor_id: 10
            usb_product_id: 11
          }
        }
      },
      {
        component_category: wireless
        qualification_status: UNQUALIFIED
        component_fields: {
          cellular: {
            type: "wireless"
            bus_type: "sdio"
            sdio_vendor_id: 10
            sdio_device_id: 11
          }
        }
      },
      {
        component_category: wireless
        qualification_status: UNQUALIFIED
        component_fields: {
          cellular: {
            type: "wireless"
            bus_type: "*(&]"
            sdio_vendor_id: 10
            sdio_device_id: 11
          }
        }
      },
      {
        component_category: network
        qualification_status: UNQUALIFIED
      }
    ]
  }
)";

const char kVerifyComponentsErrorStrAllComponents[] =
    "Audio_AudioName\n"
    "Battery_BatteryManufacturer_BatteryModelName\n"
    "Storage(eMMC)_0a_MmcName\n"
    "Storage(NVMe)_000a_000b\n"
    "Storage(SATA)_AtaVendor_AtaModel\n"
    "Storage(unknown)\n"
    "Camera_000a_000b\n"
    "Stylus_000a_000b\n"
    "Touchpad_000a_000b\n"
    "Touchscreen_000a_000b\n"
    "Memory_DramPart\n"
    "Display_DisplayVendor_000a\n"
    "Network(cellular:pci)_000a_000b\n"
    "Network(ethernet:usb)_000a_000b\n"
    "Network(wireless:sdio)_000a_000b\n"
    "Network(wireless:unknown)\n"
    "UnknownComponent\n";

}  // namespace

namespace rmad {

class HardwareVerifierClientTest : public testing::Test {
 public:
  HardwareVerifierClientTest()
      : mock_bus_(new StrictMock<dbus::MockBus>(dbus::Bus::Options())),
        mock_object_proxy_(new StrictMock<dbus::MockObjectProxy>(
            mock_bus_.get(),
            hardware_verifier::kHardwareVerifierServiceName,
            dbus::ObjectPath(
                hardware_verifier::kHardwareVerifierServicePath))) {}
  ~HardwareVerifierClientTest() override = default;

  void SetUp() override {
    EXPECT_CALL(
        *mock_bus_,
        GetObjectProxy(
            hardware_verifier::kHardwareVerifierServiceName,
            dbus::ObjectPath(hardware_verifier::kHardwareVerifierServicePath)))
        .WillOnce(Return(mock_object_proxy_.get()));
    hardware_verifier_client_ =
        std::make_unique<HardwareVerifierClientImpl>(mock_bus_);
  }

 protected:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_object_proxy_;
  std::unique_ptr<HardwareVerifierClientImpl> hardware_verifier_client_;
};

TEST_F(HardwareVerifierClientTest, GetHardwareVerificationResult_Compliant) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> hardware_verifier_response =
            dbus::Response::CreateEmpty();
        hardware_verifier::VerifyComponentsReply reply;
        CHECK(google::protobuf::TextFormat::ParseFromString(
            kVerifyComponentsReplyCompliant, &reply));
        dbus::MessageWriter writer(hardware_verifier_response.get());
        writer.AppendProtoAsArrayOfBytes(reply);
        return hardware_verifier_response;
      });

  HardwareVerificationResult result;
  EXPECT_TRUE(
      hardware_verifier_client_->GetHardwareVerificationResult(&result));
  EXPECT_EQ(result.is_compliant(), true);
  EXPECT_EQ(result.error_str(), "");
}

TEST_F(HardwareVerifierClientTest, GetHardwareVerificationResult_NotCompliant) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> hardware_verifier_response =
            dbus::Response::CreateEmpty();
        hardware_verifier::VerifyComponentsReply reply;
        CHECK(google::protobuf::TextFormat::ParseFromString(
            kVerifyComponentsReplyNotCompliant, &reply));
        dbus::MessageWriter writer(hardware_verifier_response.get());
        writer.AppendProtoAsArrayOfBytes(reply);
        return hardware_verifier_response;
      });

  HardwareVerificationResult result;
  EXPECT_TRUE(
      hardware_verifier_client_->GetHardwareVerificationResult(&result));
  EXPECT_EQ(result.is_compliant(), false);
  EXPECT_EQ(result.error_str(), "Battery_ABC_abc\n");
}

TEST_F(HardwareVerifierClientTest,
       GetHardwareVerificationResult_AllComponents) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> hardware_verifier_response =
            dbus::Response::CreateEmpty();
        hardware_verifier::VerifyComponentsReply reply;
        CHECK(google::protobuf::TextFormat::ParseFromString(
            kVerifyComponentsReplyAllComponents, &reply));
        dbus::MessageWriter writer(hardware_verifier_response.get());
        writer.AppendProtoAsArrayOfBytes(reply);
        return hardware_verifier_response;
      });

  HardwareVerificationResult result;
  EXPECT_TRUE(
      hardware_verifier_client_->GetHardwareVerificationResult(&result));
  EXPECT_EQ(result.is_compliant(), false);
  EXPECT_EQ(result.error_str(), kVerifyComponentsErrorStrAllComponents);
}

TEST_F(HardwareVerifierClientTest, GetHardwareVerificationResult_NoResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) { return nullptr; });

  HardwareVerificationResult result;
  EXPECT_FALSE(
      hardware_verifier_client_->GetHardwareVerificationResult(&result));
}

TEST_F(HardwareVerifierClientTest,
       GetHardwareVerificationResult_EmptyResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce(
          [](dbus::MethodCall*, int) { return dbus::Response::CreateEmpty(); });

  HardwareVerificationResult result;
  EXPECT_FALSE(
      hardware_verifier_client_->GetHardwareVerificationResult(&result));
}

TEST_F(HardwareVerifierClientTest,
       GetHardwareVerificationResult_ErrorResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> hardware_verifier_response =
            dbus::Response::CreateEmpty();
        hardware_verifier::VerifyComponentsReply reply;
        CHECK(google::protobuf::TextFormat::ParseFromString(
            kVerifyComponentsReplyError, &reply));
        dbus::MessageWriter writer(hardware_verifier_response.get());
        writer.AppendProtoAsArrayOfBytes(reply);
        return hardware_verifier_response;
      });

  HardwareVerificationResult result;
  EXPECT_FALSE(
      hardware_verifier_client_->GetHardwareVerificationResult(&result));
}

namespace fake {

// Tests for |FakeHardwareVerifierClient|.
class FakeHardwareVerifierClientTest : public testing::Test {
 public:
  FakeHardwareVerifierClientTest() = default;
  ~FakeHardwareVerifierClientTest() override = default;

  bool WriteHardwareVerificationResult(const std::string& str) {
    base::FilePath hw_verification_result_file_path =
        temp_dir_.GetPath().AppendASCII(kHwVerificationResultFilePath);
    return base::WriteFile(hw_verification_result_file_path, str);
  }

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_hardware_verifier_client_ =
        std::make_unique<FakeHardwareVerifierClient>(temp_dir_.GetPath());
  }

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<FakeHardwareVerifierClient> fake_hardware_verifier_client_;
};

TEST_F(FakeHardwareVerifierClientTest, GetHardwareVerificationResult_Pass) {
  WriteHardwareVerificationResult("1");
  HardwareVerificationResult result;
  EXPECT_TRUE(
      fake_hardware_verifier_client_->GetHardwareVerificationResult(&result));
  EXPECT_TRUE(result.is_compliant());
  EXPECT_EQ(result.error_str(), "hardware_verification_pass");
}

TEST_F(FakeHardwareVerifierClientTest, GetHardwareVerificationResult_Fail) {
  WriteHardwareVerificationResult("0");
  HardwareVerificationResult result;
  EXPECT_TRUE(
      fake_hardware_verifier_client_->GetHardwareVerificationResult(&result));
  EXPECT_FALSE(result.is_compliant());
  EXPECT_EQ(result.error_str(), "hardware_verification_fail");
}

TEST_F(FakeHardwareVerifierClientTest, GetHardwareVerificationResult_NoFile) {
  HardwareVerificationResult result;
  EXPECT_FALSE(
      fake_hardware_verifier_client_->GetHardwareVerificationResult(&result));
}

TEST_F(FakeHardwareVerifierClientTest, GetHardwareVerificationResult_Invalid) {
  WriteHardwareVerificationResult("");
  HardwareVerificationResult result;
  EXPECT_FALSE(
      fake_hardware_verifier_client_->GetHardwareVerificationResult(&result));
}

}  // namespace fake

}  // namespace rmad
