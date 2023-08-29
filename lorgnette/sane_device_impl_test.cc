// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_device_impl.h"

#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include <base/containers/contains.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

#include "lorgnette/constants.h"
#include "lorgnette/libsane_wrapper.h"
#include "lorgnette/libsane_wrapper_fake.h"
#include "lorgnette/libsane_wrapper_impl.h"
#include "lorgnette/manager.h"
#include "lorgnette/sane_client_impl.h"
#include "lorgnette/test_util.h"

using ::testing::ElementsAre;

namespace lorgnette {

namespace {

SANE_Option_Descriptor MakeOptionCountDescriptor() {
  return {
      .name = nullptr,
      .title = nullptr,
      .desc = nullptr,
      .type = SANE_TYPE_INT,
      .size = sizeof(SANE_Word),
  };
}

SANE_Option_Descriptor MakeGroupOptionDescriptor(const char* title) {
  return {
      .name = nullptr,
      .title = title,
      .desc = nullptr,
      .type = SANE_TYPE_GROUP,
      .size = 0,
  };
}

}  // namespace

class SaneDeviceImplTest : public testing::Test {
 protected:
  void SetUp() override {
    libsane_ = LibsaneWrapperImpl::Create();
    client_ = SaneClientImpl::Create(libsane_.get());
    device_ = client_->ConnectToDevice(nullptr, nullptr, "test");
    EXPECT_TRUE(device_);
  }

  void ReloadOptions() {
    dynamic_cast<SaneDeviceImpl*>(device_.get())->LoadOptions(nullptr);
  }

