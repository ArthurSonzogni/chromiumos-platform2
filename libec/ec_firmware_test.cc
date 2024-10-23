// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/ec_firmware.h"

#include <fmap.h>

#include <vector>

#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

constexpr uint64_t kTestImageBaseAddr = 0x8000000;
const char* kTestImageFwName = "EC_FMAP";
const char* kRoAreaName = "EC_RO";
const char* kRwAreaName = "EC_RW";
const char* kRoVerAreaName = "RO_FRID";
const char* kRwVerAreaName = "RW_FWID";

struct fmap_image_area {
  /* Actual data of area. */
  std::vector<uint8_t> data;
  /* Size to report in fmap. */
  uint32_t reported_size;
  /* Area name. */
  const char* name;
};

class Fmap {
 public:
  Fmap() = default;
  Fmap(const Fmap&) = delete;
  Fmap& operator=(const Fmap&) = delete;
  ~Fmap() { Destroy(); }

  bool Create(uint64_t base, uint32_t size, const char* name) {
    Destroy();
    fmap_ = fmap_create(base, size, reinterpret_cast<const uint8_t*>(name));
    return (fmap_ != nullptr);
  }
  bool AppendArea(uint32_t offset,
                  uint32_t size,
                  const char* name,
                  uint16_t flags) {
    CHECK(IsValid());
    return fmap_append_area(&fmap_, offset, size,
                            reinterpret_cast<const uint8_t*>(name), flags) >= 0;
  }
  bool IsValid() const { return fmap_ != nullptr; }
  const char* GetData() const { return reinterpret_cast<char*>(fmap_); }
  int GetDataLength() const { return fmap_size(fmap_); }

 private:
  void Destroy() {
    if (IsValid()) {
      fmap_destroy(fmap_);
      fmap_ = nullptr;
    }
  }

  struct fmap* fmap_ = nullptr;
};

class EcFirmwareTest : public testing::Test {
 protected:
  bool CreateFakeImage(const base::FilePath& abspath,
                       uint32_t fmap_report_size,
                       uint32_t file_size,
                       const std::vector<fmap_image_area>& areas) {
    LOG(INFO) << "Creating fake image at: " << abspath.value();

    base::File file(abspath,
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    if (!file.IsValid()) {
      return false;
    }

    Fmap fmap;
    if (!fmap.Create(kTestImageBaseAddr, fmap_report_size, kTestImageFwName)) {
      LOG(ERROR) << "Failed to allocate fmap struct";
      return false;
    }

    uint32_t offset = 0;
    for (auto& [data, reported_size, name] : areas) {
      /* Place areas at the front of the file */
      if (data.size()) {
        if (file.WriteAtCurrentPos(reinterpret_cast<const char*>(data.data()),
                                   data.size()) < 0) {
          LOG(ERROR) << "Failed to write area into fake image file.";
          return false;
        }
      }

      if (reported_size > 0) {
        if (!fmap.AppendArea(offset, reported_size, name, 0)) {
          LOG(ERROR) << "Failed to append " << name << " area.";
          return false;
        }
      }

      offset += data.size();
    }

    if (!fmap.IsValid()) {
      LOG(ERROR) << "Fmap data or size are invalid.";
      return false;
    }

    if (file.WriteAtCurrentPos(fmap.GetData(), fmap.GetDataLength()) < 0) {
      LOG(ERROR) << "Failed to write fmap into fake image.";
      return false;
    }

    /* Set requested image file size. */
    if (!file.SetLength(file_size)) {
      LOG(ERROR) << "Failed to elongate fake image to typical size.";
      return false;
    }

    EXPECT_TRUE(base::PathExists(abspath));

    return true;
  }

  EcFirmwareTest() = default;
  ~EcFirmwareTest() override = default;
};

TEST_F(EcFirmwareTest, CreateNoFile) {
  const auto fw = ec::EcFirmware::Create(base::FilePath("/abc123"));
  EXPECT_EQ(fw, nullptr);
}

TEST_F(EcFirmwareTest, CreateNoFmap) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  const auto fw_path = temp_dir.GetPath().Append("nofmap.bin");
  base::File file(fw_path,
                  base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);

  EXPECT_TRUE(file.IsValid());

  EXPECT_TRUE(base::PathExists(fw_path));

  const auto fw = ec::EcFirmware::Create(fw_path);
  EXPECT_EQ(fw, nullptr);
}

