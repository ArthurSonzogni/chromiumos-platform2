// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port.h"

#include <string>

#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "typecd/test_constants.h"
#include "typecd/test_utils.h"

namespace {
constexpr char kInvalidDataRole1[] = "xsadft [hasdr]";
constexpr char kInvalidDataRole2[] = "]asdf[ dsdd";
constexpr char kValidDataRole1[] = "device";
constexpr char kValidDataRole2[] = "[host] device";
constexpr char kValidDataRole3[] = "host [device]";

constexpr char kValidPowerRole1[] = "[source] sink";
constexpr char kValidPowerRole2[] = "source [sink]";
constexpr char kInvalidPowerRole1[] = "asdf#//%sxdfa";
}  // namespace

namespace typecd {

class PortTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    temp_dir_ = scoped_temp_dir_.GetPath();
  }

 public:
  base::FilePath temp_dir_;
  base::ScopedTempDir scoped_temp_dir_;
};

// Check that basic Port creation, partner addition/deletion works.
TEST_F(PortTest, TestBasicAdd) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);
  EXPECT_NE(nullptr, port);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  EXPECT_NE(nullptr, port->partner_);
  port->RemovePartner();
  EXPECT_EQ(nullptr, port->partner_);
}

// Check GetDataRole() for various sysfs values.
TEST_F(PortTest, TestGetDataRole) {
  // Set up fake sysfs directory for the port..
  auto port_path = temp_dir_.Append("port0");
  ASSERT_TRUE(base::CreateDirectory(port_path));

  auto data_role_path = port_path.Append("data_role");
  ASSERT_TRUE(base::WriteFile(data_role_path, kValidDataRole1,
                              strlen(kValidDataRole1)));

  // Create a port.
  auto port = std::make_unique<Port>(base::FilePath(port_path), 0);
  ASSERT_NE(nullptr, port);

  EXPECT_EQ(DataRole::kDevice, port->GetDataRole());

  ASSERT_TRUE(base::WriteFile(data_role_path, kValidDataRole2,
                              strlen(kValidDataRole2)));
  // Fake a port changed event.
  port->PortChanged();
  EXPECT_EQ(DataRole::kHost, port->GetDataRole());

  ASSERT_TRUE(base::WriteFile(port_path.Append("data_role"), kValidDataRole3,
                              strlen(kValidDataRole3)));
  // Fake a port changed event.
  port->PortChanged();
  EXPECT_EQ(DataRole::kDevice, port->GetDataRole());

  ASSERT_TRUE(base::WriteFile(port_path.Append("data_role"), kInvalidDataRole1,
                              strlen(kInvalidDataRole1)));
  // Fake a port changed event.
  port->PortChanged();
  EXPECT_EQ(DataRole::kNone, port->GetDataRole());

  ASSERT_TRUE(base::WriteFile(port_path.Append("data_role"), kInvalidDataRole2,
                              strlen(kInvalidDataRole2)));
  // Fake a port changed event.
  port->PortChanged();
  EXPECT_EQ(DataRole::kNone, port->GetDataRole());
}

// Check GetPowerRole() for various sysfs values.
TEST_F(PortTest, TestGetPowerRole) {
  // Set up fake sysfs directory for the port..
  auto port_path = temp_dir_.Append("port0");
  ASSERT_TRUE(base::CreateDirectory(port_path));

  auto data_role_path = port_path.Append("power_role");
  ASSERT_TRUE(base::WriteFile(data_role_path, kValidPowerRole1,
                              strlen(kValidPowerRole1)));

  // Create a port.
  auto port = std::make_unique<Port>(base::FilePath(port_path), 0);
  ASSERT_NE(nullptr, port);

  EXPECT_EQ(PowerRole::kSource, port->GetPowerRole());

  ASSERT_TRUE(base::WriteFile(data_role_path, kValidPowerRole2,
                              strlen(kValidPowerRole2)));
  // Fake a port changed event.
  port->PortChanged();
  EXPECT_EQ(PowerRole::kSink, port->GetPowerRole());

  ASSERT_TRUE(base::WriteFile(data_role_path, kInvalidPowerRole1,
                              strlen(kInvalidPowerRole1)));
  // Fake a port changed event.
  port->PortChanged();
  EXPECT_EQ(PowerRole::kNone, port->GetPowerRole());
}

