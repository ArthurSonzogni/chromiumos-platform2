// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <linux/if.h>  // NOLINT - Needs definitions from netinet/in.h

#include <ios>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/contains.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/notreached.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <net-base/ipv6_address.h>
#include <net-base/network_config.h>
#include <net-base/rtnl_handler.h>
#include <ModemManager/ModemManager.h>

#include "dbus/shill/dbus-constants.h"
#include "shill/adaptor_interfaces.h"
#include "shill/cellular/apn_list.h"
#include "shill/cellular/carrier_entitlement.h"
#include "shill/cellular/cellular_bearer.h"
#include "shill/cellular/cellular_capability_3gpp.h"
#include "shill/cellular/cellular_consts.h"
#include "shill/cellular/cellular_helpers.h"
#include "shill/cellular/cellular_service.h"
#include "shill/cellular/cellular_service_provider.h"
#include "shill/cellular/mobile_operator_info.h"
#include "shill/cellular/modem_info.h"
#include "shill/control_interface.h"
#include "shill/data_types.h"
#include "shill/dbus/dbus_control.h"
#include "shill/device.h"
#include "shill/device_info.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/external_task.h"
#include "shill/ipconfig.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/net/process_manager.h"
#include "shill/ppp_daemon.h"
#include "shill/profile.h"
#include "shill/service.h"
#include "shill/store/property_accessor.h"
#include "shill/store/store_interface.h"
#include "shill/technology.h"
#include "shill/tethering_manager.h"
#include "shill/virtual_device.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
}  // namespace Logging

namespace {

// Maximum time to wait for Modem registration before canceling a pending
// connect attempt.
constexpr base::TimeDelta kPendingConnectCancel = base::Minutes(1);

// Prefix used by entitlement check logging messages when the entitlement
// check is not successful. This prefix is used by the anomaly detector to
// identify these events.
constexpr char kEntitlementCheckAnomalyDetectorPrefix[] =
    "Entitlement check failed: ";

// Longer tethering start timeout value, used when the upstream network setup
// requires the connection of a new PDN.
static constexpr base::TimeDelta kLongTetheringStartTimeout = base::Seconds(45);

bool IsEnabledModemState(Cellular::ModemState state) {
  switch (state) {
    case Cellular::kModemStateFailed:
    case Cellular::kModemStateUnknown:
    case Cellular::kModemStateDisabled:
    case Cellular::kModemStateInitializing:
    case Cellular::kModemStateLocked:
    case Cellular::kModemStateDisabling:
    case Cellular::kModemStateEnabling:
      return false;
    case Cellular::kModemStateEnabled:
    case Cellular::kModemStateSearching:
    case Cellular::kModemStateRegistered:
    case Cellular::kModemStateDisconnecting:
    case Cellular::kModemStateConnecting:
    case Cellular::kModemStateConnected:
      return true;
  }
  return false;
}

Metrics::IPConfigMethod BearerIPConfigMethodToMetrics(
    CellularBearer::IPConfigMethod method) {
  using BearerType = CellularBearer::IPConfigMethod;
  using MetricsType = Metrics::IPConfigMethod;
  switch (method) {
    case BearerType::kUnknown:
      return MetricsType::kUnknown;
    case BearerType::kPPP:
      return MetricsType::kPPP;
    case BearerType::kStatic:
      return MetricsType::kStatic;
    case BearerType::kDHCP:
      return MetricsType::kDynamic;
  }
}

std::string GetFriendlyModelId(const std::string& model_id) {
  if (model_id.find("L850") != std::string::npos) {
    return "L850";
  }
  if (model_id.find("FM101") != std::string::npos) {
    return "FM101";
  }
  if (model_id.find("7c Compute") != std::string::npos) {
    return "SC7180";
  }
  if (model_id.find("4D75") != std::string::npos) {
    return "FM350";
  }
  if (model_id.find("NL668") != std::string::npos) {
    return "NL668";
  }
  return model_id;
}

// Returns if specified modem manager error can be classified as
// subscription related error. This API should be enhanced if
// better signals become available to detect subscription error.
bool IsSubscriptionError(std::string mm_error) {
  return mm_error == MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX
         ".ServiceOptionNotSubscribed";
}

void PrintApnListForDebugging(std::deque<Stringmap> apn_try_list,
                              ApnList::ApnType apn_type) {
  // Print list for debugging
  if (SLOG_IS_ON(Cellular, 3)) {
    std::string log_string =
        ": Try list: ApnType: " + ApnList::GetApnTypeString(apn_type);
    for (const auto& it : apn_try_list) {
      log_string += " " + GetPrintableApnStringmap(it);
    }
    SLOG(3) << __func__ << log_string;
  }
}

Metrics::DetailedCellularConnectionResult::ConnectionAttemptType
ConnectionAttemptTypeToMetrics(CellularServiceRefPtr service) {
  using MetricsType =
      Metrics::DetailedCellularConnectionResult::ConnectionAttemptType;
  if (!service)
    return MetricsType::kUnknown;
  if (service->is_in_user_connect())
    return MetricsType::kUserConnect;
  return MetricsType::kAutoConnect;
}

Metrics::DetailedCellularConnectionResult::APNType ApnTypeToMetricEnum(
    ApnList::ApnType apn_type) {
  switch (apn_type) {
    case ApnList::ApnType::kDefault:
      return Metrics::DetailedCellularConnectionResult::APNType::kDefault;
    case ApnList::ApnType::kAttach:
      return Metrics::DetailedCellularConnectionResult::APNType::kAttach;
    case ApnList::ApnType::kDun:
      return Metrics::DetailedCellularConnectionResult::APNType::kDUN;
  }
}

}  // namespace

// static
const char Cellular::kAllowRoaming[] = "AllowRoaming";
const char Cellular::kPolicyAllowRoaming[] = "PolicyAllowRoaming";
const char Cellular::kUseAttachApn[] = "UseAttachAPN";
const char Cellular::kQ6V5ModemManufacturerName[] = "QUALCOMM INCORPORATED";
const char Cellular::kQ6V5DriverName[] = "qcom-q6v5-mss";
const char Cellular::kQ6V5SysfsBasePath[] = "/sys/class/remoteproc";
const char Cellular::kQ6V5RemoteprocPattern[] = "remoteproc*";

// static
std::string Cellular::GetStateString(State state) {
  switch (state) {
    case State::kDisabled:
      return "Disabled";
    case State::kEnabled:
      return "Enabled";
    case State::kModemStarting:
      return "ModemStarting";
    case State::kModemStarted:
      return "ModemStarted";
    case State::kModemStopping:
      return "ModemStopping";
    case State::kRegistered:
      return "Registered";
    case State::kConnected:
      return "Connected";
    case State::kLinked:
      return "Linked";
    default:
      NOTREACHED();
  }
  return base::StringPrintf("CellularStateUnknown-%d", static_cast<int>(state));
}

// static
std::string Cellular::GetModemStateString(ModemState modem_state) {
  switch (modem_state) {
    case kModemStateFailed:
      return "ModemStateFailed";
    case kModemStateUnknown:
      return "ModemStateUnknown";
    case kModemStateInitializing:
      return "ModemStateInitializing";
    case kModemStateLocked:
      return "ModemStateLocked";
    case kModemStateDisabled:
      return "ModemStateDisabled";
    case kModemStateDisabling:
      return "ModemStateDisabling";
    case kModemStateEnabling:
      return "ModemStateEnabling";
    case kModemStateEnabled:
      return "ModemStateEnabled";
    case kModemStateSearching:
      return "ModemStateSearching";
    case kModemStateRegistered:
      return "ModemStateRegistered";
    case kModemStateDisconnecting:
      return "ModemStateDisconnecting";
    case kModemStateConnecting:
      return "ModemStateConnecting";
    case kModemStateConnected:
      return "ModemStateConnected";
    default:
      NOTREACHED();
  }
  return base::StringPrintf("ModemStateUnknown-%d", modem_state);
}

// static
void Cellular::ValidateApnTryList(std::deque<Stringmap>& apn_try_list) {
  // Entries in the APN try list must have the APN property
  apn_try_list.erase(
      std::remove_if(
          apn_try_list.begin(), apn_try_list.end(),
          [](const auto& item) { return !base::Contains(item, kApnProperty); }),
      apn_try_list.end());
}

// static
std::deque<Stringmap> Cellular::BuildFallbackEmptyApn(
    ApnList::ApnType apn_type) {
  std::deque<Stringmap> apn_list;
  std::vector<std::string> ip_types = {kApnIpTypeV4V6, kApnIpTypeV4,
                                       kApnIpTypeV6};
  for (const auto& ip_type : ip_types) {
    Stringmap apn;
    apn[kApnProperty] = "";
    apn[kApnTypesProperty] = ApnList::GetApnTypeString(apn_type);
    apn[kApnIpTypeProperty] = ip_type;
    apn[kApnSourceProperty] = cellular::kApnSourceFallback;
    apn_list.push_back(apn);
  }
  return apn_list;
}

Cellular::Cellular(Manager* manager,
                   const std::string& link_name,
                   const std::string& address,
                   int interface_index,
                   const std::string& service,
                   const RpcIdentifier& path)
    : Device(
          manager, link_name, address, interface_index, Technology::kCellular),
      mobile_operator_info_(
          new MobileOperatorInfo(manager->dispatcher(), "cellular")),
      dbus_service_(service),
      dbus_path_(path),
      dbus_path_str_(path.value()),
      process_manager_(ProcessManager::GetInstance()) {
  RegisterProperties();
  mobile_operator_info_->Init();

  socket_destroyer_ = net_base::NetlinkSockDiag::Create();
  if (!socket_destroyer_) {
    LOG(WARNING) << LoggingTag() << ": Socket destroyer failed to initialize; "
                 << "IPv6 will be unavailable.";
  }

  // Create an initial Capability.
  CreateCapability();

  // Reset networks
  default_pdn_apn_type_ = std::nullopt;
  default_pdn_.reset();
  multiplexed_tethering_pdn_.reset();

  carrier_entitlement_ = std::make_unique<CarrierEntitlement>(
      this, metrics(),
      base::BindRepeating(&Cellular::OnEntitlementCheckUpdated,
                          weak_ptr_factory_.GetWeakPtr()));
  SLOG(1) << LoggingTag() << ": Cellular()";
}

Cellular::~Cellular() {
  LOG(INFO) << LoggingTag() << ": ~Cellular()";
  if (capability_)
    DestroyCapability();
}

void Cellular::CreateImplicitNetwork(bool fixed_ip_params) {
  // No implicit network in Cellular.
}

Network* Cellular::GetPrimaryNetwork() const {
  // The default network is considered as primary always.
  return default_pdn_ ? default_pdn_->network() : nullptr;
}

std::string Cellular::GetLegacyEquipmentIdentifier() const {
  // 3GPP devices are uniquely identified by IMEI, which has 15 decimal digits.
  if (!imei_.empty())
    return imei_;

  // 3GPP2 devices are uniquely identified by MEID, which has 14 hexadecimal
  // digits.
  if (!meid_.empty())
    return meid_;

  // An equipment ID may be reported by ModemManager, which is typically the
  // serial number of a legacy AT modem, and is either the IMEI, MEID, or ESN
  // of a MBIM/QMI modem. This is used as a fallback in case neither IMEI nor
  // MEID could be retrieved through ModemManager (e.g. when there is no SIM
  // inserted, ModemManager doesn't expose modem 3GPP interface where the IMEI
  // is reported).
  if (!equipment_id_.empty())
    return equipment_id_;

  // If none of IMEI, MEID, and equipment ID is available, fall back to MAC
  // address.
  return mac_address();
}

std::string Cellular::DeviceStorageSuffix() const {
  // Cellular is not guaranteed to have a valid MAC address, and other unique
  // identifiers may not be initially available. Use the link name to
  // differentiate between internal devices and external devices.
  return link_name();
}

bool Cellular::Load(const StoreInterface* storage) {
  std::string id = GetStorageIdentifier();
  SLOG(2) << LoggingTag() << ": " << __func__ << ": Device ID: " << id;
  if (!storage->ContainsGroup(id)) {
    id = "device_" + GetLegacyEquipmentIdentifier();
    if (!storage->ContainsGroup(id)) {
      LOG(WARNING) << LoggingTag() << ": " << __func__
                   << ": Device is not available in the persistent store";
      return false;
    }
    legacy_storage_id_ = id;
  }
  storage->GetBool(id, kAllowRoaming, &allow_roaming_);
  storage->GetBool(id, kPolicyAllowRoaming, &policy_allow_roaming_);
  LOG(INFO) << LoggingTag() << ": " << __func__ << ": " << kAllowRoaming << ":"
            << allow_roaming_ << " " << kPolicyAllowRoaming << ":"
            << policy_allow_roaming_;
  return Device::Load(storage);
}

bool Cellular::Save(StoreInterface* storage) {
  const std::string id = GetStorageIdentifier();
  storage->SetBool(id, kAllowRoaming, allow_roaming_);
  storage->SetBool(id, kPolicyAllowRoaming, policy_allow_roaming_);
  bool result = Device::Save(storage);
  SLOG(2) << LoggingTag() << ": " << __func__ << ": Device ID: " << id;
  LOG(INFO) << LoggingTag() << ": " << __func__ << ": " << result;
  // TODO(b/181843251): Remove when number of users on M92 are negligible.
  if (result && !legacy_storage_id_.empty() &&
      storage->ContainsGroup(legacy_storage_id_)) {
    SLOG(2) << LoggingTag() << ": " << __func__
            << ": Deleting legacy storage id: " << legacy_storage_id_;
    storage->DeleteGroup(legacy_storage_id_);
    legacy_storage_id_.clear();
  }
  return result;
}

std::string Cellular::GetTechnologyFamily(Error* error) {
  return capability_ ? capability_->GetTypeString() : "";
}

std::string Cellular::GetDeviceId(Error* error) {
  return device_id_ ? device_id_->AsString() : "";
}

bool Cellular::GetMultiplexSupport() {
  // The device allows multiplexing support when more than one multiplexed
  // bearers can be setup at a given time.
  return (max_multiplexed_bearers_ > 1);
}

bool Cellular::ShouldBringNetworkInterfaceDownAfterDisabled() const {
  if (!device_id_)
    return false;

  // The cdc-mbim kernel driver stop draining the receive buffer after the
  // network interface is brought down. However, some MBIM modem (see
  // b:71505232) may misbehave if the host stops draining the receiver buffer
  // before issuing a MBIM command to disconnect the modem from network. To
  // work around the issue, shill needs to defer bringing down the network
  // interface until after the modem is disabled.
  //
  // TODO(benchan): Investigate if we need to apply the workaround for other
  // MBIM modems or revert this change once the issue is addressed by the modem
  // firmware on Fibocom L850-GL.
  static constexpr ModemType kAffectedModemTypes[] = {ModemType::kL850GL};
  for (const auto& affected_modem_type : kAffectedModemTypes) {
    if (modem_type_ == affected_modem_type)
      return true;
  }

  return false;
}

void Cellular::BringNetworkInterfaceDown() {
  // The physical network interface always exists, unconditionally, so it can
  // be brought down safely regardless of whether a Network exists or not.
  // There is no need to bring down explicitly any additional multiplexed
  // network interface managed by the device because those are fully removed
  // whenever the corresponding PDN is disconnected.
  rtnl_handler()->SetInterfaceFlags(interface_index(), 0, IFF_UP);
}

void Cellular::SetState(State state) {
  if (state == state_)
    return;
  LOG(INFO) << LoggingTag() << ": " << __func__ << ": "
            << GetStateString(state_) << " -> " << GetStateString(state);
  state_ = state;
  UpdateScanning();
}

void Cellular::SetModemState(ModemState modem_state) {
  if (modem_state == modem_state_)
    return;
  LOG(INFO) << LoggingTag() << ": " << __func__ << ": "
            << GetModemStateString(modem_state_) << " -> "
            << GetModemStateString(modem_state);
  modem_state_ = modem_state;
  UpdateScanning();
}

void Cellular::HelpRegisterDerivedBool(std::string_view name,
                                       bool (Cellular::*get)(Error* error),
                                       bool (Cellular::*set)(const bool& value,
                                                             Error* error)) {
  mutable_store()->RegisterDerivedBool(
      name, BoolAccessor(new CustomAccessor<Cellular, bool>(this, get, set)));
}

void Cellular::HelpRegisterConstDerivedString(
    std::string_view name, std::string (Cellular::*get)(Error*)) {
  mutable_store()->RegisterDerivedString(
      name, StringAccessor(
                new CustomAccessor<Cellular, std::string>(this, get, nullptr)));
}

void Cellular::Start(EnabledStateChangedCallback callback) {
  LOG(INFO) << LoggingTag() << ": " << __func__ << ": "
            << GetStateString(state_);

  if (!capability_) {
    // Report success, even though a connection will not succeed until a Modem
    // is instantiated and |cabability_| is created. Setting |state_|
    // to kEnabled here will cause CreateCapability to call StartModem.
    SetState(State::kEnabled);
    LOG(WARNING) << LoggingTag() << ": " << __func__
                 << ": Skipping Start (no capability).";
    std::move(callback).Run(Error(Error::kSuccess));
    return;
  }

  StartModem(std::move(callback));
}

void Cellular::Stop(EnabledStateChangedCallback callback) {
  LOG(INFO) << LoggingTag() << ": " << __func__ << ": "
            << GetStateString(state_);
  DCHECK(!stop_step_.has_value()) << "Already stopping. Unexpected Stop call.";
  stop_step_ = StopSteps::kStopModem;
  StopStep(std::move(callback), Error());
}

void Cellular::StopStep(EnabledStateChangedCallback callback,
                        const Error& error_result) {
  SLOG(1) << LoggingTag() << ": " << __func__ << ": " << GetStateString(state_);
  DCHECK(stop_step_.has_value());
  switch (stop_step_.value()) {
    case StopSteps::kStopModem:

      // Destroy any cellular services regardless of any errors that occur
      // during the stop process since we do not know the state of the modem at
      // this point.
      DestroyAllServices();

      if (capability_) {
        LOG(INFO) << LoggingTag() << ": " << __func__ << ": Calling StopModem.";
        SetState(State::kModemStopping);
        capability_->StopModem(base::BindOnce(&Cellular::StopModemCallback,
                                              weak_ptr_factory_.GetWeakPtr(),
                                              std::move(callback)));
        return;
      }
      stop_step_ = StopSteps::kModemStopped;
      [[fallthrough]];

    case StopSteps::kModemStopped:
      SetState(State::kDisabled);

      // Sockets should be destroyed here to ensure that we make new connections
      // when we next enable Cellular. Since the carrier may assign us a new IP
      // on reconnect and some carriers don't like it when packets are sent from
      // this device using the old IP, we need to make sure that we prevent
      // further packets from going out.
      DestroySockets();

      // In case no termination action was executed (and
      // TerminationActionComplete was not invoked) in response to a suspend
      // request, any registered termination action needs to be removed
      // explicitly.
      manager()->RemoveTerminationAction(link_name());

      UpdateScanning();

      if (error_result.type() == Error::kWrongState) {
        // ModemManager.Modem will not respond to Stop when in a failed state.
        // Allow the callback to succeed so that Shill identifies and persists
        // Cellular as disabled. TODO(b/184974739): StopModem should probably
        // succeed when in a failed state.
        LOG(WARNING) << LoggingTag()
                     << ": StopModem returned an error: " << error_result;
        std::move(callback).Run(Error());
      } else {
        if (error_result.IsFailure())
          LOG(ERROR) << LoggingTag()
                     << ": StopModem returned an error: " << error_result;
        std::move(callback).Run(error_result);
      }
      stop_step_.reset();
      return;
  }
}

void Cellular::StartModem(EnabledStateChangedCallback callback) {
  DCHECK(capability_);
  LOG(INFO) << LoggingTag() << ": " << __func__;
  SetState(State::kModemStarting);
  capability_->StartModem(base::BindOnce(&Cellular::StartModemCallback,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         std::move(callback)));
}

