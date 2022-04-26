// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "typecd/pd_vdo_constants.h"

namespace {

constexpr char kDualRoleRegex[] = R"(\[(\w+)\])";

}  // namespace

namespace typecd {

Port::Port(const base::FilePath& syspath, int port_num)
    : syspath_(syspath),
      port_num_(port_num),
      user_active_on_mode_entry_(false),
      current_mode_(TypeCMode::kNone),
      metrics_reported_(false),
      supports_usb4_(true),
      data_role_(DataRole::kNone),
      power_role_(PowerRole::kNone) {
  PortChanged();
  LOG(INFO) << "Port " << port_num_ << " enumerated.";
}

void Port::AddCable(const base::FilePath& path) {
  if (cable_) {
    LOG(WARNING) << "Cable already exists for port " << port_num_;
    return;
  }
  cable_ = std::make_unique<Cable>(path);

  LOG(INFO) << "Cable enumerated for port " << port_num_;
}

void Port::RemoveCable() {
  if (!cable_) {
    LOG(WARNING) << "No partner present for port " << port_num_;
    return;
  }
  cable_.reset();

  LOG(INFO) << "Cable removed for port " << port_num_;
}

void Port::AddCablePlug(const base::FilePath& syspath) {
  if (!cable_) {
    LOG(WARNING) << "No cable present for port " << port_num_;
    return;
  }

  cable_->RegisterCablePlug(syspath);
}

void Port::AddPartner(const base::FilePath& path) {
  if (partner_) {
    LOG(WARNING) << "Partner already exists for port " << port_num_;
    return;
  }
  partner_ = std::make_unique<Partner>(path, this);

  LOG(INFO) << "Partner enumerated for port " << port_num_;
}

void Port::RemovePartner() {
  if (!partner_) {
    LOG(WARNING) << "No partner present for port " << port_num_;
    return;
  }
  partner_.reset();

  // Since a partner is disconnected, we should reset the |metrics_reported_|
  // flag so that metrics can be reported on the next connect.
  metrics_reported_ = false;

  LOG(INFO) << "Partner removed for port " << port_num_;
}

void Port::AddRemovePartnerAltMode(const base::FilePath& path, bool added) {
  if (!partner_) {
    LOG(WARNING) << "Trying to add alt mode for non-existent partner on port "
                 << port_num_;
    return;
  }

  if (added) {
    if (!partner_->AddAltMode(path))
      LOG(ERROR) << "Failed to add alt mode for port " << port_num_
                 << " at path " << path;
  } else {
    partner_->RemoveAltMode(path);
  }
}

void Port::AddCableAltMode(const base::FilePath& path) {
  if (!cable_) {
    LOG(WARNING) << "Trying to add alt mode for non-existent cable on port "
                 << port_num_;
    return;
  }

  if (!cable_->AddAltMode(path)) {
    LOG(ERROR) << "Failed to add SOP' alt mode for port " << port_num_
               << " at path " << path;
  }
}

void Port::PartnerChanged() {
  if (!partner_) {
    LOG(WARNING) << "Trying to update a non-existent partner on port "
                 << port_num_;
    return;
  }

  partner_->UpdatePDInfoFromSysfs();
}

void Port::PortChanged() {
  ParseDataRole();
  ParsePowerRole();
}

DataRole Port::GetDataRole() {
  return data_role_;
}

PowerRole Port::GetPowerRole() {
  return power_role_;
}

bool Port::CanEnterDPAltMode(bool* invalid_dpalt_cable_ptr) {
  bool dp_alt_available = false;
  bool partner_is_receptacle = false;

  for (int i = 0; i < partner_->GetNumAltModes(); i++) {
    auto alt_mode = partner_->GetAltMode(i);
    // Only enter DP if:
    // - The DP SID is found.
    // - The DP altmode VDO says it is DFP_D capable.
    if (!alt_mode || alt_mode->GetSVID() != kDPAltModeSID)
      continue;

    if (alt_mode->GetVDO() & kDPModeSnk)
      dp_alt_available = true;

    if (alt_mode->GetVDO() & kDPModeReceptacle)
      partner_is_receptacle = true;
  }

  // Partner does not support DPAltMode -> partner error
  if (!dp_alt_available)
    return false;

  // If the partner supports DPAltMode and an invalid cable flag is passed to
  // the function, check to see if the cable also supports DPAltMode.
  if (invalid_dpalt_cable_ptr != nullptr) {
    // First assume the cable can support DPAlt mode.
    *invalid_dpalt_cable_ptr = false;

    // Missing cable with partner indicating it is not captive -> cable error.
    if (!cable_ && partner_is_receptacle)
      *invalid_dpalt_cable_ptr = true;

    // Cable exists and partner supports DPAltMode. If cable is usb2 and the
    // partner is a receptacle -> cable error.
    // Otherwise it is a captive cable, and not a cable error.
    auto speed = cable_->GetProductTypeVDO1() & kUSBSpeedBitMask;
    if (speed == kUSBSpeed20 && partner_is_receptacle)
      *invalid_dpalt_cable_ptr = true;
  }

  // Partner supports DPAltMode.
  return true;
}

// Mode entry check for TBT compatibility mode.
// Ref:
//   USB Type-C Connector Spec, release 2.0
//   Figure F-1.
ModeEntryResult Port::CanEnterTBTCompatibilityMode() {
  if (!supports_usb4_) {
    LOG(ERROR) << "TBT Compat  mode not supported on port: " << port_num_;
    return ModeEntryResult::kPortError;
  }

  // Check if the partner supports Modal Operation
  // Ref:
  //   USB PD spec, rev 3.0, v2.0.
  //   Table 6-29
  if (!partner_) {
    LOG(ERROR) << "No partner object registered, can't enter TBT Compat mode.";
    return ModeEntryResult::kPartnerError;
  }

  auto partner_idh = partner_->GetIdHeaderVDO();
  if (!(partner_idh & kIDHeaderVDOModalOperationBitField)) {
    return ModeEntryResult::kPartnerError;
  }

  // Check if the partner supports TBT compatibility mode.
  if (!IsPartnerAltModePresent(kTBTAltModeVID)) {
    LOG(INFO) << "TBT Compat mode not supported by partner.";
    return ModeEntryResult::kPartnerError;
  }

  if (!cable_) {
    LOG(ERROR) << "No cable object registered, can't enter TBT Compat mode.";
    return ModeEntryResult::kCableError;
  }

  // Check if the Cable meets TBT3 speed requirements.
  // NOTE: Since we aren't configuring the TBT3 entry speed, we don't
  // need to check for the existence of TBT3 alt mode in the SOP' discovery.
  if (!cable_->TBT3PDIdentityCheck())
    return ModeEntryResult::kCableError;

  return ModeEntryResult::kSuccess;
}

// Follow the USB4 entry checks as per:
// Figure 5-1: USB4 Discovery and Entry Flow Model
// USB Type-C Cable & Connector Spec Rel 2.0.
ModeEntryResult Port::CanEnterUSB4() {
  if (!supports_usb4_) {
    LOG(ERROR) << "USB4 not supported on port: " << port_num_;
    return ModeEntryResult::kPortError;
  }

  if (!partner_) {
    LOG(ERROR) << "Attempting USB4 entry without a registered partner on port: "
               << port_num_;
    return ModeEntryResult::kPartnerError;
  }

  // Partner doesn't support USB4.
  auto partner_cap =
      (partner_->GetProductTypeVDO1() >> kDeviceCapabilityBitOffset) &
      kDeviceCapabilityMask;
  if (!(partner_cap & kDeviceCapabilityUSB4))
    return ModeEntryResult::kPartnerError;

  if (!cable_) {
    LOG(ERROR) << "Attempting USB4 entry without a registered cable on port: "
               << port_num_;
    return ModeEntryResult::kCableError;
  }

  // Cable checks.
  auto cable_type =
      (cable_->GetIdHeaderVDO() >> kIDHeaderVDOProductTypeBitOffset) &
      kIDHeaderVDOProductTypeMask;
  if (cable_type == kIDHeaderVDOProductTypeCableActive) {
    auto vdo_version =
        (cable_->GetProductTypeVDO1() >> kActiveCableVDO1VDOVersionOffset) &
        kActiveCableVDO1VDOVersionBitMask;

    // For VDO version == 1.3, check if Active Cable VDO2 supports USB4.
    // NOTE: The meaning of this field is inverted; the bit field being set
    // means USB4 is *not* supported.
    if (vdo_version == kActiveCableVDO1VDOVersion13) {
      if (cable_->GetProductTypeVDO2() & kActiveCableVDO2USB4SupportedBitField)
        return ModeEntryResult::kCableError;
      else
        return ModeEntryResult::kSuccess;
    }

    // For VDO version != 1.3, don't enable USB4 if the cable:
    // - doesn't support modal operation, or
    // - doesn't have an Intel SVID Alt mode, or
    // - doesn't have rounded support.
    if (!(cable_->GetIdHeaderVDO() & kIDHeaderVDOModalOperationBitField))
      return ModeEntryResult::kCableError;

    if (!IsCableAltModePresent(kTBTAltModeVID))
      return ModeEntryResult::kCableError;

    // Go through cable alt modes and check for rounded support in the TBT VDO.
    auto num_altmodes = cable_->GetNumAltModes();
    for (int i = 0; i < num_altmodes; i++) {
      AltMode* altmode = cable_->GetAltMode(i);
      if (!altmode || altmode->GetSVID() != kTBTAltModeVID)
        continue;
      auto rounded_support =
          altmode->GetVDO() >> kTBT3CableDiscModeVDORoundedSupportOffset &
          kTBT3CableDiscModeVDORoundedSupportMask;
      if (rounded_support == kTBT3CableDiscModeVDO_3_4_Gen_Rounded_Non_Rounded)
        return ModeEntryResult::kSuccess;
    }

    return ModeEntryResult::kCableError;
  } else if (cable_type == kIDHeaderVDOProductTypeCablePassive) {
    // Apart from USB2.0, USB4 is supported for all other speeds.
    auto speed = cable_->GetProductTypeVDO1() & kUSBSpeedBitMask;
    if (speed != kUSBSpeed20)
      return ModeEntryResult::kSuccess;
    else
      return ModeEntryResult::kCableError;
  }

  LOG(ERROR) << "Invalid cable type: " << cable_type
             << ", USB4 entry aborted on port " << port_num_;

  return ModeEntryResult::kCableError;
}

bool Port::IsPartnerAltModePresent(uint16_t altmode_sid) {
  auto num_alt_modes = partner_->GetNumAltModes();
  for (int i = 0; i < num_alt_modes; i++) {
    AltMode* alt_mode = partner_->GetAltMode(i);
    if (!alt_mode)
      continue;
    if (alt_mode->GetSVID() == altmode_sid)
      return true;
  }

  return false;
}

bool Port::IsPartnerDiscoveryComplete() {
  if (!partner_) {
    LOG(INFO)
        << "Trying to check discovery complete for a non-existent partner.";
    return false;
  }

  return partner_->DiscoveryComplete();
}

bool Port::PartnerSupportsPD() {
  if (!partner_) {
    LOG(INFO) << "Trying to check supports PD for a non-existent partner.";
    return false;
  }

  return partner_->GetSupportsPD();
}

bool Port::IsCableAltModePresent(uint16_t altmode_sid) {
  return cable_->IsAltModeSVIDPresent(altmode_sid);
}

bool Port::IsCableDiscoveryComplete() {
  if (!cable_) {
    LOG(INFO) << "Trying to check discovery complete for a non-existent cable.";
    return false;
  }

  return cable_->DiscoveryComplete();
}

void Port::ParseDataRole() {
  DataRole role = DataRole::kNone;
  std::string role_str;
  std::string sysfs_str;
  auto path = syspath_.Append("data_role");

  if (!base::ReadFileToString(path, &sysfs_str)) {
    LOG(ERROR) << "Couldn't read sysfs path " << path;
    goto end;
  }

  // First check for a dual role port, in which case the current role is in
  // box-brackets. For example: [host] device
  if (!RE2::PartialMatch(sysfs_str, kDualRoleRegex, &role_str)) {
    LOG(INFO)
        << "Couldn't determine role, assuming DRP(Dual Role Port) for port "
        << port_num_;
  }

  if (role_str == "")
    role_str = sysfs_str;

  base::TrimWhitespaceASCII(role_str, base::TRIM_ALL, &role_str);
  if (role_str == "host")
    role = DataRole::kHost;
  else if (role_str == "device")
    role = DataRole::kDevice;

end:
  data_role_ = role;
}

void Port::ParsePowerRole() {
  PowerRole role = PowerRole::kNone;
  std::string role_str;
  std::string sysfs_str;
  auto path = syspath_.Append("power_role");

  if (!base::ReadFileToString(path, &sysfs_str)) {
    LOG(ERROR) << "Couldn't read sysfs path " << path;
    goto end;
  }

  // First check for a dual role port, in which case the current role is in
  // box-brackets. For example: [source] sink
  if (!RE2::PartialMatch(sysfs_str, kDualRoleRegex, &role_str)) {
    LOG(INFO)
        << "Couldn't determine role, assuming DRP(Dual Role Port) for port "
        << port_num_;
  }

  if (role_str == "")
    role_str = sysfs_str;

  base::TrimWhitespaceASCII(role_str, base::TRIM_ALL, &role_str);
  if (role_str == "source")
    role = PowerRole::kSource;
  else if (role_str == "sink")
    role = PowerRole::kSink;

end:
  power_role_ = role;
}

bool Port::CableLimitingUSBSpeed() {
  if (!partner_ || !cable_)
    return false;

  // Check for cable product type.
  auto cable_type =
      (cable_->GetIdHeaderVDO() >> kIDHeaderVDOProductTypeBitOffset) &
      kIDHeaderVDOProductTypeMask;
  if (cable_type != kIDHeaderVDOProductTypeCableActive &&
      cable_type != kIDHeaderVDOProductTypeCablePassive)
    return false;

  // Check for captive cable.
  auto cable_plug_type =
      (cable_->GetProductTypeVDO1() >> kCableVDO1VDOPlugTypeOffset) &
      kCableVDO1VDOPlugTypeBitMask;
  if (cable_plug_type == kCableVDO1VDOPlugTypeCaptive)
    return false;

  // Check for partner product type.
  auto partner_type =
      (partner_->GetIdHeaderVDO() >> kIDHeaderVDOProductTypeBitOffset) &
      kIDHeaderVDOProductTypeMask;
  if (partner_->GetPDRevision() == PDRevision::k20) {
    // PD rev 2.0, v 1.3
    // Table 6-24 Product Types (UFP)
    // Only AMAs use a product type VDO.
    if (partner_type != kIDHeaderVDOProductTypeUFPAMA)
      return false;
  } else if (partner_->GetPDRevision() == PDRevision::k30) {
    // PD rev 3.0, v 2.0
    // Table 6-30 Product Types (UFP)
    // Only PDUSB hubs, PDUSB peripherals and AMAs use a product type VDO with
    // USB speed.
    if (partner_type != kIDHeaderVDOProductTypeUFPHub &&
        partner_type != kIDHeaderVDOProductTypeUFPPeripheral &&
        partner_type != kIDHeaderVDOProductTypeUFPAMA)
      return false;
  } else {
    // Return false on undetermined PD revision.
    return false;
  }

  auto cable_speed = cable_->GetProductTypeVDO1() & kUSBSpeedBitMask;
  auto partner_speed = partner_->GetProductTypeVDO1() & kUSBSpeedBitMask;

  // In USB PD Rev 2.0 and 3.0, 0x3 in the AMA VDO USB Highest speed field
  // represents billboard only, and should not be compared against cable speed.
  if ((partner_->GetPDRevision() == PDRevision::k20 ||
       partner_->GetPDRevision() == PDRevision::k30) &&
      partner_type == kIDHeaderVDOProductTypeUFPAMA &&
      partner_speed == kAMAVDOUSBSpeedBillboard) {
    return false;
  }

  // Check for TBT supporting cables which signal as USB 3.2 Gen2 passive
  // cables in ID Header VDO and Passive Cable VDO, but can support USB4 with
  // TBT3 Gen3 speed.
  // USB Type-C Cable & Connector spec release 2.1
  // Figure 5-1 USB4 Discovery and Entry Flow Model
  if (cable_type == kIDHeaderVDOProductTypeCablePassive) {
    for (int i = 0; i < cable_->GetNumAltModes(); i++) {
      auto alt_mode = cable_->GetAltMode(i);

      if (!alt_mode || alt_mode->GetSVID() != kTBTAltModeVID)
        continue;

      auto cable_tbt_mode =
          (alt_mode->GetVDO() >> kTBT3CableDiscModeVDOModeOffset) &
          kTBT3CableDiscModeVDOModeMask;
      auto cable_tbt_speed =
          (alt_mode->GetVDO() >> kTBT3CableDiscModeVDOSpeedOffset) &
          kTBT3CableDiscModeVDOSpeedMask;

      if (cable_tbt_mode == kTBT3CableDiscModeVDOModeTBT &&
          cable_tbt_speed == kTBT3CableDiscModeVDOSpeed10G20G)
        cable_speed = kUSB40SuperSpeedGen3;

      break;
    }
  }

  return partner_speed > cable_speed;
}

void Port::ReportPartnerMetrics(Metrics* metrics) {
  if (!partner_) {
    LOG(INFO) << "Trying to report metrics for non-existent partner.";
    return;
  }

  partner_->ReportMetrics(metrics);
}

void Port::ReportCableMetrics(Metrics* metrics) {
  if (!cable_) {
    LOG(INFO) << "Trying to report metrics for non-existent cable.";
    return;
  }

  cable_->ReportMetrics(metrics);
}

void Port::ReportPortMetrics(Metrics* metrics) {
  if (!metrics || metrics_reported_)
    return;

  if (!IsCableDiscoveryComplete() || !IsPartnerDiscoveryComplete())
    return;

  // Check cable for tracking DPAltMode cable metrics.
  bool invalid_dpalt_cable = false;
  bool can_enter_dpalt_mode = CanEnterDPAltMode(&invalid_dpalt_cable);

  if (CanEnterUSB4() == ModeEntryResult::kCableError)
    metrics->ReportWrongCableError(WrongConfigurationMetric::kUSB4WrongCable);
  else if (CanEnterTBTCompatibilityMode() == ModeEntryResult::kCableError)
    metrics->ReportWrongCableError(WrongConfigurationMetric::kTBTWrongCable);
  else if (can_enter_dpalt_mode && invalid_dpalt_cable)
    metrics->ReportWrongCableError(WrongConfigurationMetric::kDPAltWrongCable);
  else if (CableLimitingUSBSpeed())
    metrics->ReportWrongCableError(
        WrongConfigurationMetric::kSpeedLimitingCable);

  metrics_reported_ = true;
  return;
}

}  // namespace typecd
