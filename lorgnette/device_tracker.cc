// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/device_tracker.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/containers/contains.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>

#include "lorgnette/firewall_manager.h"
#include "lorgnette/manager.h"
#include "lorgnette/sane_client.h"
#include "lorgnette/scanner_match.h"
#include "lorgnette/usb/libusb_wrapper.h"
#include "lorgnette/usb/usb_device.h"
#include "lorgnette/uuid_util.h"

namespace lorgnette {

namespace {

lorgnette::OperationResult ToOperationResult(SANE_Status status) {
  switch (status) {
    case SANE_STATUS_GOOD:
      return lorgnette::OPERATION_RESULT_SUCCESS;
    case SANE_STATUS_UNSUPPORTED:
      return lorgnette::OPERATION_RESULT_UNSUPPORTED;
    case SANE_STATUS_CANCELLED:
      return lorgnette::OPERATION_RESULT_CANCELLED;
    case SANE_STATUS_DEVICE_BUSY:
      return lorgnette::OPERATION_RESULT_DEVICE_BUSY;
    case SANE_STATUS_INVAL:
      return lorgnette::OPERATION_RESULT_INVALID;
    case SANE_STATUS_EOF:
      return lorgnette::OPERATION_RESULT_EOF;
    case SANE_STATUS_JAMMED:
      return lorgnette::OPERATION_RESULT_ADF_JAMMED;
    case SANE_STATUS_NO_DOCS:
      return lorgnette::OPERATION_RESULT_ADF_EMPTY;
    case SANE_STATUS_COVER_OPEN:
      return lorgnette::OPERATION_RESULT_COVER_OPEN;
    case SANE_STATUS_IO_ERROR:
      return lorgnette::OPERATION_RESULT_IO_ERROR;
    case SANE_STATUS_NO_MEM:
      return lorgnette::OPERATION_RESULT_NO_MEMORY;
    case SANE_STATUS_ACCESS_DENIED:
      return lorgnette::OPERATION_RESULT_ACCESS_DENIED;
    default:
      LOG(ERROR) << "Unexpected SANE_Status " << status << ": "
                 << sane_strstatus(status);
      return lorgnette::OPERATION_RESULT_INTERNAL_ERROR;
  }
}

}  // namespace

DeviceTracker::DeviceTracker(SaneClient* sane_client, LibusbWrapper* libusb)
    : sane_client_(sane_client), libusb_(libusb) {
  DCHECK(sane_client_);
  DCHECK(libusb_);
}

DeviceTracker::~DeviceTracker() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void DeviceTracker::SetScannerListChangedSignalSender(
    ScannerListChangedSignalSender sender) {
  signal_sender_ = sender;
}

void DeviceTracker::SetFirewallManager(FirewallManager* firewall_manager) {
  firewall_manager_ = firewall_manager;
}

size_t DeviceTracker::NumActiveDiscoverySessions() const {
  return discovery_sessions_.size();
}

base::Time DeviceTracker::LastDiscoverySessionActivity() const {
  base::Time activity = base::Time::UnixEpoch();
  for (const auto& session : discovery_sessions_) {
    if (session.second.start_time > activity) {
      activity = session.second.start_time;
    }
  }
  return activity;
}

size_t DeviceTracker::NumOpenScanners() const {
  return open_scanners_.size();
}

base::Time DeviceTracker::LastOpenScannerActivity() const {
  base::Time activity = base::Time::UnixEpoch();
  for (const auto& scanner : open_scanners_) {
    if (scanner.second.start_time > activity) {
      activity = scanner.second.start_time;
    }
    // TODO(bmgordon): Include subsequent timestamps once more operations
    // are implemented.
  }
  return activity;
}

StartScannerDiscoveryResponse DeviceTracker::StartScannerDiscovery(
    const StartScannerDiscoveryRequest& request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  StartScannerDiscoveryResponse response;
  std::string client_id = request.client_id();
  if (client_id.empty()) {
    LOG(ERROR) << __func__
               << ": Missing client_id in StartScannerDiscovery request";
    return response;
  }

  std::string session_id;
  for (auto& kv : discovery_sessions_) {
    if (kv.second.client_id == client_id) {
      // TODO(b/274860789): Clean up open scanner handles and jobs if
      // the same client requests a new discovery session.
      session_id = kv.first;
      LOG(INFO) << __func__ << ": Reusing existing discovery session "
                << session_id << " for client " << client_id;
      break;
    }
  }
  if (session_id.empty()) {
    session_id = GenerateUUID();
    LOG(INFO) << __func__ << ": Starting new discovery session " << session_id
              << " for client " << client_id;
  }
  DiscoverySessionState& session = discovery_sessions_[session_id];
  session.client_id = client_id;
  session.start_time = base::Time::Now();
  session.dlc_policy = request.download_policy();
  session.dlc_started = false;
  session.local_only = request.local_only();
  session.preferred_only = request.preferred_only();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DeviceTracker::StartDiscoverySessionInternal,
                                weak_factory_.GetWeakPtr(), session_id));

  response.set_started(true);
  response.set_session_id(session_id);
  return response;
}