TEST_F(EcFirmwareTest, CreateInvalidFmapSize) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  const auto fw_path = temp_dir.GetPath().Append("invalid_fmap_size.bin");
  constexpr uint32_t fmap_size = 1000;
  std::vector<fmap_image_area> areas;

  /* File size is smaller than size reported in fmap. */
  EXPECT_TRUE(CreateFakeImage(fw_path, fmap_size, fmap_size - 1, areas));
  EXPECT_TRUE(base::PathExists(fw_path));

  const auto fw = ec::EcFirmware::Create(fw_path);
  EXPECT_EQ(fw, nullptr);
}

TEST_F(EcFirmwareTest, CreateInvalidAreaSize) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  const auto fw_path = temp_dir.GetPath().Append("CreateInvalidAreaSize.bin");
  constexpr uint32_t fmap_size = 1000;
  /* One area size is bigger than size reported by the fmap header. */
  std::vector<fmap_image_area> areas = {
      {.reported_size = fmap_size + 1, .name = "inv_size_area"}};

  EXPECT_TRUE(CreateFakeImage(fw_path, fmap_size, 2 * fmap_size, areas));
  EXPECT_TRUE(base::PathExists(fw_path));

  const auto fw = ec::EcFirmware::Create(fw_path);
  EXPECT_EQ(fw, nullptr);
}

TEST_F(EcFirmwareTest, InvalidImage) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  const auto fw_path = temp_dir.GetPath().Append("InvalidImage.bin");
  constexpr uint32_t fmap_size = 1000;
  constexpr uint32_t area_size = 16;
  /* Prepare image with all areas. */
  std::vector<fmap_image_area> areas = {
      {.data = std::vector<uint8_t>(area_size),
       .reported_size = area_size,
       .name = kRoAreaName},
      {.data = std::vector<uint8_t>(area_size),
       .reported_size = area_size,
       .name = kRwAreaName},
      {.data = std::vector<uint8_t>(area_size),
       .reported_size = area_size,
       .name = kRoVerAreaName},
      {.data = std::vector<uint8_t>(area_size),
       .reported_size = area_size,
       .name = kRwVerAreaName}};

  EXPECT_TRUE(CreateFakeImage(fw_path, fmap_size, fmap_size, areas));
  EXPECT_TRUE(base::PathExists(fw_path));

  const auto fw = ec::EcFirmware::Create(fw_path);
  EXPECT_NE(fw, nullptr);

  constexpr std::array invalid_images = {EC_IMAGE_UNKNOWN, EC_IMAGE_RO_B,
                                         EC_IMAGE_RW_B};
  constexpr std::array valid_images = {EC_IMAGE_RO, EC_IMAGE_RW};
  for (auto& image : invalid_images) {
    EXPECT_EQ(fw->GetOffset(image), std::nullopt);
    EXPECT_EQ(fw->GetSize(image), std::nullopt);
    EXPECT_EQ(fw->GetVersion(image), std::nullopt);
    EXPECT_EQ(fw->GetData(image).data(), nullptr);
    EXPECT_TRUE(fw->GetData(image).empty());
  }
  for (auto& image : valid_images) {
    EXPECT_NE(fw->GetOffset(image), std::nullopt);
    EXPECT_NE(fw->GetSize(image), std::nullopt);
    EXPECT_NE(fw->GetVersion(image), std::nullopt);
    EXPECT_NE(fw->GetData(image).data(), nullptr);
    EXPECT_FALSE(fw->GetData(image).empty());
  }
}

TEST_F(EcFirmwareTest, NoAreas) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  const auto fw_path = temp_dir.GetPath().Append("NoAreas.bin");
  constexpr uint32_t fmap_size = 1000;
  std::vector<fmap_image_area> areas;

  /* File with the fmap header, but no fmap areas. */
  EXPECT_TRUE(CreateFakeImage(fw_path, fmap_size, fmap_size, areas));
  EXPECT_TRUE(base::PathExists(fw_path));

  const auto fw = ec::EcFirmware::Create(fw_path);
  EXPECT_NE(fw, nullptr);

  constexpr std::array valid_images = {EC_IMAGE_RO, EC_IMAGE_RW};
  for (auto& image : valid_images) {
    EXPECT_EQ(fw->GetOffset(image), std::nullopt);
    EXPECT_EQ(fw->GetSize(image), std::nullopt);
    EXPECT_EQ(fw->GetVersion(image), std::nullopt);
    EXPECT_EQ(fw->GetData(image).data(), nullptr);
    EXPECT_TRUE(fw->GetData(image).empty());
  }
}

