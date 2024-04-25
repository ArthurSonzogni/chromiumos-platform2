// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/device_tracker.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include <fcntl.h>

#include <base/containers/contains.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <brillo/file_utils.h>
#include <chromeos/constants/lorgnette_dlc.h>
#include <re2/re2.h>

#include "lorgnette/constants.h"
#include "lorgnette/firewall_manager.h"
#include "lorgnette/guess_source.h"
#include "lorgnette/manager.h"
#include "lorgnette/sane_client.h"
#include "lorgnette/scanner_match.h"
#include "lorgnette/usb/libusb_wrapper.h"
#include "lorgnette/usb/usb_device.h"
#include "lorgnette/uuid_util.h"

namespace lorgnette {

namespace {

constexpr char kDefaultCacheDirectory[] = "/run/lorgnette/cache";
constexpr char kKnownDevicesFileName[] = "known_devices";
constexpr base::TimeDelta kMaxCancelWaitTime = base::Seconds(3);
constexpr base::TimeDelta kReadPollInterval = base::Milliseconds(50);
constexpr base::TimeDelta kInitialPollInterval = base::Milliseconds(250);

// 4MB max to stay under d-bus limits.
constexpr size_t kLargestMaxReadSize = 4 * 1024 * 1024;
// 32KB min to avoid excessive IPC overhead.
constexpr size_t kSmallestMaxReadSize = 32 * 1024;

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
    : cache_dir_(kDefaultCacheDirectory),
      sane_client_(sane_client),
      libusb_(libusb),
      dlc_client_(nullptr),
      dlc_started_(false),
      dlc_completed_successfully_(false),
      smallest_max_read_size_(kSmallestMaxReadSize),
      last_discovery_activity_(base::Time::UnixEpoch()) {
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

void DeviceTracker::SetSmallestMaxReadSizeForTesting(size_t size) {
  smallest_max_read_size_ = size;
}

void DeviceTracker::SetFirewallManager(FirewallManager* firewall_manager) {
  firewall_manager_ = firewall_manager;
}

void DeviceTracker::SetDlcClient(DlcClient* dlc_client) {
  dlc_client_ = dlc_client;
  dlc_client_->SetCallbacks(base::BindRepeating(&DeviceTracker::OnDlcSuccess,
                                                weak_factory_.GetWeakPtr()),
                            base::BindRepeating(&DeviceTracker::OnDlcFailure,
                                                weak_factory_.GetWeakPtr()));
}

size_t DeviceTracker::NumActiveDiscoverySessions() const {
  return discovery_sessions_.size();
}

base::Time DeviceTracker::LastDiscoverySessionActivity() const {
  base::Time activity = last_discovery_activity_;
  for (const auto& session : discovery_sessions_) {
    if (session.second.last_activity > activity) {
      activity = session.second.last_activity;
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
    // TODO(b/276909624): Update to match the behavior of
    // LastDiscoverySessionActivity.
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
  session.last_activity = base::Time::Now();
  session.dlc_policy = request.download_policy();
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
      ClearJobsForScanner(it->first);
      it = open_scanners_.erase(it);
    } else {
      ++it;
    }
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DeviceTracker::StartDiscoverySessionInternal,
                                weak_factory_.GetWeakPtr(), session_id));

  last_discovery_activity_ = base::Time::Now();
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
  last_discovery_activity_ = base::Time::Now();

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
  auto maybe_session = GetSession(session_id);
  if (maybe_session) {
    maybe_session.value()->last_activity = base::Time::Now();
  }

  ScannerListChangedSignal signal;
  signal.set_event_type(ScannerListChangedSignal::SCANNER_ADDED);
  signal.set_session_id(std::move(session_id));
  *signal.mutable_scanner() = std::move(scanner);
  signal_sender_.Run(signal);
}

void DeviceTracker::SetCacheDirectoryForTesting(base::FilePath cache_dir) {
  cache_dir_ = std::move(cache_dir);
}

void DeviceTracker::ClearKnownDevicesForTesting() {
  known_devices_.clear();
  canonical_scanners_ = ScannerMatcher();
}

void DeviceTracker::SaveDeviceCache() {
  // The list of known scanners isn't really a ListScannersResponse, but the
  // same message can be reused to store a list of ScannerInfo messages by
  // ignoring the result field.
  ListScannersResponse list;
  for (const auto& device : known_devices_) {
    *list.add_scanners() = device;
  }
  std::string serialized;
  if (!list.SerializeToString(&serialized)) {
    LOG(ERROR) << "Unable to serialize known devices";
    return;
  }

  base::FilePath cache_path = cache_dir_.Append(kKnownDevicesFileName);
  LOG(INFO) << "Saving " << list.scanners_size() << " devices to "
            << cache_path;
  brillo::WriteStringToFile(cache_path, serialized);
}

void DeviceTracker::LoadDeviceCache() {
  base::FilePath cache_path = cache_dir_.Append(kKnownDevicesFileName);
  if (!base::PathIsReadable(cache_path)) {
    return;
  }

  base::ScopedFD fd = brillo::OpenSafely(cache_path, O_RDONLY, 0);
  if (!fd.is_valid()) {
    LOG(ERROR) << "Unable to open cache file " << cache_path;
    return;
  }

  ListScannersResponse list;
  if (!list.ParseFromFileDescriptor(fd.get())) {
    LOG(ERROR) << "Unable to decode cache file";
    return;
  }

  if (list.scanners_size() == 0) {
    return;
  }

  LOG(INFO) << "Loading " << list.scanners_size() << " devices from "
            << cache_path;
  for (auto& scanner : *list.mutable_scanners()) {
    known_devices_.emplace_back(std::move(scanner));
  }
}

void DeviceTracker::StartDiscoverySessionInternal(std::string session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If there are already known devices, they would have come from a previous
  // discovery session in the running instance of lorgnette.  This means they're
  // already current, so nothing needs to be loaded.
  // If there aren't any existing entries, this may be because lorgnette
  // previously exited for inactivity. Try to reload the previously saved state.
  // The canonical device mappings will then get re-filled when USB devices are
  // probed.
  if (known_devices_.empty()) {
    LoadDeviceCache();
  }

  auto maybe_session = GetSession(session_id);
  if (!maybe_session) {
    LOG(ERROR) << __func__ << ": Failed to get session " << session_id;
    return;
  }
  DiscoverySessionState* session = *maybe_session;

  LOG(INFO) << __func__ << ": Starting discovery session " << session_id;

  if (!session->local_only) {
    for (PortToken& token : firewall_manager_->RequestPortsForDiscovery()) {
      session->port_tokens.emplace_back(
          std::make_unique<PortToken>(std::move(token)));
    }
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

  if (!dlc_completed_successfully_ &&
      session->dlc_policy == BackendDownloadPolicy::DOWNLOAD_ALWAYS) {
    dlc_pending_sessions_.insert(session_id);
    dlc_started_ = true;
    dlc_client_->InstallDlc({kSaneBackendsPfuDlcId});
  }

  for (auto& device : libusb_->GetDevices()) {
    std::optional<std::string> dlc_id = device->GetNonBundledBackendId();
    if (!dlc_completed_successfully_ && dlc_id != std::nullopt &&
        session->dlc_policy != BackendDownloadPolicy::DOWNLOAD_NEVER) {
      dlc_pending_sessions_.insert(session_id);
      if (!dlc_started_) {
        dlc_started_ = true;
        dlc_client_->InstallDlc({kSaneBackendsPfuDlcId});
      }
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

  // If DLC download still running
  if (dlc_started_) {
    LOG(INFO) << __func__ << ": Waiting for DLC to finish";
    if (!base::Contains(dlc_pending_sessions_, session_id)) {
      // Sanity check, should never enter here
      dlc_pending_sessions_.insert(session_id);
    }
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
      canonical_scanners_.AddUsbDevice(*device, scanner_info->name());
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

std::vector<ScannerInfo> DeviceTracker::GetDevicesFromSANE(bool local_only) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  brillo::ErrorPtr error_ptr;
  std::optional<std::vector<ScannerInfo>> devices =
      sane_client_->ListDevices(&error_ptr, local_only);

  if (!devices.has_value()) {
    LOG(ERROR) << __func__
               << ": Failed to get SANE devices: " << error_ptr->GetMessage();
    return std::vector<ScannerInfo>();
  }

  LOG(INFO) << __func__ << ": Returning " << devices->size()
            << " devices from SANE";
  return devices.value();
}

std::vector<ScannerInfo> DeviceTracker::GetDevicesFromCache(bool local_only) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // This only returns the SANE devices (which, in this context, are the
  // non-ippusb devices).
  std::vector<ScannerInfo> scanners;
  for (const ScannerInfo& info : known_devices_) {
    if (IsIppUsbDevice(info.name())) {
      continue;
    }
    if (local_only && info.connection_type() != lorgnette::CONNECTION_USB) {
      continue;
    }
    scanners.emplace_back(info);
  }

  LOG(INFO) << __func__ << ": Returning " << scanners.size()
            << " devices from cache";
  return scanners;
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

  // If there are any open scanners, running a new SANE discovery can possibly
  // corrupt the memory of the open scanners (depending on the backend).  To
  // prevent this, use the cached scanners in this case.
  std::vector<ScannerInfo> devices =
      NumOpenScanners() > 0 ? GetDevicesFromCache(session->local_only)
                            : GetDevicesFromSANE(session->local_only);

  for (ScannerInfo& scanner_info : devices) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DeviceTracker::ProbeSANEDevice,
                                  weak_factory_.GetWeakPtr(), session_id,
                                  std::move(scanner_info)));
  }

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

  // For Epson scanners, check which backend should be used.  Some epson
  // scanners respond to both epson2 and epsonds.
  CheckEpsonBackend(scanner_info);

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

void DeviceTracker::CheckEpsonBackend(ScannerInfo& scanner_info) {
  // Some Epson scanners respond to the epson2 backend even though the scanner
  // requires the epsonds backend for operation.  However, epsonds will never
  // connect to an unsupported device, so if the scanner responds to the epsonds
  // backend, prefer that over the epson2 backend.
  if (!scanner_info.name().starts_with("epson2:net:")) {
    return;
  }

  // Create an epsonds name and try to connect using that.
  std::string epsonds_name = scanner_info.name();
  epsonds_name = epsonds_name.replace(0, 6, "epsonds");

  LOG(INFO) << "Attempting to connect to " << scanner_info.name()
            << " using connection string " << epsonds_name;

  brillo::ErrorPtr error;
  SANE_Status status;
  std::unique_ptr<SaneDevice> epsonds_device =
      sane_client_->ConnectToDevice(&error, &status, epsonds_name);
  if (epsonds_device) {
    LOG(INFO) << "Found epsonds device for " << epsonds_name;
    scanner_info.set_name(epsonds_name);
    scanner_info.set_protocol_type(ProtocolTypeForScanner(scanner_info));
    scanner_info.set_display_name(DisplayNameForScanner(scanner_info));
  }
}

void DeviceTracker::SendEnumerationCompletedSignal(std::string session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // When devices have all been enumerated, persist the current list so it can
  // be reused for future sessions.  Nothing else will update or access the set
  // of devices until another discovery session starts, so this saved state will
  // remain accurate indefinitel
  SaveDeviceCache();

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
    ClearJobsForScanner(scanner.first);
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
  state.port_token =
      firewall_manager_->RequestPortAccessIfNeeded(connection_string);

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

void DeviceTracker::ClearJobsForScanner(const std::string& scanner_handle) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto it = active_jobs_.begin(); it != active_jobs_.end();) {
    if (it->second.device_handle == scanner_handle) {
      LOG(INFO) << __func__ << ": Clearing existing job " << it->first
                << " for scanner " << scanner_handle;
      it = active_jobs_.erase(it);
    } else {
      ++it;
    }
  }
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

  ClearJobsForScanner(handle);
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

  // Figure out how large the max read size should be.  If the client doesn't
  // request at all, use the largest size.  If the client requests something too
  // small, this is an error.  If the client requests something too large,
  // silently clamp it to the largest size because returning less than the max
  // data is always allowed.
  size_t max_read_size = kLargestMaxReadSize;
  if (request.has_max_read_size()) {
    if (request.max_read_size() < smallest_max_read_size_) {
      LOG(ERROR) << __func__
                 << ": max_read_size too small: " << request.max_read_size();
      response.set_result(OPERATION_RESULT_INVALID);
      return response;
    }
    max_read_size = std::min(static_cast<size_t>(request.max_read_size()),
                             kLargestMaxReadSize);
  }

  // Cancel the active job if one is running, then ensure that no other active
  // jobs still point to this scanner.
  std::optional<std::string> job_id = state.device->GetCurrentJob();
  if (job_id.has_value()) {
    ActiveJobState& job_state = active_jobs_[job_id.value()];
    // Completed job states don't need any cleanup.  For other statuses, try to
    // cancel before starting a new job.
    if (job_state.last_result != OPERATION_RESULT_EOF &&
        job_state.last_result != OPERATION_RESULT_CANCELLED) {
      LOG(WARNING) << __func__ << ": Canceling existing job " << job_id.value();
      CancelScanRequest request;
      request.mutable_job_handle()->set_token(job_id.value());
      CancelScanResponse response = CancelScan(std::move(request));
      if (response.result() != OPERATION_RESULT_SUCCESS &&
          response.result() != OPERATION_RESULT_CANCELLED) {
        LOG(WARNING) << __func__ << ": Failed to cancel scan " << job_id.value()
                     << ": " << OperationResult_Name(response.result());
        // Continue because starting a new scan may reset the backend's state.
        // If it doesn't, we'll return an error from StartScan() later.
      }
    }
    active_jobs_.erase(job_id.value());
  }
  ClearJobsForScanner(handle);

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
  active_jobs_[job_id.value()] = {
      .device_handle = handle,
      .last_result = OPERATION_RESULT_UNKNOWN,
      .cancel_requested = false,
      .cancel_needed = false,
      .next_read = base::Time::Now(),
      .max_read_size = max_read_size,
      .eof_reached = false,
  };
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
  ActiveJobState& job_state = active_jobs_[job_handle];
  job_state.cancel_requested = true;
  job_state.cancel_needed = true;

  if (!base::Contains(open_scanners_, job_state.device_handle)) {
    LOG(ERROR) << __func__
               << ": No open scanner handle: " << job_state.device_handle;
    response.set_failure_reason("No open scanner found for job handle " +
                                job_handle);
    response.set_result(OPERATION_RESULT_MISSING);
    return response;
  }
  OpenScannerState& state = open_scanners_[job_state.device_handle];
  state.last_activity = base::Time::Now();

  // If there's no job handle currently, the previous job was run to completion
  // and no new job has been started.  Go ahead and report that cancelling
  // succeeds because the end state is identical.
  if (!state.device->GetCurrentJob().has_value()) {
    LOG(WARNING) << __func__ << ": Job has already completed: " << job_handle;
    response.set_success(true);
    response.set_result(OPERATION_RESULT_SUCCESS);
    return response;
  }

  if (state.device->GetCurrentJob() != job_handle) {
    LOG(ERROR) << __func__ << ": Job is not currently active: " << job_handle;
    response.set_failure_reason("Job has already been cancelled");
    response.set_result(OPERATION_RESULT_CANCELLED);
    return response;
  }

  // sane-airscan will propagate a cancelled status to the following ADF page if
  // cancel is requested while a read is in progress.  Since we're potentially
  // going to wait for the end of the page after requesting cancellation anyway,
  // just wait up front.
  // TODO(b/328244790): Remove this workaround if this is resolved upstream.
  base::Time cancel_timeout = base::Time::Now() + kMaxCancelWaitTime;
  SANE_Status status;
  if (state.connection_string.starts_with("airscan:") ||
      state.connection_string.starts_with("ippusb:")) {
    // Check for ADF sources. It is not necessary to wait for EOF on the platen.
    brillo::ErrorPtr error;
    std::optional<std::string> source_name =
        state.device->GetDocumentSource(&error);
    if (!source_name.has_value()) {
      LOG(ERROR) << __func__ << ": Unable to get current document source: "
                 << error->GetMessage();
      response.set_success(false);
      response.set_failure_reason(error->GetMessage());
      response.set_result(OPERATION_RESULT_INTERNAL_ERROR);
      return response;
    }
    std::optional<SourceType> source_type =
        GuessSourceType(source_name.value());
    if (!source_type.has_value()) {
      LOG(ERROR) << __func__
                 << ": Unable to parse source: " << source_name.value();
      response.set_success(false);
      response.set_failure_reason(
          base::StrCat({"Unable to parse source: ", source_name.value()}));
      response.set_result(OPERATION_RESULT_INTERNAL_ERROR);
      return response;
    }

    if (source_type == SOURCE_ADF_SIMPLEX || source_type == SOURCE_ADF_DUPLEX) {
      LOG(INFO) << __func__
                << ": Waiting for the end of the page. Lines of image data "
                   "already read: "
                << state.completed_lines;
      do {
        brillo::ErrorPtr error;
        size_t read;
        size_t rows;
        status = state.device->ReadEncodedData(&error, &read, &rows);
        if (status == SANE_STATUS_GOOD && read == 0) {
          // Give the hardware a little time to make progress.
          base::PlatformThread::Sleep(kReadPollInterval);
        }
      } while (status == SANE_STATUS_GOOD &&
               base::Time::Now() < cancel_timeout);
      if (status == SANE_STATUS_GOOD) {
        LOG(WARNING) << "Timed out waiting for EOF.  Deferring cancel.";
        response.set_success(false);
        response.set_failure_reason("Cancel in progress");
        response.set_result(OPERATION_RESULT_DEVICE_BUSY);
        return response;
      }
    }
  }

  LOG(INFO) << __func__ << ": Requesting device to cancel";
  brillo::ErrorPtr error;
  if (!state.device->CancelScan(&error)) {
    LOG(ERROR) << __func__ << ": Failed to cancel job: " << error->GetMessage();
    response.set_failure_reason(error->GetMessage());
    response.set_result(OPERATION_RESULT_INTERNAL_ERROR);
    return response;
  }
  job_state.cancel_needed = false;

  // Most backends will not process the cancellation until sane_read is called.
  // Call sane_read until it returns SANE_STATUS_CANCELLED, the end of the page
  // arrives, or an error happens.
  LOG(INFO) << __func__ << ": Waiting for cancel to complete";
  do {
    brillo::ErrorPtr error;
    size_t read;
    size_t rows;
    status = state.device->ReadEncodedData(&error, &read, &rows);
    if (status == SANE_STATUS_GOOD && read == 0) {
      // Give the hardware a little time to make progress.
      base::PlatformThread::Sleep(kReadPollInterval);
    }
  } while (status == SANE_STATUS_GOOD && base::Time::Now() < cancel_timeout);
  job_state.last_result = ToOperationResult(status);
  switch (status) {
    case SANE_STATUS_INVAL:
      // sane-airscan can sometimes return SANE_STATUS_INVAL if sane_cancel
      // is called at EOF.  This means the scan is done, so treat it the same as
      // EOF.
      [[fallthrough]];
    case SANE_STATUS_EOF:
      // Intentionally treat EOF the same as CANCELLED because the caller
      // doesn't get to see any of the data we discarded above.
      job_state.last_result = OPERATION_RESULT_CANCELLED;
      LOG(INFO) << __func__ << ": Got status while waiting for cancel: "
                << sane_strstatus(status);
      [[fallthrough]];
    case SANE_STATUS_CANCELLED:
      // Cancel completed or document was completely read.
      response.set_success(true);
      response.set_result(OPERATION_RESULT_SUCCESS);
      LOG(INFO) << __func__ << ": Cancel completed";
      break;
    case SANE_STATUS_GOOD:
      // Timed out.
      response.set_success(false);
      response.set_failure_reason("Cancel in progress");
      response.set_result(OPERATION_RESULT_DEVICE_BUSY);
      LOG(INFO) << __func__ << ": Cancel still in progress after timeout";
      break;
    default:
      // Other error.
      response.set_success(false);
      response.set_failure_reason(sane_strstatus(status));
      response.set_result(ToOperationResult(status));
      LOG(INFO) << __func__
                << ": Error during cancellation: " << sane_strstatus(status);
  }

  state.last_activity = base::Time::Now();
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
  ActiveJobState& job_state = active_jobs_[job_handle];

  if (!base::Contains(open_scanners_, job_state.device_handle)) {
    LOG(ERROR) << __func__
               << ": No open scanner handle: " << job_state.device_handle;
    response.set_result(OPERATION_RESULT_MISSING);
    return response;
  }
  OpenScannerState& state = open_scanners_[job_state.device_handle];
  state.last_activity = base::Time::Now();

  // If cancellation has already been requested, lorgnette has already tried to
  // wait for the scan to cancel.  If it reached a non-success status, just
  // return that without querying the device.
  if (job_state.cancel_requested &&
      job_state.last_result != OPERATION_RESULT_SUCCESS) {
    LOG(INFO) << __func__ << ": Job has already been cancelled with result "
              << OperationResult_Name(job_state.last_result);
    response.set_result(job_state.last_result);
    return response;
  }

  // If a previous read didn't produce data, wait until the delay has elapsed
  // before trying again.
  auto now = base::Time::Now();
  if (now < job_state.next_read) {
    base::PlatformThread::Sleep(job_state.next_read - now);
  }

  // If the buffer already contains unread data, return that first.
  size_t available = state.buffer->len - state.buffer->pos;
  if (available) {
    VLOG(1) << __func__
            << ": Previously read encoded bytes available: " << available;
    if (available <= job_state.max_read_size && job_state.eof_reached) {
      // Previous EOF can be returned because pending data fits in the buffer.
      response.set_result(OPERATION_RESULT_EOF);
    } else {
      response.set_result(OPERATION_RESULT_SUCCESS);
    }
    if (available > job_state.max_read_size) {
      available = job_state.max_read_size;
    }
    response.set_data(
        std::string(state.buffer->data + state.buffer->pos, available));
    response.set_estimated_completion(state.completed_lines * 100 /
                                      state.expected_lines);
    state.buffer->pos += available;
    VLOG(1) << __func__ << ": Returning previously read bytes: " << available;
    return response;
  }

  brillo::ErrorPtr error;
  size_t read;
  size_t rows;
  SANE_Status status = state.device->ReadEncodedData(&error, &read, &rows);
  response.set_result(ToOperationResult(status));
  state.completed_lines += rows;
  job_state.last_result = ToOperationResult(status);
  fflush(state.buffer->writer);
  available = state.buffer->len - state.buffer->pos;
  switch (status) {
    case SANE_STATUS_EOF:
      job_state.eof_reached = true;
      if (job_state.cancel_needed) {
        // Cancellation was deferred earlier.  This doesn't matter for the page
        // that was just finished, but request it now in case the ADF needs to
        // stop picking up pages.
        LOG(INFO) << "Sending deferred cancel request.";
        state.device->CancelScan(nullptr);
        job_state.cancel_needed = false;
      }
      if (available > job_state.max_read_size) {
        // The hardware returned EOF, but there's too much data to return it all
        // in this response.  Change to SUCCESS so the client will keep
        // requesting more.
        response.set_result(OPERATION_RESULT_SUCCESS);
      }
      // EOF needs the same data handling as GOOD because there may be image
      // footers that haven't been transmitted yet.
      [[fallthrough]];
    case SANE_STATUS_GOOD: {
      VLOG(1) << __func__ << ": Encoded bytes available: " << available;
      if (available > job_state.max_read_size) {
        available = job_state.max_read_size;
      }
      response.set_data(
          std::string(state.buffer->data + state.buffer->pos, available));
      response.set_estimated_completion(state.completed_lines * 100 /
                                        state.expected_lines);
      state.buffer->pos += available;
      if (available == 0) {
        // Rate-limit polling from the client if no data was available yet.
        // If no lines have been read yet, use a longer delay because it's
        // likely that we're still waiting for physical hardware to move.
        job_state.next_read = base::Time::Now() + (state.completed_lines > 0
                                                       ? kReadPollInterval
                                                       : kInitialPollInterval);
      }
      break;
    }
    default:
      LOG(ERROR) << __func__
                 << ": Failed to read encoded data: " << error->GetMessage();
      return response;
  }

  // If cancellation has already been requested, don't return any more data.  Do
  // allow the success status to propagate so that the client will continue
  // trying until the cancellation finally finishes.
  if (job_state.cancel_requested &&
      (status == SANE_STATUS_GOOD || status == SANE_STATUS_EOF)) {
    response.clear_data();
    response.clear_estimated_completion();
  }

  LOG(INFO) << __func__ << ": Returning " << response.data().size()
            << " encoded bytes";
  state.last_activity = base::Time::Now();
  return response;
}

void DeviceTracker::OnDlcSuccess(const std::string& dlc_id,
                                 const base::FilePath& file_path) {
  LOG(INFO) << "DLC install completed";
  dlc_root_path_ = file_path;
  dlc_started_ = false;
  dlc_completed_successfully_ = true;
  for (const std::string& session_id : dlc_pending_sessions_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DeviceTracker::EnumerateSANEDevices,
                                  weak_factory_.GetWeakPtr(), session_id));
  }
  dlc_pending_sessions_.clear();
}

void DeviceTracker::OnDlcFailure(const std::string& dlc_id,
                                 const std::string& error_msg) {
  LOG(ERROR) << "DLC install failed with message: " << error_msg;
  dlc_root_path_ = base::FilePath();
  dlc_started_ = false;
  dlc_completed_successfully_ = false;
  for (std::string session_id : dlc_pending_sessions_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DeviceTracker::EnumerateSANEDevices,
                                  weak_factory_.GetWeakPtr(), session_id));
  }
  dlc_pending_sessions_.clear();
}

base::FilePath DeviceTracker::GetDlcRootPath() {
  return dlc_root_path_;
}
}  // namespace lorgnette
