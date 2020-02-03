// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/socket.h>

#include <linux/vm_sockets.h>  // Needs to come after sys/socket.h

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <chromeos/constants/vm_tools.h>

#include "vm_tools/garcon/desktop_file.h"
#include "vm_tools/garcon/host_notifier.h"
#include "vm_tools/garcon/mime_types_parser.h"

namespace {

constexpr char kHostIpFile[] = "/dev/.host_ip";
constexpr char kSecurityTokenFile[] = "/dev/.container_token";
constexpr int kSecurityTokenLength = 36;
// File extension for desktop files.
constexpr char kDesktopFileExtension[] = ".desktop";
// Directory where the MIME types file is stored for watching with inotify.
constexpr char kMimeTypesDir[] = "/etc";
// File where MIME type information is stored in the container.
constexpr char kMimeTypesFilePath[] = "/etc/mime.types";
// Filename for the user's MIME types file in their home dir.
constexpr char kUserMimeTypesFile[] = ".mime.types";
// Duration over which we coalesce changes to the desktop file system.
constexpr base::TimeDelta kFilesystemChangeCoalesceTime =
    base::TimeDelta::FromSeconds(5);
// Delimiter for the end of a URL scheme.
constexpr char kUrlSchemeDelimiter[] = "://";

std::string GetHostIp() {
  char host_addr[INET_ADDRSTRLEN + 1];
  base::FilePath host_ip_path(kHostIpFile);
  int num_read = base::ReadFile(host_ip_path, host_addr, sizeof(host_addr) - 1);
  if (num_read <= 0) {
    LOG(ERROR) << "Failed reading the host IP from: "
               << host_ip_path.MaybeAsASCII();
    return "";
  }
  host_addr[num_read] = '\0';
  return std::string(host_addr);
}

std::string GetSecurityToken() {
  char token[kSecurityTokenLength + 1];
  base::FilePath security_token_path(kSecurityTokenFile);
  int num_read = base::ReadFile(security_token_path, token, sizeof(token) - 1);
  if (num_read <= 0) {
    LOG(ERROR) << "Failed reading the container token from: "
               << security_token_path.MaybeAsASCII();
    return "";
  }
  token[num_read] = '\0';
  return std::string(token);
}

void SendInstallStatusToHost(
    vm_tools::container::ContainerListener::Stub* stub,
    vm_tools::container::InstallLinuxPackageProgressInfo progress_info) {
  grpc::ClientContext ctx;
  vm_tools::EmptyMessage empty;
  grpc::Status grpc_status =
      stub->InstallLinuxPackageProgress(&ctx, progress_info, &empty);
  if (!grpc_status.ok()) {
    LOG(WARNING) << "Failed to notify host system about install status: "
                 << grpc_status.error_message();
  }
}

void SendUninstallStatusToHost(
    vm_tools::container::ContainerListener::Stub* stub,
    vm_tools::container::UninstallPackageProgressInfo info) {
  grpc::ClientContext ctx;
  vm_tools::EmptyMessage empty;
  grpc::Status grpc_status = stub->UninstallPackageProgress(&ctx, info, &empty);
  if (!grpc_status.ok()) {
    LOG(WARNING) << "Failed to notify host system about uninstall status: "
                 << grpc_status.error_message() << " (code "
                 << grpc_status.error_code() << ")";
  }
}

void SendApplyAnsiblePlaybookStatusToHost(
    vm_tools::container::ContainerListener::Stub* stub,
    vm_tools::container::ApplyAnsiblePlaybookProgressInfo info) {
  grpc::ClientContext ctx;
  vm_tools::EmptyMessage empty;
  grpc::Status grpc_status =
      stub->ApplyAnsiblePlaybookProgress(&ctx, info, &empty);
  if (!grpc_status.ok()) {
    LOG(WARNING) << "Failed to notify host system about ansible playbook "
                 << "application status: " << grpc_status.error_message()
                 << " (code " << grpc_status.error_code() << ")";
  }
}

}  // namespace

