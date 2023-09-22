// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_device_impl.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <brillo/files/file_util.h>
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

using ::testing::ContainsRegex;
using ::testing::ElementsAre;
using ::testing::HasSubstr;

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

SANE_Parameters MakeParameters() {
  return {
      .format = SANE_FRAME_RGB,
      .last_frame = SANE_TRUE,
      .bytes_per_line = 100,
      .pixels_per_line = 10,
      .lines = 10,
      .depth = 8,
  };
}

void CheckPngHeader(FILE* file) {
  // File must start with the PNG magic signature.
  char buf[8];
  rewind(file);
  ASSERT_EQ(fread(buf, 1, 8, file), 8);
  EXPECT_EQ(memcmp(buf, "\211PNG\r\n\032\n", 8), 0);
}

void CheckPngFooter(FILE* file) {
  // File must end with IEND chunk.
  char buf[12];
  ASSERT_EQ(fseek(file, -12, SEEK_END), 0);
  ASSERT_EQ(fread(buf, 1, 12, file), 12);
  EXPECT_EQ(memcmp(buf, "\x00\x00\x00\x00IEND\xAE\x42\x60\x82", 12), 0);
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
  ASSERT_TRUE(values.has_value());
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
  ASSERT_TRUE(values.has_value());

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
  ASSERT_TRUE(values.has_value());

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
      ASSERT_TRUE(scanner_value.has_value());
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
  ASSERT_TRUE(values.has_value());

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
      ASSERT_TRUE(scanner_value.has_value());
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
  ASSERT_TRUE(values.has_value());

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

  ScanParameters params;
  SANE_Status status = device_->GetScanParameters(nullptr, &params);
  ASSERT_EQ(status, SANE_STATUS_GOOD);
  EXPECT_TRUE(params.format == kGrayscale);

  const double mms_per_inch = 25.4;
  EXPECT_EQ(params.bytes_per_line,
            static_cast<int>(width / mms_per_inch * resolution));
  EXPECT_EQ(params.pixels_per_line,
            static_cast<int>(width / mms_per_inch * resolution));
  EXPECT_EQ(params.lines, static_cast<int>(height / mms_per_inch * resolution));
  EXPECT_EQ(params.depth, 8);
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

TEST(SaneDeviceImplFakeSaneTest, SupportedFormatsIncludesInternalFormats) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_TRUE(base::Contains(device.GetSupportedFormats(), "image/jpeg"));
  EXPECT_TRUE(base::Contains(device.GetSupportedFormats(), "image/png"));
}

TEST(SaneDeviceImplFakeSaneTest, StartScanNoJobForInvalidHandle) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, nullptr, "TestScanner", open_devices);

  EXPECT_FALSE(device.GetCurrentJob().has_value());
  EXPECT_NE(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);
  EXPECT_FALSE(device.GetCurrentJob().has_value());
}

TEST(SaneDeviceImplFakeSaneTest, DuplicateStartScanFails) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  // No job at first.
  EXPECT_FALSE(device.GetCurrentJob().has_value());

  // StartScan creates a job.
  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);
  ASSERT_TRUE(device.GetCurrentJob().has_value());
  std::string first_job = device.GetCurrentJob().value();
  EXPECT_NE(first_job, "");

  // Second StartScan fails without changing job.
  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_DEVICE_BUSY);
  ASSERT_NE(error, nullptr);
  EXPECT_NE(error->GetMessage(), "");
  ASSERT_TRUE(device.GetCurrentJob().has_value());
  EXPECT_EQ(device.GetCurrentJob().value(), first_job);
}

TEST(SaneDeviceImplFakeSaneTest, StartScanNoJobForSaneFailure) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_NO_DOCS);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  // No job at first.
  EXPECT_FALSE(device.GetCurrentJob().has_value());

  // StartScan doesn't create a job if sane_start fails.
  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_NO_DOCS);
  EXPECT_EQ(error, nullptr);
  EXPECT_FALSE(device.GetCurrentJob().has_value());
}

