// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_adaptor.h"

#include <cstdint>
#include <set>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/containers/contains.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <dbus/dlp/dbus-constants.h>
#include <google/protobuf/message_lite.h>
#include <session_manager/dbus-proxies.h>

#include "dlp/proto_bindings/dlp_service.pb.h"

namespace dlp {

namespace {

// Override watched directory for tests.
base::FilePath g_downloads_path_for_testing;

// Serializes |proto| to a vector of bytes. CHECKs for success (should
// never fail if there are no required proto fields).
std::vector<uint8_t> SerializeProto(
    const google::protobuf::MessageLite& proto) {
  std::vector<uint8_t> proto_blob(proto.ByteSizeLong());
  CHECK(proto.SerializeToArray(proto_blob.data(), proto_blob.size()));
  return proto_blob;
}

// Parses a proto from an array of bytes |proto_blob|. Returns
// error message or empty string if no error.
std::string ParseProto(const base::Location& from_here,
                       google::protobuf::MessageLite* proto,
                       const std::vector<uint8_t>& proto_blob) {
  if (!proto->ParseFromArray(proto_blob.data(), proto_blob.size())) {
    const std::string error_message = "Failed to parse proto message.";
    LOG(ERROR) << from_here.ToString() << " " << error_message;
    return error_message;
  }
  return "";
}

// Calls Session Manager to get the user hash for the primary session. Returns
// an empty string and logs on error.
std::string GetSanitizedUsername(brillo::dbus_utils::DBusObject* dbus_object) {
  std::string username;
  std::string sanitized_username;
  brillo::ErrorPtr error;
  org::chromium::SessionManagerInterfaceProxy proxy(dbus_object->GetBus());
  if (!proxy.RetrievePrimarySession(&username, &sanitized_username, &error)) {
    const char* error_msg =
        error ? error->GetMessage().c_str() : "Unknown error.";
    LOG(ERROR) << "Call to RetrievePrimarySession failed. " << error_msg;
    return std::string();
  }
  return sanitized_username;
}

FileEntry ConvertToFileEntry(ino_t inode, AddFileRequest request) {
  FileEntry result;
  result.inode = inode;
  if (request.has_source_url())
    result.source_url = request.source_url();
  if (request.has_referrer_url())
    result.referrer_url = request.referrer_url();
  return result;
}

base::FilePath GetUserDownloadsPath(const std::string& username) {
  if (!g_downloads_path_for_testing.empty())
    return g_downloads_path_for_testing;
  // TODO(crbug.com/1200575): Refactor to not hardcode it.
  return base::FilePath("/home/chronos/")
      .Append("u-" + username)
      .Append("MyFiles/Downloads");
}

std::set<ino64_t> EnumerateInodes(const base::FilePath& root_path) {
  std::set<ino64_t> inodes;
  base::FileEnumerator enumerator(root_path, /*recursive=*/true,
                                  base::FileEnumerator::FILES);
  for (base::FilePath entry = enumerator.Next(); !entry.empty();
       entry = enumerator.Next()) {
    inodes.insert(enumerator.GetInfo().stat().st_ino);
  }

  return inodes;
}

}  // namespace

DlpAdaptor::DlpAdaptor(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object)
    : org::chromium::DlpAdaptor(this), dbus_object_(std::move(dbus_object)) {
  dlp_files_policy_service_ =
      std::make_unique<org::chromium::DlpFilesPolicyServiceProxy>(
          dbus_object_->GetBus().get(), kDlpFilesPolicyServiceName);
}

DlpAdaptor::~DlpAdaptor() = default;

void DlpAdaptor::InitDatabaseOnCryptohome() {
  const std::string sanitized_username =
      GetSanitizedUsername(dbus_object_.get());
  if (sanitized_username.empty()) {
    LOG(ERROR) << "No active user, can't open the database";
    return;
  }
  const base::FilePath database_path = base::FilePath("/run/daemon-store/dlp/")
                                           .Append(sanitized_username)
                                           .Append("database");
  if (!base::CreateDirectory(database_path)) {
    PLOG(ERROR) << "Can't create database directory";
    return;
  }
  InitDatabase(database_path, /*init_callback=*/base::DoNothing());
}

void DlpAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction
        completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(std::move(completion_callback));
}