StopScannerDiscoveryResponse DeviceTracker::StopScannerDiscovery(
    const StopScannerDiscoveryRequest& request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  StopScannerDiscoveryResponse response;
  std::string session_id = request.session_id();
  if (session_id.empty()) {
    LOG(ERROR) << __func__ << ": Missing session_id in request";
    return response;
  }

  discovery_sessions_.erase(session_id);
  SendSessionEndingSignal(session_id);

  response.set_stopped(true);
  return response;
}

std::optional<DeviceTracker::DiscoverySessionState*> DeviceTracker::GetSession(
    const std::string& session_id) {
  if (session_id.empty()) {
    LOG(ERROR) << "Missing session id";
    return std::nullopt;
  }

  if (!base::Contains(discovery_sessions_, session_id)) {
    LOG(ERROR) << "No active session found for session_id=" << session_id;
    return std::nullopt;
  }

  return &discovery_sessions_.at(session_id);
}

void DeviceTracker::StartDiscoverySessionInternal(std::string session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto maybe_session = GetSession(session_id);
  if (!maybe_session) {
    LOG(ERROR) << __func__ << ": Failed to get session " << session_id;
    return;
  }
  DiscoverySessionState* session = *maybe_session;

  LOG(INFO) << __func__ << ": Starting discovery session " << session_id;

  if (!session->local_only) {
    session->port_token = std::make_unique<PortToken>(
        firewall_manager_->RequestPixmaPortAccess());
  }

  if (session->dlc_policy == BackendDownloadPolicy::DOWNLOAD_ALWAYS) {
    // TODO(rishabhagr): Kick off background DLC download.
    session->dlc_started = true;
  }

  // If a previous session already discovered some devices, go ahead and send
  // signals for those right away.  Any newly-plugged devices will be added
  // later when we re-enumerate everything.
  for (const auto& device : known_devices_) {
    ScannerListChangedSignal signal;
    signal.set_event_type(ScannerListChangedSignal::SCANNER_ADDED);
    signal.set_session_id(session_id);
    *signal.mutable_scanner() = device;
    signal_sender_.Run(signal);
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DeviceTracker::EnumerateUSBDevices,
                                weak_factory_.GetWeakPtr(), session_id));
}

void DeviceTracker::EnumerateUSBDevices(std::string session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto maybe_session = GetSession(session_id);
  if (!maybe_session) {
    LOG(ERROR) << __func__ << ": Failed to get session " << session_id;
    return;
  }
  DiscoverySessionState* session = *maybe_session;

  LOG(INFO) << __func__ << ": Enumerating USB devices for " << session_id;

  for (auto& device : libusb_->GetDevices()) {
    if (!session->dlc_started && device->NeedsNonBundledBackend()) {
      // TODO(rishabhagr): Kick off background DLC download.
      session->dlc_started = true;
    }
    if (device->SupportsIppUsb()) {
      LOG(INFO) << __func__ << ": Device " << device->Description()
                << " supports IPP-USB and needs to be probed";
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&DeviceTracker::ProbeIPPUSBDevice,
                                    weak_factory_.GetWeakPtr(), session_id,
                                    std::move(device)));
    }
  }

  if (session->dlc_started) {
    LOG(INFO) << __func__ << ": Waiting for DLC to finish";
    // TODO(rishabhagr): Track that DLC completion needs to run
    // EnumerateSANEDevices to continue.
  } else {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DeviceTracker::EnumerateSANEDevices,
                                  weak_factory_.GetWeakPtr(), session_id));
  }
}

