// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tlcl_wrapper/mock_tlcl_wrapper.h"

#include <memory>
#include <utility>

#include <brillo/secure_blob.h>
#include <openssl/sha.h>
#include <vboot/tlcl.h>

#include "libhwsec-foundation/tlcl_wrapper/fake_tlcl_wrapper.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace hwsec_foundation {

MockTlclWrapper::MockTlclWrapper(
    std::unique_ptr<FakeTlclWrapper> fake_tlcl_wrapper)
    : fake_tlcl_wrapper_(std::move(fake_tlcl_wrapper)) {
  ON_CALL(*this, Init())
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::Init));
  ON_CALL(*this, Close())
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::Close));
  ON_CALL(*this, Extend(_, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::Extend));
  ON_CALL(*this, GetOwnership(_))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::GetOwnership));
  ON_CALL(*this, GetRandom(_, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::GetRandom));
  ON_CALL(*this, DefineSpace(_, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::DefineSpace));

  ON_CALL(*this, DefineSpaceEx(_, _, _, _, _, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::DefineSpaceEx));

  ON_CALL(*this, GetPermissions(_, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::GetPermissions));

  ON_CALL(*this, GetSpaceInfo(_, _, _, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::GetSpaceInfo));

  ON_CALL(*this, Write(_, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::Write));

  ON_CALL(*this, Read(_, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::Read));

  ON_CALL(*this, WriteLock(_))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::WriteLock));

  ON_CALL(*this, ReadLock(_))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::ReadLock));

  ON_CALL(*this, PCRRead(_, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::PCRRead));

  ON_CALL(*this, InitNvAuthPolicy(_, _, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::InitNvAuthPolicy));

  ON_CALL(*this, GetVersion(_, _, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::GetVersion));

  ON_CALL(*this, IFXFieldUpgradeInfo(_))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::IFXFieldUpgradeInfo));

#if !USE_TPM2

  ON_CALL(*this, ReadPubek(_, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::ReadPubek));

  ON_CALL(*this, TakeOwnership(_, _, _))
      .WillByDefault(Invoke(GetFake(), &FakeTlclWrapper::TakeOwnership));

  ON_CALL(*this, CreateDelegationFamily(_))
      .WillByDefault(
          Invoke(GetFake(), &FakeTlclWrapper::CreateDelegationFamily));

  ON_CALL(*this, ReadDelegationFamilyTable(_, _))
      .WillByDefault(
          Invoke(GetFake(), &FakeTlclWrapper::ReadDelegationFamilyTable));

#endif  // !USE_TPM2
}

}  // namespace hwsec_foundation
