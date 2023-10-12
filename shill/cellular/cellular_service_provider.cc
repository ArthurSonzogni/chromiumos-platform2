// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_service_provider.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>

#include "base/containers/fixed_flat_set.h"
#include "shill/cellular/cellular_service.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/store/store_interface.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
}  // namespace Logging

namespace {

bool IsValidEid(const std::string& sim_card_id) {
  // eID must be 32 characters in length. Since ICCID is limited to 20
  // characters, this is a strong indicator of a valid eID.
  return sim_card_id.size() == 32;
}

bool GetServiceParametersFromArgs(const KeyValueStore& args,
                                  std::string* imsi,
                                  std::string* iccid,
                                  std::string* eid,
                                  Error* error) {
  *iccid =
      args.Lookup<std::string>(CellularService::kStorageIccid, std::string());
  if (iccid->empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Missing ICCID");
    return false;
  }

  // If SimCardId != ICCID, it matches the eID. TODO(b/182943364): Store eID.
  std::string sim_card_id = args.Lookup<std::string>(
      CellularService::kStorageSimCardId, std::string());
  if (sim_card_id != *iccid) {
    if (IsValidEid(sim_card_id)) {
      *eid = sim_card_id;
    } else {
      LOG(ERROR) << "Unexpected SIM Card Id: " << sim_card_id;
      *eid = "";
    }
  } else {
    *eid = "";
  }

  // IMSI may be empty.
  *imsi =
      args.Lookup<std::string>(CellularService::kStorageImsi, std::string());

  return true;
}

bool GetServiceParametersFromStorage(const StoreInterface* storage,
                                     const std::string& entry_name,
                                     std::string* imsi,
                                     std::string* iccid,
                                     std::string* eid,
                                     Error* error) {
  if (!storage->GetString(entry_name, CellularService::kStorageIccid, iccid) ||
      iccid->empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidProperty,
                          "Missing or empty ICCID");
    return false;
  }

  // If SimCardId != ICCID, it matches the eID. TODO(b/182943364): Store eID.
  std::string sim_card_id;
  if (storage->GetString(entry_name, CellularService::kStorageSimCardId,
                         &sim_card_id) &&
      sim_card_id != *iccid) {
    if (IsValidEid(sim_card_id)) {
      *eid = sim_card_id;
    } else {
      LOG(ERROR) << "Unexpected SIM Card Id: " << sim_card_id;
      *eid = "";
    }
  } else {
    *eid = "";
  }

  // IMSI may be empty.
  storage->GetString(entry_name, CellularService::kStorageImsi, imsi);

  return true;
}

}  // namespace

CellularServiceProvider::CellularServiceProvider(Manager* manager)
    : manager_(manager), cros_config_(std::make_unique<brillo::CrosConfig>()) {}

CellularServiceProvider::~CellularServiceProvider() = default;

void CellularServiceProvider::CreateServicesFromProfile(
    const ProfileRefPtr& profile) {
  SLOG(2) << __func__ << ": " << profile->GetFriendlyName();
  // A Cellular Device may not exist yet, so we do not load services here.
  // Cellular services associated with a Device are loaded in
  // LoadServicesForDevice when the Device is created. We store |profile| here
  // so that we always use the default profile (see comment in header).
  if (!profile_)
    profile_ = profile;
}

ServiceRefPtr CellularServiceProvider::FindSimilarService(
    const KeyValueStore& args, Error* error) const {
  SLOG(2) << __func__;
  CHECK_EQ(kTypeCellular, args.Lookup<std::string>(kTypeProperty, ""))
      << "Service type must be Cellular!";
  // This is called from Manager::ConfigureServiceForProfile when the Manager
  // dbus api call is made (e.g. from Chrome). When a Cellular Service is
  // configured (e.g. from policy), find any existing Service matching |iccid|
  // and update that configuration.
  std::string iccid = args.Lookup<std::string>(kIccidProperty,
                                               /*default_value=*/"");
  return FindService(iccid);
}

ServiceRefPtr CellularServiceProvider::GetService(const KeyValueStore& args,
                                                  Error* error) {
  SLOG(2) << __func__;
  // This is called from Manager::GetService or Manager::ConfigureService when
  // the corresponding Manager dbus api call is made (e.g. from Chrome). When a
  // Cellular Service is configured (e.g. from policy), find any existing
  // Service matching |iccid| and update that configuration. If there's no
  // matching Service, a new Cellular Service is created with the given ICCID
  // and EID from |args|.
  std::string iccid = args.Lookup<std::string>(kIccidProperty,
                                               /*default_value=*/"");
  CellularServiceRefPtr service = FindService(iccid);
  if (service)
    return service;
  std::string eid = args.Lookup<std::string>(kEidProperty,
                                             /*default_value=*/"");
  LOG(INFO) << "Creating new cellular service with iccid: " << iccid
            << ", eid: " << eid;
  service = new CellularService(manager_, "", iccid, eid);
  AddService(service);
  return service;
}

