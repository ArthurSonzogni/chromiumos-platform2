// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_DLC_HELPER_H_
#define VM_TOOLS_CONCIERGE_DLC_HELPER_H_

#include <memory>
#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <base/types/expected.h>

namespace dbus {
class Bus;
}  // namespace dbus

namespace org::chromium {
class DlcServiceInterfaceProxyInterface;
}

namespace vm_tools::concierge {

class DlcHelper {
 public:
  // Constructs a helper that uses the dbus proxy |handle| to communicate with
  // dlcservice.
  explicit DlcHelper(
      std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface> handle);

  // Constructs a helper whose handle was made from the given |bus| using the
  // default settings.
  explicit DlcHelper(const scoped_refptr<dbus::Bus>& bus);

  // We must declare a destructor in order to prevent std::make_unique from
  // thunking a default one in a TL where DlcServiceInterfaceProxyInterface is
  // only forwards declared.
  ~DlcHelper();

  // Determine the path where the |dlc_id| DLC is located. If it is not
  // installed, or some error occurs, returns an error message.
  base::expected<base::FilePath, std::string> GetRootPath(
      const std::string& dlc_id);

 private:
  std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface>
      dlcservice_handle_;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_DLC_HELPER_H_
