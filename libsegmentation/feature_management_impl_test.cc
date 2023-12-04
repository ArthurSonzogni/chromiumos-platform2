// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <base/hash/hash.h>
#include <base/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <libcrossystem/crossystem_fake.h>

#include "libsegmentation/device_info.pb.h"
#include "libsegmentation/feature_management.h"
#include "libsegmentation/feature_management_impl.h"
#include "libsegmentation/feature_management_interface.h"
#include "libsegmentation/feature_management_util.h"

#include "proto/feature_management.pb.h"

namespace segmentation {

using chromiumos::feature_management::api::software::Feature;
using libsegmentation::DeviceInfo_ScopeLevel;

using ::testing::Return;

// Use made up feature file:
const char test_feature_proto[] =
    "CiQKBUJhc2ljEhQKEmd3ZW5kYWxAZ29vZ2xlLmNvbSICAQIqAQEKHAoBRRIPCg1nZ0Bnb29nbG"
    "UuY29tGAIiAQIqAQIKHgoBRBIPCg1nZ0Bnb29nbGUuY29tGAIiAQIqAwECAwodCgFDEg8KDWdn"
    "QGdvb2dsZS5jb20YASIBAioCAgEKHAoBQhIPCg1nZ0Bnb29nbGUuY29tGAEiAQIqAQIKGwoBQR"
    "IPCg1nZ0Bnb29nbGUuY29tIgIBAioBAw==";

/*
  It produces the following bundle.
  Command line:
     echo "..." | base64 -d |
     protoc -I "src/platform/feature-management/proto" \
         --decode=chromiumos.feature_management.api.software.FeatureBundle \
         feature_management.proto
features {
  name: "Basic"
  contacts {
    email: "gwendal@google.com"
  }
  scopes: SCOPE_DEVICES_0
  scopes: SCOPE_DEVICES_1
  usages: USAGE_LOCAL
}
features {
  name: "E"
  contacts {
    email: "gg@google.com"
  }
  feature_level: 2
  scopes: SCOPE_DEVICES_1
  usages: USAGE_CHROME
}
features {
  name: "D"
  contacts {
    email: "gg@google.com"
  }
  feature_level: 2
  scopes: SCOPE_DEVICES_1
  usages: USAGE_LOCAL
  usages: USAGE_CHROME
  usages: USAGE_ANDROID
}
features {
  name: "C"
  contacts {
    email: "gg@google.com"
  }
  feature_level: 1
  scopes: SCOPE_DEVICES_1
  usages: USAGE_CHROME
  usages: USAGE_LOCAL
}
features {
  name: "B"
  contacts {
    email: "gg@google.com"
  }
  feature_level: 1
  scopes: SCOPE_DEVICES_1
  usages: USAGE_CHROME
}
features {
  name: "A"
  contacts {
    email: "gg@google.com"
  }
  scopes: SCOPE_DEVICES_0
  scopes: SCOPE_DEVICES_1
  usages: USAGE_ANDROID
}
*/

constexpr char kOsVersion[] = "1234.0.0";
const uint32_t kCurrentVersionHash = base::PersistentHash(kOsVersion);
const char* test_device_proto =
    "Cl8IARABGjAKCm1hcmFzb3YtQUESFAoDAwQHEgMwMDASAzAwMRIDMDExEgwKAgUGEgIwMBICMD"
    "EaJwoKbWFyYXNvdi1BQhIPCgMDBAcSAzAxMBIDMTEwEggKAgcIEgIwMQ==";

/*
  It produces the following bundle.
  Command line:
     echo "..." | base64 -d |
     protoc -I "src/platform/feature-management/proto" \
         --decode=chromiumos.feature_management.api.software.SelectionBundle \
         device_selection.proto

  It produce the following bundle.
  Command line:

selections {
  feature_level: 1
  scope: SCOPE_DEVICES_VALID_OFFSET
  hwid_profiles {
    prefixes: "marasov-AA"
    encoding_requirements {
      bit_locations: 3
      bit_locations: 4
      bit_locations: 7
      required_values: "000"
      required_values: "001"
      required_values: "011"
    }
    encoding_requirements {
      bit_locations: 5
      bit_locations: 6
      required_values: "00"
      required_values: "01"
    }
  }
  hwid_profiles {
    prefixes: "marasov-AB"
    encoding_requirements {
      bit_locations: 3
      bit_locations: 4
      bit_locations: 7
      required_values: "010"
      required_values: "110"
    }
    encoding_requirements {
      bit_locations: 7
      bit_locations: 8
      required_values: "01"
    }
  }
}
*/

// Test fixture for testing feature management.
class FeatureManagementImplTest : public ::testing::Test {
 public:
  FeatureManagementImplTest() = default;
  ~FeatureManagementImplTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    device_info_path_ = temp_dir_.GetPath().Append("device_info");
    auto crossystem_fake = std::make_unique<crossystem::fake::CrossystemFake>();
    cros_system_ =
        std::make_unique<crossystem::Crossystem>(std::move(crossystem_fake));

