// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/chaps_service.h"

#include <base/logging.h>
#include <base/memory/scoped_ptr.h>

#include "chaps/attributes.h"
#include "chaps/chaps.h"
#include "chaps/chaps_utility.h"
#include "chaps/object.h"
#include "chaps/session.h"
#include "chaps/slot_manager.h"

using std::string;
using std::vector;
using chromeos::SecureBlob;

namespace chaps {

ChapsServiceImpl::ChapsServiceImpl(SlotManager* slot_manager)
    : slot_manager_(slot_manager),
      init_(false) {
}

ChapsServiceImpl::~ChapsServiceImpl() {
  TearDown();
}

bool ChapsServiceImpl::Init() {
  CHECK(slot_manager_);
  init_ = true;
  return true;
}

void ChapsServiceImpl::TearDown() {
  init_ = false;
}

uint32_t ChapsServiceImpl::GetSlotList(const SecureBlob& isolate_credential,
                                       bool token_present,
                                       vector<uint64_t>* slot_list) {
  CHECK(init_);
  if (!slot_list || slot_list->size() > 0)
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  int num_slots = slot_manager_->GetSlotCount();
  for (int i = 0; i < num_slots; ++i) {
    if (slot_manager_->IsTokenAccessible(isolate_credential, i) &&
        (!token_present ||
         slot_manager_->IsTokenPresent(isolate_credential, i)))
      slot_list->push_back(i);
  }
  return CKR_OK;
}

uint32_t ChapsServiceImpl::GetSlotInfo(const SecureBlob& isolate_credential,
                                       uint64_t slot_id,
                                       vector<uint8_t>* slot_description,
                                       vector<uint8_t>* manufacturer_id,
                                       uint64_t* flags,
                                       uint8_t* hardware_version_major,
                                       uint8_t* hardware_version_minor,
                                       uint8_t* firmware_version_major,
                                       uint8_t* firmware_version_minor) {
  if (!slot_description || !manufacturer_id || !flags ||
      !hardware_version_major || !hardware_version_minor ||
      !firmware_version_major || !firmware_version_minor) {
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  }
  if (static_cast<int>(slot_id) >= slot_manager_->GetSlotCount() ||
      !slot_manager_->IsTokenAccessible(isolate_credential, slot_id))
    LOG_CK_RV_AND_RETURN(CKR_SLOT_ID_INVALID);
  CK_SLOT_INFO slot_info;
  slot_manager_->GetSlotInfo(isolate_credential, slot_id, &slot_info);
  *slot_description =
      ConvertByteBufferToVector(slot_info.slotDescription,
                                arraysize(slot_info.slotDescription));
  *manufacturer_id =
      ConvertByteBufferToVector(slot_info.manufacturerID,
                                arraysize(slot_info.manufacturerID));
  *flags = static_cast<uint64_t>(slot_info.flags);
  *hardware_version_major = slot_info.hardwareVersion.major;
  *hardware_version_minor = slot_info.hardwareVersion.minor;
  *firmware_version_major = slot_info.firmwareVersion.major;
  *firmware_version_minor = slot_info.firmwareVersion.minor;
  return CKR_OK;
}

uint32_t ChapsServiceImpl::GetTokenInfo(const SecureBlob& isolate_credential,
                                        uint64_t slot_id,
                                        vector<uint8_t>* label,
                                        vector<uint8_t>* manufacturer_id,
                                        vector<uint8_t>* model,
                                        vector<uint8_t>* serial_number,
                                        uint64_t* flags,
                                        uint64_t* max_session_count,
                                        uint64_t* session_count,
                                        uint64_t* max_session_count_rw,
                                        uint64_t* session_count_rw,
                                        uint64_t* max_pin_len,
                                        uint64_t* min_pin_len,
                                        uint64_t* total_public_memory,
                                        uint64_t* free_public_memory,
                                        uint64_t* total_private_memory,
                                        uint64_t* free_private_memory,
                                        uint8_t* hardware_version_major,
                                        uint8_t* hardware_version_minor,
                                        uint8_t* firmware_version_major,
                                        uint8_t* firmware_version_minor) {
  if (!label || !manufacturer_id || !model || !serial_number || !flags ||
      !max_session_count || !session_count || !max_session_count_rw ||
      !session_count_rw || !max_pin_len || !min_pin_len ||
      !total_public_memory || !free_public_memory ||
      !total_private_memory || !free_private_memory ||
      !hardware_version_major || !hardware_version_minor ||
      !firmware_version_major || !firmware_version_minor) {
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  }
  if (static_cast<int>(slot_id) >= slot_manager_->GetSlotCount() ||
      !slot_manager_->IsTokenAccessible(isolate_credential, slot_id))
    LOG_CK_RV_AND_RETURN(CKR_SLOT_ID_INVALID);
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->IsTokenPresent(isolate_credential,
                                                         slot_id),
                          CKR_TOKEN_NOT_PRESENT);
  CK_TOKEN_INFO token_info;
  slot_manager_->GetTokenInfo(isolate_credential, slot_id, &token_info);
  *label =
      ConvertByteBufferToVector(token_info.label, arraysize(token_info.label));
  *manufacturer_id =
      ConvertByteBufferToVector(token_info.manufacturerID,
                                arraysize(token_info.manufacturerID));
  *model = ConvertByteBufferToVector(token_info.model,
                                     arraysize(token_info.model));
  *serial_number =
      ConvertByteBufferToVector(token_info.serialNumber,
                                arraysize(token_info.serialNumber));
  *flags = token_info.flags;
  *max_session_count = token_info.ulMaxSessionCount;
  *session_count = token_info.ulSessionCount;
  *max_session_count_rw = token_info.ulMaxRwSessionCount;
  *session_count_rw = token_info.ulRwSessionCount;
  *max_pin_len = token_info.ulMaxPinLen;
  *min_pin_len = token_info.ulMinPinLen;
  *total_public_memory = token_info.ulTotalPublicMemory;
  *free_public_memory = token_info.ulFreePublicMemory;
  *total_private_memory = token_info.ulTotalPrivateMemory;
  *free_private_memory = token_info.ulFreePrivateMemory;
  *hardware_version_major = token_info.hardwareVersion.major;
  *hardware_version_minor = token_info.hardwareVersion.minor;
  *firmware_version_major = token_info.firmwareVersion.major;
  *firmware_version_minor = token_info.firmwareVersion.minor;
  return CKR_OK;
}