namespace vm_tools {
namespace garcon {

// static
std::unique_ptr<HostNotifier> HostNotifier::Create(
    base::Closure shutdown_closure) {
  return base::WrapUnique(new HostNotifier(std::move(shutdown_closure)));
}

// static
bool HostNotifier::OpenUrlInHost(const std::string& url) {
  std::string host_ip = GetHostIp();
  std::string token = GetSecurityToken();
  if (token.empty() || host_ip.empty()) {
    return false;
  }
  std::unique_ptr<vm_tools::container::ContainerListener::Stub> stub;
  stub = std::make_unique<vm_tools::container::ContainerListener::Stub>(
      grpc::CreateChannel(base::StringPrintf("vsock:%d:%u", VMADDR_CID_HOST,
                          vm_tools::kGarconPort),
                          grpc::InsecureChannelCredentials()));
  grpc::ClientContext ctx;
  vm_tools::container::OpenUrlRequest url_request;
  url_request.set_token(token);
  url_request.set_url(url);
  // If url has no scheme, but matches a local file, then convert to file://.
  auto front = url.find(kUrlSchemeDelimiter);
  if (front == std::string::npos) {
    base::FilePath path(url);
    base::FilePath abs = base::MakeAbsoluteFilePath(path);
    if (!abs.empty()) {
      url_request.set_url("file://" + abs.value());
    }
  }
  vm_tools::EmptyMessage empty;
  grpc::Status status = stub->OpenUrl(&ctx, url_request, &empty);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to request host system to open url \"" << url
                 << "\" error: " << status.error_message();
    return false;
  }
  return true;
}

bool HostNotifier::OpenTerminal(std::vector<std::string> args) {
  std::string host_ip = GetHostIp();
  std::string token = GetSecurityToken();
  if (token.empty() || host_ip.empty()) {
    return false;
  }
  std::unique_ptr<vm_tools::container::ContainerListener::Stub> stub;
  stub = std::make_unique<vm_tools::container::ContainerListener::Stub>(
      grpc::CreateChannel(base::StringPrintf("vsock:%d:%u", VMADDR_CID_HOST,
                          vm_tools::kGarconPort),
                          grpc::InsecureChannelCredentials()));
  grpc::ClientContext ctx;
  vm_tools::container::OpenTerminalRequest terminal_request;
  std::copy(std::make_move_iterator(args.begin()),
            std::make_move_iterator(args.end()),
            google::protobuf::RepeatedFieldBackInserter(
                terminal_request.mutable_params()));
  terminal_request.set_token(token);

  vm_tools::EmptyMessage empty;
  grpc::Status status = stub->OpenTerminal(&ctx, terminal_request, &empty);
  if (!status.ok()) {
    LOG(WARNING) << "Failed request to open terminal, error: "
                 << status.error_message();
    return false;
  }
  return true;
}

HostNotifier::HostNotifier(base::Closure shutdown_closure)
    : update_app_list_posted_(false),
      send_app_list_to_host_in_progress_(false),
      update_mime_types_posted_(false),
      shutdown_closure_(std::move(shutdown_closure)) {}

HostNotifier::~HostNotifier() = default;

void HostNotifier::OnSignalReadable() {
  signalfd_siginfo info;
  if (read(signal_fd_.get(), &info, sizeof(info)) != sizeof(info)) {
    PLOG(ERROR) << "Failed to read from signalfd";
  }
  DCHECK_EQ(info.ssi_signo, SIGTERM);
  // Notify the host we are shutting down, then inform our run loop to terminate
  // which should then shut us down, deallocate us and then also terminate the
  // gRPC thread.
  NotifyHostOfContainerShutdown();
  task_runner_->PostTask(FROM_HERE, shutdown_closure_);
}

void HostNotifier::OnInstallCompletion(const std::string& command_uuid,
                                       bool success,
                                       const std::string& failure_reason) {
  vm_tools::container::InstallLinuxPackageProgressInfo progress_info;
  progress_info.set_token(token_);
  progress_info.set_status(
      success ? vm_tools::container::InstallLinuxPackageProgressInfo::SUCCEEDED
              : vm_tools::container::InstallLinuxPackageProgressInfo::FAILED);
  progress_info.set_failure_details(failure_reason);
  progress_info.set_command_uuid(command_uuid);
  task_runner_->PostTask(FROM_HERE, base::Bind(&SendInstallStatusToHost,
                                               base::Unretained(stub_.get()),
                                               std::move(progress_info)));
}

void HostNotifier::OnInstallProgress(
    const std::string& command_uuid,
    vm_tools::container::InstallLinuxPackageProgressInfo::Status status,
    uint32_t percent_progress) {
  vm_tools::container::InstallLinuxPackageProgressInfo progress_info;
  progress_info.set_token(token_);
  progress_info.set_status(status);
  progress_info.set_progress_percent(percent_progress);
  progress_info.set_command_uuid(command_uuid);
  task_runner_->PostTask(FROM_HERE, base::Bind(&SendInstallStatusToHost,
                                               base::Unretained(stub_.get()),
                                               std::move(progress_info)));
}

void HostNotifier::OnUninstallCompletion(bool success,
                                         const std::string& failure_reason) {
  LOG(INFO) << "Got HostNotifier::OnUninstallCompletion(" << success << ", "
            << failure_reason << ")";
  vm_tools::container::UninstallPackageProgressInfo info;
  info.set_token(token_);
  if (success) {
    info.set_status(
        vm_tools::container::UninstallPackageProgressInfo::SUCCEEDED);
  } else {
    info.set_status(vm_tools::container::UninstallPackageProgressInfo::FAILED);
    info.set_failure_details(failure_reason);
  }
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&SendUninstallStatusToHost,
                            base::Unretained(stub_.get()), std::move(info)));
}

