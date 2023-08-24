// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <tuple>

#include <base/command_line.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <gmock/gmock.h>
#include <libcrossystem/crossystem.h>
#include <libcrossystem/crossystem_fake.h>
#include <openssl/rand.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>
#include <trunks/fuzzed_command_transceiver.h>
#include <trunks/hmac_session_impl.h>
#include <trunks/password_authorization_delegate.h>
#include <trunks/policy_session_impl.h>
#include <trunks/session_manager_impl.h>
#include <trunks/tpm_state_impl.h>
#include <trunks/trunks_dbus_proxy.h>
#include <trunks/trunks_factory_impl.h>

#include "libhwsec/backend/mock_backend.h"
#include "libhwsec/backend/tpm2/backend.h"
#include "libhwsec/error/tpm_retry_action.h"
#include "libhwsec/fuzzed/attestation.h"
#include "libhwsec/fuzzed/basic_objects.h"
#include "libhwsec/fuzzed/config.h"
#include "libhwsec/fuzzed/da_mitigation.h"
#include "libhwsec/fuzzed/encryption.h"
#include "libhwsec/fuzzed/hwsec_objects.h"
#include "libhwsec/fuzzed/key_management.h"
#include "libhwsec/fuzzed/middleware.h"
#include "libhwsec/fuzzed/pinweaver.h"
#include "libhwsec/fuzzed/protobuf.h"
#include "libhwsec/fuzzed/recovery_crypto.h"
#include "libhwsec/fuzzed/sealing.h"
#include "libhwsec/fuzzed/signature_sealing.h"
#include "libhwsec/fuzzed/signing.h"
#include "libhwsec/fuzzed/storage.h"
#include "libhwsec/fuzzed/u2f.h"
#include "libhwsec/fuzzed/vendor.h"
#include "libhwsec/fuzzers/backend_command_list.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/middleware/middleware_owner.h"
#include "libhwsec/platform/mock_platform.h"
#include "libhwsec/structures/threading_mode.h"
#include "trunks/tpm_generated.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace hwsec {
namespace {

constexpr int kMaxCommandCount = 10;
constexpr uint32_t kVendorIdGsc = 0x43524f53;

template <typename Origin>
class FuzzedAuthorizationDelegate : public Origin {
 public:
  template <typename... Args>
  FuzzedAuthorizationDelegate(FuzzedDataProvider& data_provider, Args&&... args)
      : Origin(std::forward<Args>(args)...), data_provider_(data_provider) {}

  bool GetCommandAuthorization(const std::string& command_hash,
                               bool is_command_parameter_encryption_possible,
                               bool is_response_parameter_encryption_possible,
                               std::string* authorization) override {
    if (data_provider_.ConsumeBool()) {
      return data_provider_.ConsumeBool();
    }

    return Origin::GetCommandAuthorization(
        command_hash, is_command_parameter_encryption_possible,
        is_response_parameter_encryption_possible, authorization);
  }

  bool CheckResponseAuthorization(const std::string& response_hash,
                                  const std::string& authorization) override {
    if (data_provider_.ConsumeBool()) {
      return data_provider_.ConsumeBool();
    }

    return Origin::CheckResponseAuthorization(response_hash, authorization);
  }

  bool EncryptCommandParameter(std::string* parameter) override {
    if (data_provider_.ConsumeBool()) {
      return data_provider_.ConsumeBool();
    }

    return Origin::EncryptCommandParameter(parameter);
  }

  bool DecryptResponseParameter(std::string* parameter) override {
    if (data_provider_.ConsumeBool()) {
      return data_provider_.ConsumeBool();
    }

    return Origin::DecryptResponseParameter(parameter);
  }

 private:
  FuzzedDataProvider& data_provider_;
};

template <typename Origin>
class FuzzedSession : public Origin {
 public:
  template <typename... Args>
  FuzzedSession(FuzzedDataProvider& data_provider, Args&&... args)
      : Origin(std::forward<Args>(args)...),
        data_provider_(data_provider),
        delegate_(data_provider, "") {}

  trunks::TPM_RC StartBoundSession(trunks::TPMI_DH_ENTITY bind_entity,
                                   const std::string& bind_authorization_value,
                                   bool salted,
                                   bool enable_encryption) override {
    if (data_provider_.ConsumeBool()) {
      return trunks::TPM_RC_SUCCESS;
    }

    return Origin::StartBoundSession(bind_entity, bind_authorization_value,
                                     salted, enable_encryption);
  }