void DeviceTracker::ProbeIPPUSBDevice(std::string session_id,
                                      std::unique_ptr<UsbDevice> device) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto maybe_session = GetSession(session_id);
  if (!maybe_session) {
    LOG(ERROR) << __func__ << ": Failed to get session " << session_id;
    return;
  }

  LOG(INFO) << __func__ << ": Probing IPP-USB device " << device->Description()
            << " for " << session_id;

  std::optional<ScannerInfo> scanner_info = device->IppUsbScannerInfo();
  if (!scanner_info) {
    LOG(ERROR) << __func__ << ": Unable to get scanner info from device "
               << device->Description();
    return;
  }

  LOG(INFO) << __func__ << ": Attempting eSCL connection for "
            << device->Description() << " at " << scanner_info->name();
  brillo::ErrorPtr error;
  SANE_Status status;
  std::unique_ptr<SaneDevice> sane_device =
      sane_client_->ConnectToDevice(&error, &status, scanner_info->name());
  if (!sane_device) {
    LOG(ERROR) << __func__ << ": Failed to open device "
               << device->Description() << " as " << scanner_info->name()
               << ": " << sane_strstatus(status);
    return;
  }

  // TODO(b/277049537): Fetch device UUID from the scanner.

  LOG(INFO) << __func__ << ": Device " << device->Description()
            << " supports eSCL over IPP-USB at " << scanner_info->name();
  ScannerListChangedSignal signal;
  signal.set_event_type(ScannerListChangedSignal::SCANNER_ADDED);
  signal.set_session_id(session_id);
  *signal.mutable_scanner() = *scanner_info;
  signal_sender_.Run(signal);

  std::string bus_dev = base::StringPrintf("%03d:%03d", device->GetBusNumber(),
                                           device->GetDeviceAddress());
  std::string vid_pid =
      base::StringPrintf("%04x:%04x", device->GetVid(), device->GetPid());
  known_bus_devs_.insert(bus_dev);
  known_vid_pids_.insert(vid_pid);
  known_devices_.push_back(std::move(*scanner_info));
}

void DeviceTracker::EnumerateSANEDevices(std::string session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto maybe_session = GetSession(session_id);
  if (!maybe_session) {
    LOG(ERROR) << __func__ << ": Failed to get session " << session_id;
    return;
  }

  LOG(INFO) << __func__ << ": Checking for SANE devices in " << session_id;

  brillo::ErrorPtr error_ptr;
  std::optional<std::vector<ScannerInfo>> devices =
      sane_client_->ListDevices(&error_ptr);

  if (!devices.has_value()) {
    LOG(ERROR) << __func__ << ": Failed to get SANE devices";
    return;
  }

  for (const ScannerInfo& scanner_info : devices.value()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&DeviceTracker::ProbeSANEDevice,
                       weak_factory_.GetWeakPtr(), session_id, scanner_info));
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DeviceTracker::SendEnumerationCompletedSignal,
                                weak_factory_.GetWeakPtr(), session_id));
}

void DeviceTracker::ProbeSANEDevice(std::string session_id,
                                    const ScannerInfo& scanner_info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto maybe_session = GetSession(session_id);
  if (!maybe_session) {
    LOG(ERROR) << __func__ << ": Failed to get session " << session_id;
    return;
  }

  LOG(INFO) << __func__ << ": Probing SANE device for " << session_id;

  if (!Manager::ScannerCanBeUsed(scanner_info)) {
    return;
  }

  DiscoverySessionState* session = *maybe_session;

  // The preferred_only flag tells us whether or not we want to drop any
  // duplicates of IPP-USB devices that were already discovered.
  if (session->preferred_only) {
    if (DuplicateScannerExists(scanner_info.name(), known_vid_pids_,
                               known_bus_devs_)) {
      return;
    }
  }

  ScannerListChangedSignal signal;
  signal.set_event_type(ScannerListChangedSignal::SCANNER_ADDED);
  signal.set_session_id(session_id);
  *signal.mutable_scanner() = scanner_info;

  known_devices_.push_back(std::move(scanner_info));
  signal_sender_.Run(signal);
}

void DeviceTracker::SendEnumerationCompletedSignal(std::string session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto maybe_session = GetSession(session_id);
  if (!maybe_session) {
    LOG(ERROR) << __func__ << ": Failed to get session " << session_id;
    return;
  }

  LOG(INFO) << __func__ << ": Enumeration completed for " << session_id;

  ScannerListChangedSignal signal;
  signal.set_event_type(ScannerListChangedSignal::ENUM_COMPLETE);
  signal.set_session_id(session_id);
  signal_sender_.Run(signal);
}

