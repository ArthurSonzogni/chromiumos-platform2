// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/json/json_writer.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <brillo/daemons/daemon.h>
#include <brillo/flag_helper.h>
#include <chromeos/constants/imageloader.h>
#include <dbus/bus.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <libimageloader/manifest.h>
#include <libminijail.h>
#include <scoped_minijail.h>

#include "dlcservice/dbus-proxies.h"
#include "dlcservice/utils.h"

using base::DictionaryValue;
using base::FilePath;
using base::ListValue;
using base::Value;
using dlcservice::DlcModuleInfo;
using dlcservice::DlcModuleList;
using org::chromium::DlcServiceInterfaceProxy;
using std::string;
using std::vector;

namespace {

constexpr uid_t kRootUid = 0;
constexpr uid_t kDlcServiceUid = 20118;
constexpr char kDlcServiceUser[] = "dlcservice";
constexpr char kDlcServiceGroup[] = "dlcservice";

void EnterMinijail() {
  ScopedMinijail jail(minijail_new());
  CHECK_EQ(0, minijail_change_user(jail.get(), kDlcServiceUser));
  CHECK_EQ(0, minijail_change_group(jail.get(), kDlcServiceGroup));
  minijail_inherit_usergroups(jail.get());
  minijail_no_new_privs(jail.get());
  minijail_enter(jail.get());
}

string ErrorPtrStr(const brillo::ErrorPtr& err) {
  std::ostringstream err_stream;
  err_stream << "Domain=" << err->GetDomain() << " "
             << "Error Code=" << err->GetCode() << " "
             << "Error Message=" << err->GetMessage();
  // TODO(crbug.com/999284): No inner error support, err->GetInnerError().
  return err_stream.str();
}

}  // namespace

class DlcServiceUtil : public brillo::Daemon {
 public:
  DlcServiceUtil(int argc, const char** argv)
      : argc_(argc), argv_(argv), weak_ptr_factory_(this) {}
  ~DlcServiceUtil() override = default;

 private:
  int OnEventLoopStarted() override {
    // "--install" related flags.
    DEFINE_bool(install, false, "Install a single DLC.");
    DEFINE_string(omaha_url, "",
                  "Overrides the default Omaha URL in the update_engine.");

    // "--uninstall" related flags.
    DEFINE_bool(uninstall, false, "Uninstall a single DLC.");

    // "--purge" related flags.
    DEFINE_bool(purge, false, "Purge a single DLC.");

    // "--install", "--purge", and "--uninstall" related flags.
    DEFINE_string(dlc_ids, "", "The ID of the DLC.");

    // "--dlc_state" related flags.
    DEFINE_bool(dlc_state, false, "Get the state of a given DLC.");

    // "--list" related flags.
    DEFINE_bool(list, false, "List installed DLC(s).");
    DEFINE_string(dump, "",
                  "Path to dump to, by default will print to stdout.");

    brillo::FlagHelper::Init(argc_, argv_, "dlcservice_util");

    // Enforce mutually exclusive flags.
    vector<bool> exclusive_flags = {FLAGS_install, FLAGS_uninstall, FLAGS_list,
                                    FLAGS_dlc_state};
    if (std::count(exclusive_flags.begin(), exclusive_flags.end(), true) != 1) {
      LOG(ERROR) << "Only one of --install, --uninstall, --purge, --list, "
                    "--dlc_state must be set.";
      return EX_SOFTWARE;
    }

    int error = EX_OK;
    if (!Init(&error)) {
      LOG(ERROR) << "Failed to initialize client.";
      return error;
    }

    // Called with "--list".
    if (FLAGS_list) {
      DlcModuleList dlc_module_list;
      if (!GetInstalled(&dlc_module_list))
        return EX_SOFTWARE;
      PrintInstalled(FLAGS_dump, dlc_module_list);
      Quit();
      return EX_OK;
    }

    CHECK(!FLAGS_dlc_ids.empty()) << "Please specify a single DLC ID.";
    dlc_id_ = FLAGS_dlc_ids;
    omaha_url_ = FLAGS_omaha_url;

    // Called with "--install".
    if (FLAGS_install) {
      // Set up callbacks
      dlc_service_proxy_->RegisterOnInstallStatusSignalHandler(
          base::Bind(&DlcServiceUtil::OnInstallStatus,
                     weak_ptr_factory_.GetWeakPtr()),
          base::Bind(&DlcServiceUtil::OnInstallStatusConnect,
                     weak_ptr_factory_.GetWeakPtr()));
      if (Install()) {
        // Don't |Quit()| as we will need to wait for signal of install.
        return EX_OK;
      }
    }

    // Called with "--uninstall".
    if (FLAGS_uninstall) {
      if (Uninstall(false)) {
        Quit();
        return EX_OK;
      }
    }

    // Called with "--purge".
    if (FLAGS_purge) {
      if (Uninstall(true)) {
        Quit();
        return EX_OK;
      }
    }

    // Called with "--dlc_state".
    if (FLAGS_dlc_state) {
      dlcservice::DlcState state;
      if (!GetDlcState(&state))
        return EX_SOFTWARE;
      PrintDLCState(FLAGS_dump, state);
      Quit();
      return EX_OK;
    }

    Quit();
    return EX_SOFTWARE;
  }