uint32_t ChapsServiceImpl::GetMechanismList(
    const SecureBlob& isolate_credential,
    uint64_t slot_id,
    vector<uint64_t>* mechanism_list) {
  LOG_CK_RV_AND_RETURN_IF(!mechanism_list || mechanism_list->size() > 0,
                          CKR_ARGUMENTS_BAD);
  if (static_cast<int>(slot_id) >= slot_manager_->GetSlotCount() ||
      !slot_manager_->IsTokenAccessible(isolate_credential, slot_id))
    LOG_CK_RV_AND_RETURN(CKR_SLOT_ID_INVALID);
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->IsTokenPresent(isolate_credential,
                                                         slot_id),
                          CKR_TOKEN_NOT_PRESENT);
  const MechanismMap* mechanism_info =
    slot_manager_->GetMechanismInfo(isolate_credential, slot_id);
  CHECK(mechanism_info);
  for (MechanismMapIterator it = mechanism_info->begin();
       it != mechanism_info->end();
       ++it) {
    mechanism_list->push_back(it->first);
  }
  return CKR_OK;
}

uint32_t ChapsServiceImpl::GetMechanismInfo(
      const SecureBlob& isolate_credential,
      uint64_t slot_id,
      uint64_t mechanism_type,
      uint64_t* min_key_size,
      uint64_t* max_key_size,
      uint64_t* flags) {
  if (!min_key_size || !max_key_size || !flags)
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  if (static_cast<int>(slot_id) >= slot_manager_->GetSlotCount() ||
      !slot_manager_->IsTokenAccessible(isolate_credential, slot_id))
    LOG_CK_RV_AND_RETURN(CKR_SLOT_ID_INVALID);
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->IsTokenPresent(isolate_credential,
                                                         slot_id),
                          CKR_TOKEN_NOT_PRESENT);
  const MechanismMap* mechanism_info =
    slot_manager_->GetMechanismInfo(isolate_credential, slot_id);
  CHECK(mechanism_info);
  MechanismMapIterator it = mechanism_info->find(mechanism_type);
  LOG_CK_RV_AND_RETURN_IF(it == mechanism_info->end(), CKR_MECHANISM_INVALID);
  *min_key_size = static_cast<uint64_t>(it->second.ulMinKeySize);
  *max_key_size = static_cast<uint64_t>(it->second.ulMaxKeySize);
  *flags = static_cast<uint64_t>(it->second.flags);
  return CKR_OK;
}

