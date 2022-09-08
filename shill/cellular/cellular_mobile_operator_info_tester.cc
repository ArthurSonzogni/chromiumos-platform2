// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <iostream>

#include <base/logging.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/flag_helper.h>

#include "shill/cellular/mobile_operator_info.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/test_event_dispatcher.h"

namespace shill {
class MyEventDispatcher : public shill::EventDispatcher {
 public:
  MyEventDispatcher() {}
  ~MyEventDispatcher() {}

  void DispatchForever() {}
  void DispatchPendingEvents() {}
  void PostDelayedTask(const base::Location& location,
                       base::OnceClosure task,
                       base::TimeDelta delay) {}
  void QuitDispatchForever() {}
};
}  // namespace shill

int main(int argc, char* argv[]) {
  DEFINE_string(mccmnc, "", "Home MCCMNC.");
  DEFINE_string(imsi, "", "Home IMSI.");
  DEFINE_string(iccid, "", "Home ICCID.");
  DEFINE_string(name, "", "Home Operator Name.");
  DEFINE_string(serving_mccmnc, "", "Serving MCCMNC.");

  brillo::FlagHelper::Init(argc, argv, "cellular_mobile_operator_info_tester");

  shill::MyEventDispatcher dispatcher;
  std::unique_ptr<shill::MobileOperatorInfo> home_operator_info =
      std::make_unique<shill::MobileOperatorInfo>(&dispatcher, "tester");
  std::unique_ptr<shill::MobileOperatorInfo> serving_provider_info =
      std::make_unique<shill::MobileOperatorInfo>(&dispatcher, "tester");
  home_operator_info->ClearDatabasePaths();
  serving_provider_info->ClearDatabasePaths();
  base::FilePath executable_path = base::FilePath(argv[0]).DirName();
  base::FilePath database_path =
      base::FilePath(executable_path).Append("serviceproviders.pbf");

  logging::SetMinLogLevel(logging::LOGGING_INFO);
  shill::ScopeLogger::GetInstance()->set_verbose_level(5);
  shill::ScopeLogger::GetInstance()->EnableScopesByName("cellular");

  home_operator_info->AddDatabasePath(database_path);
  home_operator_info->Init();

  serving_provider_info->AddDatabasePath(database_path);
  serving_provider_info->Init();

  if (!FLAGS_mccmnc.empty())
    home_operator_info->UpdateMCCMNC(FLAGS_mccmnc);

  home_operator_info->IsMobileNetworkOperatorKnown();

  if (!FLAGS_name.empty())
    home_operator_info->UpdateOperatorName(FLAGS_name);

  home_operator_info->IsMobileNetworkOperatorKnown();

  if (!FLAGS_iccid.empty())
    home_operator_info->UpdateICCID(FLAGS_iccid);

  home_operator_info->IsMobileNetworkOperatorKnown();

  if (!FLAGS_imsi.empty())
    home_operator_info->UpdateIMSI(FLAGS_imsi);

  home_operator_info->IsMobileNetworkOperatorKnown();

  if (!FLAGS_serving_mccmnc.empty())
    serving_provider_info->UpdateMCCMNC(FLAGS_serving_mccmnc);

  home_operator_info->UpdateRequiresRoaming(serving_provider_info.get());

  // The following lines will print to cout because ScopeLogger is set to
  // level 5.
  std::cout << "\nMobileOperatorInfo values:"
            << "\n";
  home_operator_info->uuid();
  home_operator_info->operator_name();
  home_operator_info->country();
  home_operator_info->mccmnc();
  home_operator_info->requires_roaming();
  home_operator_info->apn_list();
  home_operator_info->IsMobileNetworkOperatorKnown();
  return 0;
}