std::vector<uint8_t> DlpAdaptor::SetDlpFilesPolicy(
    const std::vector<uint8_t>& request_blob) {
  LOG(INFO) << "Received DLP files policy.";

  SetDlpFilesPolicyRequest request;
  std::string error_message = ParseProto(FROM_HERE, &request, request_blob);

  SetDlpFilesPolicyResponse response;
  if (!error_message.empty()) {
    response.set_error_message(error_message);
    return SerializeProto(response);
  }

  policy_rules_ =
      std::vector<DlpFilesRule>(request.rules().begin(), request.rules().end());

  if (!policy_rules_.empty()) {
    EnsureFanotifyWatcherStarted();
  } else {
    fanotify_watcher_.reset();
  }

  return SerializeProto(response);
}

std::vector<uint8_t> DlpAdaptor::AddFile(
    const std::vector<uint8_t>& request_blob) {
  AddFileRequest request;
  AddFileResponse response;

  const std::string parse_error = ParseProto(FROM_HERE, &request, request_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse AddFile request: " << parse_error;
    response.set_error_message(parse_error);
    return SerializeProto(response);
  }

  LOG(INFO) << "Adding file to the database: " << request.file_path();
  if (!db_) {
    LOG(ERROR) << "Database is not ready";
    response.set_error_message("Database is not ready");
    return SerializeProto(response);
  }

  const ino_t inode = GetInodeValue(request.file_path());
  if (!inode) {
    LOG(ERROR) << "Failed to get inode";
    response.set_error_message("Failed to get inode");
    return SerializeProto(response);
  }

  FileEntry file_entry = ConvertToFileEntry(inode, request);
  if (!db_->InsertFileEntry(file_entry)) {
    LOG(ERROR) << "Failed to add entry to database";
    response.set_error_message("Failed to add entry to database");
    return SerializeProto(response);
  }

  return SerializeProto(response);
}

void DlpAdaptor::RequestFileAccess(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        std::vector<uint8_t>,
        brillo::dbus_utils::FileDescriptor>> response,
    const std::vector<uint8_t>& request_blob) {
  base::ScopedFD local_fd, remote_fd;
  if (!base::CreatePipe(&local_fd, &remote_fd, /*non_blocking=*/true)) {
    PLOG(ERROR) << "Failed to create lifeline pipe";
    std::move(response)->ReplyWithError(
        FROM_HERE, brillo::errors::dbus::kDomain, dlp::kErrorFailedToCreatePipe,
        "Failed to create lifeline pipe");
    return;
  }
  RequestFileAccessRequest request;

  const std::string parse_error = ParseProto(FROM_HERE, &request, request_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse RequestFileAccess request: " << parse_error;
    ReplyOnRequestFileAccess(std::move(response), std::move(remote_fd),
                             /*allowed=*/false, parse_error);
    return;
  }

  if (!db_) {
    ReplyOnRequestFileAccess(std::move(response), std::move(remote_fd),
                             /*allowed=*/true,
                             /*error_message=*/std::string());
    return;
  }

  IsFilesTransferRestrictedRequest matching_request;
  std::vector<uint64_t> inodes;
  for (const auto& file_path : request.files_paths()) {
    const ino_t inode_n = GetInodeValue(file_path);
    std::optional<FileEntry> file_entry = db_->GetFileEntryByInode(inode_n);
    if (!file_entry) {
      // Skip file if it's not DLP-protected as access to it is always allowed.
      continue;
    }
    inodes.push_back(inode_n);

    FileMetadata* file_metadata = matching_request.add_transferred_files();
    file_metadata->set_inode(inode_n);
    file_metadata->set_source_url(file_entry->source_url);
    file_metadata->set_path(file_path);
  }
  // If access to all requested files was allowed, return immediately.
  if (inodes.empty()) {
    ReplyOnRequestFileAccess(std::move(response), std::move(remote_fd),
                             /*allowed=*/true,
                             /*error_message=*/std::string());
    return;
  }

  std::pair<RequestFileAccessCallback, RequestFileAccessCallback> callbacks =
      base::SplitOnceCallback(base::BindOnce(
          &DlpAdaptor::ReplyOnRequestFileAccess, base::Unretained(this),
          std::move(response), std::move(remote_fd)));

  if (request.has_destination_url())
    matching_request.set_destination_url(request.destination_url());
  if (request.has_destination_component())
    matching_request.set_destination_component(request.destination_component());

  matching_request.set_file_action(FileAction::TRANSFER);

  dlp_files_policy_service_->IsFilesTransferRestrictedAsync(
      SerializeProto(matching_request),
      base::BindOnce(&DlpAdaptor::OnRequestFileAccess, base::Unretained(this),
                     std::move(inodes), request.process_id(),
                     std::move(local_fd), std::move(callbacks.first)),
      base::BindOnce(&DlpAdaptor::OnRequestFileAccessError,
                     base::Unretained(this), std::move(callbacks.second)),
      /*timeout_ms=*/base::Minutes(5).InMilliseconds());
}

