// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>
#include <memory>
#include <string>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/daemons/daemon.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <gmock/gmock.h>
#include <google/protobuf/descriptor.h>
#include <libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h>
#include <metrics/metrics_library_mock.h>
#include <trunks/fuzzed_command_transceiver.h>
#include <user_data_auth-client-test/user_data_auth/dbus-proxy-mocks.h>

#include "u2fd/fuzzers/fuzzed_allowlisting_util_factory.h"
#include "u2fd/fuzzers/fuzzed_user_state.h"
#include "u2fd/fuzzers/webauthn_fuzzer_data.pb.h"
#include "u2fd/webauthn_handler.h"

namespace {

using ::brillo::dbus_utils::MockDBusMethodResponse;

using testing::_;
using testing::Return;
using testing::Unused;

using google::protobuf::Descriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::Reflection;

constexpr char kStorageRootPath[] = "/tmp/webauthn_fuzzer";
const std::string kCredentialSecret('E', 64);
constexpr size_t kMaxTpmMessageLength = 512;

class WebAuthnFuzzer : public brillo::Daemon {
 public:
  explicit WebAuthnFuzzer(const u2f::WebAuthnFuzzerData& input)
      : data_provider_(
            reinterpret_cast<const uint8_t*>(input.fuzzed_data().c_str()),
            input.fuzzed_data().size()),
        fuzzed_requests_(input.requests()) {
    fuzzed_requests_iter_ = fuzzed_requests_.begin();
  }

  WebAuthnFuzzer(const WebAuthnFuzzer&) = delete;
  WebAuthnFuzzer& operator=(const WebAuthnFuzzer&) = delete;

  ~WebAuthnFuzzer() override = default;

 protected:
  int OnInit() override {
    int exit_code = brillo::Daemon::OnInit();
    if (exit_code != EX_OK) {
      return exit_code;
    }

    Init();
    ScheduleFuzzDbusApi();

    return EX_OK;
  }

 private:
  void Init() {
    PrepareMockBus();

    handler_ = std::make_unique<u2f::WebAuthnHandler>();

    PrepareMockCryptohome();

    tpm_proxy_ = std::make_unique<u2f::TpmVendorCommandProxy>(
        std::make_unique<trunks::FuzzedCommandTransceiver>(
            &data_provider_, kMaxTpmMessageLength));

    user_state_ = std::make_unique<u2f::FuzzedUserState>(&data_provider_);

    u2f::U2fMode u2f_mode{data_provider_.ConsumeIntegral<uint8_t>()};

    std::function<void()> request_presence = []() {
      // do nothing
    };

    allowlisting_util_factory_ =
        std::make_unique<u2f::FuzzedAllowlistingUtilFactory>(&data_provider_);
    auto allowlisting_util =
        allowlisting_util_factory_->CreateAllowlistingUtil();

    PrepareStorage();

    handler_->Initialize(mock_bus_.get(), tpm_proxy_.get(), user_state_.get(),
                         u2f_mode, request_presence,
                         std::move(allowlisting_util), &mock_metrics_);
  }

