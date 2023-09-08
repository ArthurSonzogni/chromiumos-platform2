// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/cli/advanced_scan.h"

#include <iostream>
#include <utility>

#include <base/check.h>
#include <base/files/file.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/errors/error.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

#include "lorgnette/cli/file_pattern.h"
#include "lorgnette/constants.h"
#include "lorgnette/dbus-proxies.h"
#include "lorgnette/guess_source.h"

using org::chromium::lorgnette::ManagerProxy;

namespace {

std::string ExtensionForMimeType(const std::string& mime_type) {
  if (mime_type == lorgnette::kPngMimeType) {
    return "png";
  } else if (mime_type == lorgnette::kJpegMimeType) {
    return "jpg";
  } else {
    LOG(ERROR) << "No extension for format " << mime_type;
    return "raw";
  }
}

bool ReadNextDocument(ManagerProxy* manager,
                      const lorgnette::JobHandle& job_handle,
                      const base::FilePath& output_path) {
  base::File output_file(
      output_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!output_file.IsValid()) {
    std::cerr << "Failed to create file " << output_path << ": "
              << output_file.error_details() << std::endl;
    return false;
  }

  lorgnette::ReadScanDataRequest read_request;
  *read_request.mutable_job_handle() = job_handle;

  bool keep_reading = true;
  std::cout << "Progress: " << std::flush;
  while (keep_reading) {
    lorgnette::ReadScanDataResponse read_response;
    brillo::ErrorPtr error;
    if (!manager->ReadScanData(read_request, &read_response, &error)) {
      std::cerr << "ReadScanData failed: " << error->GetMessage() << std::endl;
      return false;
    }
    switch (read_response.result()) {
      case lorgnette::OPERATION_RESULT_EOF:
        // Reached the end of the page
        keep_reading = false;
        // Fall through because data may be non-empty on the final read.
        [[fallthrough]];
      case lorgnette::OPERATION_RESULT_SUCCESS:
        if (read_response.data().empty()) {
          // Read succeeded, but no data was available.
          base::PlatformThread::Sleep(base::Milliseconds(100));
          continue;
        }
        if (!output_file.WriteAtCurrentPos(read_response.data().data(),
                                           read_response.data().length())) {
          std::cerr << "Unable to write " << read_response.data().length()
                    << " bytes to output file." << std::endl;
          return false;
        }
        std::cout << read_response.estimated_completion() << "% " << std::flush;
        break;
      default:
        std::cerr << "Error while reading scan data: "
                  << lorgnette::OperationResult_Name(read_response.result())
                  << std::endl;
        return false;
    }
  }
  std::cout << "Done" << std::endl;
  return true;
}

}  // namespace

namespace lorgnette::cli {

bool DoAdvancedScan(ManagerProxy* manager,
                    const std::string& scanner_name,
                    const std::string& mime_type,
                    const std::string& output_pattern) {
  std::cout << "Opening scanner " << scanner_name << std::endl;
  brillo::ErrorPtr error;
  lorgnette::OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string(scanner_name);
  open_request.set_client_id("lorgnette_cli");
  lorgnette::OpenScannerResponse open_response;
  if (!manager->OpenScanner(open_request, &open_response, &error)) {
    std::cerr << "OpenScanner failed: " << error->GetMessage() << std::endl;
    return false;
  }
  if (open_response.result() != lorgnette::OPERATION_RESULT_SUCCESS) {
    std::cerr << "OpenScanner returned error result "
              << lorgnette::OperationResult_Name(open_response.result())
              << std::endl;
    return false;
  }

  // Ensure the scanner handle is closed when this function returns.
  base::ScopedClosureRunner close_scanner(base::BindOnce(
      [](ManagerProxy* manager, lorgnette::ScannerHandle scanner) {
        lorgnette::CloseScannerRequest close_request;
        *close_request.mutable_scanner() = std::move(scanner);
        lorgnette::CloseScannerResponse close_response;
        brillo::ErrorPtr error;
        if (!manager->CloseScanner(close_request, &close_response, &error)) {
          std::cerr << "CloseScanner failed: " << error->GetMessage()
                    << std::endl;
          return;
        }
        if (close_response.result() != lorgnette::OPERATION_RESULT_SUCCESS) {
          std::cerr << "CloseScanner returned error result "
                    << lorgnette::OperationResult_Name(close_response.result())
                    << std::endl;
        }
      },
      manager, open_response.config().scanner()));

  std::cout << "Setting options" << std::endl;
  // TODO(bmgordon): Actually set options once SetOptions is implemented.

  size_t page = 1;
  std::string extension = ExtensionForMimeType(mime_type);

  // If the source appears to be an ADF, read pages until the ADF is empty.
  // Otherwise read a single page.
  bool more_pages = false;
  if (open_response.config().options().contains("source")) {
    const ScannerOption& source = open_response.config().options().at("source");
    lorgnette::SourceType source_type = GuessSourceType(source.string_value());
    if (source_type == lorgnette::SOURCE_ADF_SIMPLEX ||
        source_type == lorgnette::SOURCE_ADF_DUPLEX) {
      more_pages = true;
    }
  }

  do {
    base::FilePath output_path =
        ExpandPattern(output_pattern, page, scanner_name, extension);
    std::cout << "Saving page " << page << " to " << output_path << std::endl;

    lorgnette::StartPreparedScanRequest scan_request;
    *scan_request.mutable_scanner() = open_response.config().scanner();
    scan_request.set_image_format(mime_type);
    lorgnette::StartPreparedScanResponse scan_response;
    if (!manager->StartPreparedScan(scan_request, &scan_response, &error)) {
      std::cerr << "StartPreparedScan failed: " << error->GetMessage()
                << std::endl;
      return false;
    }
    switch (scan_response.result()) {
      case lorgnette::OPERATION_RESULT_SUCCESS:
        break;
      case lorgnette::OPERATION_RESULT_ADF_EMPTY:
        std::cout << "ADF is empty" << std::endl;
        return page > 1;  // If we already read a page, scanning succeeded.
        break;
      default:
        std::cerr << "StartPreparedScan returned error result "
                  << lorgnette::OperationResult_Name(scan_response.result())
                  << std::endl;
        return false;
    }

    if (!ReadNextDocument(manager, scan_response.job_handle(), output_path)) {
      return false;
    }
    ++page;
  } while (more_pages);

  return true;
}

}  // namespace lorgnette::cli
