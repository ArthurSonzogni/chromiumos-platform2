// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/keymint_server.h"

#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>
#include <base/threading/platform_thread.h>
#include <keymaster/android_keymaster_messages.h>
#include <mojo/keymint.mojom.h>

#include "arc/keymint/conversion.h"

// The implementations of |arc::mojom::KeyMintServer| methods below have the
// following overall pattern:
//
// * Generate an std::unique_ptr to a KeyMint request data structure from the
//   arguments received from Mojo, usually through the helpers in conversion.h.
//
// * Execute the operation in |backend->keymint()|, posting this task to a
//   background thread. This produces a KeyMint response data structure.
//
// * Post the response to a callback that runs on the original thread (in this
//   case, the Mojo thread where the request started).
//
// * Convert the KeyMint response to the Mojo return values, and run the
//   result callback.
//
namespace arc::keymint {

namespace {

constexpr size_t kOperationTableSize = 16;
// TODO(b/278968783): Add version negotiation for KeyMint.
// KeyMint Message versions are drawn from Android
// Keymaster Messages.
constexpr int32_t kKeyMintMessageVersion = 4;

constexpr ::keymaster::KmVersion kKeyMintVersion =
    ::keymaster::KmVersion::KEYMINT_2;
}  // namespace

KeyMintServer::Backend::Backend()
    : context_(new context::ArcKeyMintContext(kKeyMintVersion)),
      keymint_(context_, kOperationTableSize, kKeyMintMessageVersion) {}

KeyMintServer::Backend::~Backend() = default;

KeyMintServer::KeyMintServer()
    : backend_thread_("BackendKeyMintThread"), weak_ptr_factory_(this) {
  CHECK(backend_thread_.Start()) << "Failed to start keymint thread";
}

KeyMintServer::~KeyMintServer() = default;

void KeyMintServer::UpdateContextPlaceholderKeys(
    std::vector<mojom::ChromeOsKeyPtr> keys,
    base::OnceCallback<void(bool)> callback) {
  base::OnceCallback<void(bool)> callback_in_original_runner = base::BindOnce(
      [](scoped_refptr<base::TaskRunner> original_task_runner,
         base::OnceCallback<void(bool)> callback, bool success) {
        original_task_runner->PostTask(
            FROM_HERE, base::BindOnce(std::move(callback), success));
      },
      base::SingleThreadTaskRunner::GetCurrentDefault(), std::move(callback));

  backend_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](context::ArcKeyMintContext* context,
                        std::vector<mojom::ChromeOsKeyPtr> keys,
                        base::OnceCallback<void(bool)> callback) {
                       // |context| is guaranteed valid here because it's owned
                       // by |backend_|, which outlives the |backend_thread_|
                       // this runs on.
                       context->set_placeholder_keys(std::move(keys));
                       std::move(callback).Run(/*success=*/true);
                     },
                     backend_.context(), std::move(keys),
                     std::move(callback_in_original_runner)));
}

void KeyMintServer::SetSystemVersion(uint32_t android_version,
                                     uint32_t android_patchlevel) {
  auto task_lambda = [](context::ArcKeyMintContext* context,
                        uint32_t android_version, uint32_t android_patchlevel) {
    // |context| is guaranteed valid here because it's owned
    // by |backend_|, which outlives the |backend_thread_|
    // this runs on.
    context->SetSystemVersion(android_version, android_patchlevel);
  };
  backend_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(task_lambda, backend_.context(),
                                android_version, android_patchlevel));
}

template <typename KmMember, typename KmRequest, typename KmResponse>
void KeyMintServer::RunKeyMintRequest(
    const base::Location& location,
    KmMember member,
    std::unique_ptr<KmRequest> request,
    base::OnceCallback<void(std::unique_ptr<KmResponse>)> callback) {
  auto task_lambda =
      [](const base::Location& location,
         scoped_refptr<base::TaskRunner> original_task_runner,
         ::keymaster::AndroidKeymaster* keymaster, KmMember member,
         std::unique_ptr<KmRequest> request,
         base::OnceCallback<void(std::unique_ptr<KmResponse>)> callback) {
        // Prepare a KeyMint response data structure.
        auto response =
            std::make_unique<KmResponse>(keymaster->message_version());
        // Execute the operation.
        (*keymaster.*member)(*request, response.get());
        // Post |callback| to the |original_task_runner| given |response|.
        original_task_runner->PostTask(
            location, base::BindOnce(std::move(callback), std::move(response)));
      };
  // Post the KeyMint operation to a background thread while capturing the
  // current task runner.
  backend_thread_.task_runner()->PostTask(
      location,
      base::BindOnce(task_lambda, location,
                     base::SingleThreadTaskRunner::GetCurrentDefault(),
                     backend_.keymint(), member, std::move(request),
                     std::move(callback)));
}