TEST(SaneDeviceImplFakeSaneTest, CancelScanRequiresHandle) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, nullptr, "TestScanner", open_devices);

  // No job at first.
  EXPECT_FALSE(device.GetCurrentJob().has_value());

  // CancelScan fails without creating a job.
  EXPECT_FALSE(device.CancelScan(&error));
  ASSERT_NE(error, nullptr);
  EXPECT_NE(error->GetMessage(), "");
  EXPECT_FALSE(device.GetCurrentJob().has_value());
}

TEST(SaneDeviceImplFakeSaneTest, CancelScanWithoutJobSucceeds) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  // No job at first.
  EXPECT_FALSE(device.GetCurrentJob().has_value());

  // CancelScan succeeds without creating a job.
  EXPECT_TRUE(device.CancelScan(&error));
  EXPECT_EQ(error, nullptr);
  EXPECT_FALSE(device.GetCurrentJob().has_value());
}

TEST(SaneDeviceImplFakeSaneTest, CreateCancelScanJobSuccessBlocking) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, false);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  // No job at first.
  EXPECT_FALSE(device.GetCurrentJob().has_value());

  // StartScan creates a job.
  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);
  ASSERT_TRUE(device.GetCurrentJob().has_value());
  EXPECT_NE(device.GetCurrentJob().value(), "");

  // CancelScan removes the job.
  EXPECT_TRUE(device.CancelScan(&error));
  EXPECT_EQ(error, nullptr);
  EXPECT_FALSE(device.GetCurrentJob().has_value());
}

TEST(SaneDeviceImplFakeSaneTest, CreateCancelScanJobSuccessNonBlocking) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  // No job at first.
  EXPECT_FALSE(device.GetCurrentJob().has_value());

  // StartScan creates a job.
  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);
  ASSERT_TRUE(device.GetCurrentJob().has_value());
  EXPECT_NE(device.GetCurrentJob().value(), "");

  // CancelScan removes the job.
  EXPECT_TRUE(device.CancelScan(&error));
  EXPECT_EQ(error, nullptr);
  EXPECT_FALSE(device.GetCurrentJob().has_value());
}

TEST(SaneDeviceImplFakeSaneTest, PrepareImageReaderFailsWithoutJob) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_NE(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);
  brillo::DeleteFile(path);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), HasSubstr("No scan job"));
}

TEST(SaneDeviceImplFakeSaneTest, PrepareImageReaderFailsWithoutParameters) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_NE(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);
  brillo::DeleteFile(path);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), ContainsRegex("Failed.*parameters"));
}

TEST(SaneDeviceImplFakeSaneTest, PrepareImageReaderFailsForZeroLines) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  SANE_Parameters params = MakeParameters();
  params.lines = 0;
  libsane.SetParameters(h, params);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_NE(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);
  brillo::DeleteFile(path);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), HasSubstr("invalid height"));
}

TEST(SaneDeviceImplFakeSaneTest, PrepareImageReaderFailsForUnknownHeight) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  SANE_Parameters params = MakeParameters();
  params.lines = -1;
  libsane.SetParameters(h, params);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_NE(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);
  brillo::DeleteFile(path);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), HasSubstr("invalid height"));
}

TEST(SaneDeviceImplFakeSaneTest, PrepareImageReaderFailsForUnknownFormat) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  libsane.SetParameters(h, MakeParameters());
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_NE(device.PrepareImageReader(&error,
                                      static_cast<lorgnette::ImageFormat>(3000),
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);
  brillo::DeleteFile(path);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), HasSubstr("image format"));
}

TEST(SaneDeviceImplFakeSaneTest, PrepareImageReaderFailsForImpossibleJPEG) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  SANE_Parameters params = MakeParameters();
  params.depth = 4;
  libsane.SetParameters(h, params);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_NE(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_JPEG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);
  brillo::DeleteFile(path);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), ContainsRegex("image reader.*JPEG"));
}