// Check that DP Alt Mode Entry checks work as expected for a true case:
TEST_F(PortTest, TestDPAltModeEntryCheckTrue) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // Set up fake sysfs paths for 1 alt mode.
  // Set the number of alt modes supported.
  port->partner_->SetNumAltModes(1);

  // Add the DP alt mode.
  std::string mode0_dirname =
      base::StringPrintf("port%d-partner.%d", 0, kDPAltModeIndex);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kDPAltModeSID, kDPVDO_WD19TB,
                                kDPVDOIndex_WD19TB));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add cable with USB3 to pass DPAltMode check. (Anker USB3.2 Gen2 Cable)
  port->AddCable(base::FilePath(kFakePort0CableSysPath));
  port->cable_->SetPDRevision(PDRevision::k30);
  port->cable_->SetIdHeaderVDO(0x6c0004e8);
  port->cable_->SetCertStatVDO(0x000f4246);
  port->cable_->SetProductVDO(0xa0200212);
  port->cable_->SetProductTypeVDO1(0x110000db);
  port->cable_->SetProductTypeVDO2(0x00000000);
  port->cable_->SetProductTypeVDO3(0x00000000);

  bool invalid_dpalt_cable = false;
  EXPECT_TRUE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_FALSE(invalid_dpalt_cable);
}

// Check that DP Alt Mode Entry checks work as expected for a specific false
// case: The Startech dock DP VDO doesn't advertise DFP_D, so we *shouldn't*
// enter DP alternate mode, despite it supporting the DP SID.
TEST_F(PortTest, TestDPAltModeEntryCheckFalseWithDPSID) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // Set up fake sysfs paths for 2 alt modes.
  // Set the number of alt modes supported.
  port->partner_->SetNumAltModes(2);

  // Add the DP alt mode.
  std::string mode0_dirname =
      base::StringPrintf("port%d-partner.%d", 0, kDPAltModeIndex);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(
      CreateFakeAltMode(mode0_path, kDPAltModeSID, kDPVDO, kDPVDOIndex));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the TBT alt mode.
  std::string mode1_dirname =
      base::StringPrintf("port%d-partner.%d", 0, kTBTAltModeIndex);
  auto mode1_path = temp_dir_.Append(mode1_dirname);
  ASSERT_TRUE(
      CreateFakeAltMode(mode1_path, kTBTAltModeVID, kTBTVDO, kTBTVDOIndex));
  port->AddRemovePartnerAltMode(mode1_path, true);

  bool invalid_dpalt_cable = false;
  EXPECT_FALSE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_FALSE(invalid_dpalt_cable);
}

// Check that DP Alt Mode Entry checks work as expected for false cases.
TEST_F(PortTest, TestDPAltModeEntryCheckFalse) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  port->partner_->SetNumAltModes(0);

  // Check the case where the partner doesn't support any alt modes.
  EXPECT_FALSE(port->CanEnterDPAltMode(nullptr));

  port->partner_->SetNumAltModes(1);

  // Set up fake sysfs paths for 1 alt mode.
  // Add the TBT alt mode.
  std::string mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(
      CreateFakeAltMode(mode_path, kTBTAltModeVID, kTBTVDO, kTBTVDOIndex));
  port->AddRemovePartnerAltMode(mode_path, true);

  EXPECT_FALSE(port->CanEnterDPAltMode(nullptr));
}

// Check that DP Alt Mode Entry works with cable check for a passing case.
// Case: The WIMAXIT Type-C display supports DP alternate mode and the CalDigit
// TBT4 cable supports up to USB4 so it should enter DP alternate mode and the
// cable will not be flagged as invalid
TEST_F(PortTest, TestDPAltModeEntryCalDigitTBT4ToDisplay) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  // Set up fake sysfs paths and add a partner.
  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // PD ID VDOs for the WIMAXIT Type-C Display.
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x6c0004e8);
  port->partner_->SetCertStatVDO(0xf4246);
  port->partner_->SetProductVDO(0xa0200212);
  port->partner_->SetProductTypeVDO1(0x110000db);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  // Set up fake sysfs paths for partner alt modes.
  port->partner_->SetNumAltModes(2);

  // Add the DP alt mode.
  std::string mode0_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kDPAltModeSID, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the TBT alt mode.
  std::string mode1_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  auto mode1_path = temp_dir_.Append(mode1_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode1_path, 0x04e8, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode1_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // PD ID VDOs for the CalDigit TBT4 cable.
  port->cable_->SetPDRevision(PDRevision::k30);
  port->cable_->SetIdHeaderVDO(0x1c002b1d);
  port->cable_->SetCertStatVDO(0x0);
  port->cable_->SetProductVDO(0x15120001);
  port->cable_->SetProductTypeVDO1(0x11082043);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  bool invalid_dpalt_cable = false;
  EXPECT_TRUE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_FALSE(invalid_dpalt_cable);
}