  trunks::TPM_RC StartUnboundSession(bool salted,
                                     bool enable_encryption) override {
    if (data_provider_.ConsumeBool()) {
      return trunks::TPM_RC_SUCCESS;
    }

    return Origin::StartUnboundSession(salted, enable_encryption);
  }

  trunks::AuthorizationDelegate* GetDelegate() override {
    if (data_provider_.ConsumeBool()) {
      return &delegate_;
    }

    return Origin::GetDelegate();
  }

 private:
  FuzzedDataProvider& data_provider_;
  FuzzedAuthorizationDelegate<trunks::PasswordAuthorizationDelegate> delegate_;
};

class FuzzedTpmState : public trunks::TpmStateImpl {
 public:
  FuzzedTpmState(FuzzedDataProvider& data_provider,
                 const trunks::TrunksFactory& factory)
      : trunks::TpmStateImpl(factory), data_provider_(data_provider) {}
  trunks::TPM_RC Initialize() override {
    if (!use_real_.has_value()) {
      use_real_ = data_provider_.ConsumeBool();
    }
    if (*use_real_) {
      return trunks::TpmStateImpl::Initialize();
    }
    return trunks::TPM_RC_SUCCESS;
  }

#define FUZZED_COMMAND(type, command)            \
  type command() override {                      \
    CHECK(use_real_.has_value());                \
    if (*use_real_) {                            \
      return trunks::TpmStateImpl::command();    \
    }                                            \
    return FuzzedObject<type>()(data_provider_); \
  }

  FUZZED_COMMAND(bool, IsOwnerPasswordSet);
  FUZZED_COMMAND(bool, IsEndorsementPasswordSet);
  FUZZED_COMMAND(bool, IsLockoutPasswordSet);
  FUZZED_COMMAND(bool, IsOwned);
  FUZZED_COMMAND(bool, IsInLockout);
  FUZZED_COMMAND(bool, IsPlatformHierarchyEnabled);
  FUZZED_COMMAND(bool, IsStorageHierarchyEnabled);
  FUZZED_COMMAND(bool, IsEndorsementHierarchyEnabled);
  FUZZED_COMMAND(bool, IsEnabled);
  FUZZED_COMMAND(bool, WasShutdownOrderly);
  FUZZED_COMMAND(bool, IsRSASupported);
  FUZZED_COMMAND(bool, IsECCSupported);
  FUZZED_COMMAND(uint32_t, GetLockoutCounter);
  FUZZED_COMMAND(uint32_t, GetLockoutThreshold);
  FUZZED_COMMAND(uint32_t, GetLockoutInterval);
  FUZZED_COMMAND(uint32_t, GetLockoutRecovery);
  FUZZED_COMMAND(uint32_t, GetMaxNVSize);
  FUZZED_COMMAND(uint32_t, GetTpmFamily);
  FUZZED_COMMAND(uint32_t, GetSpecificationLevel);
  FUZZED_COMMAND(uint32_t, GetSpecificationRevision);
  FUZZED_COMMAND(uint32_t, GetManufacturer);
  FUZZED_COMMAND(uint32_t, GetTpmModel);
  FUZZED_COMMAND(uint64_t, GetFirmwareVersion);
  FUZZED_COMMAND(std::string, GetVendorIDString);

#undef FUZZED_COMMAND

  bool GetTpmProperty(trunks::TPM_PT property, uint32_t* value) override {
    CHECK(use_real_.has_value());
    if (*use_real_) {
      return trunks::TpmStateImpl::GetTpmProperty(property, value);
    }
    if (value) {
      if (property == trunks::TPM_PT_MANUFACTURER &&
          data_provider_.ConsumeBool()) {
        *value = kVendorIdGsc;
        return true;
      }
      *value = data_provider_.ConsumeIntegral<uint32_t>();
    }
    return true;
  }

  bool GetAlgorithmProperties(trunks::TPM_ALG_ID algorithm,
                              trunks::TPMA_ALGORITHM* properties) override {
    CHECK(use_real_.has_value());
    if (*use_real_) {
      return trunks::TpmStateImpl::GetAlgorithmProperties(algorithm,
                                                          properties);
    }
    if (properties) {
      *properties = data_provider_.ConsumeIntegral<trunks::TPMA_ALGORITHM>();
    }
    return true;
  }

 private:
  FuzzedDataProvider& data_provider_;
  std::optional<bool> use_real_;
};

class FuzzedTrunksFactory : public trunks::TrunksFactoryImpl {
 public:
  FuzzedTrunksFactory(FuzzedDataProvider& data_provider,
                      trunks::CommandTransceiver* transceiver)
      : trunks::TrunksFactoryImpl(transceiver), data_provider_(data_provider) {
    CHECK(Initialize());
  }

