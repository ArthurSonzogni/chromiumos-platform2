# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Common utilities for interacting with modems.

#
# For modems managed by org.freedesktop.ModemManager1
#
MM1=org.freedesktop.ModemManager1
MM1_OBJECT=/org/freedesktop/ModemManager1
MM1_IMANAGER=org.freedesktop.ModemManager1
MM1_IMODEM=${MM1_IMANAGER}.Modem
MM1_IMODEM_SIMPLE=${MM1_IMODEM}.Simple
MM1_IMODEM_3GPP=${MM1_IMODEM}.Modem3gpp
MM1_IMODEM_CDMA=${MM1_IMODEM}.ModemCdma
MM1_ISIM=${MM1_IMANAGER}.Sim

mm1_modem_sim() {
  dbus_property "${MM1}" "${modem}" "${MM1_IMODEM}" Sim
}

mm1_modem_properties() {
  local modem="$1"
  local sim=$(mm1_modem_sim "${modem}")

  echo
  echo "Modem ${modem}:"
  echo "  GetStatus:"
  dbus_call "${MM1}" "${modem}" "${MM1_IMODEM_SIMPLE}.GetStatus" \
    | format_dbus_dict | indent 2
  echo "  Properties:"
  dbus_properties "${MM1}" "${modem}" "${MM1_IMODEM}" \
    | format_dbus_dict | indent 2
  echo "  3GPP:"
  dbus_properties "${MM1}" "${modem}" "${MM1_IMODEM_3GPP}" \
    | format_dbus_dict | indent 2
  echo "  CDMA:"
  dbus_properties "${MM1}" "${modem}" "${MM1_IMODEM_CDMA}" \
    | format_dbus_dict | indent 2

  if [ "${#sim}" -gt 1 ]; then
    echo "  SIM ${sim}:"
    dbus_properties "${MM1}" "${sim}" "${MM1_ISIM}" \
      | format_dbus_dict | indent 2
  fi
}

mm1_modems() {
  mmcli -L 2>/dev/null \
    | awk '/\/org\/freedesktop\/ModemManager1\/Modem\// { print $1 }'
}

#
# Common stuff
#
MASKED_PROPERTIES="DeviceIdentifier|EquipmentIdentifier|OwnNumbers|\
ESN|MEID|IMEI|IMSI|SimIdentifier|MDN|MIN|payment_url_postdata|Eid|Iccid"

mask_modem_properties() {
  sed -E "s/\<(${MASKED_PROPERTIES}): (.+)/\1: *** MASKED ***/i"
}

mask_profiles() {
  sed -E "s/(\/profile\/)[0-9]+\/[0-9]+/\1[ ***MASKED*** ]/"
}

all_modem_status() {
  for modem in $(mm1_modems); do
    mm1_modem_properties "${modem}"
  done
}

default_modem() {
  mm1_modems | head -1
}

# Sets the log level of the specified modem manager.
set_modem_manager_logging() {
  local level="$1"

  if [ "${level}" = "error" ]; then
    level=err
  fi
  dbus_call "${MM1}" "${MM1_OBJECT}" "${MM1_IMANAGER}.SetLogging" \
    "string:${level}"
}

#
# For interactions with modemfwd.
#
MODEMFWD=org.chromium.Modemfwd
MODEMFWD_OBJECT=/org/chromium/Modemfwd
MODEMFWD_IFACE=org.chromium.Modemfwd

force_flash() {
  local device="$1"
  [ -z "${device}" ] && error_exit "No device_id provided."

  dbus_call_with_timeout "${MODEMFWD}" 120000 "${MODEMFWD_OBJECT}" \
    "${MODEMFWD_IFACE}.ForceFlash" "string:${device}"
}

#
# For eSIM interactions.
#
HERMES=org.chromium.Hermes
HERMES_MANAGER_OBJECT=/org/chromium/Hermes/Manager
HERMES_MANAGER_IFACE=org.chromium.Hermes.Manager

HERMES_EUICC_IFACE=org.chromium.Hermes.Euicc
HERMES_PROFILE_IFACE=org.chromium.Hermes.Profile

# Timeout for Hermes esim operations (in milliseconds)
HERMES_DBUS_TIMEOUT=120000

esim() {
  local command="$1"
  shift

  local euicc
  if [ "$1" = "-euicc" ]; then
    euicc="/org/chromium/Hermes/euicc/$2"
    shift 2
  else
    euicc=$(default_euicc)
  fi
  [ -z "${euicc}" ] && error_exit "No euicc found."

  case "${command}" in
    use_test_certs)
      poll_for_dbus_service "${HERMES}"
      esim_use_test_certs "${euicc}" "$@"
      ;;
    refresh_profiles)
      poll_for_dbus_service "${HERMES}"
      esim_refresh_profiles "${euicc}" "$@"
      ;;
    request_pending_profiles)
      poll_for_dbus_service "${HERMES}"
      esim_request_pending_profiles "${euicc}" "$@"
      ;;
    status)
      poll_for_dbus_service "${HERMES}"
      esim_status "$@"
      ;;
    status_feedback)
      poll_for_dbus_service "${HERMES}"
      esim_status_feedback "$@"
      ;;
    install)
      poll_for_dbus_service "${HERMES}"
      esim_install "${euicc}" "$@"
      ;;
    install_pending_profile)
      poll_for_dbus_service "${HERMES}"
      esim_install_pending_profile "${euicc}" "$@"
      ;;
    uninstall)
      if crossystem 'cros_debug?1'; then
        poll_for_dbus_service "${HERMES}"
        esim_uninstall "${euicc}" "$@"
      else
        error_exit "Cannot uninstall profile. Uninstallation allowed in"\
          "dev mode only"
      fi
      ;;
    enable)
      poll_for_dbus_service "${HERMES}"
      esim_enable "${euicc}" "$@"
      ;;
    disable)
      poll_for_dbus_service "${HERMES}"
      esim_disable "${euicc}" "$@"
      ;;
    *)
      error_exit "Expected one of "\
        "{use_test_certs|refresh_profiles|request_pending_profiles|"\
        "status|status_feedback|install|install_pending_profile|uninstall|"\
        "enable|disable}"
      ;;
  esac
}

