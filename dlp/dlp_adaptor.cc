// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_adaptor.h"

#include <cstdint>
#include <set>
#include <string>
#include <sys/types.h>
#include <type_traits>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/process/process_handle.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/errors/error.h>
#include <dbus/dlp/dbus-constants.h>
#include <featured/feature_library.h>
#include <google/protobuf/message_lite.h>
#include <session_manager/dbus-proxies.h>
#include <sqlite3.h>

#include "dlp/proto_bindings/dlp_service.pb.h"

namespace dlp {

// The maximum delay in between file creation and adding file to database.
constexpr base::TimeDelta kAddFileMaxDelay = base::Minutes(1);

namespace {

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

FileEntry ConvertToFileEntry(FileId id, AddFileRequest request) {
  FileEntry result;
  result.id = id;
  if (request.has_source_url())
    result.source_url = request.source_url();
  if (request.has_referrer_url())
    result.referrer_url = request.referrer_url();
  return result;
}

std::set<std::pair<base::FilePath, FileId>> EnumerateFiles(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    const base::FilePath& root_path) {
  CHECK(task_runner->RunsTasksInCurrentSequence());
  std::set<std::pair<base::FilePath, FileId>> files;
  base::FileEnumerator enumerator(root_path, /*recursive=*/true,
                                  base::FileEnumerator::FILES);
  for (base::FilePath entry = enumerator.Next(); !entry.empty();
       entry = enumerator.Next()) {
    FileId file_id = GetFileId(entry.value());
    if (file_id.first > 0) {
      files.insert(std::make_pair(entry, file_id));
    }
  }

  return files;
}

// Checks whether the list of the rules is exactly the same.
bool SameRules(const std::vector<DlpFilesRule> rules1,
               const std::vector<DlpFilesRule> rules2) {
  if (rules1.size() != rules2.size()) {
    return false;
  }
  for (int i = 0; i < rules1.size(); ++i) {
    if (SerializeProto(rules1[i]) != SerializeProto(rules2[i])) {
      return false;
    }
  }
  return true;
}

// Whether the cached level should be re-checked.
bool RequiresCheck(RestrictionLevel level) {
  return level == LEVEL_UNSPECIFIED || level == LEVEL_WARN_CANCEL;
}

// If the action is permanently blocked by the current policy.
bool IsAlwaysBlocked(RestrictionLevel level) {
  return level == LEVEL_BLOCK;
}

}  // namespace

const struct VariationsFeature kCrOSLateBootDlpDatabaseCleanupFeature = {
    .name = "CrOSLateBootDlpDatabaseCleanupFeature",
    .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

DlpAdaptor::DlpAdaptor(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object,
    feature::PlatformFeaturesInterface* feature_lib,
    int fanotify_perm_fd,
    int fanotify_notif_fd,
    const base::FilePath& home_path)
    : org::chromium::DlpAdaptor(this),
      dbus_object_(std::move(dbus_object)),
      feature_lib_(feature_lib),
      home_path_(home_path),
      file_enumeration_thread_("file_enumeration_thread") {
  dlp_metrics_ = std::make_unique<DlpMetrics>();
  fanotify_watcher_ = std::make_unique<FanotifyWatcher>(this, fanotify_perm_fd,
                                                        fanotify_notif_fd);
  dlp_files_policy_service_ =
      std::make_unique<org::chromium::DlpFilesPolicyServiceProxy>(
          dbus_object_->GetBus().get(), kDlpFilesPolicyServiceName);

  CHECK(file_enumeration_thread_.Start())
      << "Failed to start file enumeration thread.";
  file_enumeration_task_runner_ = file_enumeration_thread_.task_runner();

  CHECK(!file_enumeration_task_runner_->RunsTasksInCurrentSequence());
}

DlpAdaptor::~DlpAdaptor() {
  if (!pending_files_to_add_.empty()) {
    DCHECK(!db_);
    dlp_metrics_->SendAdaptorError(
        AdaptorError::kAddFileNotCompleteBeforeDestruction);
  }
  file_enumeration_thread_.Stop();
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
    dlp_metrics_->SendAdaptorError(AdaptorError::kInvalidProtoError);
    return SerializeProto(response);
  }

  auto new_rules =
      std::vector<DlpFilesRule>(request.rules().begin(), request.rules().end());

  // No update is needed.
  if (SameRules(policy_rules_, new_rules)) {
    return SerializeProto(response);
  }
  requests_cache_.ResetCache();
  policy_rules_.swap(new_rules);

  if (!policy_rules_.empty()) {
    EnsureFanotifyWatcherStarted();
  } else {
    fanotify_watcher_->SetActive(false);
  }

  return SerializeProto(response);
}

void DlpAdaptor::AddFiles(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    const std::vector<uint8_t>& request_blob) {
  AddFilesRequest request;

  const std::string parse_error = ParseProto(FROM_HERE, &request, request_blob);
  if (!parse_error.empty()) {
    ReplyOnAddFiles(std::move(response),
                    "Failed to parse AddFiles request: " + parse_error);
    dlp_metrics_->SendAdaptorError(AdaptorError::kInvalidProtoError);
    return;
  }

  if (request.add_file_requests().empty()) {
    ReplyOnAddFiles(std::move(response), std::string());
    return;
  }

  LOG(INFO) << "Adding " << request.add_file_requests().size()
            << " files to the database.";

  std::vector<FileEntry> files_to_add;
  std::vector<base::FilePath> files_paths;
  std::vector<FileId> files_ids;
  for (const AddFileRequest& add_file_request : request.add_file_requests()) {
    LOG(INFO) << "Adding file to the database: "
              << add_file_request.file_path();

    const FileId id = GetFileId(add_file_request.file_path());
    if (!id.first) {
      ReplyOnAddFiles(std::move(response), "Failed to get inode");
      dlp_metrics_->SendAdaptorError(AdaptorError::kInodeRetrievalError);
      return;
    }
    // If file is created too long time ago - do not allow addition to the
    // database. DLP is only for new files.
    const base::Time crtime = base::Time::FromTimeT(id.second);
    if (base::Time::Now() - crtime >= kAddFileMaxDelay) {
      ReplyOnAddFiles(std::move(response), "File is too old");
      dlp_metrics_->SendAdaptorError(AdaptorError::kAddedFileIsTooOld);
      return;
    }

    const base::FilePath file_path(add_file_request.file_path());
    if (!home_path_.IsParent(file_path)) {
      ReplyOnAddFiles(std::move(response), "File is not on user's home");
      dlp_metrics_->SendAdaptorError(AdaptorError::kAddedFileIsNotOnUserHome);
      return;
    }

    FileEntry file_entry = ConvertToFileEntry(id, add_file_request);
    files_to_add.push_back(file_entry);
    files_paths.push_back(file_path);
    files_ids.emplace_back(id);
  }

  if (!db_) {
    LOG(WARNING) << "Database is not ready, pending addition of the file";
    pending_files_to_add_.insert(pending_files_to_add_.end(),
                                 files_to_add.begin(), files_to_add.end());
    ReplyOnAddFiles(std::move(response), std::string());
    dlp_metrics_->SendAdaptorError(AdaptorError::kDatabaseNotReadyError);
    return;
  }

  db_->UpsertFileEntries(
      files_to_add,
      base::BindOnce(&DlpAdaptor::OnFilesUpserted, base::Unretained(this),
                     std::move(response), std::move(files_paths),
                     std::move(files_ids)));
}

void DlpAdaptor::RequestFileAccess(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           base::ScopedFD>>
        response,
    const std::vector<uint8_t>& request_blob) {
  base::ScopedFD local_fd, remote_fd;
  if (!base::CreatePipe(&local_fd, &remote_fd, /*non_blocking=*/true)) {
    PLOG(ERROR) << "Failed to create lifeline pipe";
    dlp_metrics_->SendAdaptorError(AdaptorError::kCreatePipeError);
    std::move(response)->ReplyWithError(
        FROM_HERE, brillo::errors::dbus::kDomain, dlp::kErrorFailedToCreatePipe,
        "Failed to create lifeline pipe");
    return;
  }
  RequestFileAccessRequest request;

  const std::string parse_error = ParseProto(FROM_HERE, &request, request_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse RequestFileAccess request: " << parse_error;
    dlp_metrics_->SendAdaptorError(AdaptorError::kInvalidProtoError);
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

  std::vector<FileId> ids;
  for (const auto& file_path : request.files_paths()) {
    const FileId id = GetFileId(file_path);
    if (id.first > 0) {
      ids.push_back(id);
    }
  }

  // If no valid ids provided, return immediately.
  if (ids.empty()) {
    ReplyOnRequestFileAccess(std::move(response), std::move(remote_fd),
                             /*allowed=*/true,
                             /*error_message=*/std::string());
    return;
  }

  db_->GetFileEntriesByIds(
      ids, /*ignore_crtime=*/false,
      base::BindOnce(&DlpAdaptor::ProcessRequestFileAccessWithData,
                     base::Unretained(this), std::move(response),
                     std::move(request), std::move(local_fd),
                     std::move(remote_fd)));
}

void DlpAdaptor::ProcessRequestFileAccessWithData(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           base::ScopedFD>>
        response,
    RequestFileAccessRequest request,
    base::ScopedFD local_fd,
    base::ScopedFD remote_fd,
    std::map<FileId, FileEntry> file_entries) {
  IsFilesTransferRestrictedRequest matching_request;
  std::vector<FileId> request_ids;
  std::vector<FileId> granted_ids;

  bool allow_all_files =
      request.has_destination_component() &&
      request.destination_component() == DlpComponent::SYSTEM;

  for (const auto& file_path : request.files_paths()) {
    const FileId id = GetFileId(file_path);
    auto it = file_entries.find(id);
    if (it == std::end(file_entries)) {
      // Skip file if it's not DLP-protected as access to it is always allowed.
      continue;
    }
    if (allow_all_files) {
      granted_ids.emplace_back(id);
      continue;
    }
    RestrictionLevel cached_level = requests_cache_.Get(
        id, file_path,
        request.has_destination_url() ? request.destination_url() : "",
        request.has_destination_component() ? request.destination_component()
                                            : DlpComponent::UNKNOWN_COMPONENT);
    // Reply immediately is a file is always blocked.
    if (IsAlwaysBlocked(cached_level)) {
      ReplyOnRequestFileAccess(std::move(response), std::move(remote_fd),
                               /*allowed=*/false,
                               /*error_message=*/std::string());
      return;
    }
    // Was previously allowed, no need to check again.
    if (!RequiresCheck(cached_level)) {
      granted_ids.emplace_back(id);
      continue;
    }

    request_ids.push_back(id);

    FileMetadata* file_metadata = matching_request.add_transferred_files();
    file_metadata->set_inode(id.first);
    file_metadata->set_crtime(id.second);
    file_metadata->set_source_url(it->second.source_url);
    file_metadata->set_referrer_url(it->second.referrer_url);
    file_metadata->set_path(file_path);
  }
  // If access to all requested files was allowed, return immediately.
  if (request_ids.empty()) {
    if (!granted_ids.empty()) {
      int lifeline_fd = AddLifelineFd(local_fd.get());
      approved_requests_.insert_or_assign(
          lifeline_fd,
          std::make_pair(std::move(granted_ids), request.process_id()));
    }

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

  auto cache_callback =
      base::BindOnce(&DlpRequestsCache::CacheResult,
                     base::Unretained(&requests_cache_), matching_request);

  dlp_files_policy_service_->IsFilesTransferRestrictedAsync(
      SerializeProto(matching_request),
      base::BindOnce(&DlpAdaptor::OnRequestFileAccess, base::Unretained(this),
                     std::move(request_ids), std::move(granted_ids),
                     request.process_id(), std::move(local_fd),
                     std::move(callbacks.first), std::move(cache_callback)),
      base::BindOnce(&DlpAdaptor::OnRequestFileAccessError,
                     base::Unretained(this), std::move(callbacks.second)),
      /*timeout_ms=*/base::Minutes(5).InMilliseconds());
}

void DlpAdaptor::GetFilesSources(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    const std::vector<uint8_t>& request_blob) {
  GetFilesSourcesRequest request;
  GetFilesSourcesResponse response_proto;
  const std::string parse_error = ParseProto(FROM_HERE, &request, request_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse GetFilesSources request: " << parse_error;
    dlp_metrics_->SendAdaptorError(AdaptorError::kInvalidProtoError);
    response_proto.set_error_message(parse_error);
    response->Return(SerializeProto(response_proto));
    return;
  }

  if (!db_) {
    dlp_metrics_->SendAdaptorError(AdaptorError::kDatabaseNotReadyError);
    response_proto.set_error_message("Database not ready");
    response->Return(SerializeProto(response_proto));
    return;
  }

  std::vector<FileId> ids;
  std::vector<std::pair<FileId, std::string>> requested_files;
  for (const auto& inode : request.files_inodes()) {
    FileId id = {inode, /*crtime=*/0};
    ids.push_back(id);
    requested_files.emplace_back(id, "");
  }

  for (const auto& path : request.files_paths()) {
    const FileId id = GetFileId(path);
    if (id.first > 0) {
      ids.push_back(id);
      requested_files.emplace_back(id, path);
    }
  }

  // Crtime is not provided via requests with only inode, so ignoring it for
  // now.
  db_->GetFileEntriesByIds(
      ids, /*ignore_crtime=*/true,
      base::BindOnce(&DlpAdaptor::ProcessGetFilesSourcesWithData,
                     base::Unretained(this), std::move(response),
                     requested_files));
}

void DlpAdaptor::CheckFilesTransfer(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    const std::vector<uint8_t>& request_blob) {
  CheckFilesTransferRequest request;
  CheckFilesTransferResponse response_proto;
  const std::string parse_error = ParseProto(FROM_HERE, &request, request_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse CheckFilesTransfer request: " << parse_error;
    dlp_metrics_->SendAdaptorError(AdaptorError::kInvalidProtoError);
    response_proto.set_error_message(parse_error);
    response->Return(SerializeProto(response_proto));
    return;
  }

  if (!db_) {
    dlp_metrics_->SendAdaptorError(AdaptorError::kDatabaseNotReadyError);
    response_proto.set_error_message("Database is not ready");
    response->Return(SerializeProto(response_proto));
    return;
  }

  std::vector<FileId> ids;
  for (const auto& file_path : request.files_paths()) {
    const FileId file_id = GetFileId(file_path);
    if (file_id.first > 0) {
      ids.push_back(file_id);
    }
  }

  db_->GetFileEntriesByIds(
      ids, /*ignore_crtime=*/false,
      base::BindOnce(&DlpAdaptor::ProcessCheckFilesTransferWithData,
                     base::Unretained(this), std::move(response),
                     std::move(request)));
}

void DlpAdaptor::SetFanotifyWatcherStartedForTesting(bool is_started) {
  is_fanotify_watcher_started_for_testing_ = is_started;
}

void DlpAdaptor::CloseDatabaseForTesting() {
  db_.reset();
}

void DlpAdaptor::SetMetricsLibraryForTesting(
    std::unique_ptr<MetricsLibraryInterface> metrics_lib) {
  dlp_metrics_->SetMetricsLibraryForTesting(std::move(metrics_lib));
}

void DlpAdaptor::InitDatabase(const base::FilePath& database_path,
                              base::OnceClosure init_callback) {
  LOG(INFO) << "Opening database in: " << database_path.value();
  const base::FilePath database_file = database_path.Append("database");
  if (!base::PathExists(database_file)) {
    LOG(INFO) << "Creating database file";
    base::WriteFile(database_path, "\0", 1);
  }
  std::unique_ptr<DlpDatabase> db =
      std::make_unique<DlpDatabase>(database_file, this);
  DlpDatabase* db_ptr = db.get();

  db_ptr->Init(base::BindOnce(&DlpAdaptor::OnDatabaseInitialized,
                              base::Unretained(this), std::move(init_callback),
                              std::move(db), database_path));
}

void DlpAdaptor::OnDatabaseInitialized(base::OnceClosure init_callback,
                                       std::unique_ptr<DlpDatabase> db,
                                       const base::FilePath& database_path,
                                       std::pair<int, bool> db_status) {
  if (db_status.first != SQLITE_OK) {
    LOG(ERROR) << "Cannot connect to database " << database_path;
    dlp_metrics_->SendAdaptorError(AdaptorError::kDatabaseConnectionError);
    std::move(init_callback).Run();
    return;
  }

  dlp_metrics_->SendBooleanHistogram(kDlpDatabaseMigrationNeededHistogram,
                                     db_status.second);

  // Migration is needed.
  if (db_status.second) {
    LOG(INFO) << "Migrating from legacy table";
    file_enumeration_task_runner_->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&EnumerateFiles, file_enumeration_task_runner_,
                       home_path_),
        base::BindOnce(&DlpAdaptor::MigrateDatabase, base::Unretained(this),
                       std::move(db), std::move(init_callback), database_path));
    return;
  }

  if (!pending_files_to_add_.empty()) {
    LOG(INFO) << "Upserting pending entries";
    DlpDatabase* db_ptr = db.get();
    std::vector<FileEntry> files_being_added;
    files_being_added.swap(pending_files_to_add_);
    db_ptr->UpsertFileEntries(
        files_being_added,
        base::BindOnce(&DlpAdaptor::OnPendingFilesUpserted,
                       base::Unretained(this), std::move(init_callback),
                       std::move(db), database_path, db_status));
    return;
  }

  // Check whether cleanup of deleted files should happen when database is
  // initialized.
  // This could be disabled because for now the daemon can't traverse some
  // of the home directory folders due to inconsistent access permissions.
  if (feature_lib_ &&
      feature_lib_->IsEnabledBlocking(kCrOSLateBootDlpDatabaseCleanupFeature)) {
    LOG(INFO) << "Starting database cleanup";
    file_enumeration_task_runner_->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&EnumerateFiles, file_enumeration_task_runner_,
                       home_path_),
        base::BindOnce(&DlpAdaptor::CleanupAndSetDatabase,
                       base::Unretained(this), std::move(db),
                       std::move(init_callback)));
  } else {
    OnDatabaseCleaned(std::move(db), std::move(init_callback),
                      /*success=*/true);
  }
}