template <typename KmMember, typename KmResponse>
void KeyMintServer::RunKeyMintRequest_EmptyInput(
    const base::Location& location,
    KmMember member,
    base::OnceCallback<void(std::unique_ptr<KmResponse>)> callback) {
  auto task_lambda =
      [](const base::Location& location,
         scoped_refptr<base::TaskRunner> original_task_runner,
         ::keymaster::AndroidKeymaster* keymaster, KmMember member,
         base::OnceCallback<void(std::unique_ptr<KmResponse>)> callback) {
        // Prepare a KeyMint response data structure.
        auto response =
            std::make_unique<KmResponse>(keymaster->message_version());
        // Execute the operation.
        auto result = (*keymaster.*member)();

        response->error = result.error;
        // Post |callback| to the |original_task_runner| given |response|.
        original_task_runner->PostTask(
            location, base::BindOnce(std::move(callback), std::move(response)));
      };
  // Post the KeyMint operation to a background thread while capturing the
  // current task runner.
  backend_thread_.task_runner()->PostTask(
      location,
      base::BindOnce(task_lambda, location,
                     base::SingleThreadTaskRunner::GetCurrentDefault(),
                     backend_.keymint(), member, std::move(callback)));
}

template <typename KmMember, typename KmRequest, typename KmResponse>
void KeyMintServer::RunKeyMintRequest_SingleInput(
    const base::Location& location,
    KmMember member,
    std::unique_ptr<KmRequest> request,
    base::OnceCallback<void(std::unique_ptr<KmResponse>)> callback) {
  auto task_lambda =
      [](const base::Location& location,
         scoped_refptr<base::TaskRunner> original_task_runner,
         ::keymaster::AndroidKeymaster* keymaster, KmMember member,
         std::unique_ptr<KmRequest> request,
         base::OnceCallback<void(std::unique_ptr<KmResponse>)> callback) {
        // Prepare a KeyMint response data structure.
        auto response =
            std::make_unique<KmResponse>(keymaster->message_version());
        // Execute the operation.
        auto result = (*keymaster.*member)(*request);
        // Post |callback| to the |original_task_runner| given |response|.

        response->error = result.error;
        original_task_runner->PostTask(
            location, base::BindOnce(std::move(callback), std::move(response)));
      };
  // Post the KeyMint operation to a background thread while capturing the
  // current task runner.
  backend_thread_.task_runner()->PostTask(
      location,
      base::BindOnce(task_lambda, location,
                     base::SingleThreadTaskRunner::GetCurrentDefault(),
                     backend_.keymint(), member, std::move(request),
                     std::move(callback)));
}

