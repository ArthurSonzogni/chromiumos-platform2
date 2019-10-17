/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "arc/adbd/adbd.h"

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <memory>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/process/launch.h>
#include <base/strings/string_util.h>
#include <base/sys_info.h>
#include <base/values.h>

namespace adbd {

namespace {

constexpr char kRuntimePath[] = "/run/arc/adbd";
constexpr char kConfigFSPath[] = "/dev/config";
constexpr char kFunctionFSPath[] = "/dev/usb-ffs/adb";
constexpr char kConfigPath[] = "/etc/arc/adbd.json";

// The shifted u/gid of the shell user, used by Android's adbd.
constexpr uid_t kShellUgid = 657360;

// The blob that is sent to FunctionFS to setup the adb gadget. This works for
// newer kernels (>=3.18). This and the following blobs were created by
// https://android.googlesource.com/platform/system/core/+/master/adb/daemon/usb.cpp
constexpr const uint8_t kControlPayloadV2[] = {
    0x03, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x42, 0x01,
    0x01, 0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x07, 0x05, 0x82, 0x02,
    0x40, 0x00, 0x00, 0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x42, 0x01, 0x01,
    0x07, 0x05, 0x01, 0x02, 0x00, 0x02, 0x00, 0x07, 0x05, 0x82, 0x02, 0x00,
    0x02, 0x00, 0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x42, 0x01, 0x01, 0x07,
    0x05, 0x01, 0x02, 0x00, 0x04, 0x00, 0x06, 0x30, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x00, 0x04, 0x00, 0x06, 0x30, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x23, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// The blob that is sent to FunctionFS to setup the adb gadget. This works
// for older kernels.
constexpr const uint8_t kControlPayloadV1[] = {
    0x01, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x09, 0x04, 0x00, 0x00, 0x02, 0xFF,
    0x42, 0x01, 0x01, 0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x07,
    0x05, 0x82, 0x02, 0x40, 0x00, 0x00, 0x09, 0x04, 0x00, 0x00, 0x02,
    0xFF, 0x42, 0x01, 0x01, 0x07, 0x05, 0x01, 0x02, 0x00, 0x02, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x00, 0x02, 0x00};

// The blob that is sent to FunctionFS to setup the name of the gadget. It is
// "ADB Interface".
constexpr const uint8_t kControlStrings[] = {
    0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x09, 0x04, 0x41, 0x44, 0x42, 0x20,
    0x49, 0x6E, 0x74, 0x65, 0x72, 0x66, 0x61, 0x63, 0x65, 0x00};

// Bind-mounts a file located in |source| to |target|. It also makes it be owned
// and only writable by Android shell.
bool BindMountFile(const base::FilePath& source, const base::FilePath& target) {
  if (!base::PathExists(target)) {
    base::ScopedFD target_file(
        open(target.value().c_str(), O_WRONLY | O_CREAT, 0600));
    if (!target_file.is_valid()) {
      PLOG(ERROR) << "Failed to touch " << target.value();
      return false;
    }
  }
  if (chown(source.value().c_str(), kShellUgid, kShellUgid) == -1) {
    PLOG(ERROR) << "Failed to chown " << source.value()
                << " to Android's shell user";
    return false;
  }
  if (mount(source.value().c_str(), target.value().c_str(), nullptr, MS_BIND,
            nullptr) == -1) {
    PLOG(ERROR) << "Failed to mount " << target.value();
    return false;
  }
  return true;
}

// Writes a string to a file. Returns false if the full string was not able to
// be written.
bool WriteFile(const base::FilePath& filename, const std::string& contents) {
  int bytes_written =
      base::WriteFile(filename, contents.c_str(), contents.size());
  if (bytes_written == -1) {
    PLOG(ERROR) << "Failed to write '" << contents << "' to "
                << filename.value();
    return false;
  }
  if (bytes_written < contents.size()) {
    LOG(ERROR) << "Truncated write '" << contents << "' to "
               << filename.value();
    return false;
  }
  return true;
}

}  // namespace

bool CreatePipe(const base::FilePath& path) {
  // Create the FIFO at a temporary path. We will call rename(2) later to make
  // the whole operation atomic.
  const base::FilePath tmp_path = path.AddExtension(".tmp");
  if (unlink(tmp_path.value().c_str()) == -1 && errno != ENOENT) {
    PLOG(ERROR) << "Failed to remove stale FIFO at " << tmp_path.value();
    return false;
  }
  if (mkfifo(tmp_path.value().c_str(), 0600) == -1) {
    PLOG(ERROR) << "Failed to create FIFO at " << tmp_path.value();
    return false;
  }
  // base::Unretained is safe since the closure will be run before |tmp_path|
  // goes out of scope.
  base::ScopedClosureRunner unlink_fifo(base::Bind(
      base::IgnoreResult(&unlink), base::Unretained(tmp_path.value().c_str())));
  if (chown(tmp_path.value().c_str(), kShellUgid, kShellUgid) == -1) {
    PLOG(ERROR) << "Failed to chown FIFO at " << tmp_path.value()
                << " to Android's shell user";
    return false;
  }
  if (rename(tmp_path.value().c_str(), path.value().c_str()) == -1) {
    PLOG(ERROR) << "Failed to rename FIFO at " << tmp_path.value() << " to "
                << path.value();
    return false;
  }
  ignore_result(unlink_fifo.Release());
  return true;
}

bool GetConfiguration(AdbdConfiguration* config) {
  std::string config_json_data;
  if (!base::ReadFileToString(base::FilePath(kConfigPath), &config_json_data)) {
    PLOG(ERROR) << "Failed to read config from " << kConfigPath;
    return false;
  }

  std::string error_msg;
  std::unique_ptr<const base::Value> config_root_val =
      base::JSONReader::ReadAndReturnError(
          config_json_data, base::JSON_PARSE_RFC, nullptr /* error_code_out */,
          &error_msg, nullptr /* error_line_out */,
          nullptr /* error_column_out */);
  if (!config_root_val) {
    LOG(ERROR) << "Failed to parse adb.json: " << error_msg;
    return false;
  }
  const base::DictionaryValue* config_root_dict = nullptr;
  if (!config_root_val->GetAsDictionary(&config_root_dict)) {
    LOG(ERROR) << "Failed to parse root dictionary from adb.json";
    return false;
  }
  if (!config_root_dict->GetString("usbProductId", &config->usb_product_id)) {
    LOG(ERROR) << "Failed to parse usbProductId";
    return false;
  }
  const base::ListValue* kernel_module_list = nullptr;
  // kernelModules are optional.
  if (config_root_dict->GetList("kernelModules", &kernel_module_list)) {
    for (const auto& kernel_module_value :
         base::ValueReferenceAdapter(*kernel_module_list)) {
      AdbdConfigurationKernelModule module;
      const base::DictionaryValue* kernel_module_dict = nullptr;
      if (!kernel_module_value.GetAsDictionary(&kernel_module_dict)) {
        LOG(ERROR) << "kernelModules contains a non-dictionary";
        return false;
      }
      if (!kernel_module_dict->GetString("name", &module.name)) {
        LOG(ERROR) << "Failed to parse kernelModules.name";
        return false;
      }
      const base::ListValue* parameter_list = nullptr;
      if (kernel_module_dict->GetList("parameters", &parameter_list)) {
        // Parameters are optional.
        for (const auto& parameter_value :
             base::ValueReferenceAdapter(*parameter_list)) {
          std::string parameter;
          if (!parameter_value.GetAsString(&parameter)) {
            LOG(ERROR) << "kernelModules.parameters contains a non-string";
            return false;
          }
          module.parameters.emplace_back(parameter);
        }
      }
      config->kernel_modules.emplace_back(module);
    }
  }

  return true;
}

std::string GetStrippedReleaseBoard() {
  std::string board = base::SysInfo::GetLsbReleaseBoard();
  const size_t index = board.find("-signed-");
  if (index != std::string::npos)
    board.resize(index);

  return base::ToLowerASCII(board);
}

std::string GetUDCDriver() {
  base::FileEnumerator udc_enum(
      base::FilePath("/sys/class/udc/"), false /* recursive */,
      base::FileEnumerator::FILES | base::FileEnumerator::SHOW_SYM_LINKS);
  const base::FilePath name = udc_enum.Next();
  if (name.empty())
    return std::string();
  // We expect to only have one UDC driver in the system, so we can just return
  // the first file in the directory.
  return name.BaseName().value();
}

bool SetupConfigFS(const std::string& serialnumber,
                   const std::string& usb_product_id,
                   const std::string& usb_product_name) {
  const base::FilePath configfs_directory(kConfigFSPath);
  if (!base::CreateDirectory(configfs_directory)) {
    PLOG(ERROR) << "Failed to create " << configfs_directory.value();
    return false;
  }
  if (mount("configfs", configfs_directory.value().c_str(), "configfs",
            MS_NOEXEC | MS_NOSUID | MS_NODEV, nullptr) == -1) {
    PLOG(ERROR) << "Failed to mount configfs";
    return false;
  }

  // Setup the gadget.
  const base::FilePath gadget_path = configfs_directory.Append("usb_gadget/g1");
  if (!base::CreateDirectory(gadget_path.Append("functions/ffs.adb"))) {
    PLOG(ERROR) << "Failed to create ffs.adb directory";
    return false;
  }
  if (!base::CreateDirectory(gadget_path.Append("configs/b.1/strings/0x409"))) {
    PLOG(ERROR) << "Failed to create configs/b.1/strings directory";
    return false;
  }
  if (!base::CreateDirectory(gadget_path.Append("strings/0x409"))) {
    PLOG(ERROR) << "Failed to create config strings directory";
    return false;
  }
  const base::FilePath function_symlink_path =
      gadget_path.Append("configs/b.1/f1");
  if (!base::PathExists(function_symlink_path)) {
    if (!base::CreateSymbolicLink(gadget_path.Append("functions/ffs.adb"),
                                  function_symlink_path)) {
      PLOG(ERROR) << "Failed to create symbolic link";
      return false;
    }
  }
  if (!WriteFile(gadget_path.Append("idVendor"), "0x18d1"))
    return false;
  if (!WriteFile(gadget_path.Append("idProduct"), usb_product_id))
    return false;
  if (!WriteFile(gadget_path.Append("strings/0x409/serialnumber"),
                 serialnumber)) {
    return false;
  }
  if (!WriteFile(gadget_path.Append("strings/0x409/manufacturer"), "google"))
    return false;
  if (!WriteFile(gadget_path.Append("strings/0x409/product"), usb_product_name))
    return false;
  if (!WriteFile(gadget_path.Append("configs/b.1/MaxPower"), "500"))
    return false;

  return true;
}

base::ScopedFD SetupFunctionFS(const std::string& udc_driver_name) {
  const base::FilePath functionfs_path(kFunctionFSPath);

  // Create the FunctionFS mount.
  if (!base::CreateDirectory(functionfs_path)) {
    PLOG(ERROR) << "Failed to create " << functionfs_path.value();
    return base::ScopedFD();
  }
  if (mount("adb", functionfs_path.value().c_str(), "functionfs",
            MS_NOEXEC | MS_NOSUID | MS_NODEV, nullptr) == -1) {
    PLOG(ERROR) << "Failed to mount functionfs";
    return base::ScopedFD();
  }

  // Send the configuration to the real control endpoint.
  base::ScopedFD control_file(
      open(functionfs_path.Append("ep0").value().c_str(), O_WRONLY));
  if (!control_file.is_valid()) {
    PLOG(ERROR) << "Failed to open control file";
    return base::ScopedFD();
  }
  if (!base::WriteFileDescriptor(
          control_file.get(), reinterpret_cast<const char*>(kControlPayloadV2),
          sizeof(kControlPayloadV2))) {
    PLOG(WARNING) << "Failed to write the V2 control payload, "
                     "trying to write the V1 control payload";
    if (!base::WriteFileDescriptor(
            control_file.get(),
            reinterpret_cast<const char*>(kControlPayloadV1),
            sizeof(kControlPayloadV1))) {
      PLOG(ERROR) << "Failed to write the V1 control payload";
      return base::ScopedFD();
    }
  }
  if (!base::WriteFileDescriptor(control_file.get(),
                                 reinterpret_cast<const char*>(kControlStrings),
                                 sizeof(kControlStrings))) {
    PLOG(ERROR) << "Failed to write the control strings";
    return base::ScopedFD();
  }
  if (!WriteFile(base::FilePath("/dev/config/usb_gadget/g1/UDC"),
                 udc_driver_name)) {
    return base::ScopedFD();
  }

  // Bind-mount the bulk-in/bulk-out endpoints into the shared mount.
  const base::FilePath runtime_path(kRuntimePath);
  for (const auto& endpoint : {"ep1", "ep2"}) {
    if (!BindMountFile(functionfs_path.Append(endpoint),
                       runtime_path.Append(endpoint))) {
      return base::ScopedFD();
    }
  }

  return control_file;
}

bool SetupKernelModules(
    const std::vector<AdbdConfigurationKernelModule>& kernel_modules) {
  for (const auto& kernel_module : kernel_modules) {
    std::vector<std::string> argv;
    argv.emplace_back("/sbin/modprobe");
    argv.emplace_back(kernel_module.name);
    argv.insert(std::end(argv), std::begin(kernel_module.parameters),
                std::end(kernel_module.parameters));
    base::Process process(base::LaunchProcess(argv, base::LaunchOptions()));
    if (!process.IsValid()) {
      PLOG(ERROR) << "Failed to invoke /sbin/modprobe " << kernel_module.name;
      return false;
    }
    int exit_code = -1;
    if (!process.WaitForExit(&exit_code)) {
      PLOG(ERROR) << "Failed to wait for /sbin/modprobe " << kernel_module.name;
      return false;
    }
    if (exit_code != 0) {
      LOG(ERROR) << "Invocation of /sbin/modprobe " << kernel_module.name
                 << " exited with non-zero code " << exit_code;
      return false;
    }
  }
  return true;
}

}  // namespace adbd