void DlpAdaptor::OnPendingFilesUpserted(base::OnceClosure init_callback,
                                        std::unique_ptr<DlpDatabase> db,
                                        const base::FilePath& database_path,
                                        std::pair<int, bool> db_status,
                                        bool success) {
  if (!success) {
    LOG(ERROR) << "Error while adding pending files.";
    dlp_metrics_->SendAdaptorError(AdaptorError::kAddFileError);
  }
  OnDatabaseInitialized(std::move(init_callback), std::move(db), database_path,
                        db_status);
}

void DlpAdaptor::AddPerFileWatch(
    const std::set<std::pair<base::FilePath, FileId>>& files) {
  if (!fanotify_watcher_->IsActive())
    return;

  std::vector<FileId> ids;
  for (const auto& entry : files) {
    ids.push_back(entry.second);
  }

  db_->GetFileEntriesByIds(
      ids, /*ignore_crtime=*/false,
      base::BindOnce(&DlpAdaptor::ProcessAddPerFileWatchWithData,
                     base::Unretained(this), files));
}

void DlpAdaptor::ProcessAddPerFileWatchWithData(
    const std::set<std::pair<base::FilePath, FileId>>& files,
    std::map<FileId, FileEntry> file_entries) {
  for (const auto& entry : files) {
    if (file_entries.find(entry.second) != std::end(file_entries)) {
      fanotify_watcher_->AddFileDeleteWatch(entry.first);
    }
  }
}

