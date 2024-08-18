// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/secagent.h"

#include <sysexits.h>
#include <unistd.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "attestation-client/attestation/dbus-proxies.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "brillo/files/file_util.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/device_user.h"
#include "secagentd/message_sender.h"
#include "secagentd/metrics_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/policies_features_broker.h"
#include "secagentd/process_cache.h"

namespace secagentd {

SecAgent::SecAgent(
    base::OnceCallback<void(int)> quit_daemon_cb,
    scoped_refptr<MessageSenderInterface> message_sender,
    scoped_refptr<ProcessCacheInterface> process_cache,
    scoped_refptr<DeviceUserInterface> device_user,
    std::unique_ptr<PluginFactoryInterface> plugin_factory,
    std::unique_ptr<org::chromium::AttestationProxyInterface> attestation_proxy,
    std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_proxy,
    feature::PlatformFeaturesInterface* platform_features,
    bool bypass_policy_for_testing,
    bool bypass_enq_ok_wait_for_testing,
    bool stop_reporting_for_unaffiliated_users,
    uint32_t heartbeat_period_s,
    uint32_t plugin_batch_interval_s,
    uint32_t feature_poll_interval_s_for_testing,
    const base::FilePath& root_path)
    : message_sender_(message_sender),
      process_cache_(process_cache),
      device_user_(device_user),
      plugin_factory_(std::move(plugin_factory)),
      attestation_proxy_(std::move(attestation_proxy)),
      tpm_proxy_(std::move(tpm_proxy)),
      platform_features_(platform_features),
      bypass_policy_for_testing_(bypass_policy_for_testing),
      bypass_enq_ok_wait_for_testing_(bypass_enq_ok_wait_for_testing),
      stop_reporting_for_unaffiliated_users_(
          stop_reporting_for_unaffiliated_users),
      heartbeat_period_s_(heartbeat_period_s),
      plugin_batch_interval_s_(plugin_batch_interval_s),
      feature_poll_interval_s_(feature_poll_interval_s_for_testing),
      quit_daemon_cb_(std::move(quit_daemon_cb)),
      root_path_(root_path),
      weak_ptr_factory_(this) {
  policies_features_broker_ = base::MakeRefCounted<PoliciesFeaturesBroker>(
      std::make_unique<policy::PolicyProvider>(), platform_features_,
      base::BindRepeating(&SecAgent::CheckPolicyAndFeature,
                          weak_ptr_factory_.GetWeakPtr()));
}

void SecAgent::Activate() {
  LOG(INFO) << absl::StrFormat(
      "BypassPolicyForTesting:%d,"
      "BypassEnqOkWaitForTesting:%d,StopReportingForUnaffiliatedUsers:%d,"
      "HeartbeatPeriodSeconds:%d,"
      "PluginBatchIntervalSeconds:%d,FeaturePollIntervalSeconds:%d",
      bypass_policy_for_testing_, bypass_enq_ok_wait_for_testing_,
      stop_reporting_for_unaffiliated_users_, heartbeat_period_s_,
      plugin_batch_interval_s_, feature_poll_interval_s_);
  absl::Status result = message_sender_->Initialize();
  if (result != absl::OkStatus()) {
    LOG(ERROR) << result.message();
    std::move(quit_daemon_cb_).Run(EX_SOFTWARE);
    return;
  }

  if (stop_reporting_for_unaffiliated_users_) {
    device_user_->SetFlushCallback(base::BindRepeating(
        &SecAgent::FlushAllPluginEvents, weak_ptr_factory_.GetWeakPtr()));
    // The session change listener will indirectly call CheckPolicyAndFeature
    // to start polling.
    device_user_->RegisterSessionChangeListener(base::BindRepeating(
        &SecAgent::OnSessionStateChange, weak_ptr_factory_.GetWeakPtr()));
    device_user_->RegisterSessionChangeHandler();
  } else {
    policies_features_broker_->StartAndBlockForSync(
        base::Seconds(feature_poll_interval_s_));
  }
  // Some BPF maps are pinned for easy sharing between BPF programs.
  std::string_view relative_bpf_pin_path(kDefaultBpfPinDir);
  if (relative_bpf_pin_path.starts_with("/")) {
    relative_bpf_pin_path.remove_prefix(1);
  }

  base::FilePath absolute_bpf_pin_dir =
      root_path_.Append(relative_bpf_pin_path);
  const std::vector<base::FilePath> pinned_map_names{
      base::FilePath("shared_process_info")};
  for (const auto& map_path : pinned_map_names) {
    base::FilePath pinned_map = absolute_bpf_pin_dir.Append(map_path);
    if (base::PathExists(pinned_map)) {
      LOG(INFO) << "Cleaning up " << absolute_bpf_pin_dir.Append(map_path);
      brillo::DeleteFile(absolute_bpf_pin_dir.Append(map_path));
    }
  }
  process_cache_->InitializeFilter();
}

void SecAgent::CheckPolicyAndFeature() {
  static bool first_visit = true;
  bool xdr_reporting_policy =
      policies_features_broker_->GetDeviceReportXDREventsPolicy() ||
      bypass_policy_for_testing_;
  bool xdr_reporting_feature = policies_features_broker_->GetFeature(
      PoliciesFeaturesBroker::Feature::kCrOSLateBootSecagentdXDRReporting);

  if (first_visit) {
    LOG(INFO) << "DeviceReportXDREventsPolicy: " << xdr_reporting_policy
              << (bypass_policy_for_testing_ ? " (set by flag)" : "");
    LOG(INFO) << "CrOSLateBootSecagentdXDRReporting: " << xdr_reporting_feature;
  }

  stop_reporting_for_unaffiliated_users_ =
      policies_features_broker_->GetFeature(
          PoliciesFeaturesBroker::Feature::
              kCrosLateBootSecagentdXDRStopReportingForUnaffiliated);
  if (stop_reporting_for_unaffiliated_users_ &&
      device_user_->GetIsUnaffiliated() && !reporting_events_) {
    if (first_visit) {
      LOG(INFO) << "Not starting reporting because unaffiliated user signed in";
    }
    return;
  }

  // If either policy is false do not report.
  if (reporting_events_ && !(xdr_reporting_feature && xdr_reporting_policy)) {
    LOG(INFO) << "Stopping event reporting and quitting. Policy: "
              << std::to_string(xdr_reporting_policy)
              << " Feature: " << std::to_string(xdr_reporting_feature);
    reporting_events_ = false;
    // Will exit and restart secagentd.
    std::move(quit_daemon_cb_).Run(EX_OK);
    return;
  } else if (!reporting_events_ &&
             (xdr_reporting_feature && xdr_reporting_policy)) {
    LOG(INFO) << "Starting event reporting";
    // This is emitted at most once per daemon lifetime.
    MetricsSender::GetInstance().SendEnumMetricToUMA(metrics::kPolicy,
                                                     metrics::Policy::kEnabled);
    reporting_events_ = true;
    StartXDRReporting();
  } else if (reporting_events_ && xdr_reporting_feature &&
             xdr_reporting_policy) {
    // BPF plugins were activated in the past. Repoll features and
    // activate/deactivate relevant plugins.
    ActivateOrDeactivatePlugins();
  } else if (first_visit) {
    LOG(INFO) << "Not reporting yet.";
  }

  // Else do nothing until the next poll.
  first_visit = false;
}

void SecAgent::StartXDRReporting() {
  if (!stop_reporting_for_unaffiliated_users_) {
    device_user_->RegisterSessionChangeHandler();
  }
  MetricsSender::GetInstance().InitBatchedMetrics();

  using CbType = base::OnceCallback<void()>;
  CbType cb_for_agent = base::BindOnce(&SecAgent::CreateAndActivatePlugins,
                                       weak_ptr_factory_.GetWeakPtr());
  CbType cb_for_now = base::DoNothing();
  if (bypass_enq_ok_wait_for_testing_) {
    std::swap(cb_for_agent, cb_for_now);
  }
  agent_plugin_ = plugin_factory_->CreateAgentPlugin(
      message_sender_, device_user_, std::move(attestation_proxy_),
      std::move(tpm_proxy_), std::move(cb_for_agent), heartbeat_period_s_);
  if (!agent_plugin_) {
    std::move(quit_daemon_cb_).Run(EX_SOFTWARE);
    return;
  }

  absl::Status result = agent_plugin_->Activate();
  if (!result.ok()) {
    LOG(ERROR) << result.message();
    std::move(quit_daemon_cb_).Run(EX_SOFTWARE);
    return;
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, std::move(cb_for_now));
}

void SecAgent::ActivateOrDeactivatePlugins() {
  absl::Status result;
  std::string action;
  auto activate = [&action](PluginConfig& pc) {
    if (!pc.plugin->IsActive()) {
      action = "activated";
      return pc.plugin->Activate();
    }
    return absl::OkStatus();
  };
  auto deactivate = [&action](PluginConfig& pc) {
    if (pc.plugin->IsActive()) {
      action = "deactivated";
      return pc.plugin->Deactivate();
    }
    return absl::OkStatus();
  };

  for (auto& plugin_config : plugins_) {
    auto& feature = plugin_config.gated_by_feature;
    auto& plugin = plugin_config.plugin;
    action = "";
    if (feature.has_value()) {
      // The plugin is gated by a feature.
      if (policies_features_broker_->GetFeature(feature.value())) {
        result = activate(plugin_config);
      } else {
        result = deactivate(plugin_config);
      }
    } else {
      result = activate(plugin_config);
    }
    if (!result.ok()) {
      LOG(ERROR) << result.message();
    } else if (!action.empty()) {
      LOG(INFO) << plugin->GetName() << " plugin " << action;
    }
  }
}

void SecAgent::CreateAndActivatePlugins() {
  using Feature = PoliciesFeaturesBrokerInterface::Feature;
  using Plugin = Types::Plugin;
  static const std::vector<std::pair<Plugin, std::optional<Feature>>> plugins =
      {std::make_pair(
           Plugin::kAuthenticate,
           std::optional(Feature::kCrOSLateBootSecagentdXDRAuthenticateEvents)),
       std::make_pair(
           Plugin::kNetwork,
           std::optional(Feature::kCrOSLateBootSecagentdXDRNetworkEvents)),
       std::make_pair(Plugin::kProcess, std::nullopt),
       std::make_pair(
           Plugin::kFile,
           std::optional(Feature::kCrOSLateBootSecagentdXDRFileEvents))};
  std::unique_ptr<PluginInterface> plugin;
  for (const auto& p : plugins) {
    plugin = plugin_factory_->Create(p.first, message_sender_, process_cache_,
                                     policies_features_broker_, device_user_,
                                     plugin_batch_interval_s_);
    if (!plugin) {
      std::move(quit_daemon_cb_).Run(EX_SOFTWARE);
    }
    plugins_.push_back({p.second, std::move(plugin)});
  }
  ActivateOrDeactivatePlugins();
}

void SecAgent::OnSessionStateChange(const std::string& state) {
  static bool started_polling = false;

  // Make sure device user is updated before starting reporting.
  if (!started_polling) {
    policies_features_broker_->StartAndBlockForSync(
        base::Seconds(feature_poll_interval_s_));
    started_polling = true;
  }

  if (stop_reporting_for_unaffiliated_users_) {
    device_user_->GetDeviceUserAsync(base::BindOnce(
        &SecAgent::OnDeviceUserRetrieved, weak_ptr_factory_.GetWeakPtr()));
  }
}

void SecAgent::OnDeviceUserRetrieved(const std::string& user,
                                     const std::string& device_userhash) {
  if (reporting_events_) {
    if (device_user_->GetIsUnaffiliated()) {
      LOG(INFO) << "Stopping reporting: Unaffiliated user signed in";
      std::move(quit_daemon_cb_).Run(EX_OK);
    }
  } else {
    CheckPolicyAndFeature();
  }
}

void SecAgent::FlushAllPluginEvents() {
  for (auto& plugin : plugins_) {
    plugin.plugin->Flush();
  }
}

}  // namespace secagentd