    auto fake = std::make_unique<FeatureManagementImpl>(
        cros_system_.get(), base::FilePath(), device_info_path_,
        test_feature_proto, test_device_proto, kOsVersion);
    feature_management_ = std::make_unique<FeatureManagement>(std::move(fake));
  }

 protected:
  // Directory and file path used for simulating device info data.
  base::ScopedTempDir temp_dir_;

  // File path where device info data will be simulated.
  base::FilePath device_info_path_;

  // Crossytem to inject hwid.
  std::unique_ptr<crossystem::Crossystem> cros_system_;

  // Object to test.
  std::unique_ptr<FeatureManagement> feature_management_;
};

TEST_F(FeatureManagementImplTest, GetBasicFeature) {
  // Test with an empty file. Expect feature level to be 0, scope to 0.
  EXPECT_EQ(feature_management_->IsFeatureEnabled("A"), false);
  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementA"), true);
  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementAPad"),
            false);
  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementB"), false);

  std::set<std::string> features =
      feature_management_->ListFeatures(USAGE_ANDROID);
  EXPECT_EQ(features.size(), 1);
  EXPECT_NE(features.find("FeatureManagementA"), features.end());

  features = feature_management_->ListFeatures(USAGE_CHROME);
  EXPECT_EQ(features.size(), 0);
}

#if USE_FEATURE_MANAGEMENT

// Basic interface check: make sure both definition of Scope matches.
TEST_F(FeatureManagementImplTest, BasicInterfaceTest) {
  EXPECT_EQ(DeviceInfo_ScopeLevel::DeviceInfo_ScopeLevel_SCOPE_LEVEL_UNKNOWN,
            Feature::SCOPE_UNSPECIFIED);
  EXPECT_EQ(DeviceInfo_ScopeLevel::DeviceInfo_ScopeLevel_SCOPE_LEVEL_0,
            Feature::SCOPE_DEVICES_0);
  EXPECT_EQ(DeviceInfo_ScopeLevel::DeviceInfo_ScopeLevel_SCOPE_LEVEL_1,
            Feature::SCOPE_DEVICES_1);
}

// Use database produced by chromeos-base/feature-management-data.
TEST_F(FeatureManagementImplTest, GetAndCacheStatefulFeatureLevelTest) {
  libsegmentation::DeviceInfo device_info;
  device_info.set_feature_level(libsegmentation::DeviceInfo_FeatureLevel::
                                    DeviceInfo_FeatureLevel_FEATURE_LEVEL_1);
  device_info.set_cached_version_hash(kCurrentVersionHash);
  EXPECT_TRUE(FeatureManagementUtil::WriteDeviceInfoToFile(device_info,
                                                           device_info_path_));
  EXPECT_EQ(
      feature_management_->GetFeatureLevel(),
      FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_1 -
          FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_VALID_OFFSET);

  // Even though the file is changed we should still get the cached value stored
  // from the previous attempt.
  device_info.set_feature_level(libsegmentation::DeviceInfo_FeatureLevel::
                                    DeviceInfo_FeatureLevel_FEATURE_LEVEL_0);
  EXPECT_TRUE(FeatureManagementUtil::WriteDeviceInfoToFile(device_info,
                                                           device_info_path_));
  EXPECT_EQ(
      feature_management_->GetFeatureLevel(),
      FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_1 -
          FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_VALID_OFFSET);
}

