// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi.h"

#include <linux/if.h>  // Needs definitions from netinet/ether.h
#include <netinet/ether.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/file_path.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <glib.h>

#include "shill/control_interface.h"
#include "shill/dbus_adaptor.h"
#include "shill/device.h"
#include "shill/eap_credentials.h"
#include "shill/error.h"
#include "shill/file_reader.h"
#include "shill/geolocation_info.h"
#include "shill/icmp.h"
#include "shill/ieee80211.h"
#include "shill/ip_address.h"
#include "shill/link_monitor.h"
#include "shill/logging.h"
#include "shill/mac80211_monitor.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/netlink_manager.h"
#include "shill/netlink_message.h"
#include "shill/nl80211_message.h"
#include "shill/property_accessor.h"
#include "shill/proxy_factory.h"
#include "shill/rtnl_handler.h"
#include "shill/scan_session.h"
#include "shill/scope_logger.h"
#include "shill/shill_time.h"
#include "shill/supplicant_eap_state_handler.h"
#include "shill/supplicant_interface_proxy_interface.h"
#include "shill/supplicant_network_proxy_interface.h"
#include "shill/supplicant_process_proxy_interface.h"
#include "shill/technology.h"
#include "shill/wake_on_wifi.h"
#include "shill/wifi_endpoint.h"
#include "shill/wifi_provider.h"
#include "shill/wifi_service.h"
#include "shill/wpa_supplicant.h"

using base::Bind;
using base::FilePath;
using base::StringPrintf;
using std::map;
using std::set;
using std::string;
using std::vector;

namespace shill {

// statics
const char *WiFi::kDefaultBgscanMethod =
    WPASupplicant::kNetworkBgscanMethodSimple;
const uint16_t WiFi::kDefaultBgscanShortIntervalSeconds = 30;
const int32_t WiFi::kDefaultBgscanSignalThresholdDbm = -50;
const uint16_t WiFi::kDefaultScanIntervalSeconds = 60;
const uint16_t WiFi::kDefaultRoamThresholdDb = 18;  // Supplicant's default.
const uint32_t WiFi::kDefaultWiphyIndex = 999;

// Scan interval while connected.
const uint16_t WiFi::kBackgroundScanIntervalSeconds = 3601;
// Age (in seconds) beyond which a BSS cache entry will not be preserved,
// across a suspend/resume.
const time_t WiFi::kMaxBSSResumeAgeSeconds = 10;
const char WiFi::kInterfaceStateUnknown[] = "shill-unknown";
const time_t WiFi::kRescanIntervalSeconds = 1;
const int WiFi::kNumFastScanAttempts = 3;
const int WiFi::kFastScanIntervalSeconds = 10;
const int WiFi::kPendingTimeoutSeconds = 15;
const int WiFi::kReconnectTimeoutSeconds = 10;
const int WiFi::kRequestStationInfoPeriodSeconds = 20;
const size_t WiFi::kMinumumFrequenciesToScan = 4;  // Arbitrary but > 0.
const float WiFi::kDefaultFractionPerScan = 0.34;
const char WiFi::kProgressiveScanFieldTrialFlagFile[] =
    "/home/chronos/.progressive_scan_variation";
const size_t WiFi::kStuckQueueLengthThreshold = 40;  // ~1 full-channel scan
const int WiFi::kVerifyWakeOnWiFiSettingsDelaySeconds = 1;
const int WiFi::kMaxSetWakeOnPacketRetries = 2;

WiFi::WiFi(ControlInterface *control_interface,
           EventDispatcher *dispatcher,
           Metrics *metrics,
           Manager *manager,
           const string& link,
           const string &address,
           int interface_index)
    : Device(control_interface,
             dispatcher,
             metrics,
             manager,
             link,
             address,
             interface_index,
             Technology::kWifi),
      provider_(manager->wifi_provider()),
      weak_ptr_factory_(this),
      proxy_factory_(ProxyFactory::GetInstance()),
      time_(Time::GetInstance()),
      supplicant_present_(false),
      supplicant_state_(kInterfaceStateUnknown),
      supplicant_bss_("(unknown)"),
      need_bss_flush_(false),
      resumed_at_((struct timeval) {0}),
      fast_scans_remaining_(kNumFastScanAttempts),
      has_already_completed_(false),
      is_roaming_in_progress_(false),
      is_debugging_connection_(false),
      eap_state_handler_(new SupplicantEAPStateHandler()),
      mac80211_monitor_(new Mac80211Monitor(
          dispatcher,
          link,
          kStuckQueueLengthThreshold,
          base::Bind(&WiFi::RestartFastScanAttempts,
                     weak_ptr_factory_.GetWeakPtr()),
          metrics)),
      bgscan_short_interval_seconds_(kDefaultBgscanShortIntervalSeconds),
      bgscan_signal_threshold_dbm_(kDefaultBgscanSignalThresholdDbm),
      roam_threshold_db_(kDefaultRoamThresholdDb),
      scan_interval_seconds_(kDefaultScanIntervalSeconds),
      wiphy_index_(kDefaultWiphyIndex),
      progressive_scan_enabled_(false),
      scan_configuration_("Full scan"),
      netlink_manager_(NetlinkManager::GetInstance()),
      min_frequencies_to_scan_(kMinumumFrequenciesToScan),
      max_frequencies_to_scan_(std::numeric_limits<int>::max()),
      scan_all_frequencies_(true),
      fraction_per_scan_(kDefaultFractionPerScan),
      scan_state_(kScanIdle),
      scan_method_(kScanMethodNone),
      receive_byte_count_at_connect_(0),
      num_set_wake_on_packet_retries_(0) {
  PropertyStore *store = this->mutable_store();
  store->RegisterDerivedString(
      kBgscanMethodProperty,
      StringAccessor(
          // TODO(petkov): CustomMappedAccessor is used for convenience because
          // it provides a way to define a custom clearer (unlike
          // CustomAccessor). We need to implement a fully custom accessor with
          // no extra argument.
          new CustomMappedAccessor<WiFi, string, int>(this,
                                                      &WiFi::ClearBgscanMethod,
                                                      &WiFi::GetBgscanMethod,
                                                      &WiFi::SetBgscanMethod,
                                                      0)));  // Unused.
  HelpRegisterDerivedUint16(store,
                            kBgscanShortIntervalProperty,
                            &WiFi::GetBgscanShortInterval,
                            &WiFi::SetBgscanShortInterval);
  HelpRegisterDerivedInt32(store,
                           kBgscanSignalThresholdProperty,
                           &WiFi::GetBgscanSignalThreshold,
                           &WiFi::SetBgscanSignalThreshold);

  store->RegisterDerivedKeyValueStore(
      kLinkStatisticsProperty,
      KeyValueStoreAccessor(
          new CustomAccessor<WiFi, KeyValueStore>(
              this, &WiFi::GetLinkStatistics, NULL)));

  // TODO(quiche): Decide if scan_pending_ is close enough to
  // "currently scanning" that we don't care, or if we want to track
  // scan pending/currently scanning/no scan scheduled as a tri-state
  // kind of thing.
  HelpRegisterConstDerivedBool(store,
                               kScanningProperty,
                               &WiFi::GetScanPending);
  HelpRegisterDerivedUint16(store,
                            kRoamThresholdProperty,
                            &WiFi::GetRoamThreshold,
                            &WiFi::SetRoamThreshold);
  HelpRegisterDerivedUint16(store,
                            kScanIntervalProperty,
                            &WiFi::GetScanInterval,
                            &WiFi::SetScanInterval);
  ScopeLogger::GetInstance()->RegisterScopeEnableChangedCallback(
      ScopeLogger::kWiFi,
      Bind(&WiFi::OnWiFiDebugScopeChanged, weak_ptr_factory_.GetWeakPtr()));
  CHECK(netlink_manager_);
  // TODO(wdg): Remove after progressive scan field trial is over.
  // Only do the field trial if the user hasn't already enabled progressive
  // scan manually.  crbug.com/250945
  ParseFieldTrialFile(FilePath(kProgressiveScanFieldTrialFlagFile));
  SLOG(WiFi, 2) << "WiFi device " << link_name() << " initialized.";
}

WiFi::~WiFi() {}

void WiFi::ParseFieldTrialFile(const FilePath &info_file_path) {
  FileReader file_reader;
  if (!file_reader.Open(info_file_path)) {
    SLOG(WiFi, 7) << "Not enrolled in progressive scan field trial.";
    return;
  }
  string line;
  file_reader.ReadLine(&line);
  switch (line[0]) {
    case '1':
    case '2':
      // The minimum and maximum are the same (which makes the fraction
      // irrelevant).  Every scan batch (except, possibly, the last) contains
      // exactly 4 frequencies.  These cases is optimized for users that use
      // connect to a few frequencies or that heavily prefer the top 4.
      min_frequencies_to_scan_ = 4;
      max_frequencies_to_scan_ = 4;
      fraction_per_scan_ = .34;
      progressive_scan_enabled_ = true;
      scan_configuration_ = "Progressive scan (field trial 1/2: min/max=4)";
      break;
    case '3':
    case '4':
      // The minimum and maximum are the same (which makes the fraction
      // irrelevant).  Every scan batch (except, possibly, the last) contains
      // exactly 8 frequencies.  These cases is optimized for users that use
      // several frequencies, each with similar likelihood.
      min_frequencies_to_scan_ = 8;
      max_frequencies_to_scan_ = 8;
      fraction_per_scan_ = .51;
      progressive_scan_enabled_ = true;
      scan_configuration_ = "Progressive scan (field trial 3/4: min/max=8)";
      break;
    case '5':
    case '6':
      // Does a single scan, only of previously-seen frequencies.  The idea is
      // that, in nearly all cases, we'll find a good BSS in a scan of all
      // previously seen frequencies and that, since about 75% of the users
      // (based on preliminary field trial data) have seen less than 6 or 7
      // frequencies and 50% (based on the same data) have less than 4, 'all
      // frequencies' is not too large of a group in the worst case and is a
      // pretty small group in more then half the cases.  Note that if we don't
      // find a BSS in a scan, the code falls back to a complete scan.  This
      // algorithm is represented by two identical groups to help determine
      // whether the size of the field trial groups are large enough to make the
      // results statistically significant.
      min_frequencies_to_scan_ = 1;
      max_frequencies_to_scan_ = std::numeric_limits<int>::max();
      fraction_per_scan_ = 1.1;
      scan_all_frequencies_ = false;
      progressive_scan_enabled_ = true;
      scan_configuration_ = (line[0] == '5') ?
          "Progressive scan (field trial 5: min=1/max=all, 100%, only-seen)" :
          "Progressive scan (field trial 6: min=1/max=all, 100%, only-seen)";
      break;
    case '7':
      // Uses different min/max values.  This allows machines that have a very
      // small set of previously-seen frequencies to have very short scan
      // times, machines that have a large set of previously-seen frequencies
      // to have their scans broken up to try to find a BSS without searching
      // all of those frequencies, and scans that don't find anything in the
      // previously-seen list to scan just the frequencies that haven't just
      // been scanned.
      min_frequencies_to_scan_ = 1;
      max_frequencies_to_scan_ = 4;
      // This is 1.0 rather than 1.1 so that we only get previously seen
      // frequencies until they are exhausted.
      fraction_per_scan_ = 1.0;
      progressive_scan_enabled_ = true;
      scan_configuration_ =
          "Progressive scan (field trial 7: min=1/max=4, 100%)";
      break;
    case 'c':
      // This is the control group; it uses traditional, full, scan.  It's the
      // same size as the other test groups.
      progressive_scan_enabled_ = false;
      scan_configuration_ = "Full scan (field trial c: control group)";
      break;
    case 'x':
      // This is the non-test group; it uses traditional, full, scan.  It
      // contains all users that aren't in one of the test groups.
      progressive_scan_enabled_ = false;
      scan_configuration_ = "Full scan (field trial x: default/disabled group)";
      break;
    default:
      progressive_scan_enabled_ = false;
      scan_configuration_ = "Full scan (field trial unknown)";
      break;
  }
  LOG(INFO) << "Progressive scan (via field_trial) "
            << (progressive_scan_enabled_ ? "enabled" : "disabled");
  if (progressive_scan_enabled_) {
    LOG(INFO) << "  min_frequencies_to_scan_ = " << min_frequencies_to_scan_;
    LOG(INFO) << "  max_frequencies_to_scan_ = " << max_frequencies_to_scan_;
    LOG(INFO) << "  fraction_per_scan_ = " << fraction_per_scan_;
  }

  file_reader.Close();
}

void WiFi::Start(Error *error,
                 const EnabledStateChangedCallback &/*callback*/) {
  SLOG(WiFi, 2) << "WiFi " << link_name() << " starting.";
  if (enabled()) {
    return;
  }
  OnEnabledStateChanged(EnabledStateChangedCallback(), Error());
  if (error) {
    error->Reset();       // indicate immediate completion
  }
  if (!supplicant_name_watcher_) {
    // Registers the WPA supplicant appear/vanish callbacks only once per WiFi
    // device instance.
    supplicant_name_watcher_.reset(manager()->dbus_manager()->CreateNameWatcher(
        WPASupplicant::kDBusAddr,
        Bind(&WiFi::OnSupplicantAppear, Unretained(this)),
        Bind(&WiFi::OnSupplicantVanish, Unretained(this))));
  }
  // Subscribe to multicast events.
  netlink_manager_->SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                      NetlinkManager::kEventTypeConfig);
  netlink_manager_->SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                      NetlinkManager::kEventTypeScan);
  netlink_manager_->SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                      NetlinkManager::kEventTypeRegulatory);
  netlink_manager_->SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                      NetlinkManager::kEventTypeMlme);
  GetPhyInfo();
  // Connect to WPA supplicant if it's already present. If not, we'll connect to
  // it when it appears.
  ConnectToSupplicant();
}

