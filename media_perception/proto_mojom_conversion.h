// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_PERCEPTION_PROTO_MOJOM_CONVERSION_H_
#define MEDIA_PERCEPTION_PROTO_MOJOM_CONVERSION_H_

#include <vector>

#include "media_perception/common.pb.h"
#include "media_perception/device_management.pb.h"
#include "media_perception/frame_perception.pb.h"
#include "media_perception/media_perception_mojom.pb.h"
#include "media_perception/hotword_detection.pb.h"
#include "media_perception/pipeline.pb.h"
#include "mojom/common.mojom.h"
#include "mojom/device_management.mojom.h"
#include "mojom/frame_perception.mojom.h"
#include "mojom/hotword_detection.mojom.h"
#include "mojom/media_perception.mojom.h"
#include "mojom/pipeline.mojom.h"

namespace chromeos {
namespace media_perception {
namespace mojom {

SuccessStatusPtr ToMojom(const mri::SuccessStatus& status);
PixelFormat ToMojom(mri::PixelFormat format);
VideoStreamParamsPtr ToMojom(const mri::VideoStreamParams& params);
VideoDevicePtr ToMojom(const mri::VideoDevice& device);
VirtualVideoDevicePtr ToMojom(const mri::VirtualVideoDevice& device);
AudioStreamParamsPtr ToMojom(const mri::AudioStreamParams& params);
AudioDevicePtr ToMojom(const mri::AudioDevice& device);
DeviceType ToMojom(mri::DeviceType type);
DeviceTemplatePtr ToMojom(const mri::DeviceTemplate& device_template);

// Common conversions.
DistanceUnits ToMojom(mri::DistanceUnits units);
NormalizedBoundingBoxPtr ToMojom(const mri::NormalizedBoundingBox& bbox);
DistancePtr ToMojom(const mri::Distance& distance);

// Frame perception conversions.
EntityType ToMojom(mri::EntityType type);
FramePerceptionType ToMojom(mri::FramePerceptionType type);
EntityPtr ToMojom(const mri::Entity& entity);
FramePerceptionPtr ToMojom(const mri::FramePerception& perception);

// Hotword detection conversions.
HotwordType ToMojom(mri::HotwordType type);
HotwordPtr ToMojom(const mri::Hotword& hotword);
HotwordDetectionPtr ToMojom(const mri::HotwordDetection& hotword_detection);

// Pipeline conversions.
PipelineStatus ToMojom(mri::PipelineStatus status);
PipelineErrorType ToMojom(mri::PipelineErrorType error_type);
PipelineErrorPtr ToMojom(const mri::PipelineError& error);
PipelineStatePtr ToMojom(const mri::PipelineState& state);

}  // namespace mojom
}  // namespace media_perception
}  // namespace chromeos

namespace mri {

std::vector<uint8_t> SerializeVideoStreamParamsProto(
    const VideoStreamParams& params);
std::vector<uint8_t> SerializeVideoDeviceProto(const VideoDevice& device);
std::vector<uint8_t> SerializeSuccessStatusProto(const SuccessStatus& status);
std::vector<uint8_t> SerializePipelineStateProto(const PipelineState& state);

SuccessStatus ToProto(
    const chromeos::media_perception::mojom::SuccessStatusPtr& status_ptr);
PixelFormat ToProto(
    chromeos::media_perception::mojom::PixelFormat format);
VideoStreamParams ToProto(
    const chromeos::media_perception::mojom::VideoStreamParamsPtr& params_ptr);
VideoDevice ToProto(
    const chromeos::media_perception::mojom::VideoDevicePtr& device_ptr);
VirtualVideoDevice ToProto(
    const chromeos::media_perception::mojom::VirtualVideoDevicePtr& device_ptr);
AudioStreamParams ToProto(
    const chromeos::media_perception::mojom::AudioStreamParamsPtr& params_ptr);
AudioDevice ToProto(
    const chromeos::media_perception::mojom::AudioDevicePtr& device_ptr);
DeviceType ToProto(
    chromeos::media_perception::mojom::DeviceType type);
DeviceTemplate ToProto(
    const chromeos::media_perception::mojom::DeviceTemplatePtr& template_ptr);

// Common conversions.
DistanceUnits ToProto(chromeos::media_perception::mojom::DistanceUnits units);
NormalizedBoundingBox ToProto(
    const chromeos::media_perception::mojom::NormalizedBoundingBoxPtr&
    bbox_ptr);
Distance ToProto(
    const chromeos::media_perception::mojom::DistancePtr& distance_ptr);

// Frame perception conversions.
EntityType ToProto(chromeos::media_perception::mojom::EntityType type);
FramePerceptionType ToProto(
    chromeos::media_perception::mojom::FramePerceptionType type);
Entity ToProto(const chromeos::media_perception::mojom::EntityPtr& entity_ptr);
FramePerception ToProto(
    const chromeos::media_perception::mojom::FramePerceptionPtr&
        perception_ptr);

// Hotword detection conversions.
HotwordType ToProto(
    chromeos::media_perception::mojom::HotwordType type);
Hotword ToProto(
    const chromeos::media_perception::mojom::HotwordPtr& hotword_ptr);
HotwordDetection ToProto(
    const chromeos::media_perception::mojom::HotwordDetectionPtr&
        hotword_deteciton_ptr);

// Pipeline conversions.
PipelineStatus ToProto(
    chromeos::media_perception::mojom::PipelineStatus status);
PipelineErrorType ToProto(
    chromeos::media_perception::mojom::PipelineErrorType error_type);
PipelineError ToProto(
    const chromeos::media_perception::mojom::PipelineErrorPtr& error_ptr);
PipelineState ToProto(
    const chromeos::media_perception::mojom::PipelineStatePtr& state_ptr);

}  // namespace mri

#endif  // MEDIA_PERCEPTION_PROTO_MOJOM_CONVERSION_H_
