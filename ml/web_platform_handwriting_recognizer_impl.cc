// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/web_platform_handwriting_recognizer_impl.h"

#include <utility>
#include <vector>

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "chrome/knowledge/handwriting/handwriting_interface.pb.h"
#include "ml/handwriting.h"
#include "ml/mojom/handwriting_recognizer.mojom.h"
#include "ml/mojom/web_platform_handwriting.mojom.h"
#include "ml/request_metrics.h"
#include "ml/web_platform_handwriting_proto_mojom_conversion.h"

namespace ml {

namespace {

using ::chromeos::machine_learning::mojom::HandwritingRecognizerResult;
using ::chromeos::machine_learning::web_platform::mojom::
    HandwritingModelConstraintPtr;
using ::chromeos::machine_learning::web_platform::mojom::
    HandwritingPredictionPtr;
using ::chromeos::machine_learning::web_platform::mojom::HandwritingRecognizer;

}  // namespace

bool WebPlatformHandwritingRecognizerImpl::Create(
    HandwritingModelConstraintPtr constraint,
    mojo::PendingReceiver<HandwritingRecognizer> receiver) {
  auto recognizer_impl = new WebPlatformHandwritingRecognizerImpl(
      std::move(constraint), std::move(receiver));

  // Set the disconnection handler to strongly bind `recognizer_impl` to delete
  // `recognizer_impl` when the connection is gone.
  recognizer_impl->receiver_.set_disconnect_handler(base::Bind(
      [](const WebPlatformHandwritingRecognizerImpl* const recognizer_impl) {
        delete recognizer_impl;
      },
      base::Unretained(recognizer_impl)));

  return recognizer_impl->successfully_loaded_;
}

WebPlatformHandwritingRecognizerImpl::WebPlatformHandwritingRecognizerImpl(
    HandwritingModelConstraintPtr constraint,
    mojo::PendingReceiver<HandwritingRecognizer> receiver)
    : library_(ml::HandwritingLibrary::GetInstance()),
      receiver_(this, std::move(receiver)) {
  DCHECK(library_->GetStatus() == ml::HandwritingLibrary::Status::kOk)
      << "WebPlatformHandwritingRecognizerImpl should be created only if "
         "HandwritingLibrary is initialized successfully.";

  recognizer_ = library_->CreateHandwritingRecognizer();

  successfully_loaded_ = library_->LoadHandwritingRecognizer(
      recognizer_, constraint->languages.front());
}

WebPlatformHandwritingRecognizerImpl::~WebPlatformHandwritingRecognizerImpl() {
  library_->DestroyHandwritingRecognizer(recognizer_);
}

void WebPlatformHandwritingRecognizerImpl::GetPrediction(
    std::vector<
        chromeos::machine_learning::web_platform::mojom::HandwritingStrokePtr>
        strokes,
    chromeos::machine_learning::web_platform::mojom::HandwritingHintsPtr hints,
    GetPredictionCallback callback) {
  RequestMetrics request_metrics("WebPlatformHandwritingModel",
                                 "GetPrediction");
  request_metrics.StartRecordingPerformanceMetrics();

  chrome_knowledge::HandwritingRecognizerResult result_proto;

  if (library_->RecognizeHandwriting(
          recognizer_,
          WebPlatformHandwritingStrokesAndHintsToProto(strokes, hints),
          &result_proto)) {
    // Recognition succeeded, run callback on the result.
    auto predictions =
        WebPlatformHandwritingPredictionsFromProto(strokes, result_proto);
    if (predictions.has_value()) {
      std::move(callback).Run(std::move(predictions));
      request_metrics.FinishRecordingPerformanceMetrics();
      request_metrics.RecordRequestEvent(
          HandwritingRecognizerResult::Status::OK);
      return;
    }
  }
  // Recognition failed, run callback on empty result.
  std::move(callback).Run(base::nullopt);
  request_metrics.RecordRequestEvent(
      HandwritingRecognizerResult::Status::ERROR);
}

}  // namespace ml
