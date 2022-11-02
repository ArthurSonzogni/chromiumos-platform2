// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_PREPARE_TOKEN_H_
#define CRYPTOHOME_AUTH_BLOCKS_PREPARE_TOKEN_H_

#include <memory>
#include <utility>

#include <base/callback.h>
#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

// Token that represents an active prepared auth factor. The token can be used
// to terminate the factor, and should automatically do so upon destruction.
//
// Note to subclass implementers: you should include a TerminateOnDestruction
// member in your subclass, to ensure you get the correct destructor behavior.
class PreparedAuthFactorToken {
 public:
  // Standard callback for function that accept a token. The callback will be
  // passed either a valid token on success, or a not-OK status on failure.
  using Consumer = base::OnceCallback<void(
      CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>)>;

  // Tokens cannot be copied, since they represent a unique active factor. When
  // they are destroyed they will automatically terminate the factor, although
  // the Status of this termination is lost in that case.
  explicit PreparedAuthFactorToken(AuthFactorType auth_factor_type)
      : auth_factor_type_(auth_factor_type) {}
  PreparedAuthFactorToken(const PreparedAuthFactorToken&) = delete;
  PreparedAuthFactorToken& operator=(const PreparedAuthFactorToken&) = delete;
  virtual ~PreparedAuthFactorToken() = default;

  // The type of the auth factor that this token is used for.
  AuthFactorType auth_factor_type() const { return auth_factor_type_; }

  // Terminate the factor. Returns a status reporting any errors with the
  // termination process, but note that the factor is considered terminated
  // after the call regardless of the result. Subsequent calls to terminate will
  // do nothing and return OK.
  CryptohomeStatus Terminate() {
    if (!terminated_) {
      terminated_ = true;
      return TerminateAuthFactor();
    }
    return ::hwsec_foundation::status::OkStatus<error::CryptohomeError>();
  }

 protected:
  // Helpful RAII style class that will ensure that Terminate() is called upon
  // destruction. Subclasses should include this as their last member. Making it
  // the last member is important because you'll almost certainly want your
  // TerminateAuthFactor implementation to be called before any of the other
  // member variables are destroyed.
  class TerminateOnDestruction {
   public:
    explicit TerminateOnDestruction(PreparedAuthFactorToken& token)
        : token_(token) {}
    TerminateOnDestruction(const TerminateOnDestruction&) = delete;
    TerminateOnDestruction& operator=(const TerminateOnDestruction&) = delete;
    ~TerminateOnDestruction() { static_cast<void>(token_.Terminate()); }

   private:
    PreparedAuthFactorToken& token_;
  };

 private:
  // Override in subclasses to implement termination. Will be called exactly
  // once in the lifetime of the token.
  virtual CryptohomeStatus TerminateAuthFactor() = 0;

  bool terminated_ = false;
  const AuthFactorType auth_factor_type_;
};

// A traceable auth factor token implementation that can be used to track if
// termination (and destruction) of the token has occurred. This is mostly
// useful for testing.
class TrackedPreparedAuthFactorToken : public PreparedAuthFactorToken {
 public:
  // Type for tracking if TerminateAuthFactor() and/or the destructor were
  // called. The test token will set these to true when the corresponding
  // functions are called.
  struct WasCalled {
    bool terminate = false;
    bool destructor = false;
  };

  // Construct a tracked token, that will return the given status the first time
  // that TerminateAuthFactor is called and which will set the bits in the given
  // WasCalled object when termination or destruction occurs.
  //
  // The WasCalled object has to be provided by the user of this class, rather
  // than being in the class itself, because the token being destroyed would of
  // course also destroy any tracking stored internally. The flip side of this
  // is that the caller must ensure that the given WasCalled struct will outlive
  // the token.
  TrackedPreparedAuthFactorToken(AuthFactorType auth_factor_type,
                                 CryptohomeStatus status_to_return,
                                 WasCalled* was_called)
      : PreparedAuthFactorToken(auth_factor_type),
        status_to_return_(std::move(status_to_return)),
        was_called_(was_called),
        terminate_(*this) {}

  ~TrackedPreparedAuthFactorToken() override { was_called_->destructor = true; }

 private:
  CryptohomeStatus TerminateAuthFactor() override {
    was_called_->terminate = true;
    return std::move(status_to_return_);
  }

  CryptohomeStatus status_to_return_;
  WasCalled* was_called_;
  TerminateOnDestruction terminate_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_PREPARE_TOKEN_H_
