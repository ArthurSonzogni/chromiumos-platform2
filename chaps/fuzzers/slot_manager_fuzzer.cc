// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuzzer/FuzzedDataProvider.h>

#include <base/logging.h>
#include <base/test/task_environment.h>
#include <trunks/fuzzed_command_transceiver.h>
#include <trunks/trunks_factory_impl.h>
#include <vector>
#include <base/command_line.h>
#include <base/test/test_timeouts.h>

#include "chaps/chaps_interface.h"
#include "chaps/fuzzers/fuzzed_chaps_factory.h"
#include "chaps/fuzzers/fuzzed_object_pool.h"
#include "chaps/fuzzers/fuzzed_tpm_manager_utility.h"
#include "chaps/session.h"
#include "chaps/slot_manager_impl.h"
#include "chaps/token_manager_interface.h"
#include "chaps/tpm2_utility_impl.h"
#include "chaps/tpm_thread_utility_impl.h"

namespace {
enum class SlotManagerRequest {
  kInit,
  kGetSlotCount,
  kIsTokenAccessible,
  kIsTokenPresent,
  kGetSlotInfo,
  kGetTokenInfo,
  kGetMechanismInfo,
  kOpenSession,
  kCloseSession,
  kCloseAllSessions,
  kGetSession,
  kMaxValue = kGetSession,
};

enum class TokenManagerInterfaceRequest {
  kOpenIsolate,
  kCloseIsolate,
  kLoadToken,
  kUnloadToken,
  kChangeTokenAuthData,
  kGetTokenPath,
  kMaxValue = kGetTokenPath,
};

// An arbitrary choice that provides satisfactory coverage
constexpr size_t kMaxTpmMessageLength = 2048;
constexpr int kSuccessProbability = 90;
// Provide max iterations for a single fuzz run, otherwise it might timeout.
constexpr int kMaxIterations = 100;

class SlotManagerFuzzer {
 public:
  explicit SlotManagerFuzzer(FuzzedDataProvider* tpm_data_provider,
                             FuzzedDataProvider* data_provider)
      : data_provider_(data_provider) {
    factory_ = std::make_unique<chaps::FuzzedChapsFactory>(data_provider_);
    command_transceiver_ = std::make_unique<trunks::FuzzedCommandTransceiver>(
        tpm_data_provider, kMaxTpmMessageLength);
    trunks_factory_ =
        std::make_unique<trunks::TrunksFactoryImpl>(command_transceiver_.get());
    if (!trunks_factory_->Initialize()) {
      LOG(ERROR) << "Failed to initialize TrunksFactory.";
    }
    tpm_manager_utility_ =
        std::make_unique<chaps::FuzzedTpmManagerUtility>(tpm_data_provider);
    auto tpm_utility =
        std::make_unique<chaps::TPM2UtilityImpl>(trunks_factory_.get());
    tpm_utility->set_tpm_manager_utility_for_testing(
        tpm_manager_utility_.get());
    tpm_utility_ =
        std::make_unique<chaps::TPMThreadUtilityImpl>(std::move(tpm_utility));

    bool auto_load_system_token = data_provider_->ConsumeBool();
    slot_manager_ = std::make_unique<chaps::SlotManagerImpl>(
        factory_.get(), tpm_utility_.get(), auto_load_system_token, nullptr);
  }

  ~SlotManagerFuzzer() {
    slot_manager_.reset();
    tpm_utility_.reset();
    factory_.reset();
  }

  void Run() {
    int rounds = 0;
    while (data_provider_->remaining_bytes() > 0 && rounds < kMaxIterations) {
      if (data_provider_->ConsumeBool()) {
        FuzzSlotManagerRequest();
      } else {
        FuzzTokenManagerInterfaceRequest();
      }
      task_environment_.RunUntilIdle();
      rounds++;
    }
  }

