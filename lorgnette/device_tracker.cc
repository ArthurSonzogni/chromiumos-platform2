// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/device_tracker.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/containers/contains.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <re2/re2.h>

#include "lorgnette/constants.h"
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

DeviceTracker::ScanBuffer::ScanBuffer()
    : data(nullptr), len(0), pos(0), writer(nullptr) {}

DeviceTracker::ScanBuffer::~ScanBuffer() {
  if (writer) {
    fclose(writer);
  }
  if (data) {
    free(data);
  }
}

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

  // TODO(b/311196232): Load saved devices IDs and prepopulate
  // canonical_scanners_.

  std::string session_id;
  for (auto& kv : discovery_sessions_) {
    if (kv.second.client_id == client_id) {
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

  // Close any open scanner handles owned by the same client.  This needs to be
  // done whether the session is new or not because the client could have opened
  // a scanner without an active discovery session previously.
  for (auto it = open_scanners_.begin(); it != open_scanners_.end();) {
    if (it->second.client_id == client_id) {
      // Deleting the state object closes the scanner handle.
      LOG(INFO) << __func__
                << ": Closing existing scanner open by same client: "
                << it->second.handle << " (" << it->second.connection_string
                << ")";
      // TODO(bmgordon): Make sure outstanding job handles are cancelled.
      it = open_scanners_.erase(it);
    } else {
      ++it;
    }
  }

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

void DeviceTracker::SendScannerAddedSignal(std::string session_id,
                                           ScannerInfo scanner) {
  ScannerListChangedSignal signal;
  signal.set_event_type(ScannerListChangedSignal::SCANNER_ADDED);
  signal.set_session_id(std::move(session_id));
  *signal.mutable_scanner() = std::move(scanner);
  signal_sender_.Run(signal);
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

  // If this device was already discovered in a previous session, return it
  // without further probing.
  for (const auto& known_dev : known_devices_) {
    if (known_dev.name() == scanner_info->name()) {
      LOG(INFO) << __func__
                << ": Returning entry from cache: " << known_dev.name();
      SendScannerAddedSignal(std::move(session_id), known_dev);
      return;
    }
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

  for (const std::string& format : sane_device->GetSupportedFormats()) {
    *scanner_info->add_image_format() = format;
  }

  // IPP-USB devices are probed first and the previous check didn't find a
  // matching known device.  Therefore we can generate a UUID here without
  // checking to see if it matches a previous non-eSCL USB device.
  // TODO(b/311196232): Replace generated UUID with the eSCL UUID fetched from
  // the scanner.
  scanner_info->set_device_uuid(GenerateUUID());

  LOG(INFO) << __func__ << ": Device " << device->Description()
            << " supports eSCL over IPP-USB at " << scanner_info->name();
  SendScannerAddedSignal(session_id, *scanner_info);

  canonical_scanners_.AddUsbDevice(*device, scanner_info->name());
  known_devices_.push_back(std::move(*scanner_info));
}

void DeviceTracker::EnumerateSANEDevices(std::string session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto maybe_session = GetSession(session_id);
  if (!maybe_session) {
    LOG(ERROR) << __func__ << ": Failed to get session " << session_id;
    return;
  }
  DiscoverySessionState* session = maybe_session.value();

  LOG(INFO) << __func__ << ": Checking for SANE devices in " << session_id;

  brillo::ErrorPtr error_ptr;
  std::optional<std::vector<ScannerInfo>> devices =
      sane_client_->ListDevices(&error_ptr, session->local_only);

  if (!devices.has_value()) {
    LOG(ERROR) << __func__ << ": Failed to get SANE devices";
    // Loop over nothing so we can still tell the client the session ended.
    devices = std::vector<ScannerInfo>{};
  }

  for (ScannerInfo& scanner_info : devices.value()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DeviceTracker::ProbeSANEDevice,
                                  weak_factory_.GetWeakPtr(), session_id,
                                  std::move(scanner_info)));
  }

  // TODO(b/311196232): Persist the set of active device IDs to somewhere under
  // /run/lorgnette so they can remain stable across lorgnette runs within the
  // same login session.

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DeviceTracker::SendEnumerationCompletedSignal,
                                weak_factory_.GetWeakPtr(), session_id));
}