void WiFi::Stop(Error *error, const EnabledStateChangedCallback &/*callback*/) {
  SLOG(WiFi, 2) << "WiFi " << link_name() << " stopping.";
  // Unlike other devices, we leave the DBus name watcher in place here, because
  // WiFi callbacks expect notifications even if the device is disabled.
  DropConnection();
  StopScanTimer();
  for (const auto &endpoint : endpoint_by_rpcid_) {
    provider_->OnEndpointRemoved(endpoint.second);
  }
  endpoint_by_rpcid_.clear();
  for (const auto &map_entry : rpcid_by_service_) {
    RemoveNetwork(map_entry.second);
  }
  rpcid_by_service_.clear();
  supplicant_interface_proxy_.reset();  // breaks a reference cycle
  // TODO(quiche): Remove interface from supplicant.
  supplicant_process_proxy_.reset();
  current_service_ = NULL;            // breaks a reference cycle
  pending_service_ = NULL;            // breaks a reference cycle
  is_debugging_connection_ = false;
  SetScanState(kScanIdle, kScanMethodNone, __func__);
  StopPendingTimer();
  StopReconnectTimer();
  StopRequestingStationInfo();
  mac80211_monitor_->Stop();

  OnEnabledStateChanged(EnabledStateChangedCallback(), Error());
  if (error)
    error->Reset();       // indicate immediate completion
  weak_ptr_factory_.InvalidateWeakPtrs();

  SLOG(WiFi, 3) << "WiFi " << link_name() << " supplicant_process_proxy_ "
                << (supplicant_process_proxy_.get() ?
                    "is set." : "is not set.");
  SLOG(WiFi, 3) << "WiFi " << link_name() << " supplicant_interface_proxy_ "
                << (supplicant_interface_proxy_.get() ?
                    "is set." : "is not set.");
  SLOG(WiFi, 3) << "WiFi " << link_name() << " pending_service_ "
                << (pending_service_.get() ? "is set." : "is not set.");
  SLOG(WiFi, 3) << "WiFi " << link_name() << " has "
                << endpoint_by_rpcid_.size() << " EndpointMap entries.";
}

void WiFi::Scan(ScanType scan_type, Error */*error*/, const string &reason) {
  if ((scan_state_ != kScanIdle) ||
      (current_service_.get() && current_service_->IsConnecting())) {
    SLOG(WiFi, 2) << "Ignoring scan request while scanning or connecting.";
    return;
  }
  if (progressive_scan_enabled_ && scan_type == kProgressiveScan) {
    LOG(INFO) << __func__ << " [progressive] on " << link_name() << " from "
              << reason;
    LOG(INFO) << scan_configuration_;
    if (!scan_session_) {
      // TODO(wdg): Perform in-depth testing to determine the best values for
      // the different scans. chromium:235293
      ScanSession::FractionList scan_fractions;
      float total_fraction = 0.0;
      do {
        total_fraction += fraction_per_scan_;
        scan_fractions.push_back(fraction_per_scan_);
      } while (total_fraction < 1.0);
      scan_session_.reset(
          new ScanSession(netlink_manager_,
                          dispatcher(),
                          provider_->GetScanFrequencies(),
                          (scan_all_frequencies_ ? all_scan_frequencies_ :
                           set<uint16_t>()),
                          interface_index(),
                          scan_fractions,
                          min_frequencies_to_scan_,
                          max_frequencies_to_scan_,
                          Bind(&WiFi::OnFailedProgressiveScan,
                               weak_ptr_factory_.GetWeakPtr()),
                          metrics()));
      for (const auto &ssid : provider_->GetHiddenSSIDList()) {
        scan_session_->AddSsid(ByteString(&ssid.front(), ssid.size()));
      }
    }
    dispatcher()->PostTask(
        Bind(&WiFi::ProgressiveScanTask, weak_ptr_factory_.GetWeakPtr()));
  } else {
    LOG(INFO) << __func__ << " [full] on " << link_name()
              << " (progressive scan "
              << (progressive_scan_enabled_ ? "ENABLED" : "DISABLED")
              << ") from " << reason;
    // Needs to send a D-Bus message, but may be called from D-Bus
    // signal handler context (via Manager::RequestScan). So defer work
    // to event loop.
    dispatcher()->PostTask(
        Bind(&WiFi::ScanTask, weak_ptr_factory_.GetWeakPtr()));
  }
}

void WiFi::BSSAdded(const ::DBus::Path &path,
                    const map<string, ::DBus::Variant> &properties) {
  // Called from a D-Bus signal handler, and may need to send a D-Bus
  // message. So defer work to event loop.
  dispatcher()->PostTask(Bind(&WiFi::BSSAddedTask,
                              weak_ptr_factory_.GetWeakPtr(),
                              path, properties));
}

void WiFi::BSSRemoved(const ::DBus::Path &path) {
  // Called from a D-Bus signal handler, and may need to send a D-Bus
  // message. So defer work to event loop.
  dispatcher()->PostTask(Bind(&WiFi::BSSRemovedTask,
                              weak_ptr_factory_.GetWeakPtr(), path));
}

void WiFi::Certification(const map<string, ::DBus::Variant> &properties) {
  dispatcher()->PostTask(Bind(&WiFi::CertificationTask,
                              weak_ptr_factory_.GetWeakPtr(), properties));
}

void WiFi::EAPEvent(const string &status, const string &parameter) {
  dispatcher()->PostTask(Bind(&WiFi::EAPEventTask,
                              weak_ptr_factory_.GetWeakPtr(),
                              status,
                              parameter));
}

void WiFi::PropertiesChanged(const map<string, ::DBus::Variant> &properties) {
  SLOG(WiFi, 2) << __func__;
  // Called from D-Bus signal handler, but may need to send a D-Bus
  // message. So defer work to event loop.
  dispatcher()->PostTask(Bind(&WiFi::PropertiesChangedTask,
                              weak_ptr_factory_.GetWeakPtr(), properties));
}

void WiFi::ScanDone() {
  LOG(INFO) << __func__;

  // Defer handling of scan result processing, because that processing
  // may require the the registration of new D-Bus objects. And such
  // registration can't be done in the context of a D-Bus signal
  // handler.
  dispatcher()->PostTask(Bind(&WiFi::ScanDoneTask,
                              weak_ptr_factory_.GetWeakPtr()));
}

void WiFi::ConnectTo(WiFiService *service) {
  CHECK(service) << "Can't connect to NULL service.";
  DBus::Path network_path;

  // TODO(quiche): Handle cases where already connected.
  if (pending_service_ && pending_service_ == service) {
    // TODO(quiche): Return an error to the caller. crbug.com/206812
    LOG(INFO) << "WiFi " << link_name() << " ignoring ConnectTo service "
              << service->unique_name()
              << ", which is already pending.";
    return;
  }

  if (pending_service_ && pending_service_ != service) {
    LOG(INFO) << "Connecting to service. "
              << LogSSID(service->unique_name()) << ", "
              << "bssid: " << service->bssid() << ", "
              << "mode: " << service->mode() << ", "
              << "key management: " << service->key_management() << ", "
              << "physical mode: " << service->physical_mode() << ", "
              << "frequency: " << service->frequency();
    // This is a signal to SetPendingService(NULL) to not modify the scan
    // state since the overall story arc isn't reflected by the disconnect.
    // It is, instead, described by the transition to either kScanFoundNothing
    // or kScanConnecting (made by |SetPendingService|, below).
    if (scan_method_ != kScanMethodNone) {
      SetScanState(kScanTransitionToConnecting, scan_method_, __func__);
    }
    // Explicitly disconnect pending service.
    pending_service_->set_expecting_disconnect(true);
    DisconnectFrom(pending_service_);
  }

  Error unused_error;
  network_path = FindNetworkRpcidForService(service, &unused_error);
  if (network_path.empty()) {
    try {
      DBusPropertiesMap service_params =
          service->GetSupplicantConfigurationParameters();
      const uint32_t scan_ssid = 1;  // "True": Use directed probe.
      service_params[WPASupplicant::kNetworkPropertyScanSSID].writer().
          append_uint32(scan_ssid);
      AppendBgscan(service, &service_params);
      service_params[WPASupplicant::kNetworkPropertyDisableVHT].writer().
          append_uint32(provider_->disable_vht());
      network_path = supplicant_interface_proxy_->AddNetwork(service_params);
      CHECK(!network_path.empty());  // No DBus path should be empty.
      rpcid_by_service_[service] = network_path;
    } catch (const DBus::Error &e) {  // NOLINT
      LOG(ERROR) << "exception while adding network: " << e.what();
      SetScanState(kScanIdle, scan_method_, __func__);
      return;
    }
  }

  if (service->HasRecentConnectionIssues()) {
    SetConnectionDebugging(true);
  }
  supplicant_interface_proxy_->SelectNetwork(network_path);
  SetPendingService(service);
  CHECK(current_service_.get() != pending_service_.get());

  // SelectService here (instead of in LinkEvent, like Ethernet), so
  // that, if we fail to bring up L2, we can attribute failure correctly.
  //
  // TODO(quiche): When we add code for dealing with connection failures,
  // reconsider if this is the right place to change the selected service.
  // see discussion in crbug.com/203282.
  SelectService(service);
}

void WiFi::DisconnectFromIfActive(WiFiService *service) {
  SLOG(WiFi, 2) << __func__ << " service " << service->unique_name();

  if (service != current_service_ &&  service != pending_service_) {
    if (!service->IsActive(NULL)) {
      SLOG(WiFi, 2) << "In " << __func__ << "():  service "
                    << service->unique_name()
                    << " is not active, no need to initiate disconnect";
      return;
    }
  }

  DisconnectFrom(service);
}

void WiFi::DisconnectFrom(WiFiService *service) {
  SLOG(WiFi, 2) << __func__ << " service " << service->unique_name();

  if (service != current_service_ &&  service != pending_service_) {
    // TODO(quiche): Once we have asynchronous reply support, we should
    // generate a D-Bus error here. (crbug.com/206812)
    LOG(WARNING) << "In " << __func__ << "(): "
                 << " ignoring request to disconnect from service "
                 << service->unique_name()
                 << " which is neither current nor pending";
    return;
  }

  if (pending_service_ && service != pending_service_) {
    // TODO(quiche): Once we have asynchronous reply support, we should
    // generate a D-Bus error here. (crbug.com/206812)
    LOG(WARNING) << "In " << __func__ << "(): "
                 << " ignoring request to disconnect from service "
                 << service->unique_name()
                 << " which is not the pending service.";
    return;
  }

  if (!pending_service_ && service != current_service_) {
    // TODO(quiche): Once we have asynchronous reply support, we should
    // generate a D-Bus error here. (crbug.com/206812)
    LOG(WARNING) << "In " << __func__ << "(): "
                 << " ignoring request to disconnect from service "
                 << service->unique_name()
                 << " which is not the current service.";
    return;
  }

  if (pending_service_) {
    // Since wpa_supplicant has not yet set CurrentBSS, we can't depend
    // on this to drive the service state back to idle.  Do that here.
    // Update service state for pending service.
    ServiceDisconnected(pending_service_);
  }

  SetPendingService(NULL);
  StopReconnectTimer();
  StopRequestingStationInfo();

  if (!supplicant_present_) {
    LOG(ERROR) << "In " << __func__ << "(): "
               << "wpa_supplicant is not present; silently resetting "
               << "current_service_.";
    if (current_service_ == selected_service()) {
      DropConnection();
    }
    current_service_ = NULL;
    return;
  }

  bool disconnect_in_progress;
  try {
    supplicant_interface_proxy_->Disconnect();
    disconnect_in_progress = true;
    // We'll call RemoveNetwork and reset |current_service_| after
    // supplicant notifies us that the CurrentBSS has changed.
  } catch (const DBus::Error &e) {  // NOLINT
    disconnect_in_progress = false;
  }

  if (supplicant_state_ != WPASupplicant::kInterfaceStateCompleted ||
      !disconnect_in_progress) {
    // Can't depend on getting a notification of CurrentBSS change.
    // So effect changes immediately.  For instance, this can happen when
    // a disconnect is triggered by a BSS going away.
    Error unused_error;
    RemoveNetworkForService(service, &unused_error);
    if (service == selected_service()) {
      DropConnection();
    } else {
      SLOG(WiFi, 5) << __func__ << " skipping DropConnection, "
                    << "selected_service is "
                    << (selected_service() ?
                        selected_service()->unique_name() : "(null)");
    }
    current_service_ = NULL;
  }

  CHECK(current_service_ == NULL ||
        current_service_.get() != pending_service_.get());
}

bool WiFi::DisableNetwork(const ::DBus::Path &network) {
  scoped_ptr<SupplicantNetworkProxyInterface> supplicant_network_proxy(
      proxy_factory_->CreateSupplicantNetworkProxy(
          network, WPASupplicant::kDBusAddr));
  try {
    supplicant_network_proxy->SetEnabled(false);
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "DisableNetwork for " << network << " failed.";
    return false;
  }
  return true;
}

bool WiFi::RemoveNetwork(const ::DBus::Path &network) {
  try {
    supplicant_interface_proxy_->RemoveNetwork(network);
  } catch (const DBus::Error &e) {  // NOLINT
    // RemoveNetwork can fail with three different errors.
    //
    // If RemoveNetwork fails with a NetworkUnknown error, supplicant has
    // already removed the network object, so return true as if
    // RemoveNetwork removes the network object successfully.
    //
    // As shill always passes a valid network object path, RemoveNetwork
    // should not fail with an InvalidArgs error. Return false in such case
    // as something weird may have happened. Similarly, return false in case
    // of an UnknownError.
    if (strcmp(e.name(), WPASupplicant::kErrorNetworkUnknown) != 0) {
      return false;
    }
  }
  return true;
}

bool WiFi::IsIdle() const {
  return !current_service_ && !pending_service_;
}

