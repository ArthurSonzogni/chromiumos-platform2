// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/session_manager_service.h"

#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/command_line.h>
#include <base/containers/extend.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>
#include <brillo/message_loops/message_loop.h>
#include <chromeos/dbus/device_management/dbus-constants.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/dbus/vm_concierge/dbus-constants.h>
#include <chromeos/switches/chrome_switches.h>
#include <dbus/arc_manager/dbus-constants.h>
#include <dbus/bus.h>
#include <dbus/dbus.h>  // C dbus library header. Used in FilterMessage().
#include <dbus/error.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <vm_concierge/concierge_service.pb.h>

#include "login_manager/arc_manager.h"
#include "login_manager/arc_manager_proxy.h"
#include "login_manager/browser_job.h"
#include "login_manager/chrome_features_service_client.h"
#include "login_manager/liveness_checker_impl.h"
#include "login_manager/login_metrics.h"
#include "login_manager/nss_util.h"
#include "login_manager/session_manager_impl.h"
#include "login_manager/siginfo_description.h"
#include "login_manager/system_utils.h"
#include "login_manager/systemd_unit_starter.h"
#include "login_manager/upstart_signal_emitter.h"

// TODO(crbug.com/6331860): Clean up the code once we make sure
// arc-manager D-Bus process works well.
#define USE_DBUS_ARC_MANAGER 1