 private:
  bool IsTokenPresent(const brillo::SecureBlob& isolate_credential,
                      int slot_id) {
    return slot_id < slot_manager_->GetSlotCount() &&
           slot_manager_->IsTokenAccessible(isolate_credential, slot_id) &&
           slot_manager_->IsTokenPresent(isolate_credential, slot_id);
  }

  void FuzzSlotManagerRequest() {
    auto request = data_provider_->ConsumeEnum<SlotManagerRequest>();
    brillo::SecureBlob isolate_credential;
    int slot_id;

    LOG(INFO) << "slot manager request: " << static_cast<int>(request);
    if (!ConsumeProbability(kSuccessProbability) ||
        generated_isolate_credentials_.empty()) {
      isolate_credential =
          brillo::SecureBlob(ConsumeLowEntropyRandomLengthString(16));
    } else {
      auto idx = data_provider_->ConsumeIntegralInRange(
          0ul, generated_isolate_credentials_.size() - 1);
      isolate_credential =
          brillo::SecureBlob(generated_isolate_credentials_[idx]);
    }
    if (!ConsumeProbability(kSuccessProbability) ||
        generated_slot_ids_.empty()) {
      slot_id = data_provider_->ConsumeIntegral<int>();
    } else {
      auto idx = data_provider_->ConsumeIntegralInRange(
          0ul, generated_slot_ids_.size() - 1);
      slot_id = generated_slot_ids_[idx];
    }

    switch (request) {
      case SlotManagerRequest::kInit: {
        slot_manager_->Init();
        break;
      }
      case SlotManagerRequest::kGetSlotCount: {
        slot_manager_->GetSlotCount();
        break;
      }
      case SlotManagerRequest::kIsTokenAccessible: {
        slot_id < slot_manager_->GetSlotCount() &&
            slot_manager_->IsTokenAccessible(isolate_credential, slot_id);
        break;
      }
      case SlotManagerRequest::kIsTokenPresent: {
        IsTokenPresent(isolate_credential, slot_id);
        break;
      }
      case SlotManagerRequest::kGetSlotInfo: {
        CK_SLOT_INFO slot_info;
        if (IsTokenPresent(isolate_credential, slot_id))
          slot_manager_->GetSlotInfo(isolate_credential, slot_id, &slot_info);
        break;
      }
      case SlotManagerRequest::kGetTokenInfo: {
        CK_TOKEN_INFO token_info;
        if (IsTokenPresent(isolate_credential, slot_id))
          slot_manager_->GetTokenInfo(isolate_credential, slot_id, &token_info);
        break;
      }
      case SlotManagerRequest::kGetMechanismInfo: {
        if (IsTokenPresent(isolate_credential, slot_id))
          slot_manager_->GetMechanismInfo(isolate_credential, slot_id);
        break;
      }
      case SlotManagerRequest::kOpenSession: {
        if (IsTokenPresent(isolate_credential, slot_id))
          slot_manager_->OpenSession(isolate_credential, slot_id,
                                     data_provider_->ConsumeBool());
        break;
      }
      case SlotManagerRequest::kCloseSession: {
        slot_manager_->CloseSession(isolate_credential, slot_id);
        break;
      }
      case SlotManagerRequest::kCloseAllSessions: {
        if (slot_id < slot_manager_->GetSlotCount() &&
            slot_manager_->IsTokenAccessible(isolate_credential, slot_id)) {
          slot_manager_->CloseAllSessions(isolate_credential, slot_id);
        }
        break;
      }
      case SlotManagerRequest::kGetSession: {
        chaps::Session* session = nullptr;
        slot_manager_->GetSession(isolate_credential, slot_id, &session);
        break;
      }
    }
  }