// Check that DP Alt Mode Entry works with cable check for a passing case.
// Case: The WIMAXIT Type-C display supports DP alternate mode and the Anker
// USB3.2 Gen2 cable supports USB3 so it should enter DP alternate mode and
// the cable will not be flagged as invalid
TEST_F(PortTest, TestDPAltModeEntryAnkerUsb3Gen2ToDisplay) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  // Set up fake sysfs paths and add a partner.
  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // PD ID VDOs for the WIMAXIT Type-C Display.
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x6c0004e8);
  port->partner_->SetCertStatVDO(0xf4246);
  port->partner_->SetProductVDO(0xa0200212);
  port->partner_->SetProductTypeVDO1(0x110000db);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  // Set up fake sysfs paths for partner alt modes.
  port->partner_->SetNumAltModes(2);

  // Add the DP alt mode.
  std::string mode0_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kDPAltModeSID, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the TBT alt mode.
  std::string mode1_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  auto mode1_path = temp_dir_.Append(mode1_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode1_path, 0x04e8, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode1_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // PD ID VDOs for the Anker USB3.2 Gen2 cable.
  port->cable_->SetPDRevision(PDRevision::k20);
  port->cable_->SetIdHeaderVDO(0x1c00291a);
  port->cable_->SetCertStatVDO(0xd0b);
  port->cable_->SetProductVDO(0x1ff90000);
  port->cable_->SetProductTypeVDO1(0x11082032);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  bool invalid_dpalt_cable = false;
  EXPECT_TRUE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_FALSE(invalid_dpalt_cable);
}

// Check that DP Alt Mode Entry works with cable check for a passing case.
// Case: The WIMAXIT Type-C display supports DP alternate mode and the HP
// USB3.2 Gen1 cable supports up to USB3.2 Gen1 so it should enter DP
// alternate mode and the cable will not be flagged as invalid
TEST_F(PortTest, TestDPAltModeEntryHPUsb3Gen1ToDisplay) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  // Set up fake sysfs paths and add a partner.
  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // PD ID VDOs for the WIMAXIT Type-C Display.
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x6c0004e8);
  port->partner_->SetCertStatVDO(0xf4246);
  port->partner_->SetProductVDO(0xa0200212);
  port->partner_->SetProductTypeVDO1(0x110000db);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  // Set up fake sysfs paths for partner alt modes.
  port->partner_->SetNumAltModes(2);

  // Add the DP alt mode.
  std::string mode0_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kDPAltModeSID, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the TBT alt mode.
  std::string mode1_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  auto mode1_path = temp_dir_.Append(mode1_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode1_path, 0x04e8, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode1_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // PD ID VDOs for the HP USB3.2 Gen1 cable.
  port->cable_->SetPDRevision(PDRevision::k30);
  port->cable_->SetIdHeaderVDO(0x180003f0);
  port->cable_->SetCertStatVDO(0x4295);
  port->cable_->SetProductVDO(0x264700a0);
  port->cable_->SetProductTypeVDO1(0x11084851);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  bool invalid_dpalt_cable = false;
  EXPECT_TRUE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_FALSE(invalid_dpalt_cable);
}

// Check that DP Alt Mode Entry works with cable check for a passing case.
// Case: The WIMAXIT Type-C display supports DP alternate mode and the Apple
// TBT3 Pro cable supports up to USB4 so it should enter DP alternate mode
// and the cable will not be flagged as invalid
TEST_F(PortTest, TestDPAltModeEntryAppleTBT3ToDisplay) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  // Set up fake sysfs paths and add a partner.
  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // PD ID VDOs for the WIMAXIT Type-C Display.
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x6c0004e8);
  port->partner_->SetCertStatVDO(0xf4246);
  port->partner_->SetProductVDO(0xa0200212);
  port->partner_->SetProductTypeVDO1(0x110000db);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  // Set up fake sysfs paths for partner alt modes.
  port->partner_->SetNumAltModes(2);

  // Add the DP alt mode.
  std::string mode0_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kDPAltModeSID, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the TBT alt mode.
  std::string mode1_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  auto mode1_path = temp_dir_.Append(mode1_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode1_path, 0x04e8, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode1_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // PD ID VDOs for the Apple TBT3 Pro cable.
  port->cable_->SetPDRevision(PDRevision::k30);
  port->cable_->SetIdHeaderVDO(0x1c002b1d);
  port->cable_->SetCertStatVDO(0x0);
  port->cable_->SetProductVDO(0x15120001);
  port->cable_->SetProductTypeVDO1(0x11082043);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  port->cable_->SetNumAltModes(5);

  // Set up fake sysfs paths for cable alt modes.
  auto mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTAltModeVID, 0x00cb0001, 0));
  port->AddCableAltMode(mode_path);

  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 1);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kDPAltModeSID, 0x000c0c0c, 0));
  port->AddCableAltMode(mode_path);

  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 2);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, 0x05ac, 0x00000005, 0));
  port->AddCableAltMode(mode_path);

  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 3);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, 0x05ac, 0x00000007, 1));
  port->AddCableAltMode(mode_path);

  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 4);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, 0x05ac, 0x00000002, 2));
  port->AddCableAltMode(mode_path);

  bool invalid_dpalt_cable = false;
  EXPECT_TRUE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_FALSE(invalid_dpalt_cable);
}

