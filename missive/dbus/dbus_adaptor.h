// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_DBUS_DBUS_ADAPTOR_H_
#define MISSIVE_DBUS_DBUS_ADAPTOR_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/threading/thread.h>
#include <brillo/daemons/dbus_daemon.h>

#include "missive/missive/missive_service.h"
#include "missive/proto/interface.pb.h"
#include "missive/util/status.h"

// Must be located after all proto declarations
#include "dbus_adaptors/org.chromium.Missived.h"

namespace reporting {

class DBusAdaptor : public org::chromium::MissivedAdaptor,
                    public org::chromium::MissivedInterface {
 public:
  // Bus and missive are mandatory parameters, failure_cb by default
  // crashes with error message - may be replaced for testing.
  DBusAdaptor(
      scoped_refptr<dbus::Bus> bus,
      std::unique_ptr<MissiveService> missive,
      base::OnceCallback<void(Status)> failure_cb = base::BindOnce(OnFailure));
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  void Shutdown();

  // Forward org::chromium::MissivedInterface
  void EnqueueRecord(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                         EnqueueRecordResponse>> out_response,
                     dbus::Message* message,
                     const EnqueueRecordRequest& in_request) override;

  void FlushPriority(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                         FlushPriorityResponse>> out_response,
                     const FlushPriorityRequest& in_request) override;

  void ConfirmRecordUpload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          ConfirmRecordUploadResponse>> out_response,
      const ConfirmRecordUploadRequest& in_request) override;

  void UpdateConfigInMissive(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          UpdateConfigInMissiveResponse>> out_response,
      const UpdateConfigInMissiveRequest& in_request) override;

  void UpdateEncryptionKey(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          UpdateEncryptionKeyResponse>> out_response,
      const UpdateEncryptionKeyRequest& in_request) override;

  // Handles NameOwnerChanged signals from the D-Bus system bus. Used to detect
  // when clients disconnect so we can evict their cached UID mappings.
  void OnNameOwnerChanged(dbus::Signal* signal);

 private:
  void StartupFinished(base::OnceCallback<void(Status)> failure_cb,
                       Status status);

  static void OnFailure(Status status);

  void OnGetConnectionUnixUser(const std::string& sender,
                               dbus::Response* response);

  struct PendingEnqueueRequest {
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<EnqueueRecordResponse>>
        response;
    EnqueueRecordRequest request;
  };

  brillo::dbus_utils::DBusObject dbus_object_;
  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* dbus_proxy_;
  std::map<std::string, uid_t> sender_uids_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::map<std::string, std::vector<PendingEnqueueRequest>> pending_lookups_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<MissiveService> missive_
      GUARDED_BY_CONTEXT(sequence_checker_);
  bool daemon_is_ready_ GUARDED_BY_CONTEXT(sequence_checker_) = false;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<DBusAdaptor> weak_ptr_factory_
      GUARDED_BY_CONTEXT(sequence_checker_){this};
};

}  // namespace reporting

#endif  // MISSIVE_DBUS_DBUS_ADAPTOR_H_
