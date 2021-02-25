/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/zsl_helper.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/numerics/safe_conversions.h>
#include <base/optional.h>
#include <camera/camera_metadata.h>
#include <sync/sync.h>
#include <system/camera_metadata.h>

#include "cros-camera/common.h"
#include "cros-camera/constants.h"
#include "cros-camera/utils/camera_config.h"

namespace {

bool IsInputStream(camera3_stream_t* stream) {
  return stream->stream_type == CAMERA3_STREAM_INPUT ||
         stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL;
}

bool IsOutputStream(camera3_stream_t* stream) {
  return stream->stream_type == CAMERA3_STREAM_OUTPUT ||
         stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL;
}

int64_t GetTimestamp(const android::CameraMetadata& android_metadata) {
  camera_metadata_ro_entry_t entry;
  if (android_metadata.exists(ANDROID_SENSOR_TIMESTAMP)) {
    entry = android_metadata.find(ANDROID_SENSOR_TIMESTAMP);
    return entry.data.i64[0];
  }
  LOGF(ERROR) << "Cannot find sensor timestamp in ZSL buffer";
  return static_cast<int64_t>(-1);
}

}  // namespace

namespace cros {

ZslBuffer::ZslBuffer()
    : metadata_ready(false), buffer_ready(false), selected(false) {}
ZslBuffer::ZslBuffer(uint32_t frame_number,
                     const camera3_stream_buffer_t& buffer)
    : frame_number(frame_number),
      buffer(buffer),
      metadata_ready(false),
      buffer_ready(false),
      selected(false) {}

ZslBufferManager::ZslBufferManager()
    : initialized_(false),
      buffer_manager_(CameraBufferManager::GetInstance()) {}

ZslBufferManager::~ZslBufferManager() {
  if (free_buffers_.size() != buffer_pool_.size()) {
    LOGF(WARNING) << "Not all ZSL buffers have been released";
  }
  for (auto& buffer : buffer_pool_) {
    buffer_manager_->Free(buffer);
  }
}

void ZslBufferManager::Reset() {
  base::AutoLock l(buffer_pool_lock_);
  for (auto& buffer : buffer_pool_) {
    buffer_manager_->Free(buffer);
  }
  buffer_pool_.clear();
  free_buffers_ = {};
  buffer_to_buffer_pointer_map_.clear();
}

bool ZslBufferManager::Initialize(size_t pool_size,
                                  camera3_stream_t* output_stream) {
  DCHECK(buffer_pool_.empty());

  bool success = true;
  output_stream_ = output_stream;
  {
    base::AutoLock l(buffer_pool_lock_);
    buffer_pool_.reserve(pool_size);
    for (size_t i = 0; i < pool_size; ++i) {
      uint32_t stride;
      buffer_handle_t buffer;
      if (buffer_manager_->Allocate(
              output_stream_->width, output_stream_->height,
              ZslHelper::kZslPixelFormat,
              GRALLOC_USAGE_HW_CAMERA_ZSL | GRALLOC_USAGE_SW_READ_OFTEN |
                  GRALLOC_USAGE_SW_WRITE_OFTEN,
              &buffer, &stride) != 0) {
        LOGF(ERROR) << "Failed to allocate buffer";
        success = false;
        break;
      }
      buffer_pool_.push_back(buffer);
      free_buffers_.push(&buffer_pool_.back());
      buffer_to_buffer_pointer_map_[buffer] = &buffer_pool_.back();
    }
  }

  if (!success) {
    Reset();
    return false;
  }
  initialized_ = true;
  return true;
}

buffer_handle_t* ZslBufferManager::GetBuffer() {
  base::AutoLock buffer_pool_lock(buffer_pool_lock_);
  if (!initialized_) {
    LOGF(ERROR) << "ZSL buffer manager has not been initialized";
    return nullptr;
  }
  if (free_buffers_.empty()) {
    LOGF(ERROR) << "No more buffer left in the pool. This shouldn't happen";
    return nullptr;
  }

  buffer_handle_t* buffer = free_buffers_.front();
  free_buffers_.pop();
  return buffer;
}

bool ZslBufferManager::ReleaseBuffer(buffer_handle_t buffer_to_release) {
  base::AutoLock buffer_pool_lock(buffer_pool_lock_);
  if (!initialized_) {
    LOGF(ERROR) << "ZSL buffer manager has not been initialized";
    return false;
  }
  auto it = buffer_to_buffer_pointer_map_.find(buffer_to_release);
  if (it == buffer_to_buffer_pointer_map_.end()) {
    LOGF(ERROR) << "The released buffer doesn't belong to ZSL buffer manager";
    return false;
  }
  free_buffers_.push(it->second);
  return true;
}

// static
bool ZslHelper::TryAddEnableZslKey(android::CameraMetadata* metadata) {
  // Determine if it's possible for us to enable our in-house ZSL solution. Note
  // that we may end up not enabling it in situations where we cannot allocate
  // sufficient private buffers or the camera HAL client's stream configuration
  // wouldn't allow us to set up the streams we need.
  if (!metadata->exists(ANDROID_REQUEST_AVAILABLE_CAPABILITIES)) {
    return false;
  }
  const auto cap_entry = metadata->find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
  if (std::find(cap_entry.data.u8, cap_entry.data.u8 + cap_entry.count,
                ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING) ==
      cap_entry.data.u8 + cap_entry.count) {
    return false;
  }

  // See if the camera HAL already supports ZSL.
  if (!metadata->exists(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS)) {
    return false;
  }
  auto entry = metadata->find(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS);
  if (std::find(entry.data.i32, entry.data.i32 + entry.count,
                ANDROID_CONTROL_ENABLE_ZSL) != entry.data.i32 + entry.count) {
    LOGF(INFO) << "Device supports vendor-provided ZSL";
    return false;
  }

  std::vector<int32_t> new_request_keys{entry.data.i32,
                                        entry.data.i32 + entry.count};
  new_request_keys.push_back(ANDROID_CONTROL_ENABLE_ZSL);
  if (metadata->update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
                       new_request_keys.data(), new_request_keys.size()) != 0) {
    LOGF(ERROR) << "Failed to add ANDROID_CONTROL_ENABLE_ZSL to metadata";
    return false;
  }
  LOGF(INFO) << "Added ANDROID_CONTROL_ENABLE_ZSL to static metadata";
  return true;
}

