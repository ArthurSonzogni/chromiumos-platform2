// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains common helper functions for service*.cc

#ifndef VM_TOOLS_CONCIERGE_SERVICE_COMMON_H_
#define VM_TOOLS_CONCIERGE_SERVICE_COMMON_H_

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/system/sys_info.h>

#include "vm_tools/concierge/service.h"
#include "vm_tools/concierge/tracing.h"
#include "vm_tools/concierge/vm_base_impl.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools::concierge {

// Helper macro that contains the boilerplate associated with each concierge
// dbus method.
#define SERVICE_METHOD(responder, ...)                                \
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);                 \
  static const char* function_name = __func__;                        \
  VMT_TRACE(kCategory, function_name);                                \
  if (is_shutting_down_) {                                            \
    RejectRequestDuringShutdown(std::move(responder), ##__VA_ARGS__); \
    return;                                                           \
  }                                                                   \
  LOG(INFO) << "Received request: " << __func__

// Automatically generates boilerplate for dbus service methods with a "raw"
// handler. Also forces you to name your variables consistently.
#define RAW_SERVICE_METHOD() SERVICE_METHOD(response_sender, method_call)

// Automatically generates boilerplate for dbus service methods with an "async"
// handler. Also forces you to name your variables consistently.
#define ASYNC_SERVICE_METHOD() SERVICE_METHOD(response_cb)

// Maximum number of extra disks to be mounted inside the VM.
constexpr int kMaxExtraDisks = 10;

// Cryptohome root base path.
constexpr char kCryptohomeRoot[] = "/run/daemon-store";

// crosvm directory name.
constexpr char kCrosvmDir[] = "crosvm";

// Plugin VM directory name.
constexpr char kPluginVmDir[] = "pvm";

// Path to the runtime directory used by VMs.
constexpr char kRuntimeDir[] = "/run/vm";

// Only allow hex digits in the cryptohome id.
constexpr char kValidCryptoHomeCharacters[] = "abcdefABCDEF0123456789";

// File extension for pflash files.
constexpr char kPflashImageExtension[] = ".pflash";

// Information about the Pflash file associated with a VM.
struct PflashMetadata {
  // Path where pflash should be installed.
  base::FilePath path;

  // Does |path| exist.
  bool is_installed;
};

// Gets the path to the file given the name, user id, location, and extension.
std::optional<base::FilePath> GetFilePathFromName(
    const VmId& vm_id,
    StorageLocation storage_location,
    const std::string& extension,
    base::FilePath storage_dir = base::FilePath(kCryptohomeRoot));

bool GetPluginDirectory(const base::FilePath& prefix,
                        const std::string& extension,
                        const std::string& vm_id,
                        bool create,
                        base::FilePath* path_out);

bool GetPluginIsoDirectory(const VmId& vm_id,
                           bool create,
                           base::FilePath* path_out);

bool IsValidOwnerId(const std::string& owner_id);

bool IsValidVmName(const std::string& vm_name);

void SendDbusResponse(dbus::ExportedObject::ResponseSender response_sender,
                      dbus::MethodCall* method_call,
                      const google::protobuf::MessageLite& response);

// Returns information about the Pflash file associated with a VM. If there is a
// failure in querying the information then it returns std::nullopt.
std::optional<PflashMetadata> GetPflashMetadata(
    const VmId& vm_id,
    base::FilePath storage_dir = base::FilePath(kCryptohomeRoot));

// Returns in order -
// 1. An installed pflash file for the VM.
// 2. A valid |start_vm_request_pflash_path|
// 3. An empty file path.
//
// Returns an error -
// 1. If a pflash file is installed and |start_vm_request_pflash_path| is valid.
// 2. If there is an error in querying information about any installed pflash
// file.
std::optional<base::FilePath> GetInstalledOrRequestPflashPath(
    const VmId& vm_id, const base::FilePath& start_vm_request_pflash_path);

// Typical check based on the name of protocol buffer fields. Our business logic
// usually means that VM name is stored in field called name and owner_id stored
// in owner_id.
template <class _RequestProto, class _ResponseProto>
bool CheckVmNameAndOwner(const _RequestProto& request,
                         _ResponseProto& response,
                         bool empty_vm_name_allowed = false) {
  auto set_failure_reason = [&](const char* reason) {
    if constexpr (requires { response.set_failure_reason(reason); }) {
      response.set_failure_reason(reason);
    } else if constexpr (requires { response.set_reason(reason); }) {
      response.set_reason(reason);
    } else {
    }
  };

  if constexpr (requires { request.owner_id(); }) {
    if (!IsValidOwnerId(request.owner_id())) {
      LOG(ERROR) << "Empty or malformed owner ID";
      set_failure_reason("Empty or malformed owner ID");
      return false;
    }
  }

  if constexpr (requires { request.cryptohome_id(); }) {
    if (!IsValidOwnerId(request.cryptohome_id())) {
      LOG(ERROR) << "Empty or malformed owner ID";
      set_failure_reason("Empty or malformed owner ID");
      return false;
    }
  }

  if constexpr (requires { request.name(); }) {
    if (!IsValidVmName(request.name())) {
      LOG(ERROR) << "Empty or malformed VM name";
      set_failure_reason("Empty or malformed VM name");
      return false;
    }
  }

  if constexpr (requires { request.vm_name(); }) {
    if (request.vm_name().empty() && empty_vm_name_allowed) {
      // Allow empty VM name, for ListVmDisks
    } else if (!IsValidVmName(request.vm_name())) {
      LOG(ERROR) << "Empty or malformed VM name";
      set_failure_reason("Empty or malformed VM name");
      return false;
    }
  }

  return true;
}

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_SERVICE_COMMON_H_
