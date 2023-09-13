// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZLIB_CONST
#define ZLIB_CONST
#endif

#include "dlcservice/metadata/zlib_compressor.h"

#include <zconf.h>
#include <zlib.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <base/logging.h>

#include "dlcservice/metadata/compressor_interface.h"
#include "dlcservice/metadata/metadata.h"

namespace dlcservice::metadata {

namespace {
// The common processing loop for compression and decompression.
std::optional<std::string> ProcessImpl(std::function<int(z_streamp, int)> func,
                                       z_streamp zstream,
                                       int flush,
                                       const std::string& data_in) {
  std::string data_out;
  std::vector<Bytef> out_buffer(kMaxMetadataFileSize);
  zstream->avail_in = data_in.size();
  zstream->next_in = reinterpret_cast<const Bytef*>(data_in.data());

  do {
    // The loop runs until `avail_out != 0` meaning no more new data flushed to
    // the output buffer.
    zstream->avail_out = out_buffer.size();
    zstream->next_out = out_buffer.data();
    int ret = func(zstream, flush);
    // Error number other than end of input indicates a critical error.
    if (ret != Z_OK && ret != Z_STREAM_END &&
        !(ret == Z_BUF_ERROR && zstream->avail_in == 0)) {
      LOG(ERROR) << "Failed to process the data, error=" << ret
                 << " msg=" << zstream->msg;
      return std::nullopt;
    }
    // Copy the data to output.
    int len = out_buffer.size() - zstream->avail_out;
    data_out.append(out_buffer.begin(), out_buffer.begin() + len);
  } while (zstream->avail_out == 0);

  return data_out;
}
}  // namespace

ZlibCompressor::ZlibCompressor() {
  // Initialized the zlib compression/deflate with default memory allocation
  // routines, best compression setting, maximum windowBits and default
  // strategy.
  zstream_.zalloc = Z_NULL;
  zstream_.zfree = Z_NULL;
  zstream_.opaque = Z_NULL;
}

ZlibCompressor::~ZlibCompressor() {
  // Terminate the compression and deallocate resources.
  deflateEnd(&zstream_);
}

bool ZlibCompressor::Initialize() {
  int ret = deflateInit2(&zstream_, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS,
                         MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
  if (ret != Z_OK) {
    LOG(ERROR) << "Unable to initialize Zlib compressor, error=" << ret;
    return false;
  }
  return true;
}

std::unique_ptr<CompressorInterface> ZlibCompressor::Clone() {
  auto clone = std::make_unique<ZlibCompressor>();
  int ret = deflateCopy(&(clone->zstream_), &zstream_);
  if (ret != Z_OK) {
    LOG(ERROR) << "Failed to make a copy of the compressor, error: " << ret;
    return nullptr;
  }
  return clone;
}

std::optional<std::string> ZlibCompressor::Process(const std::string& data_in,
                                                   bool flush) {
  auto data_out = ProcessImpl(deflate, &zstream_,
                              flush ? Z_FULL_FLUSH : Z_NO_FLUSH, data_in);
  if (!data_out && !Reset()) {
    LOG(ERROR) << "Failed to reset compressor after compression failure.";
  }

  return data_out;
}

bool ZlibCompressor::Reset() {
  return Z_OK == deflateReset(&zstream_);
}

ZlibDecompressor::ZlibDecompressor() {
  // Initialized the zlib decompression/inflate with default memory allocation
  // routines and maximum windowBits.
  zstream_.zalloc = Z_NULL;
  zstream_.zfree = Z_NULL;
  zstream_.opaque = Z_NULL;
  zstream_.avail_in = 0;
  zstream_.next_in = Z_NULL;
}

ZlibDecompressor::~ZlibDecompressor() {
  // Terminate the decompression and deallocate resources.
  inflateEnd(&zstream_);
}

bool ZlibDecompressor::Initialize() {
  int ret = inflateInit2(&zstream_, -MAX_WBITS);
  if (ret != Z_OK) {
    LOG(ERROR) << "Unable to initialize Zlib decompressor, error=" << ret;
    return false;
  }
  return true;
}

std::unique_ptr<CompressorInterface> ZlibDecompressor::Clone() {
  auto clone = std::make_unique<ZlibDecompressor>();
  int ret = inflateCopy(&(clone->zstream_), &zstream_);
  if (ret != Z_OK) {
    LOG(ERROR) << "Failed to make a copy of the decompressor, error: " << ret;
    return nullptr;
  }
  return clone;
}

std::optional<std::string> ZlibDecompressor::Process(const std::string& data_in,
                                                     bool flush) {
  auto data_out = ProcessImpl(inflate, &zstream_, Z_NO_FLUSH, data_in);
  if (!data_out && !Reset()) {
    LOG(ERROR) << "Failed to reset decompressor after decompression failure.";
  }

  return data_out;
}

bool ZlibDecompressor::Reset() {
  return Z_OK == inflateReset(&zstream_);
}

}  // namespace dlcservice::metadata
