# foomatic_shell: mini-shell used by foomatic-rip

This is a simple mini-shell that is used by `foomatic-rip` to execute small
scripts included in some PPD files.

Some PPD files from foomatic provides small shell scripts that must be run in
order to process documents sent to a printer. The script is often customized in
runtime. This approach is quite flexible but also introduces security
vulnerability. Originally, `foomatic-rip` uses default OS shell to execute the
script. In ChromeOS, `foomatic-rip` calls `foomatic_shell` instead (from this
package).

`foomatic_shell` executes given shell script in controlled environment to
mitigate the security risk. It supports pipes and backticks operator
(generating command by a subshell). Only very limited set of commands is
allowed, `foomatic_shell` enforces also some restrictions on command line
parameters.

## Appendix: FOOMATIC_VERIFY_MODE

When the environment variable `FOOMATIC_VERIFY_MODE` is set,
`foomatic_shell` goes into no-op mode. It carries out command
verification as normal but does not run the overall pipeline. For
example, [this environment variable is set in the printer.TestPPDs tast
test.][tast-foomatic-verify-mode].

[tast-foomatic-verify-mode]: https://chromium.googlesource.com/chromiumos/platform/tast-tests/+/HEAD/src/chromiumos/tast/local/bundles/cros/printer/test_ppds.go