// Check that DP Alt Mode Entry works with cable check for a failing case.
// Case: The WIMAXIT Type-C display supports DP alternate mode but, an unbranded
// USB2 cable is not considered as a cable object in typecd. It should still try
// to enter alternate mode but the cable will be flagged as invalid
TEST_F(PortTest, TestDPAltModeEntryUnbrandedUSB2ToDisplay) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  // Set up fake sysfs paths and add a partner.
  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // PD ID VDOs for the WIMAXIT Type-C Display.
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x6c0004e8);
  port->partner_->SetCertStatVDO(0xf4246);
  port->partner_->SetProductVDO(0xa0200212);
  port->partner_->SetProductTypeVDO1(0x110000db);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  // Set up fake sysfs paths for partner alt modes.
  port->partner_->SetNumAltModes(2);

  // Add the DP alt mode.
  std::string mode0_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kDPAltModeSID, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the TBT alt mode.
  std::string mode1_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  auto mode1_path = temp_dir_.Append(mode1_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode1_path, 0x04e8, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode1_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // PD ID VDOs for the unbranded USB2 cable.
  port->cable_->SetPDRevision(PDRevision::kNone);
  port->cable_->SetIdHeaderVDO(0x0);
  port->cable_->SetCertStatVDO(0x0);
  port->cable_->SetProductVDO(0x0);
  port->cable_->SetProductTypeVDO1(0x0);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  bool invalid_dpalt_cable = false;
  EXPECT_TRUE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_TRUE(invalid_dpalt_cable);
}

// Check that DP Alt Mode Entry works with cable check for a failing case.
// Case: The WIMAXIT Type-C display supports DP alternate mode but, a tested
// Nekteck cable only supports up to USB2. The typec daemon should still try
// to enter alternate mode but the cable will be flagged as invalid
TEST_F(PortTest, TestDPAltModeEntryNekteckUSB2ToDisplay) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  // Set up fake sysfs paths and add a partner.
  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // PD ID VDOs for the WIMAXIT Type-C Display.
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x6c0004e8);
  port->partner_->SetCertStatVDO(0xf4246);
  port->partner_->SetProductVDO(0xa0200212);
  port->partner_->SetProductTypeVDO1(0x110000db);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  // Set up fake sysfs paths for partner alt modes.
  port->partner_->SetNumAltModes(2);

  // Add the DP alt mode.
  std::string mode0_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kDPAltModeSID, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the TBT alt mode.
  std::string mode1_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  auto mode1_path = temp_dir_.Append(mode1_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode1_path, 0x04e8, 0x40045, 0));
  port->AddRemovePartnerAltMode(mode1_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // PD ID VDOs for the Nekteck USB2 cable.
  port->cable_->SetPDRevision(PDRevision::k30);
  port->cable_->SetIdHeaderVDO(0x18002e98);
  port->cable_->SetCertStatVDO(0x1533);
  port->cable_->SetProductVDO(0x50200);
  port->cable_->SetProductTypeVDO1(0x21084040);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  bool invalid_dpalt_cable = false;
  EXPECT_TRUE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_TRUE(invalid_dpalt_cable);
}

