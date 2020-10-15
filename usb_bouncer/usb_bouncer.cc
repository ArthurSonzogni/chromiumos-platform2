// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mount.h>

#include <cstdio>
#include <cstdlib>

#include <base/command_line.h>
#include <brillo/syslog_logging.h>
#include <brillo/flag_helper.h>
#include <libminijail.h>
#include <scoped_minijail.h>

#include "usb_bouncer/entry_manager.h"
#include "usb_bouncer/util.h"

using usb_bouncer::Daemonize;
using usb_bouncer::DevpathToRuleCallback;
using usb_bouncer::EntryManager;
using usb_bouncer::GetRuleFromDevPath;
using usb_bouncer::kDBusPath;

namespace {

static constexpr char kUsageMessage[] = R"(Usage:
  cleanup - removes stale allow-list entries.
  genrules - writes the generated rules configuration and to stdout.
  udev (add|remove) <devpath> - handles a udev device event.
  userlogin - add current entries to user allow-list.
)";

enum class SeccompEnforcement {
  kEnabled,
  kDisabled,
};

enum class ForkConfig {
  kDoubleFork,
  kDisabled,
};

class Configuration {
 public:
  const SeccompEnforcement seccomp;
  const ForkConfig fork_config;
};

static constexpr char kLogPath[] = "/dev/log";
static constexpr char kUMAEventsPath[] = "/var/lib/metrics/uma-events";

void DropPrivileges(const Configuration& config) {
  ScopedMinijail j(minijail_new());
  minijail_change_user(j.get(), usb_bouncer::kUsbBouncerUser);
  minijail_change_group(j.get(), usb_bouncer::kUsbBouncerGroup);
  minijail_inherit_usergroups(j.get());
  minijail_no_new_privs(j.get());
  if (config.seccomp == SeccompEnforcement::kEnabled) {
    minijail_use_seccomp_filter(j.get());
    minijail_parse_seccomp_filters(
        j.get(), "/usr/share/policy/usb_bouncer-seccomp.policy");
  }

  minijail_namespace_ipc(j.get());
  minijail_namespace_net(j.get());
  // If minijail were to run as init, then it would be tracked by udev and
  // defeat the purpose of daemonizing. If minijail doesn't run as init, the
  // descendant processes will die when daemonizing because there won't be an
  // init to keep the pid namespace from closing.
  if (config.fork_config == ForkConfig::kDisabled) {
    minijail_namespace_pids(j.get());
  }
  minijail_namespace_uts(j.get());
  minijail_namespace_vfs(j.get());
  if (minijail_enter_pivot_root(j.get(), "/mnt/empty") != 0) {
    PLOG(FATAL) << "minijail_enter_pivot_root() failed";
  }
  for (const char* path : {"/", "/proc", "/sys"}) {
    if (minijail_bind(j.get(), path, path, 0 /*writable*/)) {
      PLOG(FATAL) << "minijail_bind('" << path << "') failed";
    }
  }
  if (!base::PathExists(base::FilePath(kLogPath))) {
    LOG(WARNING) << "Path '" << kLogPath << "' doesn't exist; "
                 << "logging via syslog won't work for this run.";
  } else if (minijail_bind(j.get(), kLogPath, kLogPath, 0 /*writable*/)) {
    PLOG(FATAL) << "minijail_bind('" << kLogPath << "') failed";
  }

  // "usb_bouncer genrules" writes to stdout.
  minijail_preserve_fd(j.get(), STDOUT_FILENO, STDOUT_FILENO);

  minijail_mount_dev(j.get());
  minijail_mount_tmp(j.get());
  for (const char* path : {"/run", "/var"}) {
    if (minijail_mount_with_data(j.get(), "tmpfs", path, "tmpfs",
                                 MS_NOSUID | MS_NOEXEC | MS_NODEV,
                                 "mode=0755,size=10M") != 0) {
      PLOG(FATAL) << "minijail_mount_with_data('" << path << "') failed";
    }
  }
  std::string global_db_path("/");
  global_db_path.append(usb_bouncer::kDefaultGlobalDir);
  if (minijail_bind(j.get(), global_db_path.c_str(), global_db_path.c_str(),
                    1 /*writable*/) != 0) {
    PLOG(FATAL) << "minijail_bind('" << global_db_path << "') failed";
  }

  if (!base::PathExists(base::FilePath(usb_bouncer::kDBusPath))) {
    LOG(WARNING) << "Path '" << usb_bouncer::kDBusPath << "' doesn't exist; "
                 << "assuming user is not yet logged in to the system.";
  } else if (minijail_bind(j.get(), usb_bouncer::kDBusPath,
                           usb_bouncer::kDBusPath, 0 /*writable*/) != 0) {
    PLOG(FATAL) << "minijail_bind('" << usb_bouncer::kDBusPath << "') failed";
  }
  if (base::PathExists(base::FilePath(kUMAEventsPath)) &&
      minijail_bind(j.get(), kUMAEventsPath, kUMAEventsPath, 1 /*writable*/) !=
          0) {
    PLOG(FATAL) << "minijail_bind('" << kUMAEventsPath << "') failed";
  }

  minijail_remount_mode(j.get(), MS_SLAVE);
  // minijail_bind was not used because the MS_REC flag is needed.
  if (!base::DirectoryExists(base::FilePath(usb_bouncer::kUserDbBaseDir))) {
    LOG(WARNING) << "Path '" << usb_bouncer::kUserDbBaseDir
                 << "' doesn't exist; userdb will be inaccessible this run.";
  } else if (minijail_mount(j.get(), usb_bouncer::kUserDbBaseDir,
                            usb_bouncer::kUserDbBaseDir, "none",
                            MS_BIND | MS_REC) != 0) {
    PLOG(FATAL) << "minijail_mount('/" << usb_bouncer::kUserDbBaseDir
                << "') failed";
  }

  minijail_forward_signals(j.get());
  pid_t pid = minijail_fork(j.get());
  if (pid != 0) {
    exit(minijail_wait(j.get()));
  }
  umask(0077);
}