namespace em = enterprise_management;
namespace login_manager {

namespace {

const int kSignals[] = {SIGTERM, SIGINT, SIGHUP};
const int kNumSignals = sizeof(kSignals) / sizeof(int);

// The path where the pid of an aborted browser process is written. This is done
// so that crash reporting tools can detect an abort that originated from
// session_manager.
const char kAbortedBrowserPidPath[] = "/run/chrome/aborted_browser_pid";

// The path where the pid of browser process is written if it took too long to
// shutdown. This is done so that crash reporting tools can detect an abort that
// originated from session_manager.
const char kShutdownBrowserPidPath[] = "/run/chrome/shutdown_browser_pid";

// How long to wait before timing out on a StopAllVms message.  Wait up to 2
// minutes as there may be multiple VMs and they may each take some time to
// cleanly shut down.
constexpr int kStopAllVmsTimeoutMs = 120000;

// Timeout for StopVm request for ARCVM.
constexpr int kStopArcVmTimeoutMs = 60000;

// Long kill time out. Used instead of the default one when chrome feature
// 'SessionManagerLongKillTimeout' is enabled. Note that this must be less than
// the 20-second kill timeout granted to session_manager in ui.conf.
constexpr base::TimeDelta kLongKillTimeout = base::Seconds(12);

// A flag file of whether to dump chrome crashes on dev/test image.
constexpr char kCollectChromeFile[] =
    "/mnt/stateful_partition/etc/collect_chrome_crashes";

constexpr char kFeatureNameSessionManagerLongKillTimeout[] =
    "SessionManagerLongKillTimeout";

// This needs to match exactly the name of feature kSessionManagerLivenessCheck
// in (Chromium) ash_features.cc.
constexpr char kFeatureNameSessionManagerLivenessCheck[] =
    "SessionManagerLivenessCheck";

// I need a do-nothing action for SIGALRM, or using alarm() will kill me.
void DoNothing(int signal) {}

const char* ExitCodeToString(SessionManagerService::ExitCode code) {
  switch (code) {
    case SessionManagerService::SUCCESS:
      return "exiting cleanly";
    case SessionManagerService::CRASH_WHILE_RESTART_DISABLED:
      return "got crash while restart disabled";
    case SessionManagerService::CHILD_EXITING_TOO_FAST:
      return "child exiting too fast";
    case SessionManagerService::MUST_WIPE_DEVICE:
      return "must wipe device";
    case SessionManagerService::DEVICE_SHUTTING_DOWN:
      return "device shutting down";
  }
  NOTREACHED_IN_MIGRATION() << "Invalid exit code " << code;
  return "unknown";
}

}  // anonymous namespace

void SessionManagerService::TestApi::ScheduleChildExit(pid_t pid, int status) {
  siginfo_t info;
  info.si_pid = pid;
  if (WIFEXITED(status)) {
    info.si_code = CLD_EXITED;
    info.si_status = WEXITSTATUS(status);
  } else {
    info.si_status = WTERMSIG(status);
  }
  brillo::MessageLoop::current()->PostTask(
      FROM_HERE, base::BindOnce(base::IgnoreResult(
                                    &SessionManagerService::HandleBrowserExit),
                                session_manager_service_, info));
}

SessionManagerService::SessionManagerService(
    base::OnceCallback<std::unique_ptr<BrowserJobInterface>(
        brillo::ProcessReaper&)> browser_job_factory,
    const base::FilePath& magic_chrome_file,
    std::optional<base::FilePath> ns_path,
    base::TimeDelta kill_timeout,
    bool enable_browser_abort_on_hang,
    base::TimeDelta hang_detection_interval,
    int hang_detection_retries,
    LoginMetrics* metrics,
    SystemUtils* system_utils)
    : chrome_mount_ns_path_(ns_path),
      kill_timeout_(kill_timeout),
      file_checker_(magic_chrome_file),
      match_rule_(base::StringPrintf("type='method_call', interface='%s'",
                                     kSessionManagerInterface)),
      login_metrics_(metrics),
      system_utils_(system_utils),
      nss_(NssUtil::Create()),
      owner_key_(system_utils, nss_->GetOwnerKeyFilePath(), nss_.get()),
      device_identifier_generator_(system_utils, metrics),
      enable_browser_abort_on_hang_(enable_browser_abort_on_hang),
      liveness_checking_interval_(hang_detection_interval),
      liveness_checking_retries_(hang_detection_retries),
      aborted_browser_pid_path_(kAbortedBrowserPidPath),
      shutdown_browser_pid_path_(kShutdownBrowserPidPath),
      browser_(std::move(browser_job_factory).Run(process_reaper_)),
      vpd_process_(system_utils, process_reaper_) {
  DCHECK(browser_);
  SetUpHandlers();
}

SessionManagerService::~SessionManagerService() {
  RevertHandlers();
}

bool SessionManagerService::Initialize() {
  LOG(INFO) << "SessionManagerService starting";
  InitializeDBus();

  screen_lock_dbus_proxy_ =
      bus_->GetObjectProxy(chromeos::kScreenLockServiceName,
                           dbus::ObjectPath(chromeos::kScreenLockServicePath));

  powerd_dbus_proxy_ = bus_->GetObjectProxy(
      power_manager::kPowerManagerServiceName,
      dbus::ObjectPath(power_manager::kPowerManagerServicePath));

  vm_concierge_dbus_proxy_ = bus_->GetObjectProxy(
      vm_tools::concierge::kVmConciergeServiceName,
      dbus::ObjectPath(vm_tools::concierge::kVmConciergeServicePath));

  vm_concierge_dbus_proxy_->SetNameOwnerChangedCallback(base::BindRepeating(
      &SessionManagerService::VmConciergeOwnerChanged, base::Unretained(this)));
  vm_concierge_dbus_proxy_->WaitForServiceToBeAvailable(base::BindOnce(
      &SessionManagerService::VmConciergeAvailable, base::Unretained(this)));

  dbus::ObjectProxy* system_clock_proxy = bus_->GetObjectProxy(
      system_clock::kSystemClockServiceName,
      dbus::ObjectPath(system_clock::kSystemClockServicePath));

#if USE_SYSTEMD
  using InitDaemonControllerImpl = SystemdUnitStarter;
#else
  using InitDaemonControllerImpl = UpstartSignalEmitter;
#endif
  dbus::ObjectProxy* init_dbus_proxy =
      bus_->GetObjectProxy(InitDaemonControllerImpl::kServiceName,
                           dbus::ObjectPath(InitDaemonControllerImpl::kPath));

  dbus::ObjectProxy* dbus_daemon_proxy = bus_->GetObjectProxy(
      dbus::kDBusServiceName, dbus::ObjectPath(dbus::kDBusServicePath));
  dbus::ObjectProxy* liveness_proxy =
      bus_->GetObjectProxy(chromeos::kLivenessServiceName,
                           dbus::ObjectPath(chromeos::kLivenessServicePath));
  liveness_checker_.reset(new LivenessCheckerImpl(
      this, liveness_proxy, dbus_daemon_proxy, enable_browser_abort_on_hang_,
      liveness_checking_interval_, liveness_checking_retries_, login_metrics_));

  fwmp_dbus_proxy_ = bus_->GetObjectProxy(
      device_management::kDeviceManagementServiceName,
      dbus::ObjectPath(device_management::kDeviceManagementServicePath));

  chrome_features_service_client_ =
      std::make_unique<ChromeFeaturesServiceClient>(bus_->GetObjectProxy(
          chromeos::kChromeFeaturesServiceName,
          dbus::ObjectPath(chromeos::kChromeFeaturesServicePath)));

#if USE_DBUS_ARC_MANAGER
  arc_manager_proxy_ = std::make_unique<ArcManagerProxyDBus>(bus_);
#else
  arc_manager_ = std::make_unique<ArcManager>(*system_utils_, *login_metrics_,
                                              process_reaper_, bus_);
  arc_manager_->Initialize();
  CHECK(arc_manager_->StartDBusService())
      << "Unable to start " << arc_manager::kArcManagerServiceName
      << " D-Bus service.";

  arc_manager_proxy_ =
      std::make_unique<ArcManagerProxyInProcess>(*arc_manager_);
#endif

  impl_ = std::make_unique<SessionManagerImpl>(
      this /* delegate */,
      std::make_unique<InitDaemonControllerImpl>(init_dbus_proxy), bus_,
      &device_identifier_generator_,
      this /* manager, i.e. ProcessManagerServiceInterface */, login_metrics_,
      nss_.get(), chrome_mount_ns_path_, system_utils_, &crossystem_,
      &vpd_process_, &owner_key_, arc_manager_proxy_.get(),
      &install_attributes_reader_, powerd_dbus_proxy_, system_clock_proxy);
  if (!InitializeImpl()) {
    return false;
  }

  InitializeBrowser();

  CHECK(impl_->StartDBusService())
      << "Unable to start " << kSessionManagerServiceName << " D-Bus service.";
  return true;
}

void SessionManagerService::InitializeBrowser() {
  // Set any flags that were specified system-wide.
  browser_->SetFeatureFlags(impl_->GetFeatureFlags(), {});
  browser_->SetExtraArguments(impl_->GetExtraCommandLineArguments());

  if (!ShouldRunBrowser()) {
    // Allows devs to start/stop browser manually.
    return;
  }
  brillo::MessageLoop::current()->PostTask(
      FROM_HERE, base::BindOnce(&SessionManagerService::RunBrowser,
                                base::Unretained(this)));
}

void SessionManagerService::Finalize() {
  LOG(INFO) << "SessionManagerService exiting";
  if (arc_manager_) {
    arc_manager_->Finalize();
  }
  impl_->Finalize();
  ShutDownDBus();
}

void SessionManagerService::LockScreen() {
  dbus::MethodCall call(chromeos::kScreenLockServiceInterface,
                        chromeos::kScreenLockServiceShowLockScreenMethod);
  screen_lock_dbus_proxy_->CallMethod(
      &call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, base::DoNothing());
}

void SessionManagerService::RestartDevice(const std::string& description) {
  dbus::MethodCall call(power_manager::kPowerManagerInterface,
                        power_manager::kRequestRestartMethod);
  dbus::MessageWriter writer(&call);
  writer.AppendInt32(power_manager::REQUEST_RESTART_OTHER);
  writer.AppendString(description);
  (void)powerd_dbus_proxy_->CallMethodAndBlock(
      &call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
}

void SessionManagerService::ScheduleShutdown() {
  SetExitAndScheduleShutdown(SUCCESS);
}

void SessionManagerService::RunBrowser() {
  DCHECK(!abort_timer_.IsRunning());
  browser_->RunInBackground(base::BindOnce(
      &SessionManagerService::HandleBrowserExit, base::Unretained(this)));

  DLOG(INFO) << "Browser is " << browser_->CurrentPid();
  liveness_checker_->Start();

  // |chrome_features_service_client_| is null in test.
  if (chrome_features_service_client_) {
    chrome_features_service_client_->IsFeatureEnabled(
        kFeatureNameSessionManagerLongKillTimeout,
        base::BindOnce(&SessionManagerService::OnLongKillTimeoutEnabled,
                       base::Unretained(this)));

    chrome_features_service_client_->IsFeatureEnabled(
        kFeatureNameSessionManagerLivenessCheck,
        base::BindOnce(&SessionManagerService::OnLivenessCheckEnabled,
                       base::Unretained(this)));
  }

  // Note that |child_exit_handler_| will catch browser process termination and
  // call HandleBrowserExit().
}

void SessionManagerService::AbortBrowserForHang() {
  if (abort_timer_.IsRunning()) {
    LOG(WARNING) << "Aborting the browser is in progress.";
    return;
  }

  LOG(INFO) << "Browser did not respond to DBus liveness check.";
  WriteBrowserPidFile(aborted_browser_pid_path_);
  browser_->Kill(SIGABRT, "Browser aborted");
  // Set a timer to trigger SIGKILL on timeout.
  // In common case, we expect HandleBrowserExit will run the post-process of
  // the termination of SIGABRT above before this timer, and it will be
  // cancelled in HandleBrowserExit.
  abort_timer_.Start(FROM_HERE, GetKillTimeout(),
                     base::BindOnce(&SessionManagerService::OnAbortTimedOut,
                                    base::Unretained(this)));
}

void SessionManagerService::OnAbortTimedOut() {
  // The browser process is not terminated yet by the SIGABRT.
  // Send SIGKILL to all the Chrome processes as a last resort.
  browser_->KillEverything(SIGKILL, "Timed out on aborting");
  abort_timer_.Start(FROM_HERE, base::Seconds(1),
                     base::BindOnce(&SessionManagerService::OnSigkillTimedOut,
                                    base::Unretained(this)));
}

void SessionManagerService::OnSigkillTimedOut() {
  pid_t pid = browser_->CurrentPid();
  // Timer should be cancelled on browser process termination.
  DCHECK_GE(pid, 0);
  LOG(ERROR) << "Browser process " << pid << "'s group still not gone ";
}

void SessionManagerService::SetBrowserTestArgs(
    const std::vector<std::string>& args) {
  browser_->SetTestArguments(args);
}

void SessionManagerService::SetBrowserArgs(
    const std::vector<std::string>& args) {
  browser_->SetArguments(args);
}

void SessionManagerService::SetBrowserAdditionalEnvironmentalVariables(
    const std::vector<std::string>& env_vars) {
  browser_->SetAdditionalEnvironmentVariables(env_vars);
}

void SessionManagerService::RestartBrowser() {
  // Waiting for Chrome to shutdown takes too much time.
  // We're killing it immediately hoping that data Chrome uses before
  // logging in is not corrupted.
  // TODO(avayvod): Remove RestartJob when crosbug.com/6924 is fixed.
  if (browser_->CurrentPid() > 0) {
    browser_->KillEverything(SIGKILL, "Restarting browser on-demand.");
  }
  // The browser will be restarted in HandleBrowserExit().
}

void SessionManagerService::SetBrowserSessionForUser(
    const std::string& account_id, const std::string& userhash) {
  browser_->StartSession(account_id, userhash);
}

void SessionManagerService::SetFlagsForUser(
    const std::string& account_id, const std::vector<std::string>& flags) {
  // Merge provided flags with the ones enforced through device policies.
  std::vector<std::string> combined_flags(flags);
  base::Extend(combined_flags, impl_->GetExtraCommandLineArguments());

  browser_->SetExtraArguments(combined_flags);
}

void SessionManagerService::SetFeatureFlagsForUser(
    const std::string& account_id,
    const std::vector<std::string>& feature_flags,
    const std::map<std::string, std::string>& origin_list_flags) {
  // Reset extra command line arguments to their default value.
  browser_->SetExtraArguments(impl_->GetExtraCommandLineArguments());
  browser_->SetFeatureFlags(feature_flags, origin_list_flags);
}

bool SessionManagerService::IsBrowser(pid_t pid) {
  return (browser_->CurrentPid() > 0 && pid == browser_->CurrentPid());
}

std::optional<pid_t> SessionManagerService::GetBrowserPid() const {
  if (browser_->CurrentPid() <= 0) {
    return std::nullopt;
  }
  return browser_->CurrentPid();
}

base::TimeTicks SessionManagerService::GetLastBrowserRestartTime() {
  return last_browser_restart_time_;
}

void SessionManagerService::HandleBrowserExit(const siginfo_t& status) {
  CHECK(IsBrowser(status.si_pid));

  // The browser process is terminated. Stop the aborting process.
  abort_timer_.Stop();
  LOG(INFO) << "Browser process " << status.si_pid << " exited with "
            << GetExitDescription(status);

  // Clears up the whole job's process group.
  browser_->KillEverything(SIGKILL, "Ensuring browser processes are gone.");
  DLOG(INFO) << "Waiting up to " << GetKillTimeout().InSeconds()
             << " seconds for browser process group to exit";
  if (!browser_->WaitForExit(GetKillTimeout())) {
    LOG(ERROR) << "Browser process still around after SIGKILL and "
               << GetKillTimeout().InSeconds() << " seconds.";
  }
  browser_->ClearPid();

  // Ensure ARC containers are gone.
  if (arc_manager_proxy_) {
    // Note: in tests, arc_manager_proxy_ is not set up.
    arc_manager_proxy_->RequestJobExit(
        static_cast<uint32_t>(ArcContainerStopReason::BROWSER_SHUTDOWN));
    arc_manager_proxy_->EnsureJobExit(
        ArcManager::kContainerTimeout.InMilliseconds());
    // Ensure ARCVM and related Upstart jobs are stopped (b/290194650).
  }
  MaybeStopArcVm();

  // Note: in tests, arc_manager_proxy_ is not set up.
  if (arc_manager_proxy_) {
    arc_manager_proxy_->EmitStopArcVmInstanceImpulse();
  }

  // Do nothing if already shutting down.
  if (shutting_down_) {
    return;
  }

  liveness_checker_->Stop();

  std::string end_reason;
  if (impl_->ShouldEndSession(&end_reason)) {
    LOG(ERROR) << "Ending session rather than restarting browser: "
               << end_reason << ".";
    SetExitAndScheduleShutdown(CRASH_WHILE_RESTART_DISABLED);
    return;
  }

  if (browser_->ShouldStop()) {
    LOG(WARNING) << "Child stopped, shutting down";
    SetExitAndScheduleShutdown(CHILD_EXITING_TOO_FAST);
  } else if (ShouldRunBrowser()) {
    // TODO(cmasone): deal with fork failing in RunBrowser()
    RunBrowser();
    last_browser_restart_time_ = base::TimeTicks::Now();
  } else {
    LOG(INFO) << "Should NOT run " << browser_->GetName() << " again.";
    AllowGracefulExitOrRunForever();
  }
}

DBusHandlerResult SessionManagerService::FilterMessage(DBusConnection* conn,
                                                       DBusMessage* message,
                                                       void* data) {
  SessionManagerService* service = static_cast<SessionManagerService*>(data);
  if (::dbus_message_is_method_call(message, kSessionManagerInterface,
                                    kSessionManagerRestartJob)) {
    const char* sender = ::dbus_message_get_sender(message);
    if (!sender) {
      LOG(ERROR) << "Call to RestartJob has no sender";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    LOG(INFO) << "Received RestartJob from " << sender;
    DBusMessage* get_pid = ::dbus_message_new_method_call(
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "GetConnectionUnixProcessID");
    CHECK(get_pid);
    ::dbus_message_append_args(get_pid, DBUS_TYPE_STRING, &sender,
                               DBUS_TYPE_INVALID);
    DBusMessage* got_pid =
        ::dbus_connection_send_with_reply_and_block(conn, get_pid, -1, nullptr);
    ::dbus_message_unref(get_pid);
    if (!got_pid) {
      LOG(ERROR) << "Could not look up sender of RestartJob.";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    uint32_t pid;
    if (!::dbus_message_get_args(got_pid, nullptr, DBUS_TYPE_UINT32, &pid,
                                 DBUS_TYPE_INVALID)) {
      ::dbus_message_unref(got_pid);
      LOG(ERROR) << "Could not extract pid of sender of RestartJob.";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    ::dbus_message_unref(got_pid);
    if (!service->IsBrowser(pid)) {
      LOG(WARNING) << "Sender of RestartJob (PID " << pid
                   << ") is no child of mine!";
      DBusMessage* denial = dbus_message_new_error(
          message, DBUS_ERROR_ACCESS_DENIED, "Sender is not browser.");
      if (!denial || !::dbus_connection_send(conn, denial, nullptr)) {
        LOG(ERROR) << "Could not create error response to RestartJob.";
      }
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  }
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void SessionManagerService::SetUpHandlers() {
  // I have to ignore SIGUSR1, because Xorg sends it to this process when it's
  // got no clients and is ready for new ones.  If we don't ignore it, we die.
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_IGN;
  CHECK_EQ(sigaction(SIGUSR1, &action, nullptr), 0);

  action.sa_handler = DoNothing;
  CHECK_EQ(sigaction(SIGALRM, &action, nullptr), 0);

  signal_handler_.Init();
  process_reaper_.Register(&signal_handler_);
  for (int i = 0; i < kNumSignals; ++i) {
    signal_handler_.RegisterHandler(
        kSignals[i],
        base::BindRepeating(&SessionManagerService::OnTerminationSignal,
                            base::Unretained(this)));
  }
}

void SessionManagerService::RevertHandlers() {
  struct sigaction action = {};
  action.sa_handler = SIG_DFL;
  RAW_CHECK(sigaction(SIGUSR1, &action, nullptr) == 0);
  RAW_CHECK(sigaction(SIGALRM, &action, nullptr) == 0);
}

base::TimeDelta SessionManagerService::GetKillTimeout() {
  // When Chrome is configured to write core files (which only happens during
  // testing), give it extra time to exit.
  if (base::PathExists(base::FilePath(kCollectChromeFile))) {
    return kLongKillTimeout;
  }

  if (use_long_kill_timeout_) {
    return kLongKillTimeout;
  }

  return kill_timeout_;
}

bool SessionManagerService::ShouldRunBrowser() {
  return !file_checker_.exists();
}

bool SessionManagerService::InitializeImpl() {
  if (!impl_->Initialize()) {
    LOG(ERROR) << "Policy key is likely corrupt. Initiating device wipe.";
    impl_->InitiateDeviceWipe("bad_policy_key");
    impl_->Finalize();
    exit_code_ = MUST_WIPE_DEVICE;
    return false;
  }
  return true;
}

void SessionManagerService::InitializeDBus() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(options);
  CHECK(bus_->Connect());
  CHECK(bus_->SetUpAsyncOperations());

  bus_->AddFilterFunction(&SessionManagerService::FilterMessage, this);
  dbus::Error error;
  bus_->AddMatch(match_rule_, &error);
  CHECK(!error.IsValid()) << "Failed to add match to bus: " << error.name()
                          << ", message=" << error.message();
}

void SessionManagerService::ShutDownDBus() {
  dbus::Error error;
  bus_->RemoveMatch(match_rule_, &error);
  if (error.IsValid()) {
    LOG(ERROR) << "Failed to remove match from bus: " << error.name()
               << ", message=" << error.message();
  }
  bus_->RemoveFilterFunction(&SessionManagerService::FilterMessage, this);
  bus_->ShutdownAndBlock();
}

void SessionManagerService::AllowGracefulExitOrRunForever() {
  if (exit_on_child_done_) {
    LOG(INFO) << "SessionManagerService set to exit on child done";
    brillo::MessageLoop::current()->PostTask(
        FROM_HERE, base::BindOnce(base::IgnoreResult(
                                      &SessionManagerService::ScheduleShutdown),
                                  this));
  } else {
    DLOG(INFO) << "OK, running forever...";
  }
}

void SessionManagerService::SetExitAndScheduleShutdown(ExitCode code) {
  LoginMetrics::SessionExitType exit_type =
      LoginMetrics::SessionExitType::NORMAL_EXIT;
  if (code == CHILD_EXITING_TOO_FAST) {
    exit_type = LoginMetrics::SessionExitType::LOGIN_CRASH_LOOP;
  }
  login_metrics_->SendSessionExitType(exit_type);

  // Stop the VMs from this session as their data will no longer be accessible.
  MaybeStopAllVms();

  shutting_down_ = true;
  exit_code_ = code;
  impl_->AnnounceSessionStoppingIfNeeded();

  process_reaper_.Unregister();
  liveness_checker_->Stop();
  CleanupChildrenBeforeExit(code);
  impl_->AnnounceSessionStopped();

  brillo::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::BindOnce(&brillo::MessageLoop::BreakLoop,
                     base::Unretained(brillo::MessageLoop::current())));
  LOG(INFO) << "SessionManagerService quitting run loop";
}

void SessionManagerService::CleanupChildrenBeforeExit(ExitCode code) {
  const std::string reason = ExitCodeToString(code);

  const base::TimeTicks browser_exit_start_time = base::TimeTicks::Now();
  browser_->Kill(SIGTERM, reason);
  if (arc_manager_proxy_) {
    // In test, arc_manager_ is nullptr.
    arc_manager_proxy_->RequestJobExit(static_cast<uint32_t>(
        code == ExitCode::SUCCESS
            ? ArcContainerStopReason::SESSION_MANAGER_SHUTDOWN
            : ArcContainerStopReason::BROWSER_SHUTDOWN));
  }
  const base::TimeDelta browser_timeout = GetKillTimeout();
  DLOG(INFO) << "Waiting up to " << browser_timeout.InSeconds()
             << " seconds for browser process group to exit";

  // We're going to wait several times for various processes to exit, but we
  // want those timeouts to be running in parallel. That is, if we end up
  // waiting 5 seconds for the browser to stop, we should reduce the later
  // timeouts by that time.
  const base::TimeTicks timeout_start = base::TimeTicks::Now();

  if (!browser_->WaitForExit(browser_timeout)) {
    LOG(WARNING) << "Browser process did not exit "
                 << browser_timeout.InSeconds() << " seconds after SIGTERM.";
    WriteBrowserPidFile(shutdown_browser_pid_path_);
    browser_->AbortAndKillAll(browser_timeout);
  }
  if (code == SessionManagerService::SUCCESS) {
    // Only record shutdown time for normal exit.
    login_metrics_->SendBrowserShutdownTime(base::TimeTicks::Now() -
                                            browser_exit_start_time);
  }

  if (arc_manager_proxy_) {
    // In test, arc_manager_proxy_ is nullptr.
    arc_manager_proxy_->EnsureJobExit(
        std::max(int64_t{0}, (ArcManager::kContainerTimeout -
                              (base::TimeTicks::Now() - timeout_start))
                                 .InMilliseconds()));
  }
}

bool SessionManagerService::OnTerminationSignal(
    const struct signalfd_siginfo& info) {
  ScheduleShutdown();
  return true;
}

void SessionManagerService::VmConciergeOwnerChanged(
    const std::string& old_owner, const std::string& new_owner) {
  vm_concierge_available_ = !new_owner.empty();
}

void SessionManagerService::VmConciergeAvailable(bool is_available) {
  vm_concierge_available_ = is_available;
}

void SessionManagerService::MaybeStopAllVms() {
  if (!vm_concierge_available_) {
    // The vm_concierge D-Bus service is not running so there are no VMs to
    // stop.
    return;
  }

  // Stop all running VMs.  We do this asynchronously as we don't need to wait
  // for the VMs to exit before restarting chrome.
  dbus::MethodCall method_call(vm_tools::concierge::kVmConciergeInterface,
                               vm_tools::concierge::kStopAllVmsMethod);
  vm_concierge_dbus_proxy_->CallMethod(&method_call, kStopAllVmsTimeoutMs,
                                       base::DoNothing());
}

// TODO(crbug.com/390297821): Move to ArcManager.
void SessionManagerService::MaybeStopArcVm() {
  if (!vm_concierge_available_) {
    return;
  }

  vm_tools::concierge::StopVmRequest request;
  request.set_name(vm_tools::concierge::kArcVmName);

  dbus::MethodCall method_call(
      vm_tools::concierge::kVmConciergeInterface,
      vm_tools::concierge::kStopVmWithoutOwnerIdMethod);
  dbus::MessageWriter writer(&method_call);
  writer.AppendProtoAsArrayOfBytes(request);

  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> dbus_response(
      vm_concierge_dbus_proxy_->CallMethodAndBlock(&method_call,
                                                   kStopArcVmTimeoutMs));
  if (!dbus_response.has_value() || !dbus_response.value()) {
    LOG(ERROR) << "Failed to stop ARCVM: empty response";
    return;
  }

  dbus::MessageReader reader(dbus_response.value().get());
  vm_tools::concierge::SuccessFailureResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response";
    return;
  }
  if (!response.success()) {
    LOG(ERROR) << "Failed to stop ARCVM: " << response.failure_reason();
  }
}

void SessionManagerService::WriteBrowserPidFile(base::FilePath path) {
  // This is safe from symlink attacks because /run/chrome is guaranteed to be a
  // root-owned directory (/run is in the rootfs, /run/chrome is created by
  // session_manager as a directory).
  if (!brillo::DeleteFile(path)) {
    PLOG(ERROR) << "Failed to delete " << path.value();
    return;
  }

  // Note that we pass O_CREAT | O_EXCL to make this fail should the file
  // already exist. This avoids race conditions with malicious chronos processes
  // attempting to recreate e.g. a symlink at the path to redirect our write
  // elsewhere.
  base::ScopedFD browser_pid_fd(open(
      path.value().c_str(),
      O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0644));
  if (!browser_pid_fd.is_valid()) {
    PLOG(ERROR) << "Could not create " << path.value();
    return;
  }

  std::string pid_string = base::NumberToString(browser_->CurrentPid());
  if (!base::WriteFileDescriptor(browser_pid_fd.get(), pid_string)) {
    PLOG(ERROR) << "Failed to write " << path.value();
    return;
  }

  // Change the file to be owned by the user and group of the containing
  // directory. crash_reporter, which reads this file, is run by chrome using
  // the chronos user.
  struct stat sbuf;
  if (stat(path.DirName().value().c_str(), &sbuf) != 0) {
    PLOG(ERROR) << "Could not stat: " << path.DirName().value();
    return;
  }

  if (fchown(browser_pid_fd.get(), sbuf.st_uid, sbuf.st_gid) < 0) {
    PLOG(ERROR) << "Could not chown: " << path.value();
  }
}

void SessionManagerService::OnLongKillTimeoutEnabled(
    std::optional<bool> enabled) {
  if (!enabled.has_value()) {
    LOG(ERROR) << "Failed to check kSessionManagerLongKillTimeout feature.";
    use_long_kill_timeout_ = false;
    return;
  }

  use_long_kill_timeout_ = enabled.value();
}

void SessionManagerService::OnLivenessCheckEnabled(
    std::optional<bool> enabled) {
  if (!enabled.has_value()) {
    LOG(ERROR) << "Failed to check SessionManagerLivenessCheck feature.";
    return;
  }

  if (!enabled.value()) {
    LOG(WARNING) << "SessionManagerLivenessCheck disabled, we will NOT abort "
                    "on a browser hang detected by the liveness checker.";
    liveness_checker_->DisableAborting();
  }
}

}  // namespace login_manager
