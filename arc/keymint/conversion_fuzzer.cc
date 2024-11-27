// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <utility>
#include <vector>

#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <keymaster/android_keymaster.h>
#include <mojo/keymint.mojom.h>

#include "arc/keymint/conversion.h"
#include "arc/keymint/keymint_logger.h"

constexpr uint32_t kSharedSecretParamVectorSize = 32;

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

std::vector<arc::mojom::keymint::KeyParameterPtr> consumeKeyParameters(
    FuzzedDataProvider* fdp) {
  uint8_t size = fdp->ConsumeIntegral<uint8_t>();
  std::vector<arc::mojom::keymint::KeyParameterPtr> params(size);

  for (size_t i = 0; i < size; ++i) {
    arc::mojom::keymint::Tag tag;
    arc::mojom::keymint::KeyParameterValuePtr param;

    switch (fdp->ConsumeIntegralInRange<uint8_t>(0, 5)) {
      case 0:
        tag = static_cast<arc::mojom::keymint::Tag>(KM_TAG_CALLER_NONCE);
        param = arc::mojom::keymint::KeyParameterValue::NewBoolValue(
            fdp->ConsumeBool());
        break;
      case 1:
        tag = arc::mojom::keymint::Tag::KEY_SIZE;
        param = arc::mojom::keymint::KeyParameterValue::NewInteger(
            fdp->ConsumeIntegral<uint32_t>());
        break;
      case 2:
        tag = static_cast<arc::mojom::keymint::Tag>(KM_TAG_RSA_PUBLIC_EXPONENT);
        param = arc::mojom::keymint::KeyParameterValue::NewLongInteger(
            fdp->ConsumeIntegral<uint64_t>());
        break;
      case 3:
        tag = static_cast<arc::mojom::keymint::Tag>(KM_TAG_ACTIVE_DATETIME);
        param = arc::mojom::keymint::KeyParameterValue::NewDateTime(
            fdp->ConsumeIntegral<uint64_t>());
        break;
      case 4:
        tag = static_cast<arc::mojom::keymint::Tag>(KM_TAG_APPLICATION_DATA);
        param = arc::mojom::keymint::KeyParameterValue::NewBlob(
            fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()));
        break;
      case 5:
        tag = arc::mojom::keymint::Tag::ALGORITHM;
        param = arc::mojom::keymint::KeyParameterValue::NewAlgorithm(
            fdp->ConsumeEnum<arc::mojom::keymint::Algorithm>());
    }

    params[i] = arc::mojom::keymint::KeyParameter::New(tag, std::move(param));
  }

  return params;
}

arc::mojom::keymint::AttestationKeyPtr consumeAttestationKey(
    FuzzedDataProvider* fdp) {
  return arc::mojom::keymint::AttestationKey::New(
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      consumeKeyParameters(fdp),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()));
}

arc::mojom::keymint::HardwareAuthTokenPtr consumeHardwareAuthToken(
    FuzzedDataProvider* fdp) {
  return arc::mojom::keymint::HardwareAuthToken::New(
      fdp->ConsumeIntegral<uint64_t>(), fdp->ConsumeIntegral<uint64_t>(),
      fdp->ConsumeIntegral<uint64_t>(),
      fdp->ConsumeEnum<arc::mojom::keymint::HardwareAuthenticatorType>(),
      arc::mojom::keymint::Timestamp::New(fdp->ConsumeIntegral<uint64_t>()),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()));
}

arc::mojom::keymint::TimeStampTokenPtr consumeTimeStampToken(
    FuzzedDataProvider* fdp) {
  return arc::mojom::keymint::TimeStampToken::New(
      fdp->ConsumeIntegral<uint64_t>(),
      arc::mojom::keymint::Timestamp::New(fdp->ConsumeIntegral<uint64_t>()),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()));
}

arc::mojom::keymint::SharedSecretParametersPtr consumeSharedSecretParameters(
    FuzzedDataProvider* fdp) {
  std::vector<uint8_t> seed =
      fdp->ConsumeBytes<uint8_t>(kSharedSecretParamVectorSize);
  std::vector<uint8_t> nonce =
      fdp->ConsumeBytes<uint8_t>(kSharedSecretParamVectorSize);

  // Resize in case there weren't enough bytes.
  seed.resize(kSharedSecretParamVectorSize);
  nonce.resize(kSharedSecretParamVectorSize);

  return arc::mojom::keymint::SharedSecretParameters::New(seed, nonce);
}

void fuzzGetKeyCharacteristics(FuzzedDataProvider* fdp) {
  auto input = arc::mojom::keymint::GetKeyCharacteristicsRequest::New(
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()));

  arc::keymint::MakeGetKeyCharacteristicsRequest(
      input, fdp->ConsumeIntegral<uint8_t>());
}

void fuzzGenerateKey(FuzzedDataProvider* fdp) {
  auto input = arc::mojom::keymint::GenerateKeyRequest::New(
      consumeKeyParameters(fdp), consumeAttestationKey(fdp));
  arc::keymint::MakeGenerateKeyRequest(input, fdp->ConsumeIntegral<uint8_t>());
}

void fuzzImportKey(FuzzedDataProvider* fdp) {
  auto input = arc::mojom::keymint::ImportKeyRequest::New(
      consumeKeyParameters(fdp),
      static_cast<arc::mojom::keymint::KeyFormat>(
          fdp->ConsumeIntegral<uint32_t>()),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      consumeAttestationKey(fdp));

  arc::keymint::MakeImportKeyRequest(input, fdp->ConsumeIntegral<uint8_t>());
}

