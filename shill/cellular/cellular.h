// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_CELLULAR_H_
#define SHILL_CELLULAR_CELLULAR_H_

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/cellular/mm1_proxy_interface.h"
#include "shill/cellular/mobile_operator_info.h"
#include "shill/device.h"
#include "shill/device_id.h"
#include "shill/event_dispatcher.h"
#include "shill/metrics.h"
#include "shill/mockable.h"
#include "shill/refptr_types.h"
#include "shill/rpc_task.h"

namespace shill {

class CellularCapability;
class Error;
class ExternalTask;
class ModemInfo;
class NetlinkSockDiag;
class PPPDeviceFactory;
class ProcessManager;

class Cellular : public Device,
                 public RpcTaskDelegate,
                 public MobileOperatorInfo::Observer {
 public:
  enum Type {
    kType3gpp,  // ModemManager1
    kTypeCdma,
    kTypeInvalid,
  };

  // The device states progress linearly from Disabled to Linked.
  enum State {
    // This is the initial state of the modem and indicates that the modem radio
    // is not turned on.
    kStateDisabled,
    // This state indicates that the modem radio is turned on, and it should be
    // possible to measure signal strength.
    kStateEnabled,
    // The modem has registered with a network and has signal quality
    // measurements. A cellular service object is created.
    kStateRegistered,
    // The modem has connected to a network.
    kStateConnected,
    // The network interface is UP.
    kStateLinked,
  };

  // This enum must be kept in sync with ModemManager's MMModemState enum.
  enum ModemState {
    kModemStateFailed = -1,
    kModemStateUnknown = 0,
    kModemStateInitializing = 1,
    kModemStateLocked = 2,
    kModemStateDisabled = 3,
    kModemStateDisabling = 4,
    kModemStateEnabling = 5,
    kModemStateEnabled = 6,
    kModemStateSearching = 7,
    kModemStateRegistered = 8,
    kModemStateDisconnecting = 9,
    kModemStateConnecting = 10,
    kModemStateConnected = 11,
  };

  // Enum for tracking Cellular::Start/Stop and
  // CellularCapability::Start/StopModem to support modemmanager restarts
  // which requires reconstructing CellularCapability.
  enum class CapabilityState {
    kCellularStopped,
    kCellularStarted,
    kModemStarting,
    kModemStarted,
    kModemStopping,
  };

  // Used in Cellular and CellularCapability3gpp to store and pass properties
  // associated with a SIM Profile.
  struct SimProperties {
    size_t slot;
    std::string iccid;
    std::string eid;
    std::string operator_id;
    std::string spn;
    std::string imsi;
    bool operator==(const SimProperties& other) const {
      return slot == other.slot && iccid == other.iccid && eid == other.eid &&
             operator_id == other.operator_id && spn == other.spn &&
             imsi == other.imsi;
    }
  };

  // Static helper for logging.
  static std::string GetStateString(State state);
  static std::string GetModemStateString(ModemState modem_state);
  static std::string GetCapabilityStateString(CapabilityState capability_state);

  // |path| is the ModemManager.Modem DBus object path (e.g.,
  // "/org/freedesktop/ModemManager1/Modem/0"). |service| is the modem
  // mananager service name (e.g., /org/freedesktop/ModemManager1).
  Cellular(ModemInfo* modem_info,
           const std::string& link_name,
           const std::string& address,
           int interface_index,
           Type type,
           const std::string& service,
           const RpcIdentifier& path);
  Cellular(const Cellular&) = delete;
  Cellular& operator=(const Cellular&) = delete;

  ~Cellular() override;

  // Returns the equipment identifier of the device. If the device is a 3GPP
  // device, its IMEI is returned. If the device is a 3GPP2 device, its MEID is
  // returned. When neither IMEI nor MEID is available, the MAC address of the
  // device is returned. IMEI and MEID are unique identifiers, while the MAC
  // address may not be (e.g. some cellular devices of the same model may share
  // the same MAC address, or some cellular devices may not have a MAC address
  // and are assigned with a randomly generated MAC address by the kernel).
  std::string GetEquipmentIdentifier() const;

  std::string GetStorageIdentifier() const override;

  // Load configuration for the device from |storage|.
  bool Load(const StoreInterface* storage) override;

  // Save configuration for the device to |storage|.
  bool Save(StoreInterface* storage) override;

  // Returns true if |service| is connectable.
  bool GetConnectable(CellularService* service) const;

  // Asynchronously connects the modem to |service|. Changes the primary slot if
  // required. Populates |error| on failure, leaves it unchanged otherwise.
  virtual void Connect(CellularService* service, Error* error);

  // Asynchronously disconnects the modem from the current network and populates
  // |error| on failure, leaves it unchanged otherwise.
  virtual void Disconnect(Error* error, const char* reason);

  // Asynchronously detach then re-attach the network.
  virtual void ReAttach();

  // Cancel any pending connect request.
  void CancelPendingConnect();

  // Performs the necessary steps to bring the service to the activated state,
  // once an online payment has been done.
  void CompleteActivation(Error* error);

  const CellularServiceRefPtr& service() const { return service_; }
  MobileOperatorInfo* home_provider_info() const {
    return home_provider_info_.get();
  }
  MobileOperatorInfo* serving_operator_info() const {
    return serving_operator_info_.get();
  }

  // Creates a CellularCapability based on properties of |this| and
  // |modem_info|. Sets |capability_| to the created capability.
  // Called once the associated Modem instance is available.
  void CreateCapability(ModemInfo* modem_info);

  // Destroys |capability_|. Called when the associated Modem instance is
  // destroyed.
  void DestroyCapability();

  // Update the home provider from the information in |operator_info|. This
  // information may be from the SIM / received OTA.
  void UpdateHomeProvider(const MobileOperatorInfo* operator_info);
  // Update the serving operator using information in |operator_info|.
  // Additionally, if |home_provider_info| is not nullptr, use it to come up
  // with a better name.
  void UpdateServingOperator(const MobileOperatorInfo* operator_info,
                             const MobileOperatorInfo* home_provider_info);

  State state() const { return state_; }

  ModemState modem_state() const { return modem_state_; }
  bool IsUnderlyingDeviceEnabled() const override;
  static bool IsEnabledModemState(ModemState state);

  void HandleNewSignalQuality(uint32_t strength);

  // Processes a change in the modem registration state, possibly creating,
  // destroying or updating the CellularService.
  void HandleNewRegistrationState();

  // Asynchronously queries capability for cellular location.
  void PollLocation();

  // Starts and stops scheduled location polls
  void StartLocationPolling();
  void StopLocationPolling();

  mockable void OnPropertiesChanged(const std::string& interface,
                                    const KeyValueStore& changed_properties);

  // Inherited from Device.
  void Start(Error* error,
             const EnabledStateChangedCallback& callback) override;
  void Stop(Error* error, const EnabledStateChangedCallback& callback) override;
  void LinkEvent(unsigned int flags, unsigned int change) override;
  void Scan(Error* error, const std::string& /*reason*/) override;
  void RegisterOnNetwork(const std::string& network_id,
                         Error* error,
                         const ResultCallback& callback) override;
  void RequirePin(const std::string& pin,
                  bool require,
                  Error* error,
                  const ResultCallback& callback) override;
  void EnterPin(const std::string& pin,
                Error* error,
                const ResultCallback& callback) override;
  void UnblockPin(const std::string& unblock_code,
                  const std::string& pin,
                  Error* error,
                  const ResultCallback& callback) override;
  void ChangePin(const std::string& old_pin,
                 const std::string& new_pin,
                 Error* error,
                 const ResultCallback& callback) override;
  void Reset(Error* error, const ResultCallback& callback) override;
  void DropConnection() override;
  void SetServiceState(Service::ConnectState state) override;
  void SetServiceFailure(Service::ConnectFailure failure_state) override;
  void SetServiceFailureSilent(Service::ConnectFailure failure_state) override;
  void OnBeforeSuspend(const ResultCallback& callback) override;
  void OnAfterResume() override;
  std::vector<GeolocationInfo> GetGeolocationObjects() const override;

  std::string GetTechnologyFamily(Error* error);
  std::string GetDeviceId(Error* error);
  void OnModemStateChanged(ModemState new_state);
  void OnScanReply(const Stringmaps& found_networks, const Error& error);

  // accessor to read the allow roaming property
  bool allow_roaming_property() const { return allow_roaming_; }
  // Is the underlying device in the process of activating?
  bool IsActivating() const;

  // Returns true if roaming is allowed by the user (via the user modifiable
  // "allow_roaming" property) or required by the mobile provider.
  bool IsRoamingAllowedOrRequired() const;

  // Initiate PPP link. Called from capabilities.
  virtual void StartPPP(const std::string& serial_device);
  // Callback for |ppp_task_|.
  virtual void OnPPPDied(pid_t pid, int exit);
  // Implements RpcTaskDelegate, for |ppp_task_|.
  void GetLogin(std::string* user, std::string* password) override;
  void Notify(const std::string& reason,
              const std::map<std::string, std::string>& dict) override;

  // Returns a list of APNs to try, in the following order:
  // - the last APN that resulted in a successful connection attempt on the
  //   current network (if any)
  // - the APN, if any, that was set by the user
  // - APNs that the modem reports as provisioned profiles
  // - the list of APNs found in the mobile broadband provider DB for the
  //   home provider associated with the current SIM
  std::deque<Stringmap> BuildApnTryList() const;

  // ///////////////////////////////////////////////////////////////////////////
  // DBus Properties exposed by the Device interface of shill.
  void RegisterProperties();

  // |dbus_path| and |mac_address| may change if the associated Modem restarts.
  void UpdateModemProperties(const RpcIdentifier& dbus_path,
                             const std::string& mac_address);

  // getters
  const std::string& dbus_service() const { return dbus_service_; }
  const RpcIdentifier& dbus_path() const { return dbus_path_; }
  const Stringmap& home_provider() const { return home_provider_; }
  bool scanning_supported() const { return scanning_supported_; }
  const std::string& eid() const { return eid_; }
  const std::string& esn() const { return esn_; }
  const std::string& firmware_revision() const { return firmware_revision_; }
  const std::string& hardware_revision() const { return hardware_revision_; }
  const DeviceId* device_id() const { return device_id_.get(); }
  const std::string& imei() const { return imei_; }
  const std::string& imsi() const { return imsi_; }
  const std::string& mdn() const { return mdn_; }
  const std::string& meid() const { return meid_; }
  const std::string& min() const { return min_; }
  const std::string& manufacturer() const { return manufacturer_; }
  const std::string& model_id() const { return model_id_; }
  const std::string& mm_plugin() const { return mm_plugin_; }
  bool scanning() const { return scanning_; }

  const std::string& selected_network() const { return selected_network_; }
  const Stringmaps& found_networks() const { return found_networks_; }
  bool provider_requires_roaming() const { return provider_requires_roaming_; }
  bool sim_present() const { return sim_present_; }
  const Stringmaps& apn_list() const { return apn_list_; }
  const std::string& iccid() const { return iccid_; }
  bool use_attach_apn() const { return use_attach_apn_; }

  Type type() const { return type_; }
  bool inhibited() const { return inhibited_; }
  const std::string& connect_pending_iccid() const {
    return connect_pending_iccid_;
  }

  // Returns a unique identifier for a SIM Card. For physical cards this will be
  // the ICCID and there should only be one matching service. For eSIM cards,
  // this will be the eUICCID (eID) and there may be multiple services
  // associated with the card.
  const std::string& GetSimCardId() const;

  // Returns true if |sim_card_id| matches any available SIM cards.
  bool HasSimCardId(const std::string& sim_card_id) const;

  // Sets the SIM properties and the primary SIM, and updates services and
  // state accordingly.
  void SetSimProperties(const std::vector<SimProperties>& slot_properties,
                        size_t primary_slot);

  // Property setters. TODO(b/176904580): Rename SetFoo and alphabetize.
  void set_home_provider(const Stringmap& home_provider);
  void set_carrier(const std::string& carrier);
  void SetScanningSupported(bool scanning_supported);
  void set_equipment_id(const std::string& equipment_id);
  void set_esn(const std::string& esn);
  void set_firmware_revision(const std::string& firmware_revision);
  void set_hardware_revision(const std::string& hardware_revision);
  void set_device_id(std::unique_ptr<DeviceId> device_id);
  void SetImei(const std::string& imei);
  void set_mdn(const std::string& mdn);
  void set_meid(const std::string& meid);
  void set_min(const std::string& min);
  void set_manufacturer(const std::string& manufacturer);
  void set_model_id(const std::string& model_id);
  void set_mm_plugin(const std::string& mm_plugin);

  void set_selected_network(const std::string& selected_network);
  void clear_found_networks();
  void set_found_networks(const Stringmaps& found_networks);
  void set_provider_requires_roaming(bool provider_requires_roaming);
  void SetApnList(const Stringmaps& apn_list);

  // Takes ownership.
  void set_home_provider_info(MobileOperatorInfo* home_provider_info);
  // Takes ownership.
  void set_serving_operator_info(MobileOperatorInfo* serving_operator_info);

  // Implements MobileOperatorInfo::Observer:
  void OnOperatorChanged() override;

  CellularCapability* capability_for_testing() { return capability_.get(); }
  mm1::Mm1ProxyInterface* mm1_proxy_for_testing() { return mm1_proxy_.get(); }
  const KeyValueStores& sim_slot_info_for_testing() { return sim_slot_info_; }

  // Sets a Service for testing. When set, Cellular does not create or destroy
  // the associated Service.
  void SetServiceForTesting(CellularServiceRefPtr service);

  void set_capability_state_for_testing(CapabilityState state) {
    capability_state_ = state;
  }
  void set_modem_state_for_testing(ModemState state) { modem_state_ = state; }
  void set_use_attach_apn_for_testing(bool on) { use_attach_apn_ = on; }
  void set_eid_for_testing(const std::string& eid) { eid_ = eid; }
  void set_iccid_for_testing(const std::string& iccid) { iccid_ = iccid; }
  void set_state_for_testing(const State& state) { state_ = state; }

  // Delay before connecting to pending connect requests. This helps prevent
  // connect failures while the Modem is still starting up.
  static constexpr base::TimeDelta kPendingConnectDelay =
      base::TimeDelta::FromSeconds(2);

 private:
  friend class CellularTest;
  friend class CellularCapabilityCdmaTest;
  friend class CellularServiceTest;
  friend class CellularServiceProviderTest;
  friend class ModemTest;
  FRIEND_TEST(CellularTest, ChangeServiceState);
  FRIEND_TEST(CellularTest, ChangeServiceStatePPP);
  FRIEND_TEST(CellularTest, Connect);
  FRIEND_TEST(CellularTest, ConnectFailure);
  FRIEND_TEST(CellularTest, ConnectFailureNoService);
  FRIEND_TEST(CellularTest, ConnectSuccessNoService);
  FRIEND_TEST(CellularTest, CustomSetterNoopChange);
  FRIEND_TEST(CellularTest, Disconnect);
  FRIEND_TEST(CellularTest, DisconnectFailure);
  FRIEND_TEST(CellularTest, DropConnection);
  FRIEND_TEST(CellularTest, DropConnectionPPP);
  FRIEND_TEST(CellularTest, EstablishLinkDHCP);
  FRIEND_TEST(CellularTest, EstablishLinkPPP);
  FRIEND_TEST(CellularTest, EstablishLinkStatic);
  FRIEND_TEST(CellularTest, FriendlyServiceName);
  FRIEND_TEST(CellularTest, HomeProviderServingOperator);
  FRIEND_TEST(CellularTest, IsRoamingAllowedOrRequired);
  FRIEND_TEST(CellularTest, LinkEventUpWithPPP);
  FRIEND_TEST(CellularTest, LinkEventUpWithoutPPP);
  FRIEND_TEST(CellularTest, LinkEventWontDestroyService);
  FRIEND_TEST(CellularTest, ModemStateChangeDisable);
  FRIEND_TEST(CellularTest, ModemStateChangeEnable);
  FRIEND_TEST(CellularTest, ModemStateChangeStaleConnected);
  FRIEND_TEST(CellularTest, ModemStateChangeValidConnected);
  FRIEND_TEST(CellularTest, Notify);
  FRIEND_TEST(CellularTest, OnAfterResumeDisableInProgressWantDisabled);
  FRIEND_TEST(CellularTest, OnAfterResumeDisableQueuedWantEnabled);
  FRIEND_TEST(CellularTest, OnAfterResumeDisabledWantDisabled);
  FRIEND_TEST(CellularTest, OnAfterResumeDisabledWantEnabled);
  FRIEND_TEST(CellularTest, OnAfterResumePowerDownInProgressWantEnabled);
  FRIEND_TEST(CellularTest, OnPPPDied);
  FRIEND_TEST(CellularTest, PPPConnectionFailedAfterAuth);
  FRIEND_TEST(CellularTest, PPPConnectionFailedBeforeAuth);
  FRIEND_TEST(CellularTest, PPPConnectionFailedDuringAuth);
  FRIEND_TEST(CellularTest, PPPConnectionFailedAfterConnect);
  FRIEND_TEST(CellularTest, ScanAsynchronousFailure);
  FRIEND_TEST(CellularTest, ScanImmediateFailure);
  FRIEND_TEST(CellularTest, ScanSuccess);
  FRIEND_TEST(CellularTest, SetAllowRoaming);
  FRIEND_TEST(CellularTest, SetInhibited);
  FRIEND_TEST(CellularTest, SetUseAttachApn);
  FRIEND_TEST(CellularTest, StopPPPOnDisconnect);
  FRIEND_TEST(CellularTest, StorageIdentifier);
  FRIEND_TEST(CellularTest, StartConnected);
  FRIEND_TEST(CellularTest, StartCdmaRegister);
  FRIEND_TEST(CellularTest, StartGsmRegister);
  FRIEND_TEST(CellularTest, StartLinked);
  FRIEND_TEST(CellularTest, StartPPP);
  FRIEND_TEST(CellularTest, StartPPPAfterEthernetUp);
  FRIEND_TEST(CellularTest, StartPPPAlreadyStarted);
  FRIEND_TEST(CellularTest, GetGeolocationObjects);

  // Names of properties in storage
  static const char kAllowRoaming[];
  static const char kUseAttachApn[];

  // Modem reset sysfs path
  static const char kModemResetSysfsName[];

  // Modem Manufacturer Name
  static const char kQ6V5ModemManufacturerName[];

  // Modem driver sysfs path
  static const char kModemDriverSysfsName[];

  // Modem driver name
  static const char kQ6V5DriverName[];

  // Time between stop and start of modem device
  static const int64_t kModemResetTimeoutMilliseconds;

  // Time between asynchronous calls to ModemManager1's GetLocation()
  static const int64_t kPollLocationIntervalMilliseconds;

  // TODO(b/173635024): Fix order of cellular.h and .cc methods.
  void StartModem(Error* error, const EnabledStateChangedCallback& callback);
  void StartModemCallback(const EnabledStateChangedCallback& callback,
                          const Error& error);
  void StopModem(Error* error, const EnabledStateChangedCallback& callback);
  void StopModemCallback(const EnabledStateChangedCallback& callback,
                         const Error& error);

  bool ShouldBringNetworkInterfaceDownAfterDisabled() const override;

  void SetState(State state);
  void SetModemState(ModemState modem_state_state);
  void SetCapabilityState(CapabilityState capability_state);
  void SetScanning(bool scanning);
  void SetScanningProperty(bool scanning);

  void OnEnabled();
  void OnConnecting();
  void OnConnected() override;
  void OnDisconnected();
  void OnDisconnectFailed();

  // Invoked when the modem is connected to the cellular network to transition
  // to the network-connected state and bring the network interface up.
  void EstablishLink();

  void InitCapability(Type type);

  void SetPrimarySimProperties(const SimProperties& properties);
  void SetSimSlotProperties(const std::vector<SimProperties>& slot_properties,
                            int primary_slot);

  // Creates or destroys services as required.
  void UpdateServices();

  // Creates and registers services for the available SIMs and sets
  // |service_| to the primary (active) service.
  void CreateServices();

  // Destroys all services and the connection, if any. This also eliminates any
  // circular references between this device and the associated service,
  // allowing eventual device destruction.
  void DestroyAllServices();

  // Creates or updates services for secondary SIMs.
  void UpdateSecondaryServices();

  // HelpRegisterDerived*: Expose a property over RPC, with the name |name|.
  //
  // Reads of the property will be handled by invoking |get|.
  // Writes to the property will be handled by invoking |set|.
  // Clearing the property will be handled by PropertyStore.
  void HelpRegisterDerivedBool(const std::string& name,
                               bool (Cellular::*get)(Error* error),
                               bool (Cellular::*set)(const bool& value,
                                                     Error* error));
  void HelpRegisterConstDerivedString(
      const std::string& name, std::string (Cellular::*get)(Error* error));

  void OnConnectReply(std::string iccid, const Error& error);
  void OnDisconnectReply(const Error& error);

  void ReAttachOnDetachComplete(const Error& error);

  // DBus accessors
  bool GetAllowRoaming(Error* /*error*/);
  bool SetAllowRoaming(const bool& value, Error* error);
  bool GetInhibited(Error* /*error*/);
  bool SetInhibited(const bool& inhibited, Error* error);
  void SetInhibitedGetDeviceCallback(bool inhibited, const brillo::Any& device);
  void OnInhibitDevice(bool inhibited, const Error& error);
  void SetInhibitedProperty(bool inhibited);
  KeyValueStore GetSimLockStatus(Error* error);
  void SetSimPresent(bool sim_present);

  // DBUS accessors to read/modify the use of an Attach APN
  bool GetUseAttachApn(Error* /*error*/) { return use_attach_apn_; }
  bool SetUseAttachApn(const bool& value, Error* error);

  // When shill terminates or ChromeOS suspends, this function is called to
  // disconnect from the cellular network.
  void StartTermination();

  // This method is invoked upon the completion of StartTermination().
  void OnTerminationCompleted(const Error& error);

  // This function does the final cleanup once a disconnect request terminates.
  // Returns true, if the device state is successfully changed.
  bool DisconnectCleanup();

  // Executed after the asynchronous CellularCapability::StartModem
  // call from OnAfterResume completes.
  static void LogRestartModemResult(const Error& error);

  // Handler to reset qcom-q6v5-mss based modems
  bool ResetQ6V5Modem();

  // Handler to check if modem is based on qcom-q6v5-mss
  bool IsQ6V5Modem();

  // Terminate the pppd process associated with this Device, and remove the
  // association between the PPPDevice and our CellularService. If this
  // Device is not using PPP, the method has no effect.
  void StopPPP();

  // Handlers for PPP events. Dispatched from Notify().
  void OnPPPAuthenticated();
  void OnPPPAuthenticating();
  void OnPPPConnected(const std::map<std::string, std::string>& params);

  void SetPendingConnect(const std::string& iccid);
  void ConnectToPending();
  void ConnectToPendingAfterDelay();
  void ConnectToPendingFailed(Service::ConnectFailure failure);
  void ConnectToPendingCancel();

  void UpdateScanning();
  void GetLocationCallback(const std::string& gpp_lac_ci_string,
                           const Error& error);
  void PollLocationTask();

  bool StateIsConnected();
  bool StateIsRegistered();

  State state_ = kStateDisabled;
  ModemState modem_state_ = kModemStateUnknown;
  CapabilityState capability_state_ = CapabilityState::kCellularStopped;

  struct LocationInfo {
    std::string mcc;
    std::string mnc;
    std::string lac;
    std::string ci;
  };
  LocationInfo location_info_;

  // Operator info objects. These objects receive updates as we receive
  // information about the network operators from the SIM or OTA. In turn, they
  // send out updates through their observer interfaces whenever the identity of
  // the network operator changes, or any other property of the operator
  // changes.
  std::unique_ptr<MobileOperatorInfo> home_provider_info_;
  std::unique_ptr<MobileOperatorInfo> serving_operator_info_;

  // ///////////////////////////////////////////////////////////////////////////
  // All DBus Properties exposed by the Cellular device.
  // Properties common to GSM and CDMA modems.
  const std::string dbus_service_;  // org.*.ModemManager*
  RpcIdentifier dbus_path_;         // ModemManager.Modem
  // Used because we currently expose |dbus_path| as a string property.
  std::string dbus_path_str_;

  Stringmap home_provider_;

  bool scanning_supported_;
  std::string equipment_id_;
  std::string esn_;
  std::string firmware_revision_;
  std::string hardware_revision_;
  std::unique_ptr<DeviceId> device_id_;
  std::string imei_;
  std::string manufacturer_;
  std::string mdn_;
  std::string meid_;
  std::string min_;
  std::string model_id_;
  std::string mm_plugin_;
  bool scanning_;
  bool polling_location_;
  base::CancelableClosure poll_location_task_;

  // GSM only properties.
  // They are always exposed but are non empty only for GSM technology modems.
  std::string selected_network_;
  Stringmaps found_networks_;
  bool provider_requires_roaming_;
  uint16_t scan_interval_;
  Stringmaps apn_list_;

  // Primary SIM properties.
  std::string eid_;  // SIM eID, aka eUICCID
  std::string iccid_;
  std::string imsi_;
  bool sim_present_;

  // vector of SimProperties, ordered by slot.
  std::vector<SimProperties> sim_slot_properties_;
  int primary_sim_slot_ = -1;
  // vector of KeyValueStore dictionaries, emitted as Device.SIMSlotInfo.
  KeyValueStores sim_slot_info_;
  bool sim_slot_switch_allowed_ = true;

  // End of DBus properties.
  // ///////////////////////////////////////////////////////////////////////////

  Type type_;
  std::unique_ptr<mm1::Mm1ProxyInterface> mm1_proxy_;
  std::unique_ptr<CellularCapability> capability_;

  // The uid from the org.freedesktop.ModemManager1.Device property.
  std::string uid_;

  PPPDeviceFactory* ppp_device_factory_;

  ProcessManager* process_manager_;

  // The active CellularService instance for this Device. This will always be
  // set to a valid service instance.
  CellularServiceRefPtr service_;
  // When set in tests, |service_| is not created or destroyed by Cellular.
  CellularServiceRefPtr service_for_testing_;

  // User preference to allow or disallow roaming
  bool allow_roaming_;

  // Chrome flags to enable setting the attach APN from the host
  bool use_attach_apn_;

  // Reflects the Device property indicating that the modem is inhibted. The
  // property is not persisted and is reset to false when the modem starts.
  bool inhibited_;

  // Track whether a user initiated scan is in prgoress (initiated via ::Scan)
  bool proposed_scan_in_progress_;

  // Flag indicating that a disconnect has been explicitly requested.
  bool explicit_disconnect_;

  std::unique_ptr<ExternalTask> ppp_task_;
  PPPDeviceRefPtr ppp_device_;
  bool is_ppp_authenticating_;

  std::unique_ptr<NetlinkSockDiag> socket_destroyer_;

  // Used to keep scanning=true while the Modem is restarting.
  // TODO(b/177588333): Make Modem and/or the MM dbus API more robust.
  base::CancelableClosure scanning_clear_callback_;

  // If a Connect request occurs while the Modem is busy, do not connect
  // immediately, instead set |connect_pending_iccid_|. The connect will occur
  // after a delay once Scanning is set to false.
  std::string connect_pending_iccid_;
  base::CancelableClosure connect_pending_callback_;
  // Used to cancel a pending connect while waiting for Modem registration.
  base::CancelableClosure connect_cancel_callback_;

  base::WeakPtrFactory<Cellular> weak_ptr_factory_;
};

}  // namespace shill

#endif  // SHILL_CELLULAR_CELLULAR_H_