ZslHelper::ZslHelper(const camera_metadata_t* static_info)
    : fence_sync_thread_("FenceSyncThread") {
  VLOGF_ENTER();
  if (!IsCapabilitySupported(
          static_info,
          ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING)) {
    LOGF(INFO) << "Private reprocessing not supported, ZSL won't be enabled";
    return;
  }
  uint32_t bi_width, bi_height;
  if (!SelectZslStreamSize(static_info, &bi_width, &bi_height,
                           &bi_stream_min_frame_duration_)) {
    LOGF(ERROR) << "Failed to select stream sizes for ZSL.";
    return;
  }
  LOGF(INFO) << "Selected ZSL stream size = " << bi_width << "x" << bi_height;
  // Create ZSL streams
  bi_stream_ = std::make_unique<camera3_stream_t>();
  bi_stream_->stream_type = CAMERA3_STREAM_BIDIRECTIONAL;
  bi_stream_->width = bi_width;
  bi_stream_->height = bi_height;
  bi_stream_->format = kZslPixelFormat;

  if (!fence_sync_thread_.Start()) {
    LOGF(ERROR) << "Fence sync thread failed to start";
  }
  partial_result_count_ = [&]() {
    camera_metadata_ro_entry entry;
    if (find_camera_metadata_ro_entry(
            static_info, ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &entry) != 0) {
      return 1;
    }
    return entry.data.i32[0];
  }();
  max_num_input_streams_ = [&]() {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(
            static_info, ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, &entry) != 0) {
      LOGF(ERROR) << "Failed to get maximum number of input streams.";
      return 0;
    }
    return entry.data.i32[0];
  }();
  timestamp_source_ = [&]() {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(
            static_info, ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &entry) != 0) {
      LOGF(ERROR) << "Failed to get timestamp source. Assuming it's UNKNOWN.";
      return ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN;
    }
    return static_cast<
        camera_metadata_enum_android_sensor_info_timestamp_source_t>(
        entry.data.u8[0]);
  }();

  auto camera_config =
      cros::CameraConfig::Create(cros::constants::kCrosCameraConfigPathString);
  // We're casting an int to int64_t here. Make sure the configured time doesn't
  // overflow (roughly 2.1s).
  zsl_lookback_ns_ = base::strict_cast<int64_t>(camera_config->GetInteger(
      cros::constants::kCrosZslLookback,
      base::checked_cast<int>(kZslDefaultLookbackNs)));
  LOGF(INFO) << "Configured ZSL lookback time = " << zsl_lookback_ns_;
}

