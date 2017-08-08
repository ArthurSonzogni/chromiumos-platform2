// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/tgt_manager.h"

#include <algorithm>
#include <vector>

#include <base/files/file_util.h>
#include <base/location.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>

#include "authpolicy/anonymizer.h"
#include "authpolicy/authpolicy_flags.h"
#include "authpolicy/authpolicy_metrics.h"
#include "authpolicy/constants.h"
#include "authpolicy/jail_helper.h"
#include "authpolicy/platform_helper.h"
#include "authpolicy/process_executor.h"
#include "authpolicy/samba_helper.h"
#include "bindings/authpolicy_containers.pb.h"

namespace authpolicy {

namespace {

// Requested TGT lifetimes in the kinit command. Format is 1d2h3m. If a server
// has a lower maximum lifetimes, the lifetimes of the TGT are capped.
const char kRequestedTgtValidityLifetime[] = "1d";
const char kRequestedTgtRenewalLifetime[] = "7d";

// Don't try to renew TGTs more often than this interval.
const int kMinTgtRenewDelaySeconds = 300;
static_assert(kMinTgtRenewDelaySeconds > 0, "");

// Fraction of the TGT validity lifetime to schedule automatic TGT renewal. For
// instance, if the TGT is valid for another 1000 seconds and the factor is 0.8,
// the TGT would be renewed after 800 seconds. Must be strictly between 0 and 1.
constexpr float kTgtRenewValidityLifetimeFraction = 0.8f;
static_assert(kTgtRenewValidityLifetimeFraction > 0.0f, "");
static_assert(kTgtRenewValidityLifetimeFraction < 1.0f, "");

// Size limit for GetKerberosFiles (1 MB).
const size_t kKrb5FileSizeLimit = 1024 * 1024;

// Kerberos configuration file data.
const char kKrb5ConfData[] =
    "[libdefaults]\n"
    // Only allow AES. (No DES, no RC4.)
    "\tdefault_tgs_enctypes = aes256-cts-hmac-sha1-96 aes128-cts-hmac-sha1-96\n"
    "\tdefault_tkt_enctypes = aes256-cts-hmac-sha1-96 aes128-cts-hmac-sha1-96\n"
    "\tpermitted_enctypes = aes256-cts-hmac-sha1-96 aes128-cts-hmac-sha1-96\n"
    // Prune weak ciphers from the above list. With current settings it’s a
    // no-op, but still.
    "\tallow_weak_crypto = false\n"
    // Default is 300 seconds, but we might add a policy for that in the future.
    "\tclockskew = 300\n"
    // Required for password change.
    "\tdefault_realm = %s\n";
const char kKrb5RealmData[] =
    "[realms]\n"
    "\t%s = {\n"
    "\t\tkdc = [%s]\n"
    "\t\tkpasswd_server = [%s]\n"
    "\t}\n";

// Env variable to trace debug info of kinit.
const char kKrb5TraceEnvKey[] = "KRB5_TRACE";

// Maximum kinit tries.
const int kKinitMaxTries = 60;
// Wait interval between two kinit tries.
const int kKinitRetryWaitSeconds = 1;

// Keys for interpreting kinit output.
const char kKeyBadPrincipal[] =
    "not found in Kerberos database while getting initial credentials";
const char kKeyBadPassword[] =
    "Preauthentication failed while getting initial credentials";
const char kKeyBadPassword2[] =
    "Password incorrect while getting initial credentials";
const char kKeyPasswordExpiredStdout[] =
    "Password expired.  You must change it now.";
const char kKeyPasswordExpiredStderr[] =
    "Cannot read password while getting initial credentials";
const char kKeyCannotResolve[] =
    "Cannot resolve network address for KDC in realm";
const char kKeyCannotContactKDC[] = "Cannot contact any KDC";
const char kKeyNoCrentialsCache[] = "No credentials cache found";
const char kKeyTicketExpired[] = "Ticket expired while renewing credentials";

// Nice marker for TGT renewal related logs, for easy grepping.
const char kTgtRenewalHeader[] = "TGT RENEWAL - ";

// Returns true if the given principal is a machine principal.
bool IsMachine(const std::string& principal) {
  return Contains(principal, "$@");
}

// Reads the file at |path| into |data|. Returns |ERROR_LOCAL_IO| if the file
// could not be read.
ErrorType ReadFile(const base::FilePath& path, std::string* data) {
  data->clear();
  if (!base::ReadFileToStringWithMaxSize(path, data, kKrb5FileSizeLimit)) {
    PLOG(ERROR) << "Failed to read '" << path.value() << "'";
    data->clear();
    return ERROR_LOCAL_IO;
  }
  return ERROR_NONE;
}

// Formats a time delta in 1h 2m 3s format.
std::string FormatTimeDelta(int delta_seconds) {
  int h = delta_seconds / 3600;
  int m = (delta_seconds / 60) % 60;
  int s = delta_seconds % 60;

  std::string str;
  if (h > 0)
    str += base::StringPrintf("%ih", h);
  if (h > 0 || m > 0)
    str += base::StringPrintf("%s%im", str.size() > 0 ? " " : "", m);
  str += base::StringPrintf("%s%is", str.size() > 0 ? " " : "", s);
  return str;
}

std::ostream& operator<<(std::ostream& os,
                         const protos::TgtLifetime& lifetime) {
  os << "(valid for " << FormatTimeDelta(lifetime.validity_seconds())
     << ", renewable for " << FormatTimeDelta(lifetime.renewal_seconds())
     << ")";
  return os;
}

// In case kinit failed, checks the output and returns appropriate error codes.
ErrorType GetKinitError(const ProcessExecutor& kinit_cmd,
                        bool is_machine_principal) {
  DCHECK_NE(0, kinit_cmd.GetExitCode());
  const std::string& kinit_out = kinit_cmd.GetStdout();
  const std::string& kinit_err = kinit_cmd.GetStderr();

  if (Contains(kinit_err, kKeyCannotContactKDC)) {
    LOG(ERROR) << "kinit failed - failed to contact KDC";
    return ERROR_CONTACTING_KDC_FAILED;
  }
  if (Contains(kinit_err, kKeyBadPrincipal)) {
    LOG(ERROR) << "kinit failed - bad "
               << (is_machine_principal ? "machine" : "user") << " name";
    return is_machine_principal ? ERROR_BAD_MACHINE_NAME : ERROR_BAD_USER_NAME;
  }
  if (Contains(kinit_err, kKeyBadPassword) ||
      Contains(kinit_err, kKeyBadPassword2)) {
    LOG(ERROR) << "kinit failed - bad password";
    return ERROR_BAD_PASSWORD;
  }
  // Check both stderr and stdout here since any kinit error in the change-
  // password-workflow would otherwise be interpreted as 'password expired'.
  if (Contains(kinit_out, kKeyPasswordExpiredStdout) &&
      Contains(kinit_err, kKeyPasswordExpiredStderr)) {
    LOG(ERROR) << "kinit failed - password expired";
    return ERROR_PASSWORD_EXPIRED;
  }
  if (Contains(kinit_err, kKeyCannotResolve)) {
    LOG(ERROR) << "kinit failed - cannot resolve KDC realm";
    return ERROR_NETWORK_PROBLEM;
  }
  if (Contains(kinit_err, kKeyNoCrentialsCache)) {
    LOG(ERROR) << "kinit failed - no credentials cache found";
    return ERROR_NO_CREDENTIALS_CACHE_FOUND;
  }
  if (Contains(kinit_err, kKeyTicketExpired)) {
    LOG(ERROR) << "kinit failed - ticket expired";
    return ERROR_KERBEROS_TICKET_EXPIRED;
  }
  LOG(ERROR) << "kinit failed with exit code " << kinit_cmd.GetExitCode();
  return ERROR_KINIT_FAILED;
}

// In case klist failed, checks the output and returns appropriate error codes.
ErrorType GetKListError(const ProcessExecutor& klist_cmd) {
  DCHECK_NE(0, klist_cmd.GetExitCode());
  const std::string& klist_out = klist_cmd.GetStdout();
  const std::string& klist_err = klist_cmd.GetStderr();

  if (Contains(klist_err, kKeyNoCrentialsCache)) {
    LOG(ERROR) << "klist failed - no credentials cache found";
    return ERROR_NO_CREDENTIALS_CACHE_FOUND;
  }

  // Test the return value of klist -s. The command returns 1 if the TGT is
  // invalid and 0 otherwise. Does not print anything.
  const std::vector<std::string>& args = klist_cmd.GetArgs();
  if (klist_out.empty() && klist_err.empty() &&
      std::find(args.begin(), args.end(), "-s") != args.end()) {
    LOG(ERROR) << "klist failed - ticket expired";
    return ERROR_KERBEROS_TICKET_EXPIRED;
  }

  LOG(ERROR) << "klist failed with exit code " << klist_cmd.GetExitCode();
  return ERROR_KLIST_FAILED;
}

}  // namespace

TgtManager::TgtManager(scoped_refptr<base::SingleThreadTaskRunner> task_runner,
                       const PathService* path_service,
                       AuthPolicyMetrics* metrics,
                       const protos::DebugFlags* flags,
                       const JailHelper* jail_helper,
                       Anonymizer* anonymizer,
                       Path config_path,
                       Path credential_cache_path)
    : task_runner_(task_runner),
      paths_(path_service),
      metrics_(metrics),
      flags_(flags),
      jail_helper_(jail_helper),
      anonymizer_(anonymizer),
      config_path_(config_path),
      credential_cache_path_(credential_cache_path) {}

TgtManager::~TgtManager() {
  // Do a best-effort cleanup.
  base::DeleteFile(base::FilePath(paths_->Get(config_path_)),
                   false /* recursive */);
  base::DeleteFile(base::FilePath(paths_->Get(credential_cache_path_)),
                   false /* recursive */);

  // Note that the destuctor of |tgt_renewal_callback_| does not cancel.
  tgt_renewal_callback_.Cancel();
}

ErrorType TgtManager::AcquireTgtWithPassword(const std::string& principal,
                                             int password_fd,
                                             const std::string& realm,
                                             const std::string& kdc_ip) {
  realm_ = realm;
  kdc_ip_ = kdc_ip;
  is_machine_principal_ = IsMachine(principal);

  // Duplicate password pipe in case we'll need to retry kinit.
  base::ScopedFD password_dup(DuplicatePipe(password_fd));
  if (!password_dup.is_valid())
    return ERROR_LOCAL_IO;

  ProcessExecutor kinit_cmd({paths_->Get(Path::KINIT),
                             principal,
                             "-l",
                             kRequestedTgtValidityLifetime,
                             "-r",
                             kRequestedTgtRenewalLifetime});
  kinit_cmd.SetInputFile(password_fd);
  ErrorType error = RunKinit(&kinit_cmd, false /* propagation_retry */);
  if (error == ERROR_CONTACTING_KDC_FAILED) {
    LOG(WARNING) << "Retrying kinit without KDC IP config in the krb5.conf";
    kdc_ip_.clear();
    kinit_cmd.SetInputFile(password_dup.get());
    error = RunKinit(&kinit_cmd, false /* propagation_retry */);
  }

  // If it worked, re-trigger the TGT renewal task.
  if (error == ERROR_NONE && tgt_autorenewal_enabled_)
    UpdateTgtAutoRenewal();

  // If there was no error, assume that the Kerberos credential cache changed.
  if (error == ERROR_NONE)
    kerberos_files_dirty_ = true;

  // Trigger the files-changed signal.
  if (kerberos_files_dirty_ && !kerberos_files_changed_.is_null())
    kerberos_files_changed_.Run();
  kerberos_files_dirty_ = false;

  return error;
}

ErrorType TgtManager::AcquireTgtWithKeytab(const std::string& principal,
                                           Path keytab_path,
                                           bool propagation_retry,
                                           const std::string& realm,
                                           const std::string& kdc_ip) {
  realm_ = realm;
  kdc_ip_ = kdc_ip;
  is_machine_principal_ = IsMachine(principal);

  // Call kinit to get the Kerberos ticket-granting-ticket.
  ProcessExecutor kinit_cmd({paths_->Get(Path::KINIT),
                             principal,
                             "-k",
                             "-l",
                             kRequestedTgtValidityLifetime,
                             "-r",
                             kRequestedTgtRenewalLifetime});
  kinit_cmd.SetEnv(kKrb5KTEnvKey, kFilePrefix + paths_->Get(keytab_path));
  ErrorType error = RunKinit(&kinit_cmd, propagation_retry);
  if (error == ERROR_CONTACTING_KDC_FAILED) {
    LOG(WARNING) << "Retrying kinit without KDC IP config in the krb5.conf";
    kdc_ip_.clear();
    error = RunKinit(&kinit_cmd, propagation_retry);
  }

  // If it worked, re-trigger the TGT renewal task.
  if (error == ERROR_NONE && tgt_autorenewal_enabled_)
    UpdateTgtAutoRenewal();
  return error;
}

ErrorType TgtManager::GetKerberosFiles(KerberosFiles* files) {
  files->clear_krb5cc();
  files->clear_krb5conf();

  ErrorType error;
  std::string krb5cc;
  {
    // Note: The krb5cc is readable only by authpolicyd-exec.
    ScopedSwitchToSavedUid switch_scope;
    base::FilePath krb5cc_path(paths_->Get(credential_cache_path_));
    if (!base::PathExists(krb5cc_path))
      return ERROR_NONE;
    error = ReadFile(krb5cc_path, &krb5cc);
    if (error != ERROR_NONE)
      return error;
  }

  std::string krb5conf;
  base::FilePath krb5conf_path(paths_->Get(config_path_));
  error = ReadFile(krb5conf_path, &krb5conf);
  if (error != ERROR_NONE)
    return error;

  files->mutable_krb5cc()->assign(krb5cc.begin(), krb5cc.end());
  files->mutable_krb5conf()->assign(krb5conf.begin(), krb5conf.end());
  return ERROR_NONE;
}

void TgtManager::SetKerberosFilesChangedCallback(
    const base::Closure& callback) {
  kerberos_files_changed_ = callback;
}

void TgtManager::EnableTgtAutoRenewal(bool enabled) {
  if (tgt_autorenewal_enabled_ != enabled) {
    tgt_autorenewal_enabled_ = enabled;
    UpdateTgtAutoRenewal();
  }
}

ErrorType TgtManager::RenewTgt() {
  // kinit -R renews the TGT.
  ProcessExecutor kinit_cmd({paths_->Get(Path::KINIT), "-R"});
  ErrorType error = RunKinit(&kinit_cmd, false);

  // No matter if it worked or not, reschedule auto-renewal. We might be offline
  // and want to try again later.
  UpdateTgtAutoRenewal();
  return error;
}

ErrorType TgtManager::GetTgtLifetime(protos::TgtLifetime* lifetime) {
  // Check local file first before calling klist -s, since that would respond
  // ERROR_KERBEROS_TICKET_EXPIRED instead of ERROR_NO_CREDENTIALS_CACHE_FOUND.
  if (!base::PathExists(base::FilePath(paths_->Get(credential_cache_path_)))) {
    LOG(ERROR) << "GetTgtLifetime failed - no credentials cache found";
    return ERROR_NO_CREDENTIALS_CACHE_FOUND;
  }

  // Call klist -s to find out whether the TGT is still valid.
  {
    ProcessExecutor klist_cmd({paths_->Get(Path::KLIST),
                               "-s",
                               "-c",
                               paths_->Get(credential_cache_path_)});
    if (!jail_helper_->SetupJailAndRun(
            &klist_cmd, Path::KLIST_SECCOMP, TIMER_KLIST)) {
      return GetKListError(klist_cmd);
    }
  }

  // Now that we know the TGT is valid, call klist again (without -s) and parse
  // the output to get the TGT lifetime.
  {
    ProcessExecutor klist_cmd(
        {paths_->Get(Path::KLIST), "-c", paths_->Get(credential_cache_path_)});
    if (!jail_helper_->SetupJailAndRun(
            &klist_cmd, Path::KLIST_SECCOMP, TIMER_KLIST)) {
      return GetKListError(klist_cmd);
    }

    // Parse the output to find the lifetime. Enclose in a sandbox for security
    // considerations.
    ProcessExecutor parse_cmd({paths_->Get(Path::PARSER),
                               kCmdParseTgtLifetime,
                               SerializeFlags(*flags_)});
    parse_cmd.SetInputString(klist_cmd.GetStdout());
    if (!jail_helper_->SetupJailAndRun(
            &parse_cmd, Path::PARSER_SECCOMP, TIMER_NONE)) {
      LOG(ERROR) << "authpolicy_parser parse_tgt_lifetime failed with "
                 << "exit code " << parse_cmd.GetExitCode();
      return ERROR_PARSE_FAILED;
    }
    if (!lifetime->ParseFromString(parse_cmd.GetStdout())) {
      LOG(ERROR) << "Failed to parse TGT lifetime protobuf from string";
      return ERROR_PARSE_FAILED;
    }
    return ERROR_NONE;
  }
}

ErrorType TgtManager::RunKinit(ProcessExecutor* kinit_cmd,
                               bool propagation_retry) const {
  // Write configuration.
  ErrorType error = WriteKrb5Conf();
  if (error != ERROR_NONE)
    return error;

  // Set Kerberos credential cache and configuration file paths.
  kinit_cmd->SetEnv(kKrb5CCEnvKey, paths_->Get(credential_cache_path_));
  kinit_cmd->SetEnv(kKrb5ConfEnvKey, kFilePrefix + paths_->Get(config_path_));

  error = ERROR_NONE;
  const int max_tries = (propagation_retry ? kKinitMaxTries : 1);
  int tries, failed_tries = 0;
  for (tries = 1; tries <= max_tries; ++tries) {
    if (tries > 1 && kinit_retry_sleep_enabled_) {
      base::PlatformThread::Sleep(
          base::TimeDelta::FromSeconds(kKinitRetryWaitSeconds));
    }
    SetupKinitTrace(kinit_cmd);
    if (jail_helper_->SetupJailAndRun(
            kinit_cmd, Path::KINIT_SECCOMP, TIMER_KINIT)) {
      error = ERROR_NONE;
      break;
    }
    failed_tries++;
    OutputKinitTrace();
    error = GetKinitError(*kinit_cmd, is_machine_principal_);
    // If kinit fails because credentials are not propagated yet, these are
    // the error types you get.
    if (error != ERROR_BAD_USER_NAME && error != ERROR_BAD_MACHINE_NAME &&
        error != ERROR_BAD_PASSWORD) {
      break;
    }
  }
  metrics_->Report(METRIC_KINIT_FAILED_TRY_COUNT, failed_tries);
  return error;
}

ErrorType TgtManager::WriteKrb5Conf() const {
  std::string data = base::StringPrintf(kKrb5ConfData, realm_.c_str());
  if (!kdc_ip_.empty())
    data += base::StringPrintf(
        kKrb5RealmData, realm_.c_str(), kdc_ip_.c_str(), kdc_ip_.c_str());
  const base::FilePath krbconf_path(paths_->Get(config_path_));

  // Only set kerberos_files_dirty_ if the config data has actually changed.
  // Otherwise, the KerberosFilesChanged signal gets triggered way too often,
  // causing the krb5cc in Chrome to reset all the time.
  std::string prev_data;
  if (!base::ReadFileToStringWithMaxSize(
          krbconf_path, &prev_data, kKrb5FileSizeLimit) ||
      data != prev_data) {
    const int data_size = static_cast<int>(data.size());
    if (base::WriteFile(krbconf_path, data.c_str(), data_size) != data_size) {
      LOG(ERROR) << "Failed to write krb5 conf file '" << krbconf_path.value()
                 << "'";
      return ERROR_LOCAL_IO;
    }
    kerberos_files_dirty_ = true;
  }

  return ERROR_NONE;
}

void TgtManager::SetupKinitTrace(ProcessExecutor* kinit_cmd) const {
  if (!flags_->trace_kinit())
    return;
  const std::string& trace_path = paths_->Get(Path::KRB5_TRACE);
  {
    // Delete kinit trace file (must be done as authpolicyd-exec).
    ScopedSwitchToSavedUid switch_scope;
    if (!base::DeleteFile(base::FilePath(trace_path), false /* recursive */)) {
      LOG(WARNING) << "Failed to delete kinit trace file";
    }
  }
  kinit_cmd->SetEnv(kKrb5TraceEnvKey, trace_path);
}

void TgtManager::OutputKinitTrace() const {
  if (!flags_->trace_kinit())
    return;
  const std::string& trace_path = paths_->Get(Path::KRB5_TRACE);
  std::string trace;
  {
    // Read kinit trace file (must be done as authpolicyd-exec).
    ScopedSwitchToSavedUid switch_scope;
    if (!base::ReadFileToString(base::FilePath(trace_path), &trace))
      trace = "<failed to read>";
  }
  LogLongString("Kinit trace: ", trace, anonymizer_);
}

void TgtManager::UpdateTgtAutoRenewal() {
  // Cancel an existing callback if there is any.
  if (!tgt_renewal_callback_.IsCancelled())
    tgt_renewal_callback_.Cancel();

  if (tgt_autorenewal_enabled_) {
    // Find out how long the TGT is valid.
    protos::TgtLifetime lifetime;
    ErrorType error = GetTgtLifetime(&lifetime);
    if (error == ERROR_NONE && lifetime.validity_seconds() > 0) {
      if (lifetime.validity_seconds() >= lifetime.renewal_seconds()) {
        // If we TGT got renewed a lot and/or is not renewable, the validity
        // lifetime is bounded by the renewal lifetime.
        LOG(WARNING) << kTgtRenewalHeader << "TGT cannot be renewed anymore "
                     << lifetime;
      } else {
        // Trigger the renewal somewhere in the validity lifetime of the TGT.
        int delay_seconds = static_cast<int>(lifetime.validity_seconds() *
                                             kTgtRenewValidityLifetimeFraction);

        // Make sure we don't trigger excessively often in case the renewal
        // fails and we're getting close to the end of the validity lifetime.
        delay_seconds = std::max(delay_seconds, kMinTgtRenewDelaySeconds);

        LOG(INFO) << kTgtRenewalHeader << "Scheduling renewal in "
                  << FormatTimeDelta(delay_seconds) << " " << lifetime;

        tgt_renewal_callback_.Reset(
            base::Bind(&TgtManager::AutoRenewTgt, base::Unretained(this)));
        task_runner_->PostDelayedTask(
            FROM_HERE,
            tgt_renewal_callback_.callback(),
            base::TimeDelta::FromSeconds(delay_seconds));
      }
    } else if (error == ERROR_KERBEROS_TICKET_EXPIRED) {
      // Expiry is the most likely error, print a nice message.
      LOG(WARNING) << kTgtRenewalHeader << "TGT expired, reinitializing "
                   << "requires credentials";
    }
  }
}

void TgtManager::AutoRenewTgt() {
  LOG(INFO) << kTgtRenewalHeader << "Running scheduled TGT renewal";
  ErrorType error = RenewTgt();
  if (error == ERROR_NONE)
    LOG(INFO) << kTgtRenewalHeader << "Succeeded";
  else
    LOG(INFO) << kTgtRenewalHeader << "Failed with error " << error;
}

}  // namespace authpolicy