void fuzzImportWrappedKey(FuzzedDataProvider* fdp) {
  auto input = arc::mojom::keymint::ImportWrappedKeyRequest::New(
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      consumeKeyParameters(fdp), fdp->ConsumeIntegral<uint64_t>(),
      fdp->ConsumeIntegral<uint64_t>());

  arc::keymint::MakeImportWrappedKeyRequest(input,
                                            fdp->ConsumeIntegral<uint8_t>());
}

void fuzzUpgradeKeyRequest(FuzzedDataProvider* fdp) {
  auto input = arc::mojom::keymint::UpgradeKeyRequest::New(
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      consumeKeyParameters(fdp));

  arc::keymint::MakeUpgradeKeyRequest(input, fdp->ConsumeIntegral<uint8_t>());
}

void fuzzUpdateOperation(FuzzedDataProvider* fdp) {
  auto input = arc::mojom::keymint::UpdateRequest::New(
      fdp->ConsumeIntegral<uint64_t>(),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      consumeHardwareAuthToken(fdp), consumeTimeStampToken(fdp));

  arc::keymint::MakeUpdateOperationRequest(input,
                                           fdp->ConsumeIntegral<uint8_t>());
}

void fuzzUpdateAadOperation(FuzzedDataProvider* fdp) {
  auto input = arc::mojom::keymint::UpdateRequest::New(
      fdp->ConsumeIntegral<uint64_t>(),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      consumeHardwareAuthToken(fdp), consumeTimeStampToken(fdp));

  arc::keymint::MakeUpdateAadOperationRequest(input,
                                              fdp->ConsumeIntegral<uint8_t>());
}

void fuzzBeginOperation(FuzzedDataProvider* fdp) {
  auto input = arc::mojom::keymint::BeginRequest::New(
      static_cast<arc::mojom::keymint::KeyPurpose>(
          fdp->ConsumeIntegral<uint32_t>()),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      consumeKeyParameters(fdp), consumeHardwareAuthToken(fdp));

  arc::keymint::MakeBeginOperationRequest(input,
                                          fdp->ConsumeIntegral<uint8_t>());
}

void fuzzMakeDeviceLocked(FuzzedDataProvider* fdp) {
  arc::keymint::MakeDeviceLockedRequest(fdp->ConsumeBool(),
                                        consumeTimeStampToken(fdp),
                                        fdp->ConsumeIntegral<uint8_t>());
}

void fuzzFinishOperation(FuzzedDataProvider* fdp) {
  auto input = arc::mojom::keymint::FinishRequest::New(
      fdp->ConsumeIntegral<uint64_t>(),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()),
      consumeHardwareAuthToken(fdp), consumeTimeStampToken(fdp),
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>()));

  arc::keymint::MakeFinishOperationRequest(input,
                                           fdp->ConsumeIntegral<uint8_t>());
}

void fuzzMakeComputeSharedSecret(FuzzedDataProvider* fdp) {
  std::vector<arc::mojom::keymint::SharedSecretParametersPtr> input;
  input.push_back(consumeSharedSecretParameters(fdp));
  input.push_back(consumeSharedSecretParameters(fdp));
  input.push_back(consumeSharedSecretParameters(fdp));

  arc::keymint::MakeComputeSharedSecretRequest(input,
                                               fdp->ConsumeIntegral<uint8_t>());
}

void fuzzMakeGenerateCsr(FuzzedDataProvider* fdp) {
  std::vector<arc::mojom::keymint::KeyMintBlobPtr> blobs;
  blobs.push_back(arc::mojom::keymint::KeyMintBlob::New(
      fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>())));
  auto input = arc::mojom::keymint::CertificateRequest::New(
      fdp->ConsumeBool(), std::move(blobs),
      arc::mojom::keymint::KeyMintBlob::New(
          fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>())),
      arc::mojom::keymint::KeyMintBlob::New(
          fdp->ConsumeBytes<uint8_t>(fdp->ConsumeIntegral<uint8_t>())));

  arc::keymint::MakeGenerateCsrRequest(input, fdp->ConsumeIntegral<uint8_t>());
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FuzzedDataProvider fdp(data, size);

  while (fdp.remaining_bytes()) {
    switch (fdp.ConsumeIntegralInRange<uint8_t>(0, 11)) {
      case 0:
        fuzzGetKeyCharacteristics(&fdp);
        break;
      case 1:
        fuzzGenerateKey(&fdp);
        break;
      case 2:
        fuzzImportKey(&fdp);
        break;
      case 3:
        fuzzImportWrappedKey(&fdp);
        break;
      case 4:
        fuzzUpgradeKeyRequest(&fdp);
        break;
      case 5:
        fuzzUpdateOperation(&fdp);
        break;
      case 6:
        fuzzUpdateAadOperation(&fdp);
        break;
      case 7:
        fuzzBeginOperation(&fdp);
        break;
      case 8:
        fuzzMakeDeviceLocked(&fdp);
        break;
      case 9:
        fuzzFinishOperation(&fdp);
        break;
      case 10:
        fuzzMakeComputeSharedSecret(&fdp);
        break;
      case 11:
        fuzzMakeGenerateCsr(&fdp);
        break;
    }
  }

  return 0;
}
