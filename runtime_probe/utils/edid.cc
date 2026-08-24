// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/edid.h"

#include <algorithm>
#include <numeric>
#include <optional>

#include <base/containers/span.h>
#include <base/logging.h>

namespace {

struct DescriptorBlock {
  uint16_t pixel_clock;
  uint8_t hactive_lo;
  uint8_t hblank_lo;
  uint8_t hactive_hblank_hi;
  uint8_t vactive_lo;
  uint8_t vblank_lo;
  uint8_t vactive_vblank_hi;
  uint8_t pad[10];
} __attribute__((packed));

// Simplified edid structure refer to:
// https://elixir.bootlin.com/linux/latest/source/include/drm/drm_edid.h
struct EdidRaw {
  uint8_t header[8];
  uint8_t mfg_id[2];
  uint8_t prod_code[2];
  uint8_t pad_1[6];  // 12 - 17
  uint8_t version;
  uint8_t pad_2[35];               // 19 - 53
  DescriptorBlock descriptors[4];  // 54 - 125 (4 x 18 bytes)
  uint8_t num_extensions;          // 126
  uint8_t checksum;                // 127
} __attribute__((packed));

// CTA-861 (Tag 0x02) 4-byte extension block header.
struct Cta861Header {
  uint8_t tag;
  uint8_t pad_1;
  uint8_t dtd_start;
  uint8_t pad_2;
} __attribute__((packed));

// DisplayID (Tag 0x70) 4-byte section header.
struct DisplayIdHeader {
  uint8_t tag;
  uint8_t pad_1;
  uint8_t section_len;
  uint8_t pad_2;
} __attribute__((packed));

// DisplayID 3-byte Data Block header.
struct DisplayIdDataBlockHeader {
  uint8_t tag;
  uint8_t pad_1;
  uint8_t num_bytes;
} __attribute__((packed));

// DisplayID 20-byte Detailed Timing Descriptor (Type I & Type VII).
struct DisplayIdTimingDescriptor {
  uint32_t pixel_clock : 24;  // 0 - 2 (3 bytes)
  uint8_t pad_1;
  uint8_t hactive_lo;  // byte 4
  uint8_t hactive_hi;  // byte 5
  uint8_t pad_2[6];
  uint8_t vactive_lo;  // byte 12
  uint8_t vactive_hi;  // byte 13
  uint8_t pad_3[6];
} __attribute__((packed));

struct Resolution {
  int width;
  int height;
};

constexpr int kVersion = 0x01;
constexpr uint8_t kMagic[] = "\x00\xff\xff\xff\xff\xff\xff\x00";
constexpr int kMagicLen = 8;
constexpr int kManufacturerIdBits = 5;

constexpr size_t kBlockSize = sizeof(EdidRaw);
constexpr size_t kBlockSizeWithoutChecksum = kBlockSize - 1;
constexpr uint8_t kCta861Tag = 0x02;
constexpr uint8_t kDisplayIdTag = 0x70;
constexpr uint8_t kDisplayIdType1TimingTag = 0x03;
constexpr uint8_t kDisplayIdType7TimingTag = 0x22;

bool ValidateChecksum(base::span<const uint8_t> block) {
  return (std::accumulate(block.begin(), block.end(), 0) & 0xff) == 0;
}

std::optional<Resolution> ExtractResolutionFromDtds(
    base::span<const DescriptorBlock> dtds) {
  for (const auto& dtd : dtds) {
    if (!dtd.pixel_clock) {
      continue;
    }
    return Resolution{
        .width = ((dtd.hactive_hblank_hi >> 4) << 8) | dtd.hactive_lo,
        .height = ((dtd.vactive_vblank_hi >> 4) << 8) | dtd.vactive_lo,
    };
  }
  return std::nullopt;
}

std::optional<Resolution> ExtractResolutionInCta861(
    base::span<const uint8_t> block) {
  const auto* header = reinterpret_cast<const Cta861Header*>(block.data());
  if (header->tag != kCta861Tag || header->dtd_start == 0) {
    // Either not a CTA-861 extension block, or no Detailed Timing Descriptors
    // in this block per CTA-861 spec.
    return std::nullopt;
  }
  if (header->dtd_start < sizeof(Cta861Header) ||
      kBlockSizeWithoutChecksum < header->dtd_start + sizeof(DescriptorBlock)) {
    LOG(ERROR) << "ExtractResolutionInCta861: invalid dtd_start offset ("
               << static_cast<int>(header->dtd_start) << ").";
    return std::nullopt;
  }
  size_t num_dtds =
      (kBlockSizeWithoutChecksum - header->dtd_start) / sizeof(DescriptorBlock);
  base::span<const DescriptorBlock> dtds(
      reinterpret_cast<const DescriptorBlock*>(&block[header->dtd_start]),
      num_dtds);
  return ExtractResolutionFromDtds(dtds);
}

std::optional<Resolution> ExtractResolutionFromDisplayIdDataBlock(
    const DisplayIdDataBlockHeader& header, base::span<const uint8_t> payload) {
  if (header.tag != kDisplayIdType1TimingTag &&
      header.tag != kDisplayIdType7TimingTag) {
    return std::nullopt;
  }
  if (payload.empty() ||
      payload.size() % sizeof(DisplayIdTimingDescriptor) != 0) {
    LOG(ERROR) << "ExtractResolutionFromDisplayIdDataBlock: invalid timing "
                  "data block size ("
               << payload.size() << ").";
    return std::nullopt;
  }
  size_t num_timing_blocks = payload.size() / sizeof(DisplayIdTimingDescriptor);
  base::span<const DisplayIdTimingDescriptor> timing_blocks(
      reinterpret_cast<const DisplayIdTimingDescriptor*>(payload.data()),
      num_timing_blocks);
  for (const auto& timing_block : timing_blocks) {
    if (!timing_block.pixel_clock) {
      continue;
    }
    return Resolution{
        .width = (timing_block.hactive_lo | (timing_block.hactive_hi << 8)) + 1,
        .height =
            (timing_block.vactive_lo | (timing_block.vactive_hi << 8)) + 1,
    };
  }
  return std::nullopt;
}

std::optional<Resolution> ExtractResolutionInDisplayId(
    base::span<const uint8_t> block) {
  const auto* header = reinterpret_cast<const DisplayIdHeader*>(block.data());
  if (header->tag != kDisplayIdTag) {
    return std::nullopt;
  }
  if (sizeof(DisplayIdHeader) + header->section_len >
      kBlockSizeWithoutChecksum) {
    LOG(ERROR) << "ExtractResolutionInDisplayId: section_len exceeds block "
                  "boundary.";
    return std::nullopt;
  }
  size_t offset = sizeof(DisplayIdHeader);
  size_t end_offset = sizeof(DisplayIdHeader) + header->section_len;

  while (offset < end_offset) {
    if (offset + sizeof(DisplayIdDataBlockHeader) > end_offset) {
      LOG(ERROR)
          << "ExtractResolutionInDisplayId: incomplete data block header.";
      break;
    }
    const auto* data_block_header =
        reinterpret_cast<const DisplayIdDataBlockHeader*>(&block[offset]);
    size_t data_block_body_offset = offset + sizeof(DisplayIdDataBlockHeader);
    if (data_block_body_offset + data_block_header->num_bytes > end_offset) {
      LOG(ERROR) << "ExtractResolutionInDisplayId: data block payload exceeds "
                    "section boundary.";
      break;
    }

    base::span<const uint8_t> payload =
        block.subspan(data_block_body_offset, data_block_header->num_bytes);
    auto resolution =
        ExtractResolutionFromDisplayIdDataBlock(*data_block_header, payload);
    if (resolution) {
      return resolution;
    }
    offset = data_block_body_offset + data_block_header->num_bytes;
  }
  return std::nullopt;
}

}  // namespace