// Check that DP Alt Mode Entry works with cable check for a passing case.
// Case: The Thinkpad Dock supports DP alternate mode and a tested unbranded
// TBT3 cable supports up to USB3.2 Gen2 so it should enter DP alternate mode
// and the cable will not be flagged as invalid
TEST_F(PortTest, TestDPAltModeEntryTBT3ToDock) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  // Set up fake sysfs paths and add a partner.
  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // PD ID VDOs for the ThinkPad Dock.
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x540017ef);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0xa31e0000);
  port->partner_->SetProductTypeVDO1(0x0);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  // Set up fake sysfs paths for partner alt modes.
  port->partner_->SetNumAltModes(3);

  // Add the TBT alt mode.
  std::string mode0_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kTBTAltModeVID, 0x1, 0));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the DP alt mode.
  std::string mode1_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  auto mode1_path = temp_dir_.Append(mode1_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode1_path, kDPAltModeSID, 0xc3c47, 0));
  port->AddRemovePartnerAltMode(mode1_path, true);

  // Add additional alt mode. (svid = 0x17ef)
  std::string mode2_dirname = base::StringPrintf("port%d-partner.%d", 0, 2);
  auto mode2_path = temp_dir_.Append(mode2_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode2_path, 0x17ef, 0x1, 0));
  port->AddRemovePartnerAltMode(mode2_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // PD ID VDOs for the unbranded TBT3 cable.
  port->cable_->SetPDRevision(PDRevision::k20);
  port->cable_->SetIdHeaderVDO(0xc0020c2);
  port->cable_->SetCertStatVDO(0x0ba);
  port->cable_->SetProductVDO(0xa31d0310);
  port->cable_->SetProductTypeVDO1(0x21082852);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  bool invalid_dpalt_cable = false;
  EXPECT_TRUE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_FALSE(invalid_dpalt_cable);
}

// Check that DP Alt Mode Entry works with cable check for a failing case.
// Case: The Thinkpad Dock supports DP alternate mode but a tested unbranded
// USB2 cable is not recognized by the typec daemon. It should try to enter
// DP alternate mode but the cable will be flagged as invalid.
TEST_F(PortTest, TestDPAltModeEntryUnbrandedUSB2ToDock) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  // Set up fake sysfs paths and add a partner.
  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // PD ID VDOs for the ThinkPad Dock.
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x540017ef);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0xa31e0000);
  port->partner_->SetProductTypeVDO1(0x0);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  // Set up fake sysfs paths for partner alt modes.
  port->partner_->SetNumAltModes(3);

  // Add the TBT alt mode.
  std::string mode0_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kTBTAltModeVID, 0x1, 0));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the DP alt mode.
  std::string mode1_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  auto mode1_path = temp_dir_.Append(mode1_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode1_path, kDPAltModeSID, 0xc3c47, 0));
  port->AddRemovePartnerAltMode(mode1_path, true);

  // Add additional alt mode. (svid = 0x17ef)
  std::string mode2_dirname = base::StringPrintf("port%d-partner.%d", 0, 2);
  auto mode2_path = temp_dir_.Append(mode2_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode2_path, 0x17ef, 0x1, 0));
  port->AddRemovePartnerAltMode(mode2_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // PD ID VDOs for the unbranded USB2 cable.
  port->cable_->SetPDRevision(PDRevision::kNone);
  port->cable_->SetIdHeaderVDO(00);
  port->cable_->SetCertStatVDO(00);
  port->cable_->SetProductVDO(0x0);
  port->cable_->SetProductTypeVDO1(0x0);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  bool invalid_dpalt_cable = false;
  EXPECT_TRUE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_TRUE(invalid_dpalt_cable);
}

// Check that DP Alt Mode Entry works with cable check for a failing case.
// Case: The Thinkpad Dock supports DP alternate mode but a tested Nekteck
// type-c cable only supports up to USB2. The typec daemon should try to
// enter DP alternate mode but the cable will be flagged as invalid.
TEST_F(PortTest, TestDPAltModeEntryNekteckUSB2ToDock) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  // Set up fake sysfs paths and add a partner.
  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // PD ID VDOs for the ThinkPad Dock.
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x540017ef);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0xa31e0000);
  port->partner_->SetProductTypeVDO1(0x0);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  // Set up fake sysfs paths for partner alt modes.
  port->partner_->SetNumAltModes(3);

  // Add the TBT alt mode.
  std::string mode0_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kTBTAltModeVID, 0x1, 0));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the DP alt mode.
  std::string mode1_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  auto mode1_path = temp_dir_.Append(mode1_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode1_path, kDPAltModeSID, 0xc3c47, 0));
  port->AddRemovePartnerAltMode(mode1_path, true);

  // Add additional alt mode. (svid = 0x17ef)
  std::string mode2_dirname = base::StringPrintf("port%d-partner.%d", 0, 2);
  auto mode2_path = temp_dir_.Append(mode2_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode2_path, 0x17ef, 0x1, 0));
  port->AddRemovePartnerAltMode(mode2_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // PD ID VDOs for the Nekteck USB2 cable.
  port->cable_->SetPDRevision(PDRevision::k30);
  port->cable_->SetIdHeaderVDO(0x18002e98);
  port->cable_->SetCertStatVDO(0x1533);
  port->cable_->SetProductVDO(0x50200);
  port->cable_->SetProductTypeVDO1(0x21084040);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  bool invalid_dpalt_cable = false;
  EXPECT_TRUE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_TRUE(invalid_dpalt_cable);
}