  std::unique_ptr<LibsaneWrapper> libsane_;
  std::unique_ptr<SaneClient> client_;
  std::unique_ptr<SaneDevice> device_;
};

// Check that GetValidOptionValues returns correct values for the test backend.
TEST_F(SaneDeviceImplTest, GetValidOptionValuesSuccess) {
  std::optional<ValidOptionValues> values =
      device_->GetValidOptionValues(nullptr);
  EXPECT_TRUE(values.has_value());
  ASSERT_EQ(values->resolutions.size(), 1200);
  for (int i = 0; i < 1200; i++)
    EXPECT_EQ(values->resolutions[i], i + 1);

  std::vector<ColorMode> color_modes = {MODE_GRAYSCALE, MODE_COLOR};
  std::vector<uint32_t> resolutions = {75, 100, 150, 200, 300, 600};
  EXPECT_THAT(values->sources,
              ElementsAre(EqualsDocumentSource(CreateDocumentSource(
                              "Flatbed", SOURCE_PLATEN, 200.0, 200.0,
                              resolutions, color_modes)),
                          EqualsDocumentSource(CreateDocumentSource(
                              "Automatic Document Feeder", SOURCE_ADF_SIMPLEX,
                              200.0, 200.0, resolutions, color_modes))));

  EXPECT_THAT(values->color_modes,
              ElementsAre(kScanPropertyModeGray, kScanPropertyModeColor));
}

// Check that SetScanResolution works for all valid values.
TEST_F(SaneDeviceImplTest, SetResolution) {
  std::optional<ValidOptionValues> values =
      device_->GetValidOptionValues(nullptr);
  EXPECT_TRUE(values.has_value());

  for (int resolution : values->resolutions)
    EXPECT_TRUE(device_->SetScanResolution(nullptr, resolution));
}

// Check the SetDocumentSource rejects invalid values and works properly for all
// valid values. Also check that GetDocumentSource returns that correct value
// after SetDocumentSource, even if we force-reload option values from scanner.
TEST_F(SaneDeviceImplTest, SetSource) {
  EXPECT_FALSE(device_->SetDocumentSource(nullptr, "invalid source"));

  std::optional<ValidOptionValues> values =
      device_->GetValidOptionValues(nullptr);
  EXPECT_TRUE(values.has_value());

  // Test both with and without reloading options after setting option, since
  // it can surface different bugs.
  for (bool reload_options : {true, false}) {
    LOG(INFO) << "Testing " << (reload_options ? "with" : "without")
              << " option reloading.";
    for (const DocumentSource& source : values->sources) {
      EXPECT_TRUE(device_->SetDocumentSource(nullptr, source.name()));
      if (reload_options) {
        ReloadOptions();
      }

      std::optional<std::string> scanner_value =
          device_->GetDocumentSource(nullptr);
      EXPECT_TRUE(scanner_value.has_value());
      EXPECT_EQ(scanner_value.value(), source.name());
    }
  }
}

// Check that SetColorMode rejects invalid values, and accepts all valid values.
// Also check that GetColorMode returns the correct value after SetColorMode,
// even if we force-reload option values from the scanner.
TEST_F(SaneDeviceImplTest, SetColorMode) {
  EXPECT_FALSE(device_->SetColorMode(nullptr, MODE_UNSPECIFIED));

  std::optional<ValidOptionValues> values =
      device_->GetValidOptionValues(nullptr);
  EXPECT_TRUE(values.has_value());

  // Test both with and without reloading options after setting option, since
  // it can surface different bugs.
  for (bool reload_options : {true, false}) {
    LOG(INFO) << "Testing " << (reload_options ? "with" : "without")
              << " option reloading.";
    for (const std::string& mode_string : values->color_modes) {
      ColorMode mode = impl::ColorModeFromSaneString(mode_string);
      EXPECT_NE(mode, MODE_UNSPECIFIED)
          << "Unexpected ColorMode string " << mode_string;
      EXPECT_TRUE(device_->SetColorMode(nullptr, mode));

      if (reload_options) {
        ReloadOptions();
      }

      std::optional<ColorMode> scanner_value = device_->GetColorMode(nullptr);
      EXPECT_TRUE(scanner_value.has_value());
      EXPECT_EQ(scanner_value.value(), mode);
    }
  }
}

// Check that Scan Region can be set without problems from justification with
// all source types.
TEST_F(SaneDeviceImplTest, SetScanRegionWithJustification) {
  ReloadOptions();
  const double width = 187;  /* mm */
  const double height = 123; /* mm */

  ScanRegion region;
  region.set_top_left_x(0);
  region.set_top_left_y(0);
  region.set_bottom_right_x(width);
  region.set_bottom_right_y(height);

  std::optional<ValidOptionValues> values =
      device_->GetValidOptionValues(nullptr);
  EXPECT_TRUE(values.has_value());

  for (const DocumentSource& source : values->sources) {
    EXPECT_TRUE(device_->SetDocumentSource(nullptr, source.name()));
    EXPECT_TRUE(device_->SetScanRegion(nullptr, region));
  }
}

// Check that extra calls to StartScan fail properly.
TEST_F(SaneDeviceImplTest, DuplicateStartScan) {
  EXPECT_EQ(device_->StartScan(nullptr), SANE_STATUS_GOOD);
  EXPECT_EQ(device_->StartScan(nullptr), SANE_STATUS_DEVICE_BUSY);
}

// Check that GetScanParameters returns the correct values corresponding to the
// input resolution and scan region.
TEST_F(SaneDeviceImplTest, GetScanParameters) {
  const int resolution = 100; /* dpi */
  EXPECT_TRUE(device_->SetScanResolution(nullptr, resolution));

  const double width = 187;  /* mm */
  const double height = 123; /* mm */

  ScanRegion region;
  region.set_top_left_x(0);
  region.set_top_left_y(0);
  region.set_bottom_right_x(width);
  region.set_bottom_right_y(height);
  EXPECT_TRUE(device_->SetScanRegion(nullptr, region));

  std::optional<ScanParameters> params = device_->GetScanParameters(nullptr);
  EXPECT_TRUE(params.has_value());
  EXPECT_TRUE(params->format == kGrayscale);

  const double mms_per_inch = 25.4;
  EXPECT_EQ(params->bytes_per_line,
            static_cast<int>(width / mms_per_inch * resolution));
  EXPECT_EQ(params->pixels_per_line,
            static_cast<int>(width / mms_per_inch * resolution));
  EXPECT_EQ(params->lines,
            static_cast<int>(height / mms_per_inch * resolution));
  EXPECT_EQ(params->depth, 8);
}

// Check that ReadScanData fails when we haven't started a scan.
TEST_F(SaneDeviceImplTest, ReadScanDataWhenNotStarted) {
  std::vector<uint8_t> buf(8192);
  size_t read = 0;

  EXPECT_EQ(device_->ReadScanData(nullptr, buf.data(), buf.size(), &read),
            SANE_STATUS_INVAL);
}

// Check that ReadScanData fails with invalid input pointers.
TEST_F(SaneDeviceImplTest, ReadScanDataBadPointers) {
  std::vector<uint8_t> buf(8192);
  size_t read = 0;

  EXPECT_EQ(device_->StartScan(nullptr), SANE_STATUS_GOOD);
  EXPECT_EQ(device_->ReadScanData(nullptr, nullptr, buf.size(), &read),
            SANE_STATUS_INVAL);
  EXPECT_EQ(device_->ReadScanData(nullptr, buf.data(), buf.size(), nullptr),
            SANE_STATUS_INVAL);
}

// Check that we can successfully run a scan to completion.
TEST_F(SaneDeviceImplTest, RunScan) {
  std::vector<uint8_t> buf(8192);
  size_t read = 0;

  EXPECT_EQ(device_->StartScan(nullptr), SANE_STATUS_GOOD);
  SANE_Status status = SANE_STATUS_GOOD;
  do {
    status = device_->ReadScanData(nullptr, buf.data(), buf.size(), &read);
  } while (status == SANE_STATUS_GOOD && read != 0);
  EXPECT_EQ(read, 0);
  EXPECT_EQ(status, SANE_STATUS_EOF);
}

// Subclass of SaneDeviceImpl that gives public access to some of the private
// methods for testing.
class SaneDeviceImplPeer : public SaneDeviceImpl {
 public:
  SaneDeviceImplPeer(LibsaneWrapper* libsane,
                     SANE_Handle handle,
                     const std::string& name,
                     std::shared_ptr<DeviceSet> open_devices)
      : SaneDeviceImpl(libsane, handle, name, open_devices) {}