void DeviceTracker::ProbeSANEDevice(std::string session_id,
                                    ScannerInfo scanner_info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto maybe_session = GetSession(session_id);
  if (!maybe_session) {
    LOG(ERROR) << __func__ << ": Failed to get session " << session_id;
    return;
  }

  LOG(INFO) << __func__ << ": Probing SANE device " << scanner_info.name()
            << " for " << session_id;

  if (!Manager::ScannerCanBeUsed(scanner_info)) {
    return;
  }

  DiscoverySessionState* session = *maybe_session;

  // Don't waste time checking network scanners if only local scanners are
  // requested.
  if (session->local_only &&
      scanner_info.connection_type() != lorgnette::CONNECTION_USB) {
    return;
  }

  // The preferred_only flag tells us whether or not we want to drop any
  // duplicates of IPP-USB devices that were already discovered.
  std::string canonical_name = canonical_scanners_.LookupScanner(scanner_info);
  if (session->preferred_only && canonical_name.starts_with("ippusb:")) {
    return;
  }

  // If this device was already discovered in a previous session, return it
  // without further probing.
  for (const auto& known_dev : known_devices_) {
    if (known_dev.name() == scanner_info.name()) {
      LOG(INFO) << __func__
                << ": Returning entry from cache: " << known_dev.name();
      SendScannerAddedSignal(std::move(session_id), known_dev);
      return;
    }
  }

  // Open the device so we can fetch supported image types.
  brillo::ErrorPtr error;
  SANE_Status status;
  std::unique_ptr<SaneDevice> device =
      sane_client_->ConnectToDevice(&error, &status, scanner_info.name());
  if (!device) {
    LOG(ERROR) << __func__ << ": Failed to open device " << scanner_info.name()
               << ": " << error->GetMessage();
    return;
  }
  for (const std::string& format : device->GetSupportedFormats()) {
    *scanner_info.add_image_format() = format;
  }

  // If we can map this to an existing device, copy the deviceUuid.  If there
  // wasn't a previous device ID match, generate one.
  std::string device_id;
  if (!canonical_name.empty()) {
    for (const auto& known_dev : known_devices_) {
      if (known_dev.name() == canonical_name) {
        device_id = known_dev.device_uuid();
        break;
      }
    }
  }
  if (device_id.empty()) {
    device_id = GenerateUUID();
  }
  scanner_info.set_device_uuid(device_id);

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
  state.completed_lines = 0;
  state.expected_lines = 0;
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

SetOptionsResponse DeviceTracker::SetOptions(const SetOptionsRequest& request) {
  LOG(INFO) << __func__ << ": Setting " << request.options().size()
            << " options for device: " << request.scanner().token();

  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  SetOptionsResponse response;
  *response.mutable_scanner() = request.scanner();

  if (!request.has_scanner() || request.scanner().token().empty()) {
    LOG(ERROR) << __func__ << ": SetOptionsRequest is missing scanner handle";
    for (const auto& option : request.options()) {
      (*response.mutable_results())[option.name()] = OPERATION_RESULT_INVALID;
    }
    return response;
  }
  const std::string& handle = request.scanner().token();

  if (!base::Contains(open_scanners_, handle)) {
    LOG(ERROR) << __func__ << ": No open handle: " << handle;
    for (const auto& option : request.options()) {
      (*response.mutable_results())[option.name()] = OPERATION_RESULT_MISSING;
    }
    return response;
  }
  OpenScannerState& state = open_scanners_[handle];
  state.last_activity = base::Time::Now();

  size_t succeeded = 0;
  size_t failed = 0;
  for (const ScannerOption& option : request.options()) {
    brillo::ErrorPtr error;
    SANE_Status status = state.device->SetOption(&error, option);
    (*response.mutable_results())[option.name()] = ToOperationResult(status);
    if (status == SANE_STATUS_GOOD) {
      ++succeeded;
    } else {
      LOG(WARNING) << __func__ << ": Failed to set option " << option.name()
                   << ": " << error->GetMessage();
      ++failed;
      // continue with remaining options
    }
  }

  brillo::ErrorPtr error;
  std::optional<ScannerConfig> config = state.device->GetCurrentConfig(&error);
  if (!config.has_value()) {
    LOG(ERROR) << __func__
               << ": Unable to get new scanner config: " << error->GetMessage();
    for (const auto& option : request.options()) {
      (*response.mutable_results())[option.name()] =
          OPERATION_RESULT_INTERNAL_ERROR;
    }
    return response;
  }

  LOG(INFO) << __func__ << ": Done with succeeded=" << succeeded
            << ", failed=" << failed << ". New config has "
            << config->options().size() << " options";

  *config->mutable_scanner() = request.scanner();
  *response.mutable_config() = std::move(config.value());
  return response;
}

GetCurrentConfigResponse DeviceTracker::GetCurrentConfig(
    const GetCurrentConfigRequest& request) {
  LOG(INFO) << __func__ << ": Getting current config for device: "
            << request.scanner().token();

  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  GetCurrentConfigResponse response;
  *response.mutable_scanner() = request.scanner();

  if (!request.has_scanner() || request.scanner().token().empty()) {
    LOG(ERROR) << __func__
               << ": GetCurrentConfigRequest is missing scanner handle";
    response.set_result(OPERATION_RESULT_INVALID);
    return response;
  }
  const std::string& handle = request.scanner().token();

  if (!base::Contains(open_scanners_, handle)) {
    LOG(ERROR) << __func__ << ": No open handle: " << handle;
    response.set_result(OPERATION_RESULT_MISSING);
    return response;
  }
  OpenScannerState& state = open_scanners_[handle];
  state.last_activity = base::Time::Now();

  brillo::ErrorPtr error;
  std::optional<ScannerConfig> config = state.device->GetCurrentConfig(&error);
  if (!config.has_value()) {
    LOG(ERROR) << __func__
               << ": Unable to get scanner config: " << error->GetMessage();
    response.set_result(OPERATION_RESULT_INTERNAL_ERROR);
    return response;
  }

  LOG(INFO) << __func__ << ": Done retrieving scanner config";

  response.set_result(OPERATION_RESULT_SUCCESS);
  *response.mutable_config() = std::move(config.value());
  return response;
}

StartPreparedScanResponse DeviceTracker::StartPreparedScan(
    const StartPreparedScanRequest& request) {
  LOG(INFO) << __func__
            << ": Scan requested on device: " << request.scanner().token();

  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  StartPreparedScanResponse response;
  *response.mutable_scanner() = request.scanner();

  if (!request.has_scanner() || request.scanner().token().empty()) {
    LOG(ERROR) << __func__
               << ": StartPreparedScanRequest is missing scanner handle";
    response.set_result(OPERATION_RESULT_INVALID);
    return response;
  }
  const std::string& handle = request.scanner().token();

  if (!base::Contains(open_scanners_, handle)) {
    LOG(WARNING) << __func__ << ": No open handle: " << handle;
    response.set_result(OPERATION_RESULT_MISSING);
    return response;
  }
  OpenScannerState& state = open_scanners_[handle];
  state.last_activity = base::Time::Now();

  if (request.image_format().empty() ||
      !base::Contains(state.device->GetSupportedFormats(),
                      request.image_format())) {
    LOG(ERROR) << __func__ << ": Unsupported image format requested: "
               << request.image_format();
    response.set_result(OPERATION_RESULT_INVALID);
    return response;
  }

  std::optional<std::string> job_id = state.device->GetCurrentJob();
  if (job_id.has_value()) {
    LOG(WARNING) << __func__ << ": Canceling existing job " << job_id.value();
    brillo::ErrorPtr error;
    if (!state.device->CancelScan(&error)) {
      LOG(WARNING) << __func__ << ": Failed to cancel scan " << job_id.value()
                   << ": " << error->GetMessage();
      // Continue because starting a new scan may reset the backend's state.
      // If it doesn't, we'll return an error from StartScan() later.
    }
    active_jobs_.erase(job_id.value());
  }

  state.completed_lines = 0;
  state.expected_lines = 0;

  auto buffer = std::make_unique<ScanBuffer>();
  buffer->writer = open_memstream(&buffer->data, &buffer->len);
  if (!buffer->writer) {
    LOG(ERROR) << __func__ << ": Failed to allocate scan buffer";
    response.set_result(OPERATION_RESULT_NO_MEMORY);
    return response;
  }

  ImageFormat format;
  if (request.image_format() == kJpegMimeType) {
    format = IMAGE_FORMAT_JPEG;
  } else if (request.image_format() == kPngMimeType) {
    format = IMAGE_FORMAT_PNG;
  } else {
    // TODO(bmgordon): Support additional pass-through image formats.
    LOG(ERROR) << __func__ << ": Unrecognized image format "
               << request.image_format();
    response.set_result(OPERATION_RESULT_INTERNAL_ERROR);
    return response;
  }

  brillo::ErrorPtr error;
  SANE_Status status = state.device->StartScan(&error);
  if (status != SANE_STATUS_GOOD) {
    LOG(ERROR) << __func__ << ": Failed to start scan on device " << handle
               << ": " << sane_strstatus(status);
    response.set_result(ToOperationResult(status));
    return response;
  }

  job_id = state.device->GetCurrentJob();
  if (!job_id.has_value()) {
    LOG(ERROR) << __func__ << ": Job was started, but no ID available";
    response.set_result(OPERATION_RESULT_INTERNAL_ERROR);

    // Try to cancel the scan since the user can't do anything with it.  We're
    // already returning an error, so don't do anything with the result.
    state.device->CancelScan(nullptr);

    return response;
  }

  size_t expected_lines;
  status = state.device->PrepareImageReader(&error, format, buffer->writer,
                                            &expected_lines);
  if (status != SANE_STATUS_GOOD) {
    LOG(ERROR) << __func__ << ": Failed to create image reader for device "
               << handle << ": " << sane_strstatus(status);
    response.set_result(ToOperationResult(status));

    // Try to cancel the scan since the user can't do anything with it.  We're
    // already returning an error, so don't do anything with the result.
    state.device->CancelScan(nullptr);

    return response;
  }

  JobHandle job;
  job.set_token(job_id.value());
  active_jobs_[job_id.value()] = handle;
  state.buffer = std::move(buffer);
  state.expected_lines = expected_lines;

  LOG(INFO) << __func__ << ": Started scan job " << job_id.value()
            << " on device " << handle;
  response.set_result(OPERATION_RESULT_SUCCESS);
  *response.mutable_job_handle() = std::move(job);
  return response;
}

CancelScanResponse DeviceTracker::CancelScan(const CancelScanRequest& request) {
  CHECK(request.has_job_handle())
      << "Manager::CancelScan must be used to cancel by UUID";

  LOG(INFO) << __func__
            << ": Cancel requested for job: " << request.job_handle().token();

  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  CancelScanResponse response;
  *response.mutable_job_handle() = request.job_handle();

  if (request.job_handle().token().empty()) {
    LOG(ERROR) << __func__ << ": CancelScanRequest is missing job handle";
    response.set_result(OPERATION_RESULT_INVALID);
    response.set_failure_reason("CancelScan request is missing job handle");
    return response;
  }
  if (!request.scan_uuid().empty()) {
    LOG(WARNING) << __func__
                 << ": Request with job handle will ignore redundant UUID: "
                 << request.scan_uuid();
  }
  const std::string& job_handle = request.job_handle().token();

  if (!base::Contains(active_jobs_, job_handle)) {
    LOG(ERROR) << __func__ << ": No job found for handle " << job_handle;
    response.set_failure_reason("No scan job found for handle " + job_handle);
    response.set_result(OperationResult::OPERATION_RESULT_INVALID);
    return response;
  }
  const std::string& device_handle = active_jobs_[job_handle];

  if (!base::Contains(open_scanners_, device_handle)) {
    LOG(ERROR) << __func__ << ": No open scanner handle: " << device_handle;
    response.set_failure_reason("No open scanner found for job handle " +
                                job_handle);
    response.set_result(OPERATION_RESULT_MISSING);
    active_jobs_.erase(job_handle);
    return response;
  }
  OpenScannerState& state = open_scanners_[device_handle];
  state.last_activity = base::Time::Now();

  // If there's no job handle currently, the previous job was run to completion
  // and no new job has been started.  Go ahead and report that cancelling
  // succeeds because the end state is identical.
  if (!state.device->GetCurrentJob().has_value()) {
    LOG(WARNING) << __func__ << ": Job has already completed: " << job_handle;
    response.set_success(true);
    response.set_result(OPERATION_RESULT_SUCCESS);
    active_jobs_.erase(job_handle);
    return response;
  }

  if (state.device->GetCurrentJob() != job_handle) {
    LOG(ERROR) << __func__ << ": Job is not currently active: " << job_handle;
    response.set_failure_reason("Job has already been cancelled");
    response.set_result(OPERATION_RESULT_CANCELLED);
    active_jobs_.erase(job_handle);
    return response;
  }

  brillo::ErrorPtr error;
  if (!state.device->CancelScan(&error)) {
    LOG(ERROR) << __func__ << ": Failed to cancel job: " << error->GetMessage();
    response.set_failure_reason(error->GetMessage());
    response.set_result(OPERATION_RESULT_INTERNAL_ERROR);
    active_jobs_.erase(job_handle);
    return response;
  }

  active_jobs_.erase(job_handle);
  response.set_success(true);
  response.set_result(OPERATION_RESULT_SUCCESS);
  return response;
}

ReadScanDataResponse DeviceTracker::ReadScanData(
    const ReadScanDataRequest& request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  VLOG(1) << __func__ << ": next chunk requested for "
          << request.job_handle().token();

  ReadScanDataResponse response;
  *response.mutable_job_handle() = request.job_handle();
  response.set_result(OPERATION_RESULT_UNKNOWN);

  if (request.job_handle().token().empty()) {
    LOG(ERROR) << __func__ << ": ReadScanData request is missing job handle";
    response.set_result(OPERATION_RESULT_INVALID);
    return response;
  }
  const std::string& job_handle = request.job_handle().token();

  if (!base::Contains(active_jobs_, job_handle)) {
    LOG(ERROR) << __func__ << ": No job found for handle " << job_handle;
    response.set_result(OperationResult::OPERATION_RESULT_INVALID);
    return response;
  }
  const std::string& device_handle = active_jobs_[job_handle];

  if (!base::Contains(open_scanners_, device_handle)) {
    LOG(ERROR) << __func__ << ": No open scanner handle: " << device_handle;
    response.set_result(OPERATION_RESULT_MISSING);
    active_jobs_.erase(job_handle);
    return response;
  }
  OpenScannerState& state = open_scanners_[device_handle];
  state.last_activity = base::Time::Now();

  brillo::ErrorPtr error;
  size_t read;
  size_t rows;
  SANE_Status status = state.device->ReadEncodedData(&error, &read, &rows);
  response.set_result(ToOperationResult(status));
  state.completed_lines += rows;
  switch (status) {
    case SANE_STATUS_EOF:
      // EOF needs the same data handling as GOOD because there may be image
      // footers that haven't been transmitted yet.
      [[fallthrough]];
    case SANE_STATUS_GOOD: {
      fflush(state.buffer->writer);
      size_t encoded_len = state.buffer->len - state.buffer->pos;
      VLOG(1) << __func__ << ": Encoded bytes available: " << encoded_len;
      response.set_data(
          std::string(state.buffer->data + state.buffer->pos, encoded_len));
      response.set_estimated_completion(state.completed_lines * 100 /
                                        state.expected_lines);
      state.buffer->pos = state.buffer->len;
      if (encoded_len == 0) {
        // Rate-limit polling from the client if no data was available yet.
        base::PlatformThread::Sleep(base::Milliseconds(100));
      }
      break;
    }
    default:
      LOG(ERROR) << __func__
                 << ": Failed to read encoded data: " << error->GetMessage();
      return response;
  }

  LOG(INFO) << __func__ << ": Returning " << response.data().size()
            << " encoded bytes";
  state.last_activity = base::Time::Now();
  return response;
}

}  // namespace lorgnette
