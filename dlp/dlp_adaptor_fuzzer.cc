// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <gtest/gtest.h>
#include <libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h>

#include "dlp/dlp_adaptor.h"
#include "dlp/dlp_adaptor_test_helper.h"
#include "dlp/dlp_fuzzer.pb.h"

using testing::_;

namespace {
std::vector<uint8_t> SerializeMessageToVector(
    const google::protobuf::Message& message) {
  std::vector<uint8_t> result(message.ByteSize());
  message.SerializeToArray(result.data(), result.size());
  return result;
}
}  // namespace

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOG_FATAL);  // <- DISABLE LOGGING.
  }
};

DEFINE_PROTO_FUZZER(const dlp::DlpFuzzer& input) {
  static Environment env;

  dlp::DlpAdaptorTestHelper helper;
  dlp::DlpAdaptor* adaptor = helper.adaptor();

  // If this function isn't called, DlpAdaptor will try to initialise Fanotify
  // when the policy is set, which will cause a crash because tests don't have
  // cap_sys_admin capability.
  adaptor->SetFanotifyWatcherStartedForTesting(true);

  EXPECT_CALL(*helper.mock_session_manager_proxy(),
              CallMethodAndBlockWithErrorDetails(_, _, _))
      .WillRepeatedly(::testing::ReturnNull());

  adaptor->SetDlpFilesPolicy(
      SerializeMessageToVector(input.set_dlp_files_policy_request()));

  adaptor->AddFile(SerializeMessageToVector(input.add_file_request()));

  std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>
      response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
          std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>(nullptr);
  adaptor->RequestFileAccess(
      std::move(response),
      SerializeMessageToVector(input.request_file_access_request()));
}
