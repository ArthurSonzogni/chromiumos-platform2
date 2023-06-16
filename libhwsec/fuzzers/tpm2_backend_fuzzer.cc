// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <tuple>

#include <base/command_line.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <gmock/gmock.h>
#include <libcrossystem/crossystem.h>
#include <libcrossystem/crossystem_fake.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>
#include <trunks/fuzzed_command_transceiver.h>
#include <trunks/trunks_dbus_proxy.h>
#include <trunks/trunks_factory_impl.h>

#include "libhwsec/backend/mock_backend.h"
#include "libhwsec/backend/tpm2/backend.h"
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

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace hwsec {
namespace {

constexpr int kMaxCommandCount = 10;

class Tpm2BackendFuzzerProxy : public Proxy {
 public:
  explicit Tpm2BackendFuzzerProxy(FuzzedDataProvider& data_provider)
      : data_provider_(data_provider),
        command_transceiver_(&data_provider_, 2048),
        trunks_factory_(&command_transceiver_),
        crossystem_(std::make_unique<crossystem::fake::CrossystemFake>()) {
    if (!trunks_factory_.Initialize()) {
      LOG(ERROR) << "Failed to initialize TrunksFactory.";
    }

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
    ON_CALL(tpm_nvram_, ListSpaces(_, _, _, _)).WillByDefault(fuzzed_result);
    ON_CALL(tpm_nvram_, GetSpaceInfo(_, _, _, _)).WillByDefault(fuzzed_result);

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
  trunks::TrunksFactoryImpl trunks_factory_;
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
  auto backend = std::make_unique<BackendTpm2>(proxy, MiddlewareDerivative{});
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

}  // namespace
}  // namespace hwsec

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  base::CommandLine::Init(0, nullptr);
  // Suppress log spam from the code-under-test.
  logging::SetMinLogLevel(logging::LOGGING_FATAL);

  FuzzedDataProvider data_provider(data, size);
  hwsec::FuzzMain(data_provider);
  return 0;
}