void Cellular::StartModemCallback(EnabledStateChangedCallback callback,
                                  const Error& error) {
  LOG(INFO) << LoggingTag() << ": " << __func__
            << ": state=" << GetStateString(state_);

  if (!error.IsSuccess()) {
    SetState(State::kEnabled);
    if (error.type() == Error::kWrongState) {
      // If the enable operation failed with Error::kWrongState, the modem is
      // in an unexpected state. This usually indicates a missing or locked
      // SIM. Invoke |callback| with no error so that the enable completes.
      // If the ModemState property later changes to 'disabled', StartModem
      // will be called again.
      LOG(WARNING) << LoggingTag() << ": StartModem failed: " << error;
      std::move(callback).Run(Error(Error::kSuccess));
    } else {
      LOG(ERROR) << LoggingTag() << ": StartModem failed: " << error;
      std::move(callback).Run(error);
    }
    return;
  }

  SetState(State::kModemStarted);

  // Registration state updates may have been ignored while the
  // modem was not yet marked enabled.
  HandleNewRegistrationState();

  metrics()->NotifyDeviceEnableFinished(interface_index());

  std::move(callback).Run(Error(Error::kSuccess));
}

void Cellular::StopModemCallback(EnabledStateChangedCallback callback,
                                 const Error& error_result) {
  LOG(INFO) << LoggingTag() << ": " << __func__ << ": "
            << GetStateString(state_) << " Error: " << error_result;
  stop_step_ = StopSteps::kModemStopped;
  StopStep(std::move(callback), error_result);
}

void Cellular::DestroySockets() {
  if (!socket_destroyer_)
    return;

  if (default_pdn_) {
    default_pdn_->DestroySockets();
  }
  if (multiplexed_tethering_pdn_) {
    multiplexed_tethering_pdn_->DestroySockets();
  }
}

void Cellular::CompleteActivation(Error* error) {
  if (capability_)
    capability_->CompleteActivation(error);
}

bool Cellular::IsUnderlyingDeviceEnabled() const {
  return IsEnabledModemState(modem_state_);
}

void Cellular::Scan(Error* error,
                    const std::string& /*reason*/,
                    bool /*is_dbus_call*/) {
  SLOG(2) << LoggingTag() << ": Scanning started";
  CHECK(error);
  if (proposed_scan_in_progress_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInProgress,
                          "Already scanning");
    return;
  }

  if (!capability_)
    return;

  capability_->Scan(
      base::BindOnce(&Cellular::OnScanStarted, weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&Cellular::OnScanReply, weak_ptr_factory_.GetWeakPtr()));
}

void Cellular::RegisterOnNetwork(const std::string& network_id,
                                 ResultCallback callback) {
  if (!capability_) {
    std::move(callback).Run(Error(Error::Type::kOperationFailed));
    return;
  }
  capability_->RegisterOnNetwork(network_id, std::move(callback));
}

void Cellular::RequirePin(const std::string& pin,
                          bool require,
                          ResultCallback callback) {
  SLOG(2) << LoggingTag() << ": " << __func__ << ": " << require;
  if (!capability_) {
    std::move(callback).Run(Error(Error::Type::kOperationFailed));
    return;
  }
  capability_->RequirePin(pin, require, std::move(callback));
}

void Cellular::EnterPin(const std::string& pin, ResultCallback callback) {
  SLOG(2) << LoggingTag() << ": " << __func__;
  if (!capability_) {
    std::move(callback).Run(Error(Error::Type::kOperationFailed));
    return;
  }
  capability_->EnterPin(pin, std::move(callback));
}

void Cellular::UnblockPin(const std::string& unblock_code,
                          const std::string& pin,
                          ResultCallback callback) {
  SLOG(2) << LoggingTag() << ": " << __func__;
  if (!capability_) {
    std::move(callback).Run(Error(Error::Type::kOperationFailed));
    return;
  }
  capability_->UnblockPin(unblock_code, pin, std::move(callback));
}

void Cellular::ChangePin(const std::string& old_pin,
                         const std::string& new_pin,
                         ResultCallback callback) {
  SLOG(2) << LoggingTag() << ": " << __func__;
  if (!capability_) {
    std::move(callback).Run(Error(Error::Type::kOperationFailed));
    return;
  }
  capability_->ChangePin(old_pin, new_pin, std::move(callback));
}

void Cellular::Reset(ResultCallback callback) {
  SLOG(2) << LoggingTag() << ": " << __func__;

  // Qualcomm q6v5 modems on trogdor do not support reset using qmi messages.
  // As per QC the only way to reset the modem is to use the sysfs interface.
  if (IsQ6V5Modem()) {
    if (!ResetQ6V5Modem()) {
      std::move(callback).Run(Error(Error::Type::kOperationFailed));
    } else {
      std::move(callback).Run(Error(Error::Type::kSuccess));
    }
    return;
  }

  if (!capability_) {
    std::move(callback).Run(Error(Error::Type::kOperationFailed));
    return;
  }
  capability_->Reset(std::move(callback));
}

void Cellular::DropConnectionDefault() {
  SetPrimaryMultiplexedInterface("");
  default_pdn_apn_type_ = std::nullopt;
  default_pdn_.reset();
  multiplexed_tethering_pdn_.reset();
  SelectService(nullptr);
}

void Cellular::DropConnection() {
  if (ppp_device_) {
    // For PPP dongles, IP configuration is handled on the |ppp_device_|,
    // rather than the netdev plumbed into |this|.
    ppp_device_->DropConnection();
    return;
  }

  DropConnectionDefault();
}

void Cellular::SetServiceState(Service::ConnectState state) {
  if (ppp_device_) {
    ppp_device_->SetServiceState(state);
  } else if (selected_service()) {
    Device::SetServiceState(state);
  } else if (service_) {
    service_->SetState(state);
  } else {
    LOG(WARNING) << LoggingTag() << ": State change with no Service.";
  }
}

void Cellular::SetServiceFailure(Service::ConnectFailure failure_state) {
  LOG(WARNING) << LoggingTag() << ": " << __func__ << ": "
               << Service::ConnectFailureToString(failure_state);
  if (ppp_device_) {
    ppp_device_->SetServiceFailure(failure_state);
  } else if (selected_service()) {
    Device::SetServiceFailure(failure_state);
  } else if (service_) {
    service_->SetFailure(failure_state);
  } else {
    LOG(WARNING) << LoggingTag() << ": State change with no Service.";
  }
}

void Cellular::SetServiceFailureSilent(Service::ConnectFailure failure_state) {
  SLOG(2) << LoggingTag() << ": " << __func__ << ": "
          << Service::ConnectFailureToString(failure_state);
  if (ppp_device_) {
    ppp_device_->SetServiceFailureSilent(failure_state);
  } else if (selected_service()) {
    Device::SetServiceFailureSilent(failure_state);
  } else if (service_) {
    service_->SetFailureSilent(failure_state);
  } else {
    LOG(WARNING) << LoggingTag() << ": State change with no Service.";
  }
}

void Cellular::OnConnected() {
  // If state is already connected and we have a default Network setup, do
  // nothing. The missing Network while connected may happen during the
  // reconnection performed by the tethering logic when using the tethering
  // APN as default.
  if (StateIsConnected() && default_pdn_) {
    SLOG(1) << LoggingTag() << ": " << __func__ << ": Already connected";
    return;
  }
  SLOG(1) << LoggingTag() << ": " << __func__;
  SetState(State::kConnected);
  if (!service_) {
    LOG(INFO) << LoggingTag() << ": Disconnecting due to no cellular service.";
    Disconnect(nullptr, "no cellular service");
  } else if (service_->IsRoamingRuleViolated()) {
    // TODO(pholla): This logic is probably unreachable since we have two gate
    // keepers that prevent this scenario.
    // a) Cellular::Connect prevents connects if roaming rules are violated.
    // b) CellularCapability3gpp::FillConnectPropertyMap will not allow MM to
    //    connect to roaming networks.
    LOG(INFO) << LoggingTag() << ": Disconnecting due to roaming.";
    Disconnect(nullptr, "roaming disallowed");
  } else {
    EstablishLink();
  }
}

void Cellular::OnBeforeSuspend(ResultCallback callback) {
  LOG(INFO) << LoggingTag() << ": " << __func__;
  Error error;
  StopPPP();
  if (capability_)
    capability_->SetModemToLowPowerModeOnModemStop(true);
  SetEnabledNonPersistent(false, std::move(callback));
}

void Cellular::OnAfterResume() {
  SLOG(2) << LoggingTag() << ": " << __func__;
  if (enabled_persistent()) {
    LOG(INFO) << LoggingTag() << ": Restarting modem after resume.";
    // TODO(b/216847428): replace this with a real toggle
    SetEnabledUnchecked(true, base::BindOnce(LogRestartModemResult));
  }

  // TODO(quiche): Consider if this should be conditional. If, e.g.,
  // the device was still disabling when we suspended, will trying to
  // renew DHCP here cause problems?
  Device::OnAfterResume();
}

void Cellular::UpdateGeolocationObjects(
    std::vector<GeolocationInfo>* geolocation_infos) const {
  const std::string& mcc = location_info_.mcc;
  const std::string& mnc = location_info_.mnc;
  const std::string& lac = location_info_.lac;
  const std::string& cid = location_info_.ci;

  GeolocationInfo geolocation_info;

  if (!(mcc.empty() || mnc.empty() || lac.empty() || cid.empty())) {
    geolocation_info[kGeoMobileCountryCodeProperty] = mcc;
    geolocation_info[kGeoMobileNetworkCodeProperty] = mnc;
    geolocation_info[kGeoLocationAreaCodeProperty] = lac;
    geolocation_info[kGeoCellIdProperty] = cid;
    // kGeoTimingAdvanceProperty currently unused in geolocation API
  }
  // Else we have either an incomplete location, no location yet,
  // or some unsupported location type, so don't return something incorrect.
  geolocation_infos->clear();
  geolocation_infos->push_back(geolocation_info);
}

void Cellular::OnConnectionUpdated(int interface_index) {
  SLOG(1) << LoggingTag() << ": connection updated: " << interface_index;

  // Event on the default network, propagate it to the parent.
  if (default_pdn_ &&
      interface_index == default_pdn_->network()->interface_index()) {
    // If the requested APN was connected during a DUN as DEFAULT connect or
    // disconnect operation, we can now complete the operation successfully.
    // If any event happens before we have connected the APN, we should
    // ignore it.
    if (IsTetheringOperationDunAsDefaultOngoing()) {
      if (tethering_operation_->apn_connected) {
        SLOG(1) << LoggingTag() << ": tethering operation can be completed";
        CompleteTetheringOperation(Error(Error::kSuccess));
      } else {
        SLOG(1) << LoggingTag() << ": tethering operation still ongoing";
      }
    }
    Device::OnConnectionUpdated(interface_index);
    return;
  }

  // Event on the tethering-specific multiplexed network.
  if (multiplexed_tethering_pdn_ &&
      interface_index ==
          multiplexed_tethering_pdn_->network()->interface_index()) {
    if (IsTetheringOperationDunMultiplexedConnectOngoing()) {
      if (tethering_operation_->apn_connected) {
        SLOG(1) << LoggingTag()
                << ": multiplexed tethering operation can be completed";
        CompleteTetheringOperation(Error(Error::kSuccess));
      } else {
        SLOG(1) << LoggingTag()
                << ": multiplexed tethering operation still ongoing";
      }
    }
    return;
  }

  LOG(WARNING) << LoggingTag()
               << ": Unexpected network connection update: " << interface_index;
}

void Cellular::ConfigureAttachApn() {
  SLOG(1) << LoggingTag() << ": " << __func__;
  if (!enabled() && !enabled_pending()) {
    LOG(WARNING) << LoggingTag() << ": " << __func__
                 << ": Modem not enabled, skip attach APN configuration.";
    return;
  }

  capability_->ConfigureAttachApn();
}

void Cellular::CancelPendingConnect() {
  ConnectToPendingFailed(Service::kFailureDisconnect);
}

void Cellular::OnScanStarted() {
  proposed_scan_in_progress_ = true;
  UpdateScanning();
}

void Cellular::OnScanReply(const Stringmaps& found_networks,
                           const Error& error) {
  SLOG(2) << LoggingTag() << ": Scanning completed";
  proposed_scan_in_progress_ = false;
  UpdateScanning();

  // TODO(jglasgow): fix error handling.
  // At present, there is no way of notifying user of this asynchronous error.
  if (error.IsFailure()) {
    error.Log();
    if (!found_networks_.empty())
      SetFoundNetworks(Stringmaps());
    return;
  }

  SetFoundNetworks(found_networks);
}

