// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_BACKEND_H_
#define LIBHWSEC_BACKEND_BACKEND_H_

#include <type_traits>

#include <base/notreached.h>

#include "libhwsec/backend/config.h"
#include "libhwsec/backend/da_mitigation.h"
#include "libhwsec/backend/deriving.h"
#include "libhwsec/backend/encryption.h"
#include "libhwsec/backend/key_management.h"
#include "libhwsec/backend/pinweaver.h"
#include "libhwsec/backend/random.h"
#include "libhwsec/backend/recovery_crypto.h"
#include "libhwsec/backend/ro_data.h"
#include "libhwsec/backend/sealing.h"
#include "libhwsec/backend/session_management.h"
#include "libhwsec/backend/signature_sealing.h"
#include "libhwsec/backend/signing.h"
#include "libhwsec/backend/state.h"
#include "libhwsec/backend/storage.h"
#include "libhwsec/backend/vendor.h"

namespace hwsec {

// Backend is the layer to abstract the difference between different security
// module(e.g. TPM1.2, TPM2.0, GSC...). And provide a unified interface. Note:
// This class is NOT thread safe.
//
// Note: The backend function parameters must be const reference or copyable.
// Otherwise the middleware would not be able to retry the command for
// communication error.
class Backend {
 public:
  // A helper to give subclasses the ability to access the backend.
  template <typename BackendType>
  class SubClassHelper {
   public:
    explicit SubClassHelper(BackendType& backend) : backend_(backend) {}

   protected:
    ~SubClassHelper() = default;
    BackendType& backend_;
  };

  using State = ::hwsec::State;
  using DAMitigation = ::hwsec::DAMitigation;
  using Storage = ::hwsec::Storage;
  using RoData = ::hwsec::RoData;
  using Sealing = ::hwsec::Sealing;
  using SignatureSealing = ::hwsec::SignatureSealing;
  using Deriving = ::hwsec::Deriving;
  using Encryption = ::hwsec::Encryption;
  using Signing = ::hwsec::Signing;
  using KeyManagement = ::hwsec::KeyManagement;
  using SessionManagement = ::hwsec::SessionManagement;
  using Config = ::hwsec::Config;
  using Random = ::hwsec::Random;
  using PinWeaver = ::hwsec::PinWeaver;
  using Vendor = ::hwsec::Vendor;
  using RecoveryCrypto = ::hwsec::RecoveryCrypto;

  virtual ~Backend() = default;

  // A helper to get the subclass pointer with subclass type.
  template <class SubClass>
  SubClass* Get() {
    if constexpr (std::is_same_v<SubClass, State>)
      return GetState();
    else if constexpr (std::is_same_v<SubClass, DAMitigation>)
      return GetDAMitigation();
    else if constexpr (std::is_same_v<SubClass, Storage>)
      return GetStorage();
    else if constexpr (std::is_same_v<SubClass, RoData>)
      return GetRoData();
    else if constexpr (std::is_same_v<SubClass, Sealing>)
      return GetSealing();
    else if constexpr (std::is_same_v<SubClass, SignatureSealing>)
      return GetSignatureSealing();
    else if constexpr (std::is_same_v<SubClass, Deriving>)
      return GetDeriving();
    else if constexpr (std::is_same_v<SubClass, Encryption>)
      return GetEncryption();
    else if constexpr (std::is_same_v<SubClass, Signing>)
      return GetSigning();
    else if constexpr (std::is_same_v<SubClass, KeyManagement>)
      return GetKeyManagement();
    else if constexpr (std::is_same_v<SubClass, SessionManagement>)
      return GetSessionManagement();
    else if constexpr (std::is_same_v<SubClass, Config>)
      return GetConfig();
    else if constexpr (std::is_same_v<SubClass, Random>)
      return GetRandom();
    else if constexpr (std::is_same_v<SubClass, PinWeaver>)
      return GetPinWeaver();
    else if constexpr (std::is_same_v<SubClass, Vendor>)
      return GetVendor();
    else if constexpr (std::is_same_v<SubClass, RecoveryCrypto>)
      return GetRecoveryCrypto();
    NOTREACHED() << "Should not reach here.";
  }

 private:
  virtual State* GetState() = 0;
  virtual DAMitigation* GetDAMitigation() = 0;
  virtual Storage* GetStorage() = 0;
  virtual RoData* GetRoData() = 0;
  virtual Sealing* GetSealing() = 0;
  virtual SignatureSealing* GetSignatureSealing() = 0;
  virtual Deriving* GetDeriving() = 0;
  virtual Encryption* GetEncryption() = 0;
  virtual Signing* GetSigning() = 0;
  virtual KeyManagement* GetKeyManagement() = 0;
  virtual SessionManagement* GetSessionManagement() = 0;
  virtual Config* GetConfig() = 0;
  virtual Random* GetRandom() = 0;
  virtual PinWeaver* GetPinWeaver() = 0;
  virtual Vendor* GetVendor() = 0;
  virtual RecoveryCrypto* GetRecoveryCrypto() = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_BACKEND_H_
