// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_BLKID_WRAPPER_H_
#define MINIOS_BLKID_WRAPPER_H_

#include <optional>
#include <string>

#include <blkid/blkid.h>
#include <gtest/gtest_prod.h>

namespace minios {

class BlkIdWrapperInterface {
 public:
  virtual ~BlkIdWrapperInterface() = default;

  // Returns true if device node (e.g. `/dev/sda1`) is present in cache.
  virtual bool FindDevice(const std::string& devname) const = 0;

  // Rebuild internal cache of devices, should be done every enumerate.
  virtual void GetCache() = 0;

  // Returns value associated for a given tag for a given device.
  virtual std::optional<std::string> GetTagValue(
      const std::string& tagname, const std::string& devname) const = 0;
};

class BlkIdWrapper : public BlkIdWrapperInterface {
 public:
  BlkIdWrapper() = default;
  virtual ~BlkIdWrapper() = default;

  bool FindDevice(const std::string& devname) const override;

  void GetCache() override;

  std::optional<std::string> GetTagValue(
      const std::string& tagname, const std::string& devname) const override;

 private:
  FRIEND_TEST(BlkidTest, VerifyGetDevice);
  FRIEND_TEST(BlkidTest, VerifyGetTagHandler);

  std::optional<std::string> HandleTagValue(const char* tag_value,
                                            const std::string& tagname,
                                            const std::string& devname) const;
  bool HandleGetDevice(const blkid_dev& dev) const;

  blkid_cache cache_ = nullptr;
};

}  // namespace minios

#endif  // MINIOS_BLKID_WRAPPER_H_
