// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/soda_recognizer_impl.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <base/memory/free_deleter.h>
#include <brillo/message_loops/message_loop.h>

#include "base/debug/leak_annotations.h"
#include "chrome/knowledge/soda/extended_soda_api.pb.h"
#include "libsoda/soda_async_impl.h"
#include "ml/soda.h"
#include "ml/soda_proto_mojom_conversion.h"
#include "ml/util.h"

namespace ml {
namespace {

using ::chromeos::machine_learning::mojom::EndpointReason;
using ::chromeos::machine_learning::mojom::FinalResult;
using ::chromeos::machine_learning::mojom::FinalResultPtr;
using ::chromeos::machine_learning::mojom::OptionalBool;
using ::chromeos::machine_learning::mojom::SodaClient;
using ::chromeos::machine_learning::mojom::SodaConfigPtr;
using ::chromeos::machine_learning::mojom::SodaRecognitionMode;
using ::chromeos::machine_learning::mojom::SodaRecognizer;
using ::chromeos::machine_learning::mojom::SpeakerDiarizationMode;
using ::chromeos::machine_learning::mojom::SpeechRecognizerEvent;
using ::chromeos::machine_learning::mojom::SpeechRecognizerEventPtr;
using ::speech::soda::chrome::ExtendedSodaConfigMsg;
using ::speech::soda::chrome::SodaResponse;

constexpr char kSodaLibraryName[] = "libsoda.so";

void SodaCallback(const char* soda_response_str,
                  int size,
                  void* soda_recognizer_impl) {
  SodaResponse response;
  if (!response.ParseFromArray(soda_response_str, size)) {
    LOG(ERROR) << "Parse SODA response failed." << std::endl;
    return;
  }
  // These will only be included when include_logging() in the initial request
  // was set to true, but we don't need to gate printing on that.
  for (const auto& log_line : response.log_lines()) {
    LOG(ERROR) << log_line;
  }

  reinterpret_cast<SodaRecognizerImpl*>(soda_recognizer_impl)
      ->OnSodaEvent(response.SerializeAsString());
}

}  // namespace

bool SodaRecognizerImpl::Create(
    SodaConfigPtr spec,
    mojo::PendingRemote<SodaClient> soda_client,
    mojo::PendingReceiver<SodaRecognizer> soda_recognizer) {
  auto recognizer_impl = new SodaRecognizerImpl(
      std::move(spec), std::move(soda_client), std::move(soda_recognizer));

  // In production, `recognizer_impl` is intentionally leaked, because this
  // model runs in its own process and the model's memory is freed when the
  // process exits. However, if being tested with ASAN, this memory leak could
  // cause an error. Therefore, we annotate it as an intentional leak.
  ANNOTATE_LEAKING_OBJECT_PTR(recognizer_impl);

  //  Set the disconnection handler to quit the message loop (i.e. exit the
  //  process) when the connection is gone, because this model is always run in
  //  a dedicated process.
  recognizer_impl->receiver_.set_disconnect_handler(
      base::BindOnce([]() { brillo::MessageLoop::current()->BreakLoop(); }));
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
    auto recognizer_event = SpeechRecognizerEventFromProto(response);
    if (recognizer_event != std::nullopt) {
      client_remote_->OnSpeechRecognizerEvent(std::move(*recognizer_event));
    }
  }
}

SodaRecognizerImpl::SodaRecognizerImpl(
    SodaConfigPtr spec,
    mojo::PendingRemote<SodaClient> soda_client,
    mojo::PendingReceiver<SodaRecognizer> soda_recognizer)
    : successfully_loaded_(false),
      receiver_(this, std::move(soda_recognizer)),
      client_remote_(std::move(soda_client)) {
  const std::optional<base::FilePath> real_library_dlc_path =
      ValidateAndGetRealDlcPath(base::FilePath(spec->library_dlc_path));
  if (!real_library_dlc_path) {
    PLOG(ERROR) << "Bad library path " << spec->library_dlc_path;
    return;
  }

  const std::optional<base::FilePath> real_language_dlc_path =
      ValidateAndGetRealDlcPath(base::FilePath(spec->language_dlc_path));
  if (!real_language_dlc_path) {
    PLOG(ERROR) << "Bad language path " << spec->language_dlc_path;
    return;
  }

  soda_library_ = ml::SodaLibrary::GetInstanceAt(
      real_library_dlc_path->Append(kSodaLibraryName));
  if (soda_library_->GetStatus() != ml::SodaLibrary::Status::kOk) {
    LOG(ERROR) << "Soda library initialization failed";
    return;
  }

  ExtendedSodaConfigMsg cfg_msg;
  cfg_msg.set_channel_count(spec->channel_count);
  cfg_msg.set_sample_rate(spec->sample_rate);
  cfg_msg.set_language_pack_directory(real_language_dlc_path->value());
  cfg_msg.set_api_key(spec->api_key);
  cfg_msg.set_include_logging(spec->include_logging_output);

  if (spec->enable_formatting != OptionalBool::kUnknown) {
    cfg_msg.set_enable_formatting(spec->enable_formatting ==
                                  OptionalBool::kTrue);
  }
  if (spec->recognition_mode == SodaRecognitionMode::kCaption) {
    cfg_msg.set_recognition_mode(ExtendedSodaConfigMsg::CAPTION);
  } else if (spec->recognition_mode == SodaRecognitionMode::kIme) {
    cfg_msg.set_recognition_mode(ExtendedSodaConfigMsg::IME);
  } else {
    LOG(DFATAL)
        << "Unknown enum type for recognition mode, setting CAPTION default.";
    cfg_msg.set_recognition_mode(ExtendedSodaConfigMsg::CAPTION);
  }

  cfg_msg.set_mask_offensive_words(spec->mask_offensive_words);
  if (spec->speaker_change_detection) {
    if (spec->speaker_diarization_mode !=
        SpeakerDiarizationMode::kSpeakerDiarizationModeOffDefault) {
      LOG(DFATAL) << "speaker_change_detection and speaker_diarization_mode "
                     "both set, ignoring speaker_change_detection";
    } else {
      cfg_msg.set_speaker_diarization_mode(
          ExtendedSodaConfigMsg::SPEAKER_CHANGE_DETECTION);
    }
  }

  if (spec->speaker_diarization_mode ==
      SpeakerDiarizationMode::kSpeakerChangeDetection) {
    cfg_msg.set_speaker_diarization_mode(
        ExtendedSodaConfigMsg::SPEAKER_CHANGE_DETECTION);
  } else if (spec->speaker_diarization_mode ==
             SpeakerDiarizationMode::kSpeakerLabelDetection) {
    cfg_msg.set_speaker_diarization_mode(
        ExtendedSodaConfigMsg::SPEAKER_LABEL_DETECTION);
    cfg_msg.set_max_speaker_count(spec->max_speaker_count);
  }

  if (spec->multi_lang_config) {
    auto multi_lang_config_mojo = *(spec->multi_lang_config);
    auto multi_lang_cfg_proto = cfg_msg.mutable_multilang_config();
    multi_lang_cfg_proto->set_rewind_when_switching_language(
        multi_lang_config_mojo.rewind_when_switching_language);
    auto& directory =
        *(multi_lang_cfg_proto->mutable_multilang_language_pack_directory());
    for (auto& mojo_map_item :
         multi_lang_config_mojo.locale_to_language_pack_map) {
      directory[mojo_map_item.first] = mojo_map_item.second;
    }
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
