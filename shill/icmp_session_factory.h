// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_ICMP_SESSION_FACTORY_H_
#define SHILL_ICMP_SESSION_FACTORY_H_

#include <memory>

#include <base/lazy_instance.h>

#include "shill/icmp_session.h"

namespace shill {

class IcmpSessionFactory {
 public:
  virtual ~IcmpSessionFactory();

  // This is a singleton. Use IcmpSessionFactory::GetInstance()->Foo().
  static IcmpSessionFactory* GetInstance();

  virtual std::unique_ptr<IcmpSession> CreateIcmpSession(
      EventDispatcher* dispatcher);

 protected:
  IcmpSessionFactory();

 private:
  friend base::LazyInstanceTraitsBase<IcmpSessionFactory>;

  DISALLOW_COPY_AND_ASSIGN(IcmpSessionFactory);
};

}  // namespace shill

#endif  // SHILL_ICMP_SESSION_FACTORY_H_
