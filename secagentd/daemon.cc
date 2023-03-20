// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <cstdlib>
#include <iomanip>
#include <memory>
#include <string>
#include <sysexits.h>
#include <utility>

#include "absl/status/status.h"
#include "attestation/proto_bindings/interface.pb.h"
#include "attestation-client/attestation/dbus-proxies.h"
#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "brillo/daemons/dbus_daemon.h"
#include "missive/client/missive_client.h"
#include "secagentd/daemon.h"
#include "secagentd/message_sender.h"
#include "secagentd/metrics_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/policies_features_broker.h"
#include "secagentd/process_cache.h"

namespace secagentd {

Daemon::Daemon(struct Inject injected) : weak_ptr_factory_(this) {
  plugin_factory_ = std::move(injected.plugin_factory_);
  message_sender_ = std::move(injected.message_sender_);
  process_cache_ = std::move(injected.process_cache_);
  policies_features_broker_ = std::move(injected.policies_features_broker_);
  bus_ = std::move(injected.dbus_);
  MetricsSender::GetInstance().SetMetricsLibraryForTesting(
      std::move(injected.metrics_library_));
}

Daemon::Daemon(bool bypass_policy_for_testing,
               bool bypass_enq_ok_wait_for_testing,
               uint32_t heartbeat_period_s,
               uint32_t plugin_batch_interval_s)
    : bypass_policy_for_testing_(bypass_policy_for_testing),
      bypass_enq_ok_wait_for_testing_(bypass_enq_ok_wait_for_testing),
      heartbeat_period_s_(heartbeat_period_s),
      plugin_batch_interval_s_(plugin_batch_interval_s),
      weak_ptr_factory_(this) {}

int Daemon::OnInit() {
  int rv = brillo::DBusDaemon::OnInit();
  if (rv != EX_OK) {
    return rv;
  }

  if (plugin_factory_ == nullptr) {
    plugin_factory_ = std::make_unique<PluginFactory>();
  }

  if (message_sender_ == nullptr) {
    // Set up ERP.
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams(
        "missive_thread_pool");
    reporting::MissiveClient::Initialize(bus_.get());

    message_sender_ = base::MakeRefCounted<MessageSender>();

    absl::Status result = message_sender_->Initialize();
    if (result != absl::OkStatus()) {
      LOG(ERROR) << result.message();
      return EX_SOFTWARE;
    }
  }

  if (process_cache_ == nullptr) {
    process_cache_ = base::MakeRefCounted<ProcessCache>();
    process_cache_->InitializeFilter();
  }

  if (policies_features_broker_ == nullptr) {
    policies_features_broker_ = base::MakeRefCounted<PoliciesFeaturesBroker>(
        std::make_unique<policy::PolicyProvider>(),
        feature::PlatformFeatures::New(bus_),
        base::BindRepeating(&Daemon::CheckPolicyAndFeature,
                            weak_ptr_factory_.GetWeakPtr()));
  }

  return EX_OK;
}

void Daemon::CheckPolicyAndFeature() {
  static bool first_visit = true;
  bool xdr_reporting_policy =
      policies_features_broker_->GetDeviceReportXDREventsPolicy() ||
      bypass_policy_for_testing_;
  bool xdr_reporting_feature = policies_features_broker_->GetFeature(
      PoliciesFeaturesBroker::Feature::kCrOSLateBootSecagentdXDRReporting);
  // If either policy is false do not report.
  if (reporting_events_ && !(xdr_reporting_feature && xdr_reporting_policy)) {
    LOG(INFO) << "Stopping event reporting and quitting. Policy: "
              << std::to_string(xdr_reporting_policy)
              << " Feature: " << std::to_string(xdr_reporting_feature);
    reporting_events_ = false;
    // Will exit and restart secagentd.
    Quit();
  } else if (!reporting_events_ &&
             (xdr_reporting_feature && xdr_reporting_policy)) {
    LOG(INFO) << "Starting event reporting";
    // This is emitted at most once per daemon lifetime.
    MetricsSender::GetInstance().SendEnumMetricToUMA(metrics::kPolicy,
                                                     metrics::Policy::kEnabled);
    reporting_events_ = true;
    StartXDRReporting();
  } else if (first_visit) {
    LOG(INFO) << "Not reporting yet.";
    LOG(INFO) << "DeviceReportXDREventsPolicy: " << xdr_reporting_policy
              << (bypass_policy_for_testing_ ? " (set by flag)" : "");
    LOG(INFO) << "CrOSLateBootSecagentdXDRReporting: " << xdr_reporting_feature;
  }
  // Else do nothing until the next poll.
  first_visit = false;
}

void Daemon::StartXDRReporting() {
  using CbType = base::OnceCallback<void()>;
  CbType cb_for_agent =
      base::BindOnce(&Daemon::RunPlugins, weak_ptr_factory_.GetWeakPtr());
  CbType cb_for_now = base::DoNothing();
  if (bypass_enq_ok_wait_for_testing_) {
    std::swap(cb_for_agent, cb_for_now);
  }
  agent_plugin_ = plugin_factory_->CreateAgentPlugin(
      message_sender_, std::make_unique<org::chromium::AttestationProxy>(bus_),
      std::make_unique<org::chromium::TpmManagerProxy>(bus_),
      std::move(cb_for_agent), heartbeat_period_s_);
  if (!agent_plugin_) {
    QuitWithExitCode(EX_SOFTWARE);
  }

  absl::Status result = agent_plugin_->Activate();
  if (!result.ok()) {
    LOG(ERROR) << result.message();
    QuitWithExitCode(EX_SOFTWARE);
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, std::move(cb_for_now));
}

void Daemon::RunPlugins() {
  if (CreatePlugin(PluginFactory::PluginType::kProcess) != EX_OK) {
    QuitWithExitCode(EX_SOFTWARE);
  }

  for (auto& plugin : plugins_) {
    absl::Status result = plugin->Activate();
    if (!result.ok()) {
      LOG(ERROR) << result.message();
      QuitWithExitCode(EX_SOFTWARE);
    }
  }
}

int Daemon::CreatePlugin(PluginFactory::PluginType type) {
  std::unique_ptr<PluginInterface> plugin;
  switch (type) {
    case PluginFactory::PluginType::kProcess: {
      plugin = plugin_factory_->Create(
          Types::Plugin::kProcess, message_sender_, process_cache_,
          policies_features_broker_, plugin_batch_interval_s_);
      break;
    }
    default:
      CHECK(false) << "Unsupported plugin type";
  }

  if (!plugin) {
    return EX_SOFTWARE;
  }
  plugins_.push_back(std::move(plugin));

  return EX_OK;
}

int Daemon::OnEventLoopStarted() {
  // We emit this metric here and not inside the polled method so that we do it
  // exactly once per daemon lifetime.
  MetricsSender::GetInstance().SendEnumMetricToUMA(metrics::kPolicy,
                                                   metrics::Policy::kChecked);
  // This will post a task to run CheckPolicyAndFeature.
  policies_features_broker_->StartAndBlockForSync(
      PoliciesFeaturesBroker::kDefaultPollDuration);

  return EX_OK;
}

void Daemon::OnShutdown(int* exit_code) {
  // Disconnect missive.
  reporting::MissiveClient::Shutdown();

  brillo::DBusDaemon::OnShutdown(exit_code);
}
}  // namespace secagentd
