// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/cli/scan_handler.h"

#include <cctype>
#include <iostream>
#include <memory>

#include <base/files/file.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <brillo/errors/error.h>

#include "lorgnette/cli/file_pattern.h"

namespace lorgnette::cli {

namespace {

// Some scanners do not respond to sane_start() or sane_read() until the
// hardware has scanned a page.  Wait extra time for the related d-bus calls.
constexpr int kScanTimeoutMs = 300000;

std::string ExtensionForFormat(lorgnette::ImageFormat image_format) {
  switch (image_format) {
    case lorgnette::IMAGE_FORMAT_PNG:
      return "png";
    case lorgnette::IMAGE_FORMAT_JPEG:
      return "jpg";
    default:
      LOG(ERROR) << "No extension for format " << image_format;
      return "raw";
  }
}

}  // namespace

ScanHandler::ScanHandler(base::RepeatingClosure quit_closure,
                         ManagerProxy* manager,
                         std::string scanner_name,
                         std::string output_pattern)
    : AsyncHandler(quit_closure, manager),
      scanner_name_(std::move(scanner_name)),
      output_pattern_(std::move(output_pattern)) {}

ScanHandler::~ScanHandler() = default;

void ScanHandler::ConnectSignal() {
  manager_->RegisterScanStatusChangedSignalHandler(
      base::BindRepeating(&ScanHandler::HandleScanStatusChangedSignal,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&ScanHandler::OnConnectedCallback,
                     weak_factory_.GetWeakPtr()));
}

bool ScanHandler::StartScan(
    uint32_t resolution,
    const lorgnette::DocumentSource& scan_source,
    const std::optional<lorgnette::ScanRegion>& scan_region,
    lorgnette::ColorMode color_mode,
    lorgnette::ImageFormat image_format) {
  lorgnette::StartScanRequest request;
  request.set_device_name(scanner_name_);
  request.mutable_settings()->set_resolution(resolution);
  request.mutable_settings()->set_source_name(scan_source.name());
  request.mutable_settings()->set_color_mode(color_mode);
  if (scan_region.has_value())
    *request.mutable_settings()->mutable_scan_region() = scan_region.value();
  request.mutable_settings()->set_image_format(image_format);
  format_extension_ = ExtensionForFormat(image_format);

  brillo::ErrorPtr error;
  lorgnette::StartScanResponse response;
  if (!manager_->StartScan(request, &response, &error, kScanTimeoutMs)) {
    LOG(ERROR) << "StartScan failed: " << error->GetMessage();
    return false;
  }

  if (response.state() == lorgnette::SCAN_STATE_FAILED) {
    LOG(ERROR) << "StartScan failed: " << response.failure_reason();
    return false;
  }

  std::cout << "Scan " << response.scan_uuid() << " started successfully"
            << std::endl;
  scan_uuid_ = response.scan_uuid();

  RequestNextPage();
  return true;
}

void ScanHandler::HandleScanStatusChangedSignal(
    const lorgnette::ScanStatusChangedSignal& signal) {
  if (!scan_uuid_.has_value()) {
    return;
  }

  if (signal.state() == lorgnette::SCAN_STATE_IN_PROGRESS) {
    std::cout << "Page " << signal.page() << " is " << signal.progress()
              << "% finished" << std::endl;
  } else if (signal.state() == lorgnette::SCAN_STATE_FAILED) {
    LOG(ERROR) << "Scan failed: " << signal.failure_reason();
    quit_closure_.Run();
  } else if (signal.state() == lorgnette::SCAN_STATE_PAGE_COMPLETED) {
    std::cout << "Page " << signal.page() << " completed." << std::endl;
    current_page_ += 1;
    if (signal.more_pages())
      RequestNextPage();
  } else if (signal.state() == lorgnette::SCAN_STATE_COMPLETED) {
    std::cout << "Scan completed successfully." << std::endl;
    quit_closure_.Run();
  } else if (signal.state() == lorgnette::SCAN_STATE_CANCELLED) {
    std::cout << "Scan cancelled." << std::endl;
    quit_closure_.Run();
  }
}

std::optional<lorgnette::GetNextImageResponse> ScanHandler::GetNextImage(
    const base::FilePath& output_path) {
  lorgnette::GetNextImageRequest request;
  request.set_scan_uuid(scan_uuid_.value());

  base::File output_file(
      output_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);

  if (!output_file.IsValid()) {
    PLOG(ERROR) << "Failed to open output file " << output_path;
    return std::nullopt;
  }

  brillo::ErrorPtr error;
  lorgnette::GetNextImageResponse response;
  if (!manager_->GetNextImage(request,
                              base::ScopedFD(output_file.TakePlatformFile()),
                              &response, &error, kScanTimeoutMs)) {
    LOG(ERROR) << "GetNextImage failed: " << error->GetMessage();
    return std::nullopt;
  }

  return response;
}

void ScanHandler::RequestNextPage() {
  base::FilePath output_path = ExpandPattern(output_pattern_, current_page_,
                                             scanner_name_, format_extension_);

  std::optional<lorgnette::GetNextImageResponse> response =
      GetNextImage(output_path);
  if (!response.has_value()) {
    quit_closure_.Run();
  }

  if (!response.value().success()) {
    LOG(ERROR) << "Requesting next page failed: "
               << response.value().failure_reason();
    quit_closure_.Run();
  } else {
    std::cout << "Reading page " << current_page_ << " to "
              << output_path.value() << std::endl;
  }
}

}  // namespace lorgnette::cli