uint32_t ChapsServiceImpl::InitToken(const SecureBlob& isolate_credential,
                                     uint64_t slot_id,
                                     const string* so_pin,
                                     const vector<uint8_t>& label) {
  LOG_CK_RV_AND_RETURN_IF(label.size() != chaps::kTokenLabelSize,
                          CKR_ARGUMENTS_BAD);
  if (static_cast<int>(slot_id) >= slot_manager_->GetSlotCount() ||
      !slot_manager_->IsTokenAccessible(isolate_credential, slot_id))
    LOG_CK_RV_AND_RETURN(CKR_SLOT_ID_INVALID);
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->IsTokenPresent(isolate_credential,
                                                         slot_id),
                          CKR_TOKEN_NOT_PRESENT);
  // We have no notion of a security officer role and no notion of initializing
  // a token via this interface.  CKR_FUNCTION_NOT_SUPPORTED could be an option
  // here but reporting an incorrect pin is more likely to be handled gracefully
  // by the caller.
  LOG_CK_RV_AND_RETURN(CKR_PIN_INCORRECT);
}

uint32_t ChapsServiceImpl::InitPIN(const SecureBlob& isolate_credential,
                                   uint64_t session_id, const string* pin) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  // Authentication is not handled via this interface.  Since this function can
  // only be called in the "R/W SO Functions" state and we don't support this
  // state, CKR_USER_NOT_LOGGED_IN is the appropriate response.
  LOG_CK_RV_AND_RETURN(CKR_USER_NOT_LOGGED_IN);
}

uint32_t ChapsServiceImpl::SetPIN(const SecureBlob& isolate_credential,
                                  uint64_t session_id,
                                  const string* old_pin,
                                  const string* new_pin) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  // Authentication is not handled via this interface.  We do not support
  // changing a pin or password of any kind.
  LOG_CK_RV_AND_RETURN(CKR_PIN_INVALID);
}

uint32_t ChapsServiceImpl::OpenSession(const SecureBlob& isolate_credential,
                                       uint64_t slot_id,
                                       uint64_t flags,
                                       uint64_t* session_id) {
  LOG_CK_RV_AND_RETURN_IF(!session_id, CKR_ARGUMENTS_BAD);
  if (static_cast<int>(slot_id) >= slot_manager_->GetSlotCount() ||
      !slot_manager_->IsTokenAccessible(isolate_credential, slot_id))
    LOG_CK_RV_AND_RETURN(CKR_SLOT_ID_INVALID);
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->IsTokenPresent(isolate_credential,
                                                         slot_id),
                          CKR_TOKEN_NOT_PRESENT);
  LOG_CK_RV_AND_RETURN_IF(0 == (flags & CKF_SERIAL_SESSION),
                          CKR_SESSION_PARALLEL_NOT_SUPPORTED);
  *session_id = slot_manager_->OpenSession(isolate_credential, slot_id,
                                           (flags & CKF_RW_SESSION) == 0);
  return CKR_OK;
}

uint32_t ChapsServiceImpl::CloseSession(const SecureBlob& isolate_credential,
                                        uint64_t session_id) {
  if (!slot_manager_->CloseSession(isolate_credential, session_id))
    LOG_CK_RV_AND_RETURN(CKR_SESSION_HANDLE_INVALID);
  return CKR_OK;
}

uint32_t ChapsServiceImpl::CloseAllSessions(
      const SecureBlob& isolate_credential,
      uint64_t slot_id) {
  if (static_cast<int>(slot_id) >= slot_manager_->GetSlotCount() ||
      !slot_manager_->IsTokenAccessible(isolate_credential, slot_id))
    LOG_CK_RV_AND_RETURN(CKR_SLOT_ID_INVALID);
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->IsTokenPresent(isolate_credential,
                                                         slot_id),
                          CKR_TOKEN_NOT_PRESENT);
  slot_manager_->CloseAllSessions(isolate_credential, slot_id);
  return CKR_OK;
}

uint32_t ChapsServiceImpl::GetSessionInfo(const SecureBlob& isolate_credential,
                                          uint64_t session_id,
                                          uint64_t* slot_id,
                                          uint64_t* state,
                                          uint64_t* flags,
                                          uint64_t* device_error) {
  if (!slot_id || !state || !flags || !device_error)
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  *slot_id = static_cast<uint64_t>(session->GetSlot());
  *state = static_cast<uint64_t>(session->GetState());
  *flags = CKF_SERIAL_SESSION;
  if (!session->IsReadOnly())
    *flags |= CKF_RW_SESSION;
  *device_error = 0;
  return CKR_OK;
}

uint32_t ChapsServiceImpl::GetOperationState(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    vector<uint8_t>* operation_state) {
  LOG_CK_RV_AND_RETURN_IF(!operation_state, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  LOG_CK_RV_AND_RETURN_IF(!session->IsOperationActive(kEncrypt) &&
                          !session->IsOperationActive(kDecrypt) &&
                          !session->IsOperationActive(kDigest) &&
                          !session->IsOperationActive(kSign) &&
                          !session->IsOperationActive(kVerify),
                          CKR_OPERATION_NOT_INITIALIZED);
  // There is an active operation but we'll still refuse to give out state.
  LOG_CK_RV_AND_RETURN(CKR_STATE_UNSAVEABLE);
}

uint32_t ChapsServiceImpl::SetOperationState(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& operation_state,
    uint64_t encryption_key_handle,
    uint64_t authentication_key_handle) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  // We don't give out operation state so there's no way this is valid.
  LOG_CK_RV_AND_RETURN(CKR_SAVED_STATE_INVALID);
}