ServiceRefPtr CellularServiceProvider::CreateTemporaryService(
    const KeyValueStore& args, Error* error) {
  SLOG(2) << __func__;
  std::string imsi, iccid, eid;
  if (GetServiceParametersFromArgs(args, &imsi, &iccid, &eid, error)) {
    return new CellularService(manager_, imsi, iccid, eid);
  }
  return nullptr;
}

ServiceRefPtr CellularServiceProvider::CreateTemporaryServiceFromProfile(
    const ProfileRefPtr& profile, const std::string& entry_name, Error* error) {
  SLOG(2) << __func__ << ": " << profile->GetFriendlyName();
  std::string imsi, iccid, eid;
  if (GetServiceParametersFromStorage(profile->GetConstStorage(), entry_name,
                                      &imsi, &iccid, &eid, error)) {
    return new CellularService(manager_, imsi, iccid, eid);
  }
  return nullptr;
}

void CellularServiceProvider::AbandonService(const ServiceRefPtr& service) {
  SLOG(2) << __func__;
}

void CellularServiceProvider::Start() {
  SLOG(2) << __func__;
}

void CellularServiceProvider::Stop() {
  SLOG(2) << __func__;
  RemoveServices();
}

CellularServiceRefPtr CellularServiceProvider::LoadServicesForDevice(
    Cellular* device) {
  SLOG(2) << __func__ << " Device ICCID: " << device->iccid();

  CellularServiceRefPtr active_service = LoadMatchingServicesFromProfile(
      device->eid(), device->iccid(), device->imsi(), device);

  // When the Cellular SIM changes or Cellular is enabled, assume that the
  // intent is to auto connect to the CellularService (if connectable and
  // AutoConnect are set), even if the service was previously explicitly
  // disconnected.
  active_service->ClearExplicitlyDisconnected();

  return active_service;
}

void CellularServiceProvider::RemoveNonDeviceServices(Cellular* device) {
  SLOG(2) << __func__ << " Device ICCID: " << device->iccid();
  std::vector<CellularServiceRefPtr> services_to_remove;
  for (CellularServiceRefPtr& service : services_) {
    if (!device->HasIccid(service->iccid()))
      services_to_remove.push_back(service);
  }
  for (CellularServiceRefPtr& service : services_to_remove)
    RemoveService(service);
}

CellularServiceRefPtr CellularServiceProvider::LoadMatchingServicesFromProfile(
    const std::string& eid,
    const std::string& iccid,
    const std::string& imsi,
    Cellular* device) {
  DCHECK(device);
  // Find Cellular profile entries matching the sim card identifier.
  DCHECK(profile_);
  StoreInterface* storage = profile_->GetStorage();
  DCHECK(storage);
  KeyValueStore args;
  args.Set<std::string>(kTypeProperty, kTypeCellular);
  args.Set<std::string>(CellularService::kStorageIccid, iccid);
  std::set<std::string> groups = storage->GetGroupsWithProperties(args);
  SLOG(2) << __func__ << ": " << iccid;
  LOG(INFO) << __func__ << ": Groups: " << groups.size();
  CellularServiceRefPtr active_service = nullptr;
  for (const std::string& group : groups) {
    std::string service_imsi, service_iccid, service_eid;
    if (!GetServiceParametersFromStorage(storage, group, &service_imsi,
                                         &service_iccid, &service_eid,
                                         /*error=*/nullptr)) {
      LOG(ERROR) << "Unable to load service properties for: " << iccid
                 << ", removing old or invalid profile entry.";
      storage->DeleteGroup(group);
      continue;
    }
    DCHECK_EQ(service_eid, eid);
    CellularServiceRefPtr service = FindService(service_iccid);
    if (!service) {
      SLOG(1) << "Creating Cellular service for ICCID: " << service_iccid;
      service = new CellularService(manager_, service_imsi, service_iccid,
                                    service_eid);
      // Device.AllowRoaming was used to store roaming preferences before M94.
      // To honor settings for services created before M94, we default
      // Service.AllowRoaming to the value of Device.AllowRoaming.
      // If a value for Service.AllowRoaming was persisted when the service was
      // last used, the default is overridden in Service::Load,
      // else the default value is stored to disk during AddService, thus the
      // value of Device.AllowRoaming is copied over to the service. This
      // completes the migration of Device.AllowRoaming to Service.AllowRoaming.
      // The plan is to remove references to device->allow_roaming_ in M108,
      // when we assume all services created before M94 have been used at least
      // once between M94 and M108, and thus have migrated their AllowRoaming.
      service->set_allow_roaming(device->allow_roaming());
      service->Load(storage);
      service->SetDevice(device);
      AddService(service);
    } else {
      SLOG(2) << "Cellular service exists for ICCID: " << service_iccid;
      service->SetDevice(device);
    }
    if (service_iccid == iccid)
      active_service = service;
  }

  if (active_service)
    return active_service;

  // If a Service was never saved, it may still exist in |services_|.
  active_service = FindService(iccid);
  if (active_service) {
    SLOG(2) << "Cellular service exists for ICCID: " << iccid
            << " (but not saved)";
    active_service->SetDevice(device);
    return active_service;
  }

  // Create a Service for the ICCID.
  SLOG(1) << "No existing Cellular service with ICCID: " << iccid;
  active_service = new CellularService(manager_, imsi, iccid, eid);
  active_service->SetDevice(device);
  AddService(active_service);
  return active_service;
}

