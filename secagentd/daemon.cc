// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/daemon.h"
#include <sysexits.h>
#include <utility>
#include "secagentd/factories.h"
#include <brillo/daemons/dbus_daemon.h>
#include <absl/status/status.h>
namespace secagentd {

Daemon::Daemon(struct Inject injected) {
  bpf_plugin_factory_ = std::move(injected.bpf_plugin_factory_);
}

int Daemon::OnInit() {
  int rv = brillo::DBusDaemon::OnInit();
  if (rv != EX_OK) {
    return rv;
  }
  if (bpf_plugin_factory_ == nullptr) {
    bpf_plugin_factory_ = std::make_unique<BpfPluginFactory>();
  }
  return EX_OK;
}

int Daemon::CreateAndRunBpfPlugins() {
  auto plugin = bpf_plugin_factory_->CreateProcessPlugin();

  if (plugin->PolicyIsEnabled()) {
    bpf_plugins_.push_back(std::move(plugin));
  }
  for (auto& plugin : bpf_plugins_) {
    // If BPFs fail loading this is a serious error and the daemon should exit.
    absl::Status result = plugin->LoadAndRun();
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
}  // namespace secagentd
