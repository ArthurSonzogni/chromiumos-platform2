// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_io/prepare_output.h"

#include <optional>

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libstorage/platform/mock_platform.h>

#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/crypto.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {
namespace {

using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

}  // namespace

TEST(PrepareOutputTest, PrepareOutputToProtoEmpty) {
  PrepareOutput prepare_output;
  user_data_auth::PrepareOutput proto = PrepareOutputToProto(prepare_output);
  EXPECT_THAT(proto.has_cryptohome_recovery_output(), IsFalse());
}

TEST(PrepareOutputTest, PrepareOutputToProtoMinimalRecovery) {
  PrepareOutput prepare_output;
  prepare_output.cryptohome_recovery_prepare_output.emplace();
  user_data_auth::PrepareOutput proto = PrepareOutputToProto(prepare_output);
  EXPECT_THAT(proto.has_cryptohome_recovery_output(), IsTrue());
  EXPECT_THAT(proto.cryptohome_recovery_output().recovery_request(), IsEmpty());
}

TEST(PrepareOutputTest, PrepareOutputToProtoPopulatedRecovery) {
  PrepareOutput prepare_output;
  prepare_output.cryptohome_recovery_prepare_output.emplace();
  prepare_output.cryptohome_recovery_prepare_output->recovery_rpc_request
      .set_protocol_version(1);
  prepare_output.cryptohome_recovery_prepare_output->ephemeral_pub_key =
      brillo::BlobFromString("something");
  user_data_auth::PrepareOutput proto = PrepareOutputToProto(prepare_output);
  EXPECT_THAT(proto.has_cryptohome_recovery_output(), IsTrue());
  EXPECT_THAT(proto.cryptohome_recovery_output().recovery_request(),
              Not(IsEmpty()));
}

}  // namespace cryptohome
