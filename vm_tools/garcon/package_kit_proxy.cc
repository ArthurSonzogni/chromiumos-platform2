// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/garcon/package_kit_proxy.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>
#include <dbus/message.h>
#include <dbus/property.h>

#include "container_guest.grpc.pb.h"  // NOLINT(build/include)

namespace vm_tools {
namespace garcon {

namespace {

// Package ID suffix we require in order to perform an automatic upgrade, this
// corresponds to the repository the package comes from.
constexpr char kManagedPackageIdSuffix[] = ";google-stable-main";

// Constants for the PackageKit D-Bus service.
// See:
// https://github.com/hughsie/PackageKit/blob/master/src/org.freedesktop.PackageKit.Transaction.xml
constexpr char kPackageKitInterface[] = "org.freedesktop.PackageKit";
constexpr char kPackageKitServicePath[] = "/org/freedesktop/PackageKit";
constexpr char kPackageKitServiceName[] = "org.freedesktop.PackageKit";
constexpr char kPackageKitTransactionInterface[] =
    "org.freedesktop.PackageKit.Transaction";
constexpr char kSetHintsMethod[] = "SetHints";
constexpr char kCreateTransactionMethod[] = "CreateTransaction";
constexpr char kGetDetailsLocalMethod[] = "GetDetailsLocal";
constexpr char kInstallFilesMethod[] = "InstallFiles";
constexpr char kRefreshCacheMethod[] = "RefreshCache";
constexpr char kGetUpdatesMethod[] = "GetUpdates";
constexpr char kUpdatePackagesMethod[] = "UpdatePackages";
constexpr char kErrorCodeSignal[] = "ErrorCode";
constexpr char kFinishedSignal[] = "Finished";
constexpr char kDetailsSignal[] = "Details";
constexpr char kPackageSignal[] = "Package";

// Key names for the Details signal from PackageKit.
constexpr char kDetailsKeyPackageId[] = "package-id";
constexpr char kDetailsKeyLicense[] = "license";
constexpr char kDetailsKeyDescription[] = "description";
constexpr char kDetailsKeyUrl[] = "url";
constexpr char kDetailsKeySize[] = "size";
constexpr char kDetailsKeySummary[] = "summary";

// See:
// https://www.freedesktop.org/software/PackageKit/gtk-doc/PackageKit-Enumerations.html#PkExitEnum
constexpr uint32_t kPackageKitExitCodeSuccess = 1;
// See:
// https://www.freedesktop.org/software/PackageKit/gtk-doc/PackageKit-Enumerations.html#PkStatusEnum
constexpr uint32_t kPackageKitStatusDownload = 8;
constexpr uint32_t kPackageKitStatusInstall = 9;
// See:
// https://www.freedesktop.org/software/PackageKit/gtk-doc/PackageKit-Enumerations.html#PkFilterEnum
constexpr uint32_t kPackageKitFilterInstalled = 2;
// See:
// https://www.freedesktop.org/software/PackageKit/gtk-doc/PackageKit-Enumerations.html#PkInfoEnum
constexpr uint32_t kPackageKitInfoSecurity = 8;

// Timeout for when we are querying for package information in case PackageKit
// dies.
constexpr int kGetLinuxPackageInfoTimeoutSeconds = 5;
constexpr base::TimeDelta kGetLinuxPackageInfoTimeout =
    base::TimeDelta::FromSeconds(kGetLinuxPackageInfoTimeoutSeconds);

// Delay after startup for doing a repository cache refresh.
constexpr base::TimeDelta kRefreshCacheStartupDelay =
    base::TimeDelta::FromMinutes(5);

// Periodic delay between repository cache refreshes after we do the initial one
// after startup.
constexpr base::TimeDelta kRefreshCachePeriod = base::TimeDelta::FromDays(1);

// Ridiculously large size for a config file.
constexpr size_t kMaxConfigFileSize = 10 * 1024;  // 10 KB
// Constants for the configuration directory/files.
constexpr char kXdgConfigHomeEnvVar[] = "XDG_CONFIG_HOME";
constexpr char kDefaultConfigDir[] = ".config";
constexpr char kConfigFilename[] = "cros-garcon.conf";
constexpr char kDisableAutoCrosUpdatesSetting[] =
    "DisableAutomaticCrosPackageUpdates";
constexpr char kDisableAutoSecurityUpdatesSetting[] =
    "DisableAutomaticSecurityUpdates";

// Bitmask values for all the signals from PackageKit
constexpr uint32_t kErrorCodeSignalMask = 1 << 0;
constexpr uint32_t kFinishedSignalMask = 1 << 1;
constexpr uint32_t kPackageSignalMask = 1 << 2;
constexpr uint32_t kDetailsSignalMask = 1 << 3;
constexpr uint32_t kPropertiesSignalMask = 1 << 4;
constexpr uint32_t kValidSignalMask =
    kErrorCodeSignalMask | kFinishedSignalMask | kPackageSignalMask |
    kDetailsSignalMask | kPropertiesSignalMask;

// Parses the configuration file and returns the results through the parameters.
void CheckDisabledUpdates(bool* disable_cros_updates_out,
                          bool* disable_security_updates_out) {
  DCHECK(disable_cros_updates_out);
  DCHECK(disable_security_updates_out);
  *disable_cros_updates_out = false;
  *disable_security_updates_out = false;
  base::FilePath config_dir;
  const char* xdg_config_dir = getenv(kXdgConfigHomeEnvVar);
  if (!xdg_config_dir || strlen(xdg_config_dir) == 0) {
    config_dir = base::GetHomeDir().Append(kDefaultConfigDir);
  } else {
    config_dir = base::FilePath(xdg_config_dir);
  }
  base::FilePath config_file = config_dir.Append(kConfigFilename);
  // First read in the file as a string.
  std::string config_contents;
  if (!ReadFileToStringWithMaxSize(config_file, &config_contents,
                                   kMaxConfigFileSize)) {
    LOG(ERROR) << "Failed reading in config file: " << config_file.value();
    return;
  }
  base::StringPairs config_pairs;
  base::SplitStringIntoKeyValuePairs(config_contents, '=', '\n', &config_pairs);
  for (auto entry : config_pairs) {
    if (entry.first == kDisableAutoCrosUpdatesSetting) {
      *disable_cros_updates_out = (entry.second == "true");
    } else if (entry.first == kDisableAutoSecurityUpdatesSetting) {
      *disable_security_updates_out = (entry.second == "true");
    }
  }
}

struct PackageKitTransactionProperties : public dbus::PropertySet {
  // These are the only 2 properties we care about.
  dbus::Property<uint32_t> status;
  dbus::Property<uint32_t> percentage;
  PackageKitTransactionProperties(dbus::ObjectProxy* object_proxy,
                                  const PropertyChangedCallback callback)
      : dbus::PropertySet(
            object_proxy, kPackageKitTransactionInterface, callback) {
    RegisterProperty("Status", &status);
    RegisterProperty("Percentage", &percentage);
  }
};

// Base class for the helpers for interacting with PackageKit. This will handle
// all the odd D-Bus failures as well as PackageKit death. This object manages
// its own lifecycle, so it should always be created in a leaky fashion, but
// StartTransaction must ALWAYS be invoked after object creation to ensure
// proper cleanup.
class PackageKitTransaction : PackageKitProxy::PackageKitDeathObserver {
 public:
  explicit PackageKitTransaction(
      scoped_refptr<dbus::Bus> bus,
      base::WeakPtr<PackageKitProxy> packagekit_proxy,
      dbus::ObjectProxy* packagekit_service_proxy,
      uint32_t signal_mask)
      : bus_(bus),
        packagekit_proxy_(packagekit_proxy),
        packagekit_service_proxy_(packagekit_service_proxy),
        signal_mask_(signal_mask),
        weak_ptr_factory_(this) {
    DCHECK_EQ(signal_mask, signal_mask & kValidSignalMask);
    packagekit_proxy_->AddPackageKitDeathObserver(this);
  }
  virtual ~PackageKitTransaction() {
    if (transaction_path_.IsValid()) {
      bus_->RemoveObjectProxy(kPackageKitServiceName, transaction_path_,
                              base::Bind(&base::DoNothing));
    }
    packagekit_proxy_->RemovePackageKitDeathObserver(this);
  }