ZslHelper::~ZslHelper() {
  fence_sync_thread_.Stop();
}

bool ZslHelper::AttachZslStream(camera3_stream_configuration_t* stream_list,
                                std::vector<camera3_stream_t*>* streams) {
  if (!CanEnableZsl(streams)) {
    return false;
  }

  stream_list->num_streams++;
  streams->push_back(bi_stream_.get());
  // There could be memory reallocation happening after the push_back call.
  stream_list->streams = streams->data();

  for (size_t i = 0; i < stream_list->num_streams; ++i) {
    // GRALLOC_USAGE_STILL_CAPTURE is a private usage from VCD.
    // We set the usage flag here to let VCD know ZSL is enabled.
    if ((*streams)[i]->usage & GRALLOC_USAGE_STILL_CAPTURE) {
      (*streams)[i]->usage |= GRALLOC_USAGE_ZSL_ENABLED;
    }
  }

  VLOGF(1) << "Attached ZSL streams. The list of streams after attaching:";
  for (size_t i = 0; i < stream_list->num_streams; ++i) {
    VLOGF(1) << "i = " << i
             << ", type = " << stream_list->streams[i]->stream_type
             << ", size = " << stream_list->streams[i]->width << "x"
             << stream_list->streams[i]->height
             << ", format = " << stream_list->streams[i]->format;
  }

  return true;
}

bool ZslHelper::Initialize(const camera3_stream_configuration_t* stream_list) {
  auto GetStillCaptureMaxBuffers =
      [&](const camera3_stream_configuration_t* stream_list) {
        uint32_t max_buffers = 0;
        for (size_t i = 0; i < stream_list->num_streams; ++i) {
          auto* stream = stream_list->streams[i];
          if (!IsOutputStream(stream)) {
            continue;
          }
          // If our private usage flag is specified, we know only this stream
          // will be used for ZSL capture.
          if (stream->usage & cros::GRALLOC_USAGE_STILL_CAPTURE) {
            return stream->max_buffers;
          } else if (stream->format == HAL_PIXEL_FORMAT_BLOB) {
            max_buffers += stream->max_buffers;
          }
        }
        return max_buffers;
      };

  base::AutoLock ring_buffer_lock(ring_buffer_lock_);

  // First, clear all the buffers and states.
  ring_buffer_.clear();
  zsl_buffer_manager_.Reset();

  // Determine at most how many buffers would be selected for private
  // reprocessing simultaneously.
  bi_stream_max_buffers_ = 0;
  for (uint32_t i = 0; i < stream_list->num_streams; i++) {
    auto* stream = stream_list->streams[i];
    if (stream == bi_stream_.get()) {
      bi_stream_max_buffers_ = stream->max_buffers;
      break;
    }
  }
  if (bi_stream_max_buffers_ == 0) {
    LOGF(ERROR) << "Failed to acquire max_buffers for the private stream";
    return false;
  }
  VLOGF(1) << "Max buffers for private stream = " << bi_stream_max_buffers_;

  // Determine at most how many still capture buffers would be in-flight.
  uint32_t still_max_buffers = GetStillCaptureMaxBuffers(stream_list);
  if (still_max_buffers == 0) {
    LOGF(ERROR) << "Failed to acquire max_buffers for the still capture stream";
    return false;
  }
  VLOGF(1) << "Max buffers for still capture streams = " << still_max_buffers;

  // We look back at most
  // ceil(|zsl_lookback_ns_| / |bi_stream_min_frame_duration_| frames, and there
  // will be at most |bi_stream_max_buffers_| being processed. We also need to
  // have |still_max_buffers| additional buffers in the buffer pool.
  if (!zsl_buffer_manager_.Initialize(
          static_cast<size_t>(std::ceil(static_cast<double>(zsl_lookback_ns_) /
                                        bi_stream_min_frame_duration_)) +
              bi_stream_max_buffers_ + still_max_buffers,
          bi_stream_.get())) {
    LOGF(ERROR) << "Failed to initialize ZSL buffer manager";
    return false;
  }

  return true;
}