std::vector<uint8_t> DlpAdaptor::GetFilesSources(
    const std::vector<uint8_t>& request_blob) {
  GetFilesSourcesRequest request;
  GetFilesSourcesResponse response_proto;
  const std::string parse_error = ParseProto(FROM_HERE, &request, request_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse GetFilesSources request: " << parse_error;
    response_proto.set_error_message(parse_error);
    return SerializeProto(response_proto);
  }

  if (!db_) {
    return SerializeProto(response_proto);
  }

  for (const auto& file_inode : request.files_inodes()) {
    std::optional<FileEntry> file_entry = db_->GetFileEntryByInode(file_inode);
    if (file_entry) {
      FileMetadata* file_metadata = response_proto.add_files_metadata();
      file_metadata->set_inode(file_inode);
      file_metadata->set_source_url(file_entry->source_url);
    }
  }

  return SerializeProto(response_proto);
}

void DlpAdaptor::CheckFilesTransfer(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    const std::vector<uint8_t>& request_blob) {
  CheckFilesTransferRequest request;
  CheckFilesTransferResponse response_proto;
  const std::string parse_error = ParseProto(FROM_HERE, &request, request_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse GetFilesSources request: " << parse_error;
    response_proto.set_error_message(parse_error);
    response->Return(SerializeProto(response_proto));
    return;
  }

  if (!db_) {
    response_proto.set_error_message("Database is not ready");
    response->Return(SerializeProto(response_proto));
    return;
  }

  IsFilesTransferRestrictedRequest matching_request;
  base::flat_set<std::string> transferred_files;
  for (const auto& file_path : request.files_paths()) {
    const ino_t file_inode = GetInodeValue(file_path);
    std::optional<FileEntry> file_entry = db_->GetFileEntryByInode(file_inode);
    if (!file_entry) {
      // Skip file if it's not DLP-protected as access to it is always allowed.
      continue;
    }

    transferred_files.insert(file_path);

    FileMetadata* file_metadata = matching_request.add_transferred_files();
    file_metadata->set_inode(file_inode);
    file_metadata->set_source_url(file_entry->source_url);
    file_metadata->set_path(file_path);
  }

  if (transferred_files.empty()) {
    response->Return(SerializeProto(response_proto));
    return;
  }

  if (request.has_destination_url())
    matching_request.set_destination_url(request.destination_url());
  if (request.has_destination_component())
    matching_request.set_destination_component(request.destination_component());
  if (request.has_file_action())
    matching_request.set_file_action(request.file_action());

  auto callbacks = base::SplitOnceCallback(
      base::BindOnce(&DlpAdaptor::ReplyOnCheckFilesTransfer,
                     base::Unretained(this), std::move(response)));

  dlp_files_policy_service_->IsFilesTransferRestrictedAsync(
      SerializeProto(matching_request),
      base::BindOnce(&DlpAdaptor::OnIsFilesTransferRestricted,
                     base::Unretained(this), std::move(transferred_files),
                     std::move(callbacks.first)),
      base::BindOnce(&DlpAdaptor::OnIsFilesTransferRestrictedError,
                     base::Unretained(this), std::move(callbacks.second)),
      /*timeout_ms=*/base::Minutes(5).InMilliseconds());
}

void DlpAdaptor::InitDatabase(const base::FilePath database_path,
                              base::OnceClosure init_callback) {
  LOG(INFO) << "Opening database in: " << database_path.value();
  const base::FilePath database_file = database_path.Append("database");
  if (!base::PathExists(database_file)) {
    LOG(INFO) << "Creating database file";
    base::WriteFile(database_path, "\0", 1);
  }
  std::unique_ptr<DlpDatabase> db =
      std::make_unique<DlpDatabase>(database_file);
  if (db->Init() != SQLITE_OK) {
    LOG(ERROR) << "Cannot connect to database " << database_path;
    std::move(init_callback).Run();
    return;
  }
  base::ThreadTaskRunnerHandle::Get()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          &EnumerateInodes,
          GetUserDownloadsPath(GetSanitizedUsername(dbus_object_.get()))),
      base::BindOnce(&DlpAdaptor::CleanupAndSetDatabase, base::Unretained(this),
                     std::move(db), std::move(init_callback)));
}