EntryManager* GetEntryManagerOrDie(const Configuration& config) {
  if (!EntryManager::CreateDefaultGlobalDB()) {
    LOG(FATAL) << "Unable to create default global DB!";
  }
  DropPrivileges(config);
  EntryManager* entry_manager = EntryManager::GetInstance(GetRuleFromDevPath);
  if (!entry_manager) {
    LOG(FATAL) << "EntryManager::GetInstance() failed!";
  }
  return entry_manager;
}

int HandleAuthorizeAll(const Configuration& config,
                       const std::vector<std::string>& argv) {
  if (!argv.empty()) {
    LOG(ERROR) << "Invalid options!";
    return EXIT_FAILURE;
  }

  if (!usb_bouncer::AuthorizeAll()) {
    LOG(FATAL) << "authorize-all failed!";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int HandleCleanup(const Configuration& config,
                  const std::vector<std::string>& argv) {
  if (!argv.empty()) {
    LOG(ERROR) << "Invalid options!";
    return EXIT_FAILURE;
  }

  EntryManager* entry_manager = GetEntryManagerOrDie(config);
  if (!entry_manager->GarbageCollect()) {
    LOG(ERROR) << "cleanup failed!";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int HandleGenRules(const Configuration& config,
                   const std::vector<std::string>& argv) {
  if (!argv.empty()) {
    LOG(ERROR) << "Invalid options!";
    return EXIT_FAILURE;
  }

  EntryManager* entry_manager = GetEntryManagerOrDie(config);
  std::string rules = entry_manager->GenerateRules();
  if (rules.empty()) {
    LOG(ERROR) << "genrules failed!";
    return EXIT_FAILURE;
  }

  printf("%s", rules.c_str());
  return EXIT_SUCCESS;
}

int HandleUdev(const Configuration& config,
               const std::vector<std::string>& argv) {
  if (argv.size() != 2) {
    LOG(ERROR) << "Invalid options!";
    return EXIT_FAILURE;
  }

  EntryManager::UdevAction action;
  if (argv[0] == "add") {
    action = EntryManager::UdevAction::kAdd;
  } else if (argv[0] == "remove") {
    action = EntryManager::UdevAction::kRemove;
  } else {
    LOG(ERROR) << "Invalid options!";
    return EXIT_FAILURE;
  }

  // We need to drop privileges prior to reading from sysfs so instead of
  // calling GetEntryManagerOrDie split it up so the privileges can be dropped
  // earlier.
  if (!EntryManager::CreateDefaultGlobalDB()) {
    LOG(FATAL) << "Unable to create default global DB!";
  }
  DropPrivileges(config);

  // Perform sysfs reads before daemonizing to avoid races.
  const std::string& devpath = argv[1];
  std::string rule;
  if (action == EntryManager::UdevAction::kAdd) {
    rule = GetRuleFromDevPath(devpath);
    if (rule.empty()) {
      LOG(ERROR) << "Unable convert devpath to USBGuard allow-list rule.";
      exit(0);
    }
  }

  // All the information needed from udev and sysfs should be obtained prior to
  // this point. Daemonizing here allows usb_bouncer to wait on other system
  // services without blocking udev.
  if (config.fork_config == ForkConfig::kDoubleFork) {
    Daemonize();
  }

  // The DevpathToRuleCallback here to forwards the result of the sysfs read
  // performed before daemonizing.
  EntryManager* entry_manager = EntryManager::GetInstance(
      [rule, devpath](const std::string& devpath_) -> const std::string {
        if (devpath != devpath_) {
          LOG(ERROR) << "Got devpath: '" << devpath_ << "' expected '"
                     << devpath;
          return "";
        }
        return rule;
      });
  if (!entry_manager) {
    LOG(FATAL) << "EntryManager::GetInstance() failed!";
  }
  if (!entry_manager->HandleUdev(action, devpath)) {
    LOG(ERROR) << "udev failed!";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int HandleUserLogin(const Configuration& config,
                    const std::vector<std::string>& argv) {
  if (!argv.empty()) {
    LOG(ERROR) << "Invalid options!";
    return EXIT_FAILURE;
  }

  EntryManager* entry_manager = GetEntryManagerOrDie(config);
  if (!entry_manager->HandleUserLogin()) {
    LOG(ERROR) << "userlogin failed!";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
  DEFINE_bool(
      seccomp, true,
      DCHECK_IS_ON()
          ? "Enable or disable seccomp filtering."
          : "Flag is ignored in production, but reported as a crash if false.");
  DEFINE_bool(fork, false,
              "Daemonizes udev commands with a double fork if enabled.");
  brillo::FlagHelper::Init(argc, argv, kUsageMessage);
  base::CommandLine::Init(argc, argv);

  // Logging may not be ready at early boot in which case it is ok if the logs
  // are lost.
  int log_flags = brillo::kLogToStderr;
  if (base::PathExists(base::FilePath(kLogPath))) {
    log_flags |= brillo::kLogToSyslog;
  }
  brillo::InitLog(log_flags);

  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  std::vector<std::string> args = cl->argv();

  // Remove switches.
  for (int x = 1; x < args.size();) {
    if (args[x].size() >= 2 && args[x].substr(0, 2) == "--") {
      args.erase(args.begin() + x);
      if (args[x].size() == 2) {
        break;
      }
    } else {
      ++x;
    }
  }

  if (args.size() < 2) {
    LOG(ERROR) << "Invalid options!";
    return EXIT_FAILURE;
  }

  const auto& command = args[1];
  auto command_args_start = args.begin() + 2;
  SeccompEnforcement seccomp;
  if (!FLAGS_seccomp) {
    if (DCHECK_IS_ON()) {
      seccomp = SeccompEnforcement::kDisabled;
    } else {
      // Spin off a child to log a crash if --secomp=false is set in production.
      pid_t pid = fork();
      if (pid < 0) {
        PLOG(FATAL) << "Failed to fork()";
      }
      if (pid == 0) {
        LOG(FATAL) << "--seccomp=false set for production code.";
      }

      seccomp = SeccompEnforcement::kEnabled;
    }
  } else {
    seccomp = SeccompEnforcement::kEnabled;
  }
  ForkConfig fork_config =
      FLAGS_fork ? ForkConfig::kDoubleFork : ForkConfig::kDisabled;

  const struct {
    const std::string command;
    int (*handler)(const Configuration& config,
                   const std::vector<std::string>& argv);
  } command_handlers[] = {
      // clang-format off
      {"authorize-all", HandleAuthorizeAll},
      {"cleanup", HandleCleanup},
      {"genrules", HandleGenRules},
      {"udev", HandleUdev},
      {"userlogin", HandleUserLogin},
      // clang-format on
  };

  for (const auto& command_handler : command_handlers) {
    if (command_handler.command == command) {
      return command_handler.handler(
          {seccomp, fork_config},
          std::vector<std::string>(command_args_start, args.end()));
    }
  }

  if (command != "help") {
    LOG(ERROR) << "Invalid options!";
  }
  return EXIT_FAILURE;
}