// Use database produced by chromeos-base/feature-management-data.
TEST_F(FeatureManagementImplTest, GetAndCacheStatefulScopeLevelTest) {
  libsegmentation::DeviceInfo device_info;
  device_info.set_scope_level(libsegmentation::DeviceInfo_ScopeLevel::
                                  DeviceInfo_ScopeLevel_SCOPE_LEVEL_1);
  device_info.set_cached_version_hash(kCurrentVersionHash);
  EXPECT_TRUE(FeatureManagementUtil::WriteDeviceInfoToFile(device_info,
                                                           device_info_path_));
  EXPECT_EQ(
      feature_management_->GetScopeLevel(),
      FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_1 -
          FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_VALID_OFFSET);

  // Even though the file is changed we should still get the cached value stored
  // from the previous attempt.
  device_info.set_scope_level(libsegmentation::DeviceInfo_ScopeLevel::
                                  DeviceInfo_ScopeLevel_SCOPE_LEVEL_0);
  EXPECT_TRUE(FeatureManagementUtil::WriteDeviceInfoToFile(device_info,
                                                           device_info_path_));
  EXPECT_EQ(
      feature_management_->GetScopeLevel(),
      FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_1 -
          FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_VALID_OFFSET);
}

TEST_F(FeatureManagementImplTest, GetFeatureLevel0) {
  libsegmentation::DeviceInfo device_info;
  device_info.set_feature_level(libsegmentation::DeviceInfo_FeatureLevel::
                                    DeviceInfo_FeatureLevel_FEATURE_LEVEL_0);
  device_info.set_cached_version_hash(kCurrentVersionHash);
  EXPECT_TRUE(FeatureManagementUtil::WriteDeviceInfoToFile(device_info,
                                                           device_info_path_));

  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementA"), true);
  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementB"), false);
  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementD"), false);
}

TEST_F(FeatureManagementImplTest, GetFeatureLevel1Scope0) {
  libsegmentation::DeviceInfo device_info;
  device_info.set_feature_level(libsegmentation::DeviceInfo_FeatureLevel::
                                    DeviceInfo_FeatureLevel_FEATURE_LEVEL_1);
  device_info.set_cached_version_hash(kCurrentVersionHash);
  EXPECT_TRUE(FeatureManagementUtil::WriteDeviceInfoToFile(device_info,
                                                           device_info_path_));

  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementA"), true);
  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementB"), false);
  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementD"), false);
}

TEST_F(FeatureManagementImplTest, GetFeatureLevel1Scope1) {
  libsegmentation::DeviceInfo device_info;
  device_info.set_feature_level(libsegmentation::DeviceInfo_FeatureLevel::
                                    DeviceInfo_FeatureLevel_FEATURE_LEVEL_1);
  device_info.set_scope_level(libsegmentation::DeviceInfo_ScopeLevel::
                                  DeviceInfo_ScopeLevel_SCOPE_LEVEL_1);
  device_info.set_cached_version_hash(kCurrentVersionHash);
  EXPECT_TRUE(FeatureManagementUtil::WriteDeviceInfoToFile(device_info,
                                                           device_info_path_));

  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementA"), true);
  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementB"), true);
  EXPECT_EQ(feature_management_->IsFeatureEnabled("FeatureManagementD"), false);
}

// Test a "real" implementation on a device without VPD (host).
// Build an non-existing directory.
class FeatureManagementImplNoVpdTest : public ::testing::Test {
 public:
  FeatureManagementImplNoVpdTest() = default;
  ~FeatureManagementImplNoVpdTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base::FilePath fake_vpd_directory =
        temp_dir_.GetPath().Append("non-existient");

    base::FilePath device_info_path =
        fake_vpd_directory.Append("feature_device_info");

    auto broken = std::make_unique<FeatureManagementImpl>(
        nullptr, fake_vpd_directory, device_info_path, "", "", "");
    feature_management_ =
        std::make_unique<FeatureManagement>(std::move(broken));
  }

 protected:
  // Directory and file path used for simulating device info data.
  base::ScopedTempDir temp_dir_;

  // Object to test.
  std::unique_ptr<FeatureManagement> feature_management_;
};

TEST_F(FeatureManagementImplNoVpdTest, GetBasicFeature) {
  // Test with an empty file. Expect feature level to be 0, scope to 0.
  EXPECT_EQ(
      feature_management_->GetFeatureLevel(),
      FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_0 -
          FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_VALID_OFFSET);
  EXPECT_EQ(
      feature_management_->GetScopeLevel(),
      FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_0 -
          FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_VALID_OFFSET);
}

#endif

}  // namespace segmentation