void HostNotifier::OnUninstallProgress(uint32_t percent_progress) {
  VLOG(3) << "Got HostNotifier::OnUninstallProgress(" << percent_progress
          << ")";
  vm_tools::container::UninstallPackageProgressInfo info;
  info.set_token(token_);
  info.set_status(
      vm_tools::container::UninstallPackageProgressInfo::UNINSTALLING);
  info.set_progress_percent(percent_progress);
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&SendUninstallStatusToHost,
                            base::Unretained(stub_.get()), std::move(info)));
}

void HostNotifier::OnApplyAnsiblePlaybookCompletion(
    bool success, const std::string& failure_reason) {
  LOG(INFO) << "Got HostNotifier::OnApplyAnsiblePlaybookCompletion(" << success
            << ", " << failure_reason << ")";
  vm_tools::container::ApplyAnsiblePlaybookProgressInfo info;
  info.set_token(token_);
  if (success) {
    info.set_status(
        vm_tools::container::ApplyAnsiblePlaybookProgressInfo::SUCCEEDED);
  } else {
    info.set_status(
        vm_tools::container::ApplyAnsiblePlaybookProgressInfo::FAILED);
    info.set_failure_details(failure_reason);
  }
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&SendApplyAnsiblePlaybookStatusToHost,
                            base::Unretained(stub_.get()), std::move(info)));
}

bool HostNotifier::Init(uint32_t vsock_port,
                        PackageKitProxy* package_kit_proxy) {
  CHECK(package_kit_proxy);
  package_kit_proxy_ = package_kit_proxy;
  task_runner_ = base::ThreadTaskRunnerHandle::Get();
  std::string host_ip = GetHostIp();
  token_ = GetSecurityToken();
  if (token_.empty() || host_ip.empty()) {
    return false;
  }
  SetUpContainerListenerStub(std::move(host_ip));
  if (!NotifyHostGarconIsReady(vsock_port)) {
    return false;
  }

  // Start listening for SIGTERM.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);

  signal_fd_.reset(signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK));
  if (!signal_fd_.is_valid()) {
    PLOG(ERROR) << "Unable to create signalfd";
    return false;
  }

  signal_controller_ = base::FileDescriptorWatcher::WatchReadable(
      signal_fd_.get(), base::BindRepeating(&HostNotifier::OnSignalReadable,
                                            base::Unretained(this)));
  if (!signal_controller_) {
    LOG(ERROR) << "Failed to watch signal file descriptor";
    return false;
  }

  // Block the standard SIGTERM handler since we will be getting it via the
  // signalfd. We have to do this before we setup the file path watcher
  // because that will end up spawning another thread for each watcher.
  if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
    PLOG(ERROR) << "Failed blocking standard SIGTERM handler";
    return false;
  }

  // Setup all of our watchers for changes to any of the paths where .desktop
  // files may reside.
  std::vector<base::FilePath> watch_paths =
      DesktopFile::GetPathsForDesktopFiles();
  for (auto& path : watch_paths) {
    std::unique_ptr<base::FilePathWatcher> watcher =
        std::make_unique<base::FilePathWatcher>();
    if (!watcher->Watch(path, true,
                        base::Bind(&HostNotifier::DesktopPathsChanged,
                                   base::Unretained(this)))) {
      LOG(ERROR) << "Failed setting up filesystem path watcher for dir: "
                 << path.value();
      // Probably better to just watch the dirs we can rather than terminate
      // garcon altogether.
      continue;
    }
    watchers_.emplace_back(std::move(watcher));
  }

  // We can only watch directories and on changes we aren't notified which
  // file changes, so we end up watching for any changes in /etc or $HOME.

  // Also setup the watcher for the /etc/mime.types file.
  std::unique_ptr<base::FilePathWatcher> mime_type_watcher =
      std::make_unique<base::FilePathWatcher>();
  base::FilePath mime_type_path(kMimeTypesDir);
  if (!mime_type_watcher->Watch(mime_type_path, false,
                                base::Bind(&HostNotifier::MimeTypesChanged,
                                           base::Unretained(this)))) {
    LOG(ERROR) << "Failed setting up filesystem path watcher for: "
               << kMimeTypesDir;
  }
  watchers_.emplace_back(std::move(mime_type_watcher));

  // Also setup the watcher for the $HOME/.mime.types file.
  std::unique_ptr<base::FilePathWatcher> home_mime_type_watcher =
      std::make_unique<base::FilePathWatcher>();
  if (!home_mime_type_watcher->Watch(base::GetHomeDir(), false,
                                     base::Bind(&HostNotifier::MimeTypesChanged,
                                                base::Unretained(this)))) {
    LOG(ERROR) << "Failed setting up filesystem path watcher for: "
               << base::GetHomeDir().value();
  }
  watchers_.emplace_back(std::move(home_mime_type_watcher));

  // If this fails, don't terminate ourself, this could be some kind of
  // transient failure.
  SendAppListToHost();
  SendMimeTypesToHost();

  return true;
}

