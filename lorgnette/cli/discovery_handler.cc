// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/cli/discovery_handler.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <brillo/errors/error.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

namespace lorgnette::cli {

namespace {

constexpr char kClientID[] = "lorgnette_cli";

void PrintScannerDetails(const lorgnette::ScannerInfo& info,
                         std::ostream& out) {
  std::vector<std::string> formats(info.image_format().begin(),
                                   info.image_format().end());
  // clang-format off
  out << "      " << "Device UUID:       " << info.device_uuid() << std::endl
      << "      " << "Connection String: " << info.name() << std::endl
      << "      " << "Manufacturer:      " << info.manufacturer() << std::endl
      << "      " << "Model:             " << info.model() << std::endl
      << "      " << "Device Type:       " << info.type() << std::endl
      << "      " << "Connection Type:   "
                  << ConnectionType_Name(info.connection_type())
                                           << std::endl
      << "      " << "Secure Connection: " << (info.secure() ? "yes" : "no")
                                           << std::endl
      << "      " << "Supported Formats: " << base::JoinString(formats, " ")
                                           << std::endl;
  // clang-format on
}

}  // namespace

DiscoveryHandler::DiscoveryHandler(
    base::RepeatingClosure quit_closure,
    org::chromium::lorgnette::ManagerProxy* manager)
    : AsyncHandler(quit_closure, manager) {}

DiscoveryHandler::~DiscoveryHandler() {
  if (!session_id_.empty()) {
    StopScannerDiscoveryRequest request;
    request.set_session_id(session_id_);
    StopScannerDiscoveryResponse response;
    brillo::ErrorPtr error;
    if (!manager_->StopScannerDiscovery(request, &response, &error)) {
      LOG(ERROR) << "Failed to stop discovery session: " << error->GetMessage();
    }
  }
}

void DiscoveryHandler::ConnectSignal() {
  manager_->RegisterScannerListChangedSignalHandler(
      base::BindRepeating(&DiscoveryHandler::HandleScannerListChangedSignal,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&DiscoveryHandler::OnConnectedCallback,
                     weak_factory_.GetWeakPtr()));
}

bool DiscoveryHandler::StartDiscovery() {
  StartScannerDiscoveryRequest request;
  request.set_client_id(kClientID);
  brillo::ErrorPtr error;
  StartScannerDiscoveryResponse response;
  if (!manager_->StartScannerDiscovery(request, &response, &error)) {
    LOG(ERROR) << "Failed to call StartScannerDiscovery: "
               << error->GetMessage();
    return false;
  }

  if (!response.started()) {
    return false;
  }

  session_id_ = response.session_id();
  return true;
}

void DiscoveryHandler::HandleScannerListChangedSignal(
    const ScannerListChangedSignal& signal) {
  if (signal.session_id() != session_id_) {
    return;
  }

  const ScannerInfo& scanner = signal.scanner();
  switch (signal.event_type()) {
    case ScannerListChangedSignal::SCANNER_ADDED:
      if (!name_substring_.empty() &&
          scanner.name().find(name_substring_) == std::string::npos) {
        break;
      }
      std::cout << "  + " << scanner.name() << std::endl;
      if (show_details_) {
        PrintScannerDetails(scanner, std::cout);
      }
      break;

    case ScannerListChangedSignal::SCANNER_REMOVED:
      if (!name_substring_.empty() &&
          scanner.name().find(name_substring_) == std::string::npos) {
        break;
      }
      std::cout << "  - " << scanner.name() << std::endl;
      break;

    case ScannerListChangedSignal::ENUM_COMPLETE:
      std::cout << "Enumeration complete" << std::endl;
      quit_closure_.Run();
      break;

    default:
      LOG(ERROR) << "Unknown event received: " << signal.event_type();
      break;
  }
}

void DiscoveryHandler::SetShowDetails(bool show_details) {
  show_details_ = show_details;
}

void DiscoveryHandler::SetScannerPattern(const std::string& scanner_substring) {
  name_substring_ = scanner_substring;
}

}  // namespace lorgnette::cli
