// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcp_controller.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <optional>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <metrics/timer.h>

#include "shill/control_interface.h"
#include "shill/event_dispatcher.h"
#include "shill/ipconfig.h"
#include "shill/logging.h"
#include "shill/metrics.h"
#include "shill/net/process_manager.h"
#include "shill/network/dhcp_provider.h"
#include "shill/network/dhcp_proxy_interface.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/network/network_config.h"
#include "shill/technology.h"
#include "shill/time.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDHCP;
static std::string ObjectID(const DHCPController* d) {
  if (d == nullptr)
    return "(dhcp_controller)";
  else
    return d->device_name();
}
}  // namespace Logging

namespace {

constexpr base::TimeDelta kAcquisitionTimeout = base::Seconds(30);
constexpr char kDHCPCDPath[] = "/sbin/dhcpcd";
constexpr char kDHCPCDUser[] = "dhcp";
constexpr char kDHCPCDGroup[] = "dhcp";
constexpr char kDHCPCDPathFormatPID[] = "var/run/dhcpcd/dhcpcd-%s-4.pid";

}  // namespace

DHCPController::DHCPController(ControlInterface* control_interface,
                               EventDispatcher* dispatcher,
                               DHCPProvider* provider,
                               const std::string& device_name,
                               const Options& opts,
                               Technology technology,
                               Metrics* metrics)
    : control_interface_(control_interface),
      provider_(provider),
      device_name_(device_name),
      technology_(technology),
      pid_(0),
      is_lease_active_(false),
      is_gateway_arp_active_(false),
      options_(opts),
      lease_acquisition_timeout_(kAcquisitionTimeout),
      root_("/"),
      weak_ptr_factory_(this),
      dispatcher_(dispatcher),
      process_manager_(ProcessManager::GetInstance()),
      metrics_(metrics),
      time_(Time::GetInstance()) {
  SLOG(this, 2) << __func__ << ": " << device_name;
  if (options_.lease_name.empty()) {
    options_.lease_name = device_name;
  }
}

DHCPController::~DHCPController() {
  SLOG(this, 2) << __func__ << ": " << device_name();

  // Don't leave behind dhcpcd running.
  Stop(__func__);
}

void DHCPController::RegisterCallbacks(UpdateCallback update_callback,
                                       DropCallback drop_callback) {
  update_callback_ = update_callback;
  drop_callback_ = drop_callback;
}

bool DHCPController::RequestIP() {
  SLOG(this, 2) << __func__ << ": " << device_name();
  if (!pid_) {
    return Start();
  }
  if (!proxy_) {
    LOG(ERROR) << "Unable to request IP before acquiring destination.";
    return Restart();
  }
  return RenewIP();
}

bool DHCPController::RenewIP() {
  SLOG(this, 2) << __func__ << ": " << device_name();
  if (!pid_) {
    return Start();
  }
  if (!proxy_) {
    LOG(ERROR) << "Unable to renew IP before acquiring destination.";
    return false;
  }
  StopExpirationTimeout();
  proxy_->Rebind(device_name());
  StartAcquisitionTimeout();
  return true;
}

bool DHCPController::ReleaseIP(ReleaseReason reason) {
  SLOG(this, 2) << __func__ << ": " << device_name();
  if (!pid_) {
    return true;
  }

  // If we are using static IP and haven't retrieved a lease yet, we should
  // allow the DHCP process to continue until we have a lease.
  if (!is_lease_active_ && reason == ReleaseReason::kStaticIP) {
    return true;
  }

  // If we are using gateway unicast ARP to speed up re-connect, don't
  // give up our leases when we disconnect.
  bool should_keep_lease =
      reason == ReleaseReason::kDisconnect && ShouldKeepLeaseOnDisconnect();

  if (!should_keep_lease && proxy_.get()) {
    proxy_->Release(device_name());
  }
  Stop(__func__);
  return true;
}

void DHCPController::InitProxy(const std::string& service) {
  if (!proxy_) {
    LOG(INFO) << "Init DHCP Proxy: " << device_name() << " at " << service;
    proxy_ = control_interface_->CreateDHCPProxy(service);
  }
}