void WiFi::ClearCachedCredentials(const WiFiService *service) {
  Error unused_error;
  RemoveNetworkForService(service, &unused_error);
}

void WiFi::NotifyEndpointChanged(const WiFiEndpointConstRefPtr &endpoint) {
  provider_->OnEndpointUpdated(endpoint);
}

void WiFi::AppendBgscan(WiFiService *service,
                        map<string, DBus::Variant> *service_params) const {
  int scan_interval = kBackgroundScanIntervalSeconds;
  string method = bgscan_method_;
  if (method.empty()) {
    // If multiple APs are detected for this SSID, configure the default method.
    // Otherwise, disable background scanning completely.
    if (service->GetEndpointCount() > 1) {
      method = kDefaultBgscanMethod;
    } else {
      LOG(INFO) << "Background scan disabled -- single Endpoint for Service.";
      return;
    }
  } else if (method.compare(WPASupplicant::kNetworkBgscanMethodNone) == 0) {
      LOG(INFO) << "Background scan disabled -- chose None method.";
      return;
  } else {
    // If the background scan method was explicitly specified, honor the
    // configured background scan interval.
    scan_interval = scan_interval_seconds_;
  }
  DCHECK(!method.empty());
  string config_string = StringPrintf("%s:%d:%d:%d",
                                      method.c_str(),
                                      bgscan_short_interval_seconds_,
                                      bgscan_signal_threshold_dbm_,
                                      scan_interval);
  LOG(INFO) << "Background scan: " << config_string;
  (*service_params)[WPASupplicant::kNetworkPropertyBgscan].writer()
      .append_string(config_string.c_str());
}

string WiFi::GetBgscanMethod(const int &/*argument*/, Error */* error */) {
  return bgscan_method_.empty() ? kDefaultBgscanMethod : bgscan_method_;
}

bool WiFi::SetBgscanMethod(
    const int &/*argument*/, const string &method, Error *error) {
  if (method != WPASupplicant::kNetworkBgscanMethodSimple &&
      method != WPASupplicant::kNetworkBgscanMethodLearn &&
      method != WPASupplicant::kNetworkBgscanMethodNone) {
    const string error_message =
        StringPrintf("Unrecognized bgscan method %s", method.c_str());
    LOG(WARNING) << error_message;
    error->Populate(Error::kInvalidArguments, error_message);
    return false;
  }
  if (bgscan_method_ == method) {
    return false;
  }
  bgscan_method_ = method;
  // We do not update kNetworkPropertyBgscan for |pending_service_| or
  // |current_service_|, because supplicant does not allow for
  // reconfiguration without disconnect and reconnect.
  return true;
}

bool WiFi::SetBgscanShortInterval(const uint16_t &seconds, Error */*error*/) {
  if (bgscan_short_interval_seconds_ == seconds) {
    return false;
  }
  bgscan_short_interval_seconds_ = seconds;
  // We do not update kNetworkPropertyBgscan for |pending_service_| or
  // |current_service_|, because supplicant does not allow for
  // reconfiguration without disconnect and reconnect.
  return true;
}

bool WiFi::SetBgscanSignalThreshold(const int32_t &dbm, Error */*error*/) {
  if (bgscan_signal_threshold_dbm_ == dbm) {
    return false;
  }
  bgscan_signal_threshold_dbm_ = dbm;
  // We do not update kNetworkPropertyBgscan for |pending_service_| or
  // |current_service_|, because supplicant does not allow for
  // reconfiguration without disconnect and reconnect.
  return true;
}

bool WiFi::SetRoamThreshold(const uint16_t &threshold, Error */*error*/) {
  roam_threshold_db_ = threshold;
  supplicant_interface_proxy_->SetRoamThreshold(threshold);
  return true;
}

bool WiFi::SetScanInterval(const uint16_t &seconds, Error */*error*/) {
  if (scan_interval_seconds_ == seconds) {
    return false;
  }
  scan_interval_seconds_ = seconds;
  if (running()) {
    StartScanTimer();
  }
  // The scan interval affects both foreground scans (handled by
  // |scan_timer_callback_|), and background scans (handled by
  // supplicant). However, we do not update |pending_service_| or
  // |current_service_|, because supplicant does not allow for
  // reconfiguration without disconnect and reconnect.
  return true;
}

void WiFi::ClearBgscanMethod(const int &/*argument*/, Error */*error*/) {
  bgscan_method_.clear();
}

void WiFi::CurrentBSSChanged(const ::DBus::Path &new_bss) {
  SLOG(WiFi, 3) << "WiFi " << link_name() << " CurrentBSS "
                << supplicant_bss_ << " -> " << new_bss;
  supplicant_bss_ = new_bss;
  has_already_completed_ = false;
  is_roaming_in_progress_ = false;

  // Any change in CurrentBSS means supplicant is actively changing our
  // connectivity.  We no longer need to track any previously pending
  // reconnect.
  StopReconnectTimer();
  StopRequestingStationInfo();

  if (new_bss == WPASupplicant::kCurrentBSSNull) {
    HandleDisconnect();
    if (!provider_->GetHiddenSSIDList().empty()) {
      // Before disconnecting, wpa_supplicant probably scanned for
      // APs. So, in the normal case, we defer to the timer for the next scan.
      //
      // However, in the case of hidden SSIDs, supplicant knows about
      // at most one of them. (That would be the hidden SSID we were
      // connected to, if applicable.)
      //
      // So, in this case, we initiate an immediate scan. This scan
      // will include the hidden SSIDs we know about (up to the limit of
      // kScanMAxSSIDsPerScan).
      //
      // We may want to reconsider this immediate scan, if/when shill
      // takes greater responsibility for scanning (vs. letting
      // supplicant handle most of it).
      Scan(kProgressiveScan, NULL, __func__);
    }
  } else {
    HandleRoam(new_bss);
  }

  // Reset the EAP handler only after calling HandleDisconnect() above
  // so our EAP state could be used to detect a failed authentication.
  eap_state_handler_->Reset();

  // If we are selecting a new service, or if we're clearing selection
  // of a something other than the pending service, call SelectService.
  // Otherwise skip SelectService, since this will cause the pending
  // service to be marked as Idle.
  if (current_service_ || selected_service() != pending_service_) {
    SelectService(current_service_);
  }

  // Invariant check: a Service can either be current, or pending, but
  // not both.
  CHECK(current_service_.get() != pending_service_.get() ||
        current_service_.get() == NULL);

  // If we are no longer debugging a problematic WiFi connection, return
  // to the debugging level indicated by the WiFi debugging scope.
  if ((!current_service_ || !current_service_->HasRecentConnectionIssues()) &&
      (!pending_service_ || !pending_service_->HasRecentConnectionIssues())) {
    SetConnectionDebugging(false);
  }
}

void WiFi::HandleDisconnect() {
  // Identify the affected service. We expect to get a disconnect
  // event when we fall off a Service that we were connected
  // to. However, we also allow for the case where we get a disconnect
  // event while attempting to connect from a disconnected state.
  WiFiService *affected_service =
      current_service_.get() ? current_service_.get() : pending_service_.get();

  if (!affected_service) {
    SLOG(WiFi, 2) << "WiFi " << link_name()
                  << " disconnected while not connected or connecting";
    return;
  }

  SLOG(WiFi, 2) << "WiFi " << link_name() << " disconnected from "
                << " (or failed to connect to) service "
                << affected_service->unique_name();

  if (affected_service == current_service_.get() && pending_service_.get()) {
    // Current service disconnected intentionally for network switching,
    // set service state to idle.
    affected_service->SetState(Service::kStateIdle);
  } else {
    // Perform necessary handling for disconnected service.
    ServiceDisconnected(affected_service);
  }

  current_service_ = NULL;

  if (affected_service == selected_service()) {
    // If our selected service has disconnected, destroy IP configuration state.
    DropConnection();
  }

  Error error;
  if (!DisableNetworkForService(affected_service, &error)) {
    if (error.type() == Error::kNotFound) {
      SLOG(WiFi, 2) << "WiFi " << link_name() << " disconnected from "
                    << " (or failed to connect to) service "
                    << affected_service->unique_name() << ", "
                    << "but could not find supplicant network to disable.";
    } else {
      LOG(FATAL) << "DisableNetwork failed on " << link_name()
                 << "for service " << affected_service->unique_name() << ".";
    }
  }

  metrics()->NotifySignalAtDisconnect(*affected_service,
                                      affected_service->SignalLevel());
  affected_service->NotifyCurrentEndpoint(NULL);
  metrics()->NotifyServiceDisconnect(*affected_service);

  if (affected_service == pending_service_.get()) {
    // The attempt to connect to |pending_service_| failed. Clear
    // |pending_service_|, to indicate we're no longer in the middle
    // of a connect request.
    SetPendingService(NULL);
  } else if (pending_service_.get()) {
    // We've attributed the disconnection to what was the
    // |current_service_|, rather than the |pending_service_|.
    //
    // If we're wrong about that (i.e. supplicant reported this
    // CurrentBSS change after attempting to connect to
    // |pending_service_|), we're depending on supplicant to retry
    // connecting to |pending_service_|, and delivering another
    // CurrentBSS change signal in the future.
    //
    // Log this fact, to help us debug (in case our assumptions are
    // wrong).
    SLOG(WiFi, 2) << "WiFi " << link_name() << " pending connection to service "
                  << pending_service_->unique_name()
                  << " after disconnect";
  }

  // If we disconnect, initially scan at a faster frequency, to make sure
  // we've found all available APs.
  RestartFastScanAttempts();
}

void WiFi::ServiceDisconnected(WiFiServiceRefPtr affected_service) {
  SLOG(WiFi, 2) << __func__ << " service " << affected_service->unique_name();

  // Check if service was explicitly disconnected due to failure or
  // is explicitly disconnected by user.
  if (!affected_service->IsInFailState() &&
      !affected_service->explicitly_disconnected() &&
      !affected_service->expecting_disconnect()) {
    // Determine disconnect failure reason.
    Service::ConnectFailure failure;
    if (SuspectCredentials(affected_service, &failure)) {
      // If we suspect bad credentials, set failure, to trigger an error
      // mole in Chrome.
      affected_service->SetFailure(failure);
      LOG(ERROR) << "Connection failure is due to suspect credentials: "
                 << "returning "
                 << Service::ConnectFailureToString(failure);
    } else {
      // Disconnected due to inability to connect to service, most likely
      // due to roaming out of range.
      LOG(ERROR) << "Disconnected due to inability to connect to the service.";
      affected_service->SetFailure(Service::kFailureOutOfRange);
    }
  }

  // Set service state back to idle, so this service can be used for
  // future connections.
  affected_service->SetState(Service::kStateIdle);
}

// We use the term "Roam" loosely. In particular, we include the case
// where we "Roam" to a BSS from the disconnected state.
void WiFi::HandleRoam(const ::DBus::Path &new_bss) {
  EndpointMap::iterator endpoint_it = endpoint_by_rpcid_.find(new_bss);
  if (endpoint_it == endpoint_by_rpcid_.end()) {
    LOG(WARNING) << "WiFi " << link_name() << " connected to unknown BSS "
                 << new_bss;
    return;
  }

  const WiFiEndpointConstRefPtr endpoint(endpoint_it->second);
  WiFiServiceRefPtr service = provider_->FindServiceForEndpoint(endpoint);
  if (!service.get()) {
      LOG(WARNING) << "WiFi " << link_name()
                   << " could not find Service for Endpoint "
                   << endpoint->bssid_string()
                   << " (service will be unchanged)";
      return;
  }

  SLOG(WiFi, 2) << "WiFi " << link_name()
                << " roamed to Endpoint " << endpoint->bssid_string()
                << " " << LogSSID(endpoint->ssid_string());

  service->NotifyCurrentEndpoint(endpoint);

  if (pending_service_.get() &&
      service.get() != pending_service_.get()) {
    // The Service we've roamed on to is not the one we asked for.
    // We assume that this is transient, and that wpa_supplicant
    // is trying / will try to connect to |pending_service_|.
    //
    // If it succeeds, we'll end up back here, but with |service|
    // pointing at the same service as |pending_service_|.
    //
    // If it fails, we'll process things in HandleDisconnect.
    //
    // So we leave |pending_service_| untouched.
    SLOG(WiFi, 2) << "WiFi " << link_name()
                  << " new current Endpoint "
                  << endpoint->bssid_string()
                  << " is not part of pending service "
                  << pending_service_->unique_name();

    // Sanity check: if we didn't roam onto |pending_service_|, we
    // should still be on |current_service_|.
    if (service.get() != current_service_.get()) {
      LOG(WARNING) << "WiFi " << link_name()
                   << " new current Endpoint "
                   << endpoint->bssid_string()
                   << " is neither part of pending service "
                   << pending_service_->unique_name()
                   << " nor part of current service "
                   << (current_service_ ?
                       current_service_->unique_name() :
                       "(NULL)");
      // wpa_supplicant has no knowledge of the pending_service_ at this point.
      // Disconnect the pending_service_, so that it can be connectable again.
      // Otherwise, we'd have to wait for the pending timeout to trigger the
      // disconnect. This will speed up the connection attempt process for
      // the pending_service_.
      DisconnectFrom(pending_service_);
    }
    return;
  }

  if (pending_service_.get()) {
    // We assume service.get() == pending_service_.get() here, because
    // of the return in the previous if clause.
    //
    // Boring case: we've connected to the service we asked
    // for. Simply update |current_service_| and |pending_service_|.
    current_service_ = service;
    SetScanState(kScanConnected, scan_method_, __func__);
    SetPendingService(NULL);
    return;
  }

  // |pending_service_| was NULL, so we weren't attempting to connect
  // to a new Service. Sanity check that we're still on
  // |current_service_|.
  if (service.get() != current_service_.get()) {
    LOG(WARNING)
        << "WiFi " << link_name()
        << " new current Endpoint "
        << endpoint->bssid_string()
        << (current_service_.get() ?
            StringPrintf(" is not part of current service %s",
                         current_service_->unique_name().c_str()) :
            " with no current service");
    // We didn't expect to be here, but let's cope as well as we
    // can. Update |current_service_| to keep it in sync with
    // supplicant.
    current_service_ = service;

    // If this service isn't already marked as actively connecting (likely,
    // since this service is a bit of a surprise) set the service as
    // associating.
    if (!current_service_->IsConnecting()) {
      current_service_->SetState(Service::kStateAssociating);
    }

    return;
  }

  // At this point, we know that |pending_service_| was NULL, and that
  // we're still on |current_service_|.  We should track this roaming
  // event so we can refresh our IPConfig if it succeeds.
  is_roaming_in_progress_ = true;

  return;
}