bool ZslHelper::CanEnableZsl(std::vector<camera3_stream_t*>* streams) {
  size_t num_input_streams = 0;
  bool has_still_capture_output_stream = false;
  bool has_zsl_output_stream = false;
  for (auto* stream : (*streams)) {
    if (IsInputStream(stream)) {
      num_input_streams++;
    }
    if (IsOutputStream(stream) &&
        (stream->format == HAL_PIXEL_FORMAT_BLOB ||
         (stream->usage & GRALLOC_USAGE_STILL_CAPTURE))) {
      has_still_capture_output_stream = true;
    }
    if (IsOutputStream(stream) &&
        (stream->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
            GRALLOC_USAGE_HW_CAMERA_ZSL) {
      has_zsl_output_stream = true;
    }
  }
  return num_input_streams < max_num_input_streams_  // Has room for an extra
                                                     // input stream for ZSL.
         && has_still_capture_output_stream  // Has a stream for still capture.
         && !has_zsl_output_stream;  // HAL doesn't support multiple raw output
                                     // streams.
}

bool ZslHelper::IsZslRequested(camera_metadata_t* settings) {
  bool enable_zsl = [&]() {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(settings, ANDROID_CONTROL_ENABLE_ZSL,
                                      &entry) == 0) {
      return static_cast<bool>(entry.data.u8[0]);
    }
    return false;
  }();
  if (!enable_zsl) {
    return false;
  }
  // We can only enable ZSL when capture intent is also still capture.
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(settings, ANDROID_CONTROL_CAPTURE_INTENT,
                                    &entry) == 0) {
    return entry.data.u8[0] == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE ||
           entry.data.u8[0] == ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
  }
  return false;
}

bool ZslHelper::IsAttachedZslBuffer(const camera3_stream_buffer_t* buffer) {
  return buffer && buffer->stream == bi_stream_.get();
}

bool ZslHelper::IsTransformedZslBuffer(const camera3_stream_buffer_t* buffer) {
  return buffer && buffer->stream == bi_stream_.get();
}

void ZslHelper::TryReleaseBuffer() {
  ring_buffer_lock_.AssertAcquired();
  // Check if the oldest buffer is already too old to be selected. In which
  // case, we can remove it from our ring buffer. If the buffer is not selected,
  // we release it back to the buffer pool. If the buffer is selected, we
  // release it when it returns from ProcessZslCaptureResult.
  if (ring_buffer_.empty()) {
    return;
  }
  const ZslBuffer& oldest_buffer = ring_buffer_.back();
  if (oldest_buffer.selected) {
    ring_buffer_.pop_back();
    return;
  }

  if (!oldest_buffer.metadata_ready) {
    return;
  }
  auto timestamp = GetTimestamp(oldest_buffer.metadata);
  DCHECK_NE(timestamp, -1);
  if (GetCurrentTimestamp() - timestamp <= zsl_lookback_ns_) {
    // Buffer is too new that we should keep it. This will happen for the
    // initial buffers.
    return;
  }
  if (!zsl_buffer_manager_.ReleaseBuffer(*oldest_buffer.buffer.buffer)) {
    LOGF(ERROR) << "Unable to release the oldest buffer";
    return;
  }
  ring_buffer_.pop_back();
}