void DlpAdaptor::EnsureFanotifyWatcherStarted() {
  if (fanotify_watcher_)
    return;

  if (is_fanotify_watcher_started_for_testing_)
    return;

  LOG(INFO) << "Starting fanotify watcher";

  fanotify_watcher_ = std::make_unique<FanotifyWatcher>(this);
  const std::string sanitized_username =
      GetSanitizedUsername(dbus_object_.get());
  fanotify_watcher_->AddWatch(GetUserDownloadsPath(sanitized_username));
}

void DlpAdaptor::ProcessFileOpenRequest(
    ino_t inode, int pid, base::OnceCallback<void(bool)> callback) {
  if (!db_) {
    LOG(WARNING) << "DLP database is not ready yet. Allowing the file request";
    std::move(callback).Run(/*allowed=*/true);
    return;
  }

  std::optional<FileEntry> file_entry = db_->GetFileEntryByInode(inode);
  if (!file_entry) {
    std::move(callback).Run(/*allowed=*/true);
    return;
  }

  int lifeline_fd = -1;
  for (const auto& [key, value] : approved_requests_) {
    if (base::Contains(value.first, inode) && value.second == pid) {
      lifeline_fd = key;
      break;
    }
  }
  if (lifeline_fd != -1) {
    std::move(callback).Run(/*allowed=*/true);
    return;
  }

  // If the file can be restricted by any DLP rule, do not allow access there.
  IsDlpPolicyMatchedRequest request;
  request.set_source_url(file_entry->source_url);
  request.mutable_file_metadata()->set_inode(inode);
  request.mutable_file_metadata()->set_source_url(file_entry->source_url);
  // TODO(crbug.com/1357967)
  // request.mutable_file_metadata()->set_path();

  std::pair<base::OnceCallback<void(bool)>, base::OnceCallback<void(bool)>>
      callbacks = base::SplitOnceCallback(std::move(callback));
  dlp_files_policy_service_->IsDlpPolicyMatchedAsync(
      SerializeProto(request),
      base::BindOnce(&DlpAdaptor::OnDlpPolicyMatched, base::Unretained(this),
                     std::move(callbacks.first)),
      base::BindOnce(&DlpAdaptor::OnDlpPolicyMatchedError,
                     base::Unretained(this), std::move(callbacks.second)));
}

void DlpAdaptor::OnDlpPolicyMatched(base::OnceCallback<void(bool)> callback,
                                    const std::vector<uint8_t>& response_blob) {
  IsDlpPolicyMatchedResponse response;
  std::string parse_error = ParseProto(FROM_HERE, &response, response_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse IsDlpPolicyMatched response: "
               << parse_error;
    std::move(callback).Run(/*allowed=*/false);
    return;
  }
  std::move(callback).Run(!response.restricted());
}

void DlpAdaptor::OnDlpPolicyMatchedError(
    base::OnceCallback<void(bool)> callback, brillo::Error* error) {
  LOG(ERROR) << "Failed to check whether file could be restricted";
  std::move(callback).Run(/*allowed=*/false);
}

void DlpAdaptor::OnRequestFileAccess(
    std::vector<uint64_t> inodes,
    int pid,
    base::ScopedFD local_fd,
    RequestFileAccessCallback callback,
    const std::vector<uint8_t>& response_blob) {
  IsFilesTransferRestrictedResponse response;
  std::string parse_error = ParseProto(FROM_HERE, &response, response_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse IsFilesTransferRestrictedResponse response: "
               << parse_error;
    std::move(callback).Run(
        /*allowed=*/false, parse_error);
    return;
  }

  if (response.restricted_files().empty()) {
    int lifeline_fd = AddLifelineFd(local_fd.get());
    approved_requests_.insert_or_assign(lifeline_fd,
                                        std::make_pair(std::move(inodes), pid));
  }

  std::move(callback).Run(response.restricted_files().empty(),
                          /*error_message=*/std::string());
}

void DlpAdaptor::OnRequestFileAccessError(RequestFileAccessCallback callback,
                                          brillo::Error* error) {
  LOG(ERROR) << "Failed to check whether file could be restricted";
  std::move(callback).Run(/*allowed=*/false, error->GetMessage());
}