uint32_t ChapsServiceImpl::Login(const SecureBlob& isolate_credential,
                                 uint64_t session_id,
                                 uint64_t user_type,
                                 const string* pin) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  // We have no notion of a security officer role.
  LOG_CK_RV_AND_RETURN_IF(user_type == CKU_SO, CKR_PIN_INCORRECT);
  LOG_CK_RV_AND_RETURN_IF(user_type != CKU_USER &&
                          user_type != CKU_CONTEXT_SPECIFIC,
                          CKR_USER_TYPE_INVALID);
  // For backwards compatibility we'll accept the hard-coded pin previously used
  // with openCryptoki.  We'll also accept a protected authentication path
  // operation (i.e. a null pin).
  const string legacy_pin("111111");
  LOG_CK_RV_AND_RETURN_IF(pin && *pin != legacy_pin, CKR_PIN_INCORRECT);
  // After calling C_Login, applications will expect private objects to be
  // available for queries. Wait for them to become available before returning.
  session->WaitForPrivateObjects();
  // We could use CKR_USER_ALREADY_LOGGED_IN but that will cause some
  // applications to close all sessions and start from scratch which is
  // unnecessary.
  return CKR_OK;
}

uint32_t ChapsServiceImpl::Logout(const SecureBlob& isolate_credential,
                                  uint64_t session_id) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  return CKR_OK;
}

uint32_t ChapsServiceImpl::CreateObject(const SecureBlob& isolate_credential,
                                        uint64_t session_id,
                                        const vector<uint8_t>& attributes,
                                        uint64_t* new_object_handle) {
  LOG_CK_RV_AND_RETURN_IF(!new_object_handle, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  Attributes parsed_attributes;
  LOG_CK_RV_AND_RETURN_IF(!parsed_attributes.Parse(attributes),
                          CKR_TEMPLATE_INCONSISTENT);
  return session->CreateObject(
      parsed_attributes.attributes(),
      parsed_attributes.num_attributes(),
      PreservedValue<uint64_t, int>(new_object_handle));
}

uint32_t ChapsServiceImpl::CopyObject(const SecureBlob& isolate_credential,
                                      uint64_t session_id,
                                      uint64_t object_handle,
                                      const vector<uint8_t>& attributes,
                                      uint64_t* new_object_handle) {
  LOG_CK_RV_AND_RETURN_IF(!new_object_handle, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  Attributes parsed_attributes;
  LOG_CK_RV_AND_RETURN_IF(!parsed_attributes.Parse(attributes),
                          CKR_TEMPLATE_INCONSISTENT);
  return session->CopyObject(parsed_attributes.attributes(),
                             parsed_attributes.num_attributes(),
                             object_handle,
                             PreservedValue<uint64_t, int>(new_object_handle));
}

uint32_t ChapsServiceImpl::DestroyObject(const SecureBlob& isolate_credential,
                                         uint64_t session_id,
                                         uint64_t object_handle) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  return session->DestroyObject(object_handle);
}

uint32_t ChapsServiceImpl::GetObjectSize(const SecureBlob& isolate_credential,
                                         uint64_t session_id,
                                         uint64_t object_handle,
                                         uint64_t* object_size) {
  LOG_CK_RV_AND_RETURN_IF(!object_size, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  const Object* object = NULL;
  LOG_CK_RV_AND_RETURN_IF(!session->GetObject(object_handle, &object),
                          CKR_OBJECT_HANDLE_INVALID);
  CHECK(object);
  *object_size = object->GetSize();
  return CKR_OK;
}

uint32_t ChapsServiceImpl::GetAttributeValue(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t object_handle,
    const vector<uint8_t>& attributes_in,
    vector<uint8_t>* attributes_out) {
  LOG_CK_RV_AND_RETURN_IF(!attributes_out, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  const Object* object = NULL;
  LOG_CK_RV_AND_RETURN_IF(!session->GetObject(object_handle, &object),
                          CKR_OBJECT_HANDLE_INVALID);
  CHECK(object);
  Attributes tmp;
  LOG_CK_RV_AND_RETURN_IF(!tmp.Parse(attributes_in), CKR_TEMPLATE_INCONSISTENT);
  CK_RV result = object->GetAttributes(tmp.attributes(), tmp.num_attributes());
  if (result == CKR_OK ||
      result == CKR_ATTRIBUTE_SENSITIVE ||
      result == CKR_ATTRIBUTE_TYPE_INVALID ||
      result == CKR_BUFFER_TOO_SMALL) {
    LOG_CK_RV_AND_RETURN_IF(!tmp.Serialize(attributes_out),
                            CKR_FUNCTION_FAILED);
  }
  return result;
}

uint32_t ChapsServiceImpl::SetAttributeValue(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t object_handle,
    const vector<uint8_t>& attributes) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  Object* object = NULL;
  LOG_CK_RV_AND_RETURN_IF(!session->GetModifiableObject(object_handle, &object),
                          CKR_OBJECT_HANDLE_INVALID);
  CHECK(object);
  Attributes tmp;
  LOG_CK_RV_AND_RETURN_IF(!tmp.Parse(attributes), CKR_TEMPLATE_INCONSISTENT);
  CK_RV result = object->SetAttributes(tmp.attributes(), tmp.num_attributes());
  LOG_CK_RV_AND_RETURN_IF_ERR(result);
  LOG_CK_RV_AND_RETURN_IF(!session->FlushModifiableObject(object),
                          CKR_FUNCTION_FAILED);
  return CKR_OK;
}

uint32_t ChapsServiceImpl::FindObjectsInit(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& attributes) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  Attributes tmp;
  LOG_CK_RV_AND_RETURN_IF(!tmp.Parse(attributes), CKR_TEMPLATE_INCONSISTENT);
  return session->FindObjectsInit(tmp.attributes(), tmp.num_attributes());
}

uint32_t ChapsServiceImpl::FindObjects(const SecureBlob& isolate_credential,
                                       uint64_t session_id,
                                       uint64_t max_object_count,
                                       vector<uint64_t>* object_list) {
  if (!object_list || object_list->size() > 0)
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  vector<int> tmp;
  CK_RV result = session->FindObjects(max_object_count, &tmp);
  if (result == CKR_OK) {
    for (size_t i = 0; i < tmp.size(); ++i) {
      object_list->push_back(static_cast<uint64_t>(tmp[i]));
    }
  }
  return result;
}

uint32_t ChapsServiceImpl::FindObjectsFinal(
      const SecureBlob& isolate_credential,
      uint64_t session_id) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  return session->FindObjectsFinal();
}