TEST(SaneDeviceImplFakeSaneTest, PrepareImageReaderFailsForImpossiblePNG) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  SANE_Parameters params = MakeParameters();
  params.depth = 4;
  libsane.SetParameters(h, params);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_NE(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);
  brillo::DeleteFile(path);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), ContainsRegex("image reader.*PNG"));
}

TEST(SaneDeviceImplFakeSaneTest, PrepareImageReaderSuccess) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  libsane.SetParameters(h, MakeParameters());
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_EQ(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);
  brillo::DeleteFile(path);
  EXPECT_EQ(error, nullptr);
}

TEST(SaneDeviceImplFakeSaneTest, ReadEncodedDataFailsWithoutPreparation) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  libsane.SetParameters(h, MakeParameters());
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  size_t bytes = -1;
  size_t lines = -1;
  EXPECT_NE(device.ReadEncodedData(&error, &bytes, &lines), SANE_STATUS_GOOD);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), HasSubstr("parameters"));
  EXPECT_EQ(bytes, 0);
  EXPECT_EQ(lines, 0);
}

TEST(SaneDeviceImplFakeSaneTest, ReadEncodedDataPropagatesCancellation) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  libsane.SetParameters(h, MakeParameters());
  libsane.AddSaneReadResponse(h, SANE_STATUS_CANCELLED, 0);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_EQ(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);

  size_t bytes = -1;
  size_t lines = -1;
  EXPECT_EQ(device.ReadEncodedData(&error, &bytes, &lines),
            SANE_STATUS_CANCELLED);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), HasSubstr("cancel"));
  EXPECT_EQ(bytes, 0);
  EXPECT_EQ(lines, 0);

  CheckPngHeader(out_file.get());
  brillo::DeleteFile(path);
}

TEST(SaneDeviceImplFakeSaneTest, ReadEncodedDataFailsIfFinalizationFails) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  libsane.SetParameters(h, MakeParameters());
  libsane.AddSaneReadResponse(h, SANE_STATUS_EOF, 0);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_EQ(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);

  size_t bytes = -1;
  size_t lines = -1;
  EXPECT_EQ(device.ReadEncodedData(&error, &bytes, &lines),
            SANE_STATUS_IO_ERROR);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), ContainsRegex("Finalizing PNG.*fail"));
  EXPECT_EQ(bytes, 0);
  EXPECT_EQ(lines, 0);

  CheckPngHeader(out_file.get());
  brillo::DeleteFile(path);
}

TEST(SaneDeviceImplFakeSaneTest, ReadEncodedDataPropagatesEOF) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  libsane.SetParameters(h, MakeParameters());
  libsane.AddSaneReadResponse(h, SANE_STATUS_GOOD, 1000);
  libsane.AddSaneReadResponse(h, SANE_STATUS_EOF, 0);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_EQ(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);

  size_t bytes = -1;
  size_t lines = -1;
  EXPECT_EQ(device.ReadEncodedData(&error, &bytes, &lines), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);
  EXPECT_GT(bytes, 0);
  EXPECT_EQ(lines, 10);

  EXPECT_EQ(device.ReadEncodedData(&error, &bytes, &lines), SANE_STATUS_EOF);
  EXPECT_EQ(error, nullptr);
  EXPECT_EQ(bytes, 0);
  EXPECT_EQ(lines, 0);

  CheckPngHeader(out_file.get());
  CheckPngFooter(out_file.get());
  brillo::DeleteFile(path);
}

TEST(SaneDeviceImplFakeSaneTest, ReadEncodedDataPropagatesFailures) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  libsane.SetParameters(h, MakeParameters());
  libsane.AddSaneReadResponse(h, SANE_STATUS_GOOD, 100);
  libsane.AddSaneReadResponse(h, SANE_STATUS_JAMMED, 0);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_EQ(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);

  size_t bytes = -1;
  size_t lines = -1;
  EXPECT_EQ(device.ReadEncodedData(&error, &bytes, &lines), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);
  EXPECT_GT(bytes, 0);
  EXPECT_EQ(lines, 1);

  EXPECT_EQ(device.ReadEncodedData(&error, &bytes, &lines), SANE_STATUS_JAMMED);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), HasSubstr("Failed to read"));
  EXPECT_EQ(bytes, 0);
  EXPECT_EQ(lines, 0);

  CheckPngHeader(out_file.get());
  brillo::DeleteFile(path);
}