bool HostNotifier::NotifyHostGarconIsReady(uint32_t vsock_port) {
  // Notify the host system that we are ready.
  grpc::ClientContext ctx;
  vm_tools::container::ContainerStartupInfo startup_info;
  startup_info.set_token(token_);
  startup_info.set_garcon_port(vsock_port);
  vm_tools::EmptyMessage empty;
  grpc::Status status = stub_->ContainerReady(&ctx, startup_info, &empty);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to notify host system that container is ready: "
                 << status.error_message();
    return false;
  }
  return true;
}

void HostNotifier::NotifyHostOfContainerShutdown() {
  // Notify the host system that we are shutting down.
  grpc::ClientContext ctx;
  vm_tools::container::ContainerShutdownInfo shutdown_info;
  shutdown_info.set_token(token_);
  vm_tools::EmptyMessage empty;
  grpc::Status status = stub_->ContainerShutdown(&ctx, shutdown_info, &empty);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to notify host system that container is shutting "
                 << "down: " << status.error_message();
  }
}

void HostNotifier::NotifyHostOfPendingAppListUpdates() {
  grpc::ClientContext ctx;
  vm_tools::container::PendingAppListUpdateCount msg;
  msg.set_token(token_);
  msg.set_count(update_app_list_posted_ + send_app_list_to_host_in_progress_);
  vm_tools::EmptyMessage empty;
  grpc::Status status =
      stub_->PendingUpdateApplicationListCalls(&ctx, msg, &empty);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to notify host system of pending app list updates: "
                 << status.error_message();
  }
}