uint32_t ChapsServiceImpl::EncryptInit(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    uint64_t key_handle) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  const Object* key = NULL;
  LOG_CK_RV_AND_RETURN_IF(!session->GetObject(key_handle, &key),
                          CKR_KEY_HANDLE_INVALID);
  CHECK(key);
  return session->OperationInit(kEncrypt,
                                mechanism_type,
                                ConvertByteVectorToString(mechanism_parameter),
                                key);
}

uint32_t ChapsServiceImpl::Encrypt(const SecureBlob& isolate_credential,
                                   uint64_t session_id,
                                   const vector<uint8_t>& data_in,
                                   uint64_t max_out_length,
                                   uint64_t* actual_out_length,
                                   vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  *actual_out_length = max_out_length;
  return session->OperationSinglePart(
      kEncrypt,
      ConvertByteVectorToString(data_in),
      PreservedValue<uint64_t, int>(actual_out_length),
      PreservedByteVector(data_out));
}

uint32_t ChapsServiceImpl::EncryptUpdate(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& data_in,
    uint64_t max_out_length,
    uint64_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  *actual_out_length = max_out_length;
  return session->OperationUpdate(
      kEncrypt,
      ConvertByteVectorToString(data_in),
      PreservedValue<uint64_t, int>(actual_out_length),
      PreservedByteVector(data_out));
}

uint32_t ChapsServiceImpl::EncryptFinal(const SecureBlob& isolate_credential,
                                        uint64_t session_id,
                                        uint64_t max_out_length,
                                        uint64_t* actual_out_length,
                                        vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  *actual_out_length = max_out_length;
  return session->OperationFinal(
      kEncrypt,
      PreservedValue<uint64_t, int>(actual_out_length),
      PreservedByteVector(data_out));
}

void ChapsServiceImpl::EncryptCancel(const SecureBlob& isolate_credential,
                                     uint64_t session_id) {
  Session* session = NULL;
  if (!slot_manager_->GetSession(isolate_credential,
                                 session_id,
                                 &session))
    return;
  CHECK(session);
  session->OperationCancel(kEncrypt);
}

