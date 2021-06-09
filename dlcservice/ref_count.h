// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_REF_COUNT_H_
#define DLCSERVICE_REF_COUNT_H_

#include <memory>
#include <set>
#include <string>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <libimageloader/manifest.h>

#include "dlcservice/ref_count.pb.h"

namespace dlcservice {

// The file name for the ref count proto file.
extern const char kRefCountFileName[];

// TODO(ahassani): Move this to login_manager's dbus-constant.h.
extern const char kSessionStarted[];

// Is passed when the ref count should count against the device users.
extern const char kUsedByUser[];

// Is passed when the DLC belongs to system and ref count should count toward
// it.
extern const char kUsedBySystem[];

// The default expiration delay in days.
extern const int kDefaultExpirationDelayDays;

// The default user name used in system based ref counts.
extern const char kSystemUsername[];

// The interface for different types of ref counts. Ref counts are used to keep
// track of the users of a DLC. If multiple users using the same DLC on a
// device, one user should not be able to easily remove the DLC because
// otherwise other users' experience will suffer. Ref counts also can be based
// on things other than users depending on the need. They can also include an
// expiration delay so the DLC is removed once the expiration is timed out.
class RefCountInterface {
 public:
  virtual ~RefCountInterface() = default;

  static std::unique_ptr<RefCountInterface> Create(
      const base::FilePath& prefs_path,
      std::shared_ptr<imageloader::Manifest> manifest);

  // Should be called when a DLC is successfully installed.
  virtual bool InstalledDlc() = 0;

  // Should be called when a DLC is successfully uninstalled.
  virtual bool UninstalledDlc() = 0;

  // Returns true if the DLC should be removed based on the ref count and
  // expiration delays.
  virtual bool ShouldPurgeDlc() const = 0;

 protected:
  RefCountInterface() = default;

 private:
  RefCountInterface(const RefCountInterface&) = delete;
  RefCountInterface& operator=(const RefCountInterface&) = delete;
};

// The base class for ref counts based on ref count proto file.
class RefCountBase : public RefCountInterface {
 public:
  ~RefCountBase() = default;

  bool InstalledDlc() override;
  bool UninstalledDlc() override;
  bool ShouldPurgeDlc() const override;

 protected:
  explicit RefCountBase(const base::FilePath& prefs_path,
                        std::shared_ptr<imageloader::Manifest> manifest);

  // Returns the current user name that should be used in the ref count.
  virtual std::string GetCurrentUserName() const = 0;

  std::set<std::string> users_;
  int64_t last_access_time_us_;

  std::shared_ptr<imageloader::Manifest> manifest_;

 private:
  friend class RefCountTest;
  FRIEND_TEST(RefCountTest, Ctor);

  // Persists the ref count proto file to disk.
  bool Persist();

  // Utility for reading the ref count info.
  static bool ReadRefCountInfo(const base::FilePath& path, RefCountInfo* info);

  base::FilePath ref_count_path_;

  RefCountBase(const RefCountBase&) = delete;
  RefCountBase& operator=(const RefCountBase&) = delete;
};

class UserRefCount : public RefCountBase {
 public:
  UserRefCount(const base::FilePath& prefs_path,
               std::shared_ptr<imageloader::Manifest> manifest);
  ~UserRefCount() = default;

  // Refreshes the internal cache of the user names we keep.
  static void SessionChanged(const std::string& state);

  static void OnSuccessRetrievePrimarySessionAsyncForTest(
      const std::string& username, const std::string& sanitized_username);

 protected:
  std::string GetCurrentUserName() const override {
    return primary_session_username_ ? *primary_session_username_ : "";
  }

 private:
  static void OnSuccessRetrievePrimarySessionAsync(
      const std::string& username, const std::string& sanitized_username);
  static void OnErrorRetrievePrimarySessionAsync(brillo::Error* err);

  static std::set<std::string> device_users_;
  static std::unique_ptr<std::string> primary_session_username_;

  UserRefCount(const UserRefCount&) = delete;
  UserRefCount& operator=(const UserRefCount&) = delete;
};

class SystemRefCount : public RefCountBase {
 public:
  explicit SystemRefCount(const base::FilePath& prefs_path,
                          std::shared_ptr<imageloader::Manifest> manifest)
      : RefCountBase(prefs_path, manifest) {}
  ~SystemRefCount() = default;

 protected:
  std::string GetCurrentUserName() const override { return kSystemUsername; }

 private:
  SystemRefCount(const SystemRefCount&) = delete;
  SystemRefCount& operator=(const SystemRefCount&) = delete;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_REF_COUNT_H_
