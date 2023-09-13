// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/cellular_controller.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/contains.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>

#include "power_manager/common/prefs.h"

namespace {
#if USE_CELLULAR
power_manager::CellularRegulatoryDomain GetRegulatoryDomainFromCountryCode(
    const std::string& country_code) {
  // Regulatory domain to country code mappings
  const struct {
    const std::string country_code;
    power_manager::CellularRegulatoryDomain domain;
  } kRdCcMappings[] = {
      {"US,IN", power_manager::CellularRegulatoryDomain::FCC},
      {"CA", power_manager::CellularRegulatoryDomain::ISED},
      {"CN,GB,FR,ES,IT,SE,DE,AT,BE,BA,BG,HR,CY,CZ,DK,EE,FI,FR,GF,GE,GI,GR,VA,"
       "HU,IE,LV,LT,LU,MT,GP,MC,ME,NL,NC,PL,PT,RE,RO,SM,ST,SK,SI,WF",
       power_manager::CellularRegulatoryDomain::CE},
      {"JP", power_manager::CellularRegulatoryDomain::MIC},
      {"KR", power_manager::CellularRegulatoryDomain::KCC}};
  std::string cc = base::ToUpperASCII(country_code);
  for (const auto& m : kRdCcMappings) {
    const auto country_code = base::SplitString(
        m.country_code, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    if (base::Contains(country_code, cc))
      return m.domain;
  }
  return power_manager::CellularRegulatoryDomain::UNKNOWN;
}
#endif

power_manager::CellularRegulatoryDomain GetRegulatoryDomainFromString(
    const std::string& name) {
  if (name == "FCC")
    return power_manager::CellularRegulatoryDomain::FCC;
  else if (name == "ISED")
    return power_manager::CellularRegulatoryDomain::ISED;
  else if (name == "CE")
    return power_manager::CellularRegulatoryDomain::CE;
  else if (name == "MIC")
    return power_manager::CellularRegulatoryDomain::MIC;
  else if (name == "KCC")
    return power_manager::CellularRegulatoryDomain::KCC;
  else
    return power_manager::CellularRegulatoryDomain::UNKNOWN;
}
}  // namespace
namespace power_manager::policy {

CellularController::CellularController() : weak_ptr_factory_(this) {}

CellularController::~CellularController() {}

void CellularController::Init(Delegate* delegate,
                              PrefsInterface* prefs,
                              system::DBusWrapperInterface* dbus_wrapper) {
  DCHECK(delegate);
  DCHECK(prefs);

  delegate_ = delegate;
  dbus_wrapper_ = dbus_wrapper;

  prefs->GetBool(kSetCellularTransmitPowerForTabletModePref,
                 &set_transmit_power_for_tablet_mode_);
  prefs->GetBool(kSetCellularTransmitPowerForProximityPref,
                 &set_transmit_power_for_proximity_);
  prefs->GetInt64(kSetCellularTransmitPowerDprGpioPref, &dpr_gpio_number_);
  prefs->GetBool(kUseModemManagerForDynamicSARPref,
                 &use_modemmanager_for_dynamic_sar_);
  prefs->GetBool(kUseMultiPowerLevelDynamicSARPref,
                 &use_multi_power_level_dynamic_sar_);
  prefs->GetBool(kUseRegulatoryDomainForDynamicSARPref,
                 &use_regulatory_domain_for_dynamic_sar_);
  prefs->GetBool(kSetDefaultProximityStateHighPref,
                 &set_default_proximity_state_far_);
  std::string levels_string;
  if (prefs->GetString(kSetCellularTransmitPowerLevelMappingPref,
                       &levels_string)) {
    base::TrimWhitespaceASCII(levels_string, base::TRIM_TRAILING,
                              &levels_string);
  }
  InitPowerLevel(levels_string);
  std::string regulatory_domain_string;
  if (prefs->GetString(kSetCellularRegulatoryDomainMappingPref,
                       &regulatory_domain_string)) {
    base::TrimWhitespaceASCII(regulatory_domain_string, base::TRIM_TRAILING,
                              &regulatory_domain_string);
  }
  InitRegulatoryDomainMapping(regulatory_domain_string);

  LOG(INFO)
      << "In CellularController::Init set_transmit_power_for_proximity_ = "
      << set_transmit_power_for_proximity_
      << " set_transmit_power_for_tablet_mode_ = "
      << set_transmit_power_for_tablet_mode_
      << " use_modemmanager_for_dynamic_sar_ = "
      << use_modemmanager_for_dynamic_sar_
      << " use_multi_power_level_dynamic_sar_ = "
      << use_multi_power_level_dynamic_sar_
      << " use_regulatory_domain_for_dynamic_sar_ = "
      << use_regulatory_domain_for_dynamic_sar_;

#if USE_CELLULAR
  if (use_modemmanager_for_dynamic_sar_) {
    InitModemManagerSarInterface();
    InitShillProxyInterface();
    return;
  }
#endif  // USE_CELLULAR

  if (set_transmit_power_for_proximity_ || set_transmit_power_for_tablet_mode_)
    CHECK_GE(dpr_gpio_number_, 0) << "DPR GPIO is unspecified or invalid";
}

void CellularController::InitPowerLevel(const std::string& power_levels) {
  if (power_levels.empty()) {
    if (use_multi_power_level_dynamic_sar_) {
      level_mappings_ = {{RadioTransmitPower::HIGH, 2},
                         {RadioTransmitPower::MEDIUM, 1},
                         {RadioTransmitPower::LOW, 0}};
    } else {
      level_mappings_ = {{RadioTransmitPower::HIGH, 0},
                         {RadioTransmitPower::LOW, 1}};
    }
    return;
  }

  base::StringPairs pairs;
  if (!base::SplitStringIntoKeyValuePairs(power_levels, ' ', '\n', &pairs))
    LOG(FATAL) << "Failed parsing " << kSetCellularTransmitPowerLevelMappingPref
               << " pref";
  for (const auto& pair : pairs) {
    const RadioTransmitPower power = GetPowerIndexFromString(pair.first);
    uint32_t level;
    if (power == RadioTransmitPower::UNSPECIFIED ||
        !base::StringToUint(pair.second, &level)) {
      LOG(FATAL) << "Unrecognized power level \"" << pair.first << "\" for \""
                 << pair.second << "\" in "
                 << kSetCellularTransmitPowerLevelMappingPref << " pref";
    }
    if (!level_mappings_.insert(std::make_pair(power, level)).second) {
      LOG(FATAL) << "Duplicate entry for \""
                 << RadioTransmitPowerToString(power) << "\" in "
                 << kSetCellularTransmitPowerLevelMappingPref << " pref";
    }
    LOG(INFO) << "power = " << RadioTransmitPowerToString(power)
              << " level = " << level;
  }
}

void CellularController::InitRegulatoryDomainMapping(
    const std::string& domain_offsets) {
  if (domain_offsets.empty()) {
    regulatory_domain_mappings_ = {{CellularRegulatoryDomain::FCC, 0},
                                   {CellularRegulatoryDomain::ISED, 0},
                                   {CellularRegulatoryDomain::CE, 0},
                                   {CellularRegulatoryDomain::MIC, 0},
                                   {CellularRegulatoryDomain::KCC, 0}};
    return;
  }
  base::StringPairs pairs;
  if (!base::SplitStringIntoKeyValuePairs(domain_offsets, ' ', '\n', &pairs))
    LOG(FATAL) << "Failed parsing " << kSetCellularTransmitPowerLevelMappingPref
               << " pref";
  for (const auto& pair : pairs) {
    const CellularRegulatoryDomain domain =
        GetRegulatoryDomainFromString(pair.first);
    uint32_t offset;
    if (domain == CellularRegulatoryDomain::UNKNOWN ||
        !base::StringToUint(pair.second, &offset)) {
      LOG(FATAL) << "Unrecognized Regulatory Domain \"" << pair.first
                 << "\" for \"" << pair.second << "\" in "
                 << kSetCellularRegulatoryDomainMappingPref << " pref";
    }
    if (!regulatory_domain_mappings_.insert(std::make_pair(domain, offset))
             .second) {
      LOG(FATAL) << "Duplicate entry for \"" << RegulatoryDomainToString(domain)
                 << "\" in " << kSetCellularRegulatoryDomainMappingPref
                 << " pref";
    }
    LOG(INFO) << "domain = " << RegulatoryDomainToString(domain)
              << " offset = " << offset;
  }
}

RadioTransmitPower CellularController::GetPowerIndexFromString(
    const std::string& name) {
  if (name == "HIGH")
    return RadioTransmitPower::HIGH;
  else if (name == "MEDIUM")
    return RadioTransmitPower::MEDIUM;
  else if (name == "LOW")
    return RadioTransmitPower::LOW;
  else
    return RadioTransmitPower::UNSPECIFIED;
}

void CellularController::ProximitySensorDetected(UserProximity value) {
  if (set_transmit_power_for_proximity_) {
    if (set_transmit_power_for_tablet_mode_) {
      LOG(INFO) << "Cellular power will be handled by proximity sensor and "
                   "tablet mode";
    } else {
      LOG(INFO) << "Cellular power will be handled by proximity sensor";
    }
    HandleProximityChange(set_default_proximity_state_far_ ? UserProximity::FAR
                                                           : value);
  }
}

void CellularController::HandleTabletModeChange(TabletMode mode) {
  if (!set_transmit_power_for_tablet_mode_)
    return;

  if (tablet_mode_ == mode)
    return;

  tablet_mode_ = mode;
  UpdateTransmitPower();
}

void CellularController::HandleProximityChange(UserProximity proximity) {
  if (!set_transmit_power_for_proximity_)
    return;

  if (proximity_ == proximity)
    return;

  proximity_ = proximity;
  UpdateTransmitPower();
}

void CellularController::HandleModemStateChange(ModemState state) {
  if (!set_transmit_power_for_proximity_ &&
      !set_transmit_power_for_tablet_mode_)
    return;

  if (state_ == state)
    return;

  state_ = state;
  UpdateTransmitPower();
}

void CellularController::HandleModemRegulatoryDomainChange(
    CellularRegulatoryDomain domain) {
  VLOG(1) << __func__ << " New domain : " << RegulatoryDomainToString(domain)
          << " current domain : "
          << RegulatoryDomainToString(regulatory_domain_);
  if (!use_regulatory_domain_for_dynamic_sar_)
    return;

  if (regulatory_domain_ == domain)
    return;

  regulatory_domain_ = domain;
  UpdateTransmitPower();
}

/*
 * The algorithm chosen is - as always - a conservative one where all inputs
 * need to be in "HIGH-allowed" mode (FAR for proximity, OFF for tablet mode)
 * in order to allow HIGH power to be selected.
 */
RadioTransmitPower CellularController::DetermineTransmitPower() const {
  RadioTransmitPower proximity_power = RadioTransmitPower::UNSPECIFIED;
  RadioTransmitPower tablet_mode_power = RadioTransmitPower::UNSPECIFIED;

  if (set_transmit_power_for_proximity_) {
    switch (proximity_) {
      case UserProximity::UNKNOWN:
        break;
      case UserProximity::NEAR:
        proximity_power = RadioTransmitPower::LOW;
        break;
      case UserProximity::FAR:
        proximity_power = RadioTransmitPower::HIGH;
        break;
    }
  }

  if (set_transmit_power_for_tablet_mode_) {
    switch (tablet_mode_) {
      case TabletMode::UNSUPPORTED:
        break;
      case TabletMode::ON:
        tablet_mode_power = RadioTransmitPower::LOW;
        break;
      case TabletMode::OFF:
        tablet_mode_power = RadioTransmitPower::HIGH;
        break;
    }
  }

  if (use_multi_power_level_dynamic_sar_) {
    if (proximity_power == RadioTransmitPower::LOW &&
        tablet_mode_power == RadioTransmitPower::LOW) {
      return RadioTransmitPower::LOW;
    }
    if (proximity_power == RadioTransmitPower::LOW &&
        tablet_mode_power == RadioTransmitPower::HIGH) {
      return RadioTransmitPower::MEDIUM;
    }
  } else {
    if (proximity_power == RadioTransmitPower::LOW ||
        tablet_mode_power == RadioTransmitPower::LOW) {
      return RadioTransmitPower::LOW;
    }
  }

  return RadioTransmitPower::HIGH;
}

void CellularController::UpdateTransmitPower() {
  RadioTransmitPower wanted_power = DetermineTransmitPower();
#if USE_CELLULAR
  if (use_modemmanager_for_dynamic_sar_)
    SetCellularTransmitPowerInModemManager(wanted_power);
  else
#endif  // USE_CELLULAR
    delegate_->SetCellularTransmitPower(wanted_power, dpr_gpio_number_);
}

#if USE_CELLULAR
void CellularController::SetCellularTransmitPowerInModemManager(
    RadioTransmitPower power) {
  brillo::ErrorPtr error;
  uint32_t offset = 0;
  if (!mm_sar_proxy_) {
    LOG(ERROR) << __func__ << " called before SAR interface is up";
    return;
  }
  auto power_it = level_mappings_.find(power);
  if (power_it == level_mappings_.end()) {
    LOG(ERROR) << "Failed to get SAR table index for power = "
               << RadioTransmitPowerToString(power);
    return;
  }

  if (use_regulatory_domain_for_dynamic_sar_) {
    auto domain_it = regulatory_domain_mappings_.find(regulatory_domain_);
    // If no domain mapping info is present then use the default value (which is
    // 0)
    offset = (domain_it == regulatory_domain_mappings_.end())
                 ? 0
                 : domain_it->second;
  }

  LOG(INFO) << "Setting cellular transmit power level to "
            << RadioTransmitPowerToString(power)
            << " Table index = " << power_it->second << " Offset = " << offset;
  if (!mm_sar_proxy_->SetPowerLevel(power_it->second + offset, &error)) {
    LOG(ERROR) << "Failed to Set SAR Power Level in modem: "
               << error->GetMessage();
  }
}

void CellularController::ModemManagerInterfacesAdded(
    const dbus::ObjectPath& object_path,
    const system::DBusInterfaceToProperties& properties) {
  brillo::ErrorPtr error;
  VLOG(1) << __func__ << ": " << object_path.value();
  if (!base::Contains(properties, modemmanager::kModemManager1SarInterface)) {
    VLOG(1) << __func__ << "Interfaces added, but not modem sar interface.";
    return;
  }
  mm_sar_proxy_ =
      std::make_unique<org::freedesktop::ModemManager1::Modem::SarProxy>(
          dbus_wrapper_->GetBus(), modemmanager::kModemManager1ServiceName,
          object_path);
  if (!mm_sar_proxy_->Enable(true, &error)) {
    LOG(ERROR) << "Failed to Enable SAR in modem: " << error->GetMessage();
  }
  VLOG(1) << __func__ << " set modem state to online";
  HandleModemStateChange(ModemState::ONLINE);
}

void CellularController::ModemManagerInterfacesRemoved(
    const dbus::ObjectPath& object_path,
    const std::vector<std::string>& interfaces) {
  if (!base::Contains(interfaces, modemmanager::kModemManager1SarInterface)) {
    // In theory, a modem could drop, say, 3GPP, but not CDMA.  In
    // practice, we don't expect this.
    VLOG(1) << __func__ << "Interfaces removed, but not modem sar interface";
    return;
  }
  if (mm_sar_proxy_) {
    mm_sar_proxy_.reset();
  }
  VLOG(1) << __func__ << " set modem state to offline";
  HandleModemStateChange(ModemState::OFFLINE);
}

void CellularController::OnGetManagedObjectsReplySuccess(
    const system::DBusObjectsWithProperties& dbus_objects_with_properties) {
  if (dbus_objects_with_properties.empty()) {
    return;
  }

  for (const auto& object_properties_pair : dbus_objects_with_properties) {
    VLOG(1) << __func__ << ": " << object_properties_pair.first.value();
    ModemManagerInterfacesAdded(object_properties_pair.first,
                                object_properties_pair.second);
  }
}

void CellularController::OnModemManagerServiceAvailable(bool available) {
  if (!available) {
    if (mm_sar_proxy_) {
      mm_sar_proxy_.reset();
    }
    VLOG(1) << __func__ << " set modem state to offline";
    HandleModemStateChange(ModemState::OFFLINE);
    return;
  }

  mm_obj_proxy_->GetManagedObjects(
      BindOnce(&CellularController::OnGetManagedObjectsReplySuccess,
               weak_ptr_factory_.GetWeakPtr()));
}

void CellularController::OnServiceOwnerChanged(const std::string& old_owner,
                                               const std::string& new_owner) {
  VLOG(1) << __func__ << " old: " << old_owner << " new: " << new_owner;
  OnModemManagerServiceAvailable(!new_owner.empty());
}

void CellularController::OnShillDeviceChanged(
    const shill::Client::Device* const device) {
  if (!device || device->type != shill::Client::Device::Type::kCellular) {
    VLOG(1) << __func__ << " ifname = " << device->ifname
            << " not cellular device";
    return;
  }
  VLOG(1) << __func__ << " ifname = " << device->ifname
          << " country_code = " << device->cellular_country_code;
  HandleModemRegulatoryDomainChange(
      GetRegulatoryDomainFromCountryCode(device->cellular_country_code));
}

void CellularController::OnShillReady(bool success) {
  VLOG(1) << __func__ << " success : " << success;
  shill_ready_ = success;
  if (!shill_ready_) {
    LOG(INFO) << __func__ << " Shill not ready";
    return;
  }
  shill_->RegisterDeviceChangedHandler(
      base::BindRepeating(&CellularController::OnShillDeviceChanged,
                          weak_ptr_factory_.GetWeakPtr()));
  for (const auto& d : shill_->GetDevices()) {
    OnShillDeviceChanged(d.get());
  }
}

void CellularController::OnShillReset(bool reset) {
  VLOG(1) << __func__ << " reset : " << reset;
  if (reset) {
    LOG(INFO) << "Shill has been reset";
    return;
  }
  LOG(INFO) << "Shill has been shutdown";
  shill_ready_ = false;
  // Listen for it to come back.
  shill_->RegisterOnAvailableCallback(base::BindOnce(
      &CellularController::OnShillReady, weak_ptr_factory_.GetWeakPtr()));
}

void CellularController::InitShillProxyInterface() {
  shill_ = std::make_unique<shill::Client>(dbus_wrapper_->GetBus());
  shill_->RegisterProcessChangedHandler(base::BindRepeating(
      &CellularController::OnShillReset, weak_ptr_factory_.GetWeakPtr()));
  shill_->RegisterOnAvailableCallback(base::BindOnce(
      &CellularController::OnShillReady, weak_ptr_factory_.GetWeakPtr()));
}

void CellularController::InitModemManagerSarInterface() {
  mm_obj_proxy_ = std::make_unique<system::DBusObjectManagerWrapper>(
      dbus_wrapper_->GetBus(), modemmanager::kModemManager1ServiceName,
      modemmanager::kModemManager1ServicePath,
      base::BindOnce(&CellularController::OnModemManagerServiceAvailable,
                     weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&CellularController::OnServiceOwnerChanged,
                          weak_ptr_factory_.GetWeakPtr()));

  mm_obj_proxy_->set_interfaces_added_callback(
      BindRepeating(&CellularController::ModemManagerInterfacesAdded,
                    weak_ptr_factory_.GetWeakPtr()));
  mm_obj_proxy_->set_interfaces_removed_callback(
      BindRepeating(&CellularController::ModemManagerInterfacesRemoved,
                    weak_ptr_factory_.GetWeakPtr()));
}
#endif  // USE_CELLULAR

}  // namespace power_manager::policy