// Check that DP Alt Mode Entry works with cable check for a passing case.
// Case: A small Cable Matters dock uses a captive cable. The type-c daemon
// will not recognize a cable for this dock, but because the partner notes it
// uses a captive cable typecd should enter DP Alt Mode without flagging the
// cable as invalid
TEST_F(PortTest, TestDPAltModeEntryCableMattersDock) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  // Set up fake sysfs paths and add a partner.
  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // PD ID VDOs for the Cable Matters dock.
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x6c0004b4);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0xf6490222);
  port->partner_->SetProductTypeVDO1(0x8);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  // Set up fake sysfs paths for partner alt modes.
  port->partner_->SetNumAltModes(1);

  // Add the DP alt mode.
  std::string mode0_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kDPAltModeSID, 0x405, 0));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // PD ID VDOs for the Cable Matters dock
  port->cable_->SetPDRevision(PDRevision::kNone);
  port->cable_->SetIdHeaderVDO(0x0);
  port->cable_->SetCertStatVDO(0x0);
  port->cable_->SetProductVDO(0x0);
  port->cable_->SetProductTypeVDO1(0x0);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  bool invalid_dpalt_cable = false;
  EXPECT_TRUE(port->CanEnterDPAltMode(&invalid_dpalt_cable));
  EXPECT_FALSE(invalid_dpalt_cable);
}

// Check that TBT Compat Mode Entry checks work as expected for the following
// working case:
// - Startech.com TB3DK2DPW Alpine Ridge Dock.
// - StarTech Passive Cable 40 Gbps PD 2.0
TEST_F(PortTest, TestTBTCompatibilityModeEntryCheckTrueStartech) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  // PD ID VDOs for the Startech.com TB3DK2DPW Alpine Ridge Dock.
  port->partner_->SetPDRevision(PDRevision::k20);
  port->partner_->SetIdHeaderVDO(0xd4008087);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0x0);
  port->partner_->SetProductTypeVDO1(0);
  port->partner_->SetProductTypeVDO2(0);
  port->partner_->SetProductTypeVDO3(0);

  port->partner_->SetNumAltModes(1);
  // Set up fake sysfs paths for 1 alt mode.
  // Add the TBT alt mode.
  std::string mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTAltModeVID, kTBTVDO, 0));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // StarTech Passive Cable 40 Gbps PD 2.0
  port->cable_->SetPDRevision(PDRevision::k20);
  port->cable_->SetIdHeaderVDO(0x1c0020c2);
  port->cable_->SetCertStatVDO(0x000000b6);
  port->cable_->SetProductVDO(0x00010310);
  port->cable_->SetProductTypeVDO1(0x11082052);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  EXPECT_EQ(ModeEntryResult::kSuccess, port->CanEnterTBTCompatibilityMode());
}

// Check that TBT Compat Mode Entry checks work as expected for the following
// non-working case:
// - Startech.com TB3DK2DPW Alpine Ridge Dock.
// - Nekteck USB 2.0 cable (5A).
TEST_F(PortTest, TestTBTCompatibilityModeEntryCheckFalseStartech) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  // PD ID VDOs for the Startech.com TB3DK2DPW Alpine Ridge Dock.
  port->partner_->SetPDRevision(PDRevision::k20);
  port->partner_->SetIdHeaderVDO(0xd4008087);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0x0);
  port->partner_->SetProductTypeVDO1(0);
  port->partner_->SetProductTypeVDO2(0);
  port->partner_->SetProductTypeVDO3(0);

  port->partner_->SetNumAltModes(1);
  // Set up fake sysfs paths for 1 alt mode.
  // Add the TBT alt mode.
  std::string mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTAltModeVID, kTBTVDO, 0));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // Nekteck USB 2.0 cable (5A).
  port->cable_->SetPDRevision(PDRevision::k30);
  port->cable_->SetIdHeaderVDO(0x18002e98);
  port->cable_->SetCertStatVDO(0x00001533);
  port->cable_->SetProductVDO(0x00010200);
  port->cable_->SetProductTypeVDO1(0xc1082040);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  EXPECT_EQ(ModeEntryResult::kCableError, port->CanEnterTBTCompatibilityMode());
}