void DHCPController::ProcessEventSignal(ClientEventReason reason,
                                        const KeyValueStore& configuration) {
  if (reason == ClientEventReason::kFail) {
    LOG(ERROR) << "Received failure event from DHCP client.";
    NotifyFailure();
    return;
  } else if (reason == ClientEventReason::kNak) {
    // If we got a NAK, this means the DHCP server is active, and any
    // Gateway ARP state we have is no longer sufficient.
    LOG_IF(ERROR, is_gateway_arp_active_)
        << "Received NAK event for our gateway-ARP lease.";
    is_gateway_arp_active_ = false;
    return;
  } else if (reason != ClientEventReason::kBound &&
             reason != ClientEventReason::kRebind &&
             reason != ClientEventReason::kReboot &&
             reason != ClientEventReason::kRenew &&
             reason != ClientEventReason::kGatewayArp) {
    LOG(WARNING) << "Event ignored.";
    return;
  }

  NetworkConfig network_config;
  DHCPv4Config::Data dhcp_data;
  if (!DHCPv4Config::ParseConfiguration(configuration, &network_config,
                                        &dhcp_data)) {
    LOG(WARNING) << device_name_
                 << ": Error parsing network configuration from DHCP client. "
                 << "The following configuration might be partial: "
                 << network_config;
  }

  // This needs to be set before calling OnIPConfigUpdated() below since
  // those functions may indirectly call other methods like ReleaseIP that
  // depend on or change this value.
  set_is_lease_active(true);

  // Only record the duration once. Note that Stop() has no effect if the timer
  // has already stopped.
  if (last_provision_timer_) {
    last_provision_timer_->Stop();
  }

  const bool is_gateway_arp = reason == ClientEventReason::kGatewayArp;

  // b/298696921#17: a race between GATEWAY-ARP response and DHCPACK can cause a
  // GATEWAY-ARP event incoming with no DHCP lease information. This empty lease
  // should be ignored.
  if (is_gateway_arp && !network_config.ipv4_address) {
    LOG(WARNING) << "Get GATEWAY-ARP reply after DHCP state change, ignored.";
    return;
  }

  // This is a non-authoritative confirmation that we or on the same
  // network as the one we received a lease on previously.  The DHCP
  // client is still running, so we should not cancel the timeout
  // until that completes.  In the meantime, however, we can tentatively
  // configure our network in anticipation of successful completion.
  OnIPConfigUpdated(network_config, dhcp_data,
                    /*new_lease_acquired=*/!is_gateway_arp);
  is_gateway_arp_active_ = is_gateway_arp;
}

void DHCPController::ProcessStatusChangedSignal(ClientStatus status) {
  if (status == ClientStatus::kIPv6Preferred) {
    StopAcquisitionTimeout();
    StopExpirationTimeout();
    dispatcher_->PostTask(FROM_HERE,
                          base::BindOnce(&DHCPController::InvokeDropCallback,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         /*is_voluntary=*/true));
  }
}

std::optional<base::TimeDelta> DHCPController::TimeToLeaseExpiry() {
  if (!current_lease_expiration_time_.has_value()) {
    SLOG(this, 2) << __func__ << ": No current DHCP lease";
    return std::nullopt;
  }
  struct timeval now;
  time_->GetTimeBoottime(&now);
  if (now.tv_sec > current_lease_expiration_time_->tv_sec) {
    SLOG(this, 2) << __func__ << ": Current DHCP lease has already expired";
    return std::nullopt;
  }
  return base::Seconds(current_lease_expiration_time_->tv_sec - now.tv_sec);
}

void DHCPController::OnIPConfigUpdated(const NetworkConfig& network_config,
                                       const DHCPv4Config::Data& dhcp_data,
                                       bool new_lease_acquired) {
  if (new_lease_acquired) {
    StopAcquisitionTimeout();
    if (dhcp_data.lease_duration.is_positive()) {
      UpdateLeaseExpirationTime(dhcp_data.lease_duration.InSeconds());
      StartExpirationTimeout(dhcp_data.lease_duration);
    } else {
      LOG(WARNING)
          << "Lease duration is zero; not starting an expiration timer.";
      ResetLeaseExpirationTime();
      StopExpirationTimeout();
    }
  }

  dispatcher_->PostTask(
      FROM_HERE, base::BindOnce(&DHCPController::InvokeUpdateCallback,
                                weak_ptr_factory_.GetWeakPtr(), network_config,
                                dhcp_data, new_lease_acquired));
}

void DHCPController::NotifyFailure() {
  StopAcquisitionTimeout();
  StopExpirationTimeout();

  dispatcher_->PostTask(
      FROM_HERE,
      base::BindOnce(&DHCPController::InvokeDropCallback,
                     weak_ptr_factory_.GetWeakPtr(), /*is_voluntary=*/false));
}