TEST(SaneDeviceImplFakeSaneTest, ReadEncodedDataCombinesChunks) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  libsane.SetParameters(h, MakeParameters());
  libsane.AddSaneReadResponse(h, SANE_STATUS_GOOD, 250);
  libsane.AddSaneReadResponse(h, SANE_STATUS_GOOD, 250);
  libsane.AddSaneReadResponse(h, SANE_STATUS_GOOD, 250);
  libsane.AddSaneReadResponse(h, SANE_STATUS_GOOD, 250);
  libsane.AddSaneReadResponse(h, SANE_STATUS_EOF, 0);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_EQ(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);

  size_t bytes = -1;
  size_t lines = -1;
  size_t total_lines = 0;
  for (size_t i = 0; i < 4; i++) {
    EXPECT_EQ(device.ReadEncodedData(&error, &bytes, &lines), SANE_STATUS_GOOD);
    EXPECT_EQ(error, nullptr);
    EXPECT_GT(bytes, 0);
    EXPECT_GT(lines, 1);
    total_lines += lines;
  }

  EXPECT_EQ(device.ReadEncodedData(&error, &bytes, &lines), SANE_STATUS_EOF);
  EXPECT_EQ(error, nullptr);
  EXPECT_EQ(bytes, 0);
  EXPECT_EQ(lines, 0);
  EXPECT_EQ(total_lines, 10);

  CheckPngHeader(out_file.get());
  CheckPngFooter(out_file.get());
  brillo::DeleteFile(path);
}

TEST(SaneDeviceImplFakeSaneTest, ReadEncodedFailsWithExcessLines) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  libsane.SetSaneStartResult(h, SANE_STATUS_GOOD);
  libsane.SetSupportsNonBlocking(h, true);
  libsane.SetParameters(h, MakeParameters());
  libsane.AddSaneReadResponse(h, SANE_STATUS_GOOD, 750);
  libsane.AddSaneReadResponse(h, SANE_STATUS_GOOD, 260);
  libsane.AddSaneReadResponse(h, SANE_STATUS_EOF, 0);
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_EQ(device.StartScan(&error), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);

  base::FilePath path;
  base::ScopedFILE out_file = base::CreateAndOpenTemporaryStream(&path);
  ASSERT_NE(out_file.get(), nullptr);

  EXPECT_EQ(device.PrepareImageReader(&error, lorgnette::IMAGE_FORMAT_PNG,
                                      out_file.get(), nullptr),
            SANE_STATUS_GOOD);

  size_t bytes = -1;
  size_t lines = -1;
  EXPECT_EQ(device.ReadEncodedData(&error, &bytes, &lines), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);
  EXPECT_GT(bytes, 0);
  EXPECT_EQ(lines, 7);

  EXPECT_EQ(device.ReadEncodedData(&error, &bytes, &lines),
            SANE_STATUS_IO_ERROR);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), HasSubstr("bytes left over"));
  EXPECT_GT(bytes, 0);
  EXPECT_GT(lines, 0);

  CheckPngHeader(out_file.get());
  brillo::DeleteFile(path);
}

TEST(SaneDeviceImplFakeSaneTest, SetOptionUnknownNameFails) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  ScannerOption option;
  option.set_name("nosuch-option");
  option.set_option_type(OptionType::TYPE_BOOL);
  option.set_bool_value(true);
  EXPECT_EQ(device.SetOption(&error, option), SANE_STATUS_UNSUPPORTED);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), ContainsRegex("Option .* not found"));
}

