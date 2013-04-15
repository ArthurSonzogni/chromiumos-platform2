// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// MountTask - The basis for asynchronous API work items.  It inherits from
// Task, which allows it to be called on an event thread.  Subclasses define the
// specific asychronous request, such as MountTaskMount, MountTaskMountGuest,
// MountTaskMigratePasskey, MountTaskUnmount, and MountTaskTestCredentials.
// Asynchronous tasks in cryptohome are serialized calls on a single worker
// thread separate from the dbus main event loop.  The synchronous versions of
// these APIs are also done on this worker thread, with the main thread waiting
// on a completion event to return.  This allows all of these calls to be
// serialized, as we use a common mount point for cryptohome.
//
// Also defined here is the MountTaskResult, which has the task result
// information, and the MountTaskObserver, which is called when a task is
// completed.
//
// Notifications can happen either by setting the completion event or providing
// a MountTaskObserver.  The former is used in Service (see service.cc) when
// faking synchronous versions of these tasks, and the latter is used in the
// asychronous versions.

#ifndef CRYPTOHOME_MOUNT_TASK_H_
#define CRYPTOHOME_MOUNT_TASK_H_

#include <base/atomicops.h>
#include <base/atomic_sequence_num.h>
#include <base/memory/ref_counted.h>
#include <base/threading/thread.h>
#include <base/synchronization/cancellation_flag.h>
#include <base/synchronization/waitable_event.h>
#include <chromeos/process.h>
#include <chromeos/secure_blob.h>
#include <chromeos/utility.h>

#include "cryptohome_event_source.h"
#include "mount.h"
#include "pkcs11_init.h"
#include "username_passkey.h"

using chromeos::SecureBlob;

namespace cryptohome {

extern const char* kMountTaskResultEventType;
extern const char* kPkcs11InitResultEventType;

class InstallAttributes;

class MountTaskResult : public CryptohomeEventBase {
 public:
  MountTaskResult()
      : sequence_id_(-1),
        return_status_(false),
        return_code_(MOUNT_ERROR_NONE),
        event_name_(kMountTaskResultEventType),
        mount_(NULL),
        pkcs11_init_(false),
        guest_(false) { }

  // Constructor which sets an alternative event name. Useful
  // for using MountTaskResult for other event types.
  explicit MountTaskResult(const char* event_name)
      : sequence_id_(-1),
        return_status_(false),
        return_code_(MOUNT_ERROR_NONE),
        event_name_(event_name),
        mount_(NULL),
        pkcs11_init_(false),
        guest_(false) { }

  // Copy constructor is necessary for inserting MountTaskResult into the events
  // vector in CryptohomeEventSource.
  MountTaskResult(const MountTaskResult& rhs)
      : sequence_id_(rhs.sequence_id_),
        return_status_(rhs.return_status_),
        return_code_(rhs.return_code_),
        event_name_(rhs.event_name_),
        mount_(rhs.mount_),
        pkcs11_init_(rhs.pkcs11_init_),
        guest_(rhs.guest_) {
    if (rhs.return_data())
      set_return_data(*rhs.return_data());
  }

  virtual ~MountTaskResult() { }

  // Get the asynchronous task id
  int sequence_id() const {
    return sequence_id_;
  }

  // Set the asynchronous task id
  void set_sequence_id(int value) {
    sequence_id_ = value;
  }

  // Get the status of the call
  bool return_status() const {
    return return_status_;
  }

  // Set the status of the call
  void set_return_status(bool value) {
    return_status_ = value;
  }

  // Get the MountError for applicable calls (Mount, MountGuest)
  MountError return_code() const {
    return return_code_;
  }

  // Set the MountError for applicable calls (Mount, MountGuest)
  void set_return_code(MountError value) {
    return_code_ = value;
  }

  scoped_refptr<Mount> mount() const {
    return mount_;
  }

  void set_mount(Mount* value) {
    mount_ = value;
  }

  bool pkcs11_init() const {
    return pkcs11_init_;
  }

  void set_pkcs11_init(bool value) {
    pkcs11_init_ = value;
  }

  bool guest() const {
    return guest_;
  }

  void set_guest(bool value) {
    guest_ = value;
  }

  const SecureBlob* return_data() const {
    return return_data_.get();
  }

  void set_return_data(const SecureBlob& data) {
    return_data_.reset(new SecureBlob(data.begin(), data.end()));
  }

  MountTaskResult& operator=(const MountTaskResult& rhs) {
    sequence_id_ = rhs.sequence_id_;
    return_status_ = rhs.return_status_;
    return_code_ = rhs.return_code_;
    return_data_.reset();
    if (rhs.return_data())
      set_return_data(*rhs.return_data());
    event_name_ = rhs.event_name_;
    mount_ = rhs.mount_;
    pkcs11_init_ = rhs.pkcs11_init_;
    guest_ = rhs.guest_;
    return *this;
  }