bool ZslHelper::ProcessZslCaptureRequest(
    camera3_capture_request_t* request,
    std::vector<camera3_stream_buffer_t>* output_buffers,
    internal::ScopedCameraMetadata* settings,
    SelectionStrategy strategy) {
  if (request->input_buffer != nullptr) {
    return false;
  }
  bool transformed = false;
  if (IsZslRequested(settings->get())) {
    transformed = TransformRequest(request, settings, strategy);
    if (!transformed) {
      LOGF(ERROR) << "Failed to find a suitable ZSL buffer";
    }
  } else {
    AttachRequest(request, output_buffers);
  }
  return transformed;
}

void ZslHelper::AttachRequest(
    camera3_capture_request_t* request,
    std::vector<camera3_stream_buffer_t>* output_buffers) {
  VLOGF_ENTER();

  base::AutoLock l(ring_buffer_lock_);
  TryReleaseBuffer();
  auto* buffer = zsl_buffer_manager_.GetBuffer();
  if (buffer == nullptr) {
    LOGF(ERROR) << "Failed to acquire a ZSL buffer";
    return;
  }
  // Attach our ZSL output buffer.
  camera3_stream_buffer_t stream_buffer;
  stream_buffer.buffer = buffer;
  stream_buffer.stream = bi_stream_.get();
  stream_buffer.acquire_fence = stream_buffer.release_fence = -1;

  ZslBuffer zsl_buffer(request->frame_number, stream_buffer);
  ring_buffer_.push_front(std::move(zsl_buffer));

  output_buffers->push_back(std::move(stream_buffer));
  request->num_output_buffers++;
}

bool ZslHelper::TransformRequest(camera3_capture_request_t* request,
                                 internal::ScopedCameraMetadata* settings,
                                 SelectionStrategy strategy) {
  VLOGF_ENTER();
  base::AutoLock l(ring_buffer_lock_);
  auto GetJpegOrientation = [&](const camera_metadata_t* settings) {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(settings, ANDROID_JPEG_ORIENTATION,
                                      &entry) != 0) {
      LOGF(ERROR) << "Failed to find JPEG orientation, defaulting to 0";
      return 0;
    }
    return *entry.data.i32;
  };

  // Select the best buffer.
  ZslBufferIterator selected_buffer_it = SelectZslBuffer(strategy);
  if (selected_buffer_it == ring_buffer_.end()) {
    LOGF(WARNING) << "Unable to find a suitable ZSL buffer. Request will not "
                     "be transformed.";
    return false;
  }

  LOGF(INFO) << "Transforming request into ZSL reprocessing request";
  request->input_buffer = &selected_buffer_it->buffer;
  request->input_buffer->stream = bi_stream_.get();
  request->input_buffer->acquire_fence = -1;
  request->input_buffer->release_fence = -1;

  // The result metadata for the RAW buffers come from the preview frames. We
  // need to add JPEG orientation back so that the resulting JPEG is of the
  // correct orientation.
  int32_t jpeg_orientation = GetJpegOrientation(settings->get());
  if (selected_buffer_it->metadata.update(ANDROID_JPEG_ORIENTATION,
                                          &jpeg_orientation, 1) != 0) {
    LOGF(ERROR) << "Failed to update JPEG_ORIENTATION";
  }
  // Note that camera device adapter would take ownership of this pointer.
  settings->reset(selected_buffer_it->metadata.release());
  return true;
}

