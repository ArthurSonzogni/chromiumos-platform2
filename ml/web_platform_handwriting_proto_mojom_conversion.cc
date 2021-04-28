// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/web_platform_handwriting_proto_mojom_conversion.h"

#include <base/time/time.h>
#include <utility>
#include <vector>

namespace ml {
namespace {

using chromeos::machine_learning::web_platform::mojom::
    HandwritingDrawingSegment;
using chromeos::machine_learning::web_platform::mojom::HandwritingHintsPtr;
using chromeos::machine_learning::web_platform::mojom::HandwritingPrediction;
using chromeos::machine_learning::web_platform::mojom::HandwritingPredictionPtr;
using chromeos::machine_learning::web_platform::mojom::HandwritingSegment;
using chromeos::machine_learning::web_platform::mojom::HandwritingStrokePtr;

// Used to avoid overflow in the following calculation (see
// https://crbug.com/1203736).
constexpr size_t kMaxInkRangeEndPoint = 1000000;

}  // namespace

chrome_knowledge::HandwritingRecognizerRequest
WebPlatformHandwritingStrokesAndHintsToProto(
    const std::vector<HandwritingStrokePtr>& strokes,
    const HandwritingHintsPtr& hints) {
  chrome_knowledge::HandwritingRecognizerRequest request;
  // Simple fields.
  // Always return segmentation result.
  request.set_return_segmentation(true);
  request.set_max_num_results(hints->alternatives);

  // Always set the pre_context.
  // TODO(https://crbug.com/1196204): we should change `hints->text_context`
  // to be optional too.
  auto& context_proto = *request.mutable_context();
  context_proto.set_pre_context(hints->text_context);
  // The web platform does not support writing guide for the moment.

  // For ink;
  for (const auto& stroke : strokes) {
    auto& stroke_proto = *request.mutable_ink()->add_strokes();
    for (const auto& point : stroke->points) {
      auto& point_proto = *stroke_proto.add_points();
      point_proto.set_x(point->location->x);
      point_proto.set_y(point->location->y);
      if (point->t.has_value()) {
        point_proto.set_t(point->t->InMilliseconds());
      }
    }
  }
  return request;
}

base::Optional<std::vector<
    chromeos::machine_learning::web_platform::mojom::HandwritingPredictionPtr>>
WebPlatformHandwritingPredictionsFromProto(
    const std::vector<HandwritingStrokePtr>& strokes,
    const chrome_knowledge::HandwritingRecognizerResult& result_proto) {
  std::vector<HandwritingPredictionPtr> result;

  // For candidates.
  if (!result_proto.candidates().empty()) {
    for (const auto& candidate_proto : result_proto.candidates()) {
      auto prediction = HandwritingPrediction::New();
      prediction->text = candidate_proto.text();
      // Ignores the score because the Web platform API does not support it.

      // Each candidate_proto contains a segmentation, which contains a list of
      // segments.
      if (candidate_proto.has_segmentation()) {
        // Denotes the starting index of the grapheme in the whole recognized
        // text (i.e. prediction->text).
        int grapheme_begin_index = 0;
        for (const auto& segment_proto :
             candidate_proto.segmentation().segments()) {
          auto segment = HandwritingSegment::New();
          // For sublabel.
          segment->grapheme = segment_proto.sublabel();
          // Update the index (currently, we only support English so we do not
          // need to consider the variate length of unicode codepoints).
          segment->begin_index = grapheme_begin_index;
          grapheme_begin_index += segment_proto.sublabel().length();
          segment->end_index = grapheme_begin_index;
          // For ink range.
          for (const auto& ink_range_proto : segment_proto.ink_ranges()) {
            // Mainly to avoid overflow when plus 1 to it in the below.
            if (ink_range_proto.end_point() > kMaxInkRangeEndPoint) {
              return base::nullopt;
            }
            // `ink_range->end_stroke` has to be smaller than `strokes.size()`.
            // This check is important because otherwise, the code
            // `strokes[stroke_idx]` below may crash.
            if (ink_range_proto.end_stroke() >= strokes.size()) {
              return base::nullopt;
            }
            for (unsigned int stroke_idx = ink_range_proto.start_stroke();
                 stroke_idx <= ink_range_proto.end_stroke(); ++stroke_idx) {
              auto draw_seg = HandwritingDrawingSegment::New();
              draw_seg->stroke_index = stroke_idx;
              draw_seg->begin_point_index =
                  (stroke_idx == ink_range_proto.start_stroke())
                      ? ink_range_proto.start_point()
                      : 0;
              draw_seg->end_point_index =
                  (stroke_idx == ink_range_proto.end_stroke())
                      ? ink_range_proto.end_point() + 1
                      : strokes[stroke_idx]->points.size();
              segment->drawing_segments.push_back(std::move(draw_seg));
            }
          }
          prediction->segmentation_result.push_back(std::move(segment));
        }
      }
      result.push_back(std::move(prediction));
    }
  }
  return result;
}

}  // namespace ml