// Called from an asyc D-Bus function
// Relies on location handler to fetch relevant value from map
void Cellular::GetLocationCallback(const std::string& gpp_lac_ci_string,
                                   const Error& error) {
  // Expects string of form "MCC,MNC,LAC,CI"
  SLOG(2) << LoggingTag() << ": " << __func__ << ": " << gpp_lac_ci_string;
  std::vector<std::string> location_vec = SplitString(
      gpp_lac_ci_string, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (location_vec.size() < 4) {
    LOG(ERROR) << LoggingTag() << ": Unable to parse location string "
               << gpp_lac_ci_string;
    return;
  }
  location_info_.mcc = location_vec[0];
  location_info_.mnc = location_vec[1];
  location_info_.lac = location_vec[2];
  location_info_.ci = location_vec[3];

  // Alert manager that location has been updated.
  manager()->OnDeviceGeolocationInfoUpdated(this);
}

void Cellular::PollLocationTask() {
  SLOG(4) << LoggingTag() << ": " << __func__;

  PollLocation();

  poll_location_task_.Reset(base::BindOnce(&Cellular::PollLocationTask,
                                           weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostDelayedTask(FROM_HERE, poll_location_task_.callback(),
                                kPollLocationInterval);
}

void Cellular::PollLocation() {
  if (!capability_)
    return;
  capability_->GetLocation(base::BindOnce(&Cellular::GetLocationCallback,
                                          weak_ptr_factory_.GetWeakPtr()));
}

void Cellular::HandleNewSignalQuality(uint32_t strength) {
  SLOG(2) << LoggingTag() << ": Signal strength: " << strength;
  if (service_) {
    service_->SetStrength(strength);
  }
}

void Cellular::HandleNewRegistrationState() {
  SLOG(2) << LoggingTag() << ": " << __func__
          << ": state = " << GetStateString(state_);

  CHECK(capability_);
  if (!capability_->IsRegistered()) {
    if (!explicit_disconnect_ && StateIsConnected() && service_.get()) {
      // TODO(b/200584652): Remove after January 2024
      if (capability_->GetNetworkTechnologyString() == "")
        LOG(INFO) << LoggingTag() << ": Logging Drop connection on unknown "
                  << "cellular technology";

      metrics()->NotifyCellularDeviceDrop(
          capability_->GetNetworkTechnologyString(), service_->strength());
    }
    if (StateIsRegistered()) {
      // If the state is moving out of Connected/Linked clean up IP/networking.
      OnDisconnected();
      SetState(State::kEnabled);
    }
    StopLocationPolling();
    return;
  }

  switch (state_) {
    case State::kDisabled:
    case State::kModemStarting:
    case State::kModemStopping:
      // Defer updating Services while disabled and during transitions.
      return;
    case State::kEnabled:
      LOG(WARNING) << LoggingTag() << ": Capability is registered but "
                   << "State=Enabled. Setting to Registered. ModemState="
                   << GetModemStateString(modem_state_);
      SetRegistered();
      break;
    case State::kModemStarted:
      SetRegistered();
      break;
    case State::kRegistered:
    case State::kConnected:
    case State::kLinked:
      // Already registered
      break;
  }
  if (service_) {
    service_->ResetAutoConnectCooldownTime();
  }
  UpdateServices();
}

void Cellular::SetRegistered() {
  DCHECK(!StateIsRegistered());
  SetState(State::kRegistered);
  // Once the modem becomes registered, begin polling location; registered means
  // we've successfully connected
  StartLocationPolling();
}

void Cellular::UpdateServices() {
  SLOG(2) << LoggingTag() << ": " << __func__;
  // When Disabled, ensure all services are destroyed except when ModemState is:
  //  * Locked: The primary SIM is locked and the modem has not started.
  //  * Failed: No valid SIM in the primary slot.
  // In these cases we want to create any services we know about for the UI.
  if (state_ == State::kDisabled && modem_state_ != kModemStateLocked &&
      modem_state_ != kModemStateFailed) {
    DestroyAllServices();
    return;
  }

  // If iccid_ is empty, the primary slot is not set, so do not create a
  // primary service. CreateSecondaryServices() will have been called in
  // SetSimProperties(). Just ensure that the Services are updated.
  if (iccid_.empty()) {
    manager()->cellular_service_provider()->UpdateServices(this);
    return;
  }

  // Ensure that a Service matching the Device SIM Profile exists and has its
  // |connectable_| property set correctly.
  if (!service_ || service_->iccid() != iccid_) {
    CreateServices();
  } else {
    manager()->cellular_service_provider()->UpdateServices(this);
  }

  if (state_ == State::kRegistered && modem_state_ == kModemStateConnected) {
    // On an idle->registered reg state change while modem is connected, we may
    // need to establish links both in default and tethering, but the tethering
    // one will need to go always once the default one is up.
    OnConnected();
  }

  service_->SetNetworkTechnology(capability_->GetNetworkTechnologyString());
  service_->SetRoamingState(capability_->GetRoamingStateString());
  manager()->UpdateService(service_);
  ConnectToPending();
}

void Cellular::CreateServices() {
  if (service_for_testing_)
    return;

  if (service_ && service_->iccid() == iccid_) {
    LOG(ERROR) << LoggingTag() << ": " << __func__
               << ": Service already exists for ICCID.";
    return;
  }

  CHECK(capability_);
  DCHECK(manager()->cellular_service_provider());

  // Create or update Cellular Services for the primary SIM.
  service_ =
      manager()->cellular_service_provider()->LoadServicesForDevice(this);
  LOG(INFO) << LoggingTag() << ": " << __func__
            << ": Service=" << service_->log_name();

  // Create or update Cellular Services for secondary SIMs.
  UpdateSecondaryServices();

  capability_->OnServiceCreated();

  // Ensure operator properties are updated.
  OnOperatorChanged();
  if (service_ && manager()->power_opt())
    manager()->power_opt()->AddOptInfoForNewService(service_->iccid());
}

void Cellular::DestroyAllServices() {
  LOG(INFO) << LoggingTag() << ": " << __func__;
  DropConnection();

  if (service_for_testing_)
    return;

  DCHECK(manager()->cellular_service_provider());
  manager()->cellular_service_provider()->RemoveServices();
  service_ = nullptr;
}

void Cellular::UpdateSecondaryServices() {
  for (const SimProperties& sim_properties : sim_slot_properties_) {
    if (sim_properties.iccid.empty() || sim_properties.iccid == iccid_)
      continue;
    manager()->cellular_service_provider()->LoadServicesForSecondarySim(
        sim_properties.eid, sim_properties.iccid, sim_properties.imsi, this);
  }

  // Remove any Services no longer associated with a SIM slot.
  manager()->cellular_service_provider()->RemoveNonDeviceServices(this);
}

void Cellular::OnNetworkValidationResult(int interface_index,
                                         const PortalDetector::Result& result) {
  SLOG(1) << LoggingTag() << ": " << __func__;

  Device::OnNetworkValidationResult(interface_index, result);

  // TODO(b/309512268) add support for tethering PDN.
  if (!IsEventOnPrimaryNetwork(interface_index)) {
    return;
  }
  if (!selected_service() || !selected_service()->IsConnected()) {
    return;
  }
  if (!service_ || !service_->GetLastGoodApn()) {
    return;
  }
  // Report cellular specific metrics with APN information.
  NotifyNetworkValidationResult(*service_->GetLastGoodApn(),
                                result.GetResultMetric(), result.num_attempts);
}

void Cellular::OnModemDestroyed() {
  SLOG(1) << LoggingTag() << ": " << __func__;
  StopLocationPolling();
  DestroyCapability();
  SetForceInitEpsBearerSettings(true);
  // Clear the dbus path.
  SetDbusPath(shill::RpcIdentifier());

  // Under certain conditions, Cellular::StopModem may not be called before
  // the Modem object is destroyed. This happens if the dbus modem exported
  // by the modem-manager daemon disappears soon after the modem is disabled,
  // not giving Shill enough time to complete the disable operation.
  // In that case, the termination action associated with this cellular object
  // may not have been removed.
  manager()->RemoveTerminationAction(link_name());
}

void Cellular::CreateCapability() {
  SLOG(1) << LoggingTag() << ": " << __func__;
  CHECK(!capability_);
  capability_ = std::make_unique<CellularCapability3gpp>(
      this, manager()->control_interface(), manager()->metrics(),
      manager()->modem_info()->pending_activation_store());
  if (initial_properties_.has_value()) {
    SetInitialProperties(*initial_properties_);
    initial_properties_ = std::nullopt;
  }

  mobile_operator_info_->AddObserver(this);

  // If Cellular::Start has not been called, or Cellular::Stop has been called,
  // we still want to create the capability, but not call StartModem.
  if (state_ == State::kModemStopping || state_ == State::kDisabled)
    return;

  StartModem(base::DoNothing());

  // Update device state that might have been pending
  // due to the lack of |capability_| during Cellular::Start().
  UpdateEnabledState();
}

void Cellular::DestroyCapability() {
  SLOG(1) << LoggingTag() << ": " << __func__;

  mobile_operator_info_->RemoveObserver(this);
  // When there is a SIM swap, ModemManager destroys and creates a new modem
  // object. Reset the mobile operator info to avoid stale data.
  mobile_operator_info()->Reset();

  // Make sure we are disconnected.
  StopPPP();
  DisconnectCleanup();

  // |service_| holds a pointer to |this|. We need to disassociate it here so
  // that |this| will be destroyed if the interface is removed.
  if (service_) {
    service_->SetDevice(nullptr);
    service_ = nullptr;
  }

  capability_.reset();

  if (state_ != State::kDisabled)
    SetState(State::kEnabled);
  SetModemState(kModemStateUnknown);
}

bool Cellular::GetConnectable(CellularService* service) const {
  // Check |iccid_| in case sim_slot_properties_ have not been set.
  if (service->iccid() == iccid_)
    return true;
  // If the Service ICCID matches the ICCID in any slot, that Service can be
  // connected to (by changing the active slot if necessary).
  for (const SimProperties& sim_properties : sim_slot_properties_) {
    if (sim_properties.iccid == service->iccid())
      return true;
  }
  return false;
}

void Cellular::NotifyCellularConnectionResult(const Error& error,
                                              const std::string& iccid,
                                              bool is_user_triggered,
                                              ApnList::ApnType apn_type) {
  SLOG(3) << LoggingTag() << ": " << __func__ << ": Result: " << error.type();
  // Don't report successive failures on the same SIM when the `Connect` is
  // triggered by `AutoConnect`, and the failures are the same.
  if (error.type() != Error::kSuccess && !is_user_triggered &&
      last_cellular_connection_results_.count(iccid) > 0 &&
      error.type() == last_cellular_connection_results_[iccid]) {
    SLOG(3) << LoggingTag() << ": "
            << " Skipping repetitive failure metric. Error: "
            << error.message();
    return;
  }
  metrics()->NotifyCellularConnectionResult(error.type(),
                                            ApnTypeToMetricEnum(apn_type));
  last_cellular_connection_results_[iccid] = error.type();
  if (error.IsSuccess()) {
    return;
  }
  // used by anomaly detector for cellular subsystem crashes
  LOG(ERROR) << LoggingTag() << ": " << GetFriendlyModelId(model_id_)
             << " could not connect (trigger="
             << (is_user_triggered ? "dbus" : "auto")
             << ") to mccmnc=" << mobile_operator_info_->mccmnc() << ": "
             << error.message();
}

bool Cellular::IsSubscriptionErrorSeen() {
  return service_ && subscription_error_seen_[service_->iccid()];
}

void Cellular::NotifyNetworkValidationResult(
    std::map<std::string, std::string> apn_info,
    int portal_detection_result,
    int portal_detection_count) {
  CHECK(service_);
  auto ipv4 = CellularBearer::IPConfigMethod::kUnknown;
  auto ipv6 = CellularBearer::IPConfigMethod::kUnknown;
  uint32_t tech_used = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
  Metrics::SimType sim_type = Metrics::SimType::kUnknown;
  // TODO(b/309512268) Add support for tethering PDN.
  const ApnList::ApnType apn_type = ApnList::ApnType::kDefault;

  LOG(INFO) << LoggingTag() << ": " << __func__
            << ": portal_detection_result: " << portal_detection_result
            << " portal_detection_count: " << portal_detection_count;

  std::string roaming_state = service_->roaming_state();
  // If EID is not empty, report as eSIM else report as pSIM
  if (!service_->eid().empty())
    sim_type = Metrics::SimType::kEsim;
  else
    sim_type = Metrics::SimType::kPsim;

  if (capability_) {
    tech_used = capability_->GetActiveAccessTechnologies();
    CellularBearer* bearer = capability_->GetActiveBearer(apn_type);
    if (bearer) {
      ipv4 = bearer->ipv4_config_method();
      ipv6 = bearer->ipv6_config_method();
    }
  }

  Metrics::CellularNetworkValidationResult result;
  result.portal_detection_result = portal_detection_result;
  result.portal_detection_count = portal_detection_count;
  result.uuid = mobile_operator_info_->uuid();
  result.apn_info = apn_info;
  result.ipv4_config_method = BearerIPConfigMethodToMetrics(ipv4);
  result.ipv6_config_method = BearerIPConfigMethodToMetrics(ipv6);
  result.home_mccmnc = mobile_operator_info_->mccmnc();
  result.serving_mccmnc = mobile_operator_info_->serving_mccmnc();
  result.roaming_state = service_->roaming_state();
  result.tech_used = tech_used;
  result.sim_type = sim_type;
  // TODO(b/309512268) consider adding apn_type here if APN name
  // is not sufficient to identify tethering APN.
  metrics()->NotifyCellularNetworkValidationResult(result);
}

void Cellular::NotifyDetailedCellularConnectionResult(
    const Error& error,
    ApnList::ApnType apn_type,
    const shill::Stringmap& apn_info) {
  CHECK(service_);
  SLOG(3) << LoggingTag() << ": " << __func__ << ": Result:" << error.type();

  auto ipv4 = CellularBearer::IPConfigMethod::kUnknown;
  auto ipv6 = CellularBearer::IPConfigMethod::kUnknown;
  uint32_t tech_used = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
  uint32_t iccid_len = 0;
  Metrics::SimType sim_type = Metrics::SimType::kUnknown;
  brillo::ErrorPtr detailed_error;
  std::string cellular_error;
  bool use_apn_revamp_ui = false;
  std::string iccid = service_->iccid();

  std::string roaming_state;
  if (service_) {
    roaming_state = service_->roaming_state();
    iccid_len = service_->iccid().length();
    use_apn_revamp_ui = service_->custom_apn_list().has_value();
    // If EID is not empty, report as eSIM else report as pSIM
    sim_type = service_->eid().empty() ? Metrics::SimType::kPsim
                                       : Metrics::SimType::kEsim;
  }

  if (capability_) {
    tech_used = capability_->GetActiveAccessTechnologies();
    CellularBearer* bearer = capability_->GetActiveBearer(apn_type);
    if (bearer) {
      ipv4 = bearer->ipv4_config_method();
      ipv6 = bearer->ipv6_config_method();
    }
  }

  error.ToDetailedError(&detailed_error);
  if (detailed_error != nullptr)
    cellular_error = detailed_error->GetCode();

  SLOG(3) << LoggingTag() << ": Cellular Error:" << cellular_error;

  if (error.IsSuccess()) {
    subscription_error_seen_[iccid] = false;
  }

  Metrics::DetailedCellularConnectionResult result;
  result.error = error.type();
  result.detailed_error = cellular_error;
  result.uuid = mobile_operator_info_->uuid();
  result.apn_info = apn_info;
  result.ipv4_config_method = BearerIPConfigMethodToMetrics(ipv4);
  result.ipv6_config_method = BearerIPConfigMethodToMetrics(ipv6);
  result.home_mccmnc = mobile_operator_info_->mccmnc();
  result.serving_mccmnc = mobile_operator_info_->serving_mccmnc();
  result.roaming_state = roaming_state;
  result.use_apn_revamp_ui = use_apn_revamp_ui;
  result.tech_used = tech_used;
  result.iccid_length = iccid_len;
  result.sim_type = sim_type;
  result.gid1 = mobile_operator_info_->gid1();
  result.connection_attempt_type = ConnectionAttemptTypeToMetrics(service_);
  result.subscription_error_seen = subscription_error_seen_[iccid];
  result.modem_state = modem_state_;
  result.interface_index = interface_index();
  result.connection_apn_types = ConnectionApnTypesToMetrics(apn_type);
  metrics()->NotifyDetailedCellularConnectionResult(result);

  // Update if we reported subscription error for this card so that
  // subsequent errors report subscription_error_seen=true.
  // This is needed because when connection attempt fail with
  // serviceOptionNotSubscribed error which in most cases indicate issues
  // related to invalid APNs, subsequent connection attempts fails with
  // different error codes, making analysis of metrics difficult.
  if (IsSubscriptionError(cellular_error)) {
    subscription_error_seen_[iccid] = true;
  }
}

std::vector<Metrics::DetailedCellularConnectionResult::APNType>
Cellular::ConnectionApnTypesToMetrics(ApnList::ApnType apn_type) {
  std::vector<Metrics::DetailedCellularConnectionResult::APNType>
      connection_apn_types;
  if (default_pdn_apn_type_.has_value() &&
      default_pdn_apn_type_.value() != apn_type) {
    connection_apn_types.push_back(
        ApnTypeToMetricEnum(default_pdn_apn_type_.value()));
  }
  connection_apn_types.push_back(ApnTypeToMetricEnum(apn_type));
  return connection_apn_types;
}

void Cellular::Connect(CellularService* service, Error* error) {
  CHECK(service);
  LOG(INFO) << LoggingTag() << ": " << __func__ << ": " << service->log_name();

  const ApnList::ApnType apn_type = ApnList::ApnType::kDefault;
  if (!capability_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kWrongState,
                          "Connect Failed: Modem not available.");
    NotifyCellularConnectionResult(*error, service->iccid(),
                                   service->is_in_user_connect(), apn_type);
    return;
  }

  if (inhibited_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kWrongState,
                          "Connect Failed: Inhibited.");
    NotifyCellularConnectionResult(*error, service->iccid(),
                                   service->is_in_user_connect(), apn_type);
    return;
  }

  if (!connect_pending_iccid_.empty() &&
      connect_pending_iccid_ == service->iccid()) {
    Error error_temp = Error(Error::kWrongState, "Connect already pending.");
    LOG(WARNING) << LoggingTag() << ": " << error_temp.message();
    NotifyCellularConnectionResult(error_temp, service->iccid(),
                                   service->is_in_user_connect(), apn_type);
    return;
  }

  if (service->iccid() != iccid_) {
    // If the Service has a different ICCID than the current one, Disconnect
    // from the current Service if connected, switch to the correct SIM slot,
    // and set |connect_pending_iccid_|. The Connect will be retried after the
    // slot change completes (which may take a while).
    if (StateIsConnected())
      Disconnect(nullptr, "switching service");
    if (capability_->SetPrimarySimSlotForIccid(service->iccid())) {
      SetPendingConnect(service->iccid());
    } else {
      Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                            "Connect Failed: ICCID not available.");
      NotifyCellularConnectionResult(*error, service->iccid(),
                                     service->is_in_user_connect(), apn_type);
    }
    return;
  }

  if (scanning_) {
    LOG(INFO) << LoggingTag() << ": "
              << "Cellular is scanning. Pending connect to: "
              << service->log_name();
    SetPendingConnect(service->iccid());
    return;
  }

  if (!StateIsStarted()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Connect Failed: Modem not started.");
    NotifyCellularConnectionResult(*error, service->iccid(),
                                   service->is_in_user_connect(), apn_type);
    return;
  }

  if (StateIsConnected()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kAlreadyConnected,
                          "Already connected; connection request ignored.");
    NotifyCellularConnectionResult(*error, service->iccid(),
                                   service->is_in_user_connect(), apn_type);
    return;
  }

  if (ModemIsEnabledButNotRegistered()) {
    LOG(WARNING) << LoggingTag() << ": " << __func__
                 << ": Waiting for Modem registration.";
    SetPendingConnect(service->iccid());
    return;
  }

  if (state_ != State::kRegistered) {
    LOG(ERROR) << LoggingTag() << ": Connect attempted while state = "
               << GetStateString(state_);
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotRegistered,
                          "Connect Failed: Modem not registered.");
    NotifyCellularConnectionResult(*error, service->iccid(),
                                   service->is_in_user_connect(), apn_type);
    // If using an attach APN, send detailed metrics since |kNotRegistered| is
    // a very common error when using Attach APNs.
    if (service->GetLastAttachApn())
      NotifyDetailedCellularConnectionResult(*error, apn_type,
                                             *service->GetLastAttachApn());
    return;
  }

  if (service->IsRoamingRuleViolated()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotOnHomeNetwork,
                          "Connect Failed: Roaming disallowed.");
    NotifyCellularConnectionResult(*error, service->iccid(),
                                   service->is_in_user_connect(), apn_type);
    return;
  }

  // Build default APN list, guaranteed to never be empty.
  std::deque<Stringmap> apn_try_list = BuildDefaultApnTryList();
  if (apn_try_list.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidApn,
                          "Connect Failed: No APNs provided.");
    NotifyCellularConnectionResult(*error, service->iccid(),
                                   service->is_in_user_connect(), apn_type);
    return;
  }

  OnConnecting();
  capability_->Connect(
      apn_type, apn_try_list,
      base::BindOnce(&Cellular::OnConnectReply, weak_ptr_factory_.GetWeakPtr(),
                     apn_type, service->iccid(),
                     service->is_in_user_connect()));

  metrics()->NotifyDeviceConnectStarted(interface_index());
}

// Note that there's no ResultCallback argument to this since Connect() isn't
// yet passed one.
void Cellular::OnConnectReply(ApnList::ApnType apn_type,
                              std::string iccid,
                              bool is_user_triggered,
                              const Error& error) {
  NotifyCellularConnectionResult(error, iccid, is_user_triggered, apn_type);

  if (!error.IsSuccess()) {
    LOG(WARNING) << LoggingTag() << ": " << __func__ << ": Failed: " << error;
    if (service_ && service_->iccid() == iccid) {
      switch (error.type()) {
        case Error::kInvalidApn:
          service_->SetFailure(Service::kFailureInvalidAPN);
          break;
        case Error::kNoCarrier:
          service_->SetFailure(Service::kFailureOutOfRange);
          break;
        default:
          service_->SetFailure(Service::kFailureConnect);
          break;
      }
    }
    // If we are connecting or disconnecting DUN as DEFAULT and an error happens
    // in the reconnection procedure, the operation must be aborted right away.
    if (IsTetheringOperationDunAsDefaultOngoing()) {
      AbortTetheringOperation(error, base::DoNothing());
    }
    return;
  }

  // If we are connecting or disconnecting DUN as DEFAULT, we can now expect to
  // complete the operation once a (default) Network connection update is
  // received.
  if (IsTetheringOperationDunAsDefaultOngoing()) {
    tethering_operation_->apn_connected = true;
  }

  // Successful bearer connection, so store the APN type.
  default_pdn_apn_type_ = apn_type;

  metrics()->NotifyDeviceConnectFinished(interface_index());
  OnConnected();
}

void Cellular::OnEnabled() {
  SLOG(1) << LoggingTag() << ": " << __func__;
  manager()->AddTerminationAction(
      link_name(), base::BindOnce(&Cellular::StartTermination,
                                  weak_ptr_factory_.GetWeakPtr()));
  if (!enabled() && !enabled_pending()) {
    LOG(WARNING) << LoggingTag() << ": OnEnabled called while not enabling, "
                 << "setting enabled.";
    SetEnabled(true);
  }
}

void Cellular::OnConnecting() {
  if (service_) {
    service_->SetState(Service::kStateAssociating);
  }
}

void Cellular::Disconnect(Error* error, const char* reason) {
  SLOG(1) << LoggingTag() << ": " << __func__ << ": " << reason;
  if (!StateIsConnected()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotConnected,
                          "Not connected; request ignored.");
    return;
  }
  if (!capability_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Modem not available.");
    return;
  }

  if (IsTetheringOperationDunAsDefaultOngoing()) {
    AbortTetheringOperation(Error(Error::kOperationFailed, reason),
                            base::DoNothing());
  }
  StopPPP();
  explicit_disconnect_ = true;
  capability_->DisconnectAll(base::BindOnce(&Cellular::OnDisconnectReply,
                                            weak_ptr_factory_.GetWeakPtr()));
}

void Cellular::OnDisconnectReply(const Error& error) {
  explicit_disconnect_ = false;
  if (!error.IsSuccess()) {
    LOG(WARNING) << LoggingTag() << ": " << __func__ << ": Failed: " << error;
    OnDisconnectFailed();
    return;
  }
  OnDisconnected();
}

void Cellular::OnDisconnected() {
  SLOG(1) << LoggingTag() << ": " << __func__;

  // The logic to reconnect the tethering APN as default involves the
  // disconnection of the currently connected default APN. We explicitly ignore
  // any additional action on this case, we don't want to do a full cleanup and
  // report the full device as disconnected.
  if (IsTetheringOperationDunAsDefaultOngoing()) {
    LOG(INFO) << LoggingTag()
              << ": Disconnected during a DUN as DEFAULT tethering operation.";
    return;
  }

  // Abort multiplexed tethering operation if the default network gets
  // disconnected for any reason. The only way to abort this operation
  // is by asking MM to disconnect all bearers, as we don't know yet which is
  // the DUN specific bearer (because we're using the Simple.Connect() API).
  // This should be fine because OnDisconnected() happens when the default PDN
  // is disconnected, so we would only have the multiplexed tethering PDN
  // attempt ongoing by the time we want to DisconnectAll().
  if (IsTetheringOperationDunMultiplexedConnectOngoing()) {
    if (capability_) {
      capability_->DisconnectAll(base::DoNothing());
    }
  }

  if (!DisconnectCleanup()) {
    LOG(WARNING) << LoggingTag() << ": Disconnect occurred while in state "
                 << GetStateString(state_);
  }
}

void Cellular::OnDisconnectFailed() {
  SLOG(1) << LoggingTag() << ": " << __func__;
  // If the modem is in the disconnecting state, then the disconnect should
  // eventually succeed, so do nothing.
  if (modem_state_ == kModemStateDisconnecting) {
    LOG(INFO) << LoggingTag()
              << ": Ignoring failed disconnect while modem is disconnecting.";
    return;
  }

  // OnDisconnectFailed got called because no bearers to disconnect were found.
  // Which means that we shouldn't really remain in the connected/linked state
  // if we are in one of those.
  if (!DisconnectCleanup()) {
    // otherwise, no-op
    LOG(WARNING) << LoggingTag()
                 << ": Ignoring failed disconnect while in state "
                 << GetStateString(state_);
  }

  // TODO(armansito): In either case, shill ends up thinking that it's
  // disconnected, while for some reason the underlying modem might still
  // actually be connected. In that case the UI would be reflecting an incorrect
  // state and a further connection request would fail. We should perhaps tear
  // down the modem and restart it here.
}

bool Cellular::IsTetheringOperationDunAsDefaultOngoing() {
  return (tethering_operation_ &&
          ((tethering_operation_->type ==
            TetheringOperationType::kConnectDunAsDefaultPdn) ||
           (tethering_operation_->type ==
            TetheringOperationType::kDisconnectDunAsDefaultPdn)));
}

bool Cellular::IsTetheringOperationDunMultiplexedConnectOngoing() {
  return (tethering_operation_ &&
          (tethering_operation_->type ==
           TetheringOperationType::kConnectDunMultiplexed));
}

bool Cellular::IsTetheringOperationDunMultiplexedDisconnectOngoing() {
  return (tethering_operation_ &&
          (tethering_operation_->type ==
           TetheringOperationType::kDisconnectDunMultiplexed));
}

