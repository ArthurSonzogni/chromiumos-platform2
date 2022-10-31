// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <sysexits.h>
#include <utility>

#include "absl/status/status.h"
#include "base/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"
#include "brillo/daemons/dbus_daemon.h"
#include "missive/client/missive_client.h"
#include "policy/device_policy.h"
#include "policy/libpolicy.h"
#include "secagentd/daemon.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/process_cache.h"

namespace secagentd {

Daemon::Daemon(struct Inject injected) {
  bpf_plugin_factory_ = std::move(injected.bpf_plugin_factory_);
  message_sender_ = std::move(injected.message_sender_);
  process_cache_ = std::move(injected.process_cache_);
  policy_provider_ = std::move(injected.policy_provider_);
}

int Daemon::OnInit() {
  int rv = brillo::DBusDaemon::OnInit();
  if (rv != EX_OK) {
    return rv;
  }

  if (bpf_plugin_factory_ == nullptr) {
    bpf_plugin_factory_ = std::make_unique<PluginFactory>();
  }

  if (message_sender_ == nullptr) {
    // Set up ERP.
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams(
        "missive_thread_pool");
    ::reporting::MissiveClient::Initialize(bus_.get());

    message_sender_ = base::MakeRefCounted<MessageSender>();

    absl::Status result = message_sender_->InitializeQueues();
    if (result != absl::OkStatus()) {
      LOG(ERROR) << result.message();
      return EX_SOFTWARE;
    }
  }

  if (process_cache_ == nullptr) {
    process_cache_ = base::MakeRefCounted<ProcessCache>();
  }

  if (policy_provider_ == nullptr) {
    policy_provider_ = std::make_unique<policy::PolicyProvider>();
  }

  return EX_OK;
}

int Daemon::CreateAndRunBpfPlugins() {
  auto plugin = bpf_plugin_factory_->Create(Types::Plugin::kProcess,
                                            message_sender_, process_cache_);

  if (!plugin) {
    return EX_SOFTWARE;
  }

  bpf_plugins_.push_back(std::move(plugin));

  for (auto& plugin : bpf_plugins_) {
    // If BPFs fail loading this is a serious error and the daemon should exit.
    absl::Status result = plugin->Activate();
    if (!result.ok()) {
      LOG(ERROR) << result.message();
      return EX_SOFTWARE;
    }
  }
  return EX_OK;
}

int Daemon::CreateAndRunAgentPlugins() {
  // TODO(b:241578769): Implement and run agent plugin.
  return EX_OK;
}

int Daemon::RunPlugins() {
  int rv;

  rv = CreateAndRunBpfPlugins();
  if (rv != EX_OK) {
    return rv;
  }
  rv = CreateAndRunAgentPlugins();
  if (rv != EX_OK) {
    return rv;
  }

  return EX_OK;
}

int Daemon::OnEventLoopStarted() {
  check_xdr_reporting_timer_.Start(
      FROM_HERE, base::Minutes(10),
      base::BindRepeating(&Daemon::PollXdrReportingIsEnabled,
                          base::Unretained(this)));

  // Delay before first timer invocation so add check.
  PollXdrReportingIsEnabled();

  return EX_OK;
}

bool Daemon::XdrReportingIsEnabled() {
  bool old_policy = xdr_reporting_policy_;

  policy_provider_->Reload();
  if (policy_provider_->device_policy_is_loaded()) {
    xdr_reporting_policy_ =
        policy_provider_->GetDevicePolicy().GetDeviceReportXDREvents().value_or(
            false);
  } else {
    // Default value is do not report.
    xdr_reporting_policy_ = false;
  }

  // Return true if xdr_reporting_policy_ has changed.
  return old_policy != xdr_reporting_policy_;
}

void Daemon::PollXdrReportingIsEnabled() {
  if (XdrReportingIsEnabled()) {
    if (xdr_reporting_policy_) {
      LOG(INFO) << "Starting event reporting";
      int rv = RunPlugins();
      if (rv != EX_OK) {
        QuitWithExitCode(rv);
      }
    } else {
      LOG(INFO) << "Stopping event reporting and quitting";
      // Will exit and restart secagentd.
      Quit();
    }
  }
}

void Daemon::OnShutdown(int* exit_code) {
  // Disconnect missive.
  ::reporting::MissiveClient::Shutdown();

  brillo::DBusDaemon::OnShutdown(exit_code);
}
}  // namespace secagentd