all_euiccs() {
  dbus_property "${HERMES}" "${HERMES_MANAGER_OBJECT}" \
    "${HERMES_MANAGER_IFACE}" AvailableEuiccs |
    sed 's|^/[[:digit:]]* ||'
}

default_euicc() {
  all_euiccs | head -1
}

esim_profile_from_iccid() {
  local euicc="$1"

  local iccid="$2"
  [ -z "${iccid}" ] && error_exit "No iccid provided."

  local profile_type
  for profile_type in "InstalledProfiles" "PendingProfiles"; do
    local profiles
    profiles="$(dbus_property "${HERMES}" "${euicc}" \
                              "${HERMES_EUICC_IFACE}" "${profile_type}" |
                              sed 's|^/[[:digit:]]* ||' | tr '\n' ' ')"

    if ! echo "${profiles}" | grep -q -E \
      '^(/org/chromium/Hermes/profile/[0-9]+/[0-9]+ )*$'; then

      error_exit "Invalid profile objects received from hermes"

    fi

    local profile
    for profile in ${profiles}; do
      local current
      current="$(dbus_property "${HERMES}" "${profile}" \
                               "${HERMES_PROFILE_IFACE}" Iccid)"
      if [ "${current}" = "${iccid}" ]; then
        echo "${profile}"
        return
      fi
    done
  done
  error_exit "No matching Profile found for iccid ${iccid}."
}

esim_use_test_certs() {
  local euicc="$1"
  dbus_call "${HERMES}" "${euicc}" \
            "${HERMES_EUICC_IFACE}.UseTestCerts" \
            boolean:"$2"
}

esim_status() {
  local euicc
  for euicc in $(all_euiccs); do
    echo "${euicc}"
    dbus_properties "${HERMES}" "${euicc}" "${HERMES_EUICC_IFACE}" |
      stripindexes

    local profile_type
    for profile_type in "InstalledProfiles" "PendingProfiles"; do
      echo "${profile_type}:" | indent 1
      local profile
      for profile in $(dbus_property "${HERMES}" "${euicc}" \
        "${HERMES_EUICC_IFACE}" "${profile_type}" |
                         sed 's|^/[[:digit:]]* ||'); do
        echo "${profile}" | indent 2
        dbus_properties "${HERMES}" "${profile}" "${HERMES_PROFILE_IFACE}" |
          stripindexes | indent 3
        echo
      done
    done
    echo ""
  done
}

esim_status_feedback() {
  esim_status | mask_modem_properties | mask_profiles
}

esim_refresh_profiles() {
  local euicc="$1"

  dbus_call "${HERMES}" "${euicc}" \
           "${HERMES_EUICC_IFACE}.RequestInstalledProfiles"
}

esim_install() {
  local euicc="$1"
  local activation_code="$2"
  local confirmation_code="$3"
  [ -z "${activation_code}" ] && error_exit "No activation_code provided."

  dbus_call_with_timeout "${HERMES}" "${HERMES_DBUS_TIMEOUT}" "${euicc}" \
            "${HERMES_EUICC_IFACE}.InstallProfileFromActivationCode" \
            string:"${activation_code}" string:"${confirmation_code}"
}

esim_install_pending_profile() {
  local euicc="$1"
  local profile
  profile=$(esim_profile_from_iccid "$@")
  dbus_call_with_timeout "${HERMES}" "${HERMES_DBUS_TIMEOUT}" "${euicc}" \
            "${HERMES_EUICC_IFACE}.InstallPendingProfile" \
            objpath:"${profile}" string:"${confirmation_code}"
}

esim_uninstall() {
  local euicc="$1"
  local profile
  profile=$(esim_profile_from_iccid "$@")
  [ -z "${profile}" ] && exit 1
  dbus_call_with_timeout "${HERMES}" "${HERMES_DBUS_TIMEOUT}" "${euicc}" \
            "${HERMES_EUICC_IFACE}.UninstallProfile" objpath:"${profile}"
}

esim_request_pending_profiles() {
  local euicc="$1"
  local smds="$2"

  dbus_call_with_timeout "${HERMES}" "${HERMES_DBUS_TIMEOUT}" "${euicc}" \
            "${HERMES_EUICC_IFACE}.RequestPendingProfiles" string:"${smds}"
}

esim_enable() {
  local profile
  profile=$(esim_profile_from_iccid "$@")
  [ -z "${profile}" ] && exit 1
  dbus_call_with_timeout "${HERMES}" "${HERMES_DBUS_TIMEOUT}" \
                         "${profile}" "${HERMES_PROFILE_IFACE}.Enable"
}

esim_disable() {
  local profile
  profile=$(esim_profile_from_iccid "$@")
  [ -z "${profile}" ] && exit 1
  dbus_call_with_timeout "${HERMES}" "${HERMES_DBUS_TIMEOUT}" "${profile}" \
            "${HERMES_PROFILE_IFACE}.Disable"
}
