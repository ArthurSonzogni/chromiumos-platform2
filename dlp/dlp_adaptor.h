// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLP_DLP_ADAPTOR_H_
#define DLP_DLP_ADAPTOR_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
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
  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
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

 private:
  friend class DlpAdaptorTest;
  FRIEND_TEST(DlpAdaptorTest, AllowedWithoutDatabase);
  FRIEND_TEST(DlpAdaptorTest, AllowedWithDatabase);
  FRIEND_TEST(DlpAdaptorTest, NotRestrictedFileAddedAndAllowed);
  FRIEND_TEST(DlpAdaptorTest, RestrictedFileAddedAndNotAllowed);
  FRIEND_TEST(DlpAdaptorTest, RestrictedFileAddedAndRequestedAllowed);
  FRIEND_TEST(DlpAdaptorTest, RestrictedFileAddedAndRequestedNotAllowed);
  FRIEND_TEST(DlpAdaptorTest,
              RestrictedFileAddedRequestedAndCancelledNotAllowed);
  FRIEND_TEST(DlpAdaptorTest, RequestAllowedWithoutDatabase);

  // Opens the database |db_| to store files sources.
  void InitDatabase(const base::FilePath database_path);

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
  // Callbacks on IsRestricted D-Bus request.
  void OnIsRestrictedReply(uint64_t inode,
                           int pid,
                           base::ScopedFD local_fd,
                           RequestFileAccessCallback callback,
                           const std::vector<uint8_t>& response_blob);
  void OnIsRestrictedError(RequestFileAccessCallback callback,
                           brillo::Error* error);
  void ReplyOnRequestFileAccess(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          std::vector<uint8_t>,
          brillo::dbus_utils::FileDescriptor>> response,
      base::ScopedFD remote_fd,
      bool allowed,
      const std::string& error);

  // Functions and callbacks to handle lifeline fd.
  int AddLifelineFd(int dbus_fd);
  bool DeleteLifelineFd(int fd);
  void OnLifelineFdClosed(int fd);

  static ino_t GetInodeValue(const std::string& path);

  // Can be nullptr if failed to initialize.
  std::unique_ptr<leveldb::DB> db_;

  std::vector<DlpFilesRule> policy_rules_;

  std::unique_ptr<FanotifyWatcher> fanotify_watcher_;

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  std::unique_ptr<org::chromium::DlpFilesPolicyServiceProxy>
      dlp_files_policy_service_;

  // Map holding the currently approved access requests.
  // Maps from the lifeline fd to a pair of inode and pid.
  std::map<int, std::pair<uint64_t, int>> approved_requests_;

  // Map holding watchers for lifeline fd corresponding currently approved
  // requests. Maps from the lifeline fd to the watcher.
  std::map<int, std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      lifeline_fd_controllers_;
};

}  // namespace dlp

#endif  // DLP_DLP_ADAPTOR_H_
