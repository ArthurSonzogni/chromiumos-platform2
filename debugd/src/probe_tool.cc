// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/probe_tool.h"

#include <fcntl.h>

#include <array>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_string_value_serializer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/values.h>
#include <brillo/errors/error_codes.h>
#include <build/build_config.h>
#include <build/buildflag.h>
#include <chromeos/dbus/service_constants.h>
#include <vboot/crossystem.h>

#include "debugd/src/error_utils.h"
#include "debugd/src/sandboxed_process.h"

namespace debugd {

namespace {
constexpr char kErrorPath[] = "org.chromium.debugd.RunProbeFunctionError";
constexpr char kSandboxInfoDir[] = "/etc/runtime_probe/sandbox";
constexpr char kSandboxArgs[] = "/etc/runtime_probe/sandbox/args.json";
constexpr char kRuntimeProbeBinary[] = "/usr/bin/runtime_probe";
constexpr char kDefaultRunAs[] = "runtime_probe";
constexpr char kMinijailBindFlag[] = "-b";
constexpr char kMinijailBindKey[] = "binds";
constexpr char kMinijailUserKey[] = "user";
constexpr char kMinijailGroupKey[] = "group";
constexpr char kMinijailOtherArgsKey[] = "other_args";

bool CreateNonblockingPipe(base::ScopedFD* read_fd, base::ScopedFD* write_fd) {
  int pipe_fd[2];
  int ret = pipe2(pipe_fd, O_CLOEXEC | O_NONBLOCK);
  if (ret != 0) {
    PLOG(ERROR) << "Cannot create a pipe";
    return false;
  }
  read_fd->reset(pipe_fd[0]);
  write_fd->reset(pipe_fd[1]);
  return true;
}

bool PathOrSymlinkExists(const base::FilePath& path) {
  auto abs_path = base::MakeAbsoluteFilePath(path);
  return base::PathExists(abs_path);
}

bool GetFunctionNameFromProbeStatement(brillo::ErrorPtr* error,
                                       const std::string& probe_statement,
                                       std::string* name_out) {
  // The name of the probe function is the only key in the dictionary.
  JSONStringValueDeserializer deserializer(probe_statement);
  auto probe_statement_dict = deserializer.Deserialize(nullptr, nullptr);
  if (!probe_statement_dict || !probe_statement_dict->is_dict()) {
    DEBUGD_ADD_ERROR_FMT(
        error, kErrorPath,
        "Failed to parse probe statement. Expected json but got: %s",
        probe_statement.c_str());
    return false;
  }
  if (probe_statement_dict->DictSize() != 1) {
    DEBUGD_ADD_ERROR_FMT(
        error, kErrorPath,
        "Expected only one probe function in probe statement but got: %zu",
        probe_statement_dict->DictSize());
    return false;
  }
  auto it = probe_statement_dict->DictItems().begin();
  name_out->assign(it->first);
  return true;
}

bool CheckArgStringAndAddError(const base::Value& arg,
                               brillo::ErrorPtr* error) {
  if (!arg.is_string()) {
    std::string arg_str;
    JSONStringValueSerializer serializer(&arg_str);
    serializer.set_pretty_print(true);
    serializer.Serialize(arg);
    DEBUGD_ADD_ERROR_FMT(
        error, kErrorPath,
        "Failed to parse Minijail arguments. Expected string but got: %s",
        arg_str.c_str());
    return false;
  }
  return true;
}

}  // namespace

bool ProbeTool::EvaluateProbeFunction(
    brillo::ErrorPtr* error,
    const std::string& probe_statement,
    int log_level,
    brillo::dbus_utils::FileDescriptor* outfd,
    brillo::dbus_utils::FileDescriptor* errfd) {
  // Details of sandboxing for probing should be centralized in a single
  // directory. Sandboxing is mandatory when we don't allow debug features.
  auto process = CreateSandboxedProcess(error, probe_statement);
  if (process == nullptr)
    return false;

  base::ScopedFD out_r_fd, out_w_fd;
  base::ScopedFD err_r_fd, err_w_fd;
  if (!CreateNonblockingPipe(&out_r_fd, &out_w_fd) ||
      !CreateNonblockingPipe(&err_r_fd, &err_w_fd)) {
    DEBUGD_ADD_ERROR(error, kErrorPath, "Cannot create a pipe");
    return false;
  }

  process->AddArg(kRuntimeProbeBinary);
  process->AddArg("--helper");
  process->AddArg(base::StringPrintf("--log_level=%d", log_level));
  process->AddArg("--");
  process->AddArg(probe_statement);
  process->BindFd(out_w_fd.get(), STDOUT_FILENO);
  process->BindFd(err_w_fd.get(), STDERR_FILENO);
  process->Start();
  process->Release();
  *outfd = std::move(out_r_fd);
  *errfd = std::move(err_r_fd);
  return true;
}

void ProbeTool::SetMinijailArgumentsForTesting(
    std::unique_ptr<base::Value> dict) {
  DCHECK(dict && dict->is_dict());
  minijail_args_dict_ = std::move(dict->GetDict());
}

bool ProbeTool::LoadMinijailArguments(brillo::ErrorPtr* error) {
  std::string minijail_args_str;
  if (!base::ReadFileToString(base::FilePath(kSandboxArgs),
                              &minijail_args_str)) {
    DEBUGD_ADD_ERROR_FMT(error, kErrorPath,
                         "Failed to read Minijail arguments from: %s",
                         kSandboxArgs);
    return false;
  }
  JSONStringValueDeserializer deserializer(minijail_args_str);
  auto dict = deserializer.Deserialize(nullptr, nullptr);
  if (!dict || !dict->is_dict()) {
    DEBUGD_ADD_ERROR_FMT(error, kErrorPath,
                         "Minijail arguments are not stored in dict. Expected "
                         "dict but got: %s",
                         minijail_args_str.c_str());
    return false;
  }
  minijail_args_dict_ = std::move(dict->GetDict());
  return true;
}

bool ProbeTool::GetValidMinijailArguments(brillo::ErrorPtr* error,
                                          const std::string& function_name,
                                          std::string* user_out,
                                          std::string* group_out,
                                          std::vector<std::string>* args_out) {
  args_out->clear();
  user_out->clear();
  group_out->clear();
  if (minijail_args_dict_.empty()) {
    if (!LoadMinijailArguments(error)) {
      return false;
    }
  }
  const auto* minijail_args = minijail_args_dict_.FindDict(function_name);
  if (!minijail_args) {
    DEBUGD_ADD_ERROR_FMT(error, kErrorPath,
                         "Arguments of \"%s\" is not found in Minijail "
                         "arguments file: %s",
                         function_name.c_str(), kSandboxArgs);
    return false;
  }
  DVLOG(1) << "Minijail arguments: " << (*minijail_args);

  // Parse user argument.
  const auto* user_arg = minijail_args->FindString(kMinijailUserKey);
  // If the user is not specified, use the default user.
  *user_out = user_arg ? *user_arg : kDefaultRunAs;

  // Parse group argument.
  const auto* group_arg = minijail_args->FindString(kMinijailGroupKey);
  // If the group is not specified, use the default group.
  *group_out = group_arg ? *group_arg : kDefaultRunAs;

  // Parse other arguments.
  // Do this before parsing bind-mount arguments because we need some -k
  // arguments to appear before some -b arguments.
  const auto* other_args = minijail_args->FindList(kMinijailOtherArgsKey);
  if (other_args) {
    for (const auto& arg : *other_args) {
      if (!CheckArgStringAndAddError(arg, error)) {
        args_out->clear();
        user_out->clear();
        group_out->clear();
        return false;
      }
      const auto& curr_arg = arg.GetString();
      args_out->push_back(curr_arg);
    }
  }

  // Parse bind-mount arguments.
  const auto* bind_args = minijail_args->FindList(kMinijailBindKey);
  if (bind_args) {
    for (const auto& arg : *bind_args) {
      if (!CheckArgStringAndAddError(arg, error)) {
        args_out->clear();
        user_out->clear();
        group_out->clear();
        return false;
      }
      const auto& curr_arg = arg.GetString();
      // Check existence of bind paths.
      auto bind_paths = base::SplitString(curr_arg, ",", base::TRIM_WHITESPACE,
                                          base::SPLIT_WANT_ALL);
      if (bind_paths.size() < 1) {
        DEBUGD_ADD_ERROR_FMT(error, kErrorPath,
                             "Failed to parse Minijail bind arguments. Got: %s",
                             curr_arg.c_str());
        args_out->clear();
        user_out->clear();
        group_out->clear();
        return false;
      }
      if (PathOrSymlinkExists(base::FilePath(bind_paths[0]))) {
        args_out->push_back(kMinijailBindFlag);
        args_out->push_back(curr_arg);
      }
    }
  }

  return true;
}

std::unique_ptr<brillo::Process> ProbeTool::CreateSandboxedProcess(
    brillo::ErrorPtr* error, const std::string& probe_statement) {
  std::string function_name;
  if (!GetFunctionNameFromProbeStatement(error, probe_statement,
                                         &function_name)) {
    return nullptr;
  }

  auto sandboxed_process = std::make_unique<SandboxedProcess>();
  // The following is the general Minijail set up for runtime_probe in debugd
  // /dev/log needs to be bind mounted before any possible tmpfs mount on run
  // See:
  //   minijail0 manpage (`man 1 minijail0` in cros\_sdk)
  //   https://chromium.googlesource.com/chromiumos/docs/+/HEAD/sandboxing.md
  std::vector<std::string> parsed_args{
      "-G",                // Inherit all the supplementary groups
      "-P", "/mnt/empty",  // Set /mnt/empty as the root fs using pivot_root
      "-b", "/",           // Bind mount rootfs
      "-b", "/proc",       // Bind mount /proc
      "-b", "/dev/log",    // Enable logging
      "-t",                // Mount a tmpfs on /tmp
      "-r",                // Remount /proc readonly
      "-d"                 // Mount /dev with a minimal set of nodes.
  };
  std::vector<std::string> config_args;
  std::string sandbox_user, sandbox_group;
  if (!GetValidMinijailArguments(error, function_name, &sandbox_user,
                                 &sandbox_group, &config_args))
    return nullptr;

  parsed_args.insert(std::end(parsed_args),
                     std::make_move_iterator(std::begin(config_args)),
                     std::make_move_iterator(std::end(config_args)));

  sandboxed_process->SandboxAs(sandbox_user, sandbox_group);
  const auto seccomp_path = base::FilePath{kSandboxInfoDir}.Append(
      base::StringPrintf("%s-seccomp.policy", function_name.c_str()));
  if (!base::PathExists(seccomp_path)) {
    DEBUGD_ADD_ERROR_FMT(error, kErrorPath,
                         "Seccomp policy file of \"%s\" is not found at: %s",
                         function_name.c_str(), seccomp_path.value().c_str());
    return nullptr;
  }
  sandboxed_process->SetSeccompFilterPolicyFile(seccomp_path.MaybeAsASCII());
  DVLOG(1) << "Sandbox for " << function_name << " is ready";
  if (!sandboxed_process->Init(parsed_args)) {
    DEBUGD_ADD_ERROR(error, kErrorPath,
                     "Sandboxed process initialization failure");
    return nullptr;
  }
  return sandboxed_process;
}

}  // namespace debugd
