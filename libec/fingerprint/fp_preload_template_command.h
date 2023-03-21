// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_PRELOAD_TEMPLATE_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_PRELOAD_TEMPLATE_COMMAND_H_

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include <base/memory/ptr_util.h>
#include <brillo/brillo_export.h>
#include "libec/ec_command.h"
#include "libec/fingerprint/fp_preload_template_params.h"

namespace ec {

class BRILLO_EXPORT FpPreloadTemplateCommand
    : public EcCommand<fp_preload_template::Params, EmptyParam> {
 public:
  template <typename T = FpPreloadTemplateCommand>
  static std::unique_ptr<T> Create(uint16_t finger,
                                   std::vector<uint8_t> tmpl,
                                   uint16_t max_write_size) {
    static_assert(
        std::is_base_of<FpPreloadTemplateCommand, T>::value,
        "Only classes derived from FpPreloadTemplateCommand can use Create");

    if (max_write_size == 0 || max_write_size > kMaxPacketSize) {
      return nullptr;
    }

    // Using new to access non-public constructor. See
    // https://abseil.io/tips/134.
    return base::WrapUnique(new T(finger, tmpl, max_write_size));
  }

  ~FpPreloadTemplateCommand() override = default;

  bool Run(int fd) override;

 protected:
  FpPreloadTemplateCommand(uint16_t finger,
                           std::vector<uint8_t> tmpl,
                           uint16_t max_write_size)
      : EcCommand(EC_CMD_FP_PRELOAD_TEMPLATE),
        finger_(finger),
        template_data_(std::move(tmpl)),
        max_write_size_(max_write_size) {}
  virtual bool EcCommandRun(int fd);

 private:
  uint16_t finger_;
  std::vector<uint8_t> template_data_;
  uint16_t max_write_size_;
};

static_assert(!std::is_copy_constructible<FpPreloadTemplateCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FpPreloadTemplateCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_PRELOAD_TEMPLATE_COMMAND_H_