  using SaneDeviceImpl::LoadOptions;
  using SaneDeviceImpl::ScanOption;

  std::unordered_map<std::string, SaneOption> GetAllOptions() {
    return all_options_;
  }
  std::vector<lorgnette::OptionGroup> GetOptionGroups() {
    return option_groups_;
  }
  std::unordered_map<SaneDeviceImpl::ScanOption, SaneOption> GetKnownOptions() {
    return known_options_;
  }
};

TEST(SaneDeviceImplFakeSaneTest, LoadOptionsNoOptionZero) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);
  EXPECT_FALSE(device.LoadOptions(&error));
  ASSERT_NE(error, nullptr);
  EXPECT_TRUE(error->HasError(kDbusDomain, kManagerServiceError));
}

TEST(SaneDeviceImplFakeSaneTest, LoadOptionsMissingOptionCountValue) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      {
          .name = nullptr,
          .title = nullptr,
          .desc = nullptr,
          .type = SANE_TYPE_INT,
          // No size to cause a failure in sane_control_option().
      },
  };
  libsane.SetDescriptors(h, sane_options);

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);
  EXPECT_FALSE(device.LoadOptions(&error));
  ASSERT_NE(error, nullptr);
  EXPECT_TRUE(error->HasError(kDbusDomain, kManagerServiceError));
}

TEST(SaneDeviceImplFakeSaneTest, LoadOptionsZeroOptionCount) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      MakeOptionCountDescriptor(),
  };
  libsane.SetDescriptors(h, sane_options);

  SANE_Int option_count = 0;
  libsane.SetOptionValue(h, 0, &option_count);

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);
  EXPECT_TRUE(device.LoadOptions(&error));
  EXPECT_EQ(error, nullptr);
  EXPECT_EQ(device.GetAllOptions().size(), 0);
  EXPECT_EQ(device.GetOptionGroups().size(), 0);
}

TEST(SaneDeviceImplFakeSaneTest, LoadOptionsZeroRealOptions) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      MakeOptionCountDescriptor(),
  };
  libsane.SetDescriptors(h, sane_options);

  SANE_Int option_count = 1;
  libsane.SetOptionValue(h, 0, &option_count);

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);
  EXPECT_TRUE(device.LoadOptions(&error));
  EXPECT_EQ(error, nullptr);
  EXPECT_EQ(device.GetAllOptions().size(), 0);
  EXPECT_EQ(device.GetOptionGroups().size(), 0);
}