string WiFi::FindNetworkRpcidForService(
    const WiFiService *service, Error *error) {
  ReverseServiceMap::const_iterator rpcid_it = rpcid_by_service_.find(service);
  if (rpcid_it == rpcid_by_service_.end()) {
    const string error_message =
        StringPrintf(
            "WiFi %s cannot find supplicant network rpcid for service %s",
            link_name().c_str(), service->unique_name().c_str());
    // There are contexts where this is not an error, such as when a service
    // is clearing whatever cached credentials may not exist.
    SLOG(WiFi, 2) << error_message;
    if (error) {
      error->Populate(Error::kNotFound, error_message);
    }
    return "";
  }

  return rpcid_it->second;
}

bool WiFi::DisableNetworkForService(const WiFiService *service, Error *error) {
  string rpcid = FindNetworkRpcidForService(service, error);
  if (rpcid.empty()) {
      // Error is already populated.
      return false;
  }

  if (!DisableNetwork(rpcid)) {
    const string error_message =
        StringPrintf("WiFi %s cannot disable network for service %s: "
                     "DBus operation failed for rpcid %s.",
                     link_name().c_str(), service->unique_name().c_str(),
                     rpcid.c_str());
    Error::PopulateAndLog(error, Error::kOperationFailed, error_message);

    // Make sure that such errored networks are removed, so problems do not
    // propagate to future connection attempts.
    RemoveNetwork(rpcid);
    rpcid_by_service_.erase(service);

    return false;
  }

  return true;
}

bool WiFi::RemoveNetworkForService(const WiFiService *service, Error *error) {
  string rpcid = FindNetworkRpcidForService(service, error);
  if (rpcid.empty()) {
      // Error is already populated.
      return false;
  }

  // Erase the rpcid from our tables regardless of failure below, since even
  // if in failure, we never want to use this network again.
  rpcid_by_service_.erase(service);

  // TODO(quiche): Reconsider giving up immediately. Maybe give
  // wpa_supplicant some time to retry, first.
  if (!RemoveNetwork(rpcid)) {
    const string error_message =
        StringPrintf("WiFi %s cannot remove network for service %s: "
                     "DBus operation failed for rpcid %s.",
                     link_name().c_str(), service->unique_name().c_str(),
                     rpcid.c_str());
    Error::PopulateAndLog(error, Error::kOperationFailed, error_message);
    return false;
  }

  return true;
}

void WiFi::BSSAddedTask(
    const ::DBus::Path &path,
    const map<string, ::DBus::Variant> &properties) {
  // Note: we assume that BSSIDs are unique across endpoints. This
  // means that if an AP reuses the same BSSID for multiple SSIDs, we
  // lose.
  WiFiEndpointRefPtr endpoint(
      new WiFiEndpoint(proxy_factory_, this, path, properties));
  SLOG(WiFi, 5) << "Found endpoint. "
                << "RPC path: " << path << ", "
                << LogSSID(endpoint->ssid_string()) << ", "
                << "bssid: " << endpoint->bssid_string() << ", "
                << "signal: " << endpoint->signal_strength() << ", "
                << "security: " << endpoint->security_mode() << ", "
                << "frequency: " << endpoint->frequency();

  if (endpoint->ssid_string().empty()) {
    // Don't bother trying to find or create a Service for an Endpoint
    // without an SSID. We wouldn't be able to connect to it anyway.
    return;
  }

  if (endpoint->ssid()[0] == 0) {
    // Assume that an SSID starting with NULL is bogus/misconfigured,
    // and filter it out.
    return;
  }

  provider_->OnEndpointAdded(endpoint);

  // Do this last, to maintain the invariant that any Endpoint we
  // know about has a corresponding Service.
  //
  // TODO(quiche): Write test to verify correct behavior in the case
  // where we get multiple BSSAdded events for a single endpoint.
  // (Old Endpoint's refcount should fall to zero, and old Endpoint
  // should be destroyed.)
  endpoint_by_rpcid_[path] = endpoint;
  endpoint->Start();
}

void WiFi::BSSRemovedTask(const ::DBus::Path &path) {
  EndpointMap::iterator i = endpoint_by_rpcid_.find(path);
  if (i == endpoint_by_rpcid_.end()) {
    SLOG(WiFi, 1) << "WiFi " << link_name()
                  << " could not find BSS " << path
                  << " to remove.";
    return;
  }

  WiFiEndpointRefPtr endpoint = i->second;
  CHECK(endpoint);
  endpoint_by_rpcid_.erase(i);

  WiFiServiceRefPtr service = provider_->OnEndpointRemoved(endpoint);
  if (!service) {
    return;
  }
  Error unused_error;
  RemoveNetworkForService(service, &unused_error);

  bool disconnect_service = !service->HasEndpoints() &&
      (service->IsConnecting() || service->IsConnected());

  if (disconnect_service) {
    LOG(INFO) << "Disconnecting from service " << service->unique_name()
              << ": BSSRemoved";
    DisconnectFrom(service);
  }
}

void WiFi::CertificationTask(
    const map<string, ::DBus::Variant> &properties) {
  if (!current_service_) {
    LOG(ERROR) << "WiFi " << link_name() << " " << __func__
               << " with no current service.";
    return;
  }

  string subject;
  uint32_t depth;
  if (WPASupplicant::ExtractRemoteCertification(properties, &subject, &depth)) {
    current_service_->AddEAPCertification(subject, depth);
  }
}

void WiFi::EAPEventTask(const string &status, const string &parameter) {
  if (!current_service_) {
    LOG(ERROR) << "WiFi " << link_name() << " " << __func__
               << " with no current service.";
    return;
  }
  Service::ConnectFailure failure = Service::kFailureUnknown;
  eap_state_handler_->ParseStatus(status, parameter, &failure);
  if (failure == Service::kFailurePinMissing) {
    // wpa_supplicant can sometimes forget the PIN on disconnect from the AP.
    const string &pin = current_service_->eap()->pin();
    Error unused_error;
    string rpcid = FindNetworkRpcidForService(current_service_, &unused_error);
    if (!pin.empty() && !rpcid.empty()) {
      // We have a PIN configured, so we can provide it back to wpa_supplicant.
      LOG(INFO) << "Re-supplying PIN parameter to wpa_supplicant.";
      supplicant_interface_proxy_->NetworkReply(
          rpcid, WPASupplicant::kEAPRequestedParameterPIN, pin);
      failure = Service::kFailureUnknown;
    }
  }
  if (failure != Service::kFailureUnknown) {
    // Avoid a reporting failure twice by resetting EAP state handler early.
    eap_state_handler_->Reset();
    Error unused_error;
    current_service_->DisconnectWithFailure(failure, &unused_error, __func__);
  }
}

void WiFi::PropertiesChangedTask(
    const map<string, ::DBus::Variant> &properties) {
  // TODO(quiche): Handle changes in other properties (e.g. signal
  // strength).

  // Note that order matters here. In particular, we want to process
  // changes in the current BSS before changes in state. This is so
  // that we update the state of the correct Endpoint/Service.

  map<string, ::DBus::Variant>::const_iterator properties_it =
      properties.find(WPASupplicant::kInterfacePropertyCurrentBSS);
  if (properties_it != properties.end()) {
    CurrentBSSChanged(properties_it->second.reader().get_path());
  }

  properties_it = properties.find(WPASupplicant::kInterfacePropertyState);
  if (properties_it != properties.end()) {
    StateChanged(properties_it->second.reader().get_string());
  }
}

void WiFi::ScanDoneTask() {
  SLOG(WiFi, 2) << __func__ << " need_bss_flush_ " << need_bss_flush_;
  if (scan_session_) {
    // Post |ProgressiveScanTask| so it runs after any |BSSAddedTask|s that have
    // been posted.  This allows connections on new BSSes to be started before
    // we decide whether to abort the progressive scan or continue scanning.
    dispatcher()->PostTask(
        Bind(&WiFi::ProgressiveScanTask, weak_ptr_factory_.GetWeakPtr()));
  } else {
    // Post |UpdateScanStateAfterScanDone| so it runs after any |BSSAddedTask|s
    // that have been posted.  This allows connections on new BSSes to be
    // started before we decide whether the scan was fruitful.
    dispatcher()->PostTask(Bind(&WiFi::UpdateScanStateAfterScanDone,
                                weak_ptr_factory_.GetWeakPtr()));
  }
  if (need_bss_flush_) {
    CHECK(supplicant_interface_proxy_ != NULL);
    // Compute |max_age| relative to |resumed_at_|, to account for the
    // time taken to scan.
    struct timeval now;
    uint32_t max_age;
    time_->GetTimeMonotonic(&now);
    max_age = kMaxBSSResumeAgeSeconds + (now.tv_sec - resumed_at_.tv_sec);
    supplicant_interface_proxy_->FlushBSS(max_age);
    need_bss_flush_ = false;
  }
  StartScanTimer();
}

void WiFi::UpdateScanStateAfterScanDone() {
  if (scan_method_ == kScanMethodFull) {
    // Only notify the Manager on completion of full scans, since the manager
    // will replace any cached geolocation info with the BSSes we have right
    // now.
    manager()->OnDeviceGeolocationInfoUpdated(this);
  }
  if (scan_state_ == kScanBackgroundScanning) {
    // Going directly to kScanIdle (instead of to kScanFoundNothing) inhibits
    // some UMA reporting in SetScanState.  That's desired -- we don't want
    // to report background scan results to UMA since the drivers may play
    // background scans over a longer period in order to not interfere with
    // traffic.
    SetScanState(kScanIdle, kScanMethodNone, __func__);
  } else if (scan_state_ != kScanIdle && IsIdle()) {
    SetScanState(kScanFoundNothing, scan_method_, __func__);
  }
}

void WiFi::ScanTask() {
  SLOG(WiFi, 2) << "WiFi " << link_name() << " scan requested.";
  if (!enabled()) {
    SLOG(WiFi, 2) << "Ignoring scan request while device is not enabled.";
    SetScanState(kScanIdle, kScanMethodNone, __func__);  // Probably redundant.
    return;
  }
  if (!supplicant_present_ || !supplicant_interface_proxy_.get()) {
    SLOG(WiFi, 2) << "Ignoring scan request while supplicant is not present.";
    SetScanState(kScanIdle, kScanMethodNone, __func__);
    return;
  }
  if ((pending_service_.get() && pending_service_->IsConnecting()) ||
      (current_service_.get() && current_service_->IsConnecting())) {
    SLOG(WiFi, 2) << "Ignoring scan request while connecting to an AP.";
    return;
  }
  map<string, DBus::Variant> scan_args;
  scan_args[WPASupplicant::kPropertyScanType].writer().
      append_string(WPASupplicant::kScanTypeActive);

  ByteArrays hidden_ssids = provider_->GetHiddenSSIDList();
  if (!hidden_ssids.empty()) {
    // TODO(pstew): Devise a better method for time-sharing with SSIDs that do
    // not fit in.
    if (hidden_ssids.size() >= WPASupplicant::kScanMaxSSIDsPerScan) {
      hidden_ssids.erase(
          hidden_ssids.begin() + WPASupplicant::kScanMaxSSIDsPerScan - 1,
          hidden_ssids.end());
    }
    // Add Broadcast SSID, signified by an empty ByteArray.  If we specify
    // SSIDs to wpa_supplicant, we need to explicitly specify the default
    // behavior of doing a broadcast probe.
    hidden_ssids.push_back(ByteArray());

    scan_args[WPASupplicant::kPropertyScanSSIDs] =
        DBusAdaptor::ByteArraysToVariant(hidden_ssids);
  }

  try {
    supplicant_interface_proxy_->Scan(scan_args);
    // Only set the scan state/method if we are starting a full scan from
    // scratch.  Keep the existing method if this is a failover from a
    // progressive scan.
    if (scan_state_ != kScanScanning) {
      SetScanState(IsIdle() ? kScanScanning : kScanBackgroundScanning,
                   kScanMethodFull, __func__);
    }
  } catch (const DBus::Error &e) {  // NOLINT
    // A scan may fail if, for example, the wpa_supplicant vanishing
    // notification is posted after this task has already started running.
    LOG(WARNING) << "Scan failed: " << e.what();
  }
}

void WiFi::ProgressiveScanTask() {
  SLOG(WiFi, 2) << __func__ << " - scan requested for " << link_name();
  if (!enabled()) {
    LOG(INFO) << "Ignoring scan request while device is not enabled.";
    SetScanState(kScanIdle, kScanMethodNone, __func__);  // Probably redundant.
    return;
  }
  if (!scan_session_) {
    SLOG(WiFi, 2) << "No scan session -- returning";
    SetScanState(kScanIdle, kScanMethodNone, __func__);
    return;
  }
  // TODO(wdg): We don't currently support progressive background scans.  If
  // we did, we couldn't bail out, here, if we're connected. Progressive scan
  // state will have to be modified to include whether there was a connection
  // when the scan started. Then, this code would only bail out if we didn't
  // start with a connection but one exists at this point.
  if (!IsIdle()) {
    SLOG(WiFi, 2) << "Ignoring scan request while connecting to an AP.";
    scan_session_.reset();
    return;
  }
  if (scan_session_->HasMoreFrequencies()) {
    SLOG(WiFi, 2) << "Initiating a scan -- returning";
    SetScanState(kScanScanning, kScanMethodProgressive, __func__);
    // After us initiating a scan, supplicant will gather the scan results and
    // send us zero or more |BSSAdded| events followed by a |ScanDone|.
    scan_session_->InitiateScan();
    return;
  }
  LOG(ERROR) << "A complete progressive scan turned-up nothing -- "
             << "do a regular scan";
  scan_session_.reset();
  SetScanState(kScanScanning, kScanMethodProgressiveFinishedToFull, __func__);
  LOG(INFO) << "Scan [full] on " << link_name()
            << " (connected to nothing on progressive scan) from " << __func__;
  ScanTask();
}

