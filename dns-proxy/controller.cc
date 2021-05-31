// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/controller.h"

#include <sys/capability.h>
#include <sys/prctl.h>

#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/notreached.h>
#include <base/process/launch.h>
#include <base/threading/thread_task_runner_handle.h>
#include <chromeos/scoped_minijail.h>
#include <shill/dbus-constants.h>

namespace dns_proxy {
namespace {

constexpr int kSubprocessRestartDelayMs = 900;
constexpr char kSeccompPolicyPath[] =
    "/usr/share/policy/dns-proxy-seccomp.policy";

}  // namespace

Controller::Controller(const std::string& progname) : progname_(progname) {}

Controller::~Controller() {
  for (const auto& p : proxies_)
    Kill(p);

  if (bus_)
    bus_->ShutdownAndBlock();
}

int Controller::OnInit() {
  LOG(INFO) << "Starting DNS Proxy service";

  // Preserve CAP_NET_BIND_SERVICE so the child processes have the capability.
  // Without the ambient set, file capabilities need to be used.
  if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0) !=
      0) {
    LOG(ERROR) << "Failed to add CAP_NET_BIND_SERVICE to the ambient set";
  }

  // Handle subprocess lifecycle.
  process_reaper_.Register(this);

  /// Run after Daemon::OnInit()
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&Controller::Setup, weak_factory_.GetWeakPtr()));
  return DBusDaemon::OnInit();
}

void Controller::OnShutdown(int*) {
  LOG(INFO) << "Stopping DNS Proxy service";
}

void Controller::Setup() {
  shill_.reset(new shill::Client(bus_));
  shill_->Init();

  patchpanel_ = patchpanel::Client::New();
  CHECK(patchpanel_) << "Failed to initialize patchpanel client";
  patchpanel_->RegisterOnAvailableCallback(base::BindRepeating(
      &Controller::OnPatchpanelReady, weak_factory_.GetWeakPtr()));

  RunProxy(Proxy::Type::kSystem);
  RunProxy(Proxy::Type::kDefault);
}

void Controller::OnPatchpanelReady(bool success) {
  CHECK(success) << "Failed to connect to patchpanel";
  patchpanel_->RegisterNetworkDeviceChangedSignalHandler(base::BindRepeating(
      &Controller::OnVirtualDeviceChanged, weak_factory_.GetWeakPtr()));

  // Process the current set of patchpanel devices and launch any required
  // proxy processes.
  for (const auto& d : patchpanel_->GetDevices())
    VirtualDeviceAdded(d);
}

void Controller::RunProxy(Proxy::Type type, const std::string& ifname) {
  ProxyProc proc(type, ifname);
  auto it = proxies_.find(proc);
  if (it != proxies_.end()) {
    return;
  }

  ScopedMinijail jail(minijail_new());
  minijail_namespace_net(jail.get());
  minijail_no_new_privs(jail.get());
  minijail_use_seccomp_filter(jail.get());
  minijail_parse_seccomp_filters(jail.get(), kSeccompPolicyPath);
  minijail_forward_signals(jail.get());
  minijail_reset_signal_mask(jail.get());
  minijail_reset_signal_handlers(jail.get());
  minijail_run_as_init(jail.get());

  std::vector<char*> argv;
  const std::string flag_t = "--t=" + std::string(Proxy::TypeToString(type));
  argv.push_back(const_cast<char*>(progname_.c_str()));
  argv.push_back(const_cast<char*>(flag_t.c_str()));
  std::string flag_i = "--i=";
  if (!ifname.empty()) {
    flag_i += ifname;
    argv.push_back(const_cast<char*>(flag_i.c_str()));
  }
  argv.push_back(nullptr);

  pid_t pid;
  if (minijail_run_pid(jail.get(), argv[0], argv.data(), &pid) != 0) {
    LOG(DFATAL) << "Failed to launch process for proxy " << proc;
    return;
  }
  proc.pid = pid;
  LOG(INFO) << "Launched process for proxy " << proc;

  if (!process_reaper_.WatchForChild(
          FROM_HERE, pid,
          base::Bind(&Controller::OnProxyExit, weak_factory_.GetWeakPtr(),
                     pid))) {
    LOG(ERROR) << "Failed to watch process for proxy " << proc
               << " - did it crash after launch?";
    return;
  }

  proxies_.emplace(proc);
}