void DlpAdaptor::EnsureFanotifyWatcherStarted() {
  if (fanotify_watcher_->IsActive())
    return;

  if (is_fanotify_watcher_started_for_testing_)
    return;

  LOG(INFO) << "Activating fanotify watcher";

  fanotify_watcher_->SetActive(true);

  // If the database is not initialized yet, we delay adding per file watch
  // till it'll be created.
  if (db_) {
    file_enumeration_task_runner_->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&EnumerateFiles, file_enumeration_task_runner_,
                       home_path_),
        base::BindOnce(&DlpAdaptor::AddPerFileWatch, base::Unretained(this)));
  } else {
    pending_per_file_watches_ = true;
  }
}

void DlpAdaptor::ProcessFileOpenRequest(
    FileId id, int pid, base::OnceCallback<void(bool)> callback) {
  if (pid == base::GetCurrentProcId()) {
    // Allowing itself all file accesses (to database files).
    std::move(callback).Run(/*allowed=*/true);
    return;
  }

  if (!db_) {
    LOG(WARNING) << "DLP database is not ready yet. Allowing the file request";
    dlp_metrics_->SendAdaptorError(AdaptorError::kDatabaseNotReadyError);
    std::move(callback).Run(/*allowed=*/true);
    return;
  }

  db_->GetFileEntriesByIds(
      {id}, /*ignore_crtime=*/false,
      base::BindOnce(&DlpAdaptor::ProcessFileOpenRequestWithData,
                     base::Unretained(this), pid, std::move(callback)));
}