  virtual const char* GetEventName() const {
    return event_name_;
  }

 private:
  int sequence_id_;
  bool return_status_;
  MountError return_code_;
  scoped_ptr<SecureBlob> return_data_;
  const char* event_name_;
  scoped_refptr<Mount> mount_;
  bool pkcs11_init_;
  bool guest_;
};

class MountTaskObserver {
 public:
  MountTaskObserver() {}
  virtual ~MountTaskObserver() {}

  // Called by the MountTask when the task is complete. If this returns true,
  // the MountTaskObserver will be freed by the MountTask.
  virtual bool MountTaskObserve(const MountTaskResult& result) = 0;
};

class MountTask : public base::RefCountedThreadSafe<MountTask> {
 public:
  MountTask(MountTaskObserver* observer,
            Mount* mount,
            const UsernamePasskey& credentials);
  MountTask(MountTaskObserver* observer,
            Mount* mount);
  virtual ~MountTask();

  // Run is called by the worker thread when this task is being processed
  virtual void Run() {
    Notify();
  }

  // Allow cancellation to be sent from the main thread. This must be checked
  // in each inherited Run().
  virtual void Cancel() {
    base::subtle::Release_Store(&cancel_flag_, 1);
  }

  // Indicate if cancellation was requested.
  virtual bool IsCanceled() {
    return base::subtle::Acquire_Load(&cancel_flag_) != 0;
  }

  // Gets the asynchronous call id of this task
  int sequence_id() {
    return sequence_id_;
  }

  // Returns the mount this task is for.
  // TODO(wad) Figure out a better way. Queue per Mount?
  scoped_refptr<Mount> mount() {
    return mount_;
  }

  // Gets the MountTaskResult for this task
  MountTaskResult* result() {
    return result_;
  }

  // Sets the MountTaskResult for this task
  void set_result(MountTaskResult* result) {
    result_ = result;
    result_->set_sequence_id(sequence_id_);
  }

  // Sets the event to be signaled when the event is completed
  void set_complete_event(base::WaitableEvent* value) {
    complete_event_ = value;
  }

 protected:
  // Notify implements the default behavior when this task is complete
  void Notify();

  // The Mount instance that does the actual work
  scoped_refptr<Mount> mount_;

  // The Credentials associated with this task
  UsernamePasskey credentials_;

  // The asychronous call id for this task
  int sequence_id_;

  // Checked before all Run() calls to cancel.
  // base::CancellationFlag isn't available in CrOS yet.
  base::subtle::Atomic32 cancel_flag_;

 private:
  // Signal will call Signal on the completion event if it is set
  void Signal();

  // Return the next sequence number
  static int NextSequence();

  // The MountTaskObserver to be notified when this task is complete
  MountTaskObserver* observer_;

  // The default instance of MountTaskResult to use if it is not set by the
  // caller
  scoped_ptr<MountTaskResult> default_result_;

  // The actual instance of MountTaskResult to use
  MountTaskResult* result_;

  // The completion event to signal when this task is complete
  base::WaitableEvent* complete_event_;

  // An atomic incrementing sequence for setting asynchronous call ids
  static base::AtomicSequenceNumber sequence_holder_;

  DISALLOW_COPY_AND_ASSIGN(MountTask);
};

// Implements a no-op task that merely posts results
class MountTaskNop : public MountTask {
 public:
  explicit MountTaskNop(MountTaskObserver* observer)
      : MountTask(observer, NULL) {
  }
  virtual ~MountTaskNop() { }

  virtual void Run() {
    Notify();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MountTaskNop);
};

// Implements asychronous calls to Mount::Mount()
class MountTaskMount : public MountTask {
 public:
  MountTaskMount(MountTaskObserver* observer,
                 Mount* mount,
                 const UsernamePasskey& credentials,
                 const Mount::MountArgs& mount_args)
      : MountTask(observer, mount, credentials) {
    mount_args_.CopyFrom(mount_args);
  }
  virtual ~MountTaskMount() { }

  virtual void Run();

 private:
  Mount::MountArgs mount_args_;

  DISALLOW_COPY_AND_ASSIGN(MountTaskMount);
};

// Implements asychronous calls to Mount::MountGuest()
class MountTaskMountGuest : public MountTask {
 public:
  MountTaskMountGuest(MountTaskObserver* observer,
                      Mount* mount)
      : MountTask(observer, mount) {
  }
  virtual ~MountTaskMountGuest() { }

  virtual void Run();

 private:
  DISALLOW_COPY_AND_ASSIGN(MountTaskMountGuest);
};

// Implements asychronous calls to HomeDirs::Migrate()
// TODO(ellyjones): move this out of MountTask, as it doesn't take a Mount.
class MountTaskMigratePasskey : public MountTask {
 public:
  MountTaskMigratePasskey(MountTaskObserver* observer,
            HomeDirs* homedirs,
            const UsernamePasskey& credentials,
            const char* old_key)
      : MountTask(observer, NULL, credentials), homedirs_(homedirs) {
    old_key_.resize(strlen(old_key));
    memcpy(old_key_.data(), old_key, old_key_.size());
  }
  virtual ~MountTaskMigratePasskey() { }