void Controller::KillProxy(Proxy::Type type, const std::string& ifname) {
  auto it = proxies_.find(ProxyProc(type, ifname));
  if (it != proxies_.end()) {
    Kill(*it);
    proxies_.erase(it);
  }
}

void Controller::Kill(const ProxyProc& proc) {
  EvalProxyExit(proc);
  process_reaper_.ForgetChild(proc.pid);
  int rc = kill(proc.pid, SIGTERM);
  if (rc < 0 && rc != ESRCH)
    LOG(ERROR) << "Failed to kill process for proxy " << proc;
}

void Controller::OnProxyExit(pid_t pid, const siginfo_t& siginfo) {
  process_reaper_.ForgetChild(pid);

  // There will only ever be a handful of entries in this map so a linear scan
  // will be trivial.
  ProxyProc proc;
  bool found = false;
  for (auto it = proxies_.begin(); it != proxies_.end(); ++it) {
    if (it->pid == pid) {
      proc = *it;
      proxies_.erase(it);
      found = true;
      break;
    }
  }
  if (!found) {
    LOG(ERROR) << "Unexpected process (" << pid << ") exit signal received";
    return;
  }

  EvalProxyExit(proc);

  switch (siginfo.si_code) {
    case CLD_EXITED:
    case CLD_DUMPED:
    case CLD_KILLED:
    case CLD_TRAPPED:
      LOG(ERROR) << "Process for proxy [" << proc
                 << " was unexpectedly killed (" << siginfo.si_code << ":"
                 << siginfo.si_status << ") - attempting to restart";

      base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&Controller::RunProxy, weak_factory_.GetWeakPtr(),
                     proc.opts.type, proc.opts.ifname),
          base::TimeDelta::FromMilliseconds(kSubprocessRestartDelayMs));
      break;

    case CLD_STOPPED:
      LOG(WARNING) << "Process for proxy " << proc
                   << " was unexpectedly stopped";
      break;

    case CLD_CONTINUED:
      LOG(WARNING) << "Process for proxy " << proc << " has continued";
      break;

    default:
      NOTREACHED();
  }
}

void Controller::EvalProxyExit(const ProxyProc& proc) {
  if (proc.opts.type != Proxy::Type::kSystem)
    return;

  // Ensure the system proxy address is cleared from shill.
  brillo::ErrorPtr error;
  if (!shill_->GetManagerProxy()->SetDNSProxyIPv4Address("", &error))
    LOG(WARNING) << "Failed to clear shill dns-proxy property for " << proc
                 << ": " << error->GetMessage();
}

void Controller::OnVirtualDeviceChanged(
    const patchpanel::NetworkDeviceChangedSignal& signal) {
  switch (signal.event()) {
    case patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED:
      VirtualDeviceAdded(signal.device());
      break;
    case patchpanel::NetworkDeviceChangedSignal::DEVICE_REMOVED:
      VirtualDeviceRemoved(signal.device());
      break;
    default:
      NOTREACHED();
  }
}

void Controller::VirtualDeviceAdded(const patchpanel::NetworkDevice& device) {
  if (device.guest_type() == patchpanel::NetworkDevice::ARC ||
      device.guest_type() == patchpanel::NetworkDevice::ARCVM) {
    RunProxy(Proxy::Type::kARC, device.phys_ifname());
  }
}

void Controller::VirtualDeviceRemoved(const patchpanel::NetworkDevice& device) {
  if (device.guest_type() == patchpanel::NetworkDevice::ARC ||
      device.guest_type() == patchpanel::NetworkDevice::ARCVM) {
    KillProxy(Proxy::Type::kARC, device.phys_ifname());
  }
}

}  // namespace dns_proxy