  // Initialize the dlcservice proxy. Returns true on success, false otherwise.
  // Sets the given error pointer on failure.
  bool Init(int* error_ptr) {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    scoped_refptr<dbus::Bus> bus{new dbus::Bus{options}};
    if (!bus->Connect()) {
      LOG(ERROR) << "Failed to connect to DBus.";
      *error_ptr = EX_UNAVAILABLE;
      return false;
    }
    dlc_service_proxy_ = std::make_unique<DlcServiceInterfaceProxy>(bus);

    return true;
  }

  // Callback invoked on receiving |OnInstallStatus| signal.
  void OnInstallStatus(const dlcservice::InstallStatus& install_status) {
    switch (install_status.status()) {
      case dlcservice::Status::COMPLETED:
        LOG(INFO) << "Install successful!: '" << dlc_id_ << "'.";
        Quit();
        break;
      case dlcservice::Status::RUNNING:
        LOG(INFO) << "Install in progress: " << install_status.progress();
        break;
      case dlcservice::Status::FAILED:
        LOG(ERROR) << "Failed to install: '" << dlc_id_
                   << "' with error code: " << install_status.error_code();
        QuitWithExitCode(EX_SOFTWARE);
        break;
      default:
        NOTREACHED();
    }
  }

  // Callback invoked on connecting |OnInstallStatus| signal.
  void OnInstallStatusConnect(const string& interface_name,
                              const string& signal_name,
                              bool success) {
    if (!success) {
      LOG(ERROR) << "Error connecting " << interface_name << "." << signal_name;
      QuitWithExitCode(EX_SOFTWARE);
    }
  }

  // Install current DLC module. Returns true if current module can be
  // installed. False otherwise.
  bool Install() {
    brillo::ErrorPtr err;
    LOG(INFO) << "Attempting to install DLC modules: " << dlc_id_;
    if (!dlc_service_proxy_->InstallWithOmahaUrl(dlc_id_, omaha_url_, &err)) {
      LOG(ERROR) << "Failed to install: " << dlc_id_ << ", "
                 << ErrorPtrStr(err);
      return false;
    }
    return true;
  }

  // Uninstalls or purges a list of DLC modules based on input argument
  // |purge|. Returns true if all uninstall/purge operations complete
  // successfully, false otherwise. Sets the given error pointer on failure.
  bool Uninstall(bool purge) {
    auto cmd_str = purge ? "purge" : "uninstall";
    brillo::ErrorPtr err;
    LOG(INFO) << "Attempting to " << cmd_str << " DLC: " << dlc_id_;
    bool result = purge ? dlc_service_proxy_->Purge(dlc_id_, &err)
                        : dlc_service_proxy_->Uninstall(dlc_id_, &err);
    if (!result) {
      LOG(ERROR) << "Failed to " << cmd_str << " DLC: " << dlc_id_ << ", "
                 << ErrorPtrStr(err);
      return false;
    }
    LOG(INFO) << "'" << dlc_id_ << "' successfully " << cmd_str << "ed.";
    return true;
  }

  // Gets the state of current DLC module.
  bool GetDlcState(dlcservice::DlcState* state) {
    brillo::ErrorPtr err;
    if (!dlc_service_proxy_->GetDlcState(dlc_id_, state, &err)) {
      LOG(ERROR) << "Failed to get state of DLC " << dlc_id_ << ", "
                 << ErrorPtrStr(err);
      return false;
    }
    return true;
  }

  // Prints the DLC state.
  void PrintDLCState(const string& dump, const dlcservice::DlcState& state) {
    DictionaryValue dict;
    dict.SetKey("state", Value(state.state()));

    PrintToFileOrStdout(dump, dict);
  }