void DeviceTracker::SendSessionEndingSignal(std::string session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (session_id.empty()) {
    LOG(ERROR) << __func__ << ": Missing session id";
  }
  LOG(INFO) << __func__ << ": Session ending for " << session_id;

  // Deliberately don't check for an active session.  This lets us
  // notify ended sessions even if lorgnette has restarted.

  ScannerListChangedSignal signal;
  signal.set_event_type(ScannerListChangedSignal::SESSION_ENDING);
  signal.set_session_id(session_id);
  signal_sender_.Run(signal);
}

OpenScannerResponse DeviceTracker::OpenScanner(
    const OpenScannerRequest& request) {
  const std::string& connection_string =
      request.scanner_id().connection_string();
  LOG(INFO) << __func__ << ": Opening device: " << connection_string;

  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  OpenScannerResponse response;
  *response.mutable_scanner_id() = request.scanner_id();
  response.set_result(OPERATION_RESULT_INVALID);
  if (connection_string.empty()) {
    LOG(ERROR) << __func__ << ": OpenScannerRequest missing connection_string";
    return response;
  }
  if (request.client_id().empty()) {
    LOG(ERROR) << __func__ << ": OpenScannerRequest missing client_id";
    return response;
  }

  for (const auto& scanner : open_scanners_) {
    if (scanner.second.connection_string != connection_string) {
      continue;
    }

    if (scanner.second.client_id != request.client_id()) {
      LOG(WARNING) << __func__ << ": Device is already open by client "
                   << scanner.second.client_id;
      response.set_result(OPERATION_RESULT_DEVICE_BUSY);
      return response;
    }

    LOG(WARNING) << __func__
                 << ": Closing existing handle owned by same client: "
                 << scanner.first;
    // TODO(bmgordon): Cancel outstanding jobs.
    open_scanners_.erase(scanner);
    break;
  }

  OpenScannerState state;
  state.client_id = request.client_id();
  state.connection_string = connection_string;
  state.handle = GenerateUUID();
  state.start_time = base::Time::Now();
  // TODO(bmgordon): Request the PortToken from the firewall if needed.
  brillo::ErrorPtr error;
  SANE_Status status;
  auto device =
      sane_client_->ConnectToDevice(&error, &status, connection_string);
  if (!device) {
    LOG(ERROR) << __func__ << ": Failed to open device " << connection_string
               << ": " << error->GetMessage();
    response.set_result(ToOperationResult(status));
    return response;
  }

  std::optional<ScannerConfig> config = device->GetCurrentConfig(&error);
  if (!config.has_value()) {
    LOG(ERROR) << __func__ << ": Unable to get current scanner config: "
               << error->GetMessage();
    response.set_result(OPERATION_RESULT_INTERNAL_ERROR);
    return response;
  }
  config->mutable_scanner()->set_token(state.handle);

  LOG(INFO) << __func__ << ": Started tracking open scanner " << state.handle
            << " for client " << state.client_id
            << ".  Active scanners: " << open_scanners_.size() + 1;
  state.device = std::move(device);
  state.last_activity = base::Time::Now();
  open_scanners_.emplace(state.handle, std::move(state));

  *response.mutable_config() = std::move(config.value());
  response.set_result(OPERATION_RESULT_SUCCESS);
  return response;
}

CloseScannerResponse DeviceTracker::CloseScanner(
    const CloseScannerRequest& request) {
  LOG(INFO) << __func__ << ": Closing device: " << request.scanner().token();

  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  CloseScannerResponse response;
  *response.mutable_scanner() = request.scanner();

  if (!request.has_scanner() || request.scanner().token().empty()) {
    LOG(ERROR) << __func__ << ": CloseScannerRequest is missing scanner handle";
    response.set_result(OPERATION_RESULT_INVALID);
    return response;
  }
  const std::string& handle = request.scanner().token();

  if (!base::Contains(open_scanners_, handle)) {
    LOG(WARNING) << __func__
                 << ": Attempting to close handle that does not exist: "
                 << handle;
    response.set_result(OPERATION_RESULT_MISSING);
    return response;
  }

  // TODO(bmgordon): Cancel any outstanding scan jobs.
  open_scanners_.erase(handle);
  LOG(INFO) << __func__ << ": Stopped tracking scanner " << handle
            << ".  Active scanners: " << open_scanners_.size();
  response.set_result(OPERATION_RESULT_SUCCESS);
  return response;
}

}  // namespace lorgnette
