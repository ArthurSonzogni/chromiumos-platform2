// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APMANAGER_PROCESS_FACTORY_H_
#define APMANAGER_PROCESS_FACTORY_H_

#include <string>

#include <base/lazy_instance.h>

#include <chromeos/process.h>

namespace apmanager {

class ProcessFactory {
 public:
  virtual ~ProcessFactory();

  // This is a singleton. Use ProcessFactory::GetInstance()->Foo().
  static ProcessFactory* GetInstance();

  virtual chromeos::Process* CreateProcess();

 protected:
  ProcessFactory();

 private:
  friend struct base::DefaultLazyInstanceTraits<ProcessFactory>;

  DISALLOW_COPY_AND_ASSIGN(ProcessFactory);
};

}  // namespace apmanager

#endif  // APMANAGER_PROCESS_FACTORY_H_