// Check that TBT Compat Mode Entry checks work as expected for the following
// working case:
// - Dell WD19TB dock.
TEST_F(PortTest, TestTBTCompatibilityModeEntryCheckTrueWD19TB) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  // PD ID VDOs for the Dell WD19TB Titan Ridge Dock.
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x4c0041c3);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0xb0700712);
  port->partner_->SetProductTypeVDO1(0x0);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  port->partner_->SetNumAltModes(4);
  // Set up fake sysfs paths for partner alt modes.
  // Add the TBT alt mode.
  std::string mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(
      CreateFakeAltMode(mode_path, kTBTAltModeVID, kTBTVDO, kTBTVDOIndex));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Add the DP alt mode.
  mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kDPAltModeSID, kDPVDO_WD19TB, 0));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Add the Dell alt mode 1.
  mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 2);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(
      CreateFakeAltMode(mode_path, kDellSVID_WD19TB, kDell_WD19TB_VDO1, 0));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Add the Dell alt mode 2.
  mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 3);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(
      CreateFakeAltMode(mode_path, kDellSVID_WD19TB, kDell_WD19TB_VDO2, 1));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // Dell's cable is captive.
  port->cable_->SetPDRevision(PDRevision::k30);
  port->cable_->SetIdHeaderVDO(0x1c00413c);
  port->cable_->SetCertStatVDO(0x0);
  port->cable_->SetProductVDO(0xb052000);
  port->cable_->SetProductTypeVDO1(0x110c2042);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  EXPECT_EQ(ModeEntryResult::kSuccess, port->CanEnterTBTCompatibilityMode());
}

// Check that USB4 mode checks work as expected for the following
// working case:
// - Intel Gatkex Creek USB4 dock.
// - Belkin TBT3 Passive Cable 40Gbps.
TEST_F(PortTest, TestUSB4EntryTrueGatkexPassiveTBT3Cable) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  // PD ID VDOs for the Gatkex creek USB4 dock..
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x4c800000);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0x0);
  port->partner_->SetProductTypeVDO1(0xd00001b);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  port->partner_->SetNumAltModes(2);
  // Set up fake sysfs paths for partner alt modes.

  // Add the DP alt mode.
  auto mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kDPAltModeSID, kDPVDO_GatkexCreek,
                                kDPVDOIndex_GatkexCreek));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Add the TBT alt mode.
  mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTAltModeVID, kTBTVDO_GatkexCreek,
                                kTBTVDOIndex_GatkexCreek));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  port->cable_->SetPDRevision(PDRevision::k30);
  port->cable_->SetIdHeaderVDO(0x1c002b1d);
  port->cable_->SetCertStatVDO(0x0);
  port->cable_->SetProductVDO(0x150c0001);
  port->cable_->SetProductTypeVDO1(0x11082042);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  port->cable_->SetNumAltModes(1);

  // Set up fake sysfs paths for cable alt modes.
  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 0);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTAltModeVID, 0x30001, 0));
  port->AddCableAltMode(mode_path);

  EXPECT_EQ(ModeEntryResult::kSuccess, port->CanEnterUSB4());
}

// Check that USB4 mode checks work as expected for the following
// working case:
// - Intel Gatkex Creek USB4 dock.
// - Hongju Full USB 3.1 Gen 1 5A passive cable..
TEST_F(PortTest, TestUSB4EntryTrueGatkexPassiveNonTBT3Cable) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  // PD ID VDOs for the Gatkex creek USB4 dock..
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x4c800000);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0x0);
  port->partner_->SetProductTypeVDO1(0xd00001b);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  port->partner_->SetNumAltModes(2);
  // Set up fake sysfs paths for partner alt modes.

  // Add the DP alt mode.
  auto mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kDPAltModeSID, kDPVDO_GatkexCreek,
                                kDPVDOIndex_GatkexCreek));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Add the TBT alt mode.
  mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTAltModeVID, kTBTVDO_GatkexCreek,
                                kTBTVDOIndex_GatkexCreek));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  port->cable_->SetPDRevision(PDRevision::k20);
  port->cable_->SetIdHeaderVDO(0x18005694);
  port->cable_->SetCertStatVDO(0x88);
  port->cable_->SetProductVDO(0xce901a0);
  port->cable_->SetProductTypeVDO1(0x84051);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  port->cable_->SetNumAltModes(0);

  EXPECT_EQ(ModeEntryResult::kSuccess, port->CanEnterUSB4());
}

// Check that USB4 mode checks work as expected for the following
// non-working case:
// - Intel Gatkex Creek USB4 dock.
// - Nekteck USB 2.0 5A Passive Cable.
TEST_F(PortTest, TestUSB4EntryFalseGatkexPassiveNonTBT3Cable) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  // PD ID VDOs for the Gatkex creek USB4 dock..
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x4c800000);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0x0);
  port->partner_->SetProductTypeVDO1(0xd00001b);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  port->partner_->SetNumAltModes(2);
  // Set up fake sysfs paths for partner alt modes.

  // Add the DP alt mode.
  auto mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kDPAltModeSID, kDPVDO_GatkexCreek,
                                kDPVDOIndex_GatkexCreek));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Add the TBT alt mode.
  mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTAltModeVID, kTBTVDO_GatkexCreek,
                                kTBTVDOIndex_GatkexCreek));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  port->cable_->SetPDRevision(PDRevision::k30);
  port->cable_->SetIdHeaderVDO(0x18002e98);
  port->cable_->SetCertStatVDO(0x1533);
  port->cable_->SetProductVDO(0x10200);
  port->cable_->SetProductTypeVDO1(0xc1082040);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  port->cable_->SetNumAltModes(0);

  EXPECT_EQ(ModeEntryResult::kCableError, port->CanEnterUSB4());
}

