// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/ui/chromium_command_builder.h"

#include <sys/resource.h>

#include <algorithm>
#include <cstdarg>
#include <ctime>

#include <base/command_line.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/userdb_utils.h>

#include "chromeos/ui/util.h"

namespace chromeos {
namespace ui {

namespace {

// Location where GPU debug information is bind-mounted.
const char kDebugfsGpuPath[] = "/run/debugfs_gpu";

// Name of the release track field.
constexpr char kChromeosReleaseTrack[] = "CHROMEOS_RELEASE_TRACK";

// Prefix for test builds.
constexpr char kTestPrefix[] = "test";

// Returns the value associated with |key| in |pairs| or an empty string if the
// key isn't present. If the value is encapsulated in single or double quotes,
// they are removed.
std::string LookUpInStringPairs(const base::StringPairs& pairs,
                                const std::string& key) {
  for (size_t i = 0; i < pairs.size(); ++i) {
    if (key != pairs[i].first)
      continue;

    // Strip quotes.
    // TODO(derat): Remove quotes from Pepper .info files after
    // session_manager_setup.sh is no longer interpreting them as shell scripts.
    std::string value = pairs[i].second;
    if (value.size() >= 2U &&
        ((value[0] == '"' && value[value.size()-1] == '"') ||
         (value[0] == '\'' && value[value.size()-1] == '\'')))
      value = value.substr(1, value.size() - 2);

    return value;
  }
  return std::string();
}

// Returns true if |name| matches /^[A-Z][_A-Z0-9]+$/.
bool IsEnvironmentVariableName(const std::string& name) {
  if (name.empty() || !(name[0] >= 'A' && name[0] <= 'Z'))
    return false;
  for (size_t i = 1; i < name.size(); ++i) {
    char ch = name[i];
    if (ch != '_' && !(ch >= '0' && ch <= '9') && !(ch >= 'A' && ch <= 'Z'))
      return false;
  }
  return true;
}

// Updates |argument_index_to_update|, a arguments's position in |arguments_|,
// in response to the argument at position |deleted_argument_index| being
// removed. If the index-to-update is beyond the deleted index, it'll be
// decremented; if it is itself being deleted, it'll be set to -1.
void UpdateArgumentIndexForDeletion(int* argument_index_to_update,
                                    int deleted_argument_index) {
  DCHECK(argument_index_to_update);
  if (*argument_index_to_update > deleted_argument_index)
    (*argument_index_to_update)--;
  else if (*argument_index_to_update == deleted_argument_index)
    *argument_index_to_update = -1;
}

// Returns true if |lsb_data| has a field called "CHROMEOS_RELEASE_TRACK",
// and its value starts with "test".
bool IsTestBuild(const std::string& lsb_data) {
  for (const auto& field : base::SplitStringPiece(
           lsb_data, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    std::vector<base::StringPiece> tokens = base::SplitStringPiece(
        field, "=", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (tokens.size() == 2 && tokens[0] == kChromeosReleaseTrack)
      return tokens[1].starts_with(kTestPrefix);
  }
  return false;
}

// Returns true if |str| has prefix in |prefixes|.
bool HasPrefix(const std::string& str, const std::set<std::string>& prefixes) {
  for (const auto& prefix : prefixes) {
    if (base::StartsWith(str, prefix, base::CompareCase::SENSITIVE))
      return true;
  }
  return false;
}

}  // namespace

const char ChromiumCommandBuilder::kUser[] = "chronos";
const char ChromiumCommandBuilder::kUseFlagsPath[] = "/etc/ui_use_flags.txt";
const char ChromiumCommandBuilder::kLsbReleasePath[] = "/etc/lsb-release";
const char ChromiumCommandBuilder::kTimeZonePath[] =
    "/var/lib/timezone/localtime";
const char ChromiumCommandBuilder::kDefaultZoneinfoPath[] =
    "/usr/share/zoneinfo/US/Pacific";
const char ChromiumCommandBuilder::kPepperPluginsPath[] =
    "/opt/google/chrome/pepper";

ChromiumCommandBuilder::ChromiumCommandBuilder()
    : uid_(0),
      gid_(0),
      is_chrome_os_hardware_(false),
      is_developer_end_user_(false),
      is_test_build_(false),
      vmodule_argument_index_(-1),
      enable_features_argument_index_(-1) {
}

ChromiumCommandBuilder::~ChromiumCommandBuilder() {}

bool ChromiumCommandBuilder::Init() {
  if (!brillo::userdb::GetUserInfo(kUser, &uid_, &gid_))
    return false;

  // Read the list of USE flags that were set at build time.
  std::string data;
  if (!base::ReadFileToString(GetPath(kUseFlagsPath), &data)) {
    PLOG(ERROR) << "Unable to read " << kUseFlagsPath;
    return false;
  }
  std::vector<std::string> lines =
      base::SplitString(data, "\n", base::KEEP_WHITESPACE,
                        base::SPLIT_WANT_ALL);
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!lines[i].empty() && lines[i][0] != '#')
      use_flags_.insert(lines[i]);
  }