  // This MUST be invoked after object construction in order to ensure proper
  // cleanup. Returns true on successful start of the process, false otherwise.
  // Even if this returns false, it will take care of it's own destruction.
  bool StartTransaction() {
    // Create a transaction with PackageKit for performing the operation.
    dbus::MethodCall method_call(kPackageKitInterface,
                                 kCreateTransactionMethod);
    dbus::MessageWriter writer(&method_call);
    std::unique_ptr<dbus::Response> dbus_response =
        packagekit_service_proxy_->CallMethodAndBlock(
            &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
    if (!dbus_response) {
      GeneralErrorInternal("Failure calling CreateTransaction");
      return false;
    }
    // CreateTransaction returns the object path for the transaction session we
    // have created.
    dbus::MessageReader reader(dbus_response.get());
    if (!reader.PopObjectPath(&transaction_path_)) {
      GeneralErrorInternal(
          "Failure reading object path from transaction result");
      return false;
    }
    transaction_proxy_ =
        bus_->GetObjectProxy(kPackageKitServiceName, transaction_path_);
    if (!transaction_proxy_) {
      GeneralErrorInternal("Failed to get proxy for transaction");
      return false;
    }

    // Set the hint that we don't support interactivity. I haven't seen a case
    // of this yet, but it seems like a good idea to set it if it does occur.
    dbus::MethodCall sethints_call(kPackageKitTransactionInterface,
                                   kSetHintsMethod);
    dbus::MessageWriter sethints_writer(&sethints_call);
    sethints_writer.AppendArrayOfStrings({"interactive=false"});
    dbus_response = transaction_proxy_->CallMethodAndBlock(
        &sethints_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
    if (!dbus_response) {
      // Don't propagate a failure, this was just a hint.
      LOG(WARNING) << "Failure calling SetHints";
    }

    // Hook up all the necessary signals to PackageKit for monitoring the
    // transaction. After these are all hooked up, we will invoke the method
    // so the subclass can initiate the actual request.

    // The properties Signal is special, there exists a helper class for that
    // where we don't manage hooking up the signals ourself.
    if (signal_mask_ & kPropertiesSignalMask) {
      // Remove the bit from the mask to indicate we processed it already.
      signal_mask_ = signal_mask_ & ~kPropertiesSignalMask;
      transaction_properties_ =
          std::make_unique<PackageKitTransactionProperties>(
              transaction_proxy_,
              base::Bind(&PackageKitTransaction::OnPackageKitPropertyChanged,
                         weak_ptr_factory_.GetWeakPtr()));
      transaction_properties_->ConnectSignals();
      transaction_properties_->GetAll();
    }

    if (signal_mask_ == 0) {
      // No signals to hookup, just go right into the request.
      if (!ExecuteRequest(transaction_proxy_)) {
        GeneralErrorInternal(
            "Failure executing the request in the transaction");
        return false;
      }
    }
    ConnectNextSignal();
    return true;
  }

  // Override to execute the actual request within the transaction such as
  // GetUpdates, RefreshCache, etc. Returns true if the call succeeded, false
  // otherwise. If this method fails, then GeneralError will be invoked.
  virtual bool ExecuteRequest(dbus::ObjectProxy* transaction_proxy) = 0;

  // Invoked when something went wrong in the D-Bus communication, the object
  // will self-destruct after this call.
  virtual void GeneralError(const std::string& details) {
    LOG(ERROR) << details;
  }

  // Invoked when the corresponding signals are received and decoded. If a
  // Finished signal occurs, then no other calls will be made after that and
  // this object will self-destruct.
  virtual void ErrorReceived(uint32_t error_code, const std::string& details) {
    LOG(ERROR) << "Error occured with PackageKit transaction with code: "
               << error_code << " and details: " << details;
  }
  virtual void FinishedReceived(uint32_t exit_code) {
    if (exit_code == kPackageKitExitCodeSuccess) {
      LOG(INFO) << "PackageKit transaction completed successfully";
    } else {
      LOG(ERROR) << "PackageKit transaction failed with code: " << exit_code;
    }
  }
  virtual void PackageReceived(uint32_t code,
                               const std::string& package_id,
                               const std::string& summary) {}
  virtual void DetailsReceived(const std::string& package_id,
                               const std::string& license,
                               const std::string& description,
                               const std::string& project_url,
                               uint64_t size,
                               const std::string& summary) {}
  virtual void PropertyChangeReceived(
      const std::string& name, PackageKitTransactionProperties* properties) {}

 private:
  // PackageKitDeathObserver overrides:
  void OnPackageKitDeath() {
    GeneralErrorInternal("PackageKit D-Bus service died, abort operation");
  }

  void GeneralErrorInternal(const std::string& details) {
    GeneralError(details);
    // An unknown error has occurred, we should self-destruct now.
    delete this;
  }

  void ConnectNextSignal() {
    std::string signal_name;
    dbus::ObjectProxy::SignalCallback signal_callback;
    if (signal_mask_ & kErrorCodeSignalMask) {
      signal_mask_ = signal_mask_ & ~kErrorCodeSignalMask;
      signal_name.assign(kErrorCodeSignal);
      signal_callback = base::Bind(&PackageKitTransaction::OnErrorSignal,
                                   weak_ptr_factory_.GetWeakPtr());
    } else if (signal_mask_ & kFinishedSignalMask) {
      signal_mask_ = signal_mask_ & ~kFinishedSignalMask;
      signal_name.assign(kFinishedSignal);
      signal_callback = base::Bind(&PackageKitTransaction::OnFinishedSignal,
                                   weak_ptr_factory_.GetWeakPtr());
    } else if (signal_mask_ & kPackageSignalMask) {
      signal_mask_ = signal_mask_ & ~kPackageSignalMask;
      signal_name.assign(kPackageSignal);
      signal_callback = base::Bind(&PackageKitTransaction::OnPackageSignal,
                                   weak_ptr_factory_.GetWeakPtr());
    } else if (signal_mask_ & kDetailsSignalMask) {
      signal_mask_ = signal_mask_ & ~kDetailsSignalMask;
      signal_name.assign(kDetailsSignal);
      signal_callback = base::Bind(&PackageKitTransaction::OnDetailsSignal,
                                   weak_ptr_factory_.GetWeakPtr());
    } else {
      NOTREACHED();
    }

    transaction_proxy_->ConnectToSignal(
        kPackageKitTransactionInterface, signal_name, signal_callback,
        base::Bind(&PackageKitTransaction::OnSignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool is_connected) {
    if (!is_connected) {
      // Any failures in signal hookups mean we should abort.
      GeneralErrorInternal("Failed to hookup " + signal_name + " signal");
      return;
    }
    if (signal_mask_ == 0) {
      // Done hooking up our signals, let the subclass invoke the request.
      if (!ExecuteRequest(transaction_proxy_)) {
        GeneralErrorInternal(
            "Failure executing the request in the transaction");
      }
    } else {
      ConnectNextSignal();
    }
  }

  void OnErrorSignal(dbus::Signal* signal) {
    CHECK(signal);
    dbus::MessageReader reader(signal);
    uint32_t code;
    std::string details;
    if (!reader.PopUint32(&code) || !reader.PopString(&details)) {
      GeneralErrorInternal("Failure parsing PackageKit error signal");
      return;
    }
    ErrorReceived(code, details);
  }

  void OnFinishedSignal(dbus::Signal* signal) {
    CHECK(signal);
    dbus::MessageReader reader(signal);
    uint32_t exit_code;
    if (!reader.PopUint32(&exit_code)) {
      GeneralErrorInternal("Failure parsing PackageKit finished signal");
      return;
    }
    FinishedReceived(exit_code);
    // We are done, we should self-destruct.
    delete this;
  }

  void OnPackageSignal(dbus::Signal* signal) {
    CHECK(signal);
    dbus::MessageReader reader(signal);
    uint32_t code;
    std::string package_id;
    std::string summary;
    if (!reader.PopUint32(&code) || !reader.PopString(&package_id) ||
        !reader.PopString(&summary)) {
      GeneralErrorInternal("Failure parsing PackageKit Package signal");
      return;
    }
    PackageReceived(code, package_id, summary);
  }

  void OnDetailsSignal(dbus::Signal* signal) {
    CHECK(signal);
    dbus::MessageReader reader(signal);
    // Read all of the details on the package. This is an array of dict entries
    // with string keys and variant values.
    dbus::MessageReader array_reader(nullptr);
    if (!reader.PopArray(&array_reader)) {
      GeneralErrorInternal("Failure parsing PackageKit Details signal");
      return;
    }
    std::string package_id;
    std::string license;
    std::string description;
    std::string project_url;
    uint64_t size = 0;
    std::string summary;
    while (array_reader.HasMoreData()) {
      dbus::MessageReader dict_entry_reader(nullptr);
      if (array_reader.PopDictEntry(&dict_entry_reader)) {
        dbus::MessageReader value_reader(nullptr);
        std::string name;
        if (!dict_entry_reader.PopString(&name) ||
            !dict_entry_reader.PopVariant(&value_reader)) {
          LOG(WARNING) << "Error popping dictionary entry from D-Bus message";
          continue;
        }
        if (name == kDetailsKeyPackageId) {
          if (!value_reader.PopString(&package_id)) {
            LOG(WARNING) << "Error popping package_id from details";
          }
        } else if (name == kDetailsKeyLicense) {
          if (!value_reader.PopString(&license)) {
            LOG(WARNING) << "Error popping license from details";
          }
        } else if (name == kDetailsKeyDescription) {
          if (!value_reader.PopString(&description)) {
            LOG(WARNING) << "Error popping description from details";
          }
        } else if (name == kDetailsKeyUrl) {
          if (!value_reader.PopString(&project_url)) {
            LOG(WARNING) << "Error popping url from details";
          }
        } else if (name == kDetailsKeySize) {
          if (!value_reader.PopUint64(&size)) {
            LOG(WARNING) << "Error popping size from details";
          }
        } else if (name == kDetailsKeySummary) {
          if (!value_reader.PopString(&summary)) {
            LOG(WARNING) << "Error popping summary from details";
          }
        }
      }
    }
    DetailsReceived(package_id, license, description, project_url, size,
                    summary);
  }

  void OnPackageKitPropertyChanged(const std::string& name) {
    PropertyChangeReceived(name, transaction_properties_.get());
  }

 protected:
  scoped_refptr<dbus::Bus> bus_;
  base::WeakPtr<PackageKitProxy> packagekit_proxy_;
  dbus::ObjectProxy* packagekit_service_proxy_;  // Not owned.

 private:
  uint32_t signal_mask_;

  dbus::ObjectProxy* transaction_proxy_;  // Owned by bus_.
  dbus::ObjectPath transaction_path_;
  std::unique_ptr<PackageKitTransactionProperties> transaction_properties_;

  base::WeakPtrFactory<PackageKitTransaction> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(PackageKitTransaction);
};

// Sublcass for handling GetDetailsLocal transaction.
class GetDetailsLocalTransaction : public PackageKitTransaction {
 public:
  GetDetailsLocalTransaction(
      scoped_refptr<dbus::Bus> bus,
      base::WeakPtr<PackageKitProxy> packagekit_proxy,
      dbus::ObjectProxy* packagekit_service_proxy,
      std::shared_ptr<PackageKitProxy::PackageInfoTransactionData> data)
      : PackageKitTransaction(
            bus,
            packagekit_proxy,
            packagekit_service_proxy,
            kErrorCodeSignalMask | kFinishedSignalMask | kDetailsSignalMask),
        data_(data) {
    data_->result = false;
  }

  void GeneralError(const std::string& details) override {
    LOG(ERROR) << "Problem with GetDetailsLocal transaction: " << details;
    // Check if we've already indicated we are done.
    if (data_->event.IsSignaled())
      return;
    data_->error.assign(details);
    data_->event.Signal();
  }

  bool ExecuteRequest(dbus::ObjectProxy* transaction_proxy) override {
    dbus::MethodCall method_call(kPackageKitTransactionInterface,
                                 kGetDetailsLocalMethod);
    dbus::MessageWriter writer(&method_call);
    writer.AppendArrayOfStrings({data_->file_path.value()});
    std::unique_ptr<dbus::Response> dbus_response =
        transaction_proxy->CallMethodAndBlock(
            &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
    return !!dbus_response;
  }

  void ErrorReceived(uint32_t error_code, const std::string& details) override {
    LOG(ERROR) << "Failure querying Linux package of: " << details;
    // Check if we've already indicated we are done.
    if (data_->event.IsSignaled())
      return;
    // We will still get a Finished signal where we finalize everything.
    data_->error.assign(details);
  }

  void FinishedReceived(uint32_t exit_code) override {
    LOG(INFO) << "Finished with query for Linux package info";
    // Check if we've already indicated we are done.
    if (data_->event.IsSignaled())
      return;
    // If this is a failure, the error message should have already been set via
    // that callback.
    data_->result = kPackageKitExitCodeSuccess == exit_code;
    data_->event.Signal();
  }

  void DetailsReceived(const std::string& package_id,
                       const std::string& license,
                       const std::string& description,
                       const std::string& project_url,
                       uint64_t size,
                       const std::string& summary) override {
    // Check if we've already indicated we are done.
    if (data_->event.IsSignaled())
      return;
    data_->pkg_info->package_id.assign(package_id);
    data_->pkg_info->license.assign(license);
    data_->pkg_info->description.assign(description);
    data_->pkg_info->project_url.assign(project_url);
    data_->pkg_info->size = size;
    data_->pkg_info->summary.assign(summary);
  }

 private:
  std::shared_ptr<PackageKitProxy::PackageInfoTransactionData> data_;
};

// This is for tracking if an install is currently in progress.
bool install_active = false;

// Sublcass for handling InstallFiles transaction.
class InstallFilesTransaction : public PackageKitTransaction {
 public:
  InstallFilesTransaction(
      scoped_refptr<dbus::Bus> bus,
      base::WeakPtr<PackageKitProxy> packagekit_proxy,
      dbus::ObjectProxy* packagekit_service_proxy,
      base::WeakPtr<PackageKitProxy::PackageKitObserver> observer,
      base::FilePath file_path)
      : PackageKitTransaction(
            bus,
            packagekit_proxy,
            packagekit_service_proxy,
            kErrorCodeSignalMask | kFinishedSignalMask | kPropertiesSignalMask),
        file_path_(file_path),
        observer_(observer) {}
  ~InstallFilesTransaction() { install_active = false; }

  void GeneralError(const std::string& details) override {
    if (!observer_)
      return;
    observer_->OnInstallCompletion(false, details);
    observer_.reset();
  }

  bool ExecuteRequest(dbus::ObjectProxy* transaction_proxy) override {
    dbus::MethodCall method_call(kPackageKitTransactionInterface,
                                 kInstallFilesMethod);
    dbus::MessageWriter writer(&method_call);
    writer.AppendUint64(0);  // Allow installing untrusted files.
    writer.AppendArrayOfStrings({file_path_.value()});
    std::unique_ptr<dbus::Response> dbus_response =
        transaction_proxy->CallMethodAndBlock(
            &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
    return !!dbus_response;
  }

  void ErrorReceived(uint32_t error_code, const std::string& details) override {
    LOG(ERROR) << "Failure installing Linux package of: " << details;
    if (!observer_)
      return;
    observer_->OnInstallCompletion(false, details);
    observer_.reset();
  }

  void FinishedReceived(uint32_t exit_code) override {
    LOG(INFO) << "Finished installing Linux package result: " << exit_code;
    if (!observer_)
      return;
    observer_->OnInstallCompletion(
        kPackageKitExitCodeSuccess == exit_code,
        "Exit Code: " + base::IntToString(exit_code));
    observer_.reset();
  }

  void PropertyChangeReceived(
      const std::string& name,
      PackageKitTransactionProperties* properties) override {
    if (!observer_)
      return;
    // There's only 2 progress states we actually care about which are logical
    // to report to the user. These are downloading and installing, which
    // correspond to similar experiences in Android and elsewhere. There are
    // various other phases this goes through, but they happen rather quickly
    // and would not be worth informing the user of.
    if (name != properties->percentage.name()) {
      // We only want to see progress percentage changes and then we filter
      // these below based on the current status.
      return;
    }
    vm_tools::container::InstallLinuxPackageProgressInfo::Status status;
    switch (properties->status.value()) {
      case kPackageKitStatusDownload:
        status =
            vm_tools::container::InstallLinuxPackageProgressInfo::DOWNLOADING;
        break;
      case kPackageKitStatusInstall:
        status =
            vm_tools::container::InstallLinuxPackageProgressInfo::INSTALLING;
        break;
      default:
        // Not a status state we care about.
        return;
    }
    int percentage = properties->percentage.value();
    // PackageKit uses 101 for the percent when it doesn't know, treat that as
    // zero because you see this at the beginning of phases.
    if (percentage == 101)
      percentage = 0;
    observer_->OnInstallProgress(status, percentage);
  }

 private:
  base::FilePath file_path_;
  base::WeakPtr<PackageKitProxy::PackageKitObserver> observer_;
};

// Sublcass for handling UpdatePackages transaction.
class UpdatePackagesTransaction : public PackageKitTransaction {
 public:
  UpdatePackagesTransaction(scoped_refptr<dbus::Bus> bus,
                            base::WeakPtr<PackageKitProxy> packagekit_proxy,
                            dbus::ObjectProxy* packagekit_service_proxy,
                            std::vector<std::string> package_ids)
      : PackageKitTransaction(bus,
                              packagekit_proxy,
                              packagekit_service_proxy,
                              kErrorCodeSignalMask | kFinishedSignalMask),
        package_ids_(package_ids) {
    LOG(INFO) << "Attempting to upgrade package IDs: "
              << base::JoinString(package_ids, ", ");
  }

  void GeneralError(const std::string& details) override {
    LOG(ERROR) << "Error occurred with UpdatePackages: " << details;
  }

  bool ExecuteRequest(dbus::ObjectProxy* transaction_proxy) override {
    dbus::MethodCall method_call(kPackageKitTransactionInterface,
                                 kUpdatePackagesMethod);
    dbus::MessageWriter writer(&method_call);
    writer.AppendUint64(0);  // No transaction flag.
    writer.AppendArrayOfStrings(package_ids_);
    std::unique_ptr<dbus::Response> dbus_response =
        transaction_proxy->CallMethodAndBlock(
            &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
    return !!dbus_response;
  }

  void ErrorReceived(uint32_t error_code, const std::string& details) override {
    LOG(ERROR) << "Failure with UpdatePackages of: " << details;
  }

  void FinishedReceived(uint32_t exit_code) override {
    if (exit_code == kPackageKitExitCodeSuccess) {
      LOG(INFO) << "Successfully performed upgrade of managed packages";
    } else {
      // PackageKit will log the specific error itself.
      LOG(ERROR) << "Failure performing upgrade of managed packages, code: "
                 << exit_code;
    }
  }

 private:
  std::vector<std::string> package_ids_;
};

// Sublcass for handling GetUpdates transaction.
class GetUpdatesTransaction : public PackageKitTransaction {
 public:
  GetUpdatesTransaction(scoped_refptr<dbus::Bus> bus,
                        base::WeakPtr<PackageKitProxy> packagekit_proxy,
                        dbus::ObjectProxy* packagekit_service_proxy)
      : PackageKitTransaction(
            bus,
            packagekit_proxy,
            packagekit_service_proxy,
            kErrorCodeSignalMask | kFinishedSignalMask | kPackageSignalMask) {
    CheckDisabledUpdates(&cros_updates_disabled_, &security_updates_disabled_);
  }

  void GeneralError(const std::string& details) override {
    LOG(ERROR) << "Error occurred with GetUpdates: " << details;
  }

  bool ExecuteRequest(dbus::ObjectProxy* transaction_proxy) override {
    dbus::MethodCall method_call(kPackageKitTransactionInterface,
                                 kGetUpdatesMethod);
    dbus::MessageWriter writer(&method_call);
    // Set the filter to installed packages.
    writer.AppendUint64(kPackageKitFilterInstalled);
    std::unique_ptr<dbus::Response> dbus_response =
        transaction_proxy->CallMethodAndBlock(
            &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
    return !!dbus_response;
  }

  void ErrorReceived(uint32_t error_code, const std::string& details) override {
    LOG(ERROR) << "Failure with GetUpdates of: " << details;
  }

  void PackageReceived(uint32_t code,
                       const std::string& package_id,
                       const std::string& /* summary */) override {
    if (!cros_updates_disabled_ &&
        base::EndsWith(package_id, kManagedPackageIdSuffix,
                       base::CompareCase::SENSITIVE)) {
      LOG(INFO) << "Found managed package that is upgradeable, add it to the "
                << "list: " << package_id;
      package_ids_.emplace_back(package_id);
    } else if (!security_updates_disabled_ && code == kPackageKitInfoSecurity) {
      LOG(INFO) << "Found package with security update, add it to the "
                << "list: " << package_id;
      package_ids_.emplace_back(package_id);
    }
  }

  void FinishedReceived(uint32_t exit_code) override {
    if (exit_code == kPackageKitExitCodeSuccess) {
      LOG(INFO) << "PackageKit GetUpdates transaction has completed with "
                << package_ids_.size() << " available managed updates";
      if (!package_ids_.empty()) {
        // This object is intentionally leaked and will clean itself up when
        // done with all the D-Bus communication.
        UpdatePackagesTransaction* transaction = new UpdatePackagesTransaction(
            bus_, packagekit_proxy_, packagekit_service_proxy_,
            std::move(package_ids_));
        transaction->StartTransaction();
      }
    } else {
      LOG(ERROR) << "Failure performing GetUpdates, code: " << exit_code;
    }
  }

 private:
  std::vector<std::string> package_ids_;
  bool cros_updates_disabled_;
  bool security_updates_disabled_;
};

// Sublcass for handling RefreshCache transaction.
class RefreshCacheTransaction : public PackageKitTransaction {
 public:
  RefreshCacheTransaction(scoped_refptr<dbus::Bus> bus,
                          base::WeakPtr<PackageKitProxy> packagekit_proxy,
                          dbus::ObjectProxy* packagekit_service_proxy)
      : PackageKitTransaction(bus,
                              packagekit_proxy,
                              packagekit_service_proxy,
                              kErrorCodeSignalMask | kFinishedSignalMask) {}

  static void RefreshCacheNow(scoped_refptr<dbus::Bus> bus,
                              base::WeakPtr<PackageKitProxy> packagekit_proxy,
                              dbus::ObjectProxy* packagekit_service_proxy) {
    bool disable_cros_updates;
    bool disable_security_updates;
    CheckDisabledUpdates(&disable_cros_updates, &disable_security_updates);
    if (disable_cros_updates && disable_security_updates) {
      // Don't do the update now, but schedule another one for later and we will
      // check the setting again then.
      LOG(INFO) << "Not performing automatic update because they are disabled";
      base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&RefreshCacheNow, bus, packagekit_proxy,
                     packagekit_service_proxy),
          kRefreshCachePeriod);
      return;
    }

    LOG(INFO) << "Refreshing the remote repository packages";
    // This object is intentionally leaked and will clean itself up when done
    // with all the D-Bus communication.
    RefreshCacheTransaction* transaction = new RefreshCacheTransaction(
        bus, packagekit_proxy, packagekit_service_proxy);
    transaction->StartTransaction();
  }

  void GeneralError(const std::string& details) override {
    LOG(ERROR) << "Error occurred with RefreshCache: " << details;
    ScheduleNextCacheRefresh();
  }

  bool ExecuteRequest(dbus::ObjectProxy* transaction_proxy) override {
    dbus::MethodCall method_call(kPackageKitTransactionInterface,
                                 kRefreshCacheMethod);
    dbus::MessageWriter writer(&method_call);
    writer.AppendBool(false);  // Don't force cache wipe.
    std::unique_ptr<dbus::Response> dbus_response =
        transaction_proxy->CallMethodAndBlock(
            &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
    return !!dbus_response;
  }

  void ErrorReceived(uint32_t error_code, const std::string& details) override {
    LOG(ERROR) << "Failure with RefreshCache of: " << details;
  }

 private:
  void ScheduleNextCacheRefresh() {
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&RefreshCacheNow, bus_, packagekit_proxy_,
                   packagekit_service_proxy_),
        kRefreshCachePeriod);
  }

 public:
  void FinishedReceived(uint32_t exit_code) override {
    if (exit_code == kPackageKitExitCodeSuccess) {
      LOG(INFO) << "Successfully performed refresh of package cache";
      // Now we need to get the list of updatable packages that we control so we
      // can perform upgrades on anything that's available.
      // This object is intentionally leaked and will clean itself up when done
      // with all the D-Bus communication.
      GetUpdatesTransaction* transaction = new GetUpdatesTransaction(
          bus_, packagekit_proxy_, packagekit_service_proxy_);
      transaction->StartTransaction();
    } else {
      LOG(ERROR) << "Failure performing refresh of package cache, code: "
                 << exit_code;
    }
    ScheduleNextCacheRefresh();
  }
};

}  // namespace

PackageKitProxy::PackageInfoTransactionData::PackageInfoTransactionData(
    const base::FilePath& file_path_in,
    std::shared_ptr<LinuxPackageInfo> pkg_info_in)
    : file_path(file_path_in),
      event(false /*manual_reset*/, false /*initially_signaled*/),
      pkg_info(pkg_info_in) {}

// static
std::unique_ptr<PackageKitProxy> PackageKitProxy::Create(
    base::WeakPtr<PackageKitObserver> observer) {
  if (!observer)
    return nullptr;
  auto pk_proxy = base::WrapUnique(new PackageKitProxy(std::move(observer)));
  if (!pk_proxy->Init()) {
    pk_proxy.reset();
  }
  return pk_proxy;
}

PackageKitProxy::PackageKitProxy(base::WeakPtr<PackageKitObserver> observer)
    : observer_(observer),
      task_runner_(base::ThreadTaskRunnerHandle::Get()),
      weak_ptr_factory_(this) {}

PackageKitProxy::~PackageKitProxy() = default;

bool PackageKitProxy::Init() {
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(std::move(opts));
  if (!bus_->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return false;
  }
  packagekit_service_proxy_ = bus_->GetObjectProxy(
      kPackageKitServiceName, dbus::ObjectPath(kPackageKitServicePath));
  if (!packagekit_service_proxy_) {
    LOG(ERROR) << "Failed to get PackageKit D-Bus proxy";
    return false;
  }
  packagekit_service_proxy_->WaitForServiceToBeAvailable(
      base::Bind(&PackageKitProxy::OnPackageKitServiceAvailable,
                 weak_ptr_factory_.GetWeakPtr()));

  // Fire off a delayed task to do a repo update so that we can do automatic
  // upgrades on our managed packages.
  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::Bind(&RefreshCacheTransaction::RefreshCacheNow, bus_,
                 weak_ptr_factory_.GetWeakPtr(), packagekit_service_proxy_),
      kRefreshCacheStartupDelay);

  return true;
}

bool PackageKitProxy::GetLinuxPackageInfo(
    const base::FilePath& file_path,
    std::shared_ptr<LinuxPackageInfo> out_pkg_info,
    std::string* out_error) {
  CHECK(out_error);
  // We use another var for the error message into the D-Bus thread call so we
  // don't have contention with that var in the case of a timeout since we want
  // to set the error in a timeout, but not the pkg_info. Shared pointers are
  // used so that if the call times out the pointers are still valid on the
  // D-Bus thread.
  std::shared_ptr<PackageInfoTransactionData> data =
      std::make_shared<PackageInfoTransactionData>(file_path, out_pkg_info);
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&PackageKitProxy::GetLinuxPackageInfoOnDBusThread,
                            weak_ptr_factory_.GetWeakPtr(), data));

  bool result;
  if (!data->event.TimedWait(kGetLinuxPackageInfoTimeout)) {
    LOG(ERROR) << "Timeout waiting on Linux package info";
    out_error->assign("Timeout");
    result = false;
  } else {
    out_error->assign(data->error);
    result = data->result;
  }
  return result;
}

int PackageKitProxy::InstallLinuxPackage(const base::FilePath& file_path,
                                         std::string* out_error) {
  base::WaitableEvent event(false /*manual_reset*/,
                            false /*initially_signaled*/);
  int status = vm_tools::container::InstallLinuxPackageResponse::FAILED;
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&PackageKitProxy::InstallLinuxPackageOnDBusThread,
                            weak_ptr_factory_.GetWeakPtr(), file_path, &event,
                            &status, out_error));
  event.Wait();
  return status;
}

void PackageKitProxy::AddPackageKitDeathObserver(
    PackageKitDeathObserver* observer) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  death_observers_.AddObserver(observer);
}

void PackageKitProxy::RemovePackageKitDeathObserver(
    PackageKitDeathObserver* observer) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  death_observers_.RemoveObserver(observer);
}

