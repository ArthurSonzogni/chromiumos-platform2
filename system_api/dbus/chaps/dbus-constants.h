// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSTEM_API_DBUS_CHAPS_DBUS_CONSTANTS_H_
#define SYSTEM_API_DBUS_CHAPS_DBUS_CONSTANTS_H_

#include <stdint.h>

namespace chaps {

constexpr uint64_t kTokenLabelSize = 32;

// Chaps-specific attributes:

// PKCS #11 v2.20 section A Manifest constants page 377. PKCS11_ prefix is added
// to avoid name collisions with #define-d constants.
constexpr uint32_t PKCS11_CKA_VENDOR_DEFINED = 0x80000000;
constexpr uint32_t kKeyBlobAttribute = PKCS11_CKA_VENDOR_DEFINED + 1;
constexpr uint32_t kAuthDataAttribute = PKCS11_CKA_VENDOR_DEFINED + 2;
// If this attribute is set to true at creation or generation time, then the
// object will not be stored/wrapped in hardware-backed security element, and
// will remain purely in software.
constexpr uint32_t kForceSoftwareAttribute = PKCS11_CKA_VENDOR_DEFINED + 4;
// This attribute is set to false if the key is stored in hardware-backed
// security element, and true otherwise.
constexpr uint32_t kKeyInSoftwareAttribute = PKCS11_CKA_VENDOR_DEFINED + 5;
// If this attribute is set to true at creation or generation time, then the
// object may be generated in software, but still stored/wrapped in the
// hardware-backed security element.
constexpr uint32_t kAllowSoftwareGenAttribute = PKCS11_CKA_VENDOR_DEFINED + 6;

// Chaps-specific return values:

// PKCS #11 v2.20 section A Manifest constants page 382. PKCS11_ prefix is added
// to avoid name collisions with #define-d constants.
constexpr uint32_t PKCS11_CKR_VENDOR_DEFINED = 0x80000000UL;
constexpr uint32_t CKR_CHAPS_SPECIFIC_FIRST =
    PKCS11_CKR_VENDOR_DEFINED + 0x47474c00;
// Error code returned in case if the operation would block waiting
// for private objects to load for the token. This value is persisted to logs
// and should not be renumbered and numeric values should never be reused.
// Please keep in sync with "ChapsSessionStatus" in
// tools/metrics/histograms/enums.xml in the Chromium repo.
constexpr uint32_t CKR_WOULD_BLOCK_FOR_PRIVATE_OBJECTS =
    CKR_CHAPS_SPECIFIC_FIRST + 0;
// Client side error code returned in case the D-Bus client is null.
constexpr uint32_t CKR_DBUS_CLIENT_IS_NULL = CKR_CHAPS_SPECIFIC_FIRST + 1;
// Client side error code returned in case D-Bus returned an empty response.
constexpr uint32_t CKR_DBUS_EMPTY_RESPONSE_ERROR = CKR_CHAPS_SPECIFIC_FIRST + 2;
// Client side error code returned in case the D-Bus response couldn't be
// decoded.
constexpr uint32_t CKR_DBUS_DECODING_ERROR = CKR_CHAPS_SPECIFIC_FIRST + 3;
// Client side error code returned in case a new PKCS#11 session could not be
// opened. It is useful to differentiate from CKR_SESSION_HANDLE_INVALID and
// CKR_SESSION_CLOSED errors because for those the receiver is expected to retry
// the operation immediately and kFailedToOpenSessionError indicates a more
// persistent failure.
constexpr uint32_t CKR_FAILED_TO_OPEN_SESSION = CKR_CHAPS_SPECIFIC_FIRST + 4;

// D-Bus service constants.
constexpr char kChapsInterface[] = "org.chromium.Chaps";
constexpr char kChapsServiceName[] = "org.chromium.Chaps";
constexpr char kChapsServicePath[] = "/org/chromium/Chaps";

// Methods, should be kept in sync with the
// chaps/dbus_bindings/org.chromium.Chaps.xml file. "OpenIsolate",
// "CloseIsolate", "InitPIN", "SetPIN", "Login", "Logout" methods are excluded
// because they are unlikely to be used.
constexpr char kLoadTokenMethod[] = "LoadToken";
constexpr char kUnloadTokenMethod[] = "UnloadToken";
constexpr char kGetTokenPathMethod[] = "GetTokenPath";
constexpr char kSetLogLevelMethod[] = "SetLogLevel";
constexpr char kGetSlotListMethod[] = "GetSlotList";
constexpr char kGetSlotInfoMethod[] = "GetSlotInfo";
constexpr char kGetTokenInfoMethod[] = "GetTokenInfo";
constexpr char kGetMechanismListMethod[] = "GetMechanismList";
constexpr char kGetMechanismInfoMethod[] = "GetMechanismInfo";
constexpr char kInitTokenMethod[] = "InitToken";
constexpr char kOpenSessionMethod[] = "OpenSession";
constexpr char kCloseSessionMethod[] = "CloseSession";
constexpr char kGetSessionInfoMethod[] = "GetSessionInfo";
constexpr char kGetOperationStateMethod[] = "GetOperationState";
constexpr char kSetOperationStateMethod[] = "SetOperationState";
constexpr char kCreateObjectMethod[] = "CreateObject";
constexpr char kCopyObjectMethod[] = "CopyObject";
constexpr char kDestroyObjectMethod[] = "DestroyObject";
constexpr char kGetObjectSizeMethod[] = "GetObjectSize";
constexpr char kGetAttributeValueMethod[] = "GetAttributeValue";
constexpr char kSetAttributeValueMethod[] = "SetAttributeValue";
constexpr char kFindObjectsInitMethod[] = "FindObjectsInit";
constexpr char kFindObjectsMethod[] = "FindObjects";
constexpr char kFindObjectsFinalMethod[] = "FindObjectsFinal";
constexpr char kEncryptInitMethod[] = "EncryptInit";
constexpr char kEncryptMethod[] = "Encrypt";
constexpr char kEncryptUpdateMethod[] = "EncryptUpdate";
constexpr char kEncryptFinalMethod[] = "EncryptFinal";
constexpr char kEncryptCancelMethod[] = "EncryptCancel";
constexpr char kDecryptInitMethod[] = "DecryptInit";
constexpr char kDecryptMethod[] = "Decrypt";
constexpr char kDecryptUpdateMethod[] = "DecryptUpdate";
constexpr char kDecryptFinalMethod[] = "DecryptFinal";
constexpr char kDecryptCancelMethod[] = "DecryptCancel";
constexpr char kDigestInitMethod[] = "DigestInit";
constexpr char kDigestMethod[] = "Digest";
constexpr char kDigestUpdateMethod[] = "DigestUpdate";
constexpr char kDigestKeyMethod[] = "DigestKey";
constexpr char kDigestFinalMethod[] = "DigestFinal";
constexpr char kDigestCancelMethod[] = "DigestCancel";
constexpr char kSignInitMethod[] = "SignInit";
constexpr char kSignMethod[] = "Sign";
constexpr char kSignUpdateMethod[] = "SignUpdate";
constexpr char kSignFinalMethod[] = "SignFinal";
constexpr char kSignCancelMethod[] = "SignCancel";
constexpr char kSignRecoverInitMethod[] = "SignRecoverInit";
constexpr char kSignRecoverMethod[] = "SignRecover";
constexpr char kVerifyInitMethod[] = "VerifyInit";
constexpr char kVerifyMethod[] = "Verify";
constexpr char kVerifyUpdateMethod[] = "VerifyUpdate";
constexpr char kVerifyFinalMethod[] = "VerifyFinal";
constexpr char kVerifyCancelMethod[] = "VerifyCancel";
constexpr char kVerifyRecoverInitMethod[] = "VerifyRecoverInit";
constexpr char kVerifyRecoverMethod[] = "VerifyRecover";
constexpr char kDigestEncryptUpdateMethod[] = "DigestEncryptUpdate";
constexpr char kDecryptDigestUpdateMethod[] = "DecryptDigestUpdate";
constexpr char kSignEncryptUpdateMethod[] = "SignEncryptUpdate";
constexpr char kDecryptVerifyUpdateMethod[] = "DecryptVerifyUpdate";
constexpr char kGenerateKeyMethod[] = "GenerateKey";
constexpr char kGenerateKeyPairMethod[] = "GenerateKeyPair";
constexpr char kWrapKeyMethod[] = "WrapKey";
constexpr char kUnwrapKeyMethod[] = "UnwrapKey";
constexpr char kDeriveKeyMethod[] = "DeriveKey";
constexpr char kSeedRandomMethod[] = "SeedRandom";
constexpr char kGenerateRandomMethod[] = "GenerateRandom";

}  // namespace chaps

#endif  // SYSTEM_API_DBUS_CHAPS_DBUS_CONSTANTS_H_