void WiFi::OnFailedProgressiveScan() {
  LOG(ERROR) << "Couldn't issue a scan on " << link_name()
             << " -- doing a regular scan";
  scan_session_.reset();
  SetScanState(kScanScanning, kScanMethodProgressiveErrorToFull, __func__);
  LOG(INFO) << "Scan [full] on " << link_name()
            << " (failover from progressive scan) from " << __func__;
  ScanTask();
}

string WiFi::GetServiceLeaseName(const WiFiService &service) {
  return service.GetStorageIdentifier();
}

void WiFi::DestroyServiceLease(const WiFiService &service) {
  DestroyIPConfigLease(GetServiceLeaseName(service));
}

void WiFi::StateChanged(const string &new_state) {
  const string old_state = supplicant_state_;
  supplicant_state_ = new_state;
  LOG(INFO) << "WiFi " << link_name() << " " << __func__ << " "
            << old_state << " -> " << new_state;

  if (new_state == WPASupplicant::kInterfaceStateCompleted ||
      new_state == WPASupplicant::kInterfaceState4WayHandshake) {
    mac80211_monitor_->UpdateConnectedState(true);
  } else {
    mac80211_monitor_->UpdateConnectedState(false);
  }

  // Identify the service to which the state change applies. If
  // |pending_service_| is non-NULL, then the state change applies to
  // |pending_service_|. Otherwise, it applies to |current_service_|.
  //
  // This policy is driven by the fact that the |pending_service_|
  // doesn't become the |current_service_| until wpa_supplicant
  // reports a CurrentBSS change to the |pending_service_|. And the
  // CurrentBSS change won't be reported until the |pending_service_|
  // reaches the WPASupplicant::kInterfaceStateCompleted state.
  WiFiService *affected_service =
      pending_service_.get() ? pending_service_.get() : current_service_.get();
  if (!affected_service) {
    SLOG(WiFi, 2) << "WiFi " << link_name() << " " << __func__
                  << " with no service";
    return;
  }

  if (new_state == WPASupplicant::kInterfaceStateCompleted) {
    if (affected_service->IsConnected()) {
      StopReconnectTimer();
      EnableHighBitrates();
      if (is_roaming_in_progress_) {
        // This means wpa_supplicant completed a roam without an intervening
        // disconnect.  We should renew our DHCP lease just in case the new
        // AP is on a different subnet than where we started.
        is_roaming_in_progress_ = false;
        const IPConfigRefPtr &ip_config = ipconfig();
        if (ip_config) {
          LOG(INFO) << link_name() << " renewing L3 configuration after roam.";
          ip_config->RenewIP();
        }
      }
    } else if (has_already_completed_) {
      LOG(INFO) << link_name() << " L3 configuration already started.";
    } else {
      provider_->IncrementConnectCount(affected_service->frequency());
      if (AcquireIPConfigWithLeaseName(
              GetServiceLeaseName(*affected_service))) {
        LOG(INFO) << link_name() << " is up; started L3 configuration.";
        affected_service->SetState(Service::kStateConfiguring);
        if (affected_service->IsSecurityMatch(kSecurityWep)) {
          // With the overwhelming majority of WEP networks, we cannot assume
          // our credentials are correct just because we have successfully
          // connected.  It is more useful to track received data as the L3
          // configuration proceeds to see if we can decrypt anything.
          receive_byte_count_at_connect_ = GetReceiveByteCount();
        } else {
          affected_service->ResetSuspectedCredentialFailures();
        }
      } else {
        LOG(ERROR) << "Unable to acquire DHCP config.";
      }
    }
    has_already_completed_ = true;
  } else if (new_state == WPASupplicant::kInterfaceStateAssociated) {
    affected_service->SetState(Service::kStateAssociating);
  } else if (new_state == WPASupplicant::kInterfaceStateAuthenticating ||
             new_state == WPASupplicant::kInterfaceStateAssociating ||
             new_state == WPASupplicant::kInterfaceState4WayHandshake ||
             new_state == WPASupplicant::kInterfaceStateGroupHandshake) {
    // Ignore transitions into these states from Completed, to avoid
    // bothering the user when roaming, or re-keying.
    if (old_state != WPASupplicant::kInterfaceStateCompleted)
      affected_service->SetState(Service::kStateAssociating);
    // TODO(quiche): On backwards transitions, we should probably set
    // a timeout for getting back into the completed state. At present,
    // we depend on wpa_supplicant eventually reporting that CurrentBSS
    // has changed. But there may be cases where that signal is not sent.
    // (crbug.com/206208)
  } else if (new_state == WPASupplicant::kInterfaceStateDisconnected &&
             affected_service == current_service_ &&
             affected_service->IsConnected()) {
    // This means that wpa_supplicant failed in a re-connect attempt, but
    // may still be reconnecting.  Give wpa_supplicant a limited amount of
    // time to transition out this condition by either connecting or changing
    // CurrentBSS.
    StartReconnectTimer();
  } else {
    // Other transitions do not affect Service state.
    //
    // Note in particular that we ignore a State change into
    // kInterfaceStateDisconnected, in favor of observing the corresponding
    // change in CurrentBSS.
  }
}

bool WiFi::SuspectCredentials(
    WiFiServiceRefPtr service, Service::ConnectFailure *failure) const {
  if (service->IsSecurityMatch(kSecurityPsk)) {
    if (supplicant_state_ == WPASupplicant::kInterfaceState4WayHandshake &&
        service->AddSuspectedCredentialFailure()) {
      if (failure) {
        *failure = Service::kFailureBadPassphrase;
      }
      return true;
    }
  } else if (service->IsSecurityMatch(kSecurity8021x)) {
    if (eap_state_handler_->is_eap_in_progress() &&
        service->AddSuspectedCredentialFailure()) {
      if (failure) {
        *failure = Service::kFailureEAPAuthentication;
      }
      return true;
    }
  }

  return false;
}

// static
bool WiFi::SanitizeSSID(string *ssid) {
  CHECK(ssid);

  size_t ssid_len = ssid->length();
  size_t i;
  bool changed = false;

  for (i = 0; i < ssid_len; ++i) {
    if (!g_ascii_isprint((*ssid)[i])) {
      (*ssid)[i] = '?';
      changed = true;
    }
  }

  return changed;
}

// static
string WiFi::LogSSID(const string &ssid) {
  string out;
  for (const auto &chr : ssid) {
    // Replace '[' and ']' (in addition to non-printable characters) so that
    // it's easy to match the right substring through a non-greedy regex.
    if (chr == '[' || chr == ']' || !g_ascii_isprint(chr)) {
      base::StringAppendF(&out, "\\x%02x", chr);
    } else {
      out += chr;
    }
  }
  return StringPrintf("[SSID=%s]", out.c_str());
}

void WiFi::OnLinkMonitorFailure() {
  // If we have never found the gateway, let's be conservative and not
  // do anything, in case this network topology does not have a gateway.
  if (!link_monitor()->IsGatewayFound()) {
    LOG(INFO) << "In " << __func__ << "(): "
              << "Skipping reassociate since gateway was never found.";
    return;
  }

  if (!supplicant_present_) {
    LOG(ERROR) << "In " << __func__ << "(): "
               << "wpa_supplicant is not present.  Cannot reassociate.";
    return;
  }

  try {
    // This will force a transition out of connected, if we are actually
    // connected.
    supplicant_interface_proxy_->Reattach();
    // If we don't eventually get a transition back into a connected state,
    // there is something wrong.
    StartReconnectTimer();
    LOG(INFO) << "In " << __func__ << "(): Called Reattach().";
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "In " << __func__ << "(): failed to call Reattach().";
    return;
  }
}

bool WiFi::ShouldUseArpGateway() const {
  return !IsUsingStaticIP();
}

void WiFi::DisassociateFromService(const WiFiServiceRefPtr &service) {
  SLOG(WiFi, 2) << "In " << __func__ << " for service: "
                << service->unique_name();
  DisconnectFromIfActive(service);
  if (service == selected_service()) {
    DropConnection();
  }
  Error unused_error;
  RemoveNetworkForService(service, &unused_error);
}

vector<GeolocationInfo> WiFi::GetGeolocationObjects() const {
  vector<GeolocationInfo> objects;
  for (const auto &endpoint_entry : endpoint_by_rpcid_) {
    GeolocationInfo geoinfo;
    const WiFiEndpointRefPtr &endpoint = endpoint_entry.second;
    geoinfo.AddField(kGeoMacAddressProperty, endpoint->bssid_string());
    geoinfo.AddField(kGeoSignalStrengthProperty,
                StringPrintf("%d", endpoint->signal_strength()));
    geoinfo.AddField(
        kGeoChannelProperty,
        StringPrintf("%d",
                     Metrics::WiFiFrequencyToChannel(endpoint->frequency())));
    // TODO(gauravsh): Include age field. crbug.com/217554
    objects.push_back(geoinfo);
  }
  return objects;
}

void WiFi::HelpRegisterDerivedInt32(
    PropertyStore *store,
    const string &name,
    int32_t(WiFi::*get)(Error *error),
    bool(WiFi::*set)(const int32_t &value, Error *error)) {
  store->RegisterDerivedInt32(
      name,
      Int32Accessor(new CustomAccessor<WiFi, int32_t>(this, get, set)));
}

void WiFi::HelpRegisterDerivedUint16(
    PropertyStore *store,
    const string &name,
    uint16_t(WiFi::*get)(Error *error),
    bool(WiFi::*set)(const uint16_t &value, Error *error)) {
  store->RegisterDerivedUint16(
      name,
      Uint16Accessor(new CustomAccessor<WiFi, uint16_t>(this, get, set)));
}

void WiFi::HelpRegisterConstDerivedBool(
    PropertyStore *store,
    const string &name,
    bool(WiFi::*get)(Error *error)) {
  store->RegisterDerivedBool(
      name,
      BoolAccessor(new CustomAccessor<WiFi, bool>(this, get, NULL)));
}

void WiFi::OnBeforeSuspend() {
  LOG(INFO) << __func__;
  Device::OnBeforeSuspend();

  // Program NIC to wake on disconnects and packets from certain IP addresses
  // iff we have buffered wake on packet programming requests.
  if (!wake_on_packet_connections_.Empty()) {
    wake_on_wifi_triggers_.insert(WakeOnWiFi::kIPAddress);
    wake_on_wifi_triggers_.insert(WakeOnWiFi::kDisconnect);
  }
  ApplyWakeOnWiFiSettings();
}

void WiFi::OnAfterResume() {
  LOG(INFO) << __func__;
  Device::OnAfterResume();  // May refresh ipconfig_

  // Unconditionally disable wake on WiFi on resume.
  wake_on_wifi_triggers_.clear();
  ApplyWakeOnWiFiSettings();

  // We want to flush the BSS cache, but we don't want to conflict
  // with an active connection attempt. So record the need to flush,
  // and take care of flushing when the next scan completes.
  //
  // Note that supplicant will automatically expire old cache
  // entries (after, e.g., a BSS is not found in two consecutive
  // scans). However, our explicit flush accelerates re-association
  // in cases where a BSS disappeared while we were asleep. (See,
  // e.g. WiFiRoaming.005SuspendRoam.)
  time_->GetTimeMonotonic(&resumed_at_);
  need_bss_flush_ = true;

  // Abort any current scan (at the shill-level; let any request that's
  // already gone out finish) since we don't know when it started.
  AbortScan();

  if (IsIdle()) {
    // Not scanning/connecting/connected, so let's get things rolling.
    Scan(kProgressiveScan, NULL, __func__);
    RestartFastScanAttempts();
  } else {
    SLOG(WiFi, 1) << __func__
                  << " skipping scan, already connecting or connected.";
  }
}

void WiFi::AbortScan() {
  if (scan_session_) {
    scan_session_.reset();
  }
  SetScanState(kScanIdle, kScanMethodNone, __func__);
}

void WiFi::OnConnected() {
  Device::OnConnected();
  EnableHighBitrates();
  if (current_service_ &&
      current_service_->IsSecurityMatch(kSecurityWep)) {
    // With a WEP network, we are now reasonably certain the credentials are
    // correct, whereas with other network types we were able to determine
    // this earlier when the association process succeeded.
    current_service_->ResetSuspectedCredentialFailures();
  }
  RequestStationInfo();
}

void WiFi::OnIPConfigFailure() {
  if (!current_service_) {
    LOG(ERROR) << "WiFi " << link_name() << " " << __func__
               << " with no current service.";
    return;
  }
  if (current_service_->IsSecurityMatch(kSecurityWep) &&
      GetReceiveByteCount() == receive_byte_count_at_connect_ &&
      current_service_->AddSuspectedCredentialFailure()) {
    // If we've connected to a WEP network and haven't successfully
    // decrypted any bytes at all during the configuration process,
    // it is fair to suspect that our credentials to this network
    // may not be correct.
    Error error;
    current_service_->DisconnectWithFailure(Service::kFailureBadPassphrase,
                                            &error,
                                            __func__);
    return;
  }

  Device::OnIPConfigFailure();
}

void WiFi::AddWakeOnPacketConnection(const IPAddress &ip_endpoint,
                                     Error *error) {
  wake_on_packet_connections_.AddUnique(ip_endpoint);
}

