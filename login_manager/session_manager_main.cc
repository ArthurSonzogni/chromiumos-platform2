// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <linux/limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/containers/extend.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/message_loop/message_pump_type.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <base/task/single_thread_task_executor.h>
#include <base/time/time.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/namespaces/mount_namespace.h>
#include <brillo/namespaces/platform.h>
#include <brillo/syslog_logging.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <chromeos/constants/cryptohome.h>
#include <libsegmentation/feature_management.h>
#include <rootdev/rootdev.h>

#include "login_manager/browser_job.h"
#include "login_manager/chrome_setup.h"
#include "login_manager/login_metrics.h"
#include "login_manager/scheduler_util.h"
#include "login_manager/session_manager_impl.h"
#include "login_manager/session_manager_service.h"
#include "login_manager/system_utils_impl.h"

using std::map;
using std::string;
using std::vector;

// Watches a Chrome binary and restarts it when it crashes. Also watches
// window manager binary as well. Actually supports watching several
// processes specified as command line arguments separated with --.
// Also listens over D-Bus for the commands specified in
// dbus_bindings/org.chromium.SessionManagerInterface.xml.

namespace switches {

// Name of the flag that contains the command for running Chrome.
static const char kChromeCommand[] = "chrome-command";
static const char kChromeCommandDefault[] = "/opt/google/chrome/chrome";

// Name of the flag that contains the path to the file which disables restart of
// managed jobs upon exit or crash if the file is present.
static const char kDisableChromeRestartFile[] = "disable-chrome-restart-file";
// The default path to this file.
static const char kDisableChromeRestartFileDefault[] =
    "/run/disable_chrome_restart";

// Flag that causes session manager to show the help message and exit.
static const char kHelp[] = "help";
// The help message shown if help flag is passed to the program.
static const char kHelpMessage[] =
    "\nAvailable Switches: \n"
    "  --chrome-command=</path/to/executable>\n"
    "    Path to the Chrome executable. Split along whitespace into arguments\n"
    "    (to which standard Chrome arguments will be appended); a value like\n"
    "    \"/usr/local/bin/strace /path/to/chrome\" may be used to wrap Chrome "
    "in\n"
    "    another program. (default: /opt/google/chrome/chrome)\n"
    "  --disable-chrome-restart-file=</path/to/file>\n"
    "    Magic file that causes this program to stop restarting the\n"
    "    chrome binary and exit. (default: /run/disable_chrome_restart)\n";
}  // namespace switches

using login_manager::BrowserJob;
using login_manager::BrowserJobInterface;
using login_manager::ConfigureNonUrgentCpuset;
using login_manager::LoginMetrics;
using login_manager::PerformChromeSetup;
using login_manager::SessionManagerService;
using login_manager::SystemUtilsImpl;

namespace {
// Directory in which per-boot metrics flag files will be stored.
constexpr char kFlagFileDir[] = "/run/session_manager";

// Hang-detection magic file and constants.
constexpr char kHangDetectionFlagFile[] = "enable_hang_detection";
constexpr base::TimeDelta kHangDetectionInterval = base::Seconds(60);
constexpr int kHangDetectionRetriesDev = 9;
// TODO(b/324017835): Enable the retry mechanism on stable, after it sits for
// a bit on beta/dev.
constexpr int kHangDetectionRetriesStable = 0;
constexpr base::TimeDelta kHangDetectionIntervalTest = base::Seconds(5);
constexpr int kHangDetectionRetriesTest = 0;

// Time to wait for children to exit gracefully before killing them
// with a SIGABRT.
constexpr base::TimeDelta kKillTimeout = base::Seconds(3);

constexpr char kChromeOSReleaseTrack[] = "CHROMEOS_RELEASE_TRACK";
constexpr char kStableChannel[] = "stable-channel";

}  // namespace