  void ScheduleFuzzDbusApi() {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&WebAuthnFuzzer::FuzzDbusApi, base::Unretained(this)));
  }

  void FuzzDbusApi() {
    if (fuzzed_requests_iter_ == fuzzed_requests_.end()) {
      Quit();
      return;
    }

    const u2f::WebAuthnFuzzerData::Request& request = *fuzzed_requests_iter_;
    if (request.has_make_credential_request()) {
      handler_->MakeCredential(
          std::make_unique<
              MockDBusMethodResponse<u2f::MakeCredentialResponse>>(),
          request.make_credential_request());
    } else if (request.has_get_assertion_request()) {
      handler_->GetAssertion(
          std::make_unique<MockDBusMethodResponse<u2f::GetAssertionResponse>>(),
          request.get_assertion_request());
    } else if (request.has_has_credentials_request()) {
      handler_->HasCredentials(request.has_credentials_request());
    } else if (request.has_has_legacy_credentials_request()) {
      handler_->HasLegacyCredentials(request.has_legacy_credentials_request());
    } else if (request.has_cancel_web_authn_flow_request()) {
      handler_->Cancel(request.cancel_web_authn_flow_request());
    } else if (request.has_is_uvpaa_request()) {
      handler_->IsUvpaa(
          std::make_unique<MockDBusMethodResponse<u2f::IsUvpaaResponse>>(),
          request.is_uvpaa_request());
    } else if (request.has_is_u2f_enabled_request()) {
      handler_->IsU2fEnabled(request.is_u2f_enabled_request());
    }

    ++fuzzed_requests_iter_;
    ScheduleFuzzDbusApi();
  }

  void PrepareMockBus() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = new testing::StrictMock<dbus::MockBus>(options);

    mock_auth_dialog_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), chromeos::kUserAuthenticationServiceName,
        dbus::ObjectPath(chromeos::kUserAuthenticationServicePath));

    EXPECT_CALL(*mock_bus_,
                GetObjectProxy(
                    chromeos::kUserAuthenticationServiceName,
                    dbus::ObjectPath(chromeos::kUserAuthenticationServicePath)))
        .WillRepeatedly(Return(mock_auth_dialog_proxy_.get()));

    EXPECT_CALL(*mock_auth_dialog_proxy_, DoCallMethod(_, _, _))
        .WillRepeatedly(
            [this](Unused, Unused,
                   base::OnceCallback<void(dbus::Response*)>* callback) {
              this->GenerateMockAuthDialogResponse();
              std::move(*callback).Run(mock_auth_dialog_response_.get());
            });

    EXPECT_CALL(*mock_auth_dialog_proxy_, CallMethodAndBlock(_, _))
        .WillRepeatedly([this](Unused, Unused) {
          GenerateMockAuthDialogResponse();
          return std::move(mock_auth_dialog_response_);
        });
  }

  void GenerateMockAuthDialogResponse() {
    this->mock_auth_dialog_response_ = dbus::Response::CreateEmpty();
    if (this->data_provider_.ConsumeBool()) {
      dbus::MessageWriter writer(mock_auth_dialog_response_.get());
      writer.AppendBool(this->data_provider_.ConsumeBool());
    }
  }

  void PrepareMockCryptohome() {
    auto mock_cryptohome_proxy = std::make_unique<
        testing::StrictMock<org::chromium::UserDataAuthInterfaceProxyMock>>();

    // GetWebAuthnSecretAsync
    {
      get_web_authn_secret_reply.set_webauthn_secret(kCredentialSecret);
      bool success = data_provider_.ConsumeBool();
      EXPECT_CALL(*mock_cryptohome_proxy, GetWebAuthnSecretAsync)
          .WillRepeatedly([this, success](auto in_request,
                                          auto success_callback,
                                          auto error_callback, int timeout_ms) {
            if (success) {
              base::SequencedTaskRunnerHandle::Get()->PostNonNestableTask(
                  FROM_HERE,
                  base::BindOnce(success_callback, get_web_authn_secret_reply));
            } else {
              // TODO(domen): Prevent showing the error message.
              // |brillo::Error| shows the error message regardless of the
              // min log level
              auto error = brillo::Error::Create(FROM_HERE, "", "", "");
              std::move(error_callback).Run(error.get());
            }
          });
    }

    // GetKeyData
    {
      bool has_key_data = data_provider_.ConsumeBool();
      EXPECT_CALL(*mock_cryptohome_proxy, GetKeyData)
          .WillRepeatedly([has_key_data](auto in_request, auto out_reply,
                                         brillo::ErrorPtr* error,
                                         int timeout_ms) {
            if (has_key_data)
              out_reply->add_key_data();
            return true;
          });
    }

    handler_->SetCryptohomeInterfaceProxyForTesting(
        std::move(mock_cryptohome_proxy));
  }

  void PrepareStorage() {
    if (!base::DeletePathRecursively(base::FilePath(kStorageRootPath))) {
      PLOG(FATAL) << "Failed to clear directory for WebAuthnStorage.";
    }
    auto webauthn_storage = std::make_unique<u2f::WebAuthnStorage>();
    webauthn_storage->SetRootPathForTesting(base::FilePath(kStorageRootPath));
    handler_->SetWebAuthnStorageForTesting(std::move(webauthn_storage));
  }

  std::unique_ptr<u2f::WebAuthnHandler> handler_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_auth_dialog_proxy_;
  std::unique_ptr<u2f::TpmVendorCommandProxy> tpm_proxy_;
  std::unique_ptr<dbus::Response> mock_auth_dialog_response_;
  std::unique_ptr<u2f::FuzzedUserState> user_state_;
  std::unique_ptr<u2f::FuzzedAllowlistingUtilFactory>
      allowlisting_util_factory_;
  testing::NiceMock<MetricsLibraryMock> mock_metrics_;

  user_data_auth::GetWebAuthnSecretReply get_web_authn_secret_reply;

  FuzzedDataProvider data_provider_;
  const u2f::RepeatedPtrField<u2f::WebAuthnFuzzerData::Request>&
      fuzzed_requests_;
  u2f::RepeatedPtrField<const u2f::WebAuthnFuzzerData::Request>::iterator
      fuzzed_requests_iter_;
};

bool IsProtoValidUtf8Only(const Message& message, int level = 0) {
  const Descriptor* descriptor = message.GetDescriptor();
  const Reflection* reflection = message.GetReflection();

  for (int i = 0; i < descriptor->field_count(); i++) {
    const FieldDescriptor* f = descriptor->field(i);

    switch (f->type()) {
      case FieldDescriptor::TYPE_MESSAGE: {
        if (f->is_repeated()) {  // repeated message
          int size = reflection->FieldSize(message, f);
          for (int j = 0; j < size; ++j) {
            const Message& m = reflection->GetRepeatedMessage(message, f, j);
            if (!IsProtoValidUtf8Only(m, level + 1))
              return false;
          }
        } else if (reflection->HasField(message, f)) {  // singular message
          const Message& m = reflection->GetMessage(message, f);
          if (!IsProtoValidUtf8Only(m, level + 1))
            return false;
        }
        break;
      }

      case FieldDescriptor::TYPE_STRING: {
        if (f->is_repeated()) {  // repeated string
          int size = reflection->FieldSize(message, f);
          for (int j = 0; j < size; ++j) {
            std::string s = reflection->GetRepeatedString(message, f, j);
            if (!base::IsStringUTF8(s))
              return false;
          }
        } else if (reflection->HasField(message, f)) {  // singular string
          std::string s = reflection->GetString(message, f);
          if (!base::IsStringUTF8(s))
            return false;
        }
        break;
      }

      default: {
        // We do not care about other types.
        break;
      }
    }
  }

  return true;
}

}  // namespace

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOG_FATAL); }
};

DEFINE_PROTO_FUZZER(const u2f::WebAuthnFuzzerData& input) {
  static Environment env;
  // A string in a dbus call must be valid UTF-8.
  // Although libprotobuf-mutator should provide UTF-8 strings in proto3, in
  // practice, there is a mismatch. Therefore, we check all the string fields in
  // the input proto with |base::IsStringUTF8|.
  if (!IsProtoValidUtf8Only(input)) {
    return;
  }
  WebAuthnFuzzer fuzzer(input);
  CHECK_EQ(fuzzer.Run(), EX_OK);
}