uint32_t ChapsServiceImpl::DecryptInit(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    uint64_t key_handle) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  const Object* key = NULL;
  LOG_CK_RV_AND_RETURN_IF(!session->GetObject(key_handle, &key),
                          CKR_KEY_HANDLE_INVALID);
  CHECK(key);
  return session->OperationInit(kDecrypt,
                                mechanism_type,
                                ConvertByteVectorToString(mechanism_parameter),
                                key);
}

uint32_t ChapsServiceImpl::Decrypt(const SecureBlob& isolate_credential,
                                   uint64_t session_id,
                                   const vector<uint8_t>& data_in,
                                   uint64_t max_out_length,
                                   uint64_t* actual_out_length,
                                   vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  *actual_out_length = max_out_length;
  return session->OperationSinglePart(
      kDecrypt,
      ConvertByteVectorToString(data_in),
      PreservedValue<uint64_t, int>(actual_out_length),
      PreservedByteVector(data_out));
}

uint32_t ChapsServiceImpl::DecryptUpdate(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& data_in,
    uint64_t max_out_length,
    uint64_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  *actual_out_length = max_out_length;
  return session->OperationUpdate(
      kDecrypt,
      ConvertByteVectorToString(data_in),
      PreservedValue<uint64_t, int>(actual_out_length),
      PreservedByteVector(data_out));
}

uint32_t ChapsServiceImpl::DecryptFinal(const SecureBlob& isolate_credential,
                                        uint64_t session_id,
                                        uint64_t max_out_length,
                                        uint64_t* actual_out_length,
                                        vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  *actual_out_length = max_out_length;
  return session->OperationFinal(
      kDecrypt,
      PreservedValue<uint64_t, int>(actual_out_length),
      PreservedByteVector(data_out));
}

void ChapsServiceImpl::DecryptCancel(const SecureBlob& isolate_credential,
                                     uint64_t session_id) {
  Session* session = NULL;
  if (!slot_manager_->GetSession(isolate_credential,
                                 session_id,
                                 &session))
    return;
  CHECK(session);
  session->OperationCancel(kDecrypt);
}


uint32_t ChapsServiceImpl::DigestInit(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  return session->OperationInit(kDigest,
                                mechanism_type,
                                ConvertByteVectorToString(mechanism_parameter),
                                NULL);
}

uint32_t ChapsServiceImpl::Digest(const SecureBlob& isolate_credential,
                                  uint64_t session_id,
                                  const vector<uint8_t>& data_in,
                                  uint64_t max_out_length,
                                  uint64_t* actual_out_length,
                                  vector<uint8_t>* digest) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !digest, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  *actual_out_length = max_out_length;
  return session->OperationSinglePart(
      kDigest,
      ConvertByteVectorToString(data_in),
      PreservedValue<uint64_t, int>(actual_out_length),
      PreservedByteVector(digest));
}

uint32_t ChapsServiceImpl::DigestUpdate(const SecureBlob& isolate_credential,
                                        uint64_t session_id,
                                        const vector<uint8_t>& data_in) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  return session->OperationUpdate(kDigest,
                                  ConvertByteVectorToString(data_in),
                                  NULL,
                                  NULL);
}

uint32_t ChapsServiceImpl::DigestKey(const SecureBlob& isolate_credential,
                                     uint64_t session_id,
                                     uint64_t key_handle) {
  // We don't give out key digests.
  LOG_CK_RV_AND_RETURN(CKR_KEY_INDIGESTIBLE);
}

uint32_t ChapsServiceImpl::DigestFinal(const SecureBlob& isolate_credential,
                                       uint64_t session_id,
                                       uint64_t max_out_length,
                                       uint64_t* actual_out_length,
                                       vector<uint8_t>* digest) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !digest, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  *actual_out_length = max_out_length;
  return session->OperationFinal(
      kDigest,
      PreservedValue<uint64_t, int>(actual_out_length),
      PreservedByteVector(digest));
}

void ChapsServiceImpl::DigestCancel(const SecureBlob& isolate_credential,
                                    uint64_t session_id) {
  Session* session = NULL;
  if (!slot_manager_->GetSession(isolate_credential,
                                 session_id,
                                 &session))
    return;
  CHECK(session);
  session->OperationCancel(kDigest);
}

uint32_t ChapsServiceImpl::SignInit(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    uint64_t key_handle) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  const Object* key = NULL;
  LOG_CK_RV_AND_RETURN_IF(!session->GetObject(key_handle, &key),
                          CKR_KEY_HANDLE_INVALID);
  CHECK(key);
  return session->OperationInit(kSign,
                                mechanism_type,
                                ConvertByteVectorToString(mechanism_parameter),
                                key);
}

