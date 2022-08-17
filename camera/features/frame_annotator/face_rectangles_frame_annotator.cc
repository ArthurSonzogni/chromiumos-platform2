/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/frame_annotator/face_rectangles_frame_annotator.h"

#include <utility>

#include "cros-camera/camera_metadata_utils.h"

namespace cros {

//
// FaceRectanglesFrameAnnotator implementations.
//

bool FaceRectanglesFrameAnnotator::Initialize(
    const camera_metadata_t* static_info) {
  base::span<const int32_t> active_array_size = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
  DCHECK_EQ(active_array_size.size(), 4);
  active_array_dimension_ = Size(active_array_size[2], active_array_size[3]);
  return true;
}

bool FaceRectanglesFrameAnnotator::ProcessCaptureResult(
    const Camera3CaptureDescriptor* result) {
  auto faces = result->feature_metadata().faces;
  if (faces.has_value()) {
    cached_faces_ = std::move(*faces);
  }
  return true;
}

bool FaceRectanglesFrameAnnotator::IsPlotNeeded() const {
  return !cached_faces_.empty();
}

bool FaceRectanglesFrameAnnotator::Plot(SkCanvas* canvas) {
  const auto canvas_info = canvas->imageInfo();

  Rect<uint32_t> full_frame_crop = GetCenteringFullCrop(
      active_array_dimension_, canvas_info.width(), canvas_info.height());

  // Annotate each faces with a white box.
  SkPaint box_paint;
  box_paint.setStyle(SkPaint::kStroke_Style);
  box_paint.setAntiAlias(true);
  box_paint.setStrokeWidth(1);
  box_paint.setColor(0xffffffff);

  for (const auto& face : cached_faces_) {
    const auto& box = face.bounding_box;

    // Assume the frame is center cropped and transform the bounding box
    // to the canvas space.
    // TODO(ototot): Check if the frame is not center cropped.
    const auto adjusted_rect = NormalizeRect(
        Rect<float>(box.x1 - static_cast<float>(full_frame_crop.left),
                    box.y1 - static_cast<float>(full_frame_crop.top),
                    box.x2 - box.x1, box.y2 - box.y1),
        Size(full_frame_crop.width, full_frame_crop.height));
    SkRect rect = SkRect::MakeXYWH(
        adjusted_rect.left * static_cast<float>(canvas_info.width()),
        adjusted_rect.top * static_cast<float>(canvas_info.height()),
        adjusted_rect.width * static_cast<float>(canvas_info.width()),
        adjusted_rect.height * static_cast<float>(canvas_info.height()));
    canvas->drawRect(rect, box_paint);
  }

  return true;
}

void FaceRectanglesFrameAnnotator::UpdateOptions(
    const FrameAnnotator::Options& options) {}

}  // namespace cros
