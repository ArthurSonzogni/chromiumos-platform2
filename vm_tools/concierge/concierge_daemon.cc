// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/concierge_daemon.h"

#include <grp.h>
#include <memory>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "base/functional/bind.h"
#include "base/sequence_checker.h"
#include "vm_tools/concierge/service.h"
#include "vm_tools/concierge/tracing.h"

namespace vm_tools::concierge {

namespace {
constexpr gid_t kCrosvmUGid = 299;
}

int ConciergeDaemon::Run(int argc, char** argv) {
  ConciergeDaemon concierge;

  // Threading setup happens after daemon setup, since threads have to inherit
  // the process masks from the daemon.
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("concierge");
  InitTracing();

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  brillo::FlagHelper::Init(argc, argv, "vm_concierge service");

  if (argc != 1) {
    LOG(ERROR) << "Unexpected command line arguments";
    return EXIT_FAILURE;
  }

  // Begin asynchronous execution here.
  {
    DCHECK_CALLED_ON_VALID_SEQUENCE(concierge.sequence_checker_);
    concierge.main_loop_.Run();
  }

  return EXIT_SUCCESS;
}

ConciergeDaemon::ConciergeDaemon()
    : task_executor_(base::MessagePumpType::IO),
      watcher_(task_executor_.task_runner()),
      weak_factory_(this) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(SetupProcess()) << "Failed to initialize concierge process";
  // Queue startup onto our task runner, so that it will begin when
  // we start the run loop.
  task_executor_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&ConciergeDaemon::Start, weak_factory_.GetWeakPtr()));
}

void ConciergeDaemon::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Service::CreateAndHost(
      signal_fd_.get(),
      base::BindOnce(&ConciergeDaemon::OnStarted, weak_factory_.GetWeakPtr()));
}

void ConciergeDaemon::OnStarted(std::unique_ptr<Service> service) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(service) << "Failed to launch service correctly";
  CHECK(!exiting_)
      << "Attempted to complete bringup after we were asked to exit";
  service_ = std::move(service);
}

void ConciergeDaemon::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Return early if we're already shutting down.
  if (exiting_) {
    return;
  }
  exiting_ = true;

  // Shutdown requested before we started hosting (i.e. before OnStarted() was
  // called). Proceed as though the stop has completed.
  if (!service_) {
    OnStopped();
  }

  service_->Stop(
      base::BindOnce(&ConciergeDaemon::OnStopped, weak_factory_.GetWeakPtr()));
}

void ConciergeDaemon::OnStopped() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Use Quit() so that we drop pending tasks. Specifically we don't want to try
  // and handle OnStarted() after we get here.
  main_loop_.Quit();
}

bool ConciergeDaemon::SetupProcess() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // It's not possible to ask minijail to set up a user namespace and switch to
  // a non-0 uid/gid, or to set up supplemental groups. Concierge needs both
  // supplemental groups and to run as a user whose id is unchanged from the
  // root namespace (dbus authentication requires this), so we configure this
  // here.
  if (setresuid(kCrosvmUGid, kCrosvmUGid, kCrosvmUGid) < 0) {
    PLOG(ERROR) << "Failed to set uid to crosvm";
    return false;
  }
  if (setresgid(kCrosvmUGid, kCrosvmUGid, kCrosvmUGid) < 0) {
    PLOG(ERROR) << "Failed to set gid to crosvm";
    return false;
  }
  // Ideally we would just call initgroups("crosvm") here, but internally glibc
  // interprets EINVAL as signaling that the list of supplemental groups is too
  // long and truncates the list, when it could also indicate that some of the
  // gids are unmapped in the current namespace. Instead we look up the groups
  // ourselves so we can log a useful error if the mapping is wrong.
  int ngroups = 0;
  getgrouplist("crosvm", kCrosvmUGid, nullptr, &ngroups);
  std::vector<gid_t> groups(ngroups);
  if (getgrouplist("crosvm", kCrosvmUGid, groups.data(), &ngroups) < 0) {
    PLOG(ERROR) << "Failed to get supplemental groups for user crosvm";
    return false;
  }
  if (setgroups(ngroups, groups.data()) < 0) {
    PLOG(ERROR)
        << "Failed to set supplemental groups. This probably means you have "
           "added user crosvm to groups that are not mapped in the concierge "
           "user namespace and need to update vm_concierge.conf.";
    return false;
  }

  // Change the umask so that the runtime directory for each VM will get the
  // right permissions.
  umask(002);

  // Set up the signalfd for receiving SIGCHLD and SIGTERM.
  // This applies to all threads created afterwards.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGTERM);

  // Restore process' "dumpable" flag so that /proc will be writable.
  // We need it to properly set up jail for Plugin VM helper process.
  if (prctl(PR_SET_DUMPABLE, 1) < 0) {
    PLOG(ERROR) << "Failed to set PR_SET_DUMPABLE";
    return false;
  }

  signal_fd_.reset(signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
  if (!signal_fd_.is_valid()) {
    PLOG(ERROR) << "Failed to create signalfd";
    return false;
  }

  signal_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      signal_fd_.get(), base::BindRepeating(&ConciergeDaemon::OnSignalReadable,
                                            weak_factory_.GetWeakPtr()));
  if (!signal_watcher_) {
    LOG(ERROR) << "Failed to watch signalfd";
    return false;
  }

  // Now block signals from the normal signal handling path so that we will get
  // them via the signalfd.
  if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
    PLOG(ERROR) << "Failed to block signals via sigprocmask";
    return false;
  }

  // TODO(b/193806814): This log line helps us detect when there is a race
  // during signal setup. When we eventually fix that bug we won't need it.
  LOG(INFO) << "Finished setting up signal handlers";
  return true;
}

void ConciergeDaemon::OnSignalReadable() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  struct signalfd_siginfo siginfo;
  if (read(signal_fd_.get(), &siginfo, sizeof(siginfo)) != sizeof(siginfo)) {
    PLOG(ERROR) << "Failed to read from signalfd";
    return;
  }

  if (siginfo.ssi_signo == SIGCHLD) {
    // Only bother forwarding the child signal if there is a service with
    // running children.
    // If the handler is blocked during shutdown we may try to process the
    // signal after the service already was destroyed.
    if (service_) {
      service_->ChildExited();
    }
    return;
  }

  if (siginfo.ssi_signo != SIGTERM) {
    LOG(ERROR) << "Received unknown signal from signal fd: "
               << strsignal(siginfo.ssi_signo);
    return;
  }

  Stop();
}

}  // namespace vm_tools::concierge