void DlpAdaptor::ProcessFileOpenRequestWithData(
    int pid,
    base::OnceCallback<void(bool)> callback,
    std::map<FileId, FileEntry> file_entries) {
  if (file_entries.size() != 1) {
    std::move(callback).Run(/*allowed=*/true);
    return;
  }
  const FileEntry& file_entry = file_entries.cbegin()->second;

  int lifeline_fd = -1;
  for (const auto& [key, value] : approved_requests_) {
    if (base::Contains(value.first, file_entry.id) && value.second == pid) {
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
  request.mutable_file_metadata()->set_inode(file_entry.id.first);
  request.mutable_file_metadata()->set_crtime(file_entry.id.second);
  request.mutable_file_metadata()->set_source_url(file_entry.source_url);
  request.mutable_file_metadata()->set_referrer_url(file_entry.referrer_url);
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

void DlpAdaptor::OnFileDeleted(ino64_t inode) {
  if (!db_) {
    LOG(WARNING) << "DLP database is not ready yet.";
    dlp_metrics_->SendAdaptorError(AdaptorError::kDatabaseNotReadyError);
    return;
  }

  db_->DeleteFileEntryByInode(inode,
                              /*callback=*/base::DoNothing());
}

void DlpAdaptor::OnFanotifyError(FanotifyError error) {
  dlp_metrics_->SendFanotifyError(error);
}

void DlpAdaptor::OnDatabaseError(DatabaseError error) {
  dlp_metrics_->SendDatabaseError(error);
}

void DlpAdaptor::OnDlpPolicyMatched(base::OnceCallback<void(bool)> callback,
                                    const std::vector<uint8_t>& response_blob) {
  IsDlpPolicyMatchedResponse response;
  std::string parse_error = ParseProto(FROM_HERE, &response, response_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse IsDlpPolicyMatched response: "
               << parse_error;
    dlp_metrics_->SendAdaptorError(AdaptorError::kInvalidProtoError);
    std::move(callback).Run(/*allowed=*/false);
    return;
  }
  std::move(callback).Run(!response.restricted());
}

void DlpAdaptor::OnDlpPolicyMatchedError(
    base::OnceCallback<void(bool)> callback, brillo::Error* error) {
  LOG(ERROR) << "Failed to check whether file could be restricted";
  dlp_metrics_->SendAdaptorError(AdaptorError::kRestrictionDetectionError);
  std::move(callback).Run(/*allowed=*/false);
}

void DlpAdaptor::OnRequestFileAccess(
    std::vector<FileId> request_ids,
    std::vector<FileId> granted_ids,
    int pid,
    base::ScopedFD local_fd,
    RequestFileAccessCallback callback,
    base::OnceCallback<void(IsFilesTransferRestrictedResponse)> cache_callback,
    const std::vector<uint8_t>& response_blob) {
  IsFilesTransferRestrictedResponse response;
  std::string parse_error = ParseProto(FROM_HERE, &response, response_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse IsFilesTransferRestrictedResponse response: "
               << parse_error;
    dlp_metrics_->SendAdaptorError(AdaptorError::kInvalidProtoError);
    std::move(callback).Run(
        /*allowed=*/false, parse_error);
    return;
  }

  // Cache the response.
  std::move(cache_callback).Run(response);

  bool allowed = true;
  for (const auto& file : response.files_restrictions()) {
    if (file.restriction_level() == ::dlp::RestrictionLevel::LEVEL_BLOCK ||
        file.restriction_level() ==
            ::dlp::RestrictionLevel::LEVEL_WARN_CANCEL) {
      allowed = false;
      break;
    }
  }

  if (allowed) {
    request_ids.insert(request_ids.end(), granted_ids.begin(),
                       granted_ids.end());
    int lifeline_fd = AddLifelineFd(local_fd.get());
    approved_requests_.insert_or_assign(
        lifeline_fd, std::make_pair(std::move(request_ids), pid));
  }

  std::move(callback).Run(allowed, /*error_message=*/std::string());
}

void DlpAdaptor::OnRequestFileAccessError(RequestFileAccessCallback callback,
                                          brillo::Error* error) {
  LOG(ERROR) << "Failed to check whether file could be restricted";
  dlp_metrics_->SendAdaptorError(AdaptorError::kRestrictionDetectionError);
  std::move(callback).Run(/*allowed=*/false, error->GetMessage());
}

void DlpAdaptor::ReplyOnRequestFileAccess(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           base::ScopedFD>>
        response,
    base::ScopedFD remote_fd,
    bool allowed,
    const std::string& error_message) {
  RequestFileAccessResponse response_proto;
  response_proto.set_allowed(allowed);
  if (!error_message.empty())
    response_proto.set_error_message(error_message);
  response->Return(SerializeProto(response_proto), std::move(remote_fd));
}

void DlpAdaptor::OnFilesUpserted(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    std::vector<base::FilePath> files_paths,
    std::vector<FileId> files_ids,
    bool success) {
  CHECK(files_paths.size() == files_ids.size());
  if (success) {
    for (size_t i = 0; i < files_paths.size(); ++i) {
      AddPerFileWatch({std::make_pair(files_paths[i], files_ids[i])});
    }
    ReplyOnAddFiles(std::move(response), std::string());
  } else {
    ReplyOnAddFiles(std::move(response), "Failed to add entries to database");
  }
}

void DlpAdaptor::ReplyOnAddFiles(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    std::string error_message) {
  AddFilesResponse response_proto;
  if (!error_message.empty()) {
    LOG(ERROR) << "Error while adding files: " << error_message;
    dlp_metrics_->SendAdaptorError(AdaptorError::kAddFileError);
    response_proto.set_error_message(error_message);
  }
  response->Return(SerializeProto(response_proto));
}

void DlpAdaptor::ProcessCheckFilesTransferWithData(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    CheckFilesTransferRequest request,
    std::map<FileId, FileEntry> file_entries) {
  CheckFilesTransferResponse response_proto;

  IsFilesTransferRestrictedRequest matching_request;
  base::flat_set<std::string> files_to_check;
  std::vector<std::string> already_restricted;
  for (const auto& file_path : request.files_paths()) {
    const FileId file_id = GetFileId(file_path);
    auto it = file_entries.find(file_id);
    if (it == std::end(file_entries)) {
      // Skip file if it's not DLP-protected as access to it is always allowed.
      continue;
    }

    RestrictionLevel cached_level = requests_cache_.Get(
        file_id, file_path,
        request.has_destination_url() ? request.destination_url() : "",
        request.has_destination_component() ? request.destination_component()
                                            : DlpComponent::UNKNOWN_COMPONENT);
    // The file is always blocked.
    if (IsAlwaysBlocked(cached_level)) {
      already_restricted.push_back(file_path);
    }
    // Was previously allowed/blocked, no need to check again.
    if (!RequiresCheck(cached_level)) {
      continue;
    }

    files_to_check.insert(file_path);

    FileMetadata* file_metadata = matching_request.add_transferred_files();
    file_metadata->set_inode(file_id.first);
    file_metadata->set_crtime(file_id.second);
    file_metadata->set_source_url(it->second.source_url);
    file_metadata->set_referrer_url(it->second.referrer_url);
    file_metadata->set_path(file_path);
  }

  if (files_to_check.empty()) {
    ReplyOnCheckFilesTransfer(std::move(response),
                              std::move(already_restricted),
                              /*error_message=*/std::string());
    return;
  }

  if (request.has_destination_url())
    matching_request.set_destination_url(request.destination_url());
  if (request.has_destination_component())
    matching_request.set_destination_component(request.destination_component());
  if (request.has_file_action())
    matching_request.set_file_action(request.file_action());
  if (request.has_io_task_id())
    matching_request.set_io_task_id(request.io_task_id());

  auto callbacks = base::SplitOnceCallback(
      base::BindOnce(&DlpAdaptor::ReplyOnCheckFilesTransfer,
                     base::Unretained(this), std::move(response)));

  auto cache_callback =
      base::BindOnce(&DlpRequestsCache::CacheResult,
                     base::Unretained(&requests_cache_), matching_request);

  dlp_files_policy_service_->IsFilesTransferRestrictedAsync(
      SerializeProto(matching_request),
      base::BindOnce(&DlpAdaptor::OnIsFilesTransferRestricted,
                     base::Unretained(this), std::move(files_to_check),
                     std::move(already_restricted), std::move(callbacks.first),
                     std::move(cache_callback)),
      base::BindOnce(&DlpAdaptor::OnIsFilesTransferRestrictedError,
                     base::Unretained(this), std::move(callbacks.second)),
      /*timeout_ms=*/base::Minutes(5).InMilliseconds());
}

void DlpAdaptor::OnIsFilesTransferRestricted(
    base::flat_set<std::string> checked_files,
    std::vector<std::string> restricted_files,
    CheckFilesTransferCallback callback,
    base::OnceCallback<void(IsFilesTransferRestrictedResponse)> cache_callback,
    const std::vector<uint8_t>& response_blob) {
  IsFilesTransferRestrictedResponse response;
  std::string parse_error = ParseProto(FROM_HERE, &response, response_blob);
  if (!parse_error.empty()) {
    LOG(ERROR) << "Failed to parse IsFilesTransferRestricted response: "
               << parse_error;
    dlp_metrics_->SendAdaptorError(AdaptorError::kInvalidProtoError);
    std::move(callback).Run(std::vector<std::string>(), parse_error);
    return;
  }

  // Cache the response.
  std::move(cache_callback).Run(response);
  for (const auto& file : response.files_restrictions()) {
    DCHECK(base::Contains(checked_files, file.file_metadata().path()));
    if (file.restriction_level() == ::dlp::RestrictionLevel::LEVEL_BLOCK ||
        file.restriction_level() ==
            ::dlp::RestrictionLevel::LEVEL_WARN_CANCEL) {
      restricted_files.push_back(file.file_metadata().path());
    }
  }

  std::move(callback).Run(std::move(restricted_files),
                          /*error_message=*/std::string());
}

void DlpAdaptor::OnIsFilesTransferRestrictedError(
    CheckFilesTransferCallback callback, brillo::Error* error) {
  LOG(ERROR) << "Failed to check which file should be restricted";
  dlp_metrics_->SendAdaptorError(AdaptorError::kRestrictionDetectionError);
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

void DlpAdaptor::ProcessGetFilesSourcesWithData(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    const std::vector<std::pair<FileId, std::string>>& requested_files,
    std::map<FileId, FileEntry> file_entries) {
  GetFilesSourcesResponse response_proto;
  for (const auto& [id, path] : requested_files) {
    // GetFilesSources request might've contained only inode info, so we need to
    // compare elements only by inode part of |id|.
    for (const auto& [file_id, file_entry] : file_entries) {
      if (file_id.first == id.first) {
        FileMetadata* file_metadata = response_proto.add_files_metadata();
        file_metadata->set_inode(file_id.first);
        file_metadata->set_crtime(file_id.second);
        if (!path.empty()) {
          file_metadata->set_path(path);
        }
        file_metadata->set_source_url(file_entry.source_url);
        file_metadata->set_referrer_url(file_entry.referrer_url);
        break;
      }
    }
  }

  response->Return(SerializeProto(response_proto));
}

int DlpAdaptor::AddLifelineFd(int dbus_fd) {
  int fd = dup(dbus_fd);
  if (fd < 0) {
    PLOG(ERROR) << "dup failed";
    dlp_metrics_->SendAdaptorError(AdaptorError::kFileDescriptorDupError);
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
    dlp_metrics_->SendAdaptorError(AdaptorError::kFileDescriptorCloseError);
  }

  return true;
}

void DlpAdaptor::OnLifelineFdClosed(int client_fd) {
  // The process that requested this access has died/exited.
  DeleteLifelineFd(client_fd);

  // Remove the approvals tied to the lifeline fd.
  approved_requests_.erase(client_fd);
}

void DlpAdaptor::CleanupAndSetDatabase(
    std::unique_ptr<DlpDatabase> db,
    base::OnceClosure callback,
    const std::set<std::pair<base::FilePath, FileId>>& files) {
  DCHECK(db);
  DlpDatabase* db_ptr = db.get();

  std::set<FileId> ids;
  for (const auto& entry : files) {
    ids.insert(entry.second);
  }

  db_ptr->DeleteFileEntriesWithIdsNotInSet(
      ids,
      base::BindOnce(&DlpAdaptor::OnDatabaseCleaned, base::Unretained(this),
                     std::move(db), std::move(callback)));
}

void DlpAdaptor::OnDatabaseCleaned(std::unique_ptr<DlpDatabase> db,
                                   base::OnceClosure callback,
                                   bool success) {
  if (success) {
    db_.swap(db);
    LOG(INFO) << "Database is initialized";
    // If fanotify watcher is already started, we need to add watches for all
    // files from the database.
    if (pending_per_file_watches_) {
      file_enumeration_task_runner_->PostTaskAndReplyWithResult(
          FROM_HERE,
          base::BindOnce(&EnumerateFiles, file_enumeration_task_runner_,
                         home_path_),
          base::BindOnce(&DlpAdaptor::AddPerFileWatch, base::Unretained(this)));
      pending_per_file_watches_ = false;
    }
    std::move(callback).Run();
  }
}

void DlpAdaptor::MigrateDatabase(
    std::unique_ptr<DlpDatabase> db,
    base::OnceClosure callback,
    const base::FilePath& database_path,
    const std::set<std::pair<base::FilePath, FileId>>& files) {
  DCHECK(db);
  DlpDatabase* db_ptr = db.get();

  std::vector<FileId> ids;
  for (const auto& entry : files) {
    ids.push_back(entry.second);
  }

  db_ptr->MigrateDatabase(
      ids,
      base::BindOnce(&DlpAdaptor::OnDatabaseMigrated, base::Unretained(this),
                     std::move(db), std::move(callback), database_path));
}

void DlpAdaptor::OnDatabaseMigrated(std::unique_ptr<DlpDatabase> db,
                                    base::OnceClosure init_callback,
                                    const base::FilePath& database_path,
                                    bool success) {
  if (!success) {
    LOG(ERROR) << "Error while migrating database.";
    dlp_metrics_->SendAdaptorError(AdaptorError::kDatabaseMigrationError);
  } else {
    LOG(INFO) << "Database was succesfully migrated.";
  }
  OnDatabaseInitialized(std::move(init_callback), std::move(db), database_path,
                        {SQLITE_OK, false});
}

}  // namespace dlp