void Cellular::CompleteTetheringOperation(const Error& error) {
  bool dun_as_default_ongoing = IsTetheringOperationDunAsDefaultOngoing();
  bool multiplexed_dun_ongoing =
      (IsTetheringOperationDunMultiplexedConnectOngoing() ||
       IsTetheringOperationDunMultiplexedDisconnectOngoing());
  CHECK(dun_as_default_ongoing || multiplexed_dun_ongoing);

  // Reset operation info right away, as there are certain generic actions
  // updated to ignore events if a tethering operation is ongoing.
  ResultCallback callback = std::move(tethering_operation_->callback);
  bool apn_connected = tethering_operation_->apn_connected;
  tethering_operation_ = std::nullopt;

  // Report error.
  if (!error.IsSuccess()) {
    LOG(WARNING) << LoggingTag() << ": Tethering operation failed: " << error;
    std::move(callback).Run(error);
    return;
  }

  // On a successful completion of any DUN as DEFAULT operation (either
  // connect or disconnect, the APN must have been connected.
  if (dun_as_default_ongoing) {
    CHECK(apn_connected);
  }

  // If the tethering specific multiplexed Network was just connected, start
  // portal detection. Not needed when connecting DUN as DEFAULT because
  // Device::OnConnectionUpdated() already does it.
  // TODO(b/291845893): Remove this special case once the portal detection state
  // machine is entirely controlled from Network and once Device is not involved
  // anymore.
  if (multiplexed_dun_ongoing && multiplexed_tethering_pdn_) {
    // On a successful completion of a multiplexed DUN connection, the APN must
    // have been connected.
    CHECK(apn_connected);
    multiplexed_tethering_pdn_->network()->StartPortalDetection(
        Network::ValidationReason::kNetworkConnectionUpdate);
  }

  // Report success.
  LOG(INFO) << LoggingTag() << ": Tethering operation successful";
  std::move(callback).Run(Error(Error::kSuccess));
}

void Cellular::NotifyCellularConnectionResultInTetheringOperation(
    TetheringOperationType type, const Error& error) {
  // Do nothing if there is no service. We need the service to know the current
  // ICCID to include in the metrics report.
  if (!service()) {
    return;
  }

  // Connect errors will be reported in metrics exclusively on tethering
  // connection operations, which are the ones triggered for the DUN APN type.
  if (type != TetheringOperationType::kConnectDunAsDefaultPdn &&
      type != TetheringOperationType::kConnectDunMultiplexed) {
    return;
  }

  // Also worth noting, because the DUN as DEFAULT logic re-uses the generic
  // connection setup callbacks, as soon as the connection operation is launched
  // it is not expected to notify the result to metrics via this method.

  NotifyCellularConnectionResult(error, service()->iccid(),
                                 service()->is_in_user_connect(),
                                 ApnList::ApnType::kDun);
}

bool Cellular::InitializeTetheringOperation(TetheringOperationType type,
                                            ResultCallback callback) {
  // If an attempt is already ongoing, for whatever reason, fail right away.
  if (tethering_operation_) {
    Error error(Error::kWrongState, "Already ongoing.");
    NotifyCellularConnectionResultInTetheringOperation(type, error);
    dispatcher()->PostTask(FROM_HERE,
                           base::BindOnce(std::move(callback), error));
    return false;
  }

  // A capability must always exist at this point both for connections and
  // disconnections.
  if (!capability_) {
    Error error(Error::kWrongState, "No capability.");
    NotifyCellularConnectionResultInTetheringOperation(type, error);
    dispatcher()->PostTask(FROM_HERE,
                           base::BindOnce(std::move(callback), error));
    return false;
  }

  // If setting up a multiplexed tethering connection and the tethering
  // specific Network already exists, fail right away.
  if (type == TetheringOperationType::kConnectDunMultiplexed &&
      multiplexed_tethering_pdn_) {
    Error error(Error::kWrongState, "Multiplexed network already available.");
    NotifyCellularConnectionResultInTetheringOperation(type, error);
    dispatcher()->PostTask(FROM_HERE,
                           base::BindOnce(std::move(callback), error));
    return false;
  }

  // Tethering connection attempt can go on.
  LOG(INFO) << LoggingTag() << ": Tethering operation started";
  tethering_operation_.emplace(type, std::move(callback));
  tethering_operation_->apn_connected = false;
  return true;
}

void Cellular::ConnectMultiplexedTetheringPdn(
    AcquireTetheringNetworkResultCallback callback) {
  CHECK(!callback.is_null());

  LOG(INFO) << LoggingTag() << ": Tethering operation requested: "
            << "connect multiplexed DUN network.";

  if (!InitializeTetheringOperation(
          TetheringOperationType::kConnectDunMultiplexed,
          base::BindOnce(&Cellular::OnConnectMultiplexedTetheringPdnReply,
                         weak_ptr_factory_.GetWeakPtr(),
                         std::move(callback)))) {
    return;
  }

  // Will disconnect DUN as multiplexed PDN.
  tethering_operation_->apn_type = ApnList::ApnType::kDun;
  tethering_operation_->apn_try_list = BuildTetheringApnTryList();
  CHECK(!tethering_operation_->apn_try_list.empty());

  capability_->Connect(
      tethering_operation_->apn_type, tethering_operation_->apn_try_list,
      base::BindOnce(&Cellular::OnCapabilityConnectMultiplexedTetheringReply,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Cellular::OnCapabilityConnectMultiplexedTetheringReply(
    const Error& error) {
  bool bearer_connected = error.IsSuccess();

  // Report multiplexed DUN connection result metrics.
  NotifyCellularConnectionResultInTetheringOperation(
      TetheringOperationType::kConnectDunMultiplexed, error);

  // If attempt was aborted, bail out.
  if (!tethering_operation_) {
    if (bearer_connected) {
      // TODO(b/301919183): We should be able to cancel the attempt even before
      // MM has created a bearer object, otherwise we'll end up in this state.
      LOG(WARNING) << LoggingTag() << ": Multiplexed DUN bearer connected but"
                   << " no attempt ongoing";
      RunDisconnectMultiplexedTetheringPdn();
    }
    return;
  }

  // Bearer connection failed.
  if (!bearer_connected) {
    LOG(WARNING) << LoggingTag() << ": Tethering operation failed.";
    AbortTetheringOperation(error, base::DoNothing());
    return;
  }

  // If the device is disconnected or the service was lost, cleanup the
  // possibly connected bearer and complete.
  if (state_ != State::kLinked || !service() || !default_pdn_) {
    AbortTetheringOperation(
        Error(Error::kWrongState, "Default PDN must be connected"),
        base::DoNothing());
    return;
  }

  // Launch multiplexed tethering Network creation
  LOG(INFO) << LoggingTag() << ": Tethering connection attempt successful.";
  tethering_operation_->apn_connected = true;

  // Not establishing the Network link if we're testing.
  if (skip_establish_link_for_testing_) {
    return;
  }

  EstablishMultiplexedTetheringLink();
  if (!multiplexed_tethering_pdn_) {
    AbortTetheringOperation(
        Error(Error::kOperationFailed, "Multiplexed DUN bearer setup failed"),
        base::DoNothing());
    return;
  }

  // The tethering operation will be completed once the tethering network is
  // connected (or an error returned in the process).
  CHECK(!multiplexed_tethering_pdn_->network()->IsConnected());
  LOG(INFO) << LoggingTag()
            << ": Multiplexed tethering connection not fully setup yet.";
}

void Cellular::DisconnectMultiplexedTetheringPdn(ResultCallback callback) {
  CHECK(!callback.is_null());

  LOG(INFO) << LoggingTag() << ": Tethering operation requested: "
            << "disconnect multiplexed DUN network.";
  if (!InitializeTetheringOperation(
          TetheringOperationType::kDisconnectDunMultiplexed,
          std::move(callback))) {
    return;
  }

  RunDisconnectMultiplexedTetheringPdn();
}

// This method may be called either during a normal user initiated tethering
// network release procedure, or also as fallback when the tethering network
// acquisition fails. In both cases, CompleteTetheringOperation() would be
// called after the capability Disconnect().
void Cellular::RunDisconnectMultiplexedTetheringPdn() {
  multiplexed_tethering_pdn_.reset();

  LOG(INFO) << LoggingTag() << ": Disconnecting multiplexed tethering network.";
  capability_->Disconnect(
      ApnList::ApnType::kDun,
      base::BindOnce(&Cellular::OnCapabilityDisconnectMultiplexedTetheringReply,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Cellular::OnCapabilityDisconnectMultiplexedTetheringReply(
    const Error& error) {
  if (error.IsFailure()) {
    LOG(WARNING) << LoggingTag()
                 << ": Multiplexed tethering disconnection failed: " << error;
  }

  if (IsTetheringOperationDunMultiplexedDisconnectOngoing()) {
    // Disconnections are not aborted, we can simply complete.
    CompleteTetheringOperation(error);
  }
}

void Cellular::ConnectTetheringAsDefaultPdn(
    AcquireTetheringNetworkResultCallback callback) {
  CHECK(!callback.is_null());

  LOG(INFO) << LoggingTag() << ": Tethering operation requested: "
            << "connect DUN as DEFAULT network.";
  if (!InitializeTetheringOperation(
          TetheringOperationType::kConnectDunAsDefaultPdn,
          base::BindOnce(&Cellular::OnConnectTetheringAsDefaultPdnReply,
                         weak_ptr_factory_.GetWeakPtr(),
                         std::move(callback)))) {
    return;
  }

  // Will disconnect DEFAULT and connect DUN as default.
  tethering_operation_->apn_type = ApnList::ApnType::kDun;
  tethering_operation_->apn_try_list = BuildTetheringApnTryList();
  CHECK(!tethering_operation_->apn_try_list.empty());
  RunTetheringOperationDunAsDefault();
}

void Cellular::DisconnectTetheringAsDefaultPdn(ResultCallback callback) {
  CHECK(!callback.is_null());

  LOG(INFO) << LoggingTag() << ": Tethering operation requested: "
            << "disconnect DUN as DEFAULT network.";
  if (!InitializeTetheringOperation(
          TetheringOperationType::kDisconnectDunAsDefaultPdn,
          std::move(callback))) {
    return;
  }

  // Will disconnect DUN as default and connect back DEFAULT.
  tethering_operation_->apn_type = ApnList::ApnType::kDefault;
  tethering_operation_->apn_try_list = BuildDefaultApnTryList();
  CHECK(!tethering_operation_->apn_try_list.empty());
  RunTetheringOperationDunAsDefault();
}

// Both operations to connect or disconnect DUN as DEFAULT involve the same
// steps: disconnect the current default and reconnect with a new APN try list.
// This method runs the logic for both operations in the same way. The only
// notable difference is that there is no ResultCallback in the disconnect
// operation.
void Cellular::RunTetheringOperationDunAsDefault() {
  // Avoid going through the Disconnect() route because that involves a lot of
  // cleanups that we shouldn't be doing while reconnecting with the tethering
  // specific APNs. Instead, run our own disconnection logic, starting with
  // the disconnection of all bearers (there should be one only either way).
  explicit_disconnect_ = true;
  capability_->DisconnectAll(
      base::BindOnce(&Cellular::OnCapabilityDisconnectBeforeReconnectReply,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Cellular::OnCapabilityDisconnectBeforeReconnectReply(const Error& error) {
  explicit_disconnect_ = false;

  // If tethering operation was aborted already, do nothing.
  if (!tethering_operation_) {
    return;
  }

  // A failure in the disconnection is assumed fatal.
  if (!error.IsSuccess()) {
    AbortTetheringOperation(error, base::DoNothing());
    return;
  }

  // If service lost while attempt ongoing, abort right away.
  if (!service()) {
    AbortTetheringOperation(
        Error(Error::kWrongState, "Tethering operation failed: no service."),
        base::DoNothing());
    return;
  }

  // Not a full cleanup, but we do transition the Service state out of a
  // connected state, so that clients can rearrange their connections.
  SetPrimaryMultiplexedInterface("");
  default_pdn_apn_type_ = std::nullopt;
  default_pdn_.reset();

  SetServiceState(Service::kStateAssociating);

  // We trigger a capability connect using a specific APN try list (which may
  // e.g. be the DUN-specific try list). The generic OnConnectReply() is used so
  // that it is treated as a standard connection attempt. From now on, the
  // tethering operation will only be completed once the newly connected Network
  // has been started.
  capability_->Connect(
      tethering_operation_->apn_type, tethering_operation_->apn_try_list,
      base::BindOnce(&Cellular::OnConnectReply, weak_ptr_factory_.GetWeakPtr(),
                     tethering_operation_->apn_type, service()->iccid(),
                     false /* is_in_user_connect */));

  metrics()->NotifyDeviceConnectStarted(interface_index());
}

void Cellular::ReuseDefaultPdnForTethering(
    AcquireTetheringNetworkResultCallback callback) {
  CHECK(!callback.is_null());
  dispatcher()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), GetPrimaryNetwork(),
                                Error(Error::kSuccess)));
}

Cellular::TetheringOperationType Cellular::GetTetheringOperationType(
    bool experimental_tethering, Error* out_error) {
  Error error(Error::kSuccess);
  if (!service_) {
    error = Error(Error::kWrongState, "No service.");
  } else if (!capability_) {
    error = Error(Error::kWrongState, "No modem.");
  } else if (inhibited_) {
    error = Error(Error::kWrongState, "Inhibited.");
  } else if (state_ != State::kLinked) {
    error = Error(Error::kWrongState, "Default connection not available.");
  } else if (!mobile_operator_info_->tethering_allowed(
                 experimental_tethering)) {
    error = Error(Error::kWrongState, "Not allowed by operator.");
  }

  if (error.IsFailure()) {
    if (out_error)
      *out_error = error;
    return TetheringOperationType::kFailed;
  }

  std::deque<Stringmap> tethering_apn_try_list = BuildTetheringApnTryList();

  // No other APN is specified for tethering, we can reuse the DEFAULT for
  // tethering as fallback.
  if (tethering_apn_try_list.empty()) {
    // This is a database error, an operator that flags "use_dun_apn_as_default"
    // must also provide a separate APN of type DUN.
    if (mobile_operator_info_->use_dun_apn_as_default()) {
      if (out_error) {
        *out_error = Error(
            Error::kWrongState,
            "Operator requires DUN APN as DEFAULT but no DUN APN configured");
      }
      return TetheringOperationType::kFailed;
    }

    LOG(INFO)
        << LoggingTag()
        << ": Tethering network selection: reusing default APN for tethering "
           "as there is no DUN specific APN";
    return TetheringOperationType::kReuseDefaultPdn;
  }

  // The currently connected APN is also flagged as DUN. If this APN is also in
  // the list of tethering APNs, we can reuse it. This additional check is done
  // to ensure that a user-defined APN doesn't override the DUN APN explicitly
  // required by the operator (i.e. is_required_by_carrier_spec).
  const Stringmap* last_good_apn_info = service_->GetLastGoodApn();
  if (last_good_apn_info && ApnList::IsTetheringApn(*last_good_apn_info) &&
      std::find(tethering_apn_try_list.begin(), tethering_apn_try_list.end(),
                *last_good_apn_info) != tethering_apn_try_list.end()) {
    LOG(INFO)
        << LoggingTag()
        << ": Tethering network selection: reusing default APN for tethering.";
    return TetheringOperationType::kReuseDefaultPdn;
  }

  // A different APN is specified for tethering, and the operator requires the
  // DUN APN to be used also as DEFAULT when tethering is enabled, so we must
  // disconnect DEFAULT and reconnect DUN as DEFAULT.
  if (mobile_operator_info_->use_dun_apn_as_default()) {
    LOG(INFO) << LoggingTag()
              << ": Tethering network selection: "
                 "connecting DUN APN as default as required by operator.";
    return TetheringOperationType::kConnectDunAsDefaultPdn;
  }

  // A different APN is specified for tethering, and the modem doesn't support
  // multiplexing, so we must disconnect DEFAULT and reconnect DUN as DEFAULT.
  if (!GetMultiplexSupport()) {
    LOG(INFO)
        << LoggingTag()
        << ": Tethering network selection: "
           "connecting DUN APN as default as multiplexing is unsupported.";
    return TetheringOperationType::kConnectDunAsDefaultPdn;
  }

  // Connect DUN APN as additional multiplexed network
  LOG(INFO) << LoggingTag()
            << ": Tethering network selection: "
               "connecting multiplexed DUN APN.";
  return TetheringOperationType::kConnectDunMultiplexed;
}

void Cellular::OnAcquireTetheringNetworkReady(
    AcquireTetheringNetworkResultCallback callback,
    Network* network,
    const Error& error) {
  SLOG(3) << __func__;
  if (!error.IsSuccess()) {
    tethering_event_callback_.Reset();
  }
  std::move(callback).Run(network, error);
}

void Cellular::AcquireTetheringNetwork(
    TetheringManager::UpdateTimeoutCallback update_timeout_callback,
    AcquireTetheringNetworkResultCallback callback,
    TetheringManager::CellularUpstreamEventCallback tethering_event_callback,
    bool experimental_tethering) {
  SLOG(1) << LoggingTag() << ": " << __func__;
  CHECK(!callback.is_null());
  CHECK(!tethering_event_callback.is_null());
  tethering_event_callback_ = std::move(tethering_event_callback);
  auto internal_callback =
      base::BindOnce(&Cellular::OnAcquireTetheringNetworkReady,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback));
  Error error;
  switch (GetTetheringOperationType(experimental_tethering, &error)) {
    case TetheringOperationType::kConnectDunMultiplexed:
      ConnectMultiplexedTetheringPdn(std::move(internal_callback));
      return;
    case TetheringOperationType::kConnectDunAsDefaultPdn:
      // Request a longer start timeout as we need to go through a full
      // PDN connection setup sequence.
      update_timeout_callback.Run(kLongTetheringStartTimeout);
      ConnectTetheringAsDefaultPdn(std::move(internal_callback));
      return;
    case TetheringOperationType::kReuseDefaultPdn:
      ReuseDefaultPdnForTethering(std::move(internal_callback));
      return;
    case TetheringOperationType::kFailed:
      dispatcher()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(internal_callback), nullptr, error));
      return;
    case TetheringOperationType::kDisconnectDunAsDefaultPdn:
    case TetheringOperationType::kDisconnectDunMultiplexed:
      // Not a valid return of GetTetheringOperationType().
      NOTREACHED();
  }
}

void Cellular::OnConnectTetheringAsDefaultPdnReply(
    AcquireTetheringNetworkResultCallback callback, const Error& error) {
  if (error.IsFailure()) {
    LOG(WARNING) << LoggingTag()
                 << ": Tethering network selection: failed to connect DUN APN "
                    "as default: "
                 << error;
    std::move(callback).Run(nullptr, error);
    return;
  }

  LOG(INFO) << LoggingTag()
            << ": Tethering network selection: connected DUN APN as default.";
  CHECK(default_pdn_);
  std::move(callback).Run(default_pdn_->network(), Error(Error::kSuccess));
}

void Cellular::OnConnectMultiplexedTetheringPdnReply(
    AcquireTetheringNetworkResultCallback callback, const Error& error) {
  if (error.IsFailure()) {
    LOG(WARNING) << LoggingTag()
                 << ": Tethering network selection: failed to connect "
                    "multiplexed DUN APN: "
                 << error;
    std::move(callback).Run(nullptr, error);
    return;
  }

  LOG(INFO) << LoggingTag()
            << ": Tethering network selection: connected multiplexed DUN APN.";
  CHECK(multiplexed_tethering_pdn_);
  std::move(callback).Run(multiplexed_tethering_pdn_->network(),
                          Error(Error::kSuccess));
}

void Cellular::AbortTetheringOperation(const Error& error,
                                       ResultCallback callback) {
  if (!tethering_operation_) {
    LOG(ERROR) << LoggingTag() << ": no ongoing tethering operation to abort.";
    dispatcher()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), Error(Error::kSuccess)));
    return;
  }

  LOG(INFO) << LoggingTag() << ": Tethering operation aborted: " << error;

  // The abort logic is exclusively used during a connection attempt, because it
  // allows us to go back to the default state without tethering enabled. There
  // is no point in trying to abort a tethering disconnection attempt.
  if (tethering_operation_->type ==
          TetheringOperationType::kDisconnectDunMultiplexed ||
      tethering_operation_->type ==
          TetheringOperationType::kDisconnectDunAsDefaultPdn) {
    LOG(WARNING) << LoggingTag() << ": Ignoring tethering abort request while"
                 << " disconnecting operation is ongoing.";
    dispatcher()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), Error(Error::kSuccess)));
    return;
  }

  // Keep track of which operation was being done.
  bool dun_as_default_ongoing = IsTetheringOperationDunAsDefaultOngoing();
  bool multiplexed_dun_ongoing =
      IsTetheringOperationDunMultiplexedConnectOngoing();

  // Abort the original connection attempt.
  LOG(INFO) << LoggingTag()
            << ": Completing tethering connect during abort sequence.";
  CompleteTetheringOperation(error);

  // Launch a new disconnection attempt. The disconnection logic needs to
  // be able to recover the state with tethering disabled from any point in the
  // logic.
  LOG(INFO) << LoggingTag()
            << ": Launching tethering disconnect during abort sequence.";
  if (dun_as_default_ongoing) {
    DisconnectTetheringAsDefaultPdn(std::move(callback));
  } else if (multiplexed_dun_ongoing) {
    DisconnectMultiplexedTetheringPdn(std::move(callback));
  } else {
    NOTREACHED_NORETURN();
  }
}