bool DHCPController::IsEphemeralLease() const {
  return options_.lease_name == device_name();
}

bool DHCPController::Start() {
  SLOG(this, 2) << __func__ << ": " << device_name();

  last_provision_timer_ = std::make_unique<chromeos_metrics::Timer>();
  last_provision_timer_->Start();

  // Setup program arguments.
  auto args = GetFlags();
  std::string interface_arg(device_name());
  if (options_.lease_name != device_name()) {
    interface_arg = base::StrCat({device_name(), "=", options_.lease_name});
  }
  args.push_back(interface_arg);

  ProcessManager::MinijailOptions minijail_options;
  minijail_options.user = kDHCPCDUser;
  minijail_options.group = kDHCPCDGroup;
  minijail_options.capmask =
      CAP_TO_MASK(CAP_NET_BIND_SERVICE) | CAP_TO_MASK(CAP_NET_BROADCAST) |
      CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
  minijail_options.inherit_supplementary_groups = false;
  pid_t pid = process_manager_->StartProcessInMinijail(
      FROM_HERE, base::FilePath(kDHCPCDPath), args, {}, minijail_options,
      base::BindOnce(&DHCPController::OnProcessExited,
                     weak_ptr_factory_.GetWeakPtr()));
  if (pid < 0) {
    return false;
  }
  pid_ = pid;
  LOG(INFO) << "Spawned " << kDHCPCDPath << " with pid: " << pid_;
  provider_->BindPID(pid_, weak_ptr_factory_.GetWeakPtr());
  StartAcquisitionTimeout();
  return true;
}

void DHCPController::Stop(const char* reason) {
  LOG_IF(INFO, pid_) << "Stopping " << pid_ << " (" << reason << ")";
  KillClient();
  // KillClient waits for the client to terminate so it's safe to cleanup the
  // state.
  CleanupClientState();
}

void DHCPController::KillClient() {
  if (!pid_) {
    return;
  }

  // Pass the termination responsibility to ProcessManager.
  // ProcessManager will try to terminate the process using SIGTERM, then
  // SIGKill signals.  It will log an error message if it is not able to
  // terminate the process in a timely manner.
  process_manager_->StopProcessAndBlock(pid_);
}

bool DHCPController::Restart() {
  Stop(__func__);
  return Start();
}

void DHCPController::OnProcessExited(int exit_status) {
  CHECK(pid_);
  if (exit_status == EXIT_SUCCESS) {
    SLOG(2) << "pid " << pid_ << " exit status " << exit_status;
  } else {
    LOG(WARNING) << "pid " << pid_ << " exit status " << exit_status;
  }
  CleanupClientState();
}

void DHCPController::CleanupClientState() {
  SLOG(this, 2) << __func__ << ": " << device_name();
  StopAcquisitionTimeout();
  StopExpirationTimeout();

  proxy_.reset();
  if (pid_) {
    int pid = pid_;
    pid_ = 0;
    provider_->UnbindPID(pid);
  }
  is_lease_active_ = false;

  // Delete lease file if it is ephemeral.
  if (IsEphemeralLease()) {
    base::DeleteFile(root().Append(base::StringPrintf(
        DHCPProvider::kDHCPCDPathFormatLease, device_name().c_str())));
  }
  base::DeleteFile(root().Append(
      base::StringPrintf(kDHCPCDPathFormatPID, device_name().c_str())));
  is_gateway_arp_active_ = false;
}

bool DHCPController::ShouldFailOnAcquisitionTimeout() const {
  // Continue to use previous lease if gateway ARP is active.
  return !is_gateway_arp_active_;
}

// Return true if we should keep the lease on disconnect.
bool DHCPController::ShouldKeepLeaseOnDisconnect() const {
  // If we are using gateway unicast ARP to speed up re-connect, don't
  // give up our leases when we disconnect.
  return options_.use_arp_gateway;
}

