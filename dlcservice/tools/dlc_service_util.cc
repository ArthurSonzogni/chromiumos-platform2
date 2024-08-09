// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>
#include <unistd.h>

#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/json/json_writer.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <base/values.h>
#include <brillo/daemons/daemon.h>
#include <brillo/flag_helper.h>
#include <chromeos/constants/imageloader.h>
#include <dbus/bus.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <libimageloader/manifest.h>
#include <libminijail.h>
#include <scoped_minijail.h>

#include "dlcservice/dbus-proxies.h"
#include "dlcservice/proto_utils.h"
#include "dlcservice/utils.h"
#include "dlcservice/utils/utils.h"

using base::FilePath;
using base::Value;
using Dict = base::Value::Dict;
using List = base::Value::List;
using dlcservice::DlcState;
using dlcservice::DlcsWithContent;
using org::chromium::DlcServiceInterfaceProxy;
using std::string;
using std::vector;

namespace {

constexpr uid_t kRootUid = 0;
constexpr uid_t kChronosUid = 1000;
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
  enum Action {
    kInstall,
    kUninstall,
    kPurge,
    kDeploy,
    kList,
    kDlcState,
    kGetExisting,
    kUnload
  };

  int OnEventLoopStarted() override {
    int error = EX_OK;
    if (!Init(&error)) {
      LOG(ERROR) << "Failed to initialize client.";
      return error;
    }

    // "--install" related flags.
    DEFINE_bool(install, false, "Install a single DLC.");
    DEFINE_string(omaha_url, "",
                  "Overrides the default Omaha URL in the update_engine.");
    DEFINE_bool(reserve, false, "Reserve the DLC on install success/failure.");

    // "--uninstall" related flags.
    DEFINE_bool(uninstall, false, "Uninstall a single DLC.");

    // "--purge" related flags.
    DEFINE_bool(purge, false, "Purge a single DLC.");

    // "--deploy" related flags.
    DEFINE_bool(deploy, false, "Load a deployed DLC.");

    // "--unload" related flags.
    DEFINE_bool(unload, false, "Unmount DLCs and mark them NOT_INSTALLED.");
    DEFINE_bool(user_tied, false, "Perform the action on user-tied DLCs.");
    DEFINE_bool(scaled, false, "Perform the action on scaled DLCs.");

    // "--install", "--purge", "--uninstall" and "--unload" related flags.
    DEFINE_string(id, "", "The ID of the DLC.");

    // "--dlc_state" related flags.
    DEFINE_bool(dlc_state, false, "Get the state of a given DLC.");

    // "--get_existing" related flags.
    DEFINE_bool(get_existing, false,
                "Returns a list of DLCs that have content on disk.");

    // "--list" related flags.
    DEFINE_bool(list, false, "List installed DLC(s).");
    DEFINE_bool(check_mount, false,
                "Check mount points to confirm installed DLC(s).");
    DEFINE_string(dump, "",
                  "Path to dump to, by default will print to stdout.");
    DEFINE_int32(timeout, 0,
                 "Timeout seconds waiting for DLC service and the command. No "
                 "timeout when setting to 0.");
    DEFINE_bool(wait_for_service, true,
                "Wait for the DLC service to be available.");

    brillo::FlagHelper::Init(argc_, argv_, "dlcservice_util");

    // Enforce mutually exclusive flags.
    vector<std::pair<bool, Action>> exclusive_action_flags = {
        {FLAGS_install, kInstall},
        {FLAGS_uninstall, kUninstall},
        {FLAGS_purge, kPurge},
        {FLAGS_deploy, kDeploy},
        {FLAGS_list, kList},
        {FLAGS_dlc_state, kDlcState},
        {FLAGS_get_existing, kGetExisting},
        {FLAGS_unload, kUnload}};
    int flags_count = 0;
    for (const auto& [flag, action] : exclusive_action_flags) {
      if (flag) {
        action_ = action;
        ++flags_count;
      }
    }
    if (flags_count != 1) {
      LOG(ERROR)
          << "Only one of --install, --uninstall, --purge, --list, --deploy, "
             "--get_existing, --dlc_state, --unload must be set.";
      return EX_USAGE;
    }

    if (FLAGS_user_tied || FLAGS_scaled) {
      select_ = std::make_optional<dlcservice::SelectDlc>();
      select_->set_user_tied(FLAGS_user_tied);
      select_->set_scaled(FLAGS_scaled);
    }
    check_mount_ = FLAGS_check_mount;
    dump_ = FilePath(FLAGS_dump);

    dlc_id_ = FLAGS_id;
    omaha_url_ = FLAGS_omaha_url;
    reserve_ = FLAGS_reserve;

    if (FLAGS_timeout > 0) {
      // Set the timeout before waiting for DLC service and process the command.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&DlcServiceUtil::TimeoutQuit,
                         weak_ptr_factory_.GetWeakPtr()),
          base::Seconds(FLAGS_timeout));
    } else if (FLAGS_timeout < 0) {
      LOG(ERROR) << "Invalid timeout value=" << FLAGS_timeout;
      return EX_USAGE;
    }

    if (FLAGS_wait_for_service) {
      // Wait for the DLC service.
      dlc_service_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
          base::BindOnce(&DlcServiceUtil::Process,
                         weak_ptr_factory_.GetWeakPtr()));
    } else {
      Process(/*is_available=*/true);
    }

    return EX_OK;
  }

  void Process(bool is_available) {
    if (!is_available) {
      LOG(ERROR) << "dlcservice is not available.";
      QuitWithExitCode(EX_UNAVAILABLE);
      return;
    }

    // Called with "--list".
    if (action_ == kList) {
      dlcservice::ListRequest request;
      request.set_check_mount(check_mount_);
      if (select_)
        *request.mutable_select() = *select_;

      dlcservice::DlcStateList installed_dlcs;
      if (!GetInstalled(request, &installed_dlcs)) {
        QuitWithExitCode(EX_SOFTWARE);
        return;
      }
      PrintInstalled(dump_, installed_dlcs);
      Quit();
      return;
    }

    // Called with "--get_existing".
    if (action_ == kGetExisting) {
      DlcsWithContent dlcs_with_content;
      if (!GetExisting(&dlcs_with_content)) {
        QuitWithExitCode(EX_SOFTWARE);
        return;
      }
      PrintDlcsWithContent(dump_, dlcs_with_content);
      Quit();
      return;
    }

    // Called with "--unload".
    if (action_ == kUnload) {
      dlcservice::UnloadRequest request;
      if (dlc_id_.size()) {
        request.set_id(dlc_id_);
      } else if (select_) {
        *request.mutable_select() = *select_;
      } else {
        LOG(ERROR) << "Please specify a DLC ID or DLC selections.";
        QuitWithExitCode(EX_USAGE);
        return;
      }
      if (!Unload(request)) {
        QuitWithExitCode(EX_SOFTWARE);
        return;
      }
      Quit();
      return;
    }

    if (dlc_id_.empty()) {
      LOG(ERROR) << "Please specify a single DLC ID.";
      QuitWithExitCode(EX_USAGE);
      return;
    }

    // Called with "--install".
    if (action_ == kInstall) {
      // Set up callbacks
      dlc_service_proxy_->RegisterDlcStateChangedSignalHandler(
          base::BindRepeating(&DlcServiceUtil::OnDlcStateChanged,
                              weak_ptr_factory_.GetWeakPtr()),
          base::BindOnce(&DlcServiceUtil::OnDlcStateChangedConnect,
                         weak_ptr_factory_.GetWeakPtr()));
      return;
    }

    // Called with "--uninstall".
    if (action_ == kUninstall) {
      if (Uninstall(false)) {
        Quit();
        return;
      }
    }

    // Called with "--purge".
    if (action_ == kPurge) {
      if (Uninstall(true)) {
        Quit();
        return;
      }
    }

    // Called with "--deploy".
    if (action_ == kDeploy) {
      if (Deploy()) {
        Quit();
        return;
      }
    }

    // Called with "--dlc_state".
    if (action_ == kDlcState) {
      DlcState state;
      if (!GetDlcState(dlc_id_, &state)) {
        QuitWithExitCode(EX_SOFTWARE);
        return;
      }
      PrintDlcState(dump_, state);
      Quit();
      return;
    }

    QuitWithExitCode(EX_SOFTWARE);
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

  void TimeoutQuit() {
    LOG(ERROR) << "dlcservice_util command timeout.";
    QuitWithExitCode(EX_SOFTWARE);
  }

  // Callback invoked on receiving |OnDlcStateChanged| signal.
  void OnDlcStateChanged(const DlcState& dlc_state) {
    // Ignore the status as it's not the one we care about.
    if (dlc_state.id() != dlc_id_)
      return;
    switch (dlc_state.state()) {
      case DlcState::INSTALLED:
        LOG(INFO) << "Install successful for DLC: " << dlc_id_;
        Quit();
        break;
      case DlcState::INSTALLING:
        LOG(INFO) << static_cast<int>(dlc_state.progress() * 100)
                  << "% installed DLC: " << dlc_id_;
        break;
      case DlcState::NOT_INSTALLED:
        if (dlc_state.last_error_code() == dlcservice::kErrorBusy) {
          LOG(INFO) << "Busy error code, posting another installation.";
          PostInstall();
          return;
        }
        LOG(ERROR) << "Failed to install DLC: " << dlc_id_
                   << " with error code: " << dlc_state.last_error_code();
        QuitWithExitCode(EX_SOFTWARE);
        break;
      default:
        NOTREACHED_IN_MIGRATION();
    }
  }

  // Callback invoked on connecting |OnDlcStateChanged| signal.
  void OnDlcStateChangedConnect(const string& interface_name,
                                const string& signal_name,
                                bool success) {
    if (!success) {
      LOG(ERROR) << "Error connecting " << interface_name << "." << signal_name;
      QuitWithExitCode(EX_SOFTWARE);
      return;
    }
    InstallWrapper();
  }

  void PostInstall() {
    if (delayed_install_id_ != brillo::MessageLoop::kTaskIdNull) {
      LOG(INFO) << "Another delayed installation already posted.";
      return;
    }
    delayed_install_id_ = brillo::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&DlcServiceUtil::InstallWrapper,
                       weak_ptr_factory_.GetWeakPtr()),
        base::Seconds(1));
  }

  void InstallWrapper() {
    delayed_install_id_ = brillo::MessageLoop::kTaskIdNull;
    bool retry = false;
    if (Install(retry)) {
      // Don't |Quit()| as we will need to wait for signal of install.
      return;
    }
    if (retry) {
      PostInstall();
      return;
    }
    QuitWithExitCode(EX_SOFTWARE);
    return;
  }

  // Install current DLC module. Returns true if current module can be
  // installed. False otherwise.
  bool Install(bool& retry) {
    brillo::ErrorPtr err;
    LOG(INFO) << "Attempting to install DLC modules: " << dlc_id_;
    // TODO(b/177932564): Temporary increase in timeout to unblock CQ cases that
    // hit DLC installation while dlcservice is busy.
    auto install_request =
        dlcservice::CreateInstallRequest(dlc_id_, omaha_url_, reserve_);
    if (!dlc_service_proxy_->Install(install_request, &err,
                                     /*timeout_ms=*/5 * 60 * 1000)) {
      retry = err->GetCode() == dlcservice::kErrorBusy;
      if (retry) {
        LOG(WARNING) << "Failed to install due to busy status, indicating "
                        "retry to caller: "
                     << ErrorPtrStr(err);
      } else {
        LOG(ERROR) << "Failed to install: " << dlc_id_ << ", "
                   << ErrorPtrStr(err);
      }
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
    LOG(INFO) << "Successfully " << (purge ? "purged" : "uninstalled")
              << " DLC: " << dlc_id_;
    return true;
  }

  bool Deploy() {
    brillo::ErrorPtr err;
    LOG(INFO) << "Attempting to load deployed DLC image: " << dlc_id_;
    if (!dlc_service_proxy_->Deploy(dlc_id_, &err)) {
      LOG(ERROR) << "Failed to load deployed DLC: " << dlc_id_ << ", "
                 << ErrorPtrStr(err);
      return false;
    }
    LOG(INFO) << "Successfully loaded deployed DLC: " << dlc_id_;
    return true;
  }

  bool Unload(dlcservice::UnloadRequest request) {
    brillo::ErrorPtr err;
    LOG(INFO) << "Attempting to unload DLCs";
    if (!dlc_service_proxy_->Unload(request, &err)) {
      LOG(ERROR) << "Failed to unload DLCs: " << ErrorPtrStr(err);
      return false;
    }
    LOG(INFO) << "Successfully unloaded DLCs";
    return true;
  }

  // Gets the state of current DLC module.
  bool GetDlcState(const string& id, DlcState* state) {
    brillo::ErrorPtr err;
    if (!dlc_service_proxy_->GetDlcState(id, state, &err)) {
      LOG(ERROR) << "Failed to get state of DLC " << dlc_id_ << ", "
                 << ErrorPtrStr(err);
      return false;
    }
    return true;
  }

  // Prints the DLC state.
  void PrintDlcState(const FilePath& dump, const DlcState& state) {
    Dict dict;
    dict.Set("id", state.id());
    dict.Set("last_error_code", state.last_error_code());
    dict.Set("progress", state.progress());
    dict.Set("root_path", state.root_path());
    dict.Set("state", state.state());
    dict.Set("is_verified", state.is_verified());
    dict.Set("image_path", state.image_path());
    PrintToFileOrStdout(dump, Value(std::move(dict)));
  }

  // Retrieves a list of all installed DLC modules. Returns true if the list is
  // retrieved successfully, false otherwise. Sets the given error pointer on
  // failure.
  bool GetInstalled(const dlcservice::ListRequest& request,
                    dlcservice::DlcStateList* dlcs) {
    brillo::ErrorPtr err;
    if (!dlc_service_proxy_->GetInstalled2(request, dlcs, &err)) {
      LOG(ERROR) << "Failed to get the list of installed DLC modules, "
                 << ErrorPtrStr(err);
      return false;
    }
    return true;
  }

  // Prints the information for DLCs with content.
  void PrintDlcsWithContent(const FilePath& dump,
                            const dlcservice::DlcsWithContent& dlcs) {
    List list;
    for (const auto& dlc_info : dlcs.dlc_infos()) {
      Dict info;
      info.Set("id", dlc_info.id());
      info.Set("name", dlc_info.name());
      info.Set("description", dlc_info.description());
      info.Set("used_bytes_on_disk",
               base::NumberToString(dlc_info.used_bytes_on_disk()));
      info.Set("is_removable", dlc_info.is_removable());
      list.Append(std::move(info));
    }
    PrintToFileOrStdout(dump, Value(std::move(list)));
  }

  // Retrieves a list of all existing DLC modules. Returns true if the list is
  // retrieved successfully, false otherwise.
  bool GetExisting(dlcservice::DlcsWithContent* dlcs) {
    brillo::ErrorPtr err;
    if (!dlc_service_proxy_->GetExistingDlcs(dlcs, &err)) {
      LOG(ERROR) << "Failed to get the list of existing DLC modules, "
                 << ErrorPtrStr(err);
      return false;
    }
    return true;
  }

  std::shared_ptr<imageloader::Manifest> GetManifest(const string& id) {
    return dlcservice::GetDlcManifest(
        id, FilePath(imageloader::kDlcManifestRootpath));
  }

  // Helper to print to file, or stdout if |path| is empty.
  void PrintToFileOrStdout(const FilePath& path, const Value& value) {
    string json;
    if (!base::JSONWriter::WriteWithOptions(
            value, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json)) {
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

  void PrintInstalled(const FilePath& dump,
                      const dlcservice::DlcStateList& dlcs) {
    Dict dict;
    for (const auto& dlc_state : dlcs.states()) {
      const auto& id = dlc_state.id();
      List dlc_info_list;
      auto manifest = GetManifest(id);
      if (!manifest)
        return;
      Dict dlc_info;
      dlc_info.Set("name", manifest->name());
      dlc_info.Set("id", manifest->id());
      dlc_info.Set("package", manifest->package());
      dlc_info.Set("version", manifest->version());
      dlc_info.Set("preallocated_size",
                   base::NumberToString(manifest->preallocated_size()));
      dlc_info.Set("size", base::NumberToString(manifest->size()));
      dlc_info.Set("image_type", manifest->image_type());
      switch (manifest->fs_type()) {
        case imageloader::FileSystem::kExt2:
          dlc_info.Set("fs-type", "ext2");
          break;
        case imageloader::FileSystem::kExt4:
          dlc_info.Set("fs-type", "ext4");
          break;
        case imageloader::FileSystem::kSquashFS:
          dlc_info.Set("fs-type", "squashfs");
          break;
      }
      dlc_info.Set("root_mount", dlc_state.root_path());
      dlc_info_list.Append(std::move(dlc_info));
      dict.Set(id, std::move(dlc_info_list));
    }

    PrintToFileOrStdout(dump, Value(std::move(dict)));
  }

  std::unique_ptr<DlcServiceInterfaceProxy> dlc_service_proxy_;

  // argc and argv passed to main().
  int argc_;
  const char** argv_;

  // The action to take.
  Action action_;
  // The ID of the current DLC.
  string dlc_id_;
  // Customized Omaha server URL (empty being the default URL).
  string omaha_url_;
  // Reserve the DLC on install success/failure.
  bool reserve_ = false;
  // Select DLCs based on manifest fields.
  std::optional<dlcservice::SelectDlc> select_;
  // Check mount points to confirm installed DLC(s).
  bool check_mount_ = false;
  // Path to dump to.
  FilePath dump_;

  // Delayed install task ID, to not dupe installation calls.
  brillo::MessageLoop::TaskId delayed_install_id_ =
      brillo::MessageLoop::kTaskIdNull;

  base::WeakPtrFactory<DlcServiceUtil> weak_ptr_factory_;

  DlcServiceUtil(const DlcServiceUtil&) = delete;
  DlcServiceUtil& operator=(const DlcServiceUtil&) = delete;
};

int main(int argc, const char** argv) {
  // Check user that is running dlcservice_util.
  switch (getuid()) {
    case kRootUid:
      EnterMinijail();
      break;
    case kChronosUid:
    case kDlcServiceUid:
      break;
    default:
      LOG(ERROR) << "dlcservice_util can only be run as "
                    "root, chronos, or dlcservice";
      return 1;
  }
  DlcServiceUtil client(argc, argv);
  return client.Run();
}
