// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLP_DLP_ADAPTOR_H_
#define DLP_DLP_ADAPTOR_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/containers/flat_map.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <leveldb/db.h>

#include "dlp/dbus-proxies.h"
#include "dlp/fanotify_watcher.h"
#include "dlp/org.chromium.Dlp.h"
#include "dlp/proto_bindings/dlp_service.pb.h"

namespace brillo {
namespace dbus_utils {
class DBusObject;
class FileDescriptor;
}  // namespace dbus_utils
}  // namespace brillo

namespace dlp {

class DlpAdaptor : public org::chromium::DlpAdaptor,
                   public org::chromium::DlpInterface,
                   public FanotifyWatcher::Delegate {
 public:
  explicit DlpAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object);
  DlpAdaptor(const DlpAdaptor&) = delete;
  DlpAdaptor& operator=(const DlpAdaptor&) = delete;
  virtual ~DlpAdaptor();

  // Initializes the database to be stored in the user's cryptohome.
  void InitDatabaseOnCryptohome();

  // Registers the D-Bus object and interfaces.
  void RegisterAsync(brillo::dbus_utils::AsyncEventSequencer::CompletionAction
                         completion_callback);

  // org::chromium::DlpInterface: (see org.chromium.Dlp.xml).
  std::vector<uint8_t> SetDlpFilesPolicy(
      const std::vector<uint8_t>& request_blob) override;
  std::vector<uint8_t> AddFile(
      const std::vector<uint8_t>& request_blob) override;
  void RequestFileAccess(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                             std::vector<uint8_t>,
                             brillo::dbus_utils::FileDescriptor>> response,
                         const std::vector<uint8_t>& request_blob) override;
  std::vector<uint8_t> GetFilesSources(
      const std::vector<uint8_t>& request_blob) override;
  void CheckFilesTransfer(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          std::vector<uint8_t>>> response,
      const std::vector<uint8_t>& request_blob) override;

  void SetFanotifyWatcherStartedForTesting(bool is_started);
  void SetDownloadsPathForTesting(const base::FilePath& path);
  void CloseDatabaseForTesting();

 private:
  friend class DlpAdaptorTest;
  FRIEND_TEST(DlpAdaptorTest, AllowedWithoutDatabase);
  FRIEND_TEST(DlpAdaptorTest, AllowedWithDatabase);
  FRIEND_TEST(DlpAdaptorTest, NotRestrictedFileAddedAndAllowed);
  FRIEND_TEST(DlpAdaptorTest, RestrictedFileAddedAndNotAllowed);
  FRIEND_TEST(DlpAdaptorTest, RestrictedFileAddedAndRequestedAllowed);
  FRIEND_TEST(DlpAdaptorTest, RestrictedFilesNotAddedAndRequestedAllowed);
  FRIEND_TEST(DlpAdaptorTest, RestrictedFileNotAddedAndImmediatelyAllowed);
  FRIEND_TEST(DlpAdaptorTest, RestrictedFileAddedAndRequestedNotAllowed);
  FRIEND_TEST(DlpAdaptorTest,
              RestrictedFileAddedRequestedAndCancelledNotAllowed);
  FRIEND_TEST(DlpAdaptorTest, RequestAllowedWithoutDatabase);
  FRIEND_TEST(DlpAdaptorTest, GetFilesSources);
  FRIEND_TEST(DlpAdaptorTest, GetFilesSourcesWithoutDatabase);
  FRIEND_TEST(DlpAdaptorTest, GetFilesSourcesFileDeleted);
  // TODO(crbug.com/1338914): LevelDB doesn't work correctly on ARM yet.
  FRIEND_TEST(DlpAdaptorTest, DISABLED_GetFilesSourcesFileDeleted);
  FRIEND_TEST(DlpAdaptorTest, SetDlpFilesPolicy);
  FRIEND_TEST(DlpAdaptorTest, CheckFilesTransfer);

  // Opens the database |db_| to store files sources, |init_callback| called
  // after the database is set.
  void InitDatabase(const base::FilePath database_path,
                    base::OnceClosure init_callback);

  // Initializes |fanotify_watcher_| if not yet started.
  void EnsureFanotifyWatcherStarted();

  void ProcessFileOpenRequest(ino_t inode,
                              int pid,
                              base::OnceCallback<void(bool)> callback) override;

  // Callbacks on DlpPolicyMatched D-Bus request.
  void OnDlpPolicyMatched(base::OnceCallback<void(bool)> callback,
                          const std::vector<uint8_t>& response_blob);
  void OnDlpPolicyMatchedError(base::OnceCallback<void(bool)> callback,
                               brillo::Error* error);

  using RequestFileAccessCallback =
      base::OnceCallback<void(bool, const std::string&)>;
  // Callbacks on IsFilesTransferRestricted D-Bus request.
  void OnRequestFileAccess(std::vector<uint64_t> inodes,
                           int pid,
                           base::ScopedFD local_fd,
                           RequestFileAccessCallback callback,
                           const std::vector<uint8_t>& response_blob);
  void OnRequestFileAccessError(RequestFileAccessCallback callback,
                                brillo::Error* error);
  void ReplyOnRequestFileAccess(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          std::vector<uint8_t>,
          brillo::dbus_utils::FileDescriptor>> response,
      base::ScopedFD remote_fd,
      bool allowed,
      const std::string& error);

  using CheckFilesTransferCallback =
      base::OnceCallback<void(std::vector<std::string>, const std::string&)>;
  // Callback on IsFilesTransferRestricted D-Bus request.
  void OnIsFilesTransferRestricted(
      base::flat_map<std::string, std::vector<std::string>> transferred_files,
      CheckFilesTransferCallback callback,
      const std::vector<uint8_t>& response_blob);
  void OnIsFilesTransferRestrictedError(CheckFilesTransferCallback callback,
                                        brillo::Error* error);
  void ReplyOnCheckFilesTransfer(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          std::vector<uint8_t>>> response,
      std::vector<std::string> restricted_files_paths,
      const std::string& error);

  // Functions and callbacks to handle lifeline fd.
  int AddLifelineFd(int dbus_fd);
  bool DeleteLifelineFd(int fd);
  void OnLifelineFdClosed(int fd);

  static ino_t GetInodeValue(const std::string& path);

  // Removes entries from |db| that are not present in |inodes| and sets the
  // used database to |db|. |callback| is called if this successfully finishes.
  void CleanupAndSetDatabase(std::unique_ptr<leveldb::DB> db,
                             base::OnceClosure callback,
                             std::set<ino64_t> inodes);

  // If true, DlpAdaptor won't try to initialise `fanotify_watcher_`.
  bool is_fanotify_watcher_started_for_testing_ = false;

  // Can be nullptr if failed to initialize or closed during a test.
  std::unique_ptr<leveldb::DB> db_;

  std::vector<DlpFilesRule> policy_rules_;

  std::unique_ptr<FanotifyWatcher> fanotify_watcher_;

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  std::unique_ptr<org::chromium::DlpFilesPolicyServiceProxy>
      dlp_files_policy_service_;

  // Map holding the currently approved access requests.
  // Maps from the lifeline fd to a pair of list of files inodes and pid.
  std::map<int, std::pair<std::vector<uint64_t>, int>> approved_requests_;

  // Map holding watchers for lifeline fd corresponding currently approved
  // requests. Maps from the lifeline fd to the watcher.
  std::map<int, std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      lifeline_fd_controllers_;
};

}  // namespace dlp

#endif  // DLP_DLP_ADAPTOR_H_