TEST_F(EcFirmwareTest, ValidImage) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  const auto fw_path = temp_dir.GetPath().Append("ValidImage.bin");
  constexpr uint32_t area_size = 256;
  constexpr uint32_t fmap_size = 1000;
  std::vector<fmap_image_area> areas = {
      {
          .data = std::vector<uint8_t>(area_size, 1),
          .reported_size = area_size,
          .name = kRoAreaName,
      },
      {
          .data = std::vector<uint8_t>(area_size, 2),
          .reported_size = area_size,
          .name = kRwAreaName,
      }};

  EXPECT_TRUE(CreateFakeImage(fw_path, fmap_size, fmap_size, areas));
  EXPECT_TRUE(base::PathExists(fw_path));

  const auto fw = ec::EcFirmware::Create(fw_path);
  EXPECT_NE(fw, nullptr);

  /* Get RO. */
  base::span<const uint8_t> data = fw->GetData(EC_IMAGE_RO);
  EXPECT_NE(data.data(), nullptr);
  EXPECT_EQ(data.size(), area_size);
  EXPECT_EQ(std::memcmp(data.data(), areas[0].data.data(), area_size), 0);
  EXPECT_EQ(fw->GetSize(EC_IMAGE_RO), area_size);
  EXPECT_EQ(fw->GetOffset(EC_IMAGE_RO), 0);
  /* Get RW. */
  data = fw->GetData(EC_IMAGE_RW);
  EXPECT_NE(data.data(), nullptr);
  EXPECT_EQ(data.size(), area_size);
  EXPECT_EQ(std::memcmp(data.data(), areas[1].data.data(), area_size), 0);
  EXPECT_EQ(fw->GetSize(EC_IMAGE_RW), area_size);
  EXPECT_EQ(fw->GetOffset(EC_IMAGE_RW), area_size);
}

TEST_F(EcFirmwareTest, InvalidVersion) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  const auto fw_path = temp_dir.GetPath().Append("InvalidVersion.bin");
  constexpr uint32_t area_size = 32;
  constexpr uint32_t fmap_size = 1000;
  /* Create version string without NULL termination. */
  std::vector<fmap_image_area> areas = {{
      .data = std::vector<uint8_t>(area_size, 'a'),
      .reported_size = area_size,
      .name = kRoVerAreaName,
  }};

  EXPECT_TRUE(CreateFakeImage(fw_path, fmap_size, fmap_size, areas));
  EXPECT_TRUE(base::PathExists(fw_path));

  const auto fw = ec::EcFirmware::Create(fw_path);
  EXPECT_NE(fw, nullptr);

  /* Get RO version. */
  std::optional<std::string> version = fw->GetVersion(EC_IMAGE_RO);
  EXPECT_EQ(version, std::nullopt);
}

TEST_F(EcFirmwareTest, GetVersion) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  const auto fw_path = temp_dir.GetPath().Append("GetVersion.bin");
  constexpr uint32_t area_size = 32;
  constexpr uint32_t fmap_size = 1000;
  std::string version_ro = "TeStingABCversionRO123xxVV";
  std::string version_rw = "1234_RORW_eve_123abc-Test";
  std::vector<fmap_image_area> areas = {
      {
          .data = std::vector<uint8_t>(area_size, 0),
          .reported_size = area_size,
          .name = kRoVerAreaName,
      },
      {
          .data = std::vector<uint8_t>(area_size, 0),
          .reported_size = area_size,
          .name = kRwVerAreaName,
      }};

  /* Prepare image with version strings. */
  std::ranges::copy(version_ro, areas[0].data.begin());
  std::ranges::copy(version_rw, areas[1].data.begin());

  EXPECT_TRUE(CreateFakeImage(fw_path, fmap_size, fmap_size, areas));
  EXPECT_TRUE(base::PathExists(fw_path));

  const auto fw = ec::EcFirmware::Create(fw_path);
  EXPECT_NE(fw, nullptr);

  /* Get RO version. */
  std::optional<std::string> version = fw->GetVersion(EC_IMAGE_RO);
  EXPECT_NE(version, std::nullopt);
  EXPECT_EQ(*version, version_ro);
  /* Get RW version. */
  version = fw->GetVersion(EC_IMAGE_RW);
  EXPECT_NE(version, std::nullopt);
  EXPECT_EQ(*version, version_rw);
}

}  // namespace