std::vector<std::string> DHCPController::GetFlags() {
  std::vector<std::string> flags = {
      "-B",                        // Run in foreground.
      "-i", "chromeos",            // Static value for Vendor class info.
      "-q",                        // Only warnings+errors to stderr.
      "-4",                        // IPv4 only.
      "-o", "captive_portal_uri",  // Request the captive portal URI.
  };

  // Request hostname from server.
  if (!options_.hostname.empty()) {
    flags.insert(flags.end(), {"-h", options_.hostname});
  }

  if (options_.use_arp_gateway) {
    flags.insert(flags.end(), {
                                  "-R",         // ARP for default gateway.
                                  "--unicast",  // Enable unicast ARP on renew.
                              });
  }

  if (options_.use_rfc_8925) {
    // Request option 108 to prefer IPv6-only. If server also supports this, no
    // dhcp lease will be assigned and dhcpcd will notify shill with an
    // IPv6OnlyPreferred StatusChanged event.
    flags.insert(flags.end(), {"-o", "ipv6_only_preferred"});
  }

  // TODO(jiejiang): This will also include the WiFi Direct GC mode now. We may
  // want to check if we should enable it in the future.
  if (options_.apply_dscp && technology_ == Technology::kWiFi) {
    // This flag is added by https://crrev.com/c/4861699.
    flags.push_back("--apply_dscp");
  }

  return flags;
}

void DHCPController::StartAcquisitionTimeout() {
  CHECK(lease_expiration_callback_.IsCancelled());
  lease_acquisition_timeout_callback_.Reset(
      base::BindOnce(&DHCPController::ProcessAcquisitionTimeout,
                     weak_ptr_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(FROM_HERE,
                               lease_acquisition_timeout_callback_.callback(),
                               lease_acquisition_timeout_);
}

void DHCPController::StopAcquisitionTimeout() {
  lease_acquisition_timeout_callback_.Cancel();
}

void DHCPController::ProcessAcquisitionTimeout() {
  LOG(ERROR) << "Timed out waiting for DHCP lease on " << device_name() << " "
             << "(after " << lease_acquisition_timeout_.InSeconds()
             << " seconds).";
  if (!ShouldFailOnAcquisitionTimeout()) {
    LOG(INFO) << "Continuing to use our previous lease, due to gateway-ARP.";
  } else {
    NotifyFailure();
  }
}

void DHCPController::StartExpirationTimeout(base::TimeDelta lease_duration) {
  CHECK(lease_acquisition_timeout_callback_.IsCancelled());
  SLOG(this, 2) << __func__ << ": " << device_name() << ": "
                << "Lease timeout is " << lease_duration.InSeconds()
                << " seconds.";
  lease_expiration_callback_.Reset(
      BindOnce(&DHCPController::ProcessExpirationTimeout,
               weak_ptr_factory_.GetWeakPtr(), lease_duration));
  dispatcher_->PostDelayedTask(FROM_HERE, lease_expiration_callback_.callback(),
                               lease_duration);
}

void DHCPController::StopExpirationTimeout() {
  lease_expiration_callback_.Cancel();
}

void DHCPController::ProcessExpirationTimeout(base::TimeDelta lease_duration) {
  LOG(ERROR) << "DHCP lease expired on " << device_name()
             << "; restarting DHCP client instance.";

  metrics_->SendToUMA(Metrics::kMetricExpiredLeaseLengthSeconds, technology_,
                      lease_duration.InSeconds());

  if (!Restart()) {
    NotifyFailure();
  }
}

void DHCPController::UpdateLeaseExpirationTime(uint32_t new_lease_duration) {
  struct timeval new_expiration_time;
  time_->GetTimeBoottime(&new_expiration_time);
  new_expiration_time.tv_sec += new_lease_duration;
  current_lease_expiration_time_ = new_expiration_time;
}

void DHCPController::ResetLeaseExpirationTime() {
  current_lease_expiration_time_ = std::nullopt;
}

void DHCPController::InvokeUpdateCallback(const NetworkConfig& network_config,
                                          const DHCPv4Config::Data& dhcp_data,
                                          bool new_lease_acquired) {
  if (!update_callback_.is_null()) {
    update_callback_.Run(network_config, dhcp_data, new_lease_acquired);
  }
}

void DHCPController::InvokeDropCallback(bool is_voluntary) {
  if (!drop_callback_.is_null()) {
    drop_callback_.Run(is_voluntary);
  }
}

std::optional<base::TimeDelta>
DHCPController::GetAndResetLastProvisionDuration() {
  if (!last_provision_timer_) {
    return std::nullopt;
  }

  if (last_provision_timer_->HasStarted()) {
    // The timer is still running, which means we haven't got any address.
    return std::nullopt;
  }

  base::TimeDelta ret;
  if (!last_provision_timer_->GetElapsedTime(&ret)) {
    // The timer has not been started. This shouldn't happen since Start() is
    // called right after the timer is created.
    return std::nullopt;
  }

  last_provision_timer_.reset();
  return ret;
}

}  // namespace shill
