// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/soda_proto_mojom_conversion.h"

#include <string>
#include <utility>

#include <base/logging.h>
#include <base/time/time.h>

using chromeos::machine_learning::mojom::AsrSwitchResult;
using chromeos::machine_learning::mojom::EndpointerType;
using speech::soda::chrome::SodaEndpointEvent;
using speech::soda::chrome::SodaLangIdEvent;
using speech::soda::chrome::SodaRecognitionResult;
using speech::soda::chrome::SodaResponse;

namespace ml {

std::optional<chromeos::machine_learning::mojom::SpeechRecognizerEventPtr>
SpeechRecognizerEventFromProto(const SodaResponse& soda_response) {
  // Always print the log lines.
  if (soda_response.log_lines_size() > 0) {
    for (const auto& log_line : soda_response.log_lines()) {
      LOG(ERROR) << log_line;
    }
  }

  chromeos::machine_learning::mojom::SpeechRecognizerEventPtr
      speech_recognizer_event;
  if (soda_response.soda_type() == SodaResponse::AUDIO_LEVEL) {
    auto audio_level_event = internal::AudioLevelEventFromProto(soda_response);
    speech_recognizer_event =
        chromeos::machine_learning::mojom::SpeechRecognizerEvent::NewAudioEvent(
            std::move(audio_level_event));
  } else if (soda_response.soda_type() == SodaResponse::RECOGNITION) {
    const auto& rec_result = soda_response.recognition_result();
    if (rec_result.result_type() == SodaRecognitionResult::PARTIAL) {
      speech_recognizer_event =
          chromeos::machine_learning::mojom::SpeechRecognizerEvent::
              NewPartialResult(internal::PartialResultFromProto(soda_response));
    } else if (rec_result.result_type() == SodaRecognitionResult::FINAL) {
      speech_recognizer_event =
          chromeos::machine_learning::mojom::SpeechRecognizerEvent::
              NewFinalResult(internal::FinalResultFromProto(soda_response));
    } else if (rec_result.result_type() == SodaRecognitionResult::PREFETCH) {
      speech_recognizer_event = chromeos::machine_learning::mojom::
          SpeechRecognizerEvent::NewPartialResult(
              internal::PartialResultFromPrefetchProto(soda_response));
    } else {
      LOG(ERROR) << "Only partial/prefetch/final results are supported, not "
                 << speech::soda::chrome::SodaRecognitionResult_ResultType_Name(
                        rec_result.result_type());
    }
  } else if (soda_response.soda_type() == SodaResponse::ENDPOINT) {
    speech_recognizer_event = chromeos::machine_learning::mojom::
        SpeechRecognizerEvent::NewEndpointerEvent(
            internal::EndpointerEventFromProto(soda_response));
  } else if (soda_response.soda_type() ==
             SodaResponse::LOGS_ONLY_ARTIFICIAL_MESSAGE) {
    return std::nullopt;
  } else if (soda_response.soda_type() == SodaResponse::LANGID) {
    speech_recognizer_event =
        chromeos::machine_learning::mojom::SpeechRecognizerEvent::
            NewLangidEvent(internal::LangIdEventFromProto(soda_response));
  } else if (soda_response.soda_type() == SodaResponse::LABEL_CORRECTION) {
    speech_recognizer_event = chromeos::machine_learning::mojom::
        SpeechRecognizerEvent::NewLabelCorrectionEvent(
            internal::LabelCorrectionEventFromProto(soda_response));
  } else {
    LOG(ERROR) << "Unexpected type of soda type to convert: "
               << speech::soda::chrome::SodaResponse_SodaMessageType_Name(
                      soda_response.soda_type());
    return std::nullopt;
  }
  return speech_recognizer_event;
}

bool IsStopSodaResponse(const SodaResponse& soda_response) {
  return soda_response.soda_type() == SodaResponse::STOP;
}
bool IsStartSodaResponse(const SodaResponse& soda_response) {
  return soda_response.soda_type() == SodaResponse::START;
}

bool IsShutdownSodaResponse(const SodaResponse& soda_response) {
  return soda_response.soda_type() == SodaResponse::SHUTDOWN;
}

namespace internal {
chromeos::machine_learning::mojom::AudioLevelEventPtr AudioLevelEventFromProto(
    const SodaResponse& soda_response) {
  auto audio_level_event =
      chromeos::machine_learning::mojom::AudioLevelEvent::New();
  if (!soda_response.has_audio_level_info()) {
    LOG(DFATAL) << "Should only call this method if audio level info is set.";
    return audio_level_event;
  }
  const auto& audio_level_info = soda_response.audio_level_info();
  audio_level_event->rms = audio_level_info.rms();
  audio_level_event->audio_level = audio_level_info.audio_level();

  // TODO(robsc): add support for time here.
  return audio_level_event;
}

chromeos::machine_learning::mojom::PartialResultPtr
PartialResultFromPrefetchProto(
    const speech::soda::chrome::SodaResponse& soda_response) {
  auto partial_result = chromeos::machine_learning::mojom::PartialResult::New();
  if (!soda_response.has_recognition_result() ||
      soda_response.soda_type() != SodaResponse::RECOGNITION ||
      soda_response.recognition_result().result_type() !=
          SodaRecognitionResult::PREFETCH) {
    LOG(DFATAL) << "Should only be called when there's a prefetch result.";
  }
  for (const std::string& hyp :
       soda_response.recognition_result().hypothesis()) {
    partial_result->partial_text.push_back(hyp);
  }
  return partial_result;
}

chromeos::machine_learning::mojom::PartialResultPtr PartialResultFromProto(
    const SodaResponse& soda_response) {
  auto partial_result = chromeos::machine_learning::mojom::PartialResult::New();
  if (!soda_response.has_recognition_result() ||
      soda_response.soda_type() != SodaResponse::RECOGNITION ||
      soda_response.recognition_result().result_type() !=
          SodaRecognitionResult::PARTIAL) {
    LOG(DFATAL)
        << "Should only call when there's a partial recognition result.";
    return partial_result;
  }
  for (const std::string& hyp :
       soda_response.recognition_result().hypothesis()) {
    partial_result->partial_text.push_back(hyp);
  }
  if (soda_response.recognition_result().hypothesis_part_size() > 0) {
    partial_result->hypothesis_part.emplace();

    for (const auto& hypothesis_part :
         soda_response.recognition_result().hypothesis_part()) {
      partial_result->hypothesis_part->push_back(
          HypothesisPartInResultFromProto(hypothesis_part));
    }
  }
  if (soda_response.recognition_result().has_timing_metrics()) {
    partial_result->timing_event = TimingInfoFromTimingMetricsProto(
        soda_response.recognition_result().timing_metrics());
  }
  return partial_result;
}

chromeos::machine_learning::mojom::FinalResultPtr FinalResultFromProto(
    const SodaResponse& soda_response) {
  auto final_result = chromeos::machine_learning::mojom::FinalResult::New();
  if (!soda_response.has_recognition_result() ||
      soda_response.soda_type() != SodaResponse::RECOGNITION ||
      soda_response.recognition_result().result_type() !=
          SodaRecognitionResult::FINAL) {
    LOG(DFATAL) << "Should only call when there's a final recognition result.";
    return final_result;
  }
  for (const std::string& hyp :
       soda_response.recognition_result().hypothesis()) {
    final_result->final_hypotheses.push_back(hyp);
  }
  if (soda_response.recognition_result().hypothesis_part_size() > 0) {
    final_result->hypothesis_part.emplace();

    for (const auto& hypothesis_part :
         soda_response.recognition_result().hypothesis_part()) {
      final_result->hypothesis_part->push_back(
          HypothesisPartInResultFromProto(hypothesis_part));
    }
  }
  // TODO(robsc): Add endpoint reason when available from
  final_result->endpoint_reason =
      chromeos::machine_learning::mojom::EndpointReason::ENDPOINT_UNKNOWN;

  if (soda_response.recognition_result().has_timing_metrics()) {
    final_result->timing_event = TimingInfoFromTimingMetricsProto(
        soda_response.recognition_result().timing_metrics());
  }
  return final_result;
}

chromeos::machine_learning::mojom::EndpointerEventPtr EndpointerEventFromProto(
    const SodaResponse& soda_response) {
  auto endpointer_event =
      chromeos::machine_learning::mojom::EndpointerEvent::New();
  if (!soda_response.has_endpoint_event() ||
      soda_response.soda_type() != SodaResponse::ENDPOINT) {
    LOG(DFATAL) << "Shouldn't have been called without an endpoint event.";
    return endpointer_event;
  }
  const auto& soda_endpoint_event = soda_response.endpoint_event();
  // Set the type, we don't have the timing right here.
  switch (soda_endpoint_event.endpoint_type()) {
    case SodaEndpointEvent::START_OF_SPEECH:
      endpointer_event->endpointer_type = EndpointerType::START_OF_SPEECH;
      break;
    case SodaEndpointEvent::END_OF_SPEECH:
      endpointer_event->endpointer_type = EndpointerType::END_OF_SPEECH;
      break;
    case SodaEndpointEvent::END_OF_AUDIO:
      endpointer_event->endpointer_type = EndpointerType::END_OF_AUDIO;
      break;
    case SodaEndpointEvent::END_OF_UTTERANCE:
      endpointer_event->endpointer_type = EndpointerType::END_OF_UTTERANCE;
      break;
    default:
      LOG(DFATAL) << "Unknown endpointer type.";
      endpointer_event->endpointer_type = EndpointerType::END_OF_UTTERANCE;
      break;
  }
  if (soda_response.recognition_result().has_timing_metrics()) {
    endpointer_event->timing_event = TimingInfoFromTimingMetricsProto(
        soda_response.recognition_result().timing_metrics());
  }
  return endpointer_event;
}

chromeos::machine_learning::mojom::LangIdEventPtr LangIdEventFromProto(
    const speech::soda::chrome::SodaResponse& soda_response) {
  auto langid_event = chromeos::machine_learning::mojom::LangIdEvent::New();
  CHECK_EQ(soda_response.soda_type(), SodaResponse::LANGID);
  const auto& langid_event_proto = soda_response.langid_event();
  langid_event->language = langid_event_proto.language();
  langid_event->confidence_level = langid_event_proto.confidence_level();
  switch (langid_event_proto.asr_switch_result()) {
    case SodaLangIdEvent::DEFAULT_NO_SWITCH:
      langid_event->asr_switch_result = AsrSwitchResult::DEFAULT_NO_SWITCH;
      break;
    case SodaLangIdEvent::SWITCH_SUCCEEDED:
      langid_event->asr_switch_result = AsrSwitchResult::SWITCH_SUCCEEDED;
      break;
    case SodaLangIdEvent::SWITCH_FAILED:
      langid_event->asr_switch_result = AsrSwitchResult::SWITCH_FAILED;
      break;
    case SodaLangIdEvent::SWITCH_SKIPPED_NO_LP:
      langid_event->asr_switch_result = AsrSwitchResult::SWITCH_SKIPPED_NO_LP;
      break;
    default:
      LOG(FATAL) << "Unknown langid asr_switch_result_type.";
  }
  return langid_event;
}

chromeos::machine_learning::mojom::LabelCorrectionEventPtr
LabelCorrectionEventFromProto(
    const speech::soda::chrome::SodaResponse& soda_response) {
  auto label_correction_event =
      chromeos::machine_learning::mojom::LabelCorrectionEvent::New();
  CHECK_EQ(soda_response.soda_type(), SodaResponse::LABEL_CORRECTION);
  for (const auto& hypothesis_part :
       soda_response.label_correction_event().hypothesis_parts()) {
    label_correction_event->hypothesis_parts.push_back(
        HypothesisPartInResultFromProto(hypothesis_part));
  }
  return label_correction_event;
}

chromeos::machine_learning::mojom::HypothesisPartInResultPtr
HypothesisPartInResultFromProto(
    const speech::soda::chrome::HypothesisPart& hypothesis_part) {
  auto part_in_result =
      chromeos::machine_learning::mojom::HypothesisPartInResult::New();
  for (const std::string& part : hypothesis_part.text()) {
    part_in_result->text.push_back(part);
  }
  part_in_result->alignment =
      base::Milliseconds(hypothesis_part.alignment_ms());
  if (hypothesis_part.has_leading_space()) {
    part_in_result->leading_space = hypothesis_part.leading_space();
  }
  part_in_result->speaker_change = hypothesis_part.speaker_change();
  if (hypothesis_part.has_speaker_label()) {
    part_in_result->speaker_label = hypothesis_part.speaker_label();
  }
  return part_in_result;
}

chromeos::machine_learning::mojom::TimingInfoPtr
TimingInfoFromTimingMetricsProto(
    const speech::soda::chrome::TimingMetrics& timing_metric) {
  auto timing_info = chromeos::machine_learning::mojom::TimingInfo::New();
  if (timing_metric.has_audio_start_epoch_usec()) {
    timing_info->audio_start_epoch = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(timing_metric.audio_start_epoch_usec()));
  }
  if (timing_metric.has_audio_start_time_usec()) {
    timing_info->audio_start_time =
        base::Microseconds(timing_metric.audio_start_time_usec());
  }
  if (timing_metric.has_elapsed_wall_time_usec()) {
    timing_info->elapsed_wall_time =
        base::Microseconds(timing_metric.elapsed_wall_time_usec());
  }
  if (timing_metric.has_event_end_time_usec()) {
    timing_info->event_end_time =
        base::Microseconds(timing_metric.event_end_time_usec());
  }
  return timing_info;
}

}  // namespace internal
}  // namespace ml