  std::unique_ptr<trunks::TpmState> GetTpmState() const override {
    return std::make_unique<FuzzedTpmState>(data_provider_, *this);
  }

  std::unique_ptr<trunks::AuthorizationDelegate> GetPasswordAuthorization(
      const std::string& password) const override {
    return std::make_unique<
        FuzzedAuthorizationDelegate<trunks::PasswordAuthorizationDelegate>>(
        data_provider_, password);
  }

  std::unique_ptr<trunks::HmacSession> GetHmacSession() const override {
    return std::make_unique<FuzzedSession<trunks::HmacSessionImpl>>(
        data_provider_, *this);
  }

  std::unique_ptr<trunks::PolicySession> GetPolicySession() const override {
    return std::make_unique<FuzzedSession<trunks::PolicySessionImpl>>(
        data_provider_, *this, trunks::TPM_SE_POLICY);
  }

  std::unique_ptr<trunks::PolicySession> GetTrialSession() const override {
    return std::make_unique<FuzzedSession<trunks::PolicySessionImpl>>(
        data_provider_, *this, trunks::TPM_SE_TRIAL);
  }

 private:
  FuzzedDataProvider& data_provider_;
};

class Tpm2BackendFuzzerProxy : public Proxy {
 public:
  explicit Tpm2BackendFuzzerProxy(FuzzedDataProvider& data_provider)
      : data_provider_(data_provider),
        command_transceiver_(&data_provider_, 2048),
        trunks_factory_(data_provider_, &command_transceiver_),
        crossystem_(std::make_unique<crossystem::fake::CrossystemFake>()) {
    auto fuzzed_result = [this](auto&&, auto* reply, auto&&, auto&&) {
      using ReplyType = std::remove_pointer_t<decltype(reply)>;
      *reply = FuzzedObject<ReplyType>()(data_provider_);
      return FuzzedObject<bool>()(data_provider_);
    };

    ON_CALL(tpm_manager_, GetTpmNonsensitiveStatus(_, _, _, _))
        .WillByDefault(fuzzed_result);
    ON_CALL(tpm_manager_, GetTpmStatus(_, _, _, _))
        .WillByDefault(fuzzed_result);
    ON_CALL(tpm_manager_, GetVersionInfo(_, _, _, _))
        .WillByDefault(fuzzed_result);
    ON_CALL(tpm_manager_, GetSupportedFeatures(_, _, _, _))
        .WillByDefault(fuzzed_result);
    ON_CALL(tpm_manager_, GetDictionaryAttackInfo(_, _, _, _))
        .WillByDefault(fuzzed_result);
    ON_CALL(tpm_manager_, GetRoVerificationStatus(_, _, _, _))
        .WillByDefault(fuzzed_result);
    ON_CALL(tpm_manager_, ResetDictionaryAttackLock(_, _, _, _))
        .WillByDefault(fuzzed_result);
    ON_CALL(tpm_manager_, RemoveOwnerDependency(_, _, _, _))
        .WillByDefault(fuzzed_result);
    ON_CALL(tpm_manager_, ClearStoredOwnerPassword(_, _, _, _))
        .WillByDefault(fuzzed_result);
    ON_CALL(tpm_nvram_, DefineSpace(_, _, _, _)).WillByDefault(fuzzed_result);
    ON_CALL(tpm_nvram_, DestroySpace(_, _, _, _)).WillByDefault(fuzzed_result);
    ON_CALL(tpm_nvram_, WriteSpace(_, _, _, _)).WillByDefault(fuzzed_result);
    ON_CALL(tpm_nvram_, ReadSpace(_, _, _, _)).WillByDefault(fuzzed_result);
    ON_CALL(tpm_nvram_, LockSpace(_, _, _, _)).WillByDefault(fuzzed_result);
    ON_CALL(tpm_nvram_, GetSpaceInfo(_, _, _, _)).WillByDefault(fuzzed_result);
    ON_CALL(tpm_nvram_, ListSpaces(_, _, _, _))
        .WillByDefault([this](auto&&, auto* reply, auto&&, auto&&) {
          using ReplyType = std::remove_pointer_t<decltype(reply)>;

          if (data_provider_.ConsumeBool()) {
            *reply = FuzzedObject<ReplyType>()(data_provider_);
            return FuzzedObject<bool>()(data_provider_);
          }

          *reply = ReplyType();
          reply->set_result(tpm_manager::NvramResult::NVRAM_RESULT_SUCCESS);
          for (uint32_t index :
               {0x100a, 0x9da5b0, 0x800004, 0x9da5b2, 0x800006, 0x100e}) {
            if (data_provider_.ConsumeBool()) {
              reply->add_index_list(index);
            }
          }
          return true;
        });

    ON_CALL(platform_, ReadFileToString(_, _))
        .WillByDefault([this](auto&&, std::string* result) {
          if (data_provider_.ConsumeBool()) {
            *result = FuzzedObject<std::string>()(data_provider_);
            return true;
          }
          return false;
        });

    Proxy::SetTrunksCommandTransceiver(&command_transceiver_);
    Proxy::SetTrunksFactory(&trunks_factory_);
    Proxy::SetTpmManager(&tpm_manager_);
    Proxy::SetTpmNvram(&tpm_nvram_);
    Proxy::SetCrossystem(&crossystem_);
    Proxy::SetPlatform(&platform_);
  }