void HostNotifier::SendAppListToHost() {
  if (send_app_list_to_host_in_progress_) {
    // Don't have multiple SendAppListToHost callback chains happening at the
    // same time. Delay the next run a little longer.
    //
    // Checking a boolean isn't a race condition because all the callbacks are
    // on the same thread.
    task_runner_->PostDelayedTask(
        FROM_HERE,
        base::Bind(&HostNotifier::SendAppListToHost, base::Unretained(this)),
        kFilesystemChangeCoalesceTime);
    return;
  }

  auto callback_state = std::make_unique<AppListBuilderState>();
  callback_state->request.set_token(token_);

  // If we hit duplicate IDs, then we are supposed to use the first one only.
  std::set<std::string> unique_app_ids;

  // Get the list of directories that we should search for .desktop files
  // recursively and then perform the search.
  std::vector<base::FilePath> search_paths =
      DesktopFile::GetPathsForDesktopFiles();
  for (auto curr_path : search_paths) {
    base::FileEnumerator file_enum(curr_path, true,
                                   base::FileEnumerator::FILES);
    for (base::FilePath enum_path = file_enum.Next(); !enum_path.empty();
         enum_path = file_enum.Next()) {
      if (enum_path.FinalExtension() != kDesktopFileExtension) {
        continue;
      }
      // We have a .desktop file path, parse it and then add it to the
      // protobuf if it parses successfully.
      std::unique_ptr<DesktopFile> desktop_file =
          DesktopFile::ParseDesktopFile(enum_path);
      if (!desktop_file) {
        LOG(WARNING) << "Failed parsing the .desktop file: "
                     << enum_path.value();
        continue;
      }
      // If we have already seen this desktop file ID then don't analyze this
      // one. We want to check this before we do the filtering to allow users
      // to put .desktop files in local locations to hide applications in
      // system locations.
      if (!unique_app_ids.insert(desktop_file->app_id()).second) {
        continue;
      }
      // Make sure this .desktop file is one we should send to the host.
      // There are various cases where we do not want to transmit certain
      // .desktop files.
      if (!desktop_file->ShouldPassToHost()) {
        continue;
      }
      // Add this app to the list in the protobuf and populate all of its
      // fields.
      vm_tools::container::Application* app =
          callback_state->request.add_application();
      app->set_desktop_file_id(desktop_file->app_id());
      const std::map<std::string, std::string>& name_map =
          desktop_file->locale_name_map();
      vm_tools::container::Application::LocalizedString* names =
          app->mutable_name();
      for (const auto& name_entry : name_map) {
        vm_tools::container::Application::LocalizedString::StringWithLocale*
            locale_string = names->add_values();
        locale_string->set_locale(name_entry.first);
        locale_string->set_value(name_entry.second);
      }
      const std::map<std::string, std::string>& comment_map =
          desktop_file->locale_comment_map();
      vm_tools::container::Application::LocalizedString* comments =
          app->mutable_comment();
      for (const auto& comment_entry : comment_map) {
        vm_tools::container::Application::LocalizedString::StringWithLocale*
            locale_string = comments->add_values();
        locale_string->set_locale(comment_entry.first);
        locale_string->set_value(comment_entry.second);
      }
      const std::map<std::string, std::vector<std::string>>& keywords_map =
          desktop_file->locale_keywords_map();
      vm_tools::container::Application::LocaleStrings* keyword =
          app->mutable_keywords();
      for (const auto& keywords_entry : keywords_map) {
        vm_tools::container::Application::LocaleStrings::StringsWithLocale*
            locale_string = keyword->add_values();
        locale_string->set_locale(keywords_entry.first);
        for (const auto& curr_keyword : keywords_entry.second) {
          locale_string->add_value(curr_keyword);
        }
      }
      for (const auto& mime_type : desktop_file->mime_types()) {
        app->add_mime_types(mime_type);
      }

      app->set_no_display(desktop_file->no_display());
      app->set_startup_wm_class(desktop_file->startup_wm_class());
      app->set_startup_notify(desktop_file->startup_notify());
      app->set_executable_file_name(desktop_file->GenerateExecutableFileName());

      callback_state->desktop_files_for_application.push_back(enum_path);
    }
  }

  CHECK_EQ(callback_state->desktop_files_for_application.size(),
           callback_state->request.application_size());

  // We now want to query all the .desktop files to see what package owns them.
  // Unforuntately, this requires D-Bus calls to the PackageKit, and we are on
  // the D-Bus thread. So we can't receive the results until this function
  // returns, so we need to set up a series of callbacks.
  //
  // Query each .desktop file in turn. The callback will record the info for
  // that file and also kick off the query for the next file until all files
  // have been queried.
  callback_state->num_package_id_queries_completed = 0;

  // Clear this in case it was set, this all happens on the same thread.
  // Clear this now, not when the package_id callbacks are complete, in case
  // we get another notification while this is still in flight; we'd want to run
  // this function again in that case.
  update_app_list_posted_ = false;

  // Don't start another round of callbacks while still trying to finish this
  // round.
  send_app_list_to_host_in_progress_ = true;

  RequestNextPackageIdOrCompleteUpdateApplicationList(
      std::move(callback_state));
}

void HostNotifier::RequestNextPackageIdOrCompleteUpdateApplicationList(
    std::unique_ptr<AppListBuilderState> state) {
  if ((state->num_package_id_queries_completed >=
       state->desktop_files_for_application.size())) {
    // We have finished all package_id queries. This data is ready to send to
    // the host.
    send_app_list_to_host_in_progress_ = false;
    vm_tools::EmptyMessage empty;
    grpc::ClientContext ctx;
    grpc::Status status =
        stub_->UpdateApplicationList(&ctx, state->request, &empty);
    VLOG(3) << "UpdatedApplicationList\n" << state->request.DebugString();
    if (!status.ok()) {
      LOG(WARNING) << "Failed to notify host of the application list: "
                   << status.error_message();
    }
    NotifyHostOfPendingAppListUpdates();
    return;
  }
  // else we still need to do more package_id queries
  package_kit_proxy_->SearchLinuxPackagesForFile(
      state->desktop_files_for_application
          [state->num_package_id_queries_completed],
      base::Bind(&HostNotifier::PackageIdCallback, base::Unretained(this),
                 base::Passed(&state)));
}