  base::CommandLine cl(base::FilePath("crossystem"));
  cl.AppendArg("mainfw_type");
  std::string output;
  if (base::GetAppOutput(cl, &output)) {
    base::TrimWhitespaceASCII(output, base::TRIM_TRAILING, &output);
    is_chrome_os_hardware_ = (output != "nonchrome");
  }

  is_developer_end_user_ = base::GetAppOutput(
      base::CommandLine(base::FilePath("is_developer_end_user")), &output);

  // Provide /etc/lsb-release contents and timestamp so that they are available
  // to Chrome immediately without requiring a blocking file read.
  const base::FilePath lsb_path(GetPath(kLsbReleasePath));
  base::File::Info info;
  if (!base::ReadFileToString(lsb_path, &lsb_data_) ||
      !base::GetFileInfo(lsb_path, &info)) {
    LOG(ERROR) << "Unable to read or stat " << kLsbReleasePath;
    return false;
  }
  lsb_release_time_ = info.creation_time;
  is_test_build_ = IsTestBuild(lsb_data_);
  return true;
}

bool ChromiumCommandBuilder::SetUpChromium() {
  AddEnvVar("USER", kUser);
  AddEnvVar("LOGNAME", kUser);
  AddEnvVar("SHELL", "/bin/sh");
  AddEnvVar("PATH", "/bin:/usr/bin");
  AddEnvVar("LC_ALL", "en_US.utf8");
  AddEnvVar("XDG_RUNTIME_DIR", "/run/chrome");

  const base::FilePath data_dir(GetPath("/home").Append(kUser));
  AddEnvVar("DATA_DIR", data_dir.value());
  if (!util::EnsureDirectoryExists(data_dir, uid_, gid_, 0755))
    return false;

  AddEnvVar("LSB_RELEASE", lsb_data_);
  AddEnvVar("LSB_RELEASE_TIME", base::IntToString(lsb_release_time_.ToTimeT()));

  // By default, libdbus treats all warnings as fatal errors. That's too strict.
  AddEnvVar("DBUS_FATAL_WARNINGS", "0");

  // Prevent Flash asserts from crashing the plugin process.
  AddEnvVar("DONT_CRASH_ON_ASSERT", "1");

  // Create the target for the /etc/localtime symlink. This allows the Chromium
  // process to change the time zone.
  const base::FilePath time_zone_symlink(GetPath(kTimeZonePath));
  // TODO(derat): Move this back into the !base::PathExists() block in M39 or
  // later, after http://crbug.com/390188 has had time to be cleaned up.
  CHECK(util::EnsureDirectoryExists(
      time_zone_symlink.DirName(), uid_, gid_, 0755));
  if (!base::PathExists(time_zone_symlink)) {
    // base::PathExists() dereferences symlinks, so make sure that there's not a
    // dangling symlink there before we create a new link.
    base::DeleteFile(time_zone_symlink, false);
    PCHECK(base::CreateSymbolicLink(
        base::FilePath(kDefaultZoneinfoPath), time_zone_symlink));
  }

  // Increase soft limit of file descriptors to 2048 (default is 1024).
  // Increase hard limit of file descriptors to 16384 (default is 4096).
  // Some offline websites using IndexedDB are particularly hungry for
  // descriptors, so the default is insufficient. See crbug.com/251385.
  // Native GPU memory buffer requires a FD per texture. See crbug.com/629521.
  struct rlimit limit;
  limit.rlim_cur = 2048;
  limit.rlim_max = 16384;
  if (setrlimit(RLIMIT_NOFILE, &limit) < 0)
    PLOG(ERROR) << "Setting max FDs with setrlimit() failed";

  // Disable sandboxing as it causes crashes in ASAN: crbug.com/127536
  bool disable_sandbox = false;
  disable_sandbox |= SetUpASAN();
  if (disable_sandbox)
    AddArg("--no-sandbox");

  SetUpPepperPlugins();
  AddUiFlags();

  if (UseFlagIsSet("pointer_events"))
    AddFeatureEnableOverride("PointerEvent");

  if (UseFlagIsSet("passive_event_listeners"))
    AddArg("--passive-listeners-default=true");

  AddArg("--enable-logging");
  AddArg("--log-level=1");
  AddArg("--use-cras");
  AddArg("--enable-wayland-server");

  return true;
}

void ChromiumCommandBuilder::EnableCoreDumps() {
  if (!util::EnsureDirectoryExists(
          base::FilePath("/var/coredumps"), uid_, gid_, 0700))
    return;

  struct rlimit limit = { RLIM_INFINITY, RLIM_INFINITY };
  if (setrlimit(RLIMIT_CORE, &limit) != 0)
    PLOG(ERROR) << "Setting unlimited coredumps with setrlimit() failed";
  const std::string kPattern("/var/coredumps/core.%e.%p");
  base::WriteFile(base::FilePath("/proc/sys/kernel/core_pattern"),
                  kPattern.c_str(), kPattern.size());
}

bool ChromiumCommandBuilder::ApplyUserConfig(const base::FilePath& path,
    const std::set<std::string>& disallowed_prefixes) {
  std::string data;
  if (!base::ReadFileToString(path, &data)) {
    PLOG(WARNING) << "Unable to read " << path.value();
    return false;
  }

  std::vector<std::string> lines =
      base::SplitString(data, "\n", base::KEEP_WHITESPACE,
                        base::SPLIT_WANT_ALL);

  for (size_t i = 0; i < lines.size(); ++i) {
    std::string line;
    base::TrimWhitespaceASCII(lines[i], base::TRIM_ALL, &line);
    if (line.empty() || line[0] == '#')
      continue;

    if (line[0] == '!' && line.size() > 1) {
      const std::string pattern = line.substr(1, line.size() - 1);
      size_t num_copied = 0;
      for (size_t src_index = 0; src_index < arguments_.size(); ++src_index) {
        if (arguments_[src_index].find(pattern) == 0) {
          // Drop the argument by not copying it and shift saved indexes if
          // needed.
          UpdateArgumentIndexForDeletion(&vmodule_argument_index_,
                                         static_cast<int>(src_index));
          UpdateArgumentIndexForDeletion(&enable_features_argument_index_,
                                         static_cast<int>(src_index));
        } else {
          arguments_[num_copied] = arguments_[src_index];
          num_copied++;
        }
      }
      arguments_.resize(num_copied);
    } else {
      base::StringPairs pairs;
      base::SplitStringIntoKeyValuePairs(line, '=', '\n', &pairs);
      if (pairs.size() == 1U && pairs[0].first == "vmodule")
        AddVmodulePattern(pairs[0].second);
      else if (pairs.size() == 1U && pairs[0].first == "enable-features")
        AddFeatureEnableOverride(pairs[0].second);
      else if (pairs.size() == 1U && IsEnvironmentVariableName(pairs[0].first))
        AddEnvVar(pairs[0].first, pairs[0].second);
      else if (!HasPrefix(line, disallowed_prefixes))
        AddArg(line);
    }
  }

  return true;
}

bool ChromiumCommandBuilder::UseFlagIsSet(const std::string& flag) const {
  return use_flags_.count(flag) > 0;
}

void ChromiumCommandBuilder::AddEnvVar(const std::string& name,
                                       const std::string& value) {
  environment_variables_[name] = value;
}

std::string ChromiumCommandBuilder::ReadEnvVar(const std::string& name) const {
  StringMap::const_iterator it = environment_variables_.find(name);
  CHECK(it != environment_variables_.end()) << name << " hasn't been set";
  return it->second;
}

void ChromiumCommandBuilder::AddArg(const std::string& arg) {
  arguments_.push_back(arg);
}

void ChromiumCommandBuilder::AddVmodulePattern(const std::string& pattern) {
  // Chrome's code for handling --vmodule applies the first matching pattern.
  // Prepend patterns here so that more-specific later patterns will override
  // more-general earlier ones.
  AddListFlagEntry(
      &vmodule_argument_index_, "--vmodule=", ",", pattern, true /* prepend */);
}

void ChromiumCommandBuilder::AddFeatureEnableOverride(
    const std::string& feature_name) {
  AddListFlagEntry(&enable_features_argument_index_,
                   "--enable-features=",
                   ",",
                   feature_name,
                   false /* prepend */);
}

base::FilePath ChromiumCommandBuilder::GetPath(const std::string& path) const {
  return util::GetReparentedPath(path, base_path_for_testing_);
}

void ChromiumCommandBuilder::AddListFlagEntry(
    int* flag_argument_index,
    const std::string& flag_prefix,
    const std::string& entry_separator,
    const std::string& new_entry,
    bool prepend) {
  DCHECK(flag_argument_index);
  if (new_entry.empty())
    return;

  if (*flag_argument_index < 0) {
    AddArg(flag_prefix + new_entry);
    *flag_argument_index = arguments_.size() - 1;
  } else if (prepend) {
      const std::string old = arguments_[*flag_argument_index];
      DCHECK_EQ(old.substr(0, flag_prefix.size()), flag_prefix);
      arguments_[*flag_argument_index] =
          flag_prefix + new_entry + entry_separator +
          old.substr(flag_prefix.size(), old.size() - flag_prefix.size());
  } else {
    arguments_[*flag_argument_index] += entry_separator + new_entry;
  }
}

bool ChromiumCommandBuilder::SetUpASAN() {
  if (!UseFlagIsSet("asan"))
    return false;

  // Make glib use system malloc.
  AddEnvVar("G_SLICE", "always-malloc");

  // Make nss use system malloc.
  AddEnvVar("NSS_DISABLE_ARENA_FREE_LIST", "1");

  // Make nss skip dlclosing dynamically loaded modules, which would result in
  // "obj:*" in backtraces.
  AddEnvVar("NSS_DISABLE_UNLOAD", "1");

  // Make ASAN output to the file because Chrome stderr is /dev/null now
  // (crbug.com/156308).
  // TODO(derat): It's weird that this lives in a Chrome directory that's
  // created by ChromeInitializer; move it somewhere else, maybe.
  AddEnvVar("ASAN_OPTIONS",
            "log_path=/var/log/chrome/asan_log:detect_odr_violation=0");

  return true;
}

void ChromiumCommandBuilder::SetUpPepperPlugins() {
  std::vector<std::string> register_plugins;

  base::FileEnumerator enumerator(GetPath(kPepperPluginsPath),
      false /* recursive */, base::FileEnumerator::FILES);
  while (true) {
    const base::FilePath path = enumerator.Next();
    if (path.empty())
      break;

    if (path.Extension() != ".info")
      continue;

    std::string data;
    if (!base::ReadFileToString(path, &data)) {
      PLOG(ERROR) << "Unable to read " << path.value();
      continue;
    }

    // .info files are full of shell junk like #-prefixed comments, so don't
    // check that SplitStringIntoKeyValuePairs() successfully parses every line.
    base::StringPairs pairs;
    base::SplitStringIntoKeyValuePairs(data, '=', '\n', &pairs);

    const std::string file_name = LookUpInStringPairs(pairs, "FILE_NAME");
    const std::string plugin_name = LookUpInStringPairs(pairs, "PLUGIN_NAME");
    const std::string version = LookUpInStringPairs(pairs, "VERSION");

    if (file_name.empty()) {
      LOG(ERROR) << "Missing FILE_NAME in " << path.value();
      continue;
    }

    if (plugin_name == "Shockwave Flash") {
      AddArg("--ppapi-flash-path=" + file_name);
      AddArg("--ppapi-flash-version=" + version);
      std::vector<std::string> flash_args;
      if (UseFlagIsSet("disable_flash_hw_video_decode")) {
        flash_args.push_back("enable_hw_video_decode=0");
        flash_args.push_back("enable_hw_video_decode_ave=0");
      }
      if (UseFlagIsSet("disable_low_latency_audio"))
        flash_args.push_back("enable_low_latency_audio=0");
      if (!flash_args.empty())
        AddArg("--ppapi-flash-args=" + base::JoinString(flash_args, ","));
    } else {
      const std::string description = LookUpInStringPairs(pairs, "DESCRIPTION");
      const std::string mime_types = LookUpInStringPairs(pairs, "MIME_TYPES");

      std::string plugin_string = file_name;
      if (!plugin_name.empty()) {
        plugin_string += "#" + plugin_name;
        if (!description.empty()) {
          plugin_string += "#" + description;
          if (!version.empty()) {
            plugin_string += "#" + version;
          }
        }
      }
      plugin_string += ";" + mime_types;
      register_plugins.push_back(plugin_string);
    }
  }

  if (!register_plugins.empty()) {
    std::sort(register_plugins.begin(), register_plugins.end());
    AddArg("--register-pepper-plugins=" + base::JoinString(register_plugins,
                                                           ","));
  }
}

void ChromiumCommandBuilder::AddUiFlags() {
  AddArg("--ui-prioritize-in-gpu-process");

  if (UseFlagIsSet("opengles"))
    AddArg("--use-gl=egl");

  // On boards with ARM NEON support, force libvpx to use the NEON-optimized
  // code paths. Remove once http://crbug.com/161834 is fixed.
  // This is needed because libvpx cannot check cpuinfo within the sandbox.
  if (UseFlagIsSet("neon"))
    AddEnvVar("VPX_SIMD_CAPS", "0xf");

  if (UseFlagIsSet("link")) {
    // This is the link board (aka Pixel).
    AddArg("--touch-calibration=0,0,0,50");
    AddArg("--touch-noise-filtering");
  }

  if (UseFlagIsSet("edge_touch_filtering"))
    AddArg("--edge-touch-filtering");

  if (UseFlagIsSet("native_gpu_memory_buffers"))
    AddArg("--enable-native-gpu-memory-buffers");

  AddArg(std::string("--gpu-sandbox-failures-fatal=") +
      (is_chrome_os_hardware() ? "yes" : "no"));

  if (UseFlagIsSet("gpu_sandbox_allow_sysv_shm"))
    AddArg("--gpu-sandbox-allow-sysv-shm");

  if (UseFlagIsSet("gpu_sandbox_start_early"))
    AddArg("--gpu-sandbox-start-early");

  // Allow Chrome to access GPU memory information despite /sys/kernel/debug
  // being owned by debugd. This limits the security attack surface versus
  // leaving the whole debug directory world-readable: http://crbug.com/175828
  // (Only do this if we're running as root, i.e. not in a test.)
  const base::FilePath debugfs_gpu_path(GetPath(kDebugfsGpuPath));
  if (getuid() == 0 && !base::DirectoryExists(debugfs_gpu_path)) {
    if (base::CreateDirectory(debugfs_gpu_path)) {
      util::Run("mount", "-o", "bind", "/sys/kernel/debug/dri/0",
                kDebugfsGpuPath, nullptr);
    } else {
      PLOG(ERROR) << "Unable to create " << kDebugfsGpuPath;
    }
  }
}

}  // namespace ui
}  // namespace chromeos