uint32_t ChapsServiceImpl::Sign(const SecureBlob& isolate_credential,
                                uint64_t session_id,
                                const vector<uint8_t>& data,
                                uint64_t max_out_length,
                                uint64_t* actual_out_length,
                                vector<uint8_t>* signature) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !signature, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  *actual_out_length = max_out_length;
  return session->OperationSinglePart(
      kSign,
      ConvertByteVectorToString(data),
      PreservedValue<uint64_t, int>(actual_out_length),
      PreservedByteVector(signature));
}

uint32_t ChapsServiceImpl::SignUpdate(const SecureBlob& isolate_credential,
                                      uint64_t session_id,
                                      const vector<uint8_t>& data_part) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  return session->OperationUpdate(kSign,
                                  ConvertByteVectorToString(data_part),
                                  NULL,
                                  NULL);
}

uint32_t ChapsServiceImpl::SignFinal(const SecureBlob& isolate_credential,
                                     uint64_t session_id,
                                     uint64_t max_out_length,
                                     uint64_t* actual_out_length,
                                     vector<uint8_t>* signature) {
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !signature, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  *actual_out_length = max_out_length;
  return session->OperationFinal(
      kSign,
      PreservedValue<uint64_t, int>(actual_out_length),
      PreservedByteVector(signature));
}

void ChapsServiceImpl::SignCancel(const SecureBlob& isolate_credential,
                                  uint64_t session_id) {
  Session* session = NULL;
  if (!slot_manager_->GetSession(isolate_credential,
                                 session_id,
                                 &session))
    return;
  CHECK(session);
  session->OperationCancel(kSign);
}

uint32_t ChapsServiceImpl::SignRecoverInit(
      const SecureBlob& isolate_credential,
      uint64_t session_id,
      uint64_t mechanism_type,
      const vector<uint8_t>& mechanism_parameter,
      uint64_t key_handle) {
  LOG_CK_RV_AND_RETURN(CKR_FUNCTION_NOT_SUPPORTED);
}

uint32_t ChapsServiceImpl::SignRecover(const SecureBlob& isolate_credential,
                                       uint64_t session_id,
                                       const vector<uint8_t>& data,
                                       uint64_t max_out_length,
                                       uint64_t* actual_out_length,
                                       vector<uint8_t>* signature) {
  LOG_CK_RV_AND_RETURN(CKR_FUNCTION_NOT_SUPPORTED);
}

uint32_t ChapsServiceImpl::VerifyInit(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    uint64_t key_handle) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  const Object* key = NULL;
  LOG_CK_RV_AND_RETURN_IF(!session->GetObject(key_handle, &key),
                          CKR_KEY_HANDLE_INVALID);
  CHECK(key);
  return session->OperationInit(kVerify,
                                mechanism_type,
                                ConvertByteVectorToString(mechanism_parameter),
                                key);
}

uint32_t ChapsServiceImpl::Verify(const SecureBlob& isolate_credential,
                                  uint64_t session_id,
                                  const vector<uint8_t>& data,
                                  const vector<uint8_t>& signature) {
  CK_RV result = VerifyUpdate(isolate_credential, session_id, data);
  if (result == CKR_OK)
    result = VerifyFinal(isolate_credential, session_id, signature);
  return result;
}

uint32_t ChapsServiceImpl::VerifyUpdate(const SecureBlob& isolate_credential,
                                        uint64_t session_id,
                                        const vector<uint8_t>& data_part) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  return session->OperationUpdate(kVerify,
                                  ConvertByteVectorToString(data_part),
                                  NULL,
                                  NULL);
}

uint32_t ChapsServiceImpl::VerifyFinal(const SecureBlob& isolate_credential,
                                       uint64_t session_id,
                                       const vector<uint8_t>& signature) {
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  return session->VerifyFinal(ConvertByteVectorToString(signature));
}

void ChapsServiceImpl::VerifyCancel(const SecureBlob& isolate_credential,
                                    uint64_t session_id) {
  Session* session = NULL;
  if (!slot_manager_->GetSession(isolate_credential,
                                 session_id,
                                 &session))
    return;
  CHECK(session);
  session->OperationCancel(kVerify);
}

uint32_t ChapsServiceImpl::VerifyRecoverInit(
      const SecureBlob& isolate_credential,
      uint64_t session_id,
      uint64_t mechanism_type,
      const vector<uint8_t>& mechanism_parameter,
      uint64_t key_handle) {
  LOG_CK_RV_AND_RETURN(CKR_FUNCTION_NOT_SUPPORTED);
}

uint32_t ChapsServiceImpl::VerifyRecover(const SecureBlob& isolate_credential,
                                         uint64_t session_id,
                                         const vector<uint8_t>& signature,
                                         uint64_t max_out_length,
                                         uint64_t* actual_out_length,
                                         vector<uint8_t>* data) {
  LOG_CK_RV_AND_RETURN(CKR_FUNCTION_NOT_SUPPORTED);
}