void HostNotifier::PackageIdCallback(
    std::unique_ptr<AppListBuilderState> state,
    bool success,
    bool pkg_found,
    const PackageKitProxy::LinuxPackageInfo& pkg_info,
    const std::string& error) {
  // The data passed in the parameters is for the Application at
  // state->request.application[state->num_package_id_queries_completed]
  CHECK_LT(state->num_package_id_queries_completed,
           state->request.application_size());
  if (success && pkg_found) {
    vm_tools::container::Application* application =
        state->request.mutable_application(
            state->num_package_id_queries_completed);
    application->set_package_id(pkg_info.package_id);
  } else if (!success) {
    LOG(ERROR) << "Failed to get Package Info: " << error;
  }

  state->num_package_id_queries_completed++;
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(
          &HostNotifier::RequestNextPackageIdOrCompleteUpdateApplicationList,
          base::Unretained(this), base::Passed(&state)));
}

void HostNotifier::SendMimeTypesToHost() {
  vm_tools::container::UpdateMimeTypesRequest request;
  request.set_token(token_);
  vm_tools::EmptyMessage empty;

  // Clear this in case it was set, this all happens on the same thread.
  update_mime_types_posted_ = false;

  MimeTypeMap mime_type_map;
  if (!ParseMimeTypes(kMimeTypesFilePath, &mime_type_map)) {
    LOG(ERROR) << "Failed parsing system mime types, will not send the list to "
               << "host";
    return;
  }
  // The user MIME types may not be set up, so we ignore failures here. User
  // values override system values, so parse this one second so they get
  // overridden.
  ParseMimeTypes(base::GetHomeDir().Append(kUserMimeTypesFile).value(),
                 &mime_type_map);

  request.mutable_mime_type_mappings()->insert(
      std::make_move_iterator(mime_type_map.begin()),
      std::make_move_iterator(mime_type_map.end()));
  // Now make the gRPC call to send this list to the host.
  grpc::ClientContext ctx;
  grpc::Status status = stub_->UpdateMimeTypes(&ctx, request, &empty);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to notify host of the MIME types: "
                 << status.error_message();
  }
}

void HostNotifier::DesktopPathsChanged(const base::FilePath& path, bool error) {
  if (error) {
    // This should never occur because the implementation for Linux never calls
    // this with an error.
    LOG(ERROR) << "Error detected in file path watching for path: "
               << path.value();
    return;
  }

  // We don't want to trigger an update every time there's a change, instead
  // wait a bit and coalesce potential groups of changes that may occur. We
  // don't want to wait too long though because then the user may feel that it
  // is unresponsive in newly installed applications not showing up in the
  // launcher when they check.
  if (update_app_list_posted_) {
    return;
  }
  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::Bind(&HostNotifier::SendAppListToHost, base::Unretained(this)),
      kFilesystemChangeCoalesceTime);
  update_app_list_posted_ = true;
  NotifyHostOfPendingAppListUpdates();
}

void HostNotifier::MimeTypesChanged(const base::FilePath& path, bool error) {
  if (error) {
    // This should never occur because the implementation for Linux never calls
    // this with an error.
    LOG(ERROR) << "Error detected in file path watching for path: "
               << path.value();
    return;
  }

  // Coalesce these calls if we have one pending.
  if (update_mime_types_posted_) {
    return;
  }
  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::Bind(&HostNotifier::SendMimeTypesToHost, base::Unretained(this)),
      kFilesystemChangeCoalesceTime);
  update_mime_types_posted_ = true;
}

void HostNotifier::SetUpContainerListenerStub(const std::string& host_ip) {
  stub_ = std::make_unique<vm_tools::container::ContainerListener::Stub>(
      grpc::CreateChannel(base::StringPrintf("vsock:%d:%u", VMADDR_CID_HOST,
                                             vm_tools::kGarconPort),
                          grpc::InsecureChannelCredentials()));
}

}  // namespace garcon
}  // namespace vm_tools
