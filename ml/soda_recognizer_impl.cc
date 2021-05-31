// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/soda_recognizer_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <base/memory/free_deleter.h>
#include <base/optional.h>
#include <base/strings/string_util.h>
#include "chrome/knowledge/soda/extended_soda_api.pb.h"
#include "ml/request_metrics.h"
#include "ml/soda.h"
#include "ml/soda_proto_mojom_conversion.h"

namespace ml {
namespace {

using ::chromeos::machine_learning::mojom::EndpointReason;
using ::chromeos::machine_learning::mojom::FinalResult;
using ::chromeos::machine_learning::mojom::FinalResultPtr;
using ::chromeos::machine_learning::mojom::OptionalBool;
using ::chromeos::machine_learning::mojom::SodaClient;
using ::chromeos::machine_learning::mojom::SodaConfigPtr;
using ::chromeos::machine_learning::mojom::SodaRecognizer;
using ::chromeos::machine_learning::mojom::SpeechRecognizerEvent;
using ::chromeos::machine_learning::mojom::SpeechRecognizerEventPtr;
using ::speech::soda::chrome::SodaResponse;

constexpr char kSodaLibraryName[] = "libsoda.so";
constexpr char kDlcBasePath[] = "/run/imageloader";

void SodaCallback(const char* soda_response_str,
                  int size,
                  void* soda_recognizer_impl) {
  SodaResponse response;
  if (!response.ParseFromArray(soda_response_str, size)) {
    LOG(ERROR) << "Parse SODA response failed." << std::endl;
    return;
  }
  reinterpret_cast<SodaRecognizerImpl*>(soda_recognizer_impl)
      ->OnSodaEvent(response.SerializeAsString());
}

bool IsDlcFilePath(const base::FilePath& path) {
  return base::StartsWith(path.value(), kDlcBasePath);
}

// Gives resolved path using realpath(3), or empty Optional upon error. Leaves
// realpath's errno unchanged.
base::Optional<base::FilePath> RealPath(const base::FilePath& path) {
  const std::unique_ptr<char, base::FreeDeleter> result(
      realpath(path.value().c_str(), nullptr));
  if (!result) {
    return {};
  }
  return base::FilePath(result.get());
}

}  // namespace

bool SodaRecognizerImpl::Create(
    SodaConfigPtr spec,
    mojo::PendingRemote<SodaClient> soda_client,
    mojo::PendingReceiver<SodaRecognizer> soda_recognizer) {
  auto recognizer_impl = new SodaRecognizerImpl(
      std::move(spec), std::move(soda_client), std::move(soda_recognizer));
  // Set the disconnection handler to strongly bind `recognizer_impl` to delete
  // `recognizer_impl` when the connection is gone.
  recognizer_impl->receiver_.set_disconnect_handler(base::Bind(
      [](const SodaRecognizerImpl* const recognizer_impl) {
        delete recognizer_impl;
      },
      base::Unretained(recognizer_impl)));
  return recognizer_impl->successfully_loaded_;
}

void SodaRecognizerImpl::AddAudio(const std::vector<uint8_t>& audio) {
  DCHECK(soda_library_->GetStatus() == ml::SodaLibrary::Status::kOk);
  soda_library_->ExtendedAddAudio(recognizer_, audio);
}

void SodaRecognizerImpl::Stop() {
  DCHECK(soda_library_->GetStatus() == ml::SodaLibrary::Status::kOk);
  soda_library_->ExtendedSodaStop(recognizer_);
}

void SodaRecognizerImpl::Start() {
  DCHECK(soda_library_->GetStatus() == ml::SodaLibrary::Status::kOk);
  soda_library_->ExtendedSodaStart(recognizer_);
}

void SodaRecognizerImpl::MarkDone() {
  DCHECK(soda_library_->GetStatus() == ml::SodaLibrary::Status::kOk);
  soda_library_->ExtendedSodaMarkDone(recognizer_);
}

void SodaRecognizerImpl::OnSodaEvent(const std::string& response_str) {
  SodaResponse response;
  response.ParseFromString(response_str);
  if (IsStartSodaResponse(response)) {
    client_remote_->OnStart();
  } else if (IsStopSodaResponse(response)) {
    client_remote_->OnStop();
  } else if (IsShutdownSodaResponse(response)) {
    // Shutdowns are ignored for now.
  } else {
    client_remote_->OnSpeechRecognizerEvent(
        SpeechRecognizerEventFromProto(response));
  }
}

SodaRecognizerImpl::SodaRecognizerImpl(
    SodaConfigPtr spec,
    mojo::PendingRemote<SodaClient> soda_client,
    mojo::PendingReceiver<SodaRecognizer> soda_recognizer)
    : successfully_loaded_(false),
      receiver_(this, std::move(soda_recognizer)),
      client_remote_(std::move(soda_client)) {
  const base::Optional<base::FilePath> real_library_dlc_path =
      RealPath(base::FilePath(spec->library_dlc_path));
  if (!real_library_dlc_path) {
    PLOG(ERROR) << "Bad library path " << spec->library_dlc_path;
    return;
  }
  if (!IsDlcFilePath(*real_library_dlc_path)) {
    LOG(DFATAL) << "Non DLC library path " << *real_library_dlc_path;
    return;
  }

  const base::Optional<base::FilePath> real_language_dlc_path =
      RealPath(base::FilePath(spec->language_dlc_path));
  if (!real_language_dlc_path) {
    PLOG(ERROR) << "Bad language path " << spec->language_dlc_path;
    return;
  }
  if (!IsDlcFilePath(*real_language_dlc_path)) {
    LOG(DFATAL) << "Non DLC language path " << *real_language_dlc_path;
    return;
  }

  soda_library_ = ml::SodaLibrary::GetInstanceAt(
      real_library_dlc_path->Append(kSodaLibraryName));
  if (soda_library_->GetStatus() != ml::SodaLibrary::Status::kOk) {
    LOG(ERROR) << "Soda library initialization failed";
    return;
  }

  speech::soda::chrome::ExtendedSodaConfigMsg cfg_msg;
  cfg_msg.set_channel_count(spec->channel_count);
  cfg_msg.set_sample_rate(spec->sample_rate);
  cfg_msg.set_language_pack_directory(real_language_dlc_path->value());
  cfg_msg.set_api_key(spec->api_key);

  if (spec->enable_formatting != OptionalBool::kUnknown) {
    cfg_msg.set_enable_formatting(spec->enable_formatting ==
                                  OptionalBool::kTrue);
  }
  std::string serialized = cfg_msg.SerializeAsString();

  ExtendedSodaConfig cfg;
  cfg.soda_config = serialized.c_str();
  cfg.soda_config_size = static_cast<int>(serialized.size());
  cfg.callback = &SodaCallback;
  cfg.callback_handle = this;

  recognizer_ = soda_library_->CreateExtendedSodaAsync(cfg);

  successfully_loaded_ = (recognizer_ != nullptr);
}

SodaRecognizerImpl::~SodaRecognizerImpl() {
  soda_library_->DeleteExtendedSodaAsync(recognizer_);
}

}  // namespace ml
