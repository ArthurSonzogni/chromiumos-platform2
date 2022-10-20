// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <sysexits.h>
#include <utility>

#include "absl/status/status.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "brillo/daemons/dbus_daemon.h"
#include "missive/client/missive_client.h"
#include "secagentd/daemon.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/process_cache.h"

namespace secagentd {

Daemon::Daemon(struct Inject injected) {
  bpf_plugin_factory_ = std::move(injected.bpf_plugin_factory_);
  message_sender_ = std::move(injected.message_sender_);
  process_cache_ = std::move(injected.process_cache_);
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

  return EX_OK;
}

int Daemon::CreateAndRunBpfPlugins() {
  auto plugin = bpf_plugin_factory_->Create(Types::Plugin::kProcess,
                                            message_sender_, process_cache_);

  if (!plugin) {
    return EX_SOFTWARE;
  }

  if (plugin->PolicyIsEnabled()) {
    bpf_plugins_.push_back(std::move(plugin));
  }
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

int Daemon::OnEventLoopStarted() {
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

void Daemon::OnShutdown(int* exit_code) {
  // Disconnect missive.
  ::reporting::MissiveClient::Shutdown();

  brillo::DBusDaemon::OnShutdown(exit_code);
}
}  // namespace secagentd