void ZslHelper::ProcessZslCaptureResult(
    const camera3_capture_result_t* result,
    const camera3_stream_buffer_t** attached_output,
    const camera3_stream_buffer_t** transformed_input) {
  VLOGF_ENTER();
  for (size_t i = 0; i < result->num_output_buffers; ++i) {
    if (IsAttachedZslBuffer(&result->output_buffers[i])) {
      *attached_output = &result->output_buffers[i];
      break;
    }
  }
  if (result->input_buffer && IsTransformedZslBuffer(result->input_buffer)) {
    *transformed_input = result->input_buffer;
    ReleaseStreamBuffer(*result->input_buffer);
  }

  base::AutoLock ring_buffer_lock(ring_buffer_lock_);
  auto it = std::find_if(ring_buffer_.begin(), ring_buffer_.end(),
                         [&](const ZslBuffer& buffer) {
                           return buffer.frame_number == result->frame_number;
                         });
  if (it == ring_buffer_.end()) {
    return;
  }
  for (size_t i = 0; i < result->num_output_buffers; ++i) {
    if (result->output_buffers[i].stream == bi_stream_.get()) {
      WaitAttachedFrame(result->frame_number,
                        result->output_buffers[i].release_fence);
      break;
    }
  }

  if (result->partial_result != 0) {  // Result has metadata. Merge it.
    it->metadata.append(result->result);
    if (result->partial_result == partial_result_count_) {
      it->metadata_ready = true;
    }
  }
}

void ZslHelper::WaitAttachedFrame(uint32_t frame_number, int release_fence) {
  fence_sync_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&ZslHelper::WaitAttachedFrameOnFenceSyncThread,
                 base::Unretained(this), frame_number, release_fence));
}

void ZslHelper::WaitAttachedFrameOnFenceSyncThread(uint32_t frame_number,
                                                   int release_fence) {
  if (release_fence != -1 &&
      sync_wait(release_fence, ZslHelper::kZslSyncWaitTimeoutMs)) {
    LOGF(WARNING) << "Failed to wait for release fence on attached ZSL buffer";
  } else {
    base::AutoLock ring_buffer_lock(ring_buffer_lock_);
    auto it = std::find_if(ring_buffer_.begin(), ring_buffer_.end(),
                           [&](const ZslBuffer& buffer) {
                             return buffer.frame_number == frame_number;
                           });
    if (it != ring_buffer_.end()) {
      it->buffer_ready = true;
    }
    return;
  }
  fence_sync_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&ZslHelper::WaitAttachedFrameOnFenceSyncThread,
                 base::Unretained(this), frame_number, release_fence));
}

void ZslHelper::ReleaseStreamBuffer(camera3_stream_buffer_t buffer) {
  fence_sync_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&ZslHelper::ReleaseStreamBufferOnFenceSyncThread,
                            base::Unretained(this), base::Passed(&buffer)));
}

void ZslHelper::ReleaseStreamBufferOnFenceSyncThread(
    camera3_stream_buffer_t buffer) {
  if (buffer.release_fence != -1 &&
      sync_wait(buffer.release_fence, ZslHelper::kZslSyncWaitTimeoutMs)) {
    LOGF(WARNING) << "Failed to wait for release fence on ZSL input buffer";
  } else {
    if (!zsl_buffer_manager_.ReleaseBuffer(*buffer.buffer)) {
      LOGF(ERROR) << "Failed to release this stream buffer";
    }
    // The above error should only happen when the mapping in buffer manager
    // becomes invalid somwhow. It's not recoverable, so we don't retry here.
    return;
  }
  fence_sync_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&ZslHelper::ReleaseStreamBufferOnFenceSyncThread,
                            base::Unretained(this), base::Passed(&buffer)));
}

bool ZslHelper::IsCapabilitySupported(const camera_metadata_t* static_info,
                                      uint8_t capability) {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(
          static_info, ANDROID_REQUEST_AVAILABLE_CAPABILITIES, &entry) == 0) {
    return std::find(entry.data.u8, entry.data.u8 + entry.count, capability) !=
           entry.data.u8 + entry.count;
  }
  return false;
}

