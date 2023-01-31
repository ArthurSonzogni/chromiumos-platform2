// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <cstdlib>
#include <iomanip>
#include <memory>
#include <sysexits.h>
#include <utility>

#include "absl/status/status.h"
#include "attestation/proto_bindings/interface.pb.h"
#include "attestation-client/attestation/dbus-proxies.h"
#include "base/functional/callback_forward.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"
#include "brillo/daemons/dbus_daemon.h"
#include "featured/c_feature_library.h"
#include "featured/feature_library.h"
#include "missive/client/missive_client.h"
#include "policy/device_policy.h"
#include "policy/libpolicy.h"
#include "secagentd/daemon.h"
#include "secagentd/message_sender.h"
#include "secagentd/metrics_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/process_cache.h"

namespace secagentd {
static const struct VariationsFeature kCrOSLateBootSecagentdXDRReporting = {
    .name = "CrOSLateBootSecagentdXDRReporting",
    .default_state = FEATURE_ENABLED_BY_DEFAULT};

Daemon::Daemon(struct Inject injected) : weak_ptr_factory_(this) {
  plugin_factory_ = std::move(injected.plugin_factory_);
  message_sender_ = std::move(injected.message_sender_);
  process_cache_ = std::move(injected.process_cache_);
  policy_provider_ = std::move(injected.policy_provider_);
}

Daemon::Daemon(bool bypass_policy_for_testing,
               bool bypass_enq_ok_wait_for_testing,
               uint32_t set_heartbeat_period_s_for_testing)
    : bypass_policy_for_testing_(bypass_policy_for_testing),
      bypass_enq_ok_wait_for_testing_(bypass_enq_ok_wait_for_testing),
      set_heartbeat_period_s_for_testing_(set_heartbeat_period_s_for_testing),
      weak_ptr_factory_(this) {}

int Daemon::OnInit() {
  int rv = brillo::DBusDaemon::OnInit();
  if (rv != EX_OK) {
    return rv;
  }

  // Finch feature interface which allows us to run experiments.
  if (features_ == nullptr) {
    features_ = feature::PlatformFeatures::New(bus_);
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

  if (policy_provider_ == nullptr) {
    policy_provider_ = std::make_unique<policy::PolicyProvider>();
  }

  return EX_OK;
}

void Daemon::CheckXDRFlags(bool isXDREnabled) {
  if (!isXDREnabled) {
    LOG(INFO) << "XDR reporting feature on this device is disabled."
                 " Not sending events.";
  } else if (!xdr_reporting_policy_) {
    LOG(INFO) << "XDR reporting policy is disabled. Not sending events.";
  }
  // If either policy is false do not report.
  if (!isXDREnabled || !xdr_reporting_policy_) {
    if (reporting_events_) {
      LOG(INFO) << "Stopping event reporting and quitting";
      // Will exit and restart secagentd.
      Quit();
    }
    return;
  } else {
    if (!reporting_events_) {
      LOG(INFO) << "Starting event reporting";
      // This is emitted at most once per daemon lifetime.
      MetricsSender::GetInstance().SendEnumMetricToUMA(
          metrics::kPolicy, metrics::Policy::kEnabled);
      reporting_events_ = true;
      StartXDRReporting();
    } else {
      return;
    }
  }
}

void Daemon::StartXDRReporting() {
  using CbType = base::OnceCallback<void()>;
  CbType cb_for_agent =
      base::BindOnce(&Daemon::RunPlugins, weak_ptr_factory_.GetWeakPtr());
  CbType cb_for_now = base::DoNothing();
  if (bypass_enq_ok_wait_for_testing_) {
    std::swap(cb_for_agent, cb_for_now);
  }
  agent_plugin_ = plugin_factory_->Create(
      Types::Plugin::kAgent, message_sender_,
      std::make_unique<org::chromium::AttestationProxy>(bus_),
      std::make_unique<org::chromium::TpmManagerProxy>(bus_),
      std::move(cb_for_agent), set_heartbeat_period_s_for_testing_);

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
      plugin = plugin_factory_->Create(Types::Plugin::kProcess, message_sender_,
                                       process_cache_);
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
  check_flags_timer_.Start(
      FROM_HERE, base::Minutes(10),
      base::BindRepeating(&Daemon::PollFlags, weak_ptr_factory_.GetWeakPtr()));

  // Delay before first timer invocation so add check.
  // We emit this metric here and not inside the polled method so that we do it
  // exactly once per daemon lifetime.
  MetricsSender::GetInstance().SendEnumMetricToUMA(metrics::kPolicy,
                                                   metrics::Policy::kChecked);
  PollFlags();

  return EX_OK;
}

void Daemon::PollFlags() {
  GetXDRReportingIsEnabled();
  features_->IsEnabled(
      kCrOSLateBootSecagentdXDRReporting,
      base::BindOnce(&Daemon::CheckXDRFlags, weak_ptr_factory_.GetWeakPtr()));
}

void Daemon::GetXDRReportingIsEnabled() {
  // Bypasses the policy check for testing.
  if (bypass_policy_for_testing_) {
    xdr_reporting_policy_ = true;
    return;
  }

  policy_provider_->Reload();
  if (policy_provider_->device_policy_is_loaded()) {
    xdr_reporting_policy_ =
        policy_provider_->GetDevicePolicy().GetDeviceReportXDREvents().value_or(
            false);
  } else {
    // Default value is do not report.
    xdr_reporting_policy_ = false;
  }
}

void Daemon::OnShutdown(int* exit_code) {
  // Disconnect missive.
  reporting::MissiveClient::Shutdown();

  brillo::DBusDaemon::OnShutdown(exit_code);
}
}  // namespace secagentd