void WiFi::RemoveWakeOnPacketConnection(const IPAddress &ip_endpoint,
                                        Error *error) {
  if (!wake_on_packet_connections_.Contains(ip_endpoint)) {
    Error::PopulateAndLog(error, Error::kNotFound,
                          "No such wake-on-packet connection registered");
    return;
  }
  wake_on_packet_connections_.Remove(ip_endpoint);
}

void WiFi::RemoveAllWakeOnPacketConnections(Error *error) {
  // Send an empty NL80211_CMD_SET_WOWLAN message to disable wowlan.
  wake_on_packet_connections_.Clear();
}

void WiFi::OnSetWakeOnPacketConnectionResponse(
    const Nl80211Message &nl80211_message) {
  // NOP because kernel does not send a response to NL80211_CMD_SET_WOWLAN
  // requests.
}

void WiFi::RequestWakeOnPacketSettings() {
  Error e;
  GetWakeOnPacketConnMessage get_wowlan_msg;
  if (!WakeOnWiFi::ConfigureGetWakeOnWiFiSettingsMessage(&get_wowlan_msg,
                                                         wiphy_index_, &e)) {
    LOG(ERROR) << e.message();
    return;
  }
  netlink_manager_->SendNl80211Message(
      &get_wowlan_msg, Bind(&WiFi::VerifyWakeOnWiFiSettings,
                            weak_ptr_factory_.GetWeakPtr()),
      Bind(&NetlinkManager::OnAckDoNothing),
      Bind(&NetlinkManager::OnNetlinkMessageError));
}

void WiFi::VerifyWakeOnWiFiSettings(
    const Nl80211Message &nl80211_message) {
  if (WakeOnWiFi::WakeOnWiFiSettingsMatch(nl80211_message,
                                          wake_on_wifi_triggers_,
                                          wake_on_packet_connections_)) {
    SLOG(WiFi, 2) << __func__ << ": "
                  << "Wake-on-packet settings successfully verified";
  } else {
    LOG(ERROR) << __func__ << " failed: discrepancy between wake-on-packet "
                              "settings on NIC and those in local data "
                              "structure detected";
    RetrySetWakeOnPacketConnections();
  }
}

