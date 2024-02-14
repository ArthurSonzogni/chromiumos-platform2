// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_CGPT_WRAPPER_H_
#define MINIOS_CGPT_WRAPPER_H_

#include <vboot/cgpt_params.h>
#include <vboot/crossystem.h>
#include <vboot/vboot_host.h>

namespace minios {

// Abstract wrapper to intercept cgpt calls.
class CgptWrapperInterface {
 public:
  virtual ~CgptWrapperInterface() = default;
  virtual void CgptFind(CgptFindParams* params) const = 0;
  virtual int CgptGetPartitionDetails(CgptAddParams* params) const = 0;
};

class CgptWrapper : public CgptWrapperInterface {
 public:
  CgptWrapper() = default;
  CgptWrapper(const CgptWrapper&) = delete;
  CgptWrapper& operator=(const CgptWrapper&) = delete;

  ~CgptWrapper() override = default;

  // CgptWrapperInterface overrides.
  void CgptFind(CgptFindParams* params) const override {
    return ::CgptFind(params);
  }
  int CgptGetPartitionDetails(CgptAddParams* params) const override {
    return ::CgptGetPartitionDetails(params);
  };
};

}  // namespace minios

#endif  // MINIOS_CGPT_WRAPPER_H_
