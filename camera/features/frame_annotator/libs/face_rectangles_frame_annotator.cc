/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/frame_annotator/libs/face_rectangles_frame_annotator.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/strings/stringprintf.h>
#include <skia/core/SkFont.h>
#include <skia/core/SkPath.h>
#include <skia/core/SkTypeface.h>

#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/face_detector_client_cros_wrapper.h"

namespace cros {

namespace {

constexpr SkScalar kLandmarkBoxLimit = 160;

std::string ConfidenceToString(float confidence) {
  return base::StringPrintf("%.2f", confidence);
}

SkPath BoxToTriangle(SkRect rect) {
  SkPath path;
  path.moveTo(rect.centerX(), rect.top());
  path.lineTo(rect.left(), rect.bottom());
  path.lineTo(rect.right(), rect.bottom());
  path.lineTo(rect.centerX(), rect.top());
  return path;
}

}  // namespace

//
// FaceRectanglesFrameAnnotator implementations.
//

bool FaceRectanglesFrameAnnotator::Initialize(
    const camera_metadata_t* static_info) {
  base::span<const int32_t> active_array_size = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
  DCHECK_EQ(active_array_size.size(), 4);
  active_array_dimension_ = Size(active_array_size[2], active_array_size[3]);

  auto facing = GetRoMetadata<uint8_t>(static_info, ANDROID_LENS_FACING);
  DCHECK(facing.has_value());
  facing_ = static_cast<camera_metadata_enum_android_lens_facing_t>(*facing);

  return true;
}

bool FaceRectanglesFrameAnnotator::ProcessCaptureResult(
    const Camera3CaptureDescriptor* result) {
  auto faces = result->feature_metadata().faces;
  // Get faces from FaceDetectionStreamManipulator if available, otherwise
  // read capture metadata to get face detection result from HAL.
  if (faces.has_value()) {
    cached_faces_ = std::move(*faces);
  } else {
    auto scores = result->GetMetadata<uint8_t>(ANDROID_STATISTICS_FACE_SCORES);
    size_t face_count = scores.size();
    if (face_count > 0) {
      // [..., x1_i, y1_i, x2_i, y2_i, ...], i = face index.
      auto rects =
          result->GetMetadata<int32_t>(ANDROID_STATISTICS_FACE_RECTANGLES);
      DCHECK(rects.size() == face_count * 4);
      cached_faces_ = std::vector<human_sensing::CrosFace>(face_count);
      for (size_t i = 0; i < face_count; ++i) {
        cached_faces_[i].bounding_box.x1 = static_cast<float>(rects[i * 4]);
        cached_faces_[i].bounding_box.y1 =
            static_cast<float>(rects[(i * 4) + 1]);
        cached_faces_[i].bounding_box.x2 =
            static_cast<float>(rects[(i * 4) + 2]);
        cached_faces_[i].bounding_box.y2 =
            static_cast<float>(rects[(i * 4) + 3]);
        cached_faces_[i].confidence = static_cast<float>(scores[i]) / 100.0;
      }
      // [.., left_eye_x_i, left_eye_y_i, right_eye_x_i, right_eye_y_i,
      //  mouth_center_x_i, mouth_center_y_i, ...], i = face index.
      auto landmarks =
          result->GetMetadata<int32_t>(ANDROID_STATISTICS_FACE_LANDMARKS);
      if (landmarks.size() > 0) {
        DCHECK(landmarks.size() == face_count * 6);
        for (size_t i = 0; i < face_count; i++) {
          cached_faces_[i].landmarks.push_back(human_sensing::Landmark{
              .x = static_cast<float>(landmarks[i * 6]),
              .y = static_cast<float>(landmarks[(i * 6) + 1]),
              .type = human_sensing::Landmark::Type::kLeftEye});
          cached_faces_[i].landmarks.push_back(human_sensing::Landmark{
              .x = static_cast<float>(landmarks[(i * 6) + 2]),
              .y = static_cast<float>(landmarks[(i * 6) + 3]),
              .type = human_sensing::Landmark::Type::kRightEye});
          cached_faces_[i].landmarks.push_back(human_sensing::Landmark{
              .x = static_cast<float>(landmarks[(i * 6) + 4]),
              .y = static_cast<float>(landmarks[(i * 6) + 5]),
              .type = human_sensing::Landmark::Type::kMouthCenter});
        }
      }
    }
  }
  return true;
}

bool FaceRectanglesFrameAnnotator::IsPlotNeeded() const {
  return !cached_faces_.empty() &&
         (options_.face_rectangles || options_.face_landmarks);
}

bool FaceRectanglesFrameAnnotator::Plot(SkCanvas* canvas) {
  const auto canvas_info = canvas->imageInfo();
  const SkScalar canvas_width = static_cast<float>(canvas_info.width());
  const SkScalar canvas_height = static_cast<float>(canvas_info.height());

  const float scale_ratio = canvas_height / 480;

  Rect<uint32_t> full_frame_crop = GetCenteringFullCrop(
      active_array_dimension_, canvas_info.width(), canvas_info.height());

  // Use a thinner font for better display if possible.
  auto typeface = SkTypeface::MakeFromName(
      nullptr,
      SkFontStyle(SkFontStyle::kThin_Weight, SkFontStyle::kNormal_Width,
                  SkFontStyle::Slant::kUpright_Slant));
  SkFont font(typeface, 10 * scale_ratio);

  // Annotate each faces with a white box.
  SkPaint paint;
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setAntiAlias(true);
  paint.setStrokeWidth(1);
  paint.setColor(0xffffffff);

  auto draw_confidence = [&](SkRect box, float confidence) {
    canvas->save();

    SkScalar x = box.fLeft, y = box.fTop - 5;

    if (options_.flip_type == FrameAnnotator::FlipType::kHorizontal ||
        options_.flip_type == FrameAnnotator::FlipType::kRotate180 ||
        (options_.flip_type == FrameAnnotator::FlipType::kDefault &&
         facing_ == ANDROID_LENS_FACING_FRONT)) {
      // Flip horizontally.
      canvas->scale(-1, 1);
      canvas->translate(-canvas_width, 0);
      x += box.width();
      x = canvas_width - x;
    }
    if (options_.flip_type == FrameAnnotator::FlipType::kVertical ||
        options_.flip_type == FrameAnnotator::FlipType::kRotate180) {
      // Flip vertically.
      canvas->scale(1, -1);
      canvas->translate(0, -canvas_height);
      y += box.height() + 10;
      y = canvas_height - y;
    }

    canvas->drawString(ConfidenceToString(confidence).c_str(), x, y, font,
                       paint);

    canvas->restore();
  };

  for (const auto& face : cached_faces_) {
    // Assume the frame is center cropped and transform the bounding box
    // to the canvas space.
    // TODO(ototot): Check if the frame is not center cropped.
    auto bounding_box_to_skrect = [&](const human_sensing::BoundingBox& box) {
      const auto adjusted_rect = NormalizeRect(
          Rect<float>(box.x1 - static_cast<float>(full_frame_crop.left),
                      box.y1 - static_cast<float>(full_frame_crop.top),
                      box.x2 - box.x1, box.y2 - box.y1),
          Size(full_frame_crop.width, full_frame_crop.height));
      return SkRect::MakeXYWH(adjusted_rect.left * canvas_width,
                              adjusted_rect.top * canvas_height,
                              adjusted_rect.width * canvas_width,
                              adjusted_rect.height * canvas_height);
    };

    auto face_rect = bounding_box_to_skrect(face.bounding_box);
    if (options_.face_rectangles) {
      canvas->drawRect(face_rect, paint);
      if (options_.face_rectangles_confidence) {
        draw_confidence(face_rect, face.confidence);
      }
    }
    if (options_.face_landmarks) {
      for (auto& landmark : face.landmarks) {
        const float box_size = 10 * scale_ratio;
        auto landmark_box =
            human_sensing::BoundingBox{.x1 = landmark.x - box_size,
                                       .y1 = landmark.y - box_size,
                                       .x2 = landmark.x + box_size,
                                       .y2 = landmark.y + box_size};
        auto landmark_rect = bounding_box_to_skrect(landmark_box);
        // If the face rectangle is too small, we will only annotate
        // landmarks with a dot. Otherwise, we will annotate eyes with
        // circles, nose with triangles, ears with ovals, and mouth with a
        // rectangle. This should help identifying which landmark is for
        // which part of body.
        if (std::min(face_rect.width(), face_rect.height()) <=
            kLandmarkBoxLimit) {
          auto saved_style = paint.getStyle();
          paint.setStyle(SkPaint::kFill_Style);
          canvas->drawCircle(landmark_rect.center(), 4 * scale_ratio, paint);
          paint.setStyle(saved_style);
          landmark_rect = landmark_rect.makeInset(box_size - 6 * scale_ratio,
                                                  box_size - 6 * scale_ratio);
        } else {
          switch (landmark.type) {
            case human_sensing::Landmark::Type::kLeftEye:
            case human_sensing::Landmark::Type::kRightEye:
              canvas->drawOval(landmark_rect, paint);
              break;
            case human_sensing::Landmark::Type::kNoseTip:
              canvas->drawPath(BoxToTriangle(landmark_rect), paint);
              break;
            case human_sensing::Landmark::Type::kMouthCenter:
              canvas->drawRect(landmark_rect, paint);
              break;
            case human_sensing::Landmark::Type::kLeftEarTragion:
            case human_sensing::Landmark::Type::kRightEarTragion: {
              const float box_width = 8 * scale_ratio;
              const float box_height = 15 * scale_ratio;
              landmark_box =
                  human_sensing::BoundingBox{.x1 = landmark.x - box_width,
                                             .y1 = landmark.y - box_height,
                                             .x2 = landmark.x + box_width,
                                             .y2 = landmark.y + box_height};
              landmark_rect = bounding_box_to_skrect(landmark_box);
              canvas->drawOval(landmark_rect, paint);
              break;
            }
            case human_sensing::Landmark::Type::kLandmarkUnknown:
              LOGF(WARNING) << "Unknown landmark type at (" << landmark.x
                            << ", " << landmark.y << ")";
              break;
          }
        }
        if (options_.face_landmarks_confidence) {
          draw_confidence(landmark_rect, landmark.confidence);
        }
      }
    }
  }

  return true;
}

void FaceRectanglesFrameAnnotator::UpdateOptions(
    const FrameAnnotator::Options& options) {
  options_ = options;
}

}  // namespace cros