void Cellular::ReleaseTetheringNetwork(Network* network,
                                       ResultCallback callback) {
  SLOG(1) << LoggingTag() << ": " << __func__;
  CHECK(!callback.is_null());
  tethering_event_callback_.Reset();
  // No explicit network given, which means there is an ongoing tethering
  // network acquisition operation that needs to be aborted.
  if (!network) {
    AbortTetheringOperation(Error(Error::kOperationAborted, "Aborted."),
                            std::move(callback));
    return;
  }

  // If we connected the tethering APN as default, we need to disconnect it and
  // reconnect with the default APN.
  if (default_pdn_apn_type_ &&
      *default_pdn_apn_type_ == ApnList::ApnType::kDun) {
    // Validate that the network requested to disconnect is the one we expect.
    if (!default_pdn_ || default_pdn_->network() != network) {
      dispatcher()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(callback),
                         Error(Error::kWrongState,
                               "Unexpected default network to release")));
      return;
    }
    DisconnectTetheringAsDefaultPdn(std::move(callback));
    return;
  }

  // If we connected a multiplexed tethering APN, disconnect it here.
  if (multiplexed_tethering_pdn_) {
    // Validate that the network requested to disconnect is the one we expect.
    if (multiplexed_tethering_pdn_->network() != network) {
      dispatcher()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(callback),
                         Error(Error::kWrongState,
                               "Unexpected multiplexed network to release")));
      return;
    }
    DisconnectMultiplexedTetheringPdn(std::move(callback));
    return;
  }

  // We had reused the default PDN, so nothing to do.
  dispatcher()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), Error(Error::kSuccess)));
}

void Cellular::EstablishLink() {
  if (skip_establish_link_for_testing_) {
    return;
  }

  SLOG(2) << LoggingTag() << ": " << __func__;
  CHECK_EQ(State::kConnected, state_);
  CHECK(capability_);

  if (!default_pdn_apn_type_) {
    LOG(WARNING) << LoggingTag() << ": Disconnecting due to missing APN type.";
    Disconnect(nullptr, "missing APN type");
    return;
  }

  CellularBearer* bearer = capability_->GetActiveBearer(*default_pdn_apn_type_);
  if (!bearer) {
    LOG(WARNING) << LoggingTag()
                 << ": Disconnecting due to missing active bearer.";
    Disconnect(nullptr, "missing active bearer");
    return;
  }

  // The APN type is ensured to be one by GetActiveBearer()
  CHECK_EQ(bearer->apn_types().size(), 1UL);
  CHECK_EQ(bearer->apn_types()[0], *default_pdn_apn_type_);

  if (bearer->ipv4_config_method() == CellularBearer::IPConfigMethod::kPPP) {
    LOG(INFO) << LoggingTag() << ": Start PPP connection on "
              << bearer->data_interface();
    StartPPP(bearer->data_interface());
    return;
  }

  // ModemManager specifies which is the network interface that has been
  // connected at this point, which may be either the same interface that was
  // used to reference this Cellular device, or a completely different one.
  LOG(INFO) << LoggingTag() << ": Establish link on "
            << bearer->data_interface();

  // Create default network
  default_pdn_ = std::make_unique<NetworkInfo>(
      this, bearer->dbus_path(),
      rtnl_handler()->GetInterfaceIndex(bearer->data_interface()),
      bearer->data_interface());

  // Start the link listener, which will ensure the initial link state for the
  // data interface is notified.
  StartLinkListener();

  // Set state to associating.
  OnConnecting();
}

void Cellular::EstablishMultiplexedTetheringLink() {
  CHECK_EQ(State::kLinked, state_);
  CHECK(capability_);

  // The multiplexed DUN bearer selection only works if the current default PDN
  // APN type is not DUN. This should be ensured by the tethering enablement
  // logic, so we can assert the assumption.
  CHECK(default_pdn_apn_type_);
  CHECK_NE(*default_pdn_apn_type_, ApnList::ApnType::kDun);

  // Do nothing if there is no tethering bearer to setup.
  CellularBearer* bearer = capability_->GetActiveBearer(ApnList::ApnType::kDun);
  if (!bearer) {
    return;
  }

  SLOG(2) << LoggingTag() << ": " << __func__;

  // The APN type is ensured to be one by GetActiveBearer()
  CHECK_EQ(bearer->apn_types().size(), 1UL);
  CHECK_EQ(bearer->apn_types()[0], ApnList::ApnType::kDun);

  if (bearer->ipv4_config_method() == CellularBearer::IPConfigMethod::kPPP) {
    LOG(WARNING) << LoggingTag() << ": No PPP support for tethering link";
    return;
  }

  LOG(INFO) << LoggingTag() << ": Establish tethering link on "
            << bearer->data_interface();

  // Create multiplexed tethering network
  multiplexed_tethering_pdn_ = std::make_unique<NetworkInfo>(
      this, bearer->dbus_path(),
      rtnl_handler()->GetInterfaceIndex(bearer->data_interface()),
      bearer->data_interface());

  // Start the link listener, which will ensure the initial link state for the
  // data interface is notified.
  StartLinkListener();

  // Unlike with the default PDN, we don't update the device state in any way at
  // this point. The multiplexed DUN acquisition operation will continue with
  // the link up event.
}

void Cellular::DefaultLinkUp() {
  if (default_pdn_->link_state() == LinkState::kUp) {
    SLOG(3) << LoggingTag() << ": Default link is up.";
    return;
  }

  default_pdn_->SetLinkState(LinkState::kUp);
  LOG(INFO) << LoggingTag() << ": Default link is up: configuring network";

  CHECK(capability_);

  if (!default_pdn_apn_type_) {
    LOG(INFO) << LoggingTag() << ": Default link APN type unknown";
    Disconnect(nullptr, "missing default link APN type.");
    return;
  }

  if (!default_pdn_->Configure(
          capability_->GetActiveBearer(*default_pdn_apn_type_))) {
    LOG(INFO) << LoggingTag() << ": Default link network configuration failed";
    Disconnect(nullptr, "link configuration failed.");
    return;
  }

  SetPrimaryMultiplexedInterface(default_pdn_->network()->interface_name());
  SetState(State::kLinked);

  // When a tethering operation to connect or disconnect DUN as DEFAULT is
  // ongoing we just update the attached network.
  if (IsTetheringOperationDunAsDefaultOngoing()) {
    ResetServiceAttachedNetwork();
  } else {
    SelectService(service_);
  }
  SetServiceState(Service::kStateConfiguring);

  default_pdn_->Start();
}

void Cellular::DefaultLinkDown() {
  if (explicit_disconnect_) {
    SLOG(3) << LoggingTag() << ": Default link is down during disconnection";
    return;
  }

  LinkState old_state = default_pdn_->link_state();
  default_pdn_->SetLinkState(LinkState::kDown);

  // LinkState::kUnknown is the initial state before the first dump
  if (old_state == LinkState::kUnknown) {
    LOG(INFO) << LoggingTag() << ": Default link is down, bringing up.";
    rtnl_handler()->SetInterfaceFlags(
        default_pdn_->network()->interface_index(), IFF_UP, IFF_UP);
    return;
  }

  if (old_state == LinkState::kUp) {
    LOG(INFO) << LoggingTag() << ": Default link is down, disconnecting.";
    Disconnect(nullptr, "link is down.");
    return;
  }

  SLOG(3) << LoggingTag() << ": Default link is down.";
}

void Cellular::DefaultLinkDeleted() {
  LOG(INFO) << LoggingTag() << ": Default link is deleted.";
  default_pdn_->SetLinkState(LinkState::kUnknown);

  // If not multiplexing, this is an indication that the cellular device is gone
  // from the system. If multiplexing, just a no-op.
  if (default_pdn_->network()->interface_index() == interface_index()) {
    DestroyAllServices();
  }
}

void Cellular::MultiplexedTetheringLinkUp() {
  if (multiplexed_tethering_pdn_->link_state() == LinkState::kUp) {
    SLOG(3) << LoggingTag() << ": Multiplexed tethering link is up.";
    return;
  }

  multiplexed_tethering_pdn_->SetLinkState(LinkState::kUp);
  LOG(INFO) << LoggingTag()
            << ": Multiplexed tethering link is up: configuring network";

  CHECK(capability_);

  // The multiplexed DUN bearer selection only works if the current default PDN
  // APN type is not DUN. This should be ensured by the tethering enablement
  // logic, so we can assert the assumption.
  CHECK(default_pdn_apn_type_);
  CHECK_NE(*default_pdn_apn_type_, ApnList::ApnType::kDun);

  if (!multiplexed_tethering_pdn_->Configure(
          capability_->GetActiveBearer(ApnList::ApnType::kDun))) {
    LOG(INFO) << LoggingTag()
              << ": Multiplexed tethering link network configuration failed";
    if (IsTetheringOperationDunMultiplexedConnectOngoing()) {
      AbortTetheringOperation(
          Error(Error::kOperationFailed, "Link configuration failed."),
          base::DoNothing());
      return;
    }
  }

  LOG(INFO) << LoggingTag()
            << ": Multiplexed tethering network configuration ready.";

  multiplexed_tethering_pdn_->Start();
  LOG(INFO) << LoggingTag() << ": Multiplexed tethering network started.";

  // Network not connected yet, need to wait for OnConnectionUpdated().
  CHECK(!multiplexed_tethering_pdn_->network()->IsConnected());
}

void Cellular::MultiplexedTetheringLinkDown() {
  LinkState old_state = multiplexed_tethering_pdn_->link_state();
  multiplexed_tethering_pdn_->SetLinkState(LinkState::kDown);

  // LinkState::kUnknown is the initial state before the first dump
  if (old_state == LinkState::kUnknown) {
    LOG(INFO) << LoggingTag()
              << ": Multiplexed tethering link is down, bringing up.";
    rtnl_handler()->SetInterfaceFlags(
        multiplexed_tethering_pdn_->network()->interface_index(), IFF_UP,
        IFF_UP);
    return;
  }

  if (old_state == LinkState::kUp) {
    LOG(INFO) << LoggingTag()
              << ": Multiplexed tethering link is down, disconnecting.";
    RunDisconnectMultiplexedTetheringPdn();
    return;
  }

  SLOG(3) << LoggingTag() << ": Multiplexed tethering link is down.";
}

void Cellular::MultiplexedTetheringLinkDeleted() {
  LOG(INFO) << LoggingTag() << ": Multiplexed tethering link is deleted.";
  multiplexed_tethering_pdn_->SetLinkState(LinkState::kUnknown);
}

void Cellular::LinkMsgHandler(const net_base::RTNLMessage& msg) {
  DCHECK(msg.type() == net_base::RTNLMessage::kTypeLink);

  int data_interface_index = msg.interface_index();

  // Actions on the default APN Network
  if (default_pdn_ &&
      data_interface_index == default_pdn_->network()->interface_index()) {
    if (msg.mode() == net_base::RTNLMessage::kModeDelete) {
      DefaultLinkDeleted();
    } else if (msg.mode() == net_base::RTNLMessage::kModeAdd) {
      if (msg.link_status().flags & IFF_UP) {
        DefaultLinkUp();
      } else {
        DefaultLinkDown();
      }
    } else {
      LOG(WARNING) << LoggingTag()
                   << ": Unexpected link message mode: " << msg.mode();
    }
  }

  // Actions on the tethering APN Network
  if (multiplexed_tethering_pdn_ &&
      data_interface_index ==
          multiplexed_tethering_pdn_->network()->interface_index()) {
    if (msg.mode() == net_base::RTNLMessage::kModeDelete) {
      MultiplexedTetheringLinkDeleted();
    } else if (msg.mode() == net_base::RTNLMessage::kModeAdd) {
      if (msg.link_status().flags & IFF_UP) {
        MultiplexedTetheringLinkUp();
      } else {
        MultiplexedTetheringLinkDown();
      }
    } else {
      LOG(WARNING) << LoggingTag()
                   << ": Unexpected link message mode: " << msg.mode();
    }
  }
}

void Cellular::StopLinkListener() {
  link_listener_.reset(nullptr);
}

void Cellular::StartLinkListener() {
  SLOG(2) << LoggingTag() << ": Started RTNL listener";
  if (!link_listener_) {
    link_listener_ = std::make_unique<net_base::RTNLListener>(
        net_base::RTNLHandler::kRequestLink,
        base::BindRepeating(&Cellular::LinkMsgHandler, base::Unretained(this)));
  }
  rtnl_handler()->RequestDump(net_base::RTNLHandler::kRequestLink);
}

void Cellular::SetInitialProperties(const InterfaceToProperties& properties) {
  if (!capability_) {
    LOG(WARNING) << LoggingTag() << ": SetInitialProperties with no Capability";
    initial_properties_ = properties;
    return;
  }
  capability_->SetInitialProperties(properties);
}

void Cellular::OnModemStateChanged(ModemState new_state) {
  ModemState old_modem_state = modem_state_;
  if (old_modem_state == new_state) {
    SLOG(3) << LoggingTag()
            << ": The new state matches the old state. Nothing to do.";
    return;
  }

  SLOG(1) << LoggingTag() << ": " << __func__
          << ": State: " << GetStateString(state_)
          << " ModemState: " << GetModemStateString(new_state);
  SetModemState(new_state);
  CHECK(capability_);

  if (old_modem_state >= kModemStateRegistered &&
      modem_state_ < kModemStateRegistered) {
    if (state_ == State::kModemStarting) {
      // Avoid un-registering the modem while the Capability is starting the
      // Modem to prevent unexpected spurious state changes.
      // TODO(stevenjb): Audit logs and remove or tighten this logic.
      LOG(WARNING) << LoggingTag()
                   << ": Modem state change while capability starting, "
                   << " ModemState: " << GetModemStateString(new_state);
    } else {
      capability_->SetUnregistered(modem_state_ == kModemStateSearching);
      HandleNewRegistrationState();
    }
  }

  if (old_modem_state < kModemStateEnabled &&
      modem_state_ >= kModemStateEnabled) {
    // Just became enabled, update enabled state.
    OnEnabled();
  }

  // Ignore state change actions while we're reconnecting the tethering APN
  // as default network.
  if (IsTetheringOperationDunAsDefaultOngoing()) {
    SLOG(1) << LoggingTag() << ": " << __func__
            << ": ignoring actions upon new state: tethering attempt ongoing";
    return;
  }

  switch (modem_state_) {
    case kModemStateFailed:
    case kModemStateUnknown:
    case kModemStateInitializing:
    case kModemStateLocked:
      break;
    case kModemStateDisabled:
      // When the Modem becomes disabled, Cellular is not necessarily disabled.
      // This may occur after a SIM swap or eSIM profile change. Ensure that
      // the Modem is started.
      if (state_ == State::kEnabled)
        StartModem(base::DoNothing());
      break;
    case kModemStateDisabling:
    case kModemStateEnabling:
      break;
    case kModemStateEnabled:
    case kModemStateSearching:
    case kModemStateRegistered:
      if (old_modem_state == kModemStateConnected ||
          old_modem_state == kModemStateConnecting ||
          old_modem_state == kModemStateDisconnecting) {
        OnDisconnected();
      }
      break;
    case kModemStateDisconnecting:
    case kModemStateConnecting:
      break;
    case kModemStateConnected:
      // Even if the modem state transitions from Connecting to Connected here
      // we don't report the cellular object as Connected yet; we require the
      // actual connection attempt operation to finish.
      break;
  }
}

bool Cellular::IsActivating() const {
  return capability_ && capability_->IsActivating();
}

bool Cellular::GetPolicyAllowRoaming(Error* /*error*/) {
  return policy_allow_roaming_;
}

bool Cellular::SetPolicyAllowRoaming(const bool& value, Error* error) {
  if (policy_allow_roaming_ == value)
    return false;

  LOG(INFO) << LoggingTag() << ": " << __func__ << ": " << policy_allow_roaming_
            << "->" << value;

  policy_allow_roaming_ = value;
  adaptor()->EmitBoolChanged(kCellularPolicyAllowRoamingProperty, value);
  manager()->UpdateDevice(this);

  if (service_ && service_->IsRoamingRuleViolated()) {
    Disconnect(nullptr, "policy updated: roaming rule violated");
  }

  return true;
}

bool Cellular::SetUseAttachApn(const bool& value, Error* error) {
  LOG(INFO) << __func__;
  // |use_attach_apn_ | is deprecated. its default value should be true.
  if (!value)
    return false;

  return true;
}

bool Cellular::GetInhibited(Error* error) {
  return inhibited_;
}

bool Cellular::SetInhibited(const bool& inhibited, Error* error) {
  if (inhibited == inhibited_) {
    LOG(WARNING) << LoggingTag() << ": " << __func__
                 << ": State already set, ignoring request.";
    return false;
  }
  LOG(INFO) << LoggingTag() << ": " << __func__ << ": " << inhibited;

  // Clear any pending connect when inhibited changes.
  SetPendingConnect(std::string());

  inhibited_ = inhibited;

  // Update and emit Scanning before Inhibited. This allows the UI to wait for
  // Scanning to be false once Inhibit changes to know when an Inhibit operation
  // completes. UpdateScanning will call ConnectToPending if Scanning is false.
  UpdateScanning();
  adaptor()->EmitBoolChanged(kInhibitedProperty, inhibited_);

  return true;
}

KeyValueStore Cellular::GetSimLockStatus(Error* error) {
  if (!capability_) {
    // modemmanager might be inhibited or restarting.
    LOG(WARNING) << LoggingTag() << ": " << __func__
                 << ": Called with null capability.";
    return KeyValueStore();
  }
  return capability_->SimLockStatusToProperty(error);
}

void Cellular::SetSimPresent(bool sim_present) {
  if (sim_present_ == sim_present)
    return;

  sim_present_ = sim_present;
  adaptor()->EmitBoolChanged(kSIMPresentProperty, sim_present_);
}

void Cellular::StartTermination() {
  SLOG(2) << LoggingTag() << ": " << __func__;
  OnBeforeSuspend(base::BindOnce(&Cellular::OnTerminationCompleted,
                                 weak_ptr_factory_.GetWeakPtr()));
}

void Cellular::OnTerminationCompleted(const Error& error) {
  LOG(INFO) << LoggingTag() << ": " << __func__ << ": " << error;
  manager()->TerminationActionComplete(link_name());
  manager()->RemoveTerminationAction(link_name());
}