void CellularServiceProvider::LoadServicesForSecondarySim(
    const std::string& eid,
    const std::string& iccid,
    const std::string& imsi,
    Cellular* device) {
  DCHECK(!iccid.empty());
  SLOG(1) << __func__ << " eid: " << eid << " iccid: " << iccid;
  LoadMatchingServicesFromProfile(eid, iccid, imsi, device);
}

void CellularServiceProvider::UpdateServices(Cellular* device) {
  SLOG(2) << __func__;
  for (CellularServiceRefPtr& service : services_)
    service->SetDevice(device);
}

void CellularServiceProvider::RemoveServices() {
  SLOG(1) << __func__;
  while (!services_.empty())
    RemoveService(services_.back());
}

CellularServiceRefPtr CellularServiceProvider::FindService(
    const std::string& iccid) const {
  const auto iter = std::find_if(
      services_.begin(), services_.end(),
      [iccid](const auto& service) { return service->iccid() == iccid; });
  if (iter != services_.end())
    return *iter;
  return nullptr;
}

void CellularServiceProvider::AddService(CellularServiceRefPtr service) {
  SLOG(1) << __func__ << " with ICCID: " << service->iccid();

  // See comment in header for |profile_|.
  service->SetProfile(profile_);
  // Save any changes to device properties (iccid, eid).
  profile_->UpdateService(service);
  manager_->RegisterService(service);
  services_.push_back(service);
}

void CellularServiceProvider::RemoveService(CellularServiceRefPtr service) {
  SLOG(1) << __func__ << " with ICCID: " << service->iccid();
  manager_->PersistService(service);
  manager_->DeregisterService(service);
  auto iter = std::find(services_.begin(), services_.end(), service);
  if (iter == services_.end()) {
    LOG(ERROR) << "RemoveService: Not found: ";
    return;
  }
  services_.erase(iter);
}

CellularService* CellularServiceProvider::GetActiveService() {
  for (CellularServiceRefPtr& service : services_) {
    if (service->IsActive(nullptr))
      return service.get();
  }
  return nullptr;
}

bool CellularServiceProvider::HardwareSupportsTethering(
    bool experimental_tethering) {
  // For now, the flag will allow all variants. If there is a need to block a
  // variant under any conditions, this has to be modified.
  if (experimental_tethering)
    return true;

  if (!variant_.has_value()) {
    SLOG(3) << __func__ << " reading modem firmware variant";
    std::string temp_variant;
    if (!cros_config_->GetString("/modem", "firmware-variant", &temp_variant)) {
      SLOG(3) << __func__ << "Cannot find modem firmware variant.";
      return false;
    }
    variant_ = std::move(temp_variant);
  }
  static constexpr auto blocklist = base::MakeFixedFlatSet<std::string_view>(
      {// trogdor variants
       "kingoftown", "lazor", "limozeen", "pazquel", "pazquel360",
       // strongbad variants
       "coachz", "quackingstick"});

  if (blocklist.contains(variant_.value())) {
    LOG(INFO) << "Cellular hardware does not support tethering";
    return false;
  }
  return true;
}

void CellularServiceProvider::TetheringEntitlementCheck(
    Cellular::EntitlementCheckResultCallback callback,
    bool experimental_tethering) {
  SLOG(3) << __func__;
  if (!HardwareSupportsTethering(experimental_tethering)) {
    manager_->metrics()->NotifyCellularEntitlementCheckResult(
        Metrics::kCellularEntitlementCheckNotAllowedOnVariant);
    std::move(callback).Run(
        TetheringManager::EntitlementStatus::kNotAllowedOnVariant);
    return;
  }

  const auto cellular_service = GetActiveService();
  if (!cellular_service || !cellular_service->cellular()) {
    SLOG(3) << __func__ << " cellular device doesn't exist";
    manager_->metrics()->NotifyCellularEntitlementCheckResult(
        Metrics::kCellularEntitlementCheckNoCellularDevice);
    std::move(callback).Run(
        TetheringManager::EntitlementStatus::kUpstreamNetworkNotAvailable);
    return;
  }

  cellular_service->cellular()->EntitlementCheck(std::move(callback),
                                                 experimental_tethering);
}