int main(int argc, char* argv[]) {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader);

  // Allow waiting for all descendants, not just immediate children.
  if (::prctl(PR_SET_CHILD_SUBREAPER, 1)) {
    PLOG(ERROR) << "Couldn't set child subreaper";
  }

  if (cl->HasSwitch(switches::kHelp)) {
    LOG(INFO) << switches::kHelpMessage;
    return 0;
  }

  // Parse the base Chrome command.
  // TODO(hidehiko): move to ChromeSetup.
  string command_flag(switches::kChromeCommandDefault);
  if (cl->HasSwitch(switches::kChromeCommand)) {
    command_flag = cl->GetSwitchValueASCII(switches::kChromeCommand);
  }
  vector<string> command =
      base::SplitString(command_flag, base::kWhitespaceASCII,
                        base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  brillo::CrosConfig cros_config;

  // Detect small cores and restrict non-urgent tasks to small cores.
  ConfigureNonUrgentCpuset(&cros_config);

  // Set things up for running Chrome.
  // TODO(hidehiko): move FeatureManagement to ChromeSetup.
  segmentation::FeatureManagement feature_management;
  std::optional<login_manager::ChromeSetup::Result> chrome_setup =
      login_manager::ChromeSetup(cros_config, feature_management).Run();
  CHECK(chrome_setup.has_value());
  base::Extend(command, std::move(chrome_setup->args));

  // Shim that wraps system calls, file system ops, etc.
  SystemUtilsImpl system_utils;

  // Checks magic file that causes the session_manager to stop managing the
  // browser process. Devs and tests can use this to keep the session_manager
  // running while stopping and starting the browser manually.
  string magic_chrome_file =
      cl->GetSwitchValueASCII(switches::kDisableChromeRestartFile);
  if (magic_chrome_file.empty()) {
    magic_chrome_file.assign(switches::kDisableChromeRestartFileDefault);
  }

  // Used to report various metrics around user type (guest vs non), dev-mode,
  // and policy/key file status.
  base::FilePath flag_file_dir(kFlagFileDir);
  if (!base::CreateDirectory(flag_file_dir)) {
    PLOG(FATAL) << "Cannot create flag file directory at " << kFlagFileDir;
  }
  LoginMetrics metrics(&system_utils);

  // The session_manager supports pinging the browser periodically to check that
  // it is still alive. On developer systems, this would be a problem, as
  // debugging the browser would cause it to be aborted. desktopui_HangDetector
  // autotest uses the flag file to indicate that an abort is expected. We
  // tolerate shorter intervals for all non-stable channels.
  const bool hang_detection_file_exists =
      base::PathExists(flag_file_dir.Append(kHangDetectionFlagFile));
  const bool enable_hang_detection =
      !chrome_setup->is_developer_end_user || hang_detection_file_exists;

  base::TimeDelta hang_detection_interval = kHangDetectionInterval;
  int hang_detection_retires = kHangDetectionRetriesStable;
  std::string channel_string;
  if (base::SysInfo::GetLsbReleaseValue(kChromeOSReleaseTrack,
                                        &channel_string) &&
      channel_string != kStableChannel) {
    hang_detection_retires = kHangDetectionRetriesDev;
  }
  if (hang_detection_file_exists) {
    hang_detection_interval = kHangDetectionIntervalTest;
    hang_detection_retires = kHangDetectionRetriesTest;
  }

  // Job configuration.
  BrowserJob::Config config;
  std::optional<base::FilePath> ns_path;
  // TODO(crbug.com/188605, crbug.com/216789): Extend user session isolation and
  // make it stricter.
  // Back when the above bugs were filed, the interaction between
  // session_manager and Chrome was a lot simpler: Chrome would display the
  // login screen, the user would log in, and then session_manager would
  // relaunch Chrome after cryptohomed had mounted the user's encrypted home
  // directory.
  // Nowadays, big features like ARC and Crostini have added a lot of complexity
  // to the runtime environment of a logged-in Chrome OS user: there are nested
  // namespaces, bind mounts between them, and complex propagation of mount
  // points. Blindly putting the user session (i.e. the Chrome browser process
  // tree) in a bunch of namespaces is bound to subtly break things.
  // Start shaving this yak by isolating Guest mode sessions, which don't
  // support many of the above features. Put Guest mode process trees in a
  // non-root mount namespace to test the waters.
  // Extending the feature for regular user sessions is developed behind
  // the USE flag 'user_session_isolation'. If the flag is set Chrome will
  // be launched in a non-root mount namespace for regular sessions as well.
  config.isolate_guest_session = true;
  config.isolate_regular_session = login_manager::IsolateUserSession();

  if (config.isolate_guest_session || config.isolate_regular_session) {
    // Instead of having Chrome unshare a new mount namespace on launch, have
    // Chrome enter the mount namespace where the user data directory exists.
    ns_path = base::FilePath(cryptohome::kUserSessionMountNamespacePath);
  }

  brillo::Platform platform;
  std::unique_ptr<brillo::MountNamespace> chrome_mnt_ns;
  if (ns_path.has_value()) {
    // Create the mount namespace here before Chrome launches.
    // If the current session is not a Guest session browser_job and
    // session_manager_impl check the user_session_isolation USE flag before
    // entering the namespace.
    chrome_mnt_ns =
        std::make_unique<brillo::MountNamespace>(ns_path.value(), &platform);
    if (chrome_mnt_ns->Create()) {
      // User session shouldn't fail if namespace creation fails.
      // browser_job enters the mount namespace if |config.chrome_mount_ns_path|
      // has a value. Populate this value only if the namespace creation
      // succeeds.
      config.chrome_mount_ns_path = ns_path;
      LOG(INFO) << "Mount namespace created at " << ns_path.value();
    } else {
      // session_manager enters the mount namespace if |ns_path| has a value.
      // Reset this value if the namespace creation fails.
      // If flags are set for user session or Guest session isolation cryptohome
      // will first check the namespace existence and fail only if cannot enter
      // the existing namespace.
      // If namespace creation fails here cryptohome will continue in the root
      // mount namespace.
      ns_path.reset();
      LOG(WARNING) << "Failed to create mount namespace at " << ns_path.value();
    }
  }

  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  brillo::BaseMessageLoop brillo_loop(task_executor.task_runner());
  brillo_loop.SetAsCurrent();

  scoped_refptr<SessionManagerService> manager = new SessionManagerService(
      base::BindOnce(
          [](const std::vector<std::string>& command,
             const std::vector<std::string>& environment, LoginMetrics* metrics,
             login_manager::SystemUtils* system_utils,
             const BrowserJob::Config& config, uid_t uid,
             brillo::ProcessReaper& process_reaper)
              -> std::unique_ptr<BrowserJobInterface> {
            // This job encapsulates the command specified on the command line,
            // and the runtime options for it.
            return std::make_unique<BrowserJob>(
                command, environment, process_reaper, metrics, system_utils,
                config,
                std::make_unique<login_manager::Subprocess>(uid, system_utils));
          },
          command, chrome_setup->env, &metrics, &system_utils, config,
          chrome_setup->uid),
      base::FilePath(magic_chrome_file), ns_path, kKillTimeout,
      enable_hang_detection, hang_detection_interval, hang_detection_retires,
      &metrics, &system_utils);

  if (manager->Initialize()) {
    // Returns when brillo_loop.BreakLoop() is called.
    brillo_loop.Run();
  }
  manager->Finalize();

  LOG_IF(WARNING, manager->exit_code() != SessionManagerService::SUCCESS)
      << "session_manager exiting with code " << manager->exit_code();
  return manager->exit_code();
}
