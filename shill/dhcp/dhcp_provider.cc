// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dhcp/dhcp_provider.h"

#include <signal.h>

#include <base/bind.h>
#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/process.h>
#include <base/process/process_iterator.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>

#include "shill/control_interface.h"
#include "shill/dhcp/dhcpcd_listener_interface.h"
#include "shill/dhcp/dhcpv4_config.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDHCP;
static std::string ObjectID(const DHCPProvider* d) {
  return "(dhcp_provider)";
}
}  // namespace Logging

namespace {
base::LazyInstance<DHCPProvider>::DestructorAtExit g_dhcp_provider =
    LAZY_INSTANCE_INITIALIZER;
static constexpr base::TimeDelta kUnbindDelay = base::Seconds(2);

const char kDHCPCDExecutableName[] = "dhcpcd";

}  // namespace

constexpr char DHCPProvider::kDHCPCDPathFormatLease[];

DHCPProvider::DHCPProvider()
    : root_("/"), control_interface_(nullptr), dispatcher_(nullptr) {
  SLOG(this, 2) << __func__;
}

DHCPProvider::~DHCPProvider() {
  SLOG(this, 2) << __func__;
}

DHCPProvider* DHCPProvider::GetInstance() {
  return g_dhcp_provider.Pointer();
}

void DHCPProvider::Init(ControlInterface* control_interface,
                        EventDispatcher* dispatcher,
                        Metrics* metrics) {
  SLOG(this, 2) << __func__;
  listener_ = control_interface->CreateDHCPCDListener(this);
  control_interface_ = control_interface;
  dispatcher_ = dispatcher;
  metrics_ = metrics;

  // Kill the dhcpcd processes accidentally left by previous run.
  base::NamedProcessIterator iter(kDHCPCDExecutableName, nullptr);
  while (const base::ProcessEntry* entry = iter.NextProcessEntry())
    kill(entry->pid(), SIGKILL);
}

void DHCPProvider::Stop() {
  listener_.reset();
  configs_.clear();
}

DHCPConfigRefPtr DHCPProvider::CreateIPv4Config(
    const std::string& device_name,
    const std::string& lease_file_suffix,
    bool arp_gateway,
    const std::string& hostname) {
  SLOG(this, 2) << __func__ << " device: " << device_name;
  return new DHCPv4Config(control_interface_, dispatcher_, this, device_name,
                          lease_file_suffix, arp_gateway, hostname, metrics_);
}

DHCPConfigRefPtr DHCPProvider::GetConfig(int pid) {
  SLOG(this, 2) << __func__ << " pid: " << pid;
  PIDConfigMap::const_iterator it = configs_.find(pid);
  if (it == configs_.end()) {
    return nullptr;
  }
  return it->second;
}

void DHCPProvider::BindPID(int pid, const DHCPConfigRefPtr& config) {
  SLOG(this, 2) << __func__ << " pid: " << pid;
  configs_[pid] = config;
}

void DHCPProvider::UnbindPID(int pid) {
  SLOG(this, 2) << __func__ << " pid: " << pid;
  configs_.erase(pid);
  recently_unbound_pids_.insert(pid);
  dispatcher_->PostDelayedTask(FROM_HERE,
                               base::BindOnce(&DHCPProvider::RetireUnboundPID,
                                              base::Unretained(this), pid),
                               kUnbindDelay);
}

void DHCPProvider::RetireUnboundPID(int pid) {
  recently_unbound_pids_.erase(pid);
}

bool DHCPProvider::IsRecentlyUnbound(int pid) {
  return base::Contains(recently_unbound_pids_, pid);
}

void DHCPProvider::DestroyLease(const std::string& name) {
  SLOG(this, 2) << __func__ << " name: " << name;
  base::DeleteFile(
      root_.Append(base::StringPrintf(kDHCPCDPathFormatLease, name.c_str())));
}

}  // namespace shill