void WiFi::ApplyWakeOnWiFiSettings() {
  Error error;
  if (wake_on_wifi_triggers_.empty()) {
    DisableWakeOnWiFi();
    return;
  }
  SetWakeOnPacketConnMessage set_wowlan_msg;
  if (!WakeOnWiFi::ConfigureSetWakeOnWiFiSettingsMessage(
          &set_wowlan_msg, wake_on_wifi_triggers_,
          wake_on_packet_connections_, wiphy_index_, &error)) {
    LOG(ERROR) << error.message();
    return;
  }
  netlink_manager_->SendNl80211Message(
      &set_wowlan_msg, Bind(&WiFi::OnSetWakeOnPacketConnectionResponse,
                            weak_ptr_factory_.GetWeakPtr()),
      Bind(&NetlinkManager::OnAckDoNothing),
      Bind(&NetlinkManager::OnNetlinkMessageError));

  verify_wake_on_packet_settings_callback_.Reset(
      Bind(&WiFi::RequestWakeOnPacketSettings, weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostDelayedTask(
      verify_wake_on_packet_settings_callback_.callback(),
      kVerifyWakeOnWiFiSettingsDelaySeconds * 1000);
}

void WiFi::DisableWakeOnWiFi() {
  Error error;
  SetWakeOnPacketConnMessage disable_wowlan_msg;
  if (!WakeOnWiFi::ConfigureDisableWakeOnWiFiMessage(&disable_wowlan_msg,
                                                     wiphy_index_, &error)) {
    LOG(ERROR) << error.message();
    return;
  }
  netlink_manager_->SendNl80211Message(
      &disable_wowlan_msg, Bind(&WiFi::OnSetWakeOnPacketConnectionResponse,
                                weak_ptr_factory_.GetWeakPtr()),
      Bind(&NetlinkManager::OnAckDoNothing),
      Bind(&NetlinkManager::OnNetlinkMessageError));

  verify_wake_on_packet_settings_callback_.Reset(
      Bind(&WiFi::RequestWakeOnPacketSettings, weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostDelayedTask(
      verify_wake_on_packet_settings_callback_.callback(),
      kVerifyWakeOnWiFiSettingsDelaySeconds * 1000);
}

void WiFi::RetrySetWakeOnPacketConnections() {
  if (num_set_wake_on_packet_retries_ < kMaxSetWakeOnPacketRetries) {
    Error e;
    SLOG(WiFi, 2) << __func__;
    ApplyWakeOnWiFiSettings();
    ++num_set_wake_on_packet_retries_;
  } else {
    SLOG(WiFi, 2) << __func__ << ": max retry attempts reached";
    num_set_wake_on_packet_retries_ = 0;
  }
}

void WiFi::RestartFastScanAttempts() {
  fast_scans_remaining_ = kNumFastScanAttempts;
  StartScanTimer();
}

void WiFi::StartScanTimer() {
  SLOG(WiFi, 2) << __func__;
  if (scan_interval_seconds_ == 0) {
    StopScanTimer();
    return;
  }
  scan_timer_callback_.Reset(
      Bind(&WiFi::ScanTimerHandler, weak_ptr_factory_.GetWeakPtr()));
  // Repeat the first few scans after disconnect relatively quickly so we
  // have reasonable trust that no APs we are looking for are present.
  size_t wait_time_milliseconds = fast_scans_remaining_ > 0 ?
      kFastScanIntervalSeconds * 1000 : scan_interval_seconds_ * 1000;
  dispatcher()->PostDelayedTask(scan_timer_callback_.callback(),
                                wait_time_milliseconds);
  SLOG(WiFi, 5) << "Next scan scheduled for " << wait_time_milliseconds << "ms";
}

void WiFi::StopScanTimer() {
  SLOG(WiFi, 2) << __func__;
  scan_timer_callback_.Cancel();
}

void WiFi::ScanTimerHandler() {
  SLOG(WiFi, 2) << "WiFi Device " << link_name() << ": " << __func__;
  if (scan_state_ == kScanIdle && IsIdle()) {
    Scan(kProgressiveScan, NULL, __func__);
    if (fast_scans_remaining_ > 0) {
      --fast_scans_remaining_;
    }
  } else {
    if (scan_state_ != kScanIdle) {
      SLOG(WiFi, 5) << "Skipping scan: scan_state_ is " << scan_state_;
    }
    if (current_service_) {
      SLOG(WiFi, 5) << "Skipping scan: current_service_ is service "
                    << current_service_->unique_name();
    }
    if (pending_service_) {
      SLOG(WiFi, 5) << "Skipping scan: pending_service_ is service"
                    << pending_service_->unique_name();
    }
  }
  StartScanTimer();
}

void WiFi::StartPendingTimer() {
  pending_timeout_callback_.Reset(
      Bind(&WiFi::PendingTimeoutHandler, weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostDelayedTask(pending_timeout_callback_.callback(),
                                kPendingTimeoutSeconds * 1000);
}

void WiFi::StopPendingTimer() {
  SLOG(WiFi, 2) << "WiFi Device " << link_name() << ": " << __func__;
  pending_timeout_callback_.Cancel();
}

void WiFi::SetPendingService(const WiFiServiceRefPtr &service) {
  SLOG(WiFi, 2) << "WiFi " << link_name() << " setting pending service to "
                << (service ? service->unique_name(): "NULL");
  if (service) {
    SetScanState(kScanConnecting, scan_method_, __func__);
    service->SetState(Service::kStateAssociating);
    StartPendingTimer();
  } else {
    // SetPendingService(NULL) is called in the following cases:
    //  a) |ConnectTo|->|DisconnectFrom|.  Connecting to a service, disconnect
    //     the old service (scan_state_ == kScanTransitionToConnecting).  No
    //     state transition is needed here.
    //  b) |HandleRoam|.  Connected to a service, it's no longer pending
    //     (scan_state_ == kScanIdle).  No state transition is needed here.
    //  c) |DisconnectFrom| and |HandleDisconnect|. Disconnected/disconnecting
    //     from a service not during a scan (scan_state_ == kScanIdle).  No
    //     state transition is needed here.
    //  d) |DisconnectFrom| and |HandleDisconnect|. Disconnected/disconnecting
    //     from a service during a scan (scan_state_ == kScanScanning or
    //     kScanConnecting).  This is an odd case -- let's discard any
    //     statistics we're gathering by transitioning directly into kScanIdle.
    if (scan_state_ == kScanScanning ||
        scan_state_ == kScanBackgroundScanning ||
        scan_state_ == kScanConnecting) {
      SetScanState(kScanIdle, kScanMethodNone, __func__);
    }
    if (pending_service_) {
      StopPendingTimer();
    }
  }
  pending_service_ = service;
}

void WiFi::PendingTimeoutHandler() {
  Error unused_error;
  LOG(INFO) << "WiFi Device " << link_name() << ": " << __func__;
  CHECK(pending_service_);
  SetScanState(kScanFoundNothing, scan_method_, __func__);
  WiFiServiceRefPtr pending_service = pending_service_;
  pending_service_->DisconnectWithFailure(
      Service::kFailureOutOfRange, &unused_error, __func__);

  // A hidden service may have no endpoints, since wpa_supplicant
  // failed to attain a CurrentBSS.  If so, the service has no
  // reference to |this| device and cannot call WiFi::DisconnectFrom()
  // to reset pending_service_.  In this case, we must perform the
  // disconnect here ourselves.
  if (pending_service_) {
    CHECK(!pending_service_->HasEndpoints());
    LOG(INFO) << "Hidden service was not found.";
    DisconnectFrom(pending_service_);
  }

  // DisconnectWithFailure will leave the pending service's state in failure
  // state. Reset its state back to idle, to allow it to be connectable again.
  pending_service->SetState(Service::kStateIdle);
}

void WiFi::StartReconnectTimer() {
  if (!reconnect_timeout_callback_.IsCancelled()) {
    LOG(INFO) << "WiFi Device " << link_name() << ": " << __func__
              << ": reconnect timer already running.";
    return;
  }
  LOG(INFO) << "WiFi Device " << link_name() << ": " << __func__;
  reconnect_timeout_callback_.Reset(
      Bind(&WiFi::ReconnectTimeoutHandler, weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostDelayedTask(reconnect_timeout_callback_.callback(),
                                kReconnectTimeoutSeconds * 1000);
}

void WiFi::StopReconnectTimer() {
  SLOG(WiFi, 2) << "WiFi Device " << link_name() << ": " << __func__;
  reconnect_timeout_callback_.Cancel();
}

void WiFi::ReconnectTimeoutHandler() {
  LOG(INFO) << "WiFi Device " << link_name() << ": " << __func__;
  reconnect_timeout_callback_.Cancel();
  CHECK(current_service_);
  current_service_->SetFailure(Service::kFailureConnect);
  DisconnectFrom(current_service_);
}

void WiFi::OnSupplicantAppear(const string &/*name*/, const string &/*owner*/) {
  LOG(INFO) << "WPA supplicant appeared.";
  if (supplicant_present_) {
    // Restart the WiFi device if it's started already. This will reset the
    // state and connect the device to the new WPA supplicant instance.
    if (enabled()) {
      Restart();
    }
    return;
  }
  supplicant_present_ = true;
  ConnectToSupplicant();
}

void WiFi::OnSupplicantVanish(const string &/*name*/) {
  LOG(INFO) << "WPA supplicant vanished.";
  if (!supplicant_present_) {
    return;
  }
  supplicant_present_ = false;
  // Restart the WiFi device if it's started already. This will effectively
  // suspend the device until the WPA supplicant reappears.
  if (enabled()) {
    Restart();
  }
}

void WiFi::OnWiFiDebugScopeChanged(bool enabled) {
  SLOG(WiFi, 2) << "WiFi debug scope changed; enable is now " << enabled;
  if (!supplicant_process_proxy_.get()) {
    SLOG(WiFi, 2) << "Supplicant process proxy not present.";
    return;
  }
  string current_level;
  try {
    current_level = supplicant_process_proxy_->GetDebugLevel();
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << __func__ << ": Failed to get wpa_supplicant debug level.";
    return;
  }

  if (current_level != WPASupplicant::kDebugLevelInfo &&
      current_level != WPASupplicant::kDebugLevelDebug) {
    SLOG(WiFi, 2) << "WiFi debug level is currently "
                  << current_level
                  << "; assuming that it is being controlled elsewhere.";
    return;
  }
  string new_level = enabled ? WPASupplicant::kDebugLevelDebug :
      WPASupplicant::kDebugLevelInfo;

  if (new_level == current_level) {
    SLOG(WiFi, 2) << "WiFi debug level is already the desired level "
                  << current_level;
    return;
  }

  try {
    supplicant_process_proxy_->SetDebugLevel(new_level);
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << __func__ << ": Failed to set wpa_supplicant debug level.";
  }
}

void WiFi::SetConnectionDebugging(bool enabled) {
  if (is_debugging_connection_ == enabled) {
    return;
  }
  OnWiFiDebugScopeChanged(
      enabled ||
      ScopeLogger::GetInstance()->IsScopeEnabled(ScopeLogger::kWiFi));
  is_debugging_connection_ = enabled;
}

void WiFi::ConnectToSupplicant() {
  LOG(INFO) << link_name() << ": " << (enabled() ? "enabled" : "disabled")
            << " supplicant: "
            << (supplicant_present_ ? "present" : "absent")
            << " proxy: "
            << (supplicant_process_proxy_.get() ? "non-null" : "null");
  if (!enabled() || !supplicant_present_ || supplicant_process_proxy_.get()) {
    return;
  }
  supplicant_process_proxy_.reset(
      proxy_factory_->CreateSupplicantProcessProxy(
          WPASupplicant::kDBusPath, WPASupplicant::kDBusAddr));
  OnWiFiDebugScopeChanged(
      ScopeLogger::GetInstance()->IsScopeEnabled(ScopeLogger::kWiFi));
  ::DBus::Path interface_path;
  try {
    map<string, DBus::Variant> create_interface_args;
    create_interface_args[WPASupplicant::kInterfacePropertyName].writer().
        append_string(link_name().c_str());
    create_interface_args[WPASupplicant::kInterfacePropertyDriver].writer().
        append_string(WPASupplicant::kDriverNL80211);
    create_interface_args[
        WPASupplicant::kInterfacePropertyConfigFile].writer().
        append_string(WPASupplicant::kSupplicantConfPath);
    interface_path =
        supplicant_process_proxy_->CreateInterface(create_interface_args);
  } catch (const DBus::Error &e) {  // NOLINT
    if (!strcmp(e.name(), WPASupplicant::kErrorInterfaceExists)) {
      interface_path =
          supplicant_process_proxy_->GetInterface(link_name());
      // TODO(quiche): Is it okay to crash here, if device is missing?
    } else {
      LOG(ERROR) << __func__ << ": Failed to create interface with supplicant.";
      return;
    }
  }

  supplicant_interface_proxy_.reset(
      proxy_factory_->CreateSupplicantInterfaceProxy(
          this, interface_path, WPASupplicant::kDBusAddr));

  RTNLHandler::GetInstance()->SetInterfaceFlags(interface_index(), IFF_UP,
                                                IFF_UP);
  // TODO(quiche) Set ApScan=1 and BSSExpireAge=190, like flimflam does?

  // Clear out any networks that might previously have been configured
  // for this interface.
  supplicant_interface_proxy_->RemoveAllNetworks();

  // Flush interface's BSS cache, so that we get BSSAdded signals for
  // all BSSes (not just new ones since the last scan).
  supplicant_interface_proxy_->FlushBSS(0);

  try {
    // TODO(pstew): Disable fast_reauth until supplicant can properly deal
    // with RADIUS servers that respond strangely to such requests.
    // crbug.com/208561
    supplicant_interface_proxy_->SetFastReauth(false);
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "Failed to disable fast_reauth. "
               << "May be running an older version of wpa_supplicant.";
  }

  try {
    supplicant_interface_proxy_->SetRoamThreshold(roam_threshold_db_);
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "Failed to set roam_threshold. "
               << "May be running an older version of wpa_supplicant.";
  }

  try {
    // Helps with passing WiFiRoaming.001SSIDSwitchBack.
    supplicant_interface_proxy_->SetScanInterval(kRescanIntervalSeconds);
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "Failed to set scan_interval. "
               << "May be running an older version of wpa_supplicant.";
  }

  try {
    supplicant_interface_proxy_->SetDisableHighBitrates(true);
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "Failed to disable high bitrates. "
               << "May be running an older version of wpa_supplicant.";
  }

  Scan(kProgressiveScan, NULL, __func__);
  StartScanTimer();
}

void WiFi::EnableHighBitrates() {
  LOG(INFO) << "Enabling high bitrates.";
  try {
    supplicant_interface_proxy_->EnableHighBitrates();
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "exception while enabling high rates: " << e.what();
  }
}

void WiFi::Restart() {
  LOG(INFO) << link_name() << " restarting.";
  WiFiRefPtr me = this;  // Make sure we don't get destructed.
  // Go through the manager rather than starting and stopping the device
  // directly so that the device can be configured with the profile.
  manager()->DeregisterDevice(me);
  manager()->RegisterDevice(me);
}

void WiFi::GetPhyInfo() {
  GetWiphyMessage get_wiphy;
  get_wiphy.attributes()->SetU32AttributeValue(NL80211_ATTR_IFINDEX,
                                               interface_index());
  netlink_manager_->SendNl80211Message(
      &get_wiphy,
      Bind(&WiFi::OnNewWiphy, weak_ptr_factory_.GetWeakPtr()),
      Bind(&NetlinkManager::OnAckDoNothing),
      Bind(&NetlinkManager::OnNetlinkMessageError));
}

void WiFi::OnNewWiphy(const Nl80211Message &nl80211_message) {
  // TODO(samueltan): parse NL80211_ATTR_WOWLAN_TRIGGERS_SUPPORTED to determine
  // out wake on WiFi capabilities of this device.
  // Verify NL80211_CMD_NEW_WIPHY.
  if (nl80211_message.command() != NewWiphyMessage::kCommand) {
    LOG(ERROR) << "Received unexpected command:"
               << nl80211_message.command();
    return;
  }

  if (!nl80211_message.const_attributes()->GetStringAttributeValue(
          NL80211_ATTR_WIPHY_NAME, &phy_name_)) {
    LOG(ERROR) << "NL80211_CMD_NEW_WIPHY had no NL80211_ATTR_WIPHY_NAME";
    return;
  }
  mac80211_monitor_->Start(phy_name_);

  if (!nl80211_message.const_attributes()->GetU32AttributeValue(
      NL80211_ATTR_WIPHY, &wiphy_index_)) {
    LOG(ERROR) << "NL80211_CMD_NEW_WIPHY had no NL80211_ATTR_WIPHY";
    return;
  }

  // The attributes, for this message, are complicated.
  // NL80211_ATTR_BANDS contains an array of bands...
  AttributeListConstRefPtr wiphy_bands;
  if (!nl80211_message.const_attributes()->ConstGetNestedAttributeList(
      NL80211_ATTR_WIPHY_BANDS, &wiphy_bands)) {
    LOG(ERROR) << "NL80211_CMD_NEW_WIPHY had no NL80211_ATTR_WIPHY_BANDS";
    return;
  }

  AttributeIdIterator band_iter(*wiphy_bands);
  for (; !band_iter.AtEnd(); band_iter.Advance()) {
    AttributeListConstRefPtr wiphy_band;
    if (!wiphy_bands->ConstGetNestedAttributeList(band_iter.GetId(),
                                                  &wiphy_band)) {
      LOG(WARNING) << "WiFi band " << band_iter.GetId() << " not found";
      continue;
    }

    // ...Each band has a FREQS attribute...
    AttributeListConstRefPtr frequencies;
    if (!wiphy_band->ConstGetNestedAttributeList(NL80211_BAND_ATTR_FREQS,
                                                 &frequencies)) {
      LOG(ERROR) << "BAND " << band_iter.GetId()
                 << " had no 'frequencies' attribute";
      continue;
    }

    // ...And each FREQS attribute contains an array of information about the
    // frequency...
    AttributeIdIterator freq_iter(*frequencies);
    for (; !freq_iter.AtEnd(); freq_iter.Advance()) {
      AttributeListConstRefPtr frequency;
      if (frequencies->ConstGetNestedAttributeList(freq_iter.GetId(),
                                                   &frequency)) {
        // ...Including the frequency, itself (the part we want).
        uint32_t frequency_value = 0;
        if (frequency->GetU32AttributeValue(NL80211_FREQUENCY_ATTR_FREQ,
                                            &frequency_value)) {
          SLOG(WiFi, 7) << "Found frequency[" << freq_iter.GetId()
                        << "] = " << frequency_value;
          all_scan_frequencies_.insert(frequency_value);
        }
      }
    }
  }
}

KeyValueStore WiFi::GetLinkStatistics(Error */*error*/) {
  return link_statistics_;
}

bool WiFi::GetScanPending(Error */* error */) {
  return scan_state_ == kScanScanning || scan_state_ == kScanBackgroundScanning;
}

void WiFi::SetScanState(ScanState new_state,
                        ScanMethod new_method,
                        const char *reason) {
  if (new_state == kScanIdle)
    new_method = kScanMethodNone;
  if (new_state == kScanConnected) {
    // The scan method shouldn't be changed by the connection process, so
    // we'll put a CHECK, here, to verify.  NOTE: this assumption is also
    // enforced by the parameters to the call to |ReportScanResultToUma|.
    CHECK(new_method == scan_method_);
  }

  int log_level = 6;
  bool state_or_method_changed = true;
  bool is_terminal_state = false;
  if (new_state == scan_state_ && new_method == scan_method_) {
    log_level = 7;
    state_or_method_changed = false;
  } else if (new_state == kScanConnected || new_state == kScanFoundNothing) {
    // These 'terminal' states are slightly more interesting than the
    // intermediate states.
    // NOTE: Since background scan goes directly to kScanIdle (skipping over
    // the states required to set |is_terminal_state|), ReportScanResultToUma,
    // below, doesn't get called.  That's intentional.
    log_level = 5;
    is_terminal_state = true;
  }

  base::TimeDelta elapsed_time;
  if (new_state == kScanScanning || new_state == kScanBackgroundScanning) {
    if (!scan_timer_.Start()) {
      LOG(ERROR) << "Scan start unreliable";
    }
  } else {
    if (!scan_timer_.GetElapsedTime(&elapsed_time)) {
      LOG(ERROR) << "Scan time unreliable";
    }
  }
  SLOG(WiFi, log_level) << (reason ? reason : "<unknown>")
                        << " - " << link_name()
                        << ": Scan state: "
                        << ScanStateString(scan_state_, scan_method_)
                        << " -> " << ScanStateString(new_state, new_method)
                        << " @ " << elapsed_time.InMillisecondsF()
                        << " ms into scan.";
  if (!state_or_method_changed)
    return;

  // Actually change the state.
  ScanState old_state = scan_state_;
  ScanMethod old_method = scan_method_;
  bool old_scan_pending = GetScanPending(NULL);
  scan_state_ = new_state;
  scan_method_ = new_method;
  bool new_scan_pending = GetScanPending(NULL);
  if (old_scan_pending != new_scan_pending) {
    adaptor()->EmitBoolChanged(kScanningProperty, new_scan_pending);
  }
  switch (new_state) {
    case kScanIdle:
      metrics()->ResetScanTimer(interface_index());
      metrics()->ResetConnectTimer(interface_index());
      if (scan_session_) {
        scan_session_.reset();
      }
      break;
    case kScanScanning:  // FALLTHROUGH
    case kScanBackgroundScanning:
      if (new_state != old_state) {
        metrics()->NotifyDeviceScanStarted(interface_index());
      }
      break;
    case kScanConnecting:
      metrics()->NotifyDeviceScanFinished(interface_index());
      // TODO(wdg): Provide |is_auto_connecting| to this interface.  For now,
      // I'll lie (because I don't care about the auto-connect metrics).
      metrics()->NotifyDeviceConnectStarted(interface_index(), false);
      break;
    case kScanConnected:
      metrics()->NotifyDeviceConnectFinished(interface_index());
      break;
    case kScanFoundNothing:
      // Note that finishing a scan that hasn't started (if, for example, we
      // get here when we fail to complete a connection) does nothing.
      metrics()->NotifyDeviceScanFinished(interface_index());
      metrics()->ResetConnectTimer(interface_index());
      break;
    case kScanTransitionToConnecting:  // FALLTHROUGH
    default:
      break;
  }
  if (is_terminal_state) {
    ReportScanResultToUma(new_state, old_method);
    // Now that we've logged a terminal state, let's call ourselves to
    // transition to the idle state.
    SetScanState(kScanIdle, kScanMethodNone, reason);
  }
}

// static
string WiFi::ScanStateString(ScanState state, ScanMethod method) {
  switch (state) {
    case kScanIdle:
      return "IDLE";
    case kScanScanning:
      DCHECK(method != kScanMethodNone) << "Scanning with no scan method.";
      switch (method) {
        case kScanMethodFull:
          return "FULL_START";
        case kScanMethodProgressive:
          return "PROGRESSIVE_START";
        case kScanMethodProgressiveErrorToFull:
          return "PROGRESSIVE_ERROR_FULL_START";
        case kScanMethodProgressiveFinishedToFull:
          return "PROGRESSIVE_FINISHED_FULL_START";
        default:
          NOTREACHED();
      }
    case kScanBackgroundScanning:
      return "BACKGROUND_START";
    case kScanTransitionToConnecting:
      return "TRANSITION_TO_CONNECTING";
    case kScanConnecting:
      switch (method) {
        case kScanMethodNone:
          return "CONNECTING (not scan related)";
        case kScanMethodFull:
          return "FULL_CONNECTING";
        case kScanMethodProgressive:
          return "PROGRESSIVE_CONNECTING";
        case kScanMethodProgressiveErrorToFull:
          return "PROGRESSIVE_ERROR_FULL_CONNECTING";
        case kScanMethodProgressiveFinishedToFull:
          return "PROGRESSIVE_FINISHED_FULL_CONNECTING";
        default:
          NOTREACHED();
      }
    case kScanConnected:
      switch (method) {
        case kScanMethodNone:
          return "CONNECTED (not scan related; e.g., from a supplicant roam)";
        case kScanMethodFull:
          return "FULL_CONNECTED";
        case kScanMethodProgressive:
          return "PROGRESSIVE_CONNECTED";
        case kScanMethodProgressiveErrorToFull:
          return "PROGRESSIVE_ERROR_FULL_CONNECTED";
        case kScanMethodProgressiveFinishedToFull:
          return "PROGRESSIVE_FINISHED_FULL_CONNECTED";
        default:
          NOTREACHED();
      }
    case kScanFoundNothing:
      switch (method) {
        case kScanMethodNone:
          return "CONNECT FAILED (not scan related)";
        case kScanMethodFull:
          return "FULL_NOCONNECTION";
        case kScanMethodProgressive:
          // This is possible if shill started to connect but timed out before
          // the connection was completed.
          return "PROGRESSIVE_FINISHED_NOCONNECTION";
        case kScanMethodProgressiveErrorToFull:
          return "PROGRESSIVE_ERROR_FULL_NOCONNECTION";
        case kScanMethodProgressiveFinishedToFull:
          return "PROGRESSIVE_FINISHED_FULL_NOCONNECTION";
        default:
          NOTREACHED();
      }
    default:
      NOTREACHED();
  }
  return "";  // To shut up the compiler (that doesn't understand NOTREACHED).
}

void WiFi::ReportScanResultToUma(ScanState state, ScanMethod method) {
  Metrics::WiFiScanResult result = Metrics::kScanResultMax;
  if (state == kScanConnected) {
    switch (method) {
      case kScanMethodFull:
        result = Metrics::kScanResultFullScanConnected;
        break;
      case kScanMethodProgressive:
        result = Metrics::kScanResultProgressiveConnected;
        break;
      case kScanMethodProgressiveErrorToFull:
        result = Metrics::kScanResultProgressiveErrorButFullConnected;
        break;
      case kScanMethodProgressiveFinishedToFull:
        result = Metrics::kScanResultProgressiveAndFullConnected;
        break;
      default:
        // OK: Connect resulting from something other than scan.
        break;
    }
  } else if (state == kScanFoundNothing) {
    switch (method) {
      case kScanMethodFull:
        result = Metrics::kScanResultFullScanFoundNothing;
        break;
      case kScanMethodProgressiveErrorToFull:
        result = Metrics::kScanResultProgressiveErrorAndFullFoundNothing;
        break;
      case kScanMethodProgressiveFinishedToFull:
        result = Metrics::kScanResultProgressiveAndFullFoundNothing;
        break;
      default:
        // OK: Connect failed, not scan related.
        break;
    }
  }

  if (result != Metrics::kScanResultMax) {
    metrics()->SendEnumToUMA(Metrics::kMetricScanResult,
                             result,
                             Metrics::kScanResultMax);
  }
}

void WiFi::RequestStationInfo() {
  if (!current_service_ || !current_service_->IsConnected()) {
    LOG(ERROR) << "Not collecting station info because we are not connected.";
    return;
  }

  EndpointMap::iterator endpoint_it = endpoint_by_rpcid_.find(supplicant_bss_);
  if (endpoint_it == endpoint_by_rpcid_.end()) {
    LOG(ERROR) << "Can't get endpoint for current supplicant BSS "
               << supplicant_bss_;
    return;
  }

  GetStationMessage get_station;
  if (!get_station.attributes()->SetU32AttributeValue(NL80211_ATTR_IFINDEX,
                                                      interface_index())) {
    LOG(ERROR) << "Could not add IFINDEX attribute for GetStation message.";
    return;
  }

  const WiFiEndpointConstRefPtr endpoint(endpoint_it->second);
  if (!get_station.attributes()->SetRawAttributeValue(
          NL80211_ATTR_MAC,
          ByteString::CreateFromHexString(endpoint->bssid_hex()))) {
    LOG(ERROR) << "Could not add MAC attribute for GetStation message.";
    return;
  }

  netlink_manager_->SendNl80211Message(
      &get_station,
      Bind(&WiFi::OnReceivedStationInfo, weak_ptr_factory_.GetWeakPtr()),
      Bind(&NetlinkManager::OnAckDoNothing),
      Bind(&NetlinkManager::OnNetlinkMessageError));

  request_station_info_callback_.Reset(
      Bind(&WiFi::RequestStationInfo, weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostDelayedTask(request_station_info_callback_.callback(),
                                kRequestStationInfoPeriodSeconds * 1000);
}

void WiFi::OnReceivedStationInfo(const Nl80211Message &nl80211_message) {
  // Verify NL80211_CMD_NEW_STATION
  if (nl80211_message.command() != NewStationMessage::kCommand) {
    LOG(ERROR) << "Received unexpected command:"
               << nl80211_message.command();
    return;
  }

  if (!current_service_ || !current_service_->IsConnected()) {
    LOG(ERROR) << "Not accepting station info because we are not connected.";
    return;
  }

  EndpointMap::iterator endpoint_it = endpoint_by_rpcid_.find(supplicant_bss_);
  if (endpoint_it == endpoint_by_rpcid_.end()) {
    LOG(ERROR) << "Can't get endpoint for current supplicant BSS."
               << supplicant_bss_;
    return;
  }

  ByteString station_bssid;
  if (!nl80211_message.const_attributes()->GetRawAttributeValue(
          NL80211_ATTR_MAC, &station_bssid)) {
    LOG(ERROR) << "Unable to get MAC attribute from received station info.";
    return;
  }

  WiFiEndpointRefPtr endpoint(endpoint_it->second);

  if (!station_bssid.Equals(
          ByteString::CreateFromHexString(endpoint->bssid_hex()))) {
    LOG(ERROR) << "Received station info for a non-current BSS.";
    return;
  }

  AttributeListConstRefPtr station_info;
  if (!nl80211_message.const_attributes()->ConstGetNestedAttributeList(
      NL80211_ATTR_STA_INFO, &station_info)) {
    LOG(ERROR) << "Received station info had no NL80211_ATTR_STA_INFO.";
    return;
  }

  uint8_t signal;
  if (!station_info->GetU8AttributeValue(NL80211_STA_INFO_SIGNAL, &signal)) {
    LOG(ERROR) << "Received station info had no NL80211_STA_INFO_SIGNAL.";
    return;
  }

  endpoint->UpdateSignalStrength(static_cast<signed char>(signal));

  link_statistics_.Clear();

  map<int, string> u32_property_map = {
      { NL80211_STA_INFO_INACTIVE_TIME, kInactiveTimeMillisecondsProperty },
      { NL80211_STA_INFO_RX_PACKETS, kPacketReceiveSuccessesProperty },
      { NL80211_STA_INFO_TX_FAILED, kPacketTransmitFailuresProperty },
      { NL80211_STA_INFO_TX_PACKETS, kPacketTransmitSuccessesProperty },
      { NL80211_STA_INFO_TX_RETRIES, kTransmitRetriesProperty }
  };

  for (const auto &kv : u32_property_map) {
    uint32_t value;
    if (station_info->GetU32AttributeValue(kv.first, &value)) {
      link_statistics_.SetUint(kv.second, value);
    }
  }

  map<int, string> s8_property_map = {
      { NL80211_STA_INFO_SIGNAL, kLastReceiveSignalDbmProperty },
      { NL80211_STA_INFO_SIGNAL_AVG, kAverageReceiveSignalDbmProperty }
  };

  for (const auto &kv : s8_property_map) {
    uint8_t value;
    if (station_info->GetU8AttributeValue(kv.first, &value)) {
      // Despite these values being reported as a U8 by the kernel, these
      // should be interpreted as signed char.
      link_statistics_.SetInt(kv.second, static_cast<signed char>(value));
    }
  }

  AttributeListConstRefPtr transmit_info;
  if (station_info->ConstGetNestedAttributeList(
      NL80211_STA_INFO_TX_BITRATE, &transmit_info)) {
    uint32_t rate = 0;  // In 100Kbps.
    uint16_t u16_rate = 0;  // In 100Kbps.
    uint8_t mcs = 0;
    uint8_t nss = 0;
    bool band_flag = false;
    bool is_short_gi = false;
    string mcs_info;
    string nss_info;
    string band_info;

    if (transmit_info->GetU16AttributeValue(
        NL80211_RATE_INFO_BITRATE, &u16_rate)) {
      rate = static_cast<uint32_t>(u16_rate);
    } else {
      transmit_info->GetU32AttributeValue(NL80211_RATE_INFO_BITRATE32, &rate);
    }

    if (transmit_info->GetU8AttributeValue(NL80211_RATE_INFO_MCS, &mcs)) {
      mcs_info = StringPrintf(" MCS %d", mcs);
    } else if (transmit_info->GetU8AttributeValue(
        NL80211_RATE_INFO_VHT_MCS, &mcs)) {
      mcs_info = StringPrintf(" VHT-MCS %d", mcs);
    }

    if (transmit_info->GetU8AttributeValue(NL80211_RATE_INFO_VHT_NSS, &nss)) {
      nss_info = StringPrintf(" VHT-NSS %d", nss);
    }

    if (transmit_info->GetFlagAttributeValue(NL80211_RATE_INFO_40_MHZ_WIDTH,
                                             &band_flag) && band_flag) {
      band_info = StringPrintf(" 40MHz");
    } else if (transmit_info->GetFlagAttributeValue(
        NL80211_RATE_INFO_80_MHZ_WIDTH, &band_flag) && band_flag) {
      band_info = StringPrintf(" 80MHz");
    } else if (transmit_info->GetFlagAttributeValue(
        NL80211_RATE_INFO_80P80_MHZ_WIDTH, &band_flag) && band_flag) {
      band_info = StringPrintf(" 80+80MHz");
    } else if (transmit_info->GetFlagAttributeValue(
        NL80211_RATE_INFO_160_MHZ_WIDTH, &band_flag) && band_flag) {
      band_info = StringPrintf(" 160MHz");
    }

    transmit_info->GetFlagAttributeValue(NL80211_RATE_INFO_SHORT_GI,
                                         &is_short_gi);
    if (rate) {
      link_statistics_.SetString(kTransmitBitrateProperty,
                                 StringPrintf("%d.%d MBit/s%s%s%s%s",
                                              rate / 10, rate % 10,
                                              mcs_info.c_str(),
                                              band_info.c_str(),
                                              is_short_gi ? " short GI" : "",
                                              nss_info.c_str()));
      metrics()->NotifyWifiTxBitrate(rate/10);
    }
  }
}

void WiFi::StopRequestingStationInfo() {
  SLOG(WiFi, 2) << "WiFi Device " << link_name() << ": " << __func__;
  request_station_info_callback_.Cancel();
  link_statistics_.Clear();
}

bool WiFi::TDLSDiscover(const string &peer) {
  try {
    supplicant_interface_proxy_->TDLSDiscover(peer);
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "exception while performing TDLS discover: " << e.what();
    return false;
  }
  return true;
}

bool WiFi::TDLSSetup(const string &peer) {
  try {
    supplicant_interface_proxy_->TDLSSetup(peer);
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "exception while performing TDLS setup: " << e.what();
    return false;
  }
  return true;
}

string WiFi::TDLSStatus(const string &peer) {
  try {
    return supplicant_interface_proxy_->TDLSStatus(peer);
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "exception while getting TDLS status: " << e.what();
    return "";
  }
}

bool WiFi::TDLSTeardown(const string &peer) {
  try {
    supplicant_interface_proxy_->TDLSTeardown(peer);
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "exception while performing TDLS teardown: " << e.what();
    return false;
  }
  return true;
}

string WiFi::PerformTDLSOperation(const string &operation,
                                  const string &peer,
                                  Error *error) {
  bool success = false;

  SLOG(WiFi, 2) << "TDLS command received: " << operation
                << " for peer " << peer;

  string peer_mac_address;
  if (!ResolvePeerMacAddress(peer, &peer_mac_address, error)) {
    return "";
  }

  if (operation == kTDLSDiscoverOperation) {
    success = TDLSDiscover(peer_mac_address);
  } else if (operation == kTDLSSetupOperation) {
    success = TDLSSetup(peer_mac_address);
  } else if (operation == kTDLSStatusOperation) {
    string supplicant_status = TDLSStatus(peer_mac_address);
    SLOG(WiFi, 2) << "TDLS status returned: " << supplicant_status;
    if (!supplicant_status.empty()) {
      if (supplicant_status == WPASupplicant::kTDLSStateConnected) {
        return kTDLSConnectedState;
      } else if (supplicant_status == WPASupplicant::kTDLSStateDisabled) {
        return kTDLSDisabledState;
      } else if (supplicant_status ==
                 WPASupplicant::kTDLSStatePeerDoesNotExist) {
        return kTDLSNonexistentState;
      } else if (supplicant_status ==
                 WPASupplicant::kTDLSStatePeerNotConnected) {
        return kTDLSDisconnectedState;
      } else {
        return kTDLSUnknownState;
      }
    }
  } else if (operation == kTDLSTeardownOperation) {
    success = TDLSTeardown(peer_mac_address);
  } else {
    error->Populate(Error::kInvalidArguments, "Unknown operation");
    return "";
  }

  if (!success) {
    Error::PopulateAndLog(error, Error::kOperationFailed,
                          "TDLS operation failed");
  }

  return "";
}

// Traffic monitor is enabled for wifi.
bool WiFi::IsTrafficMonitorEnabled() const {
  return true;
}

bool WiFi::ResolvePeerMacAddress(const string &input, string *output,
                                 Error *error) {
  if (!WiFiEndpoint::MakeHardwareAddressFromString(input).empty()) {
    // Input is already a MAC address.
    *output = input;
    return true;
  }

  IPAddress ip_address(IPAddress::kFamilyIPv4);
  if (!ip_address.SetAddressFromString(input)) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Peer is neither an IP Address nor a MAC address");
    return false;
  }

  // Peer address was specified as an IP address which we need to resolve.
  const DeviceInfo *device_info = manager()->device_info();
  if (!device_info->HasDirectConnectivityTo(interface_index(), ip_address)) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "IP address is not local to this interface");
    return false;
  }

  ByteString mac_address;
  if (device_info->GetMACAddressOfPeer(
          interface_index(), ip_address, &mac_address)) {
    *output = WiFiEndpoint::MakeStringFromHardwareAddress(
        vector<uint8_t>(mac_address.GetConstData(),
                        mac_address.GetConstData() +
                        mac_address.GetLength()));
    SLOG(WiFi, 2) << "ARP cache lookup returned peer: " << *output;
    return true;
  }

  if (!Icmp().TransmitEchoRequest(ip_address)) {
    Error::PopulateAndLog(error, Error::kOperationFailed,
                          "Failed to send ICMP reqeust to peer to setup ARP");
  } else {
    // ARP request was transmitted successfully, but overall the attempt
    // to perform a TDLS operation has failed.
    error->Populate(Error::kInProgress,
                    "Peer MAC address was not found in the ARP cache, "
                    "but an ARP request was sent to find it.  "
                    "Please try again.");
  }
  return false;
}

}  // namespace shill
