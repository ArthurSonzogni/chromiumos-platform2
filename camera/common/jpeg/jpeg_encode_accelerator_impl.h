/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_JPEG_JPEG_ENCODE_ACCELERATOR_IMPL_H_
#define CAMERA_COMMON_JPEG_JPEG_ENCODE_ACCELERATOR_IMPL_H_

#include <stdint.h>
#include <memory>
#include <unordered_map>
#include <vector>

#include <base/memory/shared_memory.h>
#include <base/threading/thread.h>

#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/future.h"
#include "cros-camera/jpeg_compressor.h"
#include "cros-camera/jpeg_encode_accelerator.h"
#include "mojo/cros_camera_service.mojom.h"
#include "mojo/jea/jpeg_encode_accelerator.mojom.h"

namespace cros {

// Encapsulates a converter from JPEG to YU12 format.
// Before using this class, make sure mojo is initialized first.
class JpegEncodeAcceleratorImpl : public JpegEncodeAccelerator {
 public:
  static std::unique_ptr<JpegEncodeAccelerator> CreateJpegEncodeAccelerator();

  JpegEncodeAcceleratorImpl();

  ~JpegEncodeAcceleratorImpl() override;

  // JpegEncodeAccelerator implementation.

  bool Start() override;

  // To be deprecated.
  int EncodeSync(int input_fd,
                 const uint8_t* input_buffer,
                 uint32_t input_buffer_size,
                 int32_t coded_size_width,
                 int32_t coded_size_height,
                 const uint8_t* exif_buffer,
                 uint32_t exif_buffer_size,
                 int output_fd,
                 uint32_t output_buffer_size,
                 uint32_t* output_data_size) override;

  int EncodeSync(uint32_t input_format,
                 const std::vector<JpegCompressor::DmaBufPlane>& input_planes,
                 const std::vector<JpegCompressor::DmaBufPlane>& output_planes,
                 const uint8_t* exif_buffer,
                 uint32_t exif_buffer_size,
                 int width,
                 int height,
                 uint32_t* output_data_size) override;

 private:
  // Map from output buffer ID to shared memory.
  using InputShmMap =
      std::unordered_map<int32_t, std::unique_ptr<base::SharedMemory>>;

  // Initialize Mojo channel to GPU process in chrome.
  // And initialize the JpegEncodeAccelerator.
  void InitializeOnIpcThread(base::Callback<void(bool)> callback);

  // Destroy resources on IPC thread.
  void DestroyOnIpcThread();

  // Error handler for JEA mojo channel.
  void OnJpegEncodeAcceleratorError();

  // Process encode request in IPC thread.
  // Either |input_fd| or |input_buffer| has to be filled up.
  void EncodeOnIpcThreadLegacy(int32_t task_id,
                               int input_fd,
                               const uint8_t* input_buffer,
                               uint32_t input_buffer_size,
                               int32_t coded_size_width,
                               int32_t coded_size_height,
                               const uint8_t* exif_buffer,
                               uint32_t exif_buffer_size,
                               int output_fd,
                               uint32_t output_buffer_size,
                               EncodeWithFDCallback callback);

  // Process encode request in IPC thread.
  void EncodeOnIpcThread(
      int32_t task_id,
      uint32_t input_format,
      const std::vector<JpegCompressor::DmaBufPlane>& input_planes,
      const std::vector<JpegCompressor::DmaBufPlane>& output_planes,
      const uint8_t* exif_buffer,
      uint32_t exif_buffer_size,
      int coded_size_width,
      int coded_size_height,
      EncodeWithDmaBufCallback callback);

  // Callback function for |jea_ptr_|->EncodeWithFD().
  void OnEncodeAck(EncodeWithFDCallback callback,
                   int32_t task_id,
                   uint32_t output_size,
                   cros::mojom::EncodeStatus status);

  // For synced Encode API.
  void EncodeSyncCallback(base::Callback<void(int)> callback,
                          uint32_t* output_data_size,
                          int32_t task_id,
                          uint32_t output_size,
                          int status);

  // Camera Mojo channel manager.
  // We use it to create JpegEncodeAccelerator Mojo channel.
  std::unique_ptr<CameraMojoChannelManager> mojo_channel_manager_;

  // Used to cancel pending futures when error occurs.
  std::unique_ptr<cros::CancellationRelay> cancellation_relay_;

  // Pointer to local proxy of remote JpegEncodeAccelerator interface
  // implementation.
  // All the Mojo communication to |jea_ptr_| happens on |ipc_thread_|.
  mojom::JpegEncodeAcceleratorPtr jea_ptr_;

  // Thread for IPC chores.
  base::Thread ipc_thread_;

  // The id for current encode task.
  int32_t task_id_;

  // A map from buffer id to input and exif shared memory.
  // |input_shm_map_| and |exif_shm_map_| should only be accessed on
  // ipc_thread_.
  // Since the input buffer may be from DMA buffer, we need to prepare a shared
  // memory for JpegEncodeAccelerator interface.
  // We will send the handles of the shared memory to the remote process, so we
  // need to keep the shared memory referenced until we receive EncodeAck.
  InputShmMap input_shm_map_;
  InputShmMap exif_shm_map_;

  DISALLOW_COPY_AND_ASSIGN(JpegEncodeAcceleratorImpl);
};

}  // namespace cros
#endif  // CAMERA_COMMON_JPEG_JPEG_ENCODE_ACCELERATOR_IMPL_H_