bool ZslHelper::SelectZslStreamSize(const camera_metadata_t* static_info,
                                    uint32_t* bi_width,
                                    uint32_t* bi_height,
                                    int64_t* min_frame_duration) {
  VLOGF_ENTER();

  *bi_width = 0;
  *bi_height = 0;
  camera_metadata_ro_entry entry;
  if (find_camera_metadata_ro_entry(
          static_info, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
          &entry) != 0) {
    LOGF(ERROR) << "Failed to find stream configurations map";
    return false;
  }
  VLOGF(1) << "Iterating stream configuration map for ZSL streams";
  for (size_t i = 0; i < entry.count; i += 4) {
    const int32_t& format = entry.data.i32[i + STREAM_CONFIG_FORMAT_INDEX];
    if (format != kZslPixelFormat)
      continue;
    const int32_t& width = entry.data.i32[i + STREAM_CONFIG_WIDTH_INDEX];
    const int32_t& height = entry.data.i32[i + STREAM_CONFIG_HEIGHT_INDEX];
    const int32_t& direction =
        entry.data.i32[i + STREAM_CONFIG_DIRECTION_INDEX];
    VLOGF(1) << "format = " << format << ", "
             << "width = " << width << ", "
             << "height = " << height << ", "
             << "direction = " << direction;
    if (direction == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT) {
      if (width * height > (*bi_width) * (*bi_height)) {
        *bi_width = width;
        *bi_height = height;
      }
    }
  }
  if (*bi_width == 0 || *bi_height == 0) {
    LOGF(ERROR) << "Failed to select ZSL stream size";
    return false;
  }

  *min_frame_duration = 0;
  if (find_camera_metadata_ro_entry(
          static_info, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, &entry) !=
      0) {
    LOGF(ERROR) << "Failed to find the minimum frame durations";
    return false;
  }
  for (size_t i = 0; i < entry.count; i += 4) {
    const int64_t& format = entry.data.i64[i + FRAME_DURATION_FOMRAT_INDEX];
    const int64_t& width = entry.data.i64[i + FRAME_DURATION_WIDTH_INDEX];
    const int64_t& height = entry.data.i64[i + FRAME_DURATION_HEIGHT_INDEX];
    const int64_t& duration = entry.data.i64[i + FRAME_DURATION_DURATION_INDEX];
    if (format == kZslPixelFormat && width == *bi_width &&
        height == *bi_height) {
      *min_frame_duration = duration;
      break;
    }
  }
  if (*min_frame_duration == 0) {
    LOGF(ERROR) << "Failed to find the minimum frame duration for the selected "
                   "ZSL stream";
    return false;
  }

  return true;
}