TEST(SaneDeviceImplFakeSaneTest, LoadOptionsExcessOptions) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      MakeOptionCountDescriptor(),
  };
  libsane.SetDescriptors(h, sane_options);

  SANE_Int option_count = 2;  // One option claimed, but none available.
  libsane.SetOptionValue(h, 0, &option_count);

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);
  EXPECT_FALSE(device.LoadOptions(&error));
  ASSERT_NE(error, nullptr);
  EXPECT_TRUE(error->HasError(kDbusDomain, kManagerServiceError));
}

TEST(SaneDeviceImplFakeSaneTest, LoadOptionsOneRealOptionMissingValue) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      MakeOptionCountDescriptor(),
      {
          .name = "first-option",
          .title = "First Option",
          .desc = "First option description",
          .type = SANE_TYPE_INT,
          .unit = SANE_UNIT_NONE,
          .size = sizeof(SANE_Word),
          .cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT,
          .constraint_type = SANE_CONSTRAINT_NONE,
      }};
  libsane.SetDescriptors(h, sane_options);

  SANE_Int option_count = 2;
  libsane.SetOptionValue(h, 0, &option_count);

  // Leave index 1 unset.  Will fail when sane_control_option is called.

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);
  EXPECT_FALSE(device.LoadOptions(&error));
  ASSERT_NE(error, nullptr);
  EXPECT_TRUE(error->HasError(kDbusDomain, kManagerServiceError));
}

TEST(SaneDeviceImplFakeSaneTest, LoadOptionsOneRealOptionNoGroup) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      MakeOptionCountDescriptor(),
      {
          .name = "first-option",
          .title = "First Option",
          .desc = "First option description",
          .type = SANE_TYPE_INT,
          .unit = SANE_UNIT_NONE,
          .size = sizeof(SANE_Word),
          .cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT,
          .constraint_type = SANE_CONSTRAINT_NONE,
      }};
  libsane.SetDescriptors(h, sane_options);

  SANE_Int option_count = 2;
  libsane.SetOptionValue(h, 0, &option_count);

  SANE_Int first_option = 42;
  libsane.SetOptionValue(h, 1, &first_option);

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);
  EXPECT_TRUE(device.LoadOptions(&error));
  EXPECT_EQ(error, nullptr);
  EXPECT_EQ(device.GetAllOptions().size(), 1);
  EXPECT_EQ(device.GetOptionGroups().size(), 0);
}

TEST(SaneDeviceImplFakeSaneTest, LoadOptionsMultipleOptionsInGroups) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      MakeOptionCountDescriptor(),
      MakeGroupOptionDescriptor("First Group"),
      {
          .name = "first-option",
          .title = "First Option",
          .desc = "First option description",
          .type = SANE_TYPE_INT,
          .unit = SANE_UNIT_NONE,
          .size = sizeof(SANE_Word),
          .cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT,
          .constraint_type = SANE_CONSTRAINT_NONE,
      },
      MakeGroupOptionDescriptor("Second Group"),
      {
          .name = "second-option",
          .title = "Second Option",
          .desc = "Second option description",
          .type = SANE_TYPE_BOOL,
          .unit = SANE_UNIT_NONE,
          .size = sizeof(SANE_Word),
          .cap =
              SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT | SANE_CAP_INACTIVE,
          .constraint_type = SANE_CONSTRAINT_NONE,
      },
      MakeGroupOptionDescriptor("Third Group"),
  };
  libsane.SetDescriptors(h, sane_options);

  SANE_Int option_count = 6;
  libsane.SetOptionValue(h, 0, &option_count);

  SANE_Int first_option = 42;
  libsane.SetOptionValue(h, 2, &first_option);

  SANE_Bool second_option = SANE_TRUE;
  libsane.SetOptionValue(h, 4, &second_option);

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);
  EXPECT_TRUE(device.LoadOptions(&error));
  EXPECT_EQ(error, nullptr);

  // Just look up the names.  Assume the actual option parsing has been tested
  // in SaneOption tests.
  EXPECT_EQ(device.GetAllOptions().size(), 2);
  EXPECT_TRUE(base::Contains(device.GetAllOptions(), "first-option"));
  EXPECT_TRUE(base::Contains(device.GetAllOptions(), "second-option"));

  ASSERT_EQ(device.GetOptionGroups().size(), 3);
  EXPECT_EQ(device.GetOptionGroups()[0].title(), "First Group");
  EXPECT_THAT(device.GetOptionGroups()[0].members(),
              ElementsAre("first-option"));
  EXPECT_EQ(device.GetOptionGroups()[1].title(), "Second Group");
  EXPECT_THAT(device.GetOptionGroups()[1].members(),
              ElementsAre("second-option"));
  EXPECT_EQ(device.GetOptionGroups()[2].title(), "Third Group");
  EXPECT_EQ(device.GetOptionGroups()[2].members().size(), 0);
}

