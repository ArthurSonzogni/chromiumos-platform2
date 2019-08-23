// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_CLIENT_TPM_MANAGER_UTILITY_H_
#define TPM_MANAGER_CLIENT_TPM_MANAGER_UTILITY_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/macros.h>
#include <base/optional.h>
#include <base/synchronization/lock.h>
#include <base/threading/thread.h>
#include "tpm_manager/client/tpm_nvram_dbus_proxy.h"
#include "tpm_manager/client/tpm_ownership_dbus_proxy.h"
#include "tpm_manager/client/tpm_ownership_signal_handler.h"
#include "tpm_manager/common/export.h"

namespace tpm_manager {

// A TpmUtility implementation for version-independent functions.
class TPM_MANAGER_EXPORT TpmManagerUtility
    : public TpmOwnershipTakenSignalHandler {
 public:
  TpmManagerUtility() = default;
  // a constructor which enables injection of mock interfaces.
  TpmManagerUtility(tpm_manager::TpmOwnershipInterface* tpm_owner,
                    tpm_manager::TpmNvramInterface* tpm_nvram);
  virtual ~TpmManagerUtility() = default;

  // Initializes the worker thread and proxies of |tpm_manager| and returns
  // |true| if successful. Returns |false| if we cannot start
  // |tpm_manager_thread_| or tpm_manager's interfaces fail to initialize.
  //
  // Once returing |true|, the calls of this function afterwards return |true|
  // without mutating any data member.
  virtual bool Initialize();

  // Blocking call of |TpmOwnershipDBusProxy::TakeOwnership|. Returns |true| iff
  // the operation succeeds.
  virtual bool TakeOwnership();

  // Blocking call of |TpmOwnershipDBusProxy::GetTpmStatus|.
  // Returns |true| iff the operation succeeds. Once returning |true|,
  // |is_enabled| indicates if TPM is enabled, and |is_owned| indicates if TPM
  // is owned. |local_data| is the current |LocalData| stored in the
  // |tpm_manager| service.
  virtual bool GetTpmStatus(bool* is_enabled,
                            bool* is_owned,
                            LocalData* local_data);

  // Blocking call of
  // |TpmOwnershipDBusProxy::RemoveOwnerDependency|. Returns |true| iff the
  // operation succeeds. |dependency| is the idenitier of the dependency.
  virtual bool RemoveOwnerDependency(const std::string& dependency);

  // Blocking call of
  // |TpmOwnershipDBusProxy::ClearStoredOwnerPassword|. Returns |true| iff the
  // operation succeeds.
  virtual bool ClearStoredOwnerPassword();

  // Blocking call of |TpmOwnershipDBusProxy::GetDictionaryAttackInfo|. Returns
  // |true| iff the operation succeeds. Once returning |true|, |counter|,
  // |threshold|, |lockout| and |seconds_remaining| will set to the respective
  // values of received |GetDictionaryAttackInfoReply|.
  virtual bool GetDictionaryAttackInfo(int* counter,
                                       int* threshold,
                                       bool* lockout,
                                       int* seconds_remaining);

  // Blocking call of |TpmOwnershipDBusProxy::GetDictionaryAttackInfo|. Returns
  // |true| iff the operation succeeds.
  virtual bool ResetDictionaryAttackLock();

  // Blocking call of |TpmOwnershipDBusProxy::ReadSpace|. Returns |true| iff
  // the operation succeeds. This call sends a request to read the content of
  // the nvram at |index| and stores the output data in |output|. If
  // |use_owner_auth| is set, the request tells the service to use owner
  // authorization. Note: currently the arbitrary auth value is not supported
  // since we got no use case for now.
  virtual bool ReadSpace(uint32_t index,
                         bool use_owner_auth,
                         std::string* output);

  // Gets the current status of the ownership taken signal. Returns |true| iff
  // the signal is connected, no matter if it's connected successfully or not.
  // |is_successful| indicates if the dbus signal connection is successful or
  // not. |has_received| indicates if this instance has received the ownership
  // taken signal. Once |has_received| is set as |true|,|local_data| gets
  // updated. Any output parameter will be ignored to be set if the value is
  // |nullptr|.
  virtual bool GetOwnershipTakenSignalStatus(bool* is_successful,
                                             bool* has_received,
                                             LocalData* local_data);

  void OnOwnershipTaken(const OwnershipTakenSignal& signal) override;

  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool is_successful) override;

