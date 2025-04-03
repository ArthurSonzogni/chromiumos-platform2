// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_TEMPLATE_INFO_H_
#define LIBEC_FINGERPRINT_TEMPLATE_INFO_H_

#include <bitset>
#include <cstdint>

struct TemplateInfo {
  uint32_t version = 0;       /**< version of the template format */
  uint32_t size = 0;          /**< max template size in bytes */
  uint16_t max_templates = 0; /**< maximum number of fingers/templates */
  uint16_t num_valid = 0;     /**< number of valid fingers/templates */
  std::bitset<32> dirty;      /**< bitmap of templates with MCU side changes */

  friend bool operator==(const TemplateInfo&, const TemplateInfo&) = default;
};

#endif  // LIBEC_FINGERPRINT_TEMPLATE_INFO_H_