bool Cellular::DisconnectCleanup() {
  SLOG(2) << LoggingTag() << ": " << __func__;
  DestroySockets();
  if (!StateIsConnected())
    return false;
  StopLinkListener();
  SetState(State::kRegistered);
  SetServiceFailureSilent(Service::kFailureNone);
  SetPrimaryMultiplexedInterface("");
  default_pdn_apn_type_ = std::nullopt;
  default_pdn_.reset();
  multiplexed_tethering_pdn_.reset();
  ResetCarrierEntitlement();
  return true;
}

void Cellular::ResetCarrierEntitlement() {
  carrier_entitlement_->Reset();
  if (!entitlement_check_callback_.is_null()) {
    std::move(entitlement_check_callback_)
        .Run(TetheringManager::EntitlementStatus::kNotAllowed);
  }
}

// static
void Cellular::LogRestartModemResult(const Error& error) {
  if (error.IsSuccess()) {
    LOG(INFO) << "Modem restart completed.";
  } else {
    LOG(WARNING) << "Attempt to restart modem failed: " << error;
  }
}

bool Cellular::ResetQ6V5Modem() {
  base::FilePath modem_reset_path = GetQ6V5ModemResetPath();
  if (!base::PathExists(modem_reset_path)) {
    PLOG(ERROR) << LoggingTag()
                << ": Unable to find sysfs file to reset modem.";
    return false;
  }

  int fd = HANDLE_EINTR(open(modem_reset_path.value().c_str(),
                             O_WRONLY | O_NONBLOCK | O_CLOEXEC));
  if (fd < 0) {
    PLOG(ERROR) << LoggingTag()
                << ": Failed to open sysfs file to reset modem.";
    return false;
  }

  base::ScopedFD scoped_fd(fd);
  if (!base::WriteFileDescriptor(scoped_fd.get(), "stop")) {
    PLOG(ERROR) << LoggingTag() << ": Failed to stop modem";
    return false;
  }
  usleep(kModemResetTimeout.InMicroseconds());
  if (!base::WriteFileDescriptor(scoped_fd.get(), "start")) {
    PLOG(ERROR) << LoggingTag() << ": Failed to start modem";
    return false;
  }
  return true;
}

base::FilePath Cellular::GetQ6V5ModemResetPath() {
  base::FilePath modem_reset_path, driver_path;

  base::FileEnumerator it(
      base::FilePath(kQ6V5SysfsBasePath), false,
      base::FileEnumerator::FILES | base::FileEnumerator::SHOW_SYM_LINKS,
      kQ6V5RemoteprocPattern);
  for (base::FilePath name = it.Next(); !name.empty(); name = it.Next()) {
    if (base::ReadSymbolicLink(name.Append("device/driver"), &driver_path) &&
        driver_path.BaseName() == base::FilePath(kQ6V5DriverName)) {
      modem_reset_path = name.Append("state");
      break;
    }
  }

  return modem_reset_path;
}

bool Cellular::IsQ6V5Modem() {
  // Check if manufacturer is equal to "QUALCOMM INCORPORATED" and
  // if one of the remoteproc[0-9]/device/driver in sysfs links
  // to "qcom-q6v5-mss".
  return (manufacturer_ == kQ6V5ModemManufacturerName &&
          base::PathExists(GetQ6V5ModemResetPath()));
}

void Cellular::StartPPP(const std::string& serial_device) {
  SLOG(2) << LoggingTag() << ": " << __func__ << ": on " << serial_device;
  // Detach any SelectedService from this device. It will be grafted onto
  // the PPPDevice after PPP is up (in Cellular::Notify).
  //
  // This has two important effects: 1) kills dhcpcd if it is running.
  // 2) stops Cellular::LinkEvent from driving changes to the
  // SelectedService.
  if (selected_service()) {
    CHECK_EQ(service_.get(), selected_service().get());
    // Save and restore |service_| state, as DropConnection calls
    // SelectService, and SelectService will move selected_service()
    // to State::kIdle.
    Service::ConnectState original_state(service_->state());
    DropConnectionDefault();  // Don't redirect to PPPDevice.
    service_->SetState(original_state);
  } else {
    // There should be no regular cellular network connected
    DCHECK(!default_pdn_);
  }

  PPPDaemon::DeathCallback death_callback(
      base::BindOnce(&Cellular::OnPPPDied, weak_ptr_factory_.GetWeakPtr()));

  PPPDaemon::Options options;
  options.no_detach = true;
  options.no_default_route = true;
  options.use_peer_dns = true;
  options.max_fail = 1;

  is_ppp_authenticating_ = false;

  Error error;
  std::unique_ptr<ExternalTask> new_ppp_task(PPPDaemon::Start(
      control_interface(), process_manager_, weak_ptr_factory_.GetWeakPtr(),
      options, serial_device, std::move(death_callback), &error));
  if (new_ppp_task) {
    SLOG(1) << LoggingTag() << ": Forked pppd process.";
    ppp_task_ = std::move(new_ppp_task);
  }
}

void Cellular::StopPPP() {
  SLOG(2) << LoggingTag() << ": " << __func__;
  if (!ppp_device_)
    return;
  DropConnection();
  ppp_task_.reset();
  ppp_device_ = nullptr;
}

// called by |ppp_task_|
void Cellular::GetLogin(std::string* user, std::string* password) {
  SLOG(2) << LoggingTag() << ": " << __func__;
  if (!service()) {
    LOG(ERROR) << LoggingTag() << ": " << __func__ << ": with no service ";
    return;
  }
  CHECK(user);
  CHECK(password);
  *user = service()->ppp_username();
  *password = service()->ppp_password();
}

// Called by |ppp_task_|.
void Cellular::Notify(const std::string& reason,
                      const std::map<std::string, std::string>& dict) {
  SLOG(2) << LoggingTag() << ": " << __func__ << ": " << reason;

  if (reason == kPPPReasonAuthenticating) {
    OnPPPAuthenticating();
  } else if (reason == kPPPReasonAuthenticated) {
    OnPPPAuthenticated();
  } else if (reason == kPPPReasonConnect) {
    OnPPPConnected(dict);
  } else if (reason == kPPPReasonDisconnect) {
    // Ignore; we get disconnect information when pppd exits.
  } else if (reason == kPPPReasonExit) {
    // Ignore; we get its exit status by the death callback for PPPDaemon.
  } else {
    NOTREACHED();
  }
}

void Cellular::OnPPPAuthenticated() {
  SLOG(2) << LoggingTag() << ": " << __func__;
  is_ppp_authenticating_ = false;
}

void Cellular::OnPPPAuthenticating() {
  SLOG(2) << LoggingTag() << ": " << __func__;
  is_ppp_authenticating_ = true;
}

bool Cellular::GetForceInitEpsBearerSettings() {
  return force_init_eps_bearer_settings_;
}

void Cellular::SetForceInitEpsBearerSettings(bool force) {
  SLOG(2) << LoggingTag() << ": " << __func__ << " force: " << std::boolalpha
          << force;
  force_init_eps_bearer_settings_ = force;
}

void Cellular::OnPPPConnected(
    const std::map<std::string, std::string>& params) {
  SLOG(2) << LoggingTag() << ": " << __func__;
  std::string interface_name = PPPDaemon::GetInterfaceName(params);
  DeviceInfo* device_info = manager()->device_info();
  int interface_index = device_info->GetIndex(interface_name);
  if (interface_index < 0) {
    // TODO(quiche): Consider handling the race when the RTNL notification about
    // the new PPP device has not been received yet. crbug.com/246832.
    NOTIMPLEMENTED() << ": No device info for " << interface_name << ".";
    return;
  }

  if (!ppp_device_ || ppp_device_->interface_index() != interface_index) {
    if (ppp_device_) {
      ppp_device_->SelectService(nullptr);  // No longer drives |service_|.
      // Destroy the existing device before creating a new one to avoid the
      // possibility of multiple DBus Objects with the same interface name.
      // See https://crbug.com/1032030 for details.
      ppp_device_ = nullptr;
    }
    ppp_device_ = device_info->CreatePPPDevice(manager(), interface_name,
                                               interface_index);
    device_info->RegisterDevice(ppp_device_);
  }

  CHECK(service_);
  // For PPP, we only SelectService on the |ppp_device_|.
  CHECK(!selected_service());
  ppp_device_->SetEnabled(true);
  ppp_device_->SelectService(service_);

  auto properties = std::make_unique<IPConfig::Properties>(
      PPPDaemon::ParseIPConfiguration(params));
  ppp_device_->UpdateIPConfig(std::move(properties), nullptr);
}

void Cellular::OnPPPDied(pid_t pid, int exit) {
  SLOG(1) << LoggingTag() << ": " << __func__;
  ppp_task_.reset();
  if (is_ppp_authenticating_) {
    SetServiceFailure(Service::kFailurePPPAuth);
  } else {
    SetServiceFailure(PPPDaemon::ExitStatusToFailure(exit));
  }
  Disconnect(nullptr, "unexpected pppd exit");
}

bool Cellular::ModemIsEnabledButNotRegistered() {
  // Normally the Modem becomes Registered immediately after becoming enabled.
  // In cases where we have an attach APN or eSIM this may not be true. See
  // b/204847937 and b/205882451 for more details.
  // TODO(b/186482862): Fix this behavior in ModemManager.
  return (state_ == State::kEnabled || state_ == State::kModemStarting ||
          state_ == State::kModemStarted) &&
         modem_state_ == kModemStateEnabled;
}