 private:
  // Tpm_manager communication thread class that cleans up after stopping.
  class TpmManagerThread : public base::Thread {
   public:
    explicit TpmManagerThread(TpmManagerUtility* utility)
        : base::Thread("tpm_manager_thread"), utility_(utility) {
      DCHECK(utility_);
    }
    ~TpmManagerThread() override { Stop(); }

   private:
    void CleanUp() override { utility_->ShutdownTask(); }

    TpmManagerUtility* const utility_;

    DISALLOW_COPY_AND_ASSIGN(TpmManagerThread);
  };

  // Initialization operation that must be performed on the tpm_manager
  // thread.
  void InitializationTask(base::WaitableEvent* completion);

  // Shutdown operation that must be performed on the tpm_manager thread.
  void ShutdownTask();

  // Sends a request to tpm_managerd and waits for a response. The given
  // interface |method| will be called and a |reply_proto| will be populated.
  //
  // Example usage:
  //
  // tpm_manager::GetTpmStatusReply tpm_status;
  // SendTpmManagerRequestAndWait(
  //     base::Bind(&tpm_manager::TpmOwnershipInterface::GetTpmStatus,
  //                base::Unretained(tpm_owner_),
  //                tpm_manager::GetTpmStatusRequest()),
  //     &tpm_status);
  template <typename ReplyProtoType, typename MethodType>
  void SendTpmManagerRequestAndWait(const MethodType& method,
                                    ReplyProtoType* reply_proto);

  // |tpm_owner_| and |tpm_nvram_| typically point to |default_tpm_owner_| and
  // |default_tpm_nvram_| respectively, created/destroyed on the
  // |tpm_manager_thread_|. As such, should not be accessed after that thread
  // is stopped/destroyed.
  tpm_manager::TpmOwnershipInterface* tpm_owner_{nullptr};
  tpm_manager::TpmNvramInterface* tpm_nvram_{nullptr};

  // |default_tpm_owner_| and |default_tpm_nvram_| are created and destroyed
  // on the |tpm_manager_thread_|, and are not available after the thread is
  // stopped/destroyed.
  std::unique_ptr<tpm_manager::TpmOwnershipDBusProxy> default_tpm_owner_;
  std::unique_ptr<tpm_manager::TpmNvramDBusProxy> default_tpm_nvram_;

  // A message loop thread dedicated for asynchronous communication with
  // tpm_managerd. Declared last, so that it is destroyed before the
  // objects it uses.
  TpmManagerThread tpm_manager_thread_{this};

  // Data structures for the dbus signal handling.

  // |ownership_signal_lock_| is used when the signal-handling data is
  // accessed; the mutex is necessary because the user of this class could read
  // the signal data.
  base::Lock ownership_signal_lock_;

  // Only uses |is_connected_| to indicate if we can rely on the dbus signal to
  // get the local data though it could mean "not connected", "being
  // connected". Note that |is_connected_| could also mean the connection has
  // been attempted but not successfully. For naming reference, see arguments of
  // |brillo::dbus_utils::ConnectToSignal|.
  bool is_connected_{false};

  // Records if it's a successful signal connection once connected.
  bool is_connection_successful_{false};

  // |ownership_taken_signal_| stores the data once the ownership
  // taken signal is received.
  base::Optional<OwnershipTakenSignal> ownership_taken_signal_;

  DISALLOW_COPY_AND_ASSIGN(TpmManagerUtility);
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_CLIENT_TPM_MANAGER_UTILITY_H_