void CellularServiceProvider::AcquireTetheringNetwork(
    TetheringManager::UpdateTimeoutCallback update_timeout_callback,
    TetheringManager::AcquireNetworkCallback callback,
    TetheringManager::CellularUpstreamEventCallback tethering_event_callback,
    bool experimental_tethering) {
  SLOG(3) << __func__;
  if (!HardwareSupportsTethering(experimental_tethering)) {
    manager_->dispatcher()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       TetheringManager::SetEnabledResult::kNotAllowed, nullptr,
                       nullptr));
    return;
  }

  // Tethering setup requires an active service with a valid device.
  const auto cellular_service = GetActiveService();
  const auto cellular_device =
      cellular_service ? cellular_service->cellular() : nullptr;
  if (!cellular_device) {
    manager_->dispatcher()->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(callback),
            TetheringManager::SetEnabledResult::kUpstreamNetworkNotAvailable,
            nullptr, nullptr));
    return;
  }

  // Request a network for tethering.
  LOG(INFO) << "Acquiring tethering network.";
  cellular_device->AcquireTetheringNetwork(
      update_timeout_callback,
      base::BindOnce(&CellularServiceProvider::OnAcquireTetheringNetworkReady,
                     weak_factory_.GetWeakPtr(), std::move(callback)),
      std::move(tethering_event_callback), experimental_tethering);
}

void CellularServiceProvider::OnAcquireTetheringNetworkReady(
    TetheringManager::AcquireNetworkCallback callback,
    Network* network,
    const Error& error) {
  SLOG(3) << __func__;

  const auto cellular_service = GetActiveService();
  if (!cellular_service || !cellular_service->cellular()) {
    LOG(WARNING)
        << "Tethering network acquisition failed: no cellular service.";
    std::move(callback).Run(
        TetheringManager::SetEnabledResult::kUpstreamNetworkNotAvailable,
        nullptr, nullptr);
    return;
  }

  if (error.IsFailure()) {
    LOG(WARNING) << "Tethering network acquisition failed: " << error;
    std::move(callback).Run(
        TetheringManager::SetEnabledResult::kUpstreamNetworkNotAvailable,
        nullptr, nullptr);
    return;
  }

  if (!network) {
    LOG(WARNING)
        << "Tethering network acquisition failed: no network reported.";
    std::move(callback).Run(
        TetheringManager::SetEnabledResult::kUpstreamNetworkNotAvailable,
        nullptr, nullptr);
    return;
  }

  LOG(INFO) << "Tethering network acquisition successful.";
  std::move(callback).Run(TetheringManager::SetEnabledResult::kSuccess, network,
                          cellular_service);
}

void CellularServiceProvider::ReleaseTetheringNetwork(
    Network* network, base::OnceCallback<void(bool is_success)> callback) {
  SLOG(3) << __func__;

  // Tethering release requires an active service with a valid device.
  const auto cellular_service = GetActiveService();
  const auto cellular_device =
      cellular_service ? cellular_service->cellular() : nullptr;
  if (!cellular_device) {
    manager_->dispatcher()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }

  LOG(INFO) << "Releasing tethering network.";
  cellular_device->ReleaseTetheringNetwork(
      network,
      base::BindOnce(&CellularServiceProvider::OnReleaseTetheringNetworkReady,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void CellularServiceProvider::OnReleaseTetheringNetworkReady(
    base::OnceCallback<void(bool is_success)> callback, const Error& error) {
  SLOG(3) << __func__;

  if (error.IsFailure()) {
    LOG(WARNING) << "Tethering network release failed: " << error;
    std::move(callback).Run(false);
    return;
  }

  LOG(WARNING) << "Tethering network release successful.";
  std::move(callback).Run(true);
}

std::optional<std::string> CellularServiceProvider::GetOperatorCountryCode() {
  SLOG(3) << __func__;
  const auto cellular_service = GetActiveService();
  if (!cellular_service || !cellular_service->cellular()) {
    return std::nullopt;
  }
  auto country = cellular_service->cellular()
                     ->mobile_operator_info()
                     ->serving_mcc_alpha2();
  return country.empty() ? std::nullopt : std::optional<std::string>(country);
}

}  // namespace shill