void Cellular::SetPendingConnect(const std::string& iccid) {
  if (iccid == connect_pending_iccid_)
    return;

  if (!connect_pending_iccid_.empty()) {
    SLOG(1) << LoggingTag()
            << ": Cancelling pending connect to: " << connect_pending_iccid_;
    ConnectToPendingFailed(Service::kFailureDisconnect);
  }

  connect_pending_callback_.Cancel();
  connect_pending_iccid_ = iccid;

  if (iccid.empty())
    return;

  SLOG(1) << LoggingTag() << ": Set Pending connect: " << iccid;
  // Pending connect requests may fail, e.g. a SIM slot change may fail or
  // registration may fail for an inactive eSIM profile. Set a timeout to
  // cancel the pending connect and inform the UI.
  connect_cancel_callback_.Reset(base::BindOnce(
      &Cellular::ConnectToPendingCancel, weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostDelayedTask(FROM_HERE, connect_cancel_callback_.callback(),
                                kPendingConnectCancel);
}

void Cellular::ConnectToPending() {
  if (connect_pending_iccid_.empty() ||
      !connect_pending_callback_.IsCancelled()) {
    return;
  }

  if (inhibited_) {
    SLOG(1) << LoggingTag() << ": " << __func__ << ": Inhibited";
    return;
  }
  if (scanning_) {
    SLOG(1) << LoggingTag() << ": " << __func__ << ": Scanning";
    return;
  }

  if (modem_state_ == kModemStateLocked) {
    // Check the lock type and set the failure appropriately
    KeyValueStore sim_lock_status = GetSimLockStatus(nullptr);
    std::string lock_type =
        sim_lock_status.Get<std::string>(kSIMLockTypeProperty);
    if (lock_type == kSIMLockNetworkPin)
      ConnectToPendingFailed(Service::kFailureSimCarrierLocked);
    else
      ConnectToPendingFailed(Service::kFailureSimLocked);
    return;
  }

  if (ModemIsEnabledButNotRegistered()) {
    LOG(WARNING) << LoggingTag() << ": " << __func__
                 << ": Waiting for Modem registration.";
    return;
  }

  if (!StateIsRegistered()) {
    LOG(WARNING) << LoggingTag() << ": " << __func__
                 << ": Cellular not registered, State: "
                 << GetStateString(state_);
    ConnectToPendingFailed(Service::kFailureNotRegistered);
    return;
  }
  if (modem_state_ != kModemStateRegistered) {
    LOG(WARNING) << LoggingTag() << ": " << __func__
                 << ": Modem not registered, State: "
                 << GetModemStateString(modem_state_);
    ConnectToPendingFailed(Service::kFailureNotRegistered);
    return;
  }

  SLOG(1) << LoggingTag() << ": " << __func__ << ": " << connect_pending_iccid_;
  connect_cancel_callback_.Cancel();
  connect_pending_callback_.Reset(base::BindOnce(
      &Cellular::ConnectToPendingAfterDelay, weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostDelayedTask(FROM_HERE, connect_pending_callback_.callback(),
                                kPendingConnectDelay);
}

void Cellular::ConnectToPendingAfterDelay() {
  SLOG(1) << LoggingTag() << ": " << __func__ << ": " << connect_pending_iccid_;

  std::string pending_iccid;
  if (connect_pending_iccid_ == kUnknownIccid) {
    // Connect to the current iccid if we want to connect to an unknown
    // iccid. This usually occurs when the inactive slot's iccid is unknown, but
    // we want to connect to it after a slot switch.
    pending_iccid = iccid_;
  } else {
    pending_iccid = connect_pending_iccid_;
  }

  // Clear pending connect request regardless of whether a service is found.
  connect_pending_iccid_.clear();

  CellularServiceRefPtr service =
      manager()->cellular_service_provider()->FindService(pending_iccid);
  if (!service) {
    LOG(WARNING) << LoggingTag()
                 << ": No matching service for pending connect.";
    return;
  }

  Error error;
  LOG(INFO) << LoggingTag() << ": Connecting to pending Cellular Service: "
            << service->log_name();
  service->Connect(&error, "Pending connect");
  if (!error.IsSuccess())
    service->SetFailure(Service::kFailureDelayedConnectSetup);
}

void Cellular::ConnectToPendingFailed(Service::ConnectFailure failure) {
  if (!connect_pending_iccid_.empty()) {
    SLOG(1) << LoggingTag() << ": " << __func__ << ": "
            << connect_pending_iccid_
            << " Failure: " << Service::ConnectFailureToString(failure);
    CellularServiceRefPtr service =
        manager()->cellular_service_provider()->FindService(
            connect_pending_iccid_);
    bool is_user_triggered = false;
    if (service) {
      service->SetFailure(failure);
      is_user_triggered = service->is_in_user_connect();
    }
    // populate the error for the sake of metrics
    Error error;
    switch (failure) {
      case Service::kFailureNotRegistered:
        error.Populate(Error::kNotRegistered);
        break;
      case Service::kFailureDisconnect:
        error.Populate(Error::kOperationAborted);
        break;
      case Service::kFailureSimLocked:
        error.Populate(Error::kPinRequired);
        break;
      default:
        error.Populate(Error::kOperationFailed);
        break;
    }
    NotifyCellularConnectionResult(std::move(error), connect_pending_iccid_,
                                   is_user_triggered,
                                   ApnList::ApnType::kDefault);
  }
  connect_cancel_callback_.Cancel();
  connect_pending_callback_.Cancel();
  connect_pending_iccid_.clear();
}

void Cellular::ConnectToPendingCancel() {
  LOG(WARNING) << LoggingTag() << ": " << __func__;
  ConnectToPendingFailed(Service::kFailureNotRegistered);
}

void Cellular::UpdateScanning() {
  bool scanning;
  switch (state_) {
    case State::kDisabled:
      scanning = false;
      break;
    case State::kEnabled:
      // Cellular is enabled, but the Modem object has not been created, or was
      // destroyed because the Modem is Inhibited or Locked, or StartModem
      // failed.
      scanning = !inhibited_ && modem_state_ != kModemStateLocked &&
                 modem_state_ != kModemStateFailed;
      break;
    case State::kModemStarting:
    case State::kModemStopping:
      scanning = true;
      break;
    case State::kModemStarted:
    case State::kRegistered:
    case State::kConnected:
    case State::kLinked:
      // When the modem is started and enabling or searching, treat as scanning.
      // Also set scanning if an active scan is in progress.
      scanning = modem_state_ == kModemStateEnabling ||
                 modem_state_ == kModemStateSearching ||
                 proposed_scan_in_progress_;
      break;
  }
  SetScanning(scanning);
}

void Cellular::RegisterProperties() {
  PropertyStore* store = this->mutable_store();

  // These properties do not have setters, and events are not generated when
  // they are changed.
  store->RegisterConstString(kDBusServiceProperty, &dbus_service_);
  store->RegisterConstString(kDBusObjectProperty, &dbus_path_str_);

  store->RegisterUint16(kScanIntervalProperty, &scan_interval_);

  // These properties have setters that should be used to change their values.
  // Events are generated whenever the values change.
  store->RegisterConstStringmap(kHomeProviderProperty, &home_provider_);
  store->RegisterConstBool(kSupportNetworkScanProperty, &scanning_supported_);
  store->RegisterConstString(kEidProperty, &eid_);
  store->RegisterConstString(kEsnProperty, &esn_);
  store->RegisterConstString(kFirmwareRevisionProperty, &firmware_revision_);
  store->RegisterConstString(kHardwareRevisionProperty, &hardware_revision_);
  store->RegisterConstString(kImeiProperty, &imei_);
  store->RegisterConstString(kImsiProperty, &imsi_);
  store->RegisterConstString(kMdnProperty, &mdn_);
  store->RegisterConstString(kMeidProperty, &meid_);
  store->RegisterConstString(kMinProperty, &min_);
  store->RegisterConstString(kManufacturerProperty, &manufacturer_);
  store->RegisterConstString(kModelIdProperty, &model_id_);
  store->RegisterConstString(kEquipmentIdProperty, &equipment_id_);
  store->RegisterConstBool(kScanningProperty, &scanning_);

  store->RegisterConstString(kSelectedNetworkProperty, &selected_network_);
  store->RegisterConstStringmaps(kFoundNetworksProperty, &found_networks_);
  store->RegisterConstBool(kProviderRequiresRoamingProperty,
                           &provider_requires_roaming_);
  store->RegisterConstBool(kSIMPresentProperty, &sim_present_);
  store->RegisterConstKeyValueStores(kSIMSlotInfoProperty, &sim_slot_info_);
  store->RegisterConstStringmaps(kCellularApnListProperty, &apn_list_);
  store->RegisterConstString(kIccidProperty, &iccid_);
  store->RegisterConstString(kPrimaryMultiplexedInterfaceProperty,
                             &primary_multiplexed_interface_);

  HelpRegisterConstDerivedString(kTechnologyFamilyProperty,
                                 &Cellular::GetTechnologyFamily);
  HelpRegisterConstDerivedString(kDeviceIdProperty, &Cellular::GetDeviceId);
  HelpRegisterDerivedBool(kCellularPolicyAllowRoamingProperty,
                          &Cellular::GetPolicyAllowRoaming,
                          &Cellular::SetPolicyAllowRoaming);
  // TODO(b/277792069): Remove when Chrome removes the attach APN code.
  HelpRegisterDerivedBool(kUseAttachAPNProperty, &Cellular::GetUseAttachApn,
                          &Cellular::SetUseAttachApn);
  HelpRegisterDerivedBool(kInhibitedProperty, &Cellular::GetInhibited,
                          &Cellular::SetInhibited);

  store->RegisterDerivedKeyValueStore(
      kSIMLockStatusProperty,
      KeyValueStoreAccessor(new CustomAccessor<Cellular, KeyValueStore>(
          this, &Cellular::GetSimLockStatus, /*error=*/nullptr)));
}

void Cellular::UpdateModemProperties(const RpcIdentifier& dbus_path,
                                     const std::string& mac_address) {
  if (dbus_path_ == dbus_path) {
    SLOG(1) << LoggingTag() << ": " << __func__
            << ": Skipping update. Same dbus_path provided: "
            << dbus_path.value();
    return;
  }
  LOG(INFO) << LoggingTag() << ": " << __func__
            << ": Modem Path: " << dbus_path.value();
  SetDbusPath(dbus_path);
  SetModemState(kModemStateUnknown);
  set_mac_address(mac_address);
  CreateCapability();
}

const std::string& Cellular::GetSimCardId() const {
  if (!eid_.empty())
    return eid_;
  return iccid_;
}

bool Cellular::HasIccid(const std::string& iccid) const {
  if (iccid == iccid_)
    return true;
  for (const SimProperties& sim_properties : sim_slot_properties_) {
    if (sim_properties.iccid == iccid) {
      return true;
    }
  }
  return false;
}

void Cellular::SetSimProperties(
    const std::vector<SimProperties>& sim_properties, size_t primary_slot) {
  LOG(INFO) << LoggingTag() << ": " << __func__
            << ": Slots: " << sim_properties.size()
            << " Primary: " << primary_slot;
  if (sim_properties.empty()) {
    // This might occur while the Modem is starting.
    SetPrimarySimProperties(SimProperties());
    SetSimSlotProperties(sim_properties, 0);
    return;
  }
  if (primary_slot >= sim_properties.size()) {
    LOG(ERROR) << LoggingTag() << ": Invalid Primary Slot Id: " << primary_slot;
    primary_slot = 0u;
  }

  const SimProperties& primary_sim_properties = sim_properties[primary_slot];

  // Update SIM properties for the primary SIM slot and create or update the
  // primary Service.
  SetPrimarySimProperties(primary_sim_properties);

  // Update the KeyValueStore for Device.Cellular.SIMSlotInfo and emit it.
  SetSimSlotProperties(sim_properties, static_cast<int>(primary_slot));

  // Ensure that secondary services are created and updated.
  UpdateSecondaryServices();
}

void Cellular::OnProfilesChanged() {
  if (!service_) {
    LOG(ERROR) << LoggingTag()
               << ": 3GPP profiles were updated with no service.";
    return;
  }

  // Rebuild the APN try list.
  OnOperatorChanged();

  if (!StateIsConnected()) {
    return;
  }

  LOG(INFO) << LoggingTag() << ": Reconnecting for OTA profile update.";
  Disconnect(nullptr, "OTA profile update");
  SetPendingConnect(service_->iccid());
}

bool Cellular::CompareApns(const Stringmap& apn1, const Stringmap& apn2) const {
  static const std::string always_ignore_keys[] = {
      cellular::kApnVersionProperty, kApnNameProperty,
      kApnLanguageProperty,          kApnSourceProperty,
      kApnLocalizedNameProperty,     kApnIsRequiredByCarrierSpecProperty};
  std::set<std::string> ignore_keys{std::begin(always_ignore_keys),
                                    std::end(always_ignore_keys)};

  // Enforce the APN keys so that developers explicitly define the behavior
  // for each key in this function.
  static const std::string only_allowed_keys[] = {
      kApnProperty,         kApnTypesProperty,          kApnUsernameProperty,
      kApnPasswordProperty, kApnAuthenticationProperty, kApnIpTypeProperty,
      kApnAttachProperty};
  std::set<std::string> allowed_keys{std::begin(only_allowed_keys),
                                     std::end(only_allowed_keys)};
  for (auto const& pair : apn1) {
    if (ignore_keys.count(pair.first))
      continue;

    DCHECK(allowed_keys.count(pair.first)) << " key: " << pair.first;
    if (!base::Contains(apn2, pair.first) || pair.second != apn2.at(pair.first))
      return false;
    // Keys match, ignore them below.
    ignore_keys.insert(pair.first);
  }
  // Find keys in apn2 which are not in apn1.
  for (auto const& pair : apn2) {
    DCHECK(allowed_keys.count(pair.first) || ignore_keys.count(pair.first))
        << " key: " << pair.first;
    if (ignore_keys.count(pair.first) == 0)
      return false;
  }
  return true;
}

std::deque<Stringmap> Cellular::BuildAttachApnTryList() const {
  std::deque<Stringmap> try_list = BuildApnTryList(ApnList::ApnType::kAttach);
  if (try_list.size() > 1) {
    // When multiple Attach APNs are present, shill should fall back to the
    // default one(first in the list) if all of them fail to register.
    try_list.emplace_back(try_list.front());
    PrintApnListForDebugging(try_list, ApnList::ApnType::kAttach);
  }
  return try_list;
}

std::deque<Stringmap> Cellular::BuildDefaultApnTryList() const {
  return BuildApnTryList(ApnList::ApnType::kDefault);
}

std::deque<Stringmap> Cellular::BuildTetheringApnTryList() const {
  return BuildApnTryList(ApnList::ApnType::kDun);
}

bool Cellular::IsRequiredByCarrierApn(const Stringmap& apn) const {
  // Only check the property in MODB APNs to avoid getting into a situation in
  // which the UI or user send the property by mistake and the UI cannot
  // update the APN list because there is an existing APN with the property
  // set to true.
  return base::Contains(apn, kApnSourceProperty) &&
         apn.at(kApnSourceProperty) == cellular::kApnSourceMoDb &&
         base::Contains(apn, kApnIsRequiredByCarrierSpecProperty) &&
         apn.at(kApnIsRequiredByCarrierSpecProperty) ==
             kApnIsRequiredByCarrierSpecTrue;
}

bool Cellular::RequiredApnExists(ApnList::ApnType apn_type) const {
  for (auto apn : apn_list_) {
    if (ApnList::IsApnType(apn, apn_type) && IsRequiredByCarrierApn(apn))
      return true;
  }
  return false;
}

std::deque<Stringmap> Cellular::BuildApnTryList(
    ApnList::ApnType apn_type) const {
  std::deque<Stringmap> apn_try_list;
  // When a required APN exists, no other APNs of that type will be included in
  // the try list.
  bool modb_required_apn_exists = RequiredApnExists(apn_type);
  // If a required APN exists, the last good APN is not added.
  bool add_last_good_apn = !modb_required_apn_exists;
  std::vector<const Stringmap*> custom_apns_info;
  const Stringmap* custom_apn_info = nullptr;
  const Stringmap* last_good_apn_info = nullptr;
  const Stringmaps* custom_apn_list = nullptr;
  // Add custom APNs(from UI or Admin)
  if (!modb_required_apn_exists && service_) {
    if (service_->custom_apn_list().has_value()) {
      // The LastGoodAPN is no longer used in the try list when the APN Revamp
      // is enabled.
      add_last_good_apn = false;
      custom_apn_list = &service_->custom_apn_list().value();
      for (const auto& custom_apn : *custom_apn_list) {
        if (ApnList::IsApnType(custom_apn, apn_type))
          custom_apns_info.emplace_back(&custom_apn);
      }
    } else if (service_->GetUserSpecifiedApn() &&
               ApnList::IsApnType(*service_->GetUserSpecifiedApn(), apn_type)) {
      custom_apn_info = service_->GetUserSpecifiedApn();
      custom_apns_info.emplace_back(custom_apn_info);
    }

    last_good_apn_info = service_->GetLastGoodApn();
    for (auto custom_apn : custom_apns_info) {
      apn_try_list.push_back(*custom_apn);
      if (!base::Contains(apn_try_list.back(), kApnSourceProperty))
        apn_try_list.back()[kApnSourceProperty] = kApnSourceUi;

      SLOG(3) << LoggingTag() << ": " << __func__
              << ": Adding User Specified APN: "
              << GetPrintableApnStringmap(apn_try_list.back());
      if ((last_good_apn_info &&
           CompareApns(*last_good_apn_info, apn_try_list.back()))) {
        add_last_good_apn = false;
      }
    }
  }
  // - With the revamp APN UI, if the user has entered an APN in the UI, only
  // customs APNs are used. Return early.
  // - For the old UI, the Attach APN round robin is skipped if there is a
  // custom attach APN.
  if ((custom_apn_list && custom_apn_list->size() > 0) ||
      (!custom_apn_list && custom_apn_info &&
       apn_type == ApnList::ApnType::kAttach)) {
    PrintApnListForDebugging(apn_try_list, apn_type);
    ValidateApnTryList(apn_try_list);
    return apn_try_list;
  }
  // Ensure all Modem APNs are added before MODB APNs.
  for (auto apn : apn_list_) {
    if (!ApnList::IsApnType(apn, apn_type))
      continue;
    DCHECK(base::Contains(apn, kApnSourceProperty));
    // Verify all APNs are either from the Modem or MODB.
    DCHECK(apn[kApnSourceProperty] == cellular::kApnSourceModem ||
           apn[kApnSourceProperty] == cellular::kApnSourceMoDb);
    if (apn[kApnSourceProperty] != cellular::kApnSourceModem)
      continue;
    apn_try_list.push_back(apn);
  }
  // Add MODB APNs and update the origin of the custom APN(only for old UI).
  int index_of_first_modb_apn = apn_try_list.size();
  for (const auto& apn : apn_list_) {
    if (!ApnList::IsApnType(apn, apn_type) ||
        (modb_required_apn_exists && !IsRequiredByCarrierApn(apn)))
      continue;
    // Updating the origin of the custom APN is only needed for the old UI,
    // since the APN UI revamp will include the correct APN source.
    if (!custom_apn_list && custom_apn_info &&
        CompareApns(*custom_apn_info, apn) &&
        base::Contains(apn, kApnSourceProperty)) {
      // If |custom_apn_info| is not null, it is located at the first position
      // of |apn_try_list|, and we update the APN source for it.
      apn_try_list[0][kApnSourceProperty] = apn.at(kApnSourceProperty);
      continue;
    }

    bool is_same_as_last_good_apn =
        last_good_apn_info && CompareApns(*last_good_apn_info, apn);
    if (is_same_as_last_good_apn)
      add_last_good_apn = false;

    if (base::Contains(apn, kApnSourceProperty) &&
        apn.at(kApnSourceProperty) == cellular::kApnSourceMoDb) {
      if (is_same_as_last_good_apn) {
        apn_try_list.insert(apn_try_list.begin() + index_of_first_modb_apn,
                            apn);
      } else {
        apn_try_list.push_back(apn);
      }
    }
  }
  // Add fallback empty APN as a last try for Default and Attach
  if (apn_type == ApnList::ApnType::kDefault ||
      apn_type == ApnList::ApnType::kAttach) {
    bool is_same_as_last_good_apn = false;
    std::deque<Stringmap> empty_apn_list = BuildFallbackEmptyApn(apn_type);
    for (const auto& apn : empty_apn_list) {
      apn_try_list.push_back(apn);
      if (last_good_apn_info) {
        is_same_as_last_good_apn |= CompareApns(*last_good_apn_info, apn);
      }
    }
    if (is_same_as_last_good_apn)
      add_last_good_apn = false;
  }
  // The last good APN will be a last-ditch effort to connect in case the APN
  // list is misconfigured somehow.
  if (last_good_apn_info && add_last_good_apn &&
      ApnList::IsApnType(*last_good_apn_info, apn_type)) {
    apn_try_list.push_back(*last_good_apn_info);
    LOG(INFO) << LoggingTag() << ": " << __func__ << ": Adding last good APN: "
              << GetPrintableApnStringmap(*last_good_apn_info);
  }

  PrintApnListForDebugging(apn_try_list, apn_type);
  ValidateApnTryList(apn_try_list);
  return apn_try_list;
}

void Cellular::SetScanningSupported(bool scanning_supported) {
  if (scanning_supported_ == scanning_supported)
    return;

  scanning_supported_ = scanning_supported;
  adaptor()->EmitBoolChanged(kSupportNetworkScanProperty, scanning_supported_);
}

void Cellular::SetEquipmentId(const std::string& equipment_id) {
  if (equipment_id_ == equipment_id)
    return;

  equipment_id_ = equipment_id;
  adaptor()->EmitStringChanged(kEquipmentIdProperty, equipment_id_);
}

void Cellular::SetEsn(const std::string& esn) {
  if (esn_ == esn)
    return;

  esn_ = esn;
  adaptor()->EmitStringChanged(kEsnProperty, esn_);
}

void Cellular::SetFirmwareRevision(const std::string& firmware_revision) {
  if (firmware_revision_ == firmware_revision)
    return;

  firmware_revision_ = firmware_revision;
  adaptor()->EmitStringChanged(kFirmwareRevisionProperty, firmware_revision_);
}

void Cellular::SetHardwareRevision(const std::string& hardware_revision) {
  if (hardware_revision_ == hardware_revision)
    return;

  hardware_revision_ = hardware_revision;
  adaptor()->EmitStringChanged(kHardwareRevisionProperty, hardware_revision_);
}

void Cellular::SetDeviceId(std::unique_ptr<DeviceId> device_id) {
  device_id_ = std::move(device_id);
  if (!device_id_) {
    SLOG(2) << "device_id: {}";
    modem_type_ = ModemType::kUnknown;
    return;
  }
  SLOG(2) << "device_id: " << device_id_->AsString();
  if (device_id_->Match(DeviceId(cellular::kL850GLBusType, cellular::kL850GLVid,
                                 cellular::kL850GLPid))) {
    modem_type_ = ModemType::kL850GL;
  } else if (device_id_->Match(DeviceId(cellular::kFM101BusType,
                                        cellular::kFM101Vid,
                                        cellular::kFM101Pid))) {
    modem_type_ = ModemType::kFM101;
  } else if (device_id_->Match(DeviceId(cellular::kFM350BusType,
                                        cellular::kFM350Vid,
                                        cellular::kFM350Pid))) {
    modem_type_ = ModemType::kFM350;
  } else {
    modem_type_ = ModemType::kOther;
  }
}

void Cellular::SetImei(const std::string& imei) {
  if (imei_ == imei)
    return;

  imei_ = imei;
  adaptor()->EmitStringChanged(kImeiProperty, imei_);
}

void Cellular::SetPrimarySimProperties(const SimProperties& sim_properties) {
  SLOG(1) << LoggingTag() << ": " << __func__ << ": EID= " << sim_properties.eid
          << " ICCID= " << sim_properties.iccid
          << " IMSI= " << sim_properties.imsi
          << " OperatorId= " << sim_properties.operator_id
          << " ServiceProviderName= " << sim_properties.spn
          << " GID1= " << sim_properties.gid1;

  eid_ = sim_properties.eid;
  iccid_ = sim_properties.iccid;
  imsi_ = sim_properties.imsi;

  mobile_operator_info()->UpdateMCCMNC(sim_properties.operator_id);
  mobile_operator_info()->UpdateOperatorName(sim_properties.spn);
  mobile_operator_info()->UpdateICCID(iccid_);
  if (!imsi_.empty()) {
    mobile_operator_info()->UpdateIMSI(imsi_);
  }
  if (!sim_properties.gid1.empty()) {
    mobile_operator_info()->UpdateGID1(sim_properties.gid1);
  }

  adaptor()->EmitStringChanged(kEidProperty, eid_);
  adaptor()->EmitStringChanged(kIccidProperty, iccid_);
  adaptor()->EmitStringChanged(kImsiProperty, imsi_);
  SetSimPresent(!iccid_.empty());

  // Ensure Service creation once SIM properties are set.
  UpdateServices();
}

void Cellular::SetSimSlotProperties(
    const std::vector<SimProperties>& slot_properties, int primary_slot) {
  if (sim_slot_properties_ == slot_properties &&
      primary_sim_slot_ == primary_slot) {
    return;
  }
  SLOG(1) << LoggingTag() << ": " << __func__
          << ": Slots: " << slot_properties.size()
          << " Primary: " << primary_slot;
  sim_slot_properties_ = slot_properties;
  if (primary_sim_slot_ != primary_slot) {
    primary_sim_slot_ = primary_slot;
  }
  // Set |sim_slot_info_| and emit SIMSlotInfo
  sim_slot_info_.clear();
  for (int i = 0; i < static_cast<int>(slot_properties.size()); ++i) {
    const SimProperties& sim_properties = slot_properties[i];
    KeyValueStore properties;
    properties.Set(kSIMSlotInfoEID, sim_properties.eid);
    properties.Set(kSIMSlotInfoICCID, sim_properties.iccid);
    bool is_primary = i == primary_slot;
    properties.Set(kSIMSlotInfoPrimary, is_primary);
    sim_slot_info_.push_back(properties);
    SLOG(2) << LoggingTag() << ": " << __func__
            << ": Slot: " << sim_properties.slot
            << " EID: " << sim_properties.eid
            << " ICCID: " << sim_properties.iccid << " Primary: " << is_primary;
  }
  adaptor()->EmitKeyValueStoresChanged(kSIMSlotInfoProperty, sim_slot_info_);
}

void Cellular::SetMdn(const std::string& mdn) {
  if (mdn_ == mdn)
    return;

  mdn_ = mdn;
  adaptor()->EmitStringChanged(kMdnProperty, mdn_);
}

void Cellular::SetMeid(const std::string& meid) {
  if (meid_ == meid)
    return;

  meid_ = meid;
  adaptor()->EmitStringChanged(kMeidProperty, meid_);
}

void Cellular::SetMin(const std::string& min) {
  if (min_ == min)
    return;

  min_ = min;
  adaptor()->EmitStringChanged(kMinProperty, min_);
}

void Cellular::SetManufacturer(const std::string& manufacturer) {
  if (manufacturer_ == manufacturer)
    return;

  manufacturer_ = manufacturer;
  adaptor()->EmitStringChanged(kManufacturerProperty, manufacturer_);
}

void Cellular::SetModelId(const std::string& model_id) {
  if (model_id_ == model_id)
    return;

  model_id_ = model_id;
  adaptor()->EmitStringChanged(kModelIdProperty, model_id_);
}

void Cellular::SetMMPlugin(const std::string& mm_plugin) {
  mm_plugin_ = mm_plugin;
}

void Cellular::SetMaxActiveMultiplexedBearers(
    uint32_t max_multiplexed_bearers) {
  max_multiplexed_bearers_ = max_multiplexed_bearers;
}

bool Cellular::IsModemFM350() {
  SLOG(2) << LoggingTag() << ": " << __func__ << " : " << std::boolalpha
          << (modem_type_ == ModemType::kFM350);
  return modem_type_ == ModemType::kFM350;
}

bool Cellular::IsModemFM101() {
  SLOG(2) << LoggingTag() << ": " << __func__ << " : " << std::boolalpha
          << (modem_type_ == ModemType::kFM101);
  return modem_type_ == ModemType::kFM101;
}

bool Cellular::IsModemL850GL() {
  SLOG(2) << LoggingTag() << ": " << __func__ << " : " << std::boolalpha
          << (modem_type_ == ModemType::kL850GL);
  return modem_type_ == ModemType::kL850GL;
}

void Cellular::StartLocationPolling() {
  CHECK(capability_);
  if (!capability_->IsLocationUpdateSupported()) {
    SLOG(2) << LoggingTag() << ": Location polling not enabled for "
            << mm_plugin_ << " plugin.";
    return;
  }

  if (polling_location_)
    return;

  polling_location_ = true;

  CHECK(poll_location_task_.IsCancelled());
  SLOG(2) << LoggingTag() << ": " << __func__
          << ": Starting location polling tasks.";

  // Schedule an immediate task
  poll_location_task_.Reset(base::BindOnce(&Cellular::PollLocationTask,
                                           weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostTask(FROM_HERE, poll_location_task_.callback());
}

void Cellular::StopLocationPolling() {
  if (!polling_location_)
    return;
  polling_location_ = false;

  if (!poll_location_task_.IsCancelled()) {
    SLOG(2) << LoggingTag() << ": " << __func__
            << ": Cancelling outstanding timeout.";
    poll_location_task_.Cancel();
  }
}

void Cellular::SetDbusPath(const shill::RpcIdentifier& dbus_path) {
  dbus_path_ = dbus_path;
  dbus_path_str_ = dbus_path.value();
  adaptor()->EmitStringChanged(kDBusObjectProperty, dbus_path_str_);
}

void Cellular::SetScanning(bool scanning) {
  if (scanning_ == scanning)
    return;
  LOG(INFO) << LoggingTag() << ": " << __func__ << ": " << scanning
            << " State: " << GetStateString(state_)
            << " Modem State: " << GetModemStateString(modem_state_);
  if (scanning) {
    // Set Scanning=true immediately.
    SetScanningProperty(true);
    return;
  }
  // If the modem is disabled, set Scanning=false immediately.
  // A delayed clear in this case might hit after the service is destroyed.
  if (state_ == State::kDisabled) {
    SetScanningProperty(false);
    return;
  }
  // Delay Scanning=false to delay operations while the Modem is starting.
  // TODO(b/177588333): Make Modem and/or the MM dbus API more robust.
  if (!scanning_clear_callback_.IsCancelled())
    return;

  SLOG(2) << LoggingTag() << ": " << __func__ << ": Delaying clear";
  scanning_clear_callback_.Reset(base::BindOnce(
      &Cellular::SetScanningProperty, weak_ptr_factory_.GetWeakPtr(), false));
  dispatcher()->PostDelayedTask(FROM_HERE, scanning_clear_callback_.callback(),
                                kModemResetTimeout);
}

void Cellular::SetScanningProperty(bool scanning) {
  SLOG(2) << LoggingTag() << ": " << __func__ << ": " << scanning;
  if (!scanning_clear_callback_.IsCancelled())
    scanning_clear_callback_.Cancel();
  scanning_ = scanning;
  adaptor()->EmitBoolChanged(kScanningProperty, scanning_);

  if (scanning)
    metrics()->NotifyDeviceScanStarted(interface_index());
  else
    metrics()->NotifyDeviceScanFinished(interface_index());

  if (!scanning_)
    ConnectToPending();
}

void Cellular::SetSelectedNetwork(const std::string& selected_network) {
  if (selected_network_ == selected_network)
    return;

  selected_network_ = selected_network;
  adaptor()->EmitStringChanged(kSelectedNetworkProperty, selected_network_);
}

void Cellular::SetFoundNetworks(const Stringmaps& found_networks) {
  // There is no canonical form of a Stringmaps value.
  // So don't check for redundant updates.
  found_networks_ = found_networks;
  adaptor()->EmitStringmapsChanged(kFoundNetworksProperty, found_networks_);
}

void Cellular::SetPrimaryMultiplexedInterface(
    const std::string& interface_name) {
  if (primary_multiplexed_interface_ == interface_name) {
    return;
  }

  primary_multiplexed_interface_ = interface_name;
  adaptor()->EmitStringChanged(kPrimaryMultiplexedInterfaceProperty,
                               primary_multiplexed_interface_);
}

void Cellular::SetProviderRequiresRoaming(bool provider_requires_roaming) {
  if (provider_requires_roaming_ == provider_requires_roaming)
    return;

  provider_requires_roaming_ = provider_requires_roaming;
  adaptor()->EmitBoolChanged(kProviderRequiresRoamingProperty,
                             provider_requires_roaming_);
}

bool Cellular::IsRoamingAllowed() {
  return service_ && service_->IsRoamingAllowed();
}

PowerOpt* Cellular::power_opt() {
  return manager()->power_opt();
}

void Cellular::SetApnList(const Stringmaps& apn_list) {
  // There is no canonical form of a Stringmaps value, so don't check for
  // redundant updates.
  apn_list_ = apn_list;
  adaptor()->EmitStringmapsChanged(kCellularApnListProperty, apn_list_);
}

void Cellular::UpdateHomeProvider() {
  SLOG(2) << LoggingTag() << ": " << __func__;

  Stringmap home_provider;
  auto AssignIfNotEmpty = [&](const std::string& key,
                              const std::string& value) {
    if (!value.empty())
      home_provider[key] = value;
  };

  if (mobile_operator_info_->IsMobileNetworkOperatorKnown()) {
    AssignIfNotEmpty(kOperatorCodeKey, mobile_operator_info_->mccmnc());
    AssignIfNotEmpty(kOperatorNameKey, mobile_operator_info_->operator_name());
    AssignIfNotEmpty(kOperatorCountryKey, mobile_operator_info_->country());
    AssignIfNotEmpty(kOperatorUuidKey, mobile_operator_info_->uuid());
  } else if (mobile_operator_info_->IsServingMobileNetworkOperatorKnown()) {
    SLOG(2) << "Serving provider proxying in for home provider.";
    AssignIfNotEmpty(kOperatorCodeKey, mobile_operator_info_->serving_mccmnc());
    AssignIfNotEmpty(kOperatorNameKey,
                     mobile_operator_info_->serving_operator_name());
    AssignIfNotEmpty(kOperatorCountryKey,
                     mobile_operator_info_->serving_country());
    AssignIfNotEmpty(kOperatorUuidKey, mobile_operator_info_->serving_uuid());
  } else {
    SLOG(2) << "Home and Serving provider are unknown, so using default info.";
    AssignIfNotEmpty(kOperatorCodeKey, mobile_operator_info_->mccmnc());
    AssignIfNotEmpty(kOperatorNameKey, mobile_operator_info_->operator_name());
    AssignIfNotEmpty(kOperatorCountryKey, mobile_operator_info_->country());
    AssignIfNotEmpty(kOperatorUuidKey, mobile_operator_info_->uuid());
  }
  if (home_provider != home_provider_) {
    home_provider_ = home_provider;
    adaptor()->EmitStringmapChanged(kHomeProviderProperty, home_provider_);
  }
  // On the new APN UI revamp, modem and modb APNs are not shown to
  // the user and the behavior of modem APNs should not be altered.
  bool merge_similar_apns =
      !(service_ && service_->custom_apn_list().has_value());
  ApnList apn_list(merge_similar_apns);
  // TODO(b:180004055): remove this when we have captive portal checks that
  // mark APNs as bad and can skip the null APN for data connections
  if (manufacturer_ != kQ6V5ModemManufacturerName) {
    auto profiles = capability_->GetProfiles();
    if (profiles) {
      apn_list.AddApns(*profiles, ApnList::ApnSource::kModem);
    }
  }
  apn_list.AddApns(mobile_operator_info_->apn_list(),
                   ApnList::ApnSource::kModb);
  SetApnList(apn_list.GetList());

  SetProviderRequiresRoaming(mobile_operator_info_->requires_roaming());
}

void Cellular::UpdateServingOperator() {
  SLOG(3) << LoggingTag() << ": " << __func__;
  if (!service()) {
    return;
  }

  Stringmap serving_operator;
  auto AssignIfNotEmpty = [&](const std::string& key,
                              const std::string& value) {
    if (!value.empty())
      serving_operator[key] = value;
  };
  if (mobile_operator_info_->IsServingMobileNetworkOperatorKnown()) {
    AssignIfNotEmpty(kOperatorCodeKey, mobile_operator_info_->serving_mccmnc());
    AssignIfNotEmpty(kOperatorNameKey,
                     mobile_operator_info_->serving_operator_name());
    AssignIfNotEmpty(kOperatorCountryKey,
                     mobile_operator_info_->serving_country());
    AssignIfNotEmpty(kOperatorUuidKey, mobile_operator_info_->serving_uuid());
  } else {
    AssignIfNotEmpty(kOperatorCodeKey, mobile_operator_info_->mccmnc());
    AssignIfNotEmpty(kOperatorNameKey, mobile_operator_info_->operator_name());
    AssignIfNotEmpty(kOperatorCountryKey, mobile_operator_info_->country());
    AssignIfNotEmpty(kOperatorUuidKey, mobile_operator_info_->uuid());
  }

  service()->SetServingOperator(serving_operator);

  // Set friendly name of service.
  std::string service_name = mobile_operator_info_->friendly_operator_name(
      service()->roaming_state() == kRoamingStateRoaming);
  if (service_name.empty()) {
    LOG(WARNING) << LoggingTag()
                 << ": No properties for setting friendly name for: "
                 << service()->log_name();
    return;
  }
  SLOG(2) << LoggingTag() << ": " << __func__
          << ": Service: " << service()->log_name()
          << " Name: " << service_name;
  service()->SetFriendlyName(service_name);

  SetProviderRequiresRoaming(mobile_operator_info_->requires_roaming());
}

void Cellular::OnOperatorChanged() {
  SLOG(2) << LoggingTag() << ": " << __func__;
  CHECK(capability_);

  if (service()) {
    capability_->UpdateServiceOLP();
  }

  UpdateHomeProvider();
  UpdateServingOperator();
  if (mobile_operator_info_->IsMobileNetworkOperatorKnown() ||
      mobile_operator_info_->IsServingMobileNetworkOperatorKnown()) {
    ResetCarrierEntitlement();
  }
}

bool Cellular::StateIsConnected() {
  return state_ == State::kConnected || state_ == State::kLinked;
}

bool Cellular::StateIsRegistered() {
  return state_ == State::kRegistered || state_ == State::kConnected ||
         state_ == State::kLinked;
}

bool Cellular::StateIsStarted() {
  return state_ == State::kModemStarted || state_ == State::kRegistered ||
         state_ == State::kConnected || state_ == State::kLinked;
}

void Cellular::SetServiceForTesting(CellularServiceRefPtr service) {
  service_for_testing_ = service;
  service_ = service;
}

void Cellular::SetSelectedServiceForTesting(CellularServiceRefPtr service) {
  SelectService(service);
}

void Cellular::EntitlementCheck(EntitlementCheckResultCallback callback,
                                bool experimental_tethering) {
  // Only one entitlement check request should exist at any point.
  DCHECK(entitlement_check_callback_.is_null());
  if (!entitlement_check_callback_.is_null()) {
    LOG(ERROR) << kEntitlementCheckAnomalyDetectorPrefix
               << "request received while another one is in progress";
    metrics()->NotifyCellularEntitlementCheckResult(
        Metrics::kCellularEntitlementCheckIllegalInProgress);
    dispatcher()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       TetheringManager::EntitlementStatus::kNotAllowed));
    return;
  }

  if (!mobile_operator_info_->tethering_allowed(experimental_tethering)) {
    LOG(ERROR) << kEntitlementCheckAnomalyDetectorPrefix
               << "tethering is not allowed by database settings";
    metrics()->NotifyCellularEntitlementCheckResult(
        Metrics::kCellularEntitlementCheckNotAllowedByModb);
    dispatcher()->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(callback),
            TetheringManager::EntitlementStatus::kNotAllowedByCarrier));
    return;
  }
  // TODO(b/270210498): remove this check when tethering is allowed by default.
  if (!mobile_operator_info_->IsMobileNetworkOperatorKnown() &&
      !mobile_operator_info_->IsServingMobileNetworkOperatorKnown()) {
    LOG(ERROR) << kEntitlementCheckAnomalyDetectorPrefix
               << "carrier is not known.";
    metrics()->NotifyCellularEntitlementCheckResult(
        Metrics::kCellularEntitlementCheckUnknownCarrier);
    dispatcher()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       TetheringManager::EntitlementStatus::kNotAllowed));
    return;
  }

  entitlement_check_callback_ = std::move(callback);
  carrier_entitlement_->Check(mobile_operator_info_->entitlement_config());
}

