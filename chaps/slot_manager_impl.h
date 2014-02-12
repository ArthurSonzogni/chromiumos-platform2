// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_SLOT_MANAGER_IMPL_H_
#define CHAPS_SLOT_MANAGER_IMPL_H_

#include "chaps/handle_generator.h"
#include "chaps/slot_manager.h"
#include "chaps/token_manager_interface.h"

#include <map>
#include <set>
#include <string>
#include <tr1/memory>
#include <vector>

#include <base/basictypes.h>
#include <base/synchronization/lock.h>
#include <base/threading/platform_thread.h>

#include "chaps/chaps_factory.h"
#include "chaps/object_pool.h"

namespace chaps {

class SessionFactory;
class TPMUtility;

// Maintains a list of PKCS #11 slots and modifies the list according to login
// events received. Sample usage:
//    SlotManagerImpl slot_manager(&my_factory, &my_tpm);
//    if (!slot_manager.Init()) {
//      ...
//    }
//    // Ready for use by SlotManager and LoginEventListener clients.
class SlotManagerImpl : public SlotManager,
                        public TokenManagerInterface,
                        public HandleGenerator {
 public:
  SlotManagerImpl(ChapsFactory* factory,
                  TPMUtility* tpm_utility,
                  bool auto_load_system_token);
  virtual ~SlotManagerImpl();

  // Initializes the slot manager. Returns true on success.
  virtual bool Init();

  // SlotManager methods.
  virtual int GetSlotCount() const;
  virtual bool IsTokenAccessible(const chromeos::SecureBlob& isolate_credential,
                                 int slot_id) const;
  virtual bool IsTokenPresent(const chromeos::SecureBlob& isolate_credential,
                              int slot_id) const;
  virtual void GetSlotInfo(const chromeos::SecureBlob& isolate_credential,
                           int slot_id,
                           CK_SLOT_INFO* slot_info) const;
  virtual void GetTokenInfo(const chromeos::SecureBlob& isolate_credential,
                            int slot_id,
                            CK_TOKEN_INFO* token_info) const;
  virtual const MechanismMap* GetMechanismInfo(
                            const chromeos::SecureBlob& isolate_credential,
                            int slot_id) const;
  virtual int OpenSession(const chromeos::SecureBlob& isolate_credential,
                          int slot_id,
                          bool is_read_only);
  virtual bool CloseSession(const chromeos::SecureBlob& isolate_credential,
                            int session_id);
  virtual void CloseAllSessions(const chromeos::SecureBlob& isolate_credential,
                                int slot_id);
  virtual bool GetSession(const chromeos::SecureBlob& isolate_credential,
                          int session_id, Session** session) const;

  // TokenManagerInterface methods.
  virtual bool OpenIsolate(chromeos::SecureBlob* isolate_credential,
                           bool* new_isolate_created);
  virtual void CloseIsolate(const chromeos::SecureBlob& isolate_credential);
  virtual bool LoadToken(const chromeos::SecureBlob& isolate_credential,
                         const FilePath& path,
                         const chromeos::SecureBlob& auth_data,
                         const std::string& label,
                         int* slot_id);
  virtual void UnloadToken(const chromeos::SecureBlob& isolate_credential,
                           const FilePath& path);
  virtual void ChangeTokenAuthData(const FilePath& path,
                                   const chromeos::SecureBlob& old_auth_data,
                                   const chromeos::SecureBlob& new_auth_data);
  virtual bool GetTokenPath(const chromeos::SecureBlob& isolate_credential,
                            int slot_id,
                            FilePath* path);

  // HandleGenerator methods.
  virtual int CreateHandle();

 private:

  // Holds all information associated with a particular isolate.
  struct Isolate {
    chromeos::SecureBlob credential;
    int open_count;
    // The set of slots accessible through this isolate.
    std::set<int> slot_ids;
  };

  // Holds all information associated with a particular slot.
  struct Slot {
    CK_SLOT_INFO slot_info;
    CK_TOKEN_INFO token_info;
    std::tr1::shared_ptr<ObjectPool> token_object_pool;
    // Key: A session identifier.
    // Value: The associated session object.
    std::map<int, std::tr1::shared_ptr<Session> > sessions;
    std::tr1::shared_ptr<base::PlatformThread::Delegate> worker_thread;
    base::PlatformThreadHandle worker_thread_handle;
  };

  // Internal token presence check without isolate_credential check.
  bool IsTokenPresent(int slot_id) const;

  // Provides default PKCS #11 slot and token information. This method fills
  // the given information structures with constant default values formatted to
  // be PKCS #11 compliant.
  void GetDefaultInfo(CK_SLOT_INFO* slot_info, CK_TOKEN_INFO* token_info);

  // Initializes a key hierarchy for a particular token. This will clobber any
  // existing key hierarchy for the token. Returns true on success.
  //  slot_id - The slot of the token to be initialized.
  //  object_pool - This must be the token's persistent object pool; it will be
  //                used to store internal blobs related to the key hierarchy.
  //  auth_data - Authorization data to be used for the key hierarchy. This same
  //              data will be required to use the key hierarchy in the future.
  //  master_key - On success will be assigned the new master key for the token.
  bool InitializeKeyHierarchy(int slot_id,
                              ObjectPool* object_pool,
                              const std::string& auth_data,
                              std::string* master_key);

  // Searches for slot that does not currently contain a token. If no such slot
  // exists a new slot is created. The slot identifier of the empty slot is
  // returned.
  int FindEmptySlot();

  // Creates new slots.
  //  num_slots - The number of slots to be created.
  void AddSlots(int num_slots);

  // Creates a new isolate with the given isolate credential.
  void AddIsolate(const chromeos::SecureBlob& isolate_credential);

  // Destroy isolate and unload any tokens in that isolate.
  void DestroyIsolate(const Isolate& isolate);

  // Get the path of the token loaded in the given slot.
  bool PathFromSlotId(int slot_id, FilePath* path) const;

  ChapsFactory* factory_;
  int last_handle_;
  MechanismMap mechanism_info_;
  // Key: A path to a token's storage directory.
  // Value: The identifier of the associated slot.
  std::map<FilePath, int> path_slot_map_;
  std::vector<Slot> slot_list_;
  // Key: A session identifier.
  // Value: The identifier of the associated slot.
  std::map<int, int> session_slot_map_;
  std::map<chromeos::SecureBlob, Isolate> isolate_map_;
  TPMUtility* tpm_utility_;
  base::Lock handle_generator_lock_;
  bool auto_load_system_token_;

  DISALLOW_COPY_AND_ASSIGN(SlotManagerImpl);
};

}  // namespace chaps

#endif  // CHAPS_SLOT_MANAGER_IMPL_H_