namespace runtime_probe {

std::unique_ptr<Edid> Edid::From(const std::vector<uint8_t>& blob) {
  if (blob.size() < kBlockSize) {
    LOG(ERROR) << "Edid::From: length too small. (" << blob.size() << ")";
    return nullptr;
  }

  EdidRaw edid_raw;
  std::copy(blob.begin(), blob.begin() + kBlockSize,
            reinterpret_cast<uint8_t*>(&edid_raw));

  if (!std::equal(kMagic, kMagic + kMagicLen, edid_raw.header)) {
    LOG(ERROR) << "Edid::From: incorrect header.";
    return nullptr;
  }
  if (edid_raw.version != kVersion) {
    LOG(ERROR) << "Edid::From: unsupported EDID version.";
    return nullptr;
  }
  if (!ValidateChecksum(base::span(blob).first(kBlockSize))) {
    LOG(ERROR) << "Edid::From: checksum error.";
    return nullptr;
  }

  auto edid = std::make_unique<Edid>();

  // Extract resolution from descriptor blocks in base EDID.
  std::optional<Resolution> resolution =
      ExtractResolutionFromDtds(edid_raw.descriptors);
  // Fallback to extract resolution from supported extension blocks.
  if (!resolution) {
    for (int i = 1; i <= edid_raw.num_extensions; ++i) {
      size_t ext_offset = i * kBlockSize;
      if (blob.size() < ext_offset + kBlockSize) {
        LOG(ERROR) << "Edid::From: incomplete extension block " << i
                   << " (expected at least " << ext_offset + kBlockSize
                   << " bytes, got " << blob.size() << " bytes).";
        return nullptr;
      }
      base::span<const uint8_t> ext_block =
          base::span(blob).subspan(ext_offset, kBlockSize);
      if (!ValidateChecksum(ext_block)) {
        LOG(ERROR) << "Edid::From: extension block " << i << " checksum error.";
        continue;
      }
      resolution = ExtractResolutionInDisplayId(ext_block);
      if (!resolution) {
        resolution = ExtractResolutionInCta861(ext_block);
      }
      if (resolution) {
        break;
      }
    }
  }

  if (!resolution) {
    LOG(ERROR)
        << "Edid::From: no valid timing found in Base EDID or Extensions.";
    return nullptr;
  }

  edid->width = resolution->width;
  edid->height = resolution->height;

  int vendor_code = (edid_raw.mfg_id[0] << 8) | edid_raw.mfg_id[1];
  edid->product_id = (edid_raw.prod_code[1] << 8) | edid_raw.prod_code[0];
  edid->vendor = "";
  for (int i = 2; i >= 0; i--) {
    char vendor_char = (vendor_code >> (i * kManufacturerIdBits)) & 0x1f;
    edid->vendor += vendor_char + 'A' - 1;
  }
  return edid;
}

}  // namespace runtime_probe
