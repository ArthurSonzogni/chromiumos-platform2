// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/partner.h"

#include <string>

#include <base/files/file_enumerator.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "typecd/pd_vdo_constants.h"
#include "typecd/port.h"

namespace {

constexpr char kPartnerAltModeRegex[] = R"(port(\d+)-partner.(\d+))";

}

namespace typecd {

Partner::Partner(const base::FilePath& syspath, Port* port) : Partner(syspath) {
  port_ = port;
}

Partner::Partner(const base::FilePath& syspath)
    : Peripheral(syspath, "Partner"),
      num_alt_modes_(-1),
      supports_pd_(false),
      metrics_reported_(false),
      port_(nullptr) {
  // Search for all alt modes which were already registered prior to daemon
  // init.
  base::FileEnumerator iter(GetSysPath(), false,
                            base::FileEnumerator::DIRECTORIES);
  // This needs to be called explicitly since it's not in the base Peripheral
  // class.
  UpdateSupportsPD();
  for (auto path = iter.Next(); !path.empty(); path = iter.Next())
    AddAltMode(path);

  SetNumAltModes(ParseNumAltModes());
}

bool Partner::AddAltMode(const base::FilePath& mode_syspath) {
  int port, index;
  if (!RE2::FullMatch(mode_syspath.BaseName().value(), kPartnerAltModeRegex,
                      &port, &index))
    return false;

  if (IsAltModePresent(index)) {
    LOG(INFO) << "Alt mode already registered for syspath "
              << mode_syspath.BaseName();
    return true;
  }

  auto alt_mode = AltMode::CreateAltMode(mode_syspath);
  if (!alt_mode) {
    LOG(ERROR) << "Error creating alt mode for syspath " << mode_syspath;
    return false;
  }

  alt_modes_.emplace(index, std::move(alt_mode));

  LOG(INFO) << "Added alt mode for port " << port << " index " << index;

  return true;
}

void Partner::RemoveAltMode(const base::FilePath& mode_syspath) {
  int port, index;
  if (!RE2::FullMatch(mode_syspath.BaseName().value(), kPartnerAltModeRegex,
                      &port, &index)) {
    LOG(ERROR) << "Couldn't parse alt mode index from syspath " << mode_syspath;
    return;
  }

  auto it = alt_modes_.find(index);
  if (it == alt_modes_.end()) {
    LOG(INFO) << "Trying to delete non-existent alt mode " << index;
    return;
  }

  alt_modes_.erase(it);

  LOG(INFO) << "Removed alt mode for port " << port << " index " << index;
}

bool Partner::IsAltModePresent(int index) {
  auto it = alt_modes_.find(index);
  if (it != alt_modes_.end()) {
    return true;
  }

  LOG(INFO) << "Alt mode not found at index " << index;
  return false;
}

void Partner::UpdatePDInfoFromSysfs() {
  if (GetNumAltModes() == -1)
    SetNumAltModes(ParseNumAltModes());
  UpdatePDIdentityVDOs();
  UpdatePDRevision();
  UpdateSupportsPD();
}

int Partner::ParseNumAltModes() {
  auto path = GetSysPath().Append("number_of_alternate_modes");

  std::string val_str;
  if (!base::ReadFileToString(path, &val_str))
    return -1;

  base::TrimWhitespaceASCII(val_str, base::TRIM_TRAILING, &val_str);

  int num_altmodes;
  if (!base::StringToInt(val_str.c_str(), &num_altmodes)) {
    LOG(ERROR) << "Couldn't parse num_altmodes from string: " << val_str;
    return -1;
  }

  return num_altmodes;
}

AltMode* Partner::GetAltMode(int index) {
  if (!IsAltModePresent(index))
    return nullptr;

  return alt_modes_.find(index)->second.get();
}

bool Partner::DiscoveryComplete() {
  return num_alt_modes_ == alt_modes_.size();
}

void Partner::UpdateSupportsPD() {
  auto path = GetSysPath().Append("supports_usb_power_delivery");
  std::string val_str;
  if (!base::ReadFileToString(path, &val_str)) {
    LOG(ERROR) << "Couldn't read value from path " << path;
    return;
  }

  base::TrimWhitespaceASCII(val_str, base::TRIM_TRAILING, &val_str);
  if (val_str == "yes")
    supports_pd_ = true;
  else
    supports_pd_ = false;
}

PartnerTypeMetric Partner::GetPartnerTypeMetric() {
  bool usb4 = false;
  auto partner_cap = (GetProductTypeVDO1() >> kDeviceCapabilityBitOffset) &
                     kDeviceCapabilityMask;
  if (partner_cap & kDeviceCapabilityUSB4) {
    usb4 = true;
  }

  // Check for TBT/DP.
  bool tbt_present = false;
  bool dp_present = false;
  for (const auto& [index, mode] : alt_modes_) {
    if (mode->GetSVID() == kTBTAltModeVID)
      tbt_present = true;

    if ((mode->GetSVID() == kDPAltModeSID) && (mode->GetVDO() & kDPModeSnk))
      dp_present = true;
  }

  bool usb_present = false;
  // For situations where the device is a "regular" USB peripheral, try to
  // determine whether it at least supports anything other than billboard.
  auto product_type = (GetIdHeaderVDO() >> kIDHeaderVDOProductTypeBitOffset) &
                      kIDHeaderVDOProductTypeMask;
  if (product_type == kIDHeaderVDOProductTypeUFPPeripheral ||
      product_type == kIDHeaderVDOProductTypeUFPHub) {
    auto device_cap = (GetProductTypeVDO1() >> kDeviceCapabilityBitOffset) &
                      kDeviceCapabilityMask;
    if (device_cap != kDeviceCapabilityBillboard)
      usb_present = true;
  }

  // Determine whether it is a hub or peripheral.
  bool hub = false;
  bool peripheral = false;
  if (product_type == kIDHeaderVDOProductTypeUFPHub) {
    hub = true;
  } else if (product_type == kIDHeaderVDOProductTypeUFPPeripheral) {
    peripheral = true;
  } else if (product_type == kIDHeaderVDOProductTypeUFPAMA) {
    // If it's an Alternate Mode Adapter, we have to guess.
    // Check the AMA VDO. If only billboard is supported, we guess that it's a
    // peripheral. In all other cases, we consider it's a hub.
    auto usb_speed = GetProductTypeVDO1() & kAMAVDOUSBSpeedBitMask;
    if (usb_speed != kAMAVDOUSBSpeedBillboard)
      hub = true;
    else
      peripheral = true;
  }

  // Now that we have all the data, let's make a type selection.
  PartnerTypeMetric ret = PartnerTypeMetric::kOther;
  if (usb4) {
    if (hub)
      ret = PartnerTypeMetric::kUSB4Hub;
    else if (peripheral)
      ret = PartnerTypeMetric::kUSB4Peripheral;
  } else if (tbt_present && dp_present) {
    if (hub)
      ret = PartnerTypeMetric::kTBTDPAltHub;
    else if (peripheral)
      ret = PartnerTypeMetric::kTBTDPAltPeripheral;
  } else if (tbt_present) {
    if (hub)
      ret = PartnerTypeMetric::kTBTHub;
    else if (peripheral)
      ret = PartnerTypeMetric::kTBTPeripheral;
  } else if (dp_present) {
    if (hub)
      ret = PartnerTypeMetric::kDPAltHub;
    else if (peripheral)
      ret = PartnerTypeMetric::kDPAltPeripheral;
  } else if (usb_present) {
    if (hub)
      ret = PartnerTypeMetric::kUSBHub;
    else if (peripheral)
      ret = PartnerTypeMetric::kUSBPeripheral;
  }

  // If we've found a valid category let's return.
  if (ret != PartnerTypeMetric::kOther)
    return ret;

  // If we still haven't been able to categorize the partner, we make a guess
  // based on current port state and hints about partner capabilities.
  if (!port_) {
    LOG(INFO) << "Port pointer not available; can't determine partner type";
    return ret;
  }

  // We only proceed in this exercise if the partner doesn't have an ID header
  // VDO. Otherwise, it should have been classified some way from the above.
  if (GetIdHeaderVDO() != 0x0) {
    return ret;
  }

  // Grab all the variables together.
  DataRole port_dr = port_->GetDataRole();
  PowerRole port_pr = port_->GetPowerRole();
  bool partner_has_pd = GetSupportsPD();

  // Refer to b/195056095 for details about the selection matrix.
  if (port_pr == PowerRole::kSink) {
    if (partner_has_pd) {
      if (port_dr == DataRole::kHost)
        ret = PartnerTypeMetric::kPDSourcingDevice;
      else if (port_dr == DataRole::kDevice)
        ret = PartnerTypeMetric::kPDPowerSource;
    } else {
      ret = PartnerTypeMetric::kNonPDPowerSource;
    }
  } else if (port_pr == PowerRole::kSource) {
    if (partner_has_pd) {
      if (port_dr == DataRole::kHost)
        ret = PartnerTypeMetric::kPDSink;
      else if (port_dr == DataRole::kDevice)
        ret = PartnerTypeMetric::kPDSinkingHost;
    } else {
      ret = PartnerTypeMetric::kNonPDSink;
    }
  }

  return ret;
}

void Partner::ReportMetrics(Metrics* metrics) {
  if (!metrics || metrics_reported_)
    return;

  metrics->ReportPartnerType(GetPartnerTypeMetric());

  metrics_reported_ = true;
}

}  // namespace typecd
