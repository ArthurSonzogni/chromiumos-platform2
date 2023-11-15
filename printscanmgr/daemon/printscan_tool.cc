// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printscanmgr/daemon/printscan_tool.h"

#include <set>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <brillo/files/file_util.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <lorgnette-client/lorgnette/dbus-proxies.h>
#include <printscanmgr/proto_bindings/printscanmgr_service.pb.h>

namespace printscanmgr {

namespace {

const base::FilePath kCupsFilePath =
    base::FilePath("run/cups/debug/debug-flag");
const base::FilePath kIppusbFilePath =
    base::FilePath("run/ippusb/debug/debug-flag");

// Saves the values of `success` and `error` to `success_out` and `error_out`,
// respectively.
void SaveArgs(base::OnceClosure quit_closure,
              bool* success_out,
              std::string* error_out,
              bool success,
              const std::string& error) {
  DCHECK(success_out);
  DCHECK(error_out);
  *success_out = success;
  *error_out = error;
  std::move(quit_closure).Run();
}

}  // namespace

PrintscanTool::PrintscanTool(mojom::Executor* remote)
    : PrintscanTool(remote, base::FilePath("/")) {}

PrintscanTool::PrintscanTool(mojom::Executor* remote,
                             const base::FilePath& root_path)
    : root_path_(root_path), remote_(remote) {
  DCHECK(remote_);
}

void PrintscanTool::Init(
    std::unique_ptr<org::chromium::lorgnette::ManagerProxyInterface>
        lorgnette_proxy) {
  DCHECK(lorgnette_proxy);
  lorgnette_proxy_ = std::move(lorgnette_proxy);
}

PrintscanDebugSetCategoriesResponse PrintscanTool::DebugSetCategories(
    const PrintscanDebugSetCategoriesRequest& request) {
  PrintscanDebugSetCategoriesResponse response;

  std::set<PrintscanDebugSetCategoriesRequest::DebugLogCategory> categories;
  for (const auto category : request.categories()) {
    if (!PrintscanDebugSetCategoriesRequest_DebugLogCategory_IsValid(
            category)) {
      LOG(ERROR) << "Unknown category flag: " << category;
      response.set_result(false);
      return response;
    }
    categories.insert(
        static_cast<PrintscanDebugSetCategoriesRequest::DebugLogCategory>(
            category));
  }

  auto printing_search = categories.find(
      PrintscanDebugSetCategoriesRequest::DEBUG_LOG_CATEGORY_PRINTING);
  auto scanning_search = categories.find(
      PrintscanDebugSetCategoriesRequest::DEBUG_LOG_CATEGORY_SCANNING);
  bool enable_cups = printing_search != categories.end();
  bool enable_ippusb = printing_search != categories.end() ||
                       scanning_search != categories.end();
  bool enable_lorgnette = scanning_search != categories.end();

  bool success = true;
  // Enable Cups logging if the printing category is enabled.
  success = ToggleCups(enable_cups);
  if (success) {
    // Enable Ippusb logging if the printing or scanning category is
    // enabled.
    success = ToggleIppusb(enable_ippusb);
  }
  if (success) {
    // Enable Lorgnette logging if the scanning category is enabled.
    success = ToggleLorgnette(enable_lorgnette);
  }
  if (!success) {
    // Disable all logging if there were any errors setting up logging.
    ToggleCups(false);
    ToggleIppusb(false);
    ToggleLorgnette(false);
  }
  success &= RestartServices();

  response.set_result(success);
  return response;
}

// static
std::unique_ptr<PrintscanTool> PrintscanTool::CreateAndInitForTesting(
    mojom::Executor* remote,
    const base::FilePath& path,
    std::unique_ptr<org::chromium::lorgnette::ManagerProxyInterface>
        lorgnette_proxy_mock) {
  std::unique_ptr<PrintscanTool> printscan_tool(
      new PrintscanTool(remote, path));
  printscan_tool->Init(std::move(lorgnette_proxy_mock));
  return printscan_tool;
}

// Create an empty file at the given path from root_path_.
bool PrintscanTool::CreateEmptyFile(PrintscanFilePaths path) {
  base::FilePath full_path;
  switch (path) {
    case PRINTSCAN_CUPS_FILEPATH:
      full_path = root_path_.Append(kCupsFilePath);
      break;
    case PRINTSCAN_IPPUSB_FILEPATH:
      full_path = root_path_.Append(kIppusbFilePath);
      break;
  }
  return base::WriteFile(full_path, "", 0) == 0;
}

// Delete a file at the given path from root_path_.
bool PrintscanTool::DeleteFile(PrintscanFilePaths path) {
  base::FilePath full_path;
  switch (path) {
    case PRINTSCAN_CUPS_FILEPATH:
      full_path = root_path_.Append(kCupsFilePath);
      break;
    case PRINTSCAN_IPPUSB_FILEPATH:
      full_path = root_path_.Append(kIppusbFilePath);
      break;
  }
  return brillo::DeleteFile(full_path);
}

// Enable Cups debug logs if `enable` is set, otherwise disable the logs.
// Return true on success.
bool PrintscanTool::ToggleCups(bool enable) {
  if (enable) {
    if (!CreateEmptyFile(PRINTSCAN_CUPS_FILEPATH)) {
      LOG(ERROR) << "Failed to create cups debug-flag.";
      return false;
    }
    LOG(INFO) << "Advanced CUPS logging enabled.";
  } else {
    if (!DeleteFile(PRINTSCAN_CUPS_FILEPATH)) {
      LOG(ERROR) << "Failed to delete cups debug-flag.";
      return false;
    }
    LOG(INFO) << "Advanced CUPS logging disabled.";
  }
  return true;
}

// Enable Ippusb debug logs if `enable` is set, otherwise disable the logs.
// Return true on success.
bool PrintscanTool::ToggleIppusb(bool enable) {
  if (enable) {
    if (!CreateEmptyFile(PRINTSCAN_IPPUSB_FILEPATH)) {
      LOG(ERROR) << "Failed to create ippusb debug-flag.";
      return false;
    }
    LOG(INFO) << "Advanced ippusb logging enabled.";
  } else {
    if (!DeleteFile(PRINTSCAN_IPPUSB_FILEPATH)) {
      LOG(ERROR) << "Failed to delete ippusb delete-flag.";
      return false;
    }
    LOG(INFO) << "Advanced ippusb logging disabled.";
  }
  return true;
}

// Enable Lorgnette debug logs if `enable` is set, otherwise disable the logs.
// Return true on success.
bool PrintscanTool::ToggleLorgnette(bool enable) {
  DCHECK(lorgnette_proxy_);

  lorgnette::SetDebugConfigRequest request;
  request.set_enabled(enable);
  lorgnette::SetDebugConfigResponse response;
  brillo::ErrorPtr error;
  if (!lorgnette_proxy_->SetDebugConfig(request, &response, &error)) {
    LOG(ERROR) << "Failed to call SetDebugConfig: " << error->GetMessage();
  }

  if (!response.success()) {
    return false;
  }

  if (enable) {
    LOG(INFO) << "Advanced lorgnette logging enabled.";
  } else {
    LOG(INFO) << "Advanced lorgnette logging disabled.";
  }

  return true;
}

// Restart cupsd.
bool PrintscanTool::RestartServices() {
  // cupsd is intended to have the same lifetime as the ui, so we need to
  // fully restart it.
  std::string error;
  bool success;
  base::RunLoop run_loop;
  remote_->RestartUpstartJob(
      mojom::UpstartJob::kCupsd,
      base::BindOnce(&SaveArgs, run_loop.QuitClosure(), &success, &error));
  run_loop.Run();
  if (!success) {
    LOG(ERROR) << "Executor mojo method RestartUpstartJob for cupsd failed: "
               << error;
  }

  return success;
}

}  // namespace printscanmgr
