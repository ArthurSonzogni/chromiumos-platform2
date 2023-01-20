// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/image_content_annotation.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace ml {

TEST(ImageContentAnnotationLibraryTest, CanLoadLibrary) {
  auto* instance = ImageContentAnnotationLibrary::GetInstance();
  ASSERT_EQ(instance->GetStatus(), ImageContentAnnotationLibrary::Status::kOk);
  ASSERT_TRUE(
      ImageContentAnnotationLibrary::IsUseImageContentAnnotationEnabled());
}

TEST(ImageContentAnnotationLibraryTest, AnnotateImage) {
  auto* instance = ImageContentAnnotationLibrary::GetInstance();
  ASSERT_EQ(instance->GetStatus(), ImageContentAnnotationLibrary::Status::kOk);
  ASSERT_TRUE(
      ImageContentAnnotationLibrary::IsUseImageContentAnnotationEnabled());
  ImageContentAnnotator* annotator = instance->CreateImageContentAnnotator();
  ASSERT_NE(annotator, nullptr);
  ASSERT_TRUE(instance->InitImageContentAnnotator(annotator, "en-US"));

  std::string image_encoded;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath("/build/share/ica/moon_big.jpg"), &image_encoded));

  auto mat =
      cv::imdecode(cv::_InputArray(image_encoded.data(), image_encoded.size()),
                   cv::IMREAD_COLOR);
  cv::cvtColor(mat, mat, cv::COLOR_BGR2RGB);

  chrome_knowledge::AnnotationScoreList annotation_scores;
  instance->AnnotateImage(annotator, mat.data, mat.cols, mat.rows, mat.step,
                          &annotation_scores);

  ASSERT_EQ(3, annotation_scores.annotation_size());
  EXPECT_EQ(annotation_scores.annotation(0).id(), 335);
  EXPECT_EQ(annotation_scores.annotation(0).confidence(), 232);
  EXPECT_EQ(annotation_scores.annotation(0).mid(), "/m/06wqb");
  EXPECT_EQ(annotation_scores.annotation(0).name(), "Space");

  instance->DestroyImageContentAnnotator(annotator);
}

}  // namespace ml