void DlpAdaptor::ReplyOnRequestFileAccess(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        std::vector<uint8_t>,
        brillo::dbus_utils::FileDescriptor>> response,
    base::ScopedFD remote_fd,
    bool allowed,
    const std::string& error) {
  RequestFileAccessResponse response_proto;
  response_proto.set_allowed(allowed);
  if (!error.empty())
    response_proto.set_error_message(error);
  response->Return(SerializeProto(response_proto), std::move(remote_fd));
}

void DlpAdaptor::OnIsFilesTransferRestricted(
    base::flat_set<std::string> transferred_files,
    CheckFilesTransferCallback callback,
    const std::vector<uint8_t>& response_blob) {
  IsFilesTransferRestrictedResponse response;
  std::string parse_error = ParseProto(FROM_HERE, &response, response_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse IsFilesTransferRestricted response: "
               << parse_error;
    std::move(callback).Run(std::vector<std::string>(), parse_error);
    return;
  }

  std::vector<std::string> restricted_files;
  for (const auto& file : *response.mutable_restricted_files()) {
    DCHECK(base::Contains(transferred_files, file.path()));
    restricted_files.push_back(file.path());
  }

  std::move(callback).Run(std::move(restricted_files),
                          /*error_message=*/std::string());
}

void DlpAdaptor::OnIsFilesTransferRestrictedError(
    CheckFilesTransferCallback callback, brillo::Error* error) {
  LOG(ERROR) << "Failed to check which file should be restricted";
  std::move(callback).Run(std::vector<std::string>(), error->GetMessage());
}

void DlpAdaptor::ReplyOnCheckFilesTransfer(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    std::vector<std::string> restricted_files_paths,
    const std::string& error) {
  CheckFilesTransferResponse response_proto;
  *response_proto.mutable_files_paths() = {restricted_files_paths.begin(),
                                           restricted_files_paths.end()};
  if (!error.empty())
    response_proto.set_error_message(error);
  response->Return(SerializeProto(response_proto));
}

void DlpAdaptor::SetFanotifyWatcherStartedForTesting(bool is_started) {
  is_fanotify_watcher_started_for_testing_ = is_started;
}

void DlpAdaptor::SetDownloadsPathForTesting(const base::FilePath& path) {
  g_downloads_path_for_testing = path;
}

void DlpAdaptor::CloseDatabaseForTesting() {
  db_.reset();
}

int DlpAdaptor::AddLifelineFd(int dbus_fd) {
  int fd = dup(dbus_fd);
  if (fd < 0) {
    PLOG(ERROR) << "dup failed";
    return -1;
  }

  lifeline_fd_controllers_[fd] = base::FileDescriptorWatcher::WatchReadable(
      fd, base::BindRepeating(&DlpAdaptor::OnLifelineFdClosed,
                              base::Unretained(this), fd));

  return fd;
}

bool DlpAdaptor::DeleteLifelineFd(int fd) {
  auto iter = lifeline_fd_controllers_.find(fd);
  if (iter == lifeline_fd_controllers_.end()) {
    return false;
  }

  iter->second.reset();  // Destruct the controller, which removes the callback.
  lifeline_fd_controllers_.erase(iter);

  // AddLifelineFd() calls dup(), so this function should close the fd.
  // We still return true since at this point the FileDescriptorWatcher object
  // has been destructed.
  if (IGNORE_EINTR(close(fd)) < 0) {
    PLOG(ERROR) << "close failed";
  }

  return true;
}

void DlpAdaptor::OnLifelineFdClosed(int client_fd) {
  // The process that requested this access has died/exited.
  DeleteLifelineFd(client_fd);

  // Remove the approvals tied to the lifeline fd.
  approved_requests_.erase(client_fd);
}

// static
ino_t DlpAdaptor::GetInodeValue(const std::string& path) {
  struct stat file_stats;
  if (stat(path.c_str(), &file_stats) != 0) {
    PLOG(ERROR) << "Could not access " << path;
    return 0;
  }
  return file_stats.st_ino;
}

void DlpAdaptor::CleanupAndSetDatabase(std::unique_ptr<DlpDatabase> db,
                                       base::OnceClosure callback,
                                       std::set<ino64_t> inodes) {
  DCHECK(db);

  if (db->DeleteFileEntriesWithInodesNotInSet(inodes)) {
    db_.swap(db);
    std::move(callback).Run();
  }
}

}  // namespace dlp
