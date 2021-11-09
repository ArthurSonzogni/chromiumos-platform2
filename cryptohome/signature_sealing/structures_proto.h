// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SIGNATURE_SEALING_STRUCTURES_PROTO_H_
#define CRYPTOHOME_SIGNATURE_SEALING_STRUCTURES_PROTO_H_

#include "cryptohome/key.pb.h"
#include "cryptohome/signature_sealed_data.pb.h"
#include "cryptohome/signature_sealing/structures.h"

namespace cryptohome {
namespace proto {

ChallengeSignatureAlgorithm ToProto(structure::ChallengeSignatureAlgorithm obj);
structure::ChallengeSignatureAlgorithm FromProto(
    ChallengeSignatureAlgorithm obj);

SignatureSealedData ToProto(const structure::SignatureSealedData& obj);
structure::SignatureSealedData FromProto(const SignatureSealedData& obj);

}  // namespace proto
}  // namespace cryptohome

#endif  // CRYPTOHOME_SIGNATURE_SEALING_STRUCTURES_PROTO_H_
