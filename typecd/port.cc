// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port.h"

#include <vector>

#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_runner.h>
#include <re2/re2.h>

#include "typecd/pd_vdo_constants.h"
#include "typecd/peripheral.h"
#include "typecd/utils.h"

namespace {

constexpr char kDualRoleRegex[] = R"(\[(\w+)\])";

// Give a delay before reporting metrics to finish PD negotiation. Calculated
// as follows:
// nCapsCount (50) * tTypeCSendSourceCap (100ms ~ 200ms)
// which gives 10000ms or 10 sec to officially declare non-PD.
constexpr uint32_t kPDNegotiationDelayMs = 10000;

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
      power_role_(PowerRole::kNone),
      panel_(Panel::kUnknown),
      horizontal_position_(HorizontalPosition::kUnknown),
      vertical_position_(VerticalPosition::kUnknown) {
  PortChanged();
  ParsePhysicalLocation();
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

bool Port::HasPartner() {
  return partner_.get() != nullptr;
}

void Port::AddRemovePartnerAltMode(const base::FilePath& path, bool added) {
  if (!partner_) {
    LOG(WARNING) << "Trying to add alt mode for non-existent partner on port "
                 << port_num_;
    return;
  }

  if (added) {
    if (!partner_->AddAltMode(path)) {
      LOG(ERROR) << "Failed to add alt mode for port " << port_num_
                 << " at path " << path;
    }
  } else {
    partner_->RemoveAltMode(path);
  }
}

void Port::AddRemovePartnerPowerProfile(bool added) {
  if (!partner_) {
    LOG(WARNING)
        << "Trying to modify power profile for non-existent partner on port "
        << port_num_;
    return;
  }

  if (added) {
    partner_->AddPowerProfile();
  } else {
    partner_->RemovePowerProfile();
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

void Port::CableChanged() {
  if (!cable_) {
    LOG(WARNING) << "Trying to update a non-existent cable on port "
                 << port_num_;
    return;
  }

  cable_->UpdatePDInfoFromSysfs();
}

void Port::CablePlugChanged(const base::FilePath& path) {
  if (!cable_) {
    LOG(WARNING) << "Trying to update a non-existent cable plug on port "
                 << port_num_;
    return;
  }

  cable_->UpdateNumAltModes(path);
}

void Port::PortChanged() {
  ParseDataRole();
  ParsePowerRole();
}

std::vector<AltMode*> Port::GetAltModes(uint32_t recipient) {
  switch (recipient) {
    case ((uint32_t)Recipient::kPartner):
      if (partner_) {
        return partner_->GetAltModes();
      }
      break;
    case ((uint32_t)Recipient::kCable):
      if (cable_) {
        return cable_->GetAltModes();
      }
      break;
    default:
      break;
  }
  return {};
}

std::vector<uint32_t> Port::GetIdentity(uint32_t recipient) {
  switch (recipient) {
    case ((uint32_t)Recipient::kPartner):
      if (partner_) {
        return partner_->GetIdentity();
      }
      break;
    case ((uint32_t)Recipient::kCable):
      if (cable_) {
        return cable_->GetIdentity();
      }
      break;
    default:
      break;
  }
  return {};
}

PDRevision Port::GetPDRevision(uint32_t recipient) {
  switch (recipient) {
    case ((uint32_t)Recipient::kPartner):
      if (partner_) {
        return partner_->GetPDRevision();
      }
      break;
    case ((uint32_t)Recipient::kCable):
      if (cable_) {
        return cable_->GetPDRevision();
      }
      break;
    default:
      break;
  }
  return PDRevision::kNone;
}

DataRole Port::GetDataRole() {
  return data_role_;
}

PowerRole Port::GetPowerRole() {
  return power_role_;
}

Panel Port::GetPanel() {
  return panel_;
}

HorizontalPosition Port::GetHorizontalPosition() {
  return horizontal_position_;
}

VerticalPosition Port::GetVerticalPosition() {
  return vertical_position_;
}

bool Port::CanEnterDPAltMode(bool* invalid_dpalt_cable_ptr) {
  if (!partner_->SupportsDp()) {
    return false;
  }

  bool partner_is_receptacle = false;
  for (int i = 0; i < partner_->GetNumAltModes(); i++) {
    auto alt_mode = partner_->GetAltMode(i);
    if (!alt_mode || alt_mode->GetSVID() != kDPAltModeSID) {
      continue;
    }

    if (alt_mode->GetVDO() & kDPModeReceptacle) {
      partner_is_receptacle = true;
    }
  }

  // Partner supports DPAltMode. Clear the invalid_dpalt_cable_ptr flag to
  // assume the cable can support driving displays.
  if (invalid_dpalt_cable_ptr != nullptr) {
    *invalid_dpalt_cable_ptr = false;
  }

  // Only check the cable if the partner is a receptacle.
  if (partner_is_receptacle) {
    // Non-emarked cables will not prevent mode entry, but may not support
    // displays. Return true, but set the invalid_dpalt_cable_ptr flag to
    // warn the user.
    if (!cable_ || !(cable_->GetIdHeaderVDO())) {
      if (invalid_dpalt_cable_ptr != nullptr) {
        *invalid_dpalt_cable_ptr = true;
      }

      return true;
    }

    // If the cable is emarked, check it's identity to determine mode entry
    // support. If the idenity check fails, prevent mode entry and set the
    // invalid_dpalt_cable_ptr flag to warn the user.
    if (!cable_->DPPDIdentityCheck()) {
      if (invalid_dpalt_cable_ptr != nullptr) {
        *invalid_dpalt_cable_ptr = true;
      }

      return false;
    }
  }

  // Partner and cable support DPAltMode entry.
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
  if (!partner_->SupportsTbt()) {
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
  if (!cable_->TBT3PDIdentityCheck()) {
    return ModeEntryResult::kCableError;
  }

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

  if (!partner_->SupportsUsb4()) {
    return ModeEntryResult::kPartnerError;
  }

  if (!cable_) {
    LOG(ERROR) << "Attempting USB4 entry without a registered cable on port: "
               << port_num_;
    return ModeEntryResult::kCableError;
  }

  // Check if the cable meets USB4 requirements.
  if (!cable_->USB4PDIdentityCheck()) {
    LOG(ERROR) << "Cable does not support USB4 entry on port: " << port_num_;
    return ModeEntryResult::kCableError;
  }

  return ModeEntryResult::kSuccess;
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

  if (role_str == "") {
    role_str = sysfs_str;
  }

  base::TrimWhitespaceASCII(role_str, base::TRIM_ALL, &role_str);
  if (role_str == "host") {
    role = DataRole::kHost;
  } else if (role_str == "device") {
    role = DataRole::kDevice;
  }

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

  if (role_str == "") {
    role_str = sysfs_str;
  }

  base::TrimWhitespaceASCII(role_str, base::TRIM_ALL, &role_str);
  if (role_str == "source") {
    role = PowerRole::kSource;
  } else if (role_str == "sink") {
    role = PowerRole::kSink;
  }

end:
  power_role_ = role;
}

void Port::ParsePhysicalLocation() {
  Panel panel = Panel::kUnknown;
  HorizontalPosition hpos = HorizontalPosition::kUnknown;
  VerticalPosition vpos = VerticalPosition::kUnknown;

  std::string panel_str, hpos_str, vpos_str;
  auto panel_path = syspath_.Append("physical_location/panel");
  auto hpos_path = syspath_.Append("physical_location/horizontal_position");
  auto vpos_path = syspath_.Append("physical_location/vertical_position");

  if (!base::ReadFileToString(panel_path, &panel_str) ||
      !base::ReadFileToString(hpos_path, &hpos_str) ||
      !base::ReadFileToString(vpos_path, &vpos_str)) {
    // No error logged since kernel v5.4 or older does not expose
    // physical_location to sysfs.
    goto end;
  }

  base::TrimWhitespaceASCII(panel_str, base::TRIM_ALL, &panel_str);
  if (panel_str == "top") {
    panel = Panel::kTop;
  } else if (panel_str == "bottom") {
    panel = Panel::kBottom;
  } else if (panel_str == "left") {
    panel = Panel::kLeft;
  } else if (panel_str == "right") {
    panel = Panel::kRight;
  } else if (panel_str == "front") {
    panel = Panel::kFront;
  } else if (panel_str == "back") {
    panel = Panel::kBack;
  }

  base::TrimWhitespaceASCII(hpos_str, base::TRIM_ALL, &hpos_str);
  if (hpos_str == "left") {
    hpos = HorizontalPosition::kLeft;
  } else if (hpos_str == "center") {
    hpos = HorizontalPosition::kCenter;
  } else if (hpos_str == "right") {
    hpos = HorizontalPosition::kRight;
  }

  base::TrimWhitespaceASCII(vpos_str, base::TRIM_ALL, &vpos_str);
  if (vpos_str == "upper") {
    vpos = VerticalPosition::kUpper;
  } else if (vpos_str == "center") {
    vpos = VerticalPosition::kCenter;
  } else if (vpos_str == "lower") {
    vpos = VerticalPosition::kLower;
  }

end:
  panel_ = panel;
  horizontal_position_ = hpos;
  vertical_position_ = vpos;
}

bool Port::CableLimitingUSBSpeed(bool tbt3_alt_mode) {
  if (!partner_ || !cable_) {
    return false;
  }

  // Initialize cable speeds from USB PD identity response.
  auto cable_speed = cable_->GetProductTypeVDO1() & kUSBSpeedBitMask;
  auto partner_speed = partner_->GetProductTypeVDO1() & kUSBSpeedBitMask;

  // Check for cable product type.
  auto cable_type =
      (cable_->GetIdHeaderVDO() >> kIDHeaderVDOProductTypeBitOffset) &
      kIDHeaderVDOProductTypeMask;
  if (cable_type != kIDHeaderVDOProductTypeCableActive &&
      cable_type != kIDHeaderVDOProductTypeCablePassive) {
    return false;
  }

  // Check for captive cable.
  if (IsCaptiveCableConnected()) {
    return false;
  }

  // In Thunderbolt 3 alternate mode, the partner will support 40 Gbps.
  // Otherwise, check partner type to confirm product_type_vdo1 speed is
  // accurate.
  if (tbt3_alt_mode) {
    partner_speed = kUSB40SuperSpeedGen3;
  } else {
    // Check for partner product type and PD revision.
    auto partner_type =
        (partner_->GetIdHeaderVDO() >> kIDHeaderVDOProductTypeBitOffset) &
        kIDHeaderVDOProductTypeMask;
    auto partner_pd_revision = partner_->GetPDRevision();
    if (partner_pd_revision == PDRevision::k20) {
      // PD rev 2.0, v 1.3
      // Table 6-24 Product Types (UFP)
      // Only AMAs use a product type VDO.
      if (partner_type != kIDHeaderVDOProductTypeUFPAMA) {
        return false;
      }
    } else if (partner_pd_revision == PDRevision::k30) {
      // PD rev 3.0, v 2.0
      // Table 6-30 Product Types (UFP)
      // Only PDUSB hubs, PDUSB peripherals and AMAs use a product type VDO with
      // USB speed.
      if (partner_type != kIDHeaderVDOProductTypeUFPHub &&
          partner_type != kIDHeaderVDOProductTypeUFPPeripheral &&
          partner_type != kIDHeaderVDOProductTypeUFPAMA) {
        return false;
      }
    } else if (partner_pd_revision == PDRevision::k31 ||
               partner_pd_revision == PDRevision::k32) {
      // PD rev 3.1, v1.8 Table 6-36 Product Types (UFP)
      // PD rev 3.2, v1.0 Table 6-35 Product Types (UFP)
      // Only PDUSB hubs, PDUSB peripherals use a product type VDO with
      // USB speed.
      if (partner_type != kIDHeaderVDOProductTypeUFPHub &&
          partner_type != kIDHeaderVDOProductTypeUFPPeripheral) {
        return false;
      }
    } else {
      // Return false on undetermined PD revision.
      return false;
    }

    // In USB PD Rev 2.0 and 3.0, 0x3 in the AMA VDO USB Highest speed field
    // represents billboard only, and should not be compared against cable
    // speed.
    if ((partner_pd_revision == PDRevision::k20 ||
         partner_pd_revision == PDRevision::k30) &&
        partner_type == kIDHeaderVDOProductTypeUFPAMA &&
        partner_speed == kAMAVDOUSBSpeedBillboard) {
      return false;
    }
  }

  // Check for TBT3 cables supporting USB4 speeds.
  // USB Type-C Cable & Connector spec release 2.1
  // Figure 5-1 USB4 Discovery and Entry Flow Model (passive cables)
  // Section 5.4.3.2 (active cables)
  for (int i = 0; i < cable_->GetNumAltModes(); i++) {
    auto alt_mode = cable_->GetAltMode(i);

    if (!alt_mode || alt_mode->GetSVID() != kTBTAltModeVID) {
      continue;
    }

    // Return false after finding TBT3 cable in TBT3 mode.
    if (tbt3_alt_mode) {
      return false;
    }

    auto cable_tbt_mode =
        (alt_mode->GetVDO() >> kTBT3CableDiscModeVDOModeOffset) &
        kTBT3CableDiscModeVDOModeMask;
    auto cable_tbt_speed =
        (alt_mode->GetVDO() >> kTBT3CableDiscModeVDOSpeedOffset) &
        kTBT3CableDiscModeVDOSpeedMask;
    auto cable_tbt_rounded_support =
        (alt_mode->GetVDO() >> kTBT3CableDiscModeVDORoundedSupportOffset) &
        kTBT3CableDiscModeVDORoundedSupportMask;

    if (cable_tbt_mode == kTBT3CableDiscModeVDOModeTBT &&
        cable_tbt_speed == kTBT3CableDiscModeVDOSpeed10G20G) {
      if (cable_type == kIDHeaderVDOProductTypeCablePassive) {
        cable_speed = kUSB40SuperSpeedGen4;
      } else if (cable_type == kIDHeaderVDOProductTypeCableActive &&
                 cable_tbt_rounded_support ==
                     kTBT3CableDiscModeVDO_3_4_Gen_Rounded_Non_Rounded) {
        cable_speed = kUSB40SuperSpeedGen3;
      }
    }

    break;
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

void Port::ReportCableMetrics(Metrics* metrics, bool captive) {
  if (!cable_) {
    LOG(INFO) << "Trying to report metrics for non-existent cable.";
    return;
  }

  cable_->ReportMetrics(metrics, captive);
}

void Port::ReportPortMetrics(Metrics* metrics) {
  if (!metrics || metrics_reported_) {
    return;
  }

  if (!IsCableDiscoveryComplete() || !IsPartnerDiscoveryComplete()) {
    return;
  }

  // Check cable for tracking DPAltMode cable metrics.
  bool invalid_dpalt_cable = false;
  bool can_enter_dpalt_mode = CanEnterDPAltMode(&invalid_dpalt_cable);
  bool cable_limiting_speed_mode =
      CanEnterUSB4() != ModeEntryResult::kSuccess &&
      CanEnterTBTCompatibilityMode() == ModeEntryResult::kSuccess;

  if (CanEnterUSB4() == ModeEntryResult::kCableError) {
    metrics->ReportWrongCableError(WrongConfigurationMetric::kUSB4WrongCable);
  } else if (CanEnterTBTCompatibilityMode() == ModeEntryResult::kCableError) {
    metrics->ReportWrongCableError(WrongConfigurationMetric::kTBTWrongCable);
  } else if (can_enter_dpalt_mode && invalid_dpalt_cable) {
    metrics->ReportWrongCableError(WrongConfigurationMetric::kDPAltWrongCable);
  } else if (CableLimitingUSBSpeed(cable_limiting_speed_mode)) {
    metrics->ReportWrongCableError(
        WrongConfigurationMetric::kSpeedLimitingCable);
  } else {
    metrics->ReportWrongCableError(WrongConfigurationMetric::kNone);
  }

  metrics_reported_ = true;
  return;
}

bool Port::GetDpEntryState(DpSuccessMetric& result) {
  bool hpd;
  if (!ec_util_->HpdState(port_num_, &hpd)) {
    return false;
  }

  bool dp;
  if (!ec_util_->DpState(port_num_, &dp)) {
    return false;
  }

  if (dp) {
    if (hpd) {
      result = DpSuccessMetric::kSuccessHpd;
    } else {
      result = DpSuccessMetric::kSuccessNoHpd;
    }
  } else {
    result = DpSuccessMetric::kFail;
  }

  return true;
}

bool Port::GetModeEntryResult(ModeEntryMetric& result,
                              bool mode_entry_supported) {
  result = ModeEntryMetric::kUnknown;

  if (!mode_entry_supported) {
    result = ModeEntryMetric::kModeEntryUnsupported;
    return true;
  }

  switch (current_mode_) {
    case TypeCMode::kNone:
      result = ModeEntryMetric::kModeEntryNotAttempted;
      break;
    case TypeCMode::kDP:
      bool dp;
      if (!ec_util_->DpState(port_num_, &dp)) {
        return false;
      }

      if (dp) {
        result = ModeEntryMetric::kDpSuccess;
      } else {
        result = ModeEntryMetric::kDpFailure;
      }

      break;
    case TypeCMode::kTBT:
      if (GetTbtDeviceCount() > tbt_device_count_) {
        result = ModeEntryMetric::kTbtSuccess;
      } else {
        result = ModeEntryMetric::kTbtFailure;
      }

      break;
    case TypeCMode::kUSB4:
      if (GetTbtDeviceCount() > tbt_device_count_) {
        result = ModeEntryMetric::kUsb4Success;
      } else {
        result = ModeEntryMetric::kUsb4Failure;
      }

      break;
    default:
      break;
  }

  return true;
}

void Port::ReportDpMetric(Metrics* metrics) {
  DpSuccessMetric result;
  if (!GetDpEntryState(result)) {
    return;
  }
  metrics->ReportDpSuccess(result);
}

void Port::ReportQualityMetrics(Metrics* metrics, bool mode_entry_supported) {
  if (!metrics) {
    return;
  }

  std::string boot_id;
  if (!base::ReadFileToString(base::FilePath("/proc/sys/kernel/random/boot_id"),
                              &boot_id)) {
    boot_id = "";
  }
  base::TrimWhitespaceASCII(boot_id, base::TRIM_ALL, &boot_id);

  int vid = 0;
  int pid = 0;
  std::string usb2_id = "";
  std::string usb3_id = "";
  PartnerTypeMetric partner_type = PartnerTypeMetric::kOther;
  if (partner_) {
    vid = partner_->GetVendorId();
    pid = partner_->GetProductId();
    if (!DeviceInMetricsAllowlist(vid, pid)) {
      vid = 0;
      pid = 0;
    }

    partner_type = partner_->GetPartnerTypeMetric();
    base::FilePath usb2_device, usb3_device;
    if (!boot_id.empty() && partner_->GetUsbDevice(0, 480, &usb2_device)) {
      usb2_id = GetConnectionId(boot_id, usb2_device);
    }

    if (!boot_id.empty() && partner_->GetUsbDevice(5000, 20000, &usb3_device)) {
      usb3_id = GetConnectionId(boot_id, usb3_device);
    }
  }

  CableSpeedMetric cable_speed = CableSpeedMetric::kOther;
  if (cable_) {
    cable_speed = cable_->GetCableSpeedMetric(IsCaptiveCableConnected());
  }

  ModeEntryMetric mode_entry;
  GetModeEntryResult(mode_entry, mode_entry_supported);

  metrics->ReportModeEntry(mode_entry);
  metrics->ReportPdConnect(boot_id, usb2_id, usb3_id, vid, pid, partner_type,
                           cable_speed, mode_entry);
}

void Port::ReportMetrics(Metrics* metrics, bool mode_entry_supported) {
  if (!metrics) {
    return;
  }

  ReportPartnerMetrics(metrics);
  ReportCableMetrics(metrics, IsCaptiveCableConnected());
  ReportQualityMetrics(metrics, mode_entry_supported);
  if (mode_entry_supported) {
    ReportPortMetrics(metrics);
  }
  if (CanEnterDPAltMode(nullptr)) {
    ReportDpMetric(metrics);
  }
}

void Port::EnqueueMetricsTask(Metrics* metrics, bool mode_entry_supported) {
  report_metrics_callback_.Reset(base::BindOnce(&Port::ReportMetrics,
                                                base::Unretained(this), metrics,
                                                mode_entry_supported));
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, report_metrics_callback_.callback(),
      base::Milliseconds(kPDNegotiationDelayMs));
}

void Port::CancelMetricsTask() {
  report_metrics_callback_.Cancel();
}

bool Port::IsCaptiveCableConnected() {
  if (!partner_) {
    return false;
  }

  // If cable enumerated, check cable plug type.
  if (cable_) {
    auto cable_plug_type =
        (cable_->GetProductTypeVDO1() >> kCableVDO1VDOPlugTypeOffset) &
        kCableVDO1VDOPlugTypeBitMask;
    if (cable_plug_type == kCableVDO1VDOPlugTypeCaptive) {
      return true;
    }
  }

  // Check partner ID Header VDO connector type.
  auto partner_connector_type =
      (partner_->GetIdHeaderVDO() >> kIDHeaderVDOConnectorTypeBitOffset) &
      kIDHeaderVDOConnectorTypeMask;
  if (partner_connector_type == kIDHeaderVDOConnectorTypePlug) {
    return true;
  }

  // Check partners DP alt mode connector type.
  if (partner_->SupportsDp()) {
    for (int i = 0; i < partner_->GetNumAltModes(); i++) {
      auto alt_mode = partner_->GetAltMode(i);
      if (!alt_mode || alt_mode->GetSVID() != kDPAltModeSID) {
        continue;
      }

      if (!(alt_mode->GetVDO() & kDPModeReceptacle)) {
        return true;
      }
    }
  }

  return false;
}

}  // namespace typecd