ZslHelper::ZslBufferIterator ZslHelper::SelectZslBuffer(
    SelectionStrategy strategy) {
  ring_buffer_lock_.AssertAcquired();
  if (strategy == LAST_SUBMITTED) {
    for (auto it = ring_buffer_.begin(); it != ring_buffer_.end(); it++) {
      if (it->metadata_ready && it->buffer_ready && !it->selected) {
        it->selected = true;
        return it;
      }
    }
    LOGF(WARNING) << "Failed to find a unselected submitted ZSL buffer";
    return ring_buffer_.end();
  }

  // For CLOSEST or CLOSEST_3A strategies.
  int64_t cur_timestamp = GetCurrentTimestamp();
  LOGF(INFO) << "Current timestamp = " << cur_timestamp;
  ZslBufferIterator selected_buffer_it = ring_buffer_.end();
  int64_t min_diff = zsl_lookback_ns_;
  int64_t ideal_timestamp = cur_timestamp - zsl_lookback_ns_;
  for (auto it = ring_buffer_.begin(); it != ring_buffer_.end(); it++) {
    if (!it->metadata_ready || !it->buffer_ready || it->selected) {
      continue;
    }
    int64_t timestamp = GetTimestamp(it->metadata);
    bool satisfy_3a = strategy == CLOSEST ||
                      (strategy == CLOSEST_3A && Is3AConverged(it->metadata));
    int64_t diff = timestamp - ideal_timestamp;
    VLOGF(1) << "Candidate timestamp = " << timestamp
             << " (Satisfy 3A = " << satisfy_3a << ", "
             << "Difference from desired timestamp = " << diff << ")";
    if (diff > kZslLookbackLengthNs) {
      continue;
    } else if (diff < 0) {
      // We don't select buffers that are older than what is displayed.
      break;
    }
    if (satisfy_3a) {
      if (diff < min_diff) {
        min_diff = diff;
        selected_buffer_it = it;
      } else {
        // Not possible to find a better buffer
        break;
      }
    }
  }
  if (selected_buffer_it == ring_buffer_.end()) {
    LOGF(WARNING)
        << "Failed to a find suitable ZSL buffer with the given strategy";
    return selected_buffer_it;
  }
  LOGF(INFO) << "Timestamp of the selected buffer = "
             << GetTimestamp(selected_buffer_it->metadata);
  selected_buffer_it->selected = true;
  return selected_buffer_it;
}

int64_t ZslHelper::GetCurrentTimestamp() {
  struct timespec t = {};
  clock_gettime(
      timestamp_source_ == ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN
          ? CLOCK_MONOTONIC
          : CLOCK_BOOTTIME /* ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME */,
      &t);
  return static_cast<int64_t>(t.tv_sec) * 1000000000LL + t.tv_nsec;
}

bool ZslHelper::Is3AConverged(const android::CameraMetadata& android_metadata) {
  auto GetState = [&](size_t tag) {
    camera_metadata_ro_entry_t entry;
    if (android_metadata.exists(tag)) {
      entry = android_metadata.find(tag);
      return entry.data.u8[0];
    }
    LOGF(ERROR) << "Cannot find the metadata for "
                << get_camera_metadata_tag_name(tag);
    return static_cast<uint8_t>(0);
  };
  uint8_t ae_mode = GetState(ANDROID_CONTROL_AE_MODE);
  uint8_t ae_state = GetState(ANDROID_CONTROL_AE_STATE);
  bool ae_converged = [&]() {
    if (ae_mode != ANDROID_CONTROL_AE_MODE_OFF) {
      if (ae_state != ANDROID_CONTROL_AE_STATE_CONVERGED &&
          ae_state != ANDROID_CONTROL_AE_STATE_FLASH_REQUIRED &&
          ae_state != ANDROID_CONTROL_AE_STATE_LOCKED) {
        return false;
      }
    }
    return true;
  }();
  if (!ae_converged) {
    return false;
  }
  uint8_t af_mode = GetState(ANDROID_CONTROL_AF_MODE);
  uint8_t af_state = GetState(ANDROID_CONTROL_AF_STATE);
  bool af_converged = [&]() {
    if (af_mode != ANDROID_CONTROL_AF_MODE_OFF) {
      if (af_state != ANDROID_CONTROL_AF_STATE_PASSIVE_FOCUSED &&
          af_state != ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED) {
        return false;
      }
    }
    return true;
  }();
  if (!af_converged) {
    return false;
  }
  uint8_t awb_mode = GetState(ANDROID_CONTROL_AWB_MODE);
  uint8_t awb_state = GetState(ANDROID_CONTROL_AWB_STATE);
  bool awb_converged = [&]() {
    if (awb_mode != ANDROID_CONTROL_AWB_MODE_OFF) {
      if (awb_state != ANDROID_CONTROL_AWB_STATE_CONVERGED &&
          awb_state != ANDROID_CONTROL_AWB_STATE_LOCKED) {
        return false;
      }
    }
    return true;
  }();
  // We won't reach here if neither AE nor AF is converged.
  return awb_converged;
}

}  // namespace cros
