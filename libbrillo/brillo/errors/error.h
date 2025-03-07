// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_ERRORS_ERROR_H_
#define LIBBRILLO_BRILLO_ERRORS_ERROR_H_

#include <memory>
#include <string>
#include <string_view>

#include <base/compiler_specific.h>
#include <base/location.h>
#include <brillo/brillo_export.h>

namespace brillo {

class Error;  // Forward declaration.

using ErrorPtr = std::unique_ptr<Error>;

class BRILLO_EXPORT Error {
 public:
  virtual ~Error() = default;

  // Creates an instance of Error class and logs the error.
  static ErrorPtr Create(const base::Location& location,
                         std::string_view domain,
                         std::string_view code,
                         std::string_view message);
  static ErrorPtr Create(const base::Location& location,
                         std::string_view domain,
                         std::string_view code,
                         std::string_view message,
                         ErrorPtr inner_error);

  // Creates an instance of Error class without logging.
  static ErrorPtr CreateNoLog(const base::Location& location,
                              std::string_view domain,
                              std::string_view code,
                              std::string_view message,
                              ErrorPtr inner_error);

  // If |error| is not nullptr, creates another instance of Error class,
  // initializes it with specified arguments and adds it to the head of
  // the error chain pointed to by |error|.
  static void AddTo(ErrorPtr* error,
                    const base::Location& location,
                    std::string_view domain,
                    std::string_view code,
                    std::string_view message);
  // Same as the Error::AddTo above, but allows to pass in a printf-like
  // format string and optional parameters to format the error message.
  PRINTF_FORMAT(5, 6)
  static void AddToPrintf(ErrorPtr* error,
                          const base::Location& location,
                          std::string_view domain,
                          std::string_view code,
                          const char* format,
                          ...);

  // Clones error with all inner errors.
  ErrorPtr Clone() const;

  // Returns the error domain, code and message
  const std::string& GetDomain() const { return domain_; }
  const std::string& GetCode() const { return code_; }
  const std::string& GetMessage() const { return message_; }

  // Returns the location of the error in the source code.
  const base::Location& GetLocation() const { return location_; }

  // Checks if this or any of the inner errors in the chain has the specified
  // error domain.
  bool HasDomain(std::string_view domain) const;

  // Checks if this or any of the inner errors in the chain matches the
  // specified error domain and code.
  bool HasError(std::string_view domain, std::string_view code) const;

  // Gets a pointer to the inner error, if present. Returns nullptr otherwise.
  const Error* GetInnerError() const { return inner_error_.get(); }

  // Gets a pointer to the first error occurred.
  // Returns itself if no inner error are available.
  const Error* GetFirstError() const;

  // Finds an error object of particular domain in the error chain stating at
  // |error_chain_start|. Returns the a pointer to the first matching error
  // object found.
  // Returns nullptr if no match is found.
  // This method is safe to call on a nullptr |error_chain_start| in which case
  // the result will also be nullptr.
  static const Error* FindErrorOfDomain(const Error* error_chain_start,
                                        std::string_view domain);
  // Finds an error of particular domain with the given code in the error chain
  // stating at |error_chain_start|. Returns the pointer to the first matching
  // error object.
  // Returns nullptr if no match is found or if |error_chain_start| is nullptr.
  static const Error* FindError(const Error* error_chain_start,
                                std::string_view domain,
                                std::string_view code);

 protected:
  // Constructor is protected since this object is supposed to be
  // created via the Create factory methods.
  Error(const base::Location& location,
        std::string_view domain,
        std::string_view code,
        std::string_view message,
        ErrorPtr inner_error);
  Error(const Error&) = delete;
  Error& operator=(const Error&) = delete;

  // Error domain. The domain defines the scopes for error codes.
  // Two errors with the same code but different domains are different errors.
  std::string domain_;
  // Error code. A unique error code identifier within the given domain.
  std::string code_;
  // Human-readable error message.
  std::string message_;
  // Error origin in the source code.
  // TODO(crbug.com/980935): Consider dropping this.
  base::Location location_;
  // Pointer to inner error, if any. This forms a chain of errors.
  ErrorPtr inner_error_;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_ERRORS_ERROR_H_
