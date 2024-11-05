// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/image_content_annotation.h"

#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <google/protobuf/text_format.h>
#include <gtest/gtest.h>

#include "chrome/knowledge/raid/raid.pb.h"

namespace ml {

static base::FilePath LibPath() {
  return base::FilePath("/build/share/ml_core/libcros_ml_core_internal.so");
}

TEST(ImageContentAnnotationLibraryTest, CanLoadLibrary) {
  auto* instance = ImageContentAnnotationLibrary::GetInstance(LibPath());
  ASSERT_EQ(instance->GetStatus(), ImageContentAnnotationLibrary::Status::kOk);
}

TEST(ImageContentAnnotationLibraryTest, DetectEncodedImage) {
  auto* instance = ImageContentAnnotationLibrary::GetInstance(LibPath());
  ASSERT_EQ(instance->GetStatus(), ImageContentAnnotationLibrary::Status::kOk);
  RaidV2ImageAnnotator* annotator = instance->CreateImageAnnotator();
  ASSERT_NE(annotator, nullptr);
  ASSERT_TRUE(instance->InitImageAnnotator(annotator));

  auto image_encoded = base::ReadFileToBytes(
      base::FilePath("/build/share/ml_core/cat_and_dog.webp"));
  ASSERT_NE(image_encoded, std::nullopt);

  chrome_knowledge::DetectionResultList detection_scores;
  instance->DetectEncodedImage(annotator, image_encoded.value().data(),
                               image_encoded.value().size(), &detection_scores);

  chrome_knowledge::DetectionResultList expected_detections;
  google::protobuf::TextFormat::ParseFromString(
      R"pb(
        detection {
          score: 0.73828125
          mid: "/m/01lrl"
          name: "Carnivore"
          bounding_box { left: 646 top: 245 right: 1195 bottom: 718 }
        }
        detection {
          score: 0.73828125
          mid: "/m/0jbk"
          name: "Animal"
          bounding_box { left: 646 top: 245 right: 1195 bottom: 718 }
        }
        detection {
          score: 0.73828125
          mid: "/m/04rky"
          name: "Mammal"
          bounding_box { left: 646 top: 245 right: 1195 bottom: 718 }
        }
        detection {
          score: 0.73828125
          mid: "/m/01yrx"
          name: "Cat"
          bounding_box { left: 646 top: 245 right: 1195 bottom: 718 }
        }
        detection {
          score: 0.45703125
          mid: "/m/0bt9lr"
          name: "Dog"
          bounding_box { left: 9 top: 94 right: 844 bottom: 722 }
        }
      )pb",
      &expected_detections);

  // As chromeos does not support `IgnoringRepeatedFieldOrdering`, manually
  // check each detection is returned as expected.
  EXPECT_EQ(detection_scores.detection_size(), 5);
  for (const auto& detection : detection_scores.detection()) {
    bool match_expected = false;
    for (const auto& detection_expected : expected_detections.detection()) {
      if (detection.SerializeAsString() ==
          detection_expected.SerializeAsString()) {
        match_expected = true;
        break;
      }
    }
    EXPECT_TRUE(match_expected);
  }

  instance->DestroyImageAnnotator(annotator);
}

}  // namespace ml