void PackageKitProxy::GetLinuxPackageInfoOnDBusThread(
    std::shared_ptr<PackageInfoTransactionData> data) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Getting information on local Linux package";
  // This object is intentionally leaked and will clean itself up when done
  // with all the D-Bus communication.
  GetDetailsLocalTransaction* transaction = new GetDetailsLocalTransaction(
      bus_, weak_ptr_factory_.GetWeakPtr(), packagekit_service_proxy_, data);
  transaction->StartTransaction();
}

void PackageKitProxy::InstallLinuxPackageOnDBusThread(
    const base::FilePath& file_path,
    base::WaitableEvent* event,
    int* status,
    std::string* out_error) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  CHECK(event);
  CHECK(status);
  CHECK(out_error);
  // Make sure we don't already have one in progress.
  if (install_active) {
    *status = vm_tools::container::InstallLinuxPackageResponse::
        INSTALL_ALREADY_ACTIVE;
    *out_error = "Install is already active";
    LOG(ERROR) << *out_error;
    event->Signal();
    return;
  }
  install_active = true;
  // This object is intentionally leaked and will clean itself up when done
  // with all the D-Bus communication.
  InstallFilesTransaction* transaction = new InstallFilesTransaction(
      bus_, weak_ptr_factory_.GetWeakPtr(), packagekit_service_proxy_,
      observer_, file_path);
  if (!transaction->StartTransaction()) {
    install_active = false;
    *status = vm_tools::container::InstallLinuxPackageResponse::FAILED;
    *out_error = "Failure with D-Bus communication";
    LOG(ERROR) << *out_error;
    event->Signal();
    return;
  }

  *status = vm_tools::container::InstallLinuxPackageResponse::STARTED;
  *out_error = "";
  event->Signal();
}

void PackageKitProxy::OnPackageKitNameOwnerChanged(
    const std::string& old_owner, const std::string& new_owner) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  if (new_owner.empty()) {
    for (PackageKitDeathObserver& obs : death_observers_)
      obs.OnPackageKitDeath();
  }
}

void PackageKitProxy::OnPackageKitServiceAvailable(bool service_is_available) {
  if (service_is_available) {
    packagekit_service_proxy_->SetNameOwnerChangedCallback(
        base::Bind(&PackageKitProxy::OnPackageKitNameOwnerChanged,
                   weak_ptr_factory_.GetWeakPtr()));
  }
}

}  // namespace garcon
}  // namespace vm_tools