void Cellular::TriggerEntitlementCheckCallbacks(
    TetheringManager::EntitlementStatus result) {
  if (!entitlement_check_callback_.is_null()) {
    std::move(entitlement_check_callback_).Run(result);
  }
  if (!tethering_event_callback_.is_null() &&
      result != TetheringManager::EntitlementStatus::kReady) {
    tethering_event_callback_.Run(
        TetheringManager::CellularUpstreamEvent::kUserNoLongerEntitled);
  }
}

void Cellular::OnEntitlementCheckUpdated(CarrierEntitlement::Result result) {
  LOG(INFO) << "Entitlement check updated: " << static_cast<int>(result);
  switch (result) {
    case shill::CarrierEntitlement::Result::kAllowed:
      TriggerEntitlementCheckCallbacks(
          TetheringManager::EntitlementStatus::kReady);
      break;
    case shill::CarrierEntitlement::Result::kNetworkNotReady:
      TriggerEntitlementCheckCallbacks(
          TetheringManager::EntitlementStatus::kUpstreamNetworkNotAvailable);
      break;
    case shill::CarrierEntitlement::Result::kGenericError:
      LOG(ERROR) << kEntitlementCheckAnomalyDetectorPrefix << "Generic error";
      [[fallthrough]];
    case shill::CarrierEntitlement::Result::kUnrecognizedUser:
    case shill::CarrierEntitlement::Result::kUserNotAllowedToTether:
      TriggerEntitlementCheckCallbacks(
          TetheringManager::EntitlementStatus::kNotAllowedUserNotEntitled);
      break;
  }
}

bool Cellular::FirmwareSupportsTethering() {
  // This list should only include FW versions in which hotspot was fully
  // validated on.
  static const std::map<Cellular::ModemType, std::vector<std::string_view>>
      blocklist = {
          {Cellular::ModemType::kL850GL,
           {"18500.5001.00.02", "18500.5001.00.03", "18500.5001.00.04"}},
          // 81600.0000.00.29.21* doesn't support multiple PDNs either, but we
          // don't disable it because this FW is only used by Japanese carriers,
          // which only use single PDNs.
          {Cellular::ModemType::kFM350, {"81600.0000.00.29.19"}},
      };

  const auto it = blocklist.find(modem_type_);
  if (it != blocklist.end()) {
    for (const auto& prefix : it->second) {
      if (firmware_revision_.starts_with(prefix)) {
        SLOG(2) << LoggingTag() << ": " << __func__
                << " : Firmware doesn't support tethering: "
                << firmware_revision_;
        return false;
      }
    }
  }
  return true;
}

void Cellular::SetDefaultPdnForTesting(const RpcIdentifier& dbus_path,
                                       std::unique_ptr<Network> network,
                                       LinkState link_state) {
  default_pdn_ = std::make_unique<NetworkInfo>(this, dbus_path,
                                               std::move(network), link_state);
}

void Cellular::SetMultiplexedTetheringPdnForTesting(
    const RpcIdentifier& dbus_path,
    std::unique_ptr<Network> network,
    LinkState link_state) {
  multiplexed_tethering_pdn_ = std::make_unique<NetworkInfo>(
      this, dbus_path, std::move(network), link_state);
}

Cellular::NetworkInfo::NetworkInfo(Cellular* cellular,
                                   const RpcIdentifier& bearer_path,
                                   int interface_index,
                                   const std::string& interface_name)
    : cellular_(cellular), bearer_path_(bearer_path) {
  network_ = std::make_unique<Network>(
      interface_index, interface_name, Technology::kCellular, false,
      cellular_->manager()->control_interface(),
      cellular_->manager()->dispatcher(), cellular_->manager()->metrics());
  network_->RegisterEventHandler(cellular_);
}

Cellular::NetworkInfo::NetworkInfo(Cellular* cellular,
                                   const RpcIdentifier& bearer_path,
                                   std::unique_ptr<Network> network,
                                   LinkState link_state)
    : cellular_(cellular),
      bearer_path_(bearer_path),
      network_(std::move(network)),
      link_state_(link_state) {}

Cellular::NetworkInfo::~NetworkInfo() {
  network_->Stop();
}

std::string Cellular::NetworkInfo::LoggingTag() {
  return cellular_->LoggingTag() + " [" + network_->interface_name() + "]";
}

bool Cellular::NetworkInfo::Configure(const CellularBearer* bearer) {
  if (!bearer) {
    LOG(INFO) << LoggingTag()
              << ": No active bearer detected: aborting network setup.";
    return false;
  }
  if (bearer->dbus_path() != bearer_path_) {
    LOG(INFO) << LoggingTag()
              << ": Mismatched active bearer detected: aborting network setup.";
    return false;
  }

  bool ipv6_configured = false;
  bool ipv4_configured = false;

  // If the modem has done its own SLAAC and it was able to retrieve a correct
  // address and gateway it will report kMethodStatic. If the modem didn't do
  // SLAAC by itself it will report kMethodDHCP and optionally include a
  // link-local address to configure before running host SLAAC. In both those
  // cases, the modem will receive DNS information via PCOs from the network,
  // which should be considered in the setup.
  if (bearer->ipv6_config_method() !=
      CellularBearer::IPConfigMethod::kUnknown) {
    SLOG(2) << LoggingTag()
            << ": Assign static IPv6 configuration from bearer.";
    const auto& props = *bearer->ipv6_config_properties();
    ipv6_props_ = props;
    start_opts_.accept_ra = true;

    // TODO(b/285205946): Currently IPv6 method is always set to static so we
    // need to look into the actual address to tell whether it's a link local
    // address to be used for SLAAC or it's already a global address reported by
    // modem SLAAC. After we revert the ModemManager patch we should be able to
    // simply check IPv6 method here instead.
    const auto link_local_mask =
        *net_base::IPv6CIDR::CreateFromStringAndPrefix("fe80::", 10);
    const auto local = net_base::IPv6Address::CreateFromString(props.address);
    if (!local) {
      LOG(ERROR) << LoggingTag()
                 << ": IPv6 address is not valid: " << props.address;
    } else if (link_local_mask.InSameSubnetWith(*local)) {
      ipv6_props_->address.clear();
      ipv6_props_->subnet_prefix = 0;
      start_opts_.link_local_address = local;
    } else {
      start_opts_.accept_ra = false;
    }
    ipv6_configured = true;
  }

  std::optional<DHCPProvider::Options> dhcp_opts;
  if (bearer->ipv4_config_method() == CellularBearer::IPConfigMethod::kStatic) {
    SLOG(2) << LoggingTag()
            << ": Assign static IPv4 configuration from bearer.";
    ipv4_props_ = *bearer->ipv4_config_properties();
    ipv4_configured = true;
  } else if (bearer->ipv4_config_method() ==
             CellularBearer::IPConfigMethod::kDHCP) {
    if (cellular_->IsModemL850GL()) {
      LOG(WARNING) << LoggingTag()
                   << ": DHCP configuration not supported on L850"
                      " (Ignoring kDHCP).";
    } else {
      SLOG(2) << LoggingTag() << ": Needs DHCP to acquire IPv4 configuration.";
      dhcp_opts = cellular_->manager()->CreateDefaultDHCPOption();
      dhcp_opts->use_arp_gateway = false;
      dhcp_opts->use_rfc_8925 = false;
      ipv4_configured = true;
    }
  }

  if (!ipv6_configured && !ipv4_configured) {
    LOG(WARNING) << LoggingTag()
                 << ": No supported IP configuration found in bearer";
    return false;
  }

  // Override the MTU with a given limit for a specific serving operator
  // if the network doesn't report something lower. The setting is applied both
  // in IPv4 and IPv6 settings.
  if (cellular_->mobile_operator_info_ &&
      cellular_->mobile_operator_info_->mtu() != IPConfig::kUndefinedMTU) {
    if (ipv4_props_ &&
        (ipv4_props_->mtu == IPConfig::kUndefinedMTU ||
         cellular_->mobile_operator_info_->mtu() < ipv4_props_->mtu)) {
      ipv4_props_->mtu = cellular_->mobile_operator_info_->mtu();
    }
    if (ipv6_props_ &&
        (ipv6_props_->mtu == IPConfig::kUndefinedMTU ||
         cellular_->mobile_operator_info_->mtu() < ipv6_props_->mtu)) {
      ipv6_props_->mtu = cellular_->mobile_operator_info_->mtu();
    }
  }

  start_opts_.dhcp = dhcp_opts;
  // TODO(b/234300343#comment43): Read probe URL override configuration
  // from shill APN dB.
  start_opts_.probing_configuration =
      cellular_->manager()->GetPortalDetectorProbingConfiguration();

  return true;
}

void Cellular::NetworkInfo::Start() {
  // TODO(b/269401899): Use net_base::NetworkConfig in NetworkInfo instead of
  // ipv6_props_ and ipv4_props_.
  if (ipv6_props_ || ipv4_props_) {
    IPConfig::Properties* ipv6 = ipv6_props_ ? &*ipv6_props_ : nullptr;
    IPConfig::Properties* ipv4 = ipv4_props_ ? &*ipv4_props_ : nullptr;
    auto network_config = IPConfig::Properties::ToNetworkConfig(ipv4, ipv6);
    network_->set_link_protocol_network_config(
        std::make_unique<net_base::NetworkConfig>(std::move(network_config)));
  }
  network_->Start(start_opts_);
}

void Cellular::NetworkInfo::DestroySockets() {
  for (const auto& address : network_->GetAddresses()) {
    SLOG(2) << LoggingTag() << ": Destroy all sockets of address: " << address;
    cellular_->rtnl_handler()->RemoveInterfaceAddress(
        network_->interface_index(), address);
    if (!cellular_->socket_destroyer_->DestroySockets(IPPROTO_TCP,
                                                      address.address()))
      SLOG(2) << LoggingTag() << ": no tcp sockets found for " << address;
    // Chrome sometimes binds to UDP sockets, so lets destroy them.
    if (!cellular_->socket_destroyer_->DestroySockets(IPPROTO_UDP,
                                                      address.address()))
      SLOG(2) << LoggingTag() << ": no udp sockets found for " << address;
  }
  SLOG(2) << LoggingTag() << ": " << __func__ << " complete.";
}

}  // namespace shill
