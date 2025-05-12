// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>

#ifndef HARDWARE_VERIFIER_SYSTEM_CONTEXT_H_
#define HARDWARE_VERIFIER_SYSTEM_CONTEXT_H_

namespace brillo {
class CrosConfigInterface;
}  // namespace brillo

namespace crossystem {
class Crossystem;
}

namespace hardware_verifier {

// A context class for holding the helper objects used in Hardware Verifier,
// which simplifies the passing of the helper objects to other objects. For
// instance, instead of passing various helper objects to an object via its
// constructor, the context object is passed.
class Context {
 public:
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  // Returns the current global context instance. The global instance will be
  // overridden by derived classes. Only one global instance is allowed at a
  // time.
  static Context* Get();

  // The object to access the ChromeOS model configuration.
  virtual brillo::CrosConfigInterface* cros_config() = 0;

  // The object to access crossystem system properties.
  virtual crossystem::Crossystem* crossystem() = 0;

  // Returns the root directory. This can be overridden during test.
  virtual const base::FilePath& root_dir();

 protected:
  Context();
  virtual ~Context();
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_SYSTEM_CONTEXT_H_