void KeyMintServer::AddRngEntropy(const std::vector<uint8_t>& data,
                                  AddRngEntropyCallback callback) {
  // Convert input |data| into |km_request|. All data is deep copied to avoid
  // use-after-free.
  auto km_request =
      std::make_unique<::keymaster::AddEntropyRequest>(kKeyMintMessageVersion);
  ConvertToKeymasterMessage(data, &km_request->random_data);

  auto task_lambda = base::BindOnce(
      [](AddRngEntropyCallback callback,
         std::unique_ptr<::keymaster::AddEntropyResponse> km_response) {
        // Run callback.
        std::move(callback).Run(km_response->error);
      },
      std::move(callback));

  // Call keymint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::AddRngEntropy,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::GetKeyCharacteristics(
    arc::mojom::keymint::GetKeyCharacteristicsRequestPtr request,
    GetKeyCharacteristicsCallback callback) {
  // Convert input |request| into |km_request|. All data is deep copied to avoid
  // use-after-free.
  auto km_request = MakeGetKeyCharacteristicsRequest(
      request, backend_.keymint()->message_version());

  auto task_lambda = base::BindOnce(
      [](GetKeyCharacteristicsCallback callback,
         std::unique_ptr<::keymaster::GetKeyCharacteristicsResponse>
             km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeGetKeyCharacteristicsResult(*km_response);
        // Run callback.
        std::move(callback).Run(std::move(response));
      },
      std::move(callback));

  // Call keymint.
  RunKeyMintRequest(FROM_HERE,
                    &::keymaster::AndroidKeymaster::GetKeyCharacteristics,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::GenerateKey(
    arc::mojom::keymint::GenerateKeyRequestPtr request,
    GenerateKeyCallback callback) {
  auto km_request = MakeGenerateKeyRequest(
      request->key_params, backend_.keymint()->message_version());

  auto task_lambda = base::BindOnce(
      [](GenerateKeyCallback callback,
         std::unique_ptr<::keymaster::GenerateKeyResponse> km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeGenerateKeyResult(*km_response);
        // Run callback.
        std::move(callback).Run(std::move(response));
      },
      std::move(callback));

  // Call keymint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::GenerateKey,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::ImportKey(arc::mojom::keymint::ImportKeyRequestPtr request,
                              ImportKeyCallback callback) {
  auto km_request =
      MakeImportKeyRequest(request, backend_.keymint()->message_version());

  auto task_lambda = base::BindOnce(
      [](ImportKeyCallback callback,
         std::unique_ptr<::keymaster::ImportKeyResponse> km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeImportKeyResult(*km_response);
        // Run callback.
        std::move(callback).Run(std::move(response));
      },
      std::move(callback));

  // Call keymint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::ImportKey,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::ImportWrappedKey(
    arc::mojom::keymint::ImportWrappedKeyRequestPtr request,
    ImportWrappedKeyCallback callback) {
  auto km_request = MakeImportWrappedKeyRequest(
      request, backend_.keymint()->message_version());

  auto task_lambda = base::BindOnce(
      [](ImportKeyCallback callback,
         std::unique_ptr<::keymaster::ImportWrappedKeyResponse> km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeImportWrappedKeyResult(*km_response);
        std::move(callback).Run(std::move(response));
      },
      std::move(callback));

  // Call keymint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::ImportWrappedKey,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::UpgradeKey(
    arc::mojom::keymint::UpgradeKeyRequestPtr request,
    UpgradeKeyCallback callback) {
  auto km_request =
      MakeUpgradeKeyRequest(request, backend_.keymint()->message_version());

  auto task_lambda = base::BindOnce(
      [](UpgradeKeyCallback callback,
         std::unique_ptr<::keymaster::UpgradeKeyResponse> km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeUpgradeKeyResult(*km_response);

        // Run callback.
        std::move(callback).Run(std::move(response));
      },
      std::move(callback));

  // Call keymint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::UpgradeKey,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::DeleteKey(const std::vector<uint8_t>& key_blob,
                              DeleteKeyCallback callback) {
  // Convert input |key_blob| into |km_request|. All data is deep copied to
  // avoid use-after-free.
  auto km_request = std::make_unique<::keymaster::DeleteKeyRequest>(
      backend_.keymint()->message_version());
  km_request->SetKeyMaterial(key_blob.data(), key_blob.size());

  auto task_lambda = base::BindOnce(
      [](DeleteKeyCallback callback,
         std::unique_ptr<::keymaster::DeleteKeyResponse> km_response) {
        // Run callback.
        CHECK(km_response);
        std::move(callback).Run(km_response->error);
      },
      std::move(callback));

  // Call keymint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::DeleteKey,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::DeleteAllKeys(DeleteAllKeysCallback callback) {
  // Prepare keymint request.
  auto km_request = std::make_unique<::keymaster::DeleteAllKeysRequest>(
      backend_.keymint()->message_version());

  auto task_lambda = base::BindOnce(
      [](DeleteAllKeysCallback callback,
         std::unique_ptr<::keymaster::DeleteAllKeysResponse> km_response) {
        // Run callback.
        CHECK(km_response);
        std::move(callback).Run(km_response->error);
      },
      std::move(callback));

  // Call keymint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::DeleteAllKeys,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::DestroyAttestationIds(
    DestroyAttestationIdsCallback callback) {
  // Implement this when needed.
  std::move(callback).Run(KM_ERROR_UNIMPLEMENTED);
}

void KeyMintServer::Begin(arc::mojom::keymint::BeginRequestPtr request,
                          BeginCallback callback) {
  // Convert input |request| into |km_request|. All data is deep copied to avoid
  // use-after-free.
  auto km_request =
      MakeBeginOperationRequest(request, backend_.keymint()->message_version());

  auto task_lambda = base::BindOnce(
      [](BeginCallback callback,
         std::unique_ptr<::keymaster::BeginOperationResponse> km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeBeginResult(*km_response);

        // Run callback.
        std::move(callback).Run(std::move(response));
      },
      std::move(callback));

  // Call KeyMint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::BeginOperation,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::DeviceLocked(
    bool password_only,
    arc::mojom::keymint::TimeStampTokenPtr timestamp_token,
    DeviceLockedCallback callback) {
  // Convert input |request| into |km_request|. All data is deep copied to avoid
  // use-after-free.
  auto km_request =
      MakeDeviceLockedRequest(password_only, std::move(timestamp_token),
                              backend_.keymint()->message_version());

  auto task_lambda = base::BindOnce(
      [](DeviceLockedCallback callback,
         std::unique_ptr<::keymaster::DeviceLockedResponse> km_response) {
        // Run callback.
        CHECK(km_response);
        std::move(callback).Run(km_response->error);
      },
      std::move(callback));

  // Call KeyMint.
  RunKeyMintRequest_SingleInput(FROM_HERE,
                                &::keymaster::AndroidKeymaster::DeviceLocked,
                                std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::EarlyBootEnded(EarlyBootEndedCallback callback) {
  auto task_lambda = base::BindOnce(
      [](EarlyBootEndedCallback callback,
         std::unique_ptr<::keymaster::EarlyBootEndedResponse> km_response) {
        // Run callback.
        CHECK(km_response);
        std::move(callback).Run(km_response->error);
      },
      std::move(callback));

  // Call KeyMint.
  RunKeyMintRequest_EmptyInput(FROM_HERE,
                               &::keymaster::AndroidKeymaster::EarlyBootEnded,
                               std::move(task_lambda));
}

void KeyMintServer::ConvertStorageKeyToEphemeral(
    const std::vector<uint8_t>& storage_key_blob,
    ConvertStorageKeyToEphemeralCallback callback) {
  // Implement this when needed.
  auto error =
      arc::mojom::keymint::ByteArrayOrError::NewError(KM_ERROR_UNIMPLEMENTED);
  std::move(callback).Run(std::move(error));
}

void KeyMintServer::GetRootOfTrustChallenge(
    GetRootOfTrustChallengeCallback callback) {
  // Implement this when needed.
  auto error =
      arc::mojom::keymint::ByteArrayOrError::NewError(KM_ERROR_UNIMPLEMENTED);
  std::move(callback).Run(std::move(error));
}

void KeyMintServer::GetRootOfTrust(const std::vector<uint8_t>& challenge,
                                   GetRootOfTrustCallback callback) {
  // Implement this when needed.
  auto error =
      arc::mojom::keymint::ByteArrayOrError::NewError(KM_ERROR_UNIMPLEMENTED);
  std::move(callback).Run(std::move(error));
}

void KeyMintServer::SendRootOfTrust(const std::vector<uint8_t>& root_of_trust,
                                    SendRootOfTrustCallback callback) {
  // Implement this when needed.
  std::move(callback).Run(KM_ERROR_UNIMPLEMENTED);
}

void KeyMintServer::UpdateAad(arc::mojom::keymint::UpdateRequestPtr request,
                              UpdateAadCallback callback) {
  // Convert input |request| into |km_request|. All data is deep copied to avoid
  // use-after-free.
  auto km_request = MakeUpdateAadOperationRequest(
      request, backend_.keymint()->message_version());

  auto task_lambda = base::BindOnce(
      [](UpdateAadCallback callback,
         std::unique_ptr<::keymaster::UpdateOperationResponse> km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeUpdateResult(*km_response);

        std::move(callback).Run(km_response->error);
      },
      std::move(callback));

  // Call KeyMint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::UpdateOperation,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::Update(arc::mojom::keymint::UpdateRequestPtr request,
                           UpdateCallback callback) {
  // Convert input |request| into |km_request|. All data is deep copied to avoid
  // use-after-free.
  auto km_request = MakeUpdateOperationRequest(
      request, backend_.keymint()->message_version());
  auto input_size = km_request->input.buffer_size();

  auto task_lambda = base::BindOnce(
      [](UpdateCallback callback, const size_t input_size,
         std::unique_ptr<::keymaster::UpdateOperationResponse> km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeUpdateResult(*km_response);

        // Run callback.
        // This logic is derived from AndroidKeyMintOperation.cpp
        if (km_response->error == KM_ERROR_OK &&
            km_response->input_consumed != input_size) {
          LOG(ERROR) << "KeyMint Server: Unknown error in Update due to size "
                        "mismatch.";
          std::move(callback).Run(
              arc::mojom::keymint::ByteArrayOrError::NewError(
                  KM_ERROR_UNKNOWN_ERROR));
        } else {
          std::move(callback).Run(std::move(response));
        }
      },
      std::move(callback), std::move(input_size));

  // Call KeyMint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::UpdateOperation,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::Finish(arc::mojom::keymint::FinishRequestPtr request,
                           FinishCallback callback) {
  // Convert input |request| into |km_request|. All data is deep copied to avoid
  // use-after-free.
  auto km_request = MakeFinishOperationRequest(
      request, backend_.keymint()->message_version());

  auto task_lambda = base::BindOnce(
      [](FinishCallback callback,
         std::unique_ptr<::keymaster::FinishOperationResponse> km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeFinishResult(*km_response);

        // Run callback.
        std::move(callback).Run(std::move(response));
      },
      std::move(callback));

  // Call KeyMint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::FinishOperation,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::Abort(uint64_t op_handle, AbortCallback callback) {
  // Prepare keymint request.
  auto km_request = std::make_unique<::keymaster::AbortOperationRequest>(
      backend_.keymint()->message_version());
  km_request->op_handle = op_handle;

  auto task_lambda = base::BindOnce(
      [](AbortCallback callback,
         std::unique_ptr<::keymaster::AbortOperationResponse> km_response) {
        // Run callback.
        CHECK(km_response);
        std::move(callback).Run(km_response->error);
      },
      std::move(callback));

  // Call keymint.
  RunKeyMintRequest(FROM_HERE, &::keymaster::AndroidKeymaster::AbortOperation,
                    std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::GetSharedSecretParameters(
    GetSharedSecretParametersCallback callback) {
  auto task_lambda = base::BindOnce(
      [](GetSharedSecretParametersCallback callback,
         std::unique_ptr<::keymaster::GetHmacSharingParametersResponse>
             km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeGetSharedSecretParametersResult(*km_response);
        // Run callback.
        std::move(callback).Run(std::move(response));
      },
      std::move(callback));

  // Call keymint.
  RunKeyMintRequest_EmptyInput(
      FROM_HERE, &::keymaster::AndroidKeymaster::GetHmacSharingParameters,
      std::move(task_lambda));
}

void KeyMintServer::ComputeSharedSecret(
    const std::vector<arc::mojom::keymint::SharedSecretParametersPtr> request,
    ComputeSharedSecretCallback callback) {
  // Convert input |request| into |km_request|. All data is deep copied to avoid
  // use-after-free.
  auto km_request = MakeComputeSharedSecretRequest(
      request, backend_.keymint()->message_version());

  // Validate the |km_request| before passing to Reference impl.
  if (km_request->params_array.params_array == nullptr) {
    auto response = arc::mojom::keymint::ByteArrayOrError::NewError(
        KM_ERROR_MEMORY_ALLOCATION_FAILED);
    std::move(callback).Run(std::move(response));
    return;
  }

  // Validate nonce size for each shared secret parameter.
  for (size_t i = 0; i < request.size(); i++) {
    if (sizeof(km_request->params_array.params_array[i].nonce) !=
        request[i]->nonce.size()) {
      auto response = arc::mojom::keymint::ByteArrayOrError::NewError(
          KM_ERROR_INVALID_ARGUMENT);
      std::move(callback).Run(std::move(response));
      return;
    }
  }

  auto task_lambda = base::BindOnce(
      [](ComputeSharedSecretCallback callback,
         std::unique_ptr<::keymaster::ComputeSharedHmacResponse> km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeComputeSharedSecretResult(*km_response);

        // Run callback.
        std::move(callback).Run(std::move(response));
      },
      std::move(callback));

  // Call KeyMint.
  RunKeyMintRequest_SingleInput(
      FROM_HERE, &::keymaster::AndroidKeymaster::ComputeSharedHmac,
      std::move(km_request), std::move(task_lambda));
}

void KeyMintServer::GenerateTimeStamp(const uint64 challenge,
                                      GenerateTimeStampCallback callback) {
  // Convert input |request| into |km_request|. All data is deep copied to avoid
  // use-after-free.
  auto km_request =
      std::make_unique<::keymaster::GenerateTimestampTokenRequest>(
          backend_.keymint()->message_version());

  km_request->challenge = challenge;

  auto task_lambda = base::BindOnce(
      [](GenerateTimeStampCallback callback,
         std::unique_ptr<::keymaster::GenerateTimestampTokenResponse>
             km_response) {
        // Prepare mojo response.
        CHECK(km_response);
        auto response = MakeGenerateTimeStampTokenResult(*km_response);

        // Run callback.
        std::move(callback).Run(std::move(response));
      },
      std::move(callback));

  // Call KeyMint.
  RunKeyMintRequest(FROM_HERE,
                    &::keymaster::AndroidKeymaster::GenerateTimestampToken,
                    std::move(km_request), std::move(task_lambda));
}

}  // namespace arc::keymint