  ~Tpm2BackendFuzzerProxy() override = default;

 private:
  FuzzedDataProvider& data_provider_;
  trunks::FuzzedCommandTransceiver command_transceiver_;
  FuzzedTrunksFactory trunks_factory_;
  testing::NiceMock<org::chromium::TpmManagerProxyMock> tpm_manager_;
  testing::NiceMock<org::chromium::TpmNvramProxyMock> tpm_nvram_;
  crossystem::Crossystem crossystem_;
  testing::NiceMock<MockPlatform> platform_;
};

// A variable |T| that need to be initialized & destructed manually.
template <typename T>
class ManualVariable {
 public:
  T* ptr() { return reinterpret_cast<T*>(storage_); }

 private:
  alignas(T) uint8_t storage_[sizeof(T)];
};

// Call the |Command| with fuzzed arguments synchronously.
template <auto Command, typename TupleType, std::size_t... I>
void FuzzCallSync(FuzzedDataProvider& data_provider,
                  Middleware middleware,
                  std::index_sequence<I...>) {
  // According to the C++ spec, the execution order of function is unspecified.
  // We will need to generated the input parameters one by one, otherwise the
  // data_provider would be consumed with the unspecified order.
  std::tuple<ManualVariable<std::tuple_element_t<I, TupleType>>...> data;

  // Create fuzzed objects in order on the stack.
  (
      [&]() {
        using Arg = std::tuple_element_t<I, TupleType>;
        new (std::get<I>(data).ptr()) Arg(FuzzedObject<Arg>()(data_provider));
      }(),
      ...);

  middleware.CallSync<Command>(std::move(*std::get<I>(data).ptr())...).ok();

  // Cleanup fuzzed objects in reverse order on the stack.
  (
      [&]() {
        using Arg = std::tuple_element_t<sizeof...(I) - I - 1, TupleType>;
        std::get<sizeof...(I) - I - 1>(data).ptr()->~Arg();
      }(),
      ...);
}

// A helper to get the input argument types from the |Func|.
template <auto Command, typename Func, typename = void>
struct FuzzCommandHelper {
  static_assert(sizeof(Func) == -1, "Unknown member function");
};

// Fuzz command helper for the synchronous backend call.
template <auto Command, typename R, typename S, typename... Args>
struct FuzzCommandHelper<Command,
                         R (S::*)(Args...),
                         std::enable_if_t<std::is_convertible_v<Status, R>>> {
  void Fuzz(FuzzedDataProvider& data_provider, Middleware middleware) {
    FuzzCallSync<
        Command,
        std::tuple<std::remove_cv_t<std::remove_reference_t<Args>>...>>(
        data_provider, middleware, std::make_index_sequence<sizeof...(Args)>());
  }
};

// Fuzz command helper for the asynchronous backend call.
template <auto Command, typename R, typename S, typename... Args>
struct FuzzCommandHelper<Command,
                         void (S::*)(base::OnceCallback<void(R)>, Args...),
                         std::enable_if_t<std::is_convertible_v<Status, R>>> {
  void Fuzz(FuzzedDataProvider& data_provider, Middleware middleware) {
    FuzzCallSync<
        Command,
        std::tuple<std::remove_cv_t<std::remove_reference_t<Args>>...>>(
        data_provider, middleware, std::make_index_sequence<sizeof...(Args)>());
  }
};

// Call the |Command| on |middleware| with fuzzed arguments from |data_provider|
// .
template <auto Command>
void RunCommand(FuzzedDataProvider& data_provider, Middleware middleware) {
  FuzzCommandHelper<Command, decltype(Command)>().Fuzz(data_provider,
                                                       middleware);
}

// Run the nth command from the |CmdList| and index_sequence to enumerate the
// index.
template <typename CmdList, std::size_t... I>
void RunNthCommandListSeq(size_t n,
                          FuzzedDataProvider& data_provider,
                          Middleware middleware,
                          std::index_sequence<I...>) {
  // fold expression with , operator.
  (
      [&]() {
        if (n == I) {
          RunCommand<CmdList::template Get<I>()>(data_provider, middleware);
        }
      }(),
      ...);
}

// Run the nth command from the |CmdList|.
template <typename CmdList>
void RunNthCommandList(size_t n,
                       FuzzedDataProvider& data_provider,
                       Middleware middleware) {
  RunNthCommandListSeq<CmdList>(n, data_provider, middleware,
                                std::make_index_sequence<CmdList::size>());
}

void FuzzMain(FuzzedDataProvider& data_provider) {
  Tpm2BackendFuzzerProxy proxy(data_provider);
  base::ScopedTempDir tmp_dir_;
  CHECK(tmp_dir_.CreateUniqueTempDir());
  auto backend = std::make_unique<BackendTpm2>(proxy, MiddlewareDerivative{},
                                               tmp_dir_.GetPath());
  BackendTpm2* backend_ptr = backend.get();
  auto middleware_owner = std::make_unique<MiddlewareOwner>(
      std::move(backend), ThreadingMode::kCurrentThread);
  // We are testing the backend, so we should not call set_data_provider here.
  backend_ptr->set_middleware_derivative_for_test(middleware_owner->Derive());
  Middleware middleware(middleware_owner->Derive());

  int command_count = data_provider.ConsumeIntegralInRange(1, kMaxCommandCount);
  for (int i = 0; i < command_count; i++) {
    if (data_provider.remaining_bytes() == 0) {
      break;
    }
    size_t command_index = data_provider.ConsumeIntegralInRange(
        static_cast<size_t>(0), FuzzCommandList::size - 1);
    RunNthCommandList<FuzzCommandList>(command_index, data_provider,
                                       middleware);
  }
}

FuzzedDataProvider* g_data_provider;
std::independent_bits_engine<std::mt19937, CHAR_BIT, unsigned char> g_engine;

// Generating too much low entropy random data will cause the fuzzer stuck
// in the RSA keygen, we should not waste time for that kind of case.
uint32_t g_low_entropy_rand_count = 0;

enum class RandByteType {
  kQuick,
  kConsume,
  kZero,
  kOne,
  kMaxValue = kOne,
};

int FuzzRandBytes(unsigned char* buf, int num) {
  if (g_data_provider == nullptr) {
    return 0;
  }

  if (g_low_entropy_rand_count > 4096) {
    std::generate(buf, buf + num, std::ref(g_engine));
    return 1;
  }

  switch (g_data_provider->ConsumeEnum<RandByteType>()) {
    case RandByteType::kQuick: {
      std::generate(buf, buf + num, std::ref(g_engine));
      break;
    }
    case RandByteType::kConsume:
      // Reset the buffer first, because we may not have enough data in data
      // provider.
      memset(buf, 0, num);
      g_data_provider->ConsumeData(buf, num);
      break;
    case RandByteType::kZero:
      memset(buf, 0, num);
      g_low_entropy_rand_count++;
      break;
    case RandByteType::kOne:
      memset(buf, 0xff, num);
      g_low_entropy_rand_count++;
      break;
  }

  return 1;
}

int FuzzRandAdd(const void* buf, int num, double randomness) {
  return 1;
}

int FuzzRandSeed(const void* buf, int num) {
  return 1;
}

int FuzzRandStatus(void) {
  return g_data_provider != nullptr;
}

bool StaticInit() {
  static RAND_METHOD rand_method = {
      .seed = FuzzRandSeed,
      .bytes = FuzzRandBytes,
      .cleanup = nullptr,
      .add = FuzzRandAdd,
      .pseudorand = FuzzRandBytes,
      .status = FuzzRandStatus,
  };

  base::CommandLine::Init(0, nullptr);

  CHECK(RAND_set_rand_method(&rand_method));

  // Suppress log spam from the code-under-test.
  logging::SetMinLogLevel(logging::LOGGING_FATAL);
  return true;
}

[[maybe_unused]] bool static_init = StaticInit();

}  // namespace
}  // namespace hwsec

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider data_provider(data, size);
  hwsec::g_data_provider = &data_provider;
  hwsec::g_engine.seed(0);
  hwsec::g_low_entropy_rand_count = 0;

  hwsec::FuzzMain(data_provider);
  return 0;
}