uint32_t ChapsServiceImpl::DigestEncryptUpdate(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& data_in,
    uint64_t max_out_length,
    uint64_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN(CKR_FUNCTION_NOT_SUPPORTED);
}

uint32_t ChapsServiceImpl::DecryptDigestUpdate(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& data_in,
    uint64_t max_out_length,
    uint64_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN(CKR_FUNCTION_NOT_SUPPORTED);
}

uint32_t ChapsServiceImpl::SignEncryptUpdate(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& data_in,
    uint64_t max_out_length,
    uint64_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN(CKR_FUNCTION_NOT_SUPPORTED);
}

uint32_t ChapsServiceImpl::DecryptVerifyUpdate(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    const vector<uint8_t>& data_in,
    uint64_t max_out_length,
    uint64_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN(CKR_FUNCTION_NOT_SUPPORTED);
}

uint32_t ChapsServiceImpl::GenerateKey(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    const vector<uint8_t>& attributes,
    uint64_t* key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!key_handle, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  Attributes tmp;
  LOG_CK_RV_AND_RETURN_IF(!tmp.Parse(attributes), CKR_TEMPLATE_INCONSISTENT);
  return session->GenerateKey(mechanism_type,
                              ConvertByteVectorToString(mechanism_parameter),
                              tmp.attributes(),
                              tmp.num_attributes(),
                              PreservedValue<uint64_t, int>(key_handle));
}

uint32_t ChapsServiceImpl::GenerateKeyPair(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    const vector<uint8_t>& public_attributes,
    const vector<uint8_t>& private_attributes,
    uint64_t* public_key_handle,
    uint64_t* private_key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!public_key_handle || !private_key_handle,
                          CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  Attributes tmp_public;
  LOG_CK_RV_AND_RETURN_IF(!tmp_public.Parse(public_attributes),
                          CKR_TEMPLATE_INCONSISTENT);
  Attributes tmp_private;
  LOG_CK_RV_AND_RETURN_IF(!tmp_private.Parse(private_attributes),
                          CKR_TEMPLATE_INCONSISTENT);
  return session->GenerateKeyPair(
      mechanism_type,
      ConvertByteVectorToString(mechanism_parameter),
      tmp_public.attributes(),
      tmp_public.num_attributes(),
      tmp_private.attributes(),
      tmp_private.num_attributes(),
      PreservedValue<uint64_t, int>(public_key_handle),
      PreservedValue<uint64_t, int>(private_key_handle));
}

uint32_t ChapsServiceImpl::WrapKey(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    uint64_t wrapping_key_handle,
    uint64_t key_handle,
    uint64_t max_out_length,
    uint64_t* actual_out_length,
    vector<uint8_t>* wrapped_key) {
  LOG_CK_RV_AND_RETURN(CKR_FUNCTION_NOT_SUPPORTED);
}

uint32_t ChapsServiceImpl::UnwrapKey(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    uint64_t wrapping_key_handle,
    const vector<uint8_t>& wrapped_key,
    const vector<uint8_t>& attributes,
    uint64_t* key_handle) {
  LOG_CK_RV_AND_RETURN(CKR_FUNCTION_NOT_SUPPORTED);
}

uint32_t ChapsServiceImpl::DeriveKey(
    const SecureBlob& isolate_credential,
    uint64_t session_id,
    uint64_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    uint64_t base_key_handle,
    const vector<uint8_t>& attributes,
    uint64_t* key_handle) {
  LOG_CK_RV_AND_RETURN(CKR_FUNCTION_NOT_SUPPORTED);
}

uint32_t ChapsServiceImpl::SeedRandom(const SecureBlob& isolate_credential,
                                      uint64_t session_id,
                                      const vector<uint8_t>& seed) {
  LOG_CK_RV_AND_RETURN_IF(seed.size() == 0, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  session->SeedRandom(ConvertByteVectorToString(seed));
  return CKR_OK;
}

uint32_t ChapsServiceImpl::GenerateRandom(const SecureBlob& isolate_credential,
                                          uint64_t session_id,
                                          uint64_t num_bytes,
                                          vector<uint8_t>* random_data) {
  LOG_CK_RV_AND_RETURN_IF(!random_data || num_bytes == 0, CKR_ARGUMENTS_BAD);
  Session* session = NULL;
  LOG_CK_RV_AND_RETURN_IF(!slot_manager_->GetSession(isolate_credential,
                                                     session_id,
                                                     &session),
                          CKR_SESSION_HANDLE_INVALID);
  CHECK(session);
  session->GenerateRandom(num_bytes, PreservedByteVector(random_data));
  return CKR_OK;
}

}  // namespace chaps