  // Retrieves a list of all installed DLC modules. Returns true if the list is
  // retrieved successfully, false otherwise. Sets the given error pointer on
  // failure.
  bool GetInstalled(DlcModuleList* dlc_module_list) {
    brillo::ErrorPtr err;
    if (!dlc_service_proxy_->GetInstalled(dlc_module_list, &err)) {
      LOG(ERROR) << "Failed to get the list of installed DLC modules, "
                 << ErrorPtrStr(err);
      return false;
    }
    return true;
  }

  decltype(auto) GetPackages(const string& id) {
    return dlcservice::ScanDirectory(
        dlcservice::JoinPaths(imageloader::kDlcManifestRootpath, id));
  }

  bool GetManifest(const string& id,
                   const string& package,
                   imageloader::Manifest* manifest) {
    if (!dlcservice::GetDlcManifest(FilePath(imageloader::kDlcManifestRootpath),
                                    id, package, manifest)) {
      LOG(ERROR) << "Failed to get DLC manifest.";
      return false;
    }
    return true;
  }

  // Helper to print to file, or stdout if |path| is empty.
  void PrintToFileOrStdout(const string& path, const DictionaryValue& dict) {
    string json;
    if (!base::JSONWriter::WriteWithOptions(
            dict, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json)) {
      LOG(ERROR) << "Failed to write json.";
      return;
    }
    if (!path.empty()) {
      if (!dlcservice::WriteToFile(FilePath(path), json))
        PLOG(ERROR) << "Failed to write to file " << path;
    } else {
      std::cout << json;
    }
  }

  void PrintInstalled(const string& dump,
                      const DlcModuleList& dlc_module_list) {
    DictionaryValue dict;
    for (const auto& dlc_module_info : dlc_module_list.dlc_module_infos()) {
      const auto& id = dlc_module_info.dlc_id();
      const auto& packages = GetPackages(id);
      if (packages.empty())
        continue;
      auto dlc_info_list = std::make_unique<ListValue>();
      for (const auto& package : packages) {
        imageloader::Manifest manifest;
        if (!GetManifest(id, package, &manifest))
          return;
        auto dlc_info = std::make_unique<DictionaryValue>();
        dlc_info->SetKey("name", Value(manifest.name()));
        dlc_info->SetKey("id", Value(manifest.id()));
        dlc_info->SetKey("package", Value(manifest.package()));
        dlc_info->SetKey("version", Value(manifest.version()));
        dlc_info->SetKey(
            "preallocated_size",
            Value(base::Int64ToString(manifest.preallocated_size())));
        dlc_info->SetKey("size", Value(base::Int64ToString(manifest.size())));
        dlc_info->SetKey("image_type", Value(manifest.image_type()));
        switch (manifest.fs_type()) {
          case imageloader::FileSystem::kExt4:
            dlc_info->SetKey("fs-type", Value("ext4"));
            break;
          case imageloader::FileSystem::kSquashFS:
            dlc_info->SetKey("fs-type", Value("squashfs"));
            break;
        }
        dlc_info->SetKey("manifest",
                         Value(dlcservice::JoinPaths(
                                   FilePath(imageloader::kDlcManifestRootpath),
                                   id, package, dlcservice::kManifestName)
                                   .value()));
        dlc_info->SetKey("root_mount", Value(dlc_module_info.dlc_root()));
        dlc_info_list->Append(std::move(dlc_info));
      }
      dict.Set(id, std::move(dlc_info_list));
    }

    PrintToFileOrStdout(dump, dict);
  }

  std::unique_ptr<DlcServiceInterfaceProxy> dlc_service_proxy_{};

  // argc and argv passed to main().
  int argc_;
  const char** argv_;

  // The ID of the current DLC.
  string dlc_id_;
  // Customized Omaha server URL (empty being the default URL).
  string omaha_url_;

  base::WeakPtrFactory<DlcServiceUtil> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DlcServiceUtil);
};

int main(int argc, const char** argv) {
  // Check user that is running dlcservice_util.
  switch (getuid()) {
    case kRootUid:
      EnterMinijail();
      break;
    case kDlcServiceUid:
      break;
    default:
      LOG(ERROR) << "dlcservice_util can only be run as root or dlcservice";
      return 1;
  }
  DlcServiceUtil client(argc, argv);
  return client.Run();
}