  void FuzzTokenManagerInterfaceRequest() {
    auto request = data_provider_->ConsumeEnum<TokenManagerInterfaceRequest>();
    brillo::SecureBlob isolate_credential;

    LOG(INFO) << "token manager request: " << static_cast<int>(request);
    if (data_provider_->ConsumeBool() ||
        generated_isolate_credentials_.empty()) {
      isolate_credential =
          brillo::SecureBlob(ConsumeLowEntropyRandomLengthString(16));
    } else {
      auto idx = data_provider_->ConsumeIntegralInRange(
          0ul, generated_isolate_credentials_.size() - 1);
      isolate_credential =
          brillo::SecureBlob(generated_isolate_credentials_[idx]);
    }

    switch (request) {
      case TokenManagerInterfaceRequest::kOpenIsolate: {
        bool new_isolate_created;

        if (slot_manager_->OpenIsolate(&isolate_credential,
                                       &new_isolate_created) &&
            new_isolate_created) {
          generated_isolate_credentials_.push_back(
              isolate_credential.to_string());
        }
        break;
      }
      case TokenManagerInterfaceRequest::kCloseIsolate: {
        slot_manager_->CloseIsolate(isolate_credential);
        break;
      }
      case TokenManagerInterfaceRequest::kLoadToken: {
        auto path = base::FilePath(ConsumeLowEntropyRandomLengthString(10));
        auto auth_data =
            brillo::SecureBlob(ConsumeLowEntropyRandomLengthString(10));
        std::string label = ConsumeLowEntropyRandomLengthString(10);
        int slot_id;
        if (slot_manager_->LoadToken(isolate_credential, path, auth_data, label,
                                     &slot_id)) {
          generated_slot_ids_.push_back(slot_id);
        }

        break;
      }
      case TokenManagerInterfaceRequest::kUnloadToken: {
        auto path = base::FilePath(ConsumeLowEntropyRandomLengthString(10));
        slot_manager_->UnloadToken(isolate_credential, path);
        break;
      }
      case TokenManagerInterfaceRequest::kChangeTokenAuthData: {
        auto path = base::FilePath(ConsumeLowEntropyRandomLengthString(10));
        auto old_auth_data =
            brillo::SecureBlob(ConsumeLowEntropyRandomLengthString(10));
        auto new_auth_data =
            brillo::SecureBlob(ConsumeLowEntropyRandomLengthString(10));
        slot_manager_->ChangeTokenAuthData(path, old_auth_data, new_auth_data);
        break;
      }
      case TokenManagerInterfaceRequest::kGetTokenPath: {
        base::FilePath path;
        int slot_id = data_provider_->ConsumeIntegral<int>();
        slot_manager_->GetTokenPath(isolate_credential, slot_id, &path);
        break;
      }
    }
  }

  bool ConsumeProbability(uint32_t probability) {
    return data_provider_->ConsumeIntegralInRange<uint32_t>(0, 9) * 10 <
           probability;
  }

  std::string ConsumeLowEntropyRandomLengthString(int len) {
    return std::string(
               data_provider_->ConsumeIntegralInRange<size_t>(0, len - 1),
               '0') +
           data_provider_->ConsumeBytesAsString(1);
  }

  FuzzedDataProvider* data_provider_;
  std::unique_ptr<chaps::SlotManagerImpl> slot_manager_;
  std::unique_ptr<chaps::FuzzedChapsFactory> factory_;
  std::unique_ptr<trunks::TrunksFactoryImpl> trunks_factory_;
  std::unique_ptr<chaps::FuzzedTpmManagerUtility> tpm_manager_utility_;
  std::unique_ptr<chaps::TPMThreadUtilityImpl> tpm_utility_;
  std::unique_ptr<trunks::FuzzedCommandTransceiver> command_transceiver_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::vector<std::string> generated_isolate_credentials_;
  std::vector<int> generated_slot_ids_;
};

}  // namespace

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOG_FATAL);
    base::CommandLine::Init(0, nullptr);
    TestTimeouts::Initialize();
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  if (size <= 1) {
    return 0;
  }
  size_t tpm_data_size = size / 2;
  FuzzedDataProvider tpm_data_provider(data, tpm_data_size),
      data_provider(data + tpm_data_size, size - tpm_data_size);

  SlotManagerFuzzer fuzzer(&tpm_data_provider, &data_provider);
  fuzzer.Run();
  return 0;
}