  virtual void Run();

 private:
  chromeos::SecureBlob old_key_;
  HomeDirs* homedirs_;

  DISALLOW_COPY_AND_ASSIGN(MountTaskMigratePasskey);
};

// Implements asychronous calls to Mount::Unmount()
class MountTaskUnmount : public MountTask {
 public:
  MountTaskUnmount(MountTaskObserver* observer,
                   Mount* mount)
      : MountTask(observer, mount) {
  }
  virtual ~MountTaskUnmount() { }

  virtual void Run();

 private:
  DISALLOW_COPY_AND_ASSIGN(MountTaskUnmount);
};

// Implements asychronous calls to Mount::TestCredentials()
class MountTaskTestCredentials : public MountTask {
 public:
  MountTaskTestCredentials(MountTaskObserver* observer,
                           Mount* mount,
                           HomeDirs* homedirs,
                           const UsernamePasskey& credentials)
      : MountTask(observer, mount, credentials), homedirs_(homedirs) {
  }
  virtual ~MountTaskTestCredentials() { }

  virtual void Run();

 private:
  HomeDirs* homedirs_;
  DISALLOW_COPY_AND_ASSIGN(MountTaskTestCredentials);
};

// Implements asychronous calls to Mount::RemoveCryptohome()
class MountTaskRemove : public MountTask {
 public:
  MountTaskRemove(MountTaskObserver* observer,
                  Mount* mount,
                  const UsernamePasskey& credentials,
                  HomeDirs* homedirs)
      : MountTask(observer, mount, credentials), homedirs_(homedirs) {
  }
  virtual ~MountTaskRemove() { }

  virtual void Run();

 private:
  HomeDirs* homedirs_;
  DISALLOW_COPY_AND_ASSIGN(MountTaskRemove);
};

// Implements asychronous reset of the TPM context
class MountTaskResetTpmContext : public MountTask {
 public:
  MountTaskResetTpmContext(MountTaskObserver* observer, Mount* mount)
      : MountTask(observer, mount) {
  }
  virtual ~MountTaskResetTpmContext() { }

  virtual void Run();

 private:
  DISALLOW_COPY_AND_ASSIGN(MountTaskResetTpmContext);
};

// Implements asychronous removal of tracked subdirectories
class MountTaskAutomaticFreeDiskSpace : public MountTask {
 public:
  MountTaskAutomaticFreeDiskSpace(MountTaskObserver* observer,
                                  HomeDirs* homedirs)
      : MountTask(observer, NULL), homedirs_(homedirs) {
  }
  virtual ~MountTaskAutomaticFreeDiskSpace() { }

  virtual void Run();

 private:
  HomeDirs* homedirs_;
  DISALLOW_COPY_AND_ASSIGN(MountTaskAutomaticFreeDiskSpace);
};

// Implements asynchronous updating of the current user (if any)
// activity timestamp
class MountTaskUpdateCurrentUserActivityTimestamp : public MountTask {
 public:
  MountTaskUpdateCurrentUserActivityTimestamp(MountTaskObserver* observer,
                                              Mount* mount,
                                              int time_shift_sec)
      : MountTask(observer, mount),
        time_shift_sec_(time_shift_sec) {
  }
  virtual ~MountTaskUpdateCurrentUserActivityTimestamp() { }

  virtual void Run();

 private:
  const int time_shift_sec_;
  DISALLOW_COPY_AND_ASSIGN(MountTaskUpdateCurrentUserActivityTimestamp);
};

// Implements asynchronous initialization of Pkcs11.
class MountTaskPkcs11Init : public MountTask {
 public:
  MountTaskPkcs11Init(MountTaskObserver* observer, Mount* mount);
  virtual ~MountTaskPkcs11Init() { }

  virtual void Run();

 private:
  scoped_ptr<MountTaskResult> pkcs11_init_result_;
  DISALLOW_COPY_AND_ASSIGN(MountTaskPkcs11Init);
};

// Asynchronous install attr finalization. Not really a 'mount task' per se but
// we want to use the thread.
class MountTaskInstallAttrsFinalize : public MountTask {
 public:
  MountTaskInstallAttrsFinalize(MountTaskObserver* observer,
                                InstallAttributes* attrs)
      : MountTask(observer, NULL),
        install_attrs_(attrs) { }
  virtual ~MountTaskInstallAttrsFinalize() { }

  virtual void Run();
 private:
  InstallAttributes *install_attrs_;
  DISALLOW_COPY_AND_ASSIGN(MountTaskInstallAttrsFinalize);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOUNT_TASK_H_
