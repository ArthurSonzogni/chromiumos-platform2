// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/l2tp_connection.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <libpasswordprovider/password_provider.h>
#include <net-base/network_config.h>
#include <net-base/process_manager.h>

#include "shill/ppp_daemon.h"
#include "shill/vpn/vpn_util.h"

namespace shill {

namespace {

constexpr char kRunDir[] = "/run/xl2tpd";
constexpr char kXl2tpdPath[] = "/usr/sbin/xl2tpd";
constexpr char kXl2tpdControlPath[] = "/usr/sbin/xl2tpd-control";
constexpr char kL2TPDConfigFileName[] = "l2tpd.conf";
constexpr char kL2TPDControlFileName[] = "l2tpd.control";
constexpr char kPPPDConfigFileName[] = "pppd.conf";
constexpr char kPPPDLogFileName[] = "pppd.log";

// Environment variable available to ppp plugin to know the resolved address
// of the L2TP server.
constexpr char kLnsAddress[] = "LNS_ADDRESS";

// Constants used in the config file for xl2tpd.
constexpr char kL2TPConnectionName[] = "managed";
constexpr char kBpsParameter[] = "1000000";
constexpr char kRedialTimeoutParameter[] = "2";
constexpr char kMaxRedialsParameter[] = "30";

// xl2tpd (1.3.12 at the time of writing) uses fgets with a size 1024 buffer to
// get configuration lines. If a configuration line was longer than that and
// didn't contain the comment delimiter ';', it could be used to populate
// multiple configuration options.
constexpr size_t kXl2tpdMaxConfigurationLength = 1023;

// Reads the pppd log at |log_path|, and returns true if the log indicates that
// there is an authentication failure.
bool IsAuthErrorFromPPPDLog(const base::FilePath& log_path) {
  // The max size that this function reads from the log, the connect failure
  // should happen at the very early stage so it shouldn't be long.
  constexpr int kMaxLogSize = 4096;
  constexpr char kAuthFailureLine[] = "authentication failed";

  std::string log;
  if (!base::ReadFileToStringWithMaxSize(log_path, &log, kMaxLogSize)) {
    if (log.size() == kMaxLogSize) {
      LOG(INFO) << "Skip parsing pppd log since the log size is too long";
      return false;
    }
    PLOG(ERROR) << "Failed to read pppd log at " << log_path;
    return false;
  }

  // Split the lines and do the match with `ends_with()` to be more efficient
  // (compared with `std::string::find()`). The correctness will be verified by
  // the network.VPNIncorrectCreds test. See b/329328608.
  std::vector<std::string_view> lines = base::SplitStringPiece(
      log, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto line : lines) {
    if (line.ends_with(kAuthFailureLine)) {
      return true;
    }
  }
  return false;
}

}  // namespace

L2TPConnection::L2TPConnection(std::unique_ptr<Config> config,
                               std::unique_ptr<Callbacks> callbacks,
                               ControlInterface* control_interface,
                               DeviceInfo* device_info,
                               EventDispatcher* dispatcher,
                               net_base::ProcessManager* process_manager)
    : VPNConnection(std::move(callbacks), dispatcher),
      config_(std::move(config)),
      control_interface_(control_interface),
      device_info_(device_info),
      password_provider_(
          std::make_unique<password_provider::PasswordProvider>()),
      process_manager_(process_manager),
      vpn_util_(VPNUtil::New()) {}

// |external_task_| will be killed in its dtor if it is still running so no
// explicit cleanup is needed here.
L2TPConnection::~L2TPConnection() {}

void L2TPConnection::OnConnect() {
  temp_dir_ = vpn_util_->CreateScopedTempDir(base::FilePath(kRunDir));

  if (!WritePPPDConfig()) {
    NotifyFailure(Service::kFailureInternal,
                  "Failed to write pppd config file");
    return;
  }

  if (!WriteL2TPDConfig()) {
    NotifyFailure(Service::kFailureInternal,
                  "Failed to write xl2tpd config file");
    return;
  }

  StartXl2tpd();
}

void L2TPConnection::GetLogin(std::string* user, std::string* password) {
  LOG(INFO) << "Login requested.";
  if (config_->user.empty()) {
    LOG(ERROR) << "User not set.";
    return;
  }

  std::string password_local = config_->password;
  if (config_->use_login_password) {
    std::unique_ptr<password_provider::Password> login_password =
        password_provider_->GetPassword();
    if (login_password == nullptr || login_password->size() == 0) {
      LOG(ERROR) << "Unable to retrieve user password";
      return;
    }
    password_local =
        std::string(login_password->GetRaw(), login_password->size());
  } else if (password_local.empty()) {
    LOG(ERROR) << "Password not set.";
    return;
  }

  *user = config_->user;
  *password = password_local;
}

void L2TPConnection::Notify(const std::string& reason,
                            const std::map<std::string, std::string>& dict) {
  if (reason == kPPPReasonAuthenticating || reason == kPPPReasonAuthenticated) {
    // These are uninteresting intermediate states that do not indicate failure.
    return;
  }

  if (reason == kPPPReasonDisconnect) {
    // Ignored. Failure is handled when pppd exits since the exit status
    // contains more information.
    LOG(INFO) << "pppd disconnected";
    return;
  }

  if (reason == kPPPReasonExit) {
    if (!IsConnectingOrConnected()) {
      // We have notified the upper layer, or the disconnect is triggered by the
      // upper layer. In both cases, we don't need call NotifyFailure().
      LOG(INFO) << "pppd notifies us of " << reason << ", the current state is "
                << state();
      return;
    }

    Service::ConnectFailure failure = PPPDaemon::ParseExitFailure(dict);

    // The exit code may be unknown even if the failure is actually auth error,
    // so let's parse the logs in this case. See b/329328608.
    if (failure == Service::kFailureUnknown &&
        IsAuthErrorFromPPPDLog(pppd_log_path_)) {
      LOG(INFO) << "Found pattern of auth failure in pppd log";
      failure = Service::kFailurePPPAuth;
    }

    NotifyFailure(failure, "pppd disconnected");
    return;
  }

  // The message is kPPPReasonConnect. Checks if we are in the connecting state
  // at first.
  if (state() != State::kConnecting) {
    LOG(WARNING) << "pppd notifies us of " << reason
                 << ", the current state is " << state();
    return;
  }

  std::string interface_name = PPPDaemon::GetInterfaceName(dict);
  auto network_config = std::make_unique<net_base::NetworkConfig>(
      PPPDaemon::ParseNetworkConfig(dict));

  // There is no IPv6 support for L2TP/IPsec VPN at this moment, so create a
  // blackhole route for IPv6 traffic after establishing a IPv4 VPN.
  network_config->ipv6_blackhole_route = true;

  // Reduce MTU to the minimum viable for IPv6, since the IPsec layer consumes
  // some variable portion of the payload.  Although this system does not yet
  // support IPv6, it is a reasonable value to start with, since the minimum
  // IPv6 packet size will plausibly be a size any gateway would support, and
  // is also larger than the IPv4 minimum size.
  network_config->mtu = net_base::NetworkConfig::kMinIPv6MTU;

  // Notify() could be invoked either before or after the creation of the ppp
  // interface. We need to make sure that the interface is ready (by checking
  // DeviceInfo) before invoking the connected callback here.
  int interface_index = device_info_->GetIndex(interface_name);
  if (interface_index != -1) {
    NotifyConnected(interface_name, interface_index, std::move(network_config));
  } else {
    device_info_->AddVirtualInterfaceReadyCallback(
        interface_name,
        base::BindOnce(&L2TPConnection::OnLinkReady, weak_factory_.GetWeakPtr(),
                       std::move(network_config)));
  }
}

void L2TPConnection::OnDisconnect() {
  // Do the cleanup directly if xl2tpd is not running.
  if (!external_task_) {
    OnXl2tpdControlDisconnectDone(/*exit_code=*/0);
    return;
  }

  const std::vector<std::string> args = {"-c", l2tpd_control_path_.value(),
                                         "disconnect", kL2TPConnectionName};
  int pid = process_manager_->StartProcessInMinijail(
      FROM_HERE, base::FilePath(kXl2tpdControlPath), args, {},
      VPNUtil::BuildMinijailOptions(0),
      base::BindOnce(&L2TPConnection::OnXl2tpdControlDisconnectDone,
                     weak_factory_.GetWeakPtr()));
  if (pid != -1) {
    return;
  }
  LOG(ERROR) << "Failed to start xl2tpd-control";
  OnXl2tpdControlDisconnectDone(/*exit_code=*/0);
}

bool L2TPConnection::WritePPPDConfig() {
  pppd_config_path_ = temp_dir_.GetPath().Append(kPPPDConfigFileName);
  pppd_log_path_ = temp_dir_.GetPath().Append(kPPPDLogFileName);

  // Note that since string_view is used here, all the strings in this vector
  // MUST be alive until the end of this function. Unit tests are supposed to
  // catch the issue if this condition is not met.
  // TODO(b/200636771): Use proper mtu and mru.
  std::vector<std::string_view> lines = {
      "ipcp-accept-local",
      "ipcp-accept-remote",
      "refuse-eap",
      "noccp",
      "noauth",
      "crtscts",
      "mtu 1410",
      "mru 1410",
      "lock",
      "connect-delay 5000",
      "nodefaultroute",
      "nosystemconfig",
      "usepeerdns",
  };
  if (config_->lcp_echo) {
    lines.push_back("lcp-echo-failure 4");
    lines.push_back("lcp-echo-interval 30");
  }

  // pppd logs to stdout by default. Change it to a file so that we can read it
  // for checking connection failures later. Note that:
  // - pppd will log to this file and syslog at the same time.
  // - Even without doing this, we still need to disable the behavior of logging
  //   to stdout of pppd. See b/218437737 and https://crrev.com/c/3569930.
  const std::string logfile_line =
      base::StrCat({"logfile ", pppd_log_path_.value()});
  lines.push_back(logfile_line);

  const std::string plugin_line =
      base::StrCat({"plugin ", PPPDaemon::kShimPluginPath});
  lines.push_back(plugin_line);

  std::string contents = base::JoinString(lines, "\n");
  return vpn_util_->WriteConfigFile(pppd_config_path_, contents);
}

bool L2TPConnection::WriteL2TPDConfig() {
  CHECK(!pppd_config_path_.empty());

  // b/187984628: When UseLoginPassword is enabled, PAP must be refused to
  // prevent potential password leak to a malicious server.
  if (config_->use_login_password) {
    config_->refuse_pap = true;
  }

  l2tpd_config_path_ = temp_dir_.GetPath().Append(kL2TPDConfigFileName);

  std::vector<std::string> lines;
  lines.push_back(base::StringPrintf("[lac %s]", kL2TPConnectionName));

  // Fills in bool properties.
  auto bool_property = [](std::string_view key, bool value) -> std::string {
    return base::StrCat({key, " = ", value ? "yes" : "no"});
  };
  lines.push_back(bool_property("require chap", config_->require_chap));
  lines.push_back(bool_property("refuse pap", config_->refuse_pap));
  lines.push_back(
      bool_property("require authentication", config_->require_auth));
  lines.push_back(bool_property("length bit", config_->length_bit));
  lines.push_back(bool_property("redial", true));
  lines.push_back(bool_property("autodial", true));

  // Fills in string properties. Note that some values are input by users, we
  // need to check them to ensure that the generated config file will not be
  // polluted. See https://crbug.com/1077754. Note that the ordering of
  // properties in the config file does not matter, we use a vector instead of
  // map just for the ease of unit tests. Using StringPiece is safe here since
  // because there is no temporary string object when constructing this vector.
  const std::vector<std::pair<std::string_view, std::string_view>>
      string_properties = {
          {"lns", config_->remote_ip},
          {"name", config_->user},
          {"bps", kBpsParameter},
          {"redial timeout", kRedialTimeoutParameter},
          {"max redials", kMaxRedialsParameter},
          {"pppoptfile", pppd_config_path_.value()},
      };
  for (const auto& [key, value] : string_properties) {
    if (value.find('\n') != value.npos) {
      LOG(ERROR) << "The value for " << key << " contains newline characters";
      return false;
    }
    const auto line = base::StrCat({key, " = ", value});
    if (line.size() > kXl2tpdMaxConfigurationLength) {
      LOG(ERROR) << "Line length for " << key << " exceeds "
                 << kXl2tpdMaxConfigurationLength;
      return false;
    }
    lines.push_back(line);
  }

  std::string contents = base::JoinString(lines, "\n");
  return vpn_util_->WriteConfigFile(l2tpd_config_path_, contents);
}

void L2TPConnection::StartXl2tpd() {
  l2tpd_control_path_ = temp_dir_.GetPath().Append(kL2TPDControlFileName);

  std::vector<std::string> args = {
      "-c", l2tpd_config_path_.value(), "-C", l2tpd_control_path_.value(),
      "-D",  // prevents xl2tpd from detaching from the terminal and daemonizing
      "-l",  // lets xl2tpd use syslog
  };

  std::map<std::string, std::string> env = {
      {kLnsAddress, config_->remote_ip},
  };

  auto external_task_local = std::make_unique<ExternalTask>(
      control_interface_, process_manager_, weak_factory_.GetWeakPtr(),
      base::BindOnce(&L2TPConnection::OnXl2tpdExitedUnexpectedly,
                     weak_factory_.GetWeakPtr()));

  Error error;
  constexpr uint64_t kCapMask = CAP_TO_MASK(CAP_NET_ADMIN);
  if (!external_task_local->StartInMinijail(
          base::FilePath(kXl2tpdPath), &args, env,
          VPNUtil::BuildMinijailOptions(kCapMask), &error)) {
    NotifyFailure(Service::kFailureInternal,
                  base::StrCat({"Failed to start xl2tpd: ", error.message()}));
    return;
  }

  external_task_ = std::move(external_task_local);
}

void L2TPConnection::OnLinkReady(
    std::unique_ptr<net_base::NetworkConfig> network_config,
    const std::string& if_name,
    int if_index) {
  if (state() != State::kConnecting) {
    // Needs to do nothing here. The ppp interface is managed by the pppd
    // process so we don't need to remove it here.
    LOG(WARNING) << "OnLinkReady() called but the current state is " << state();
    return;
  }
  NotifyConnected(if_name, if_index, std::move(network_config));
}

void L2TPConnection::OnXl2tpdExitedUnexpectedly(pid_t pid, int exit_code) {
  external_task_ = nullptr;
  const std::string message =
      base::StringPrintf("xl2tpd exited unexpectedly with code=%d", exit_code);
  if (!IsConnectingOrConnected()) {
    LOG(WARNING) << message;
    return;
  }
  NotifyFailure(Service::kFailureInternal, message);
}

void L2TPConnection::OnXl2tpdControlDisconnectDone(int exit_code) {
  // Since this is only called in the disconnecting procedure, we only log this
  // uncommon event instead of reporting it to the upper layer.
  if (exit_code != 0) {
    LOG(ERROR) << "xl2tpd-control exited with code=" << exit_code;
  }

  // Kill xl2tpd if it is still running. Note that it is usually the case that
  // xl2tpd has disconnected the connection at this time, but this cannot be
  // guaranteed, but we don't have better signal for that. Some servers might be
  // unhappy when that happens (see b/234162302).
  external_task_ = nullptr;
  if (state() == State::kDisconnecting) {
    NotifyStopped();
  }
}

}  // namespace shill