// Check that USB4 mode checks work as expected for the following
// non-working case:
// - Intel Gatkex Creek USB4 dock.
// - Belkin TBT3 Active Cable 40Gbps.
//
// NOTE: This case is interesting because the TBT3 cable fails as it doesn't
// support Rounded Data rates.
TEST_F(PortTest, TestUSB4EntryFalseGatkexActiveTBT3Cable) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  // PD ID VDOs for the Gatkex creek USB4 dock..
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x4c800000);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0x0);
  port->partner_->SetProductTypeVDO1(0xd00001b);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  port->partner_->SetNumAltModes(2);
  // Set up fake sysfs paths for partner alt modes.

  // Add the DP alt mode.
  auto mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kDPAltModeSID, kDPVDO_GatkexCreek,
                                kDPVDOIndex_GatkexCreek));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Add the TBT alt mode.
  mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTAltModeVID, kTBTVDO_GatkexCreek,
                                kTBTVDOIndex_GatkexCreek));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  port->cable_->SetPDRevision(PDRevision::k20);
  port->cable_->SetIdHeaderVDO(0x240020c2);
  port->cable_->SetCertStatVDO(0x0);
  port->cable_->SetProductVDO(0x40010);
  port->cable_->SetProductTypeVDO1(0x21085858);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  port->cable_->SetNumAltModes(2);

  // Set up fake sysfs paths for cable alt modes.
  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 0);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTAltModeVID, 0x430001, 0));
  port->AddCableAltMode(mode_path);

  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 1);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, 0x04b4, 0x1, 0));
  port->AddCableAltMode(mode_path);

  EXPECT_EQ(ModeEntryResult::kCableError, port->CanEnterUSB4());
}

// Check that USB4 mode checks work as expected for the following
// working case:
// - Intel Gatkex Creek USB4 dock.
// - Apple Thunderbolt 3 Pro Cable.
TEST_F(PortTest, TestUSB4EntryTrueGatkexAppleTBT3ProCable) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  // PD ID VDOs for the Gatkex creek USB4 dock..
  port->partner_->SetPDRevision(PDRevision::k30);
  port->partner_->SetIdHeaderVDO(0x4c800000);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0x0);
  port->partner_->SetProductTypeVDO1(0xd00001b);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  port->partner_->SetNumAltModes(2);
  // Set up fake sysfs paths for partner alt modes.

  // Add the DP alt mode.
  auto mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kDPAltModeSID, kDPVDO_GatkexCreek,
                                kDPVDOIndex_GatkexCreek));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Add the TBT alt mode.
  mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTAltModeVID, kTBTVDO_GatkexCreek,
                                kTBTVDOIndex_GatkexCreek));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  port->cable_->SetPDRevision(PDRevision::k30);
  port->cable_->SetIdHeaderVDO(0x240005ac);
  port->cable_->SetCertStatVDO(0x0);
  port->cable_->SetProductVDO(0x72043002);
  port->cable_->SetProductTypeVDO1(0x434858da);
  port->cable_->SetProductTypeVDO2(0x5a5f0001);
  port->cable_->SetProductTypeVDO3(0x0);

  port->cable_->SetNumAltModes(5);

  // Set up fake sysfs paths for cable alt modes.
  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 0);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTAltModeVID, 0x00cb0001, 0));
  port->AddCableAltMode(mode_path);

  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 1);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kDPAltModeSID, 0x000c0c0c, 0));
  port->AddCableAltMode(mode_path);

  // Apple alt modes.
  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 2);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, 0x05ac, 0x00000005, 0));
  port->AddCableAltMode(mode_path);

  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 3);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, 0x05ac, 0x00000007, 1));
  port->AddCableAltMode(mode_path);

  mode_dirname = base::StringPrintf("port%d-plug0.%d", 0, 4);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, 0x05ac, 0x00000002, 2));
  port->AddCableAltMode(mode_path);

  EXPECT_EQ(ModeEntryResult::kSuccess, port->CanEnterUSB4());
}

}  // namespace typecd
