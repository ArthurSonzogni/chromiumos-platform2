# ErrorBase object

An error object with error information in it.

It supports multiple layers of error messages, custom contents, custom function and printable.

This would be helpful to convert the error into some actions and logging the error message.

## How to add a new error type?

1. The custom error object needs to inherit the `ErrorBaseObj`.
2. It needs to implement the constructor.
3. It needs to implement `ToReadableString` to convert the error object to a readable string.
4. It needs to implement `SelfCopy` to copy itself.
5. Optional: Define `CustomError` to simplify the usage.
6. Optional: Override the `CreateError` helper function to create some special error type, for example: `nullptr`.
7. Optional: Override the `CreateErrorWrap` helper function to create some special error.
8. Optional: Implement the move operator to add the support of caller info.

```C++
class CustomErrorObj : public ErrorBaseObj {
 public:
  explicit ErrorObj(const std::string& error_message)
      : error_message_(error_message) {}
  explicit ErrorObj(std::string&& error_message)
      : error_message_(std::move(error_message)) {}
  virtual ~ErrorObj() = default;

  hwsec_foundation::error::ErrorBase SelfCopy() const {
    return std::make_unique<ErrorObj>(error_message_);
  }

  std::string ToReadableString() const { return error_message_; }

 protected:
  ErrorObj(ErrorObj&&) = default;

 private:
  const std::string error_message_;
};
using CustomError = std::unique_ptr<CustomErrorObj>;

template <typename ErrorType,
          typename T,
          typename std::enable_if<
              std::is_same<ErrorType, CustomError>::value>::type* =
              nullptr,
          decltype(CustomErrorObj(
              std::forward<T>(std::declval<T&&>())))* = nullptr>
CustomError CreateError(T&& error_msg) {
  if (error_msg == "") {
      return nullptr;
  }
  return std::make_unique<CustomError>(std::forward<T>(error_msg));
}
```
