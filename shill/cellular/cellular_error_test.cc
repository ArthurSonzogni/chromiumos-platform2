// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_error.h"

#include <brillo/errors/error_codes.h>
#include <gtest/gtest.h>

namespace shill {
namespace {

const char kErrorIncorrectPasswordMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment.IncorrectPassword";

const char kErrorSimPinMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment.SimPin";

const char kErrorSimPukMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment.SimPuk";

const char kErrorNotSubscribedMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "ServiceOptionNotSubscribed";

const char kErrorMissingOrUnknownApnMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "MissingOrUnknownApn";

const char kErrorUserAuthenticationFailedMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "UserAuthenticationFailed";

const char kErrorIpv4OnlyAllowedMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "Ipv4OnlyAllowed";

const char kErrorIpv6OnlyAllowedMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "Ipv6OnlyAllowed";

const char kErrorIpv4v6OnlyAllowedMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "Ipv4v6OnlyAllowed";

const char kErrorNoCellsInArea[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "NoCellsInArea";

const char kErrorPlmnNotAllowed[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "PlmnNotAllowed";

const char kErrorServiceOptionNotAuthorizedInPlmn[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "ServiceOptionNotAuthorizedInPlmn";

const char kErrorServingNetworkNotAuthorized[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "ServingNetworkNotAuthorized";

const char kErrorPhoneFailureMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "PhoneFailure";
const char kErrorUnknownMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "Unknown";
const char kErrorMultipleAccessToPdnConnectionNotAllowedMM1[] =
    "org.freedesktop.ModemManager1.Error.MobileEquipment."
    "MultipleAccessToPdnConnectionNotAllowed";
const char kErrorThrottledMM1[] =
    "org.freedesktop.ModemManager1.Error.Core.Throttled";
const char kErrorWrongStateMM1[] =
    "org.freedesktop.ModemManager1.Error.Core.WrongState";

const char kErrorOperationNotAllowedMM1[] =
    "org.freedesktop.libmbim.Error.Status.OperationNotAllowed";

const char kErrorMessage[] = "Some error message.";

struct TestParam {
  TestParam(const char* dbus_error, Error::Type error_type)
      : dbus_error(dbus_error), error_type(error_type) {}

  const char* dbus_error;
  Error::Type error_type;
};

class CellularErrorMM1Test : public testing::TestWithParam<TestParam> {};

TEST_P(CellularErrorMM1Test, FromDBusError) {
  TestParam param = GetParam();

  brillo::ErrorPtr detailed_dbus_error;

  brillo::ErrorPtr dbus_error =
      brillo::Error::Create(FROM_HERE, brillo::errors::dbus::kDomain,
                            param.dbus_error, kErrorMessage);
  Error shill_error;
  CellularError::FromMM1ChromeosDBusError(dbus_error.get(), &shill_error);

  EXPECT_EQ(param.error_type, shill_error.type());

  shill_error.ToDetailedError(&detailed_dbus_error);

  EXPECT_EQ(param.dbus_error, detailed_dbus_error->GetCode());
}

INSTANTIATE_TEST_SUITE_P(
    CellularErrorMM1Test,
    CellularErrorMM1Test,
    testing::Values(
        TestParam(kErrorIncorrectPasswordMM1, Error::kIncorrectPin),
        TestParam(kErrorSimPinMM1, Error::kPinRequired),
        TestParam(kErrorSimPukMM1, Error::kPinBlocked),
        TestParam(kErrorIpv4OnlyAllowedMM1, Error::kInvalidApn),
        TestParam(kErrorIpv6OnlyAllowedMM1, Error::kInvalidApn),
        TestParam(kErrorIpv4v6OnlyAllowedMM1, Error::kInvalidApn),
        TestParam(kErrorNotSubscribedMM1, Error::kInvalidApn),
        TestParam(kErrorMissingOrUnknownApnMM1, Error::kInvalidApn),
        TestParam(kErrorUserAuthenticationFailedMM1, Error::kInvalidApn),
        TestParam(kErrorThrottledMM1, Error::kThrottled),
        TestParam(kErrorNoCellsInArea, Error::kNoCarrier),
        TestParam(kErrorPlmnNotAllowed, Error::kNoCarrier),
        TestParam(kErrorServiceOptionNotAuthorizedInPlmn, Error::kNoCarrier),
        TestParam(kErrorServingNetworkNotAuthorized, Error::kNoCarrier),
        TestParam(kErrorPhoneFailureMM1, Error::kInternalError),
        TestParam(kErrorUnknownMM1, Error::kInternalError),
        TestParam(kErrorWrongStateMM1, Error::kWrongState),
        TestParam(kErrorOperationNotAllowedMM1, Error::kOperationNotAllowed),
        TestParam(kErrorMultipleAccessToPdnConnectionNotAllowedMM1,
                  Error::kThrottled),
        TestParam("Some random error name.", Error::kOperationFailed)));

}  // namespace
}  // namespace shill