TEST(SaneDeviceImplFakeSaneTest, LoadOptionsOneKnownOption) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      MakeOptionCountDescriptor(),
      {
          .name = "resolution",
          .title = "Resolution",
          .desc = "Scanning resolution in DPI",
          .type = SANE_TYPE_INT,
          .unit = SANE_UNIT_DPI,
          .size = sizeof(SANE_Word),
          .cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT,
          .constraint_type = SANE_CONSTRAINT_NONE,
      }};
  libsane.SetDescriptors(h, sane_options);

  SANE_Int option_count = 2;
  libsane.SetOptionValue(h, 0, &option_count);

  SANE_Int resolution_value = 150;
  libsane.SetOptionValue(h, 1, &resolution_value);

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);
  EXPECT_TRUE(device.LoadOptions(&error));
  EXPECT_EQ(error, nullptr);
  EXPECT_EQ(device.GetAllOptions().size(), 1);
  EXPECT_TRUE(base::Contains(device.GetAllOptions(), "resolution"));
  EXPECT_EQ(device.GetKnownOptions().size(), 1);
  EXPECT_TRUE(base::Contains(device.GetKnownOptions(),
                             SaneDeviceImplPeer::ScanOption::kResolution));
  EXPECT_EQ(device.GetOptionGroups().size(), 0);
}

TEST(SaneDeviceImplFakeSaneTest, GetCurrentConfiguration) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      MakeOptionCountDescriptor(),
      MakeGroupOptionDescriptor("First Group"),
      {
          .name = "first-option",
          .title = "First Option",
          .desc = "First option description",
          .type = SANE_TYPE_INT,
          .unit = SANE_UNIT_NONE,
          .size = sizeof(SANE_Word),
          .cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT,
          .constraint_type = SANE_CONSTRAINT_NONE,
      },
      MakeGroupOptionDescriptor("Second Group"),
      {
          .name = "second-option",
          .title = "Second Option",
          .desc = "Second option description",
          .type = SANE_TYPE_BOOL,
          .unit = SANE_UNIT_NONE,
          .size = sizeof(SANE_Word),
          .cap =
              SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT | SANE_CAP_INACTIVE,
          .constraint_type = SANE_CONSTRAINT_NONE,
      },
      MakeGroupOptionDescriptor("Third Group"),
  };
  libsane.SetDescriptors(h, sane_options);

  SANE_Int option_count = 6;
  libsane.SetOptionValue(h, 0, &option_count);

  SANE_Int first_option = 42;
  libsane.SetOptionValue(h, 2, &first_option);

  SANE_Bool second_option = SANE_TRUE;
  libsane.SetOptionValue(h, 4, &second_option);

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);
  EXPECT_TRUE(device.LoadOptions(&error));
  EXPECT_EQ(error, nullptr);

  std::optional<ScannerConfig> config = device.GetCurrentConfig(&error);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(error, nullptr);

  // Verify option names are present.  The structure of option parsing has been
  // checked by other tests.
  EXPECT_EQ(config->options().size(), 2);
  EXPECT_TRUE(config->options().contains("first-option"));
  EXPECT_TRUE(config->options().contains("second-option"));

  // Make sure group memberships are correct.
  ASSERT_EQ(config->option_groups_size(), 3);
  EXPECT_EQ(config->option_groups(0).title(), "First Group");
  EXPECT_THAT(config->option_groups(0).members(), ElementsAre("first-option"));
  EXPECT_EQ(config->option_groups(1).title(), "Second Group");
  EXPECT_THAT(config->option_groups(1).members(), ElementsAre("second-option"));
  EXPECT_EQ(config->option_groups(2).title(), "Third Group");
  EXPECT_EQ(config->option_groups(2).members_size(), 0);
}

}  // namespace lorgnette