TEST(SaneDeviceImplFakeSaneTest, SetOptionInvalidDataFails) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      MakeOptionCountDescriptor(),
      {
          .name = "int-option",
          .title = "Int Option",
          .desc = "Int option description",
          .type = SANE_TYPE_INT,
          .unit = SANE_UNIT_NONE,
          .size = sizeof(SANE_Word),
          .cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT,
          .constraint_type = SANE_CONSTRAINT_NONE,
      }};
  libsane.SetDescriptors(h, sane_options);

  SANE_Int option_count = 2;
  libsane.SetOptionValue(h, 0, &option_count);

  SANE_Int int_option = 42;
  libsane.SetOptionValue(h, 1, &int_option);

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_TRUE(device.LoadOptions(&error));
  EXPECT_EQ(error, nullptr);

  ScannerOption option;
  option.set_name("int-option");
  option.set_option_type(OptionType::TYPE_BOOL);
  option.set_bool_value(true);
  EXPECT_EQ(device.SetOption(&error, option), SANE_STATUS_INVAL);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), HasSubstr("Unable to set"));
  EXPECT_EQ(int_option, 42);
}

TEST(SaneDeviceImplFakeSaneTest, SetOptionUnsettableOptionFails) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      MakeOptionCountDescriptor(),
      {
          .name = "int-option",
          .title = "Int Option",
          .desc = "Int option description",
          .type = SANE_TYPE_INT,
          .unit = SANE_UNIT_NONE,
          .size = sizeof(SANE_Word),
          .cap = SANE_CAP_SOFT_DETECT,  // No SANE_CAP_SOFT_SELECT.
          .constraint_type = SANE_CONSTRAINT_NONE,
      }};
  libsane.SetDescriptors(h, sane_options);

  SANE_Int option_count = 2;
  libsane.SetOptionValue(h, 0, &option_count);

  SANE_Int int_option = 42;
  libsane.SetOptionValue(h, 1, &int_option);

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_TRUE(device.LoadOptions(&error));
  EXPECT_EQ(error, nullptr);

  ScannerOption option;
  option.set_name("int-option");
  option.set_option_type(OptionType::TYPE_INT);
  option.mutable_int_value()->add_value(24);
  EXPECT_EQ(device.SetOption(&error, option), SANE_STATUS_UNSUPPORTED);
  ASSERT_NE(error, nullptr);
  EXPECT_THAT(error->GetMessage(), ContainsRegex("Failed to set .* to .*:"));
  EXPECT_EQ(int_option, 42);
}

TEST(SaneDeviceImplFakeSaneTest, SetOptionValidUpdateSucceeds) {
  LibsaneWrapperFake libsane;
  SANE_Handle h = libsane.CreateScanner("TestScanner");
  auto open_devices = std::make_shared<DeviceSet>();

  std::vector<SANE_Option_Descriptor> sane_options = {
      MakeOptionCountDescriptor(),
      {
          .name = "int-option",
          .title = "Int Option",
          .desc = "Int option description",
          .type = SANE_TYPE_INT,
          .unit = SANE_UNIT_NONE,
          .size = sizeof(SANE_Word),
          .cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT,
          .constraint_type = SANE_CONSTRAINT_NONE,
      }};
  libsane.SetDescriptors(h, sane_options);

  SANE_Int option_count = 2;
  libsane.SetOptionValue(h, 0, &option_count);

  SANE_Int int_option = 42;
  libsane.SetOptionValue(h, 1, &int_option);

  brillo::ErrorPtr error;
  ASSERT_EQ(libsane.sane_open("TestScanner", &h), SANE_STATUS_GOOD);
  SaneDeviceImplPeer device(&libsane, h, "TestScanner", open_devices);

  EXPECT_TRUE(device.LoadOptions(&error));
  EXPECT_EQ(error, nullptr);

  ScannerOption option;
  option.set_name("int-option");
  option.set_option_type(OptionType::TYPE_INT);
  option.mutable_int_value()->add_value(24);
  EXPECT_EQ(device.SetOption(&error, option), SANE_STATUS_GOOD);
  EXPECT_EQ(error, nullptr);
  EXPECT_EQ(int_option, 24);
}

}  // namespace lorgnette
