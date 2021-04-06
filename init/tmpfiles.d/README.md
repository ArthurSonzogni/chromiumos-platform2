# tmpfiles.d Configuration Files

These `.conf` files define filesystem operations that are needed to setup paths.
This is commonly creating specific files and directories with specific
permissions and ownership prior to running a system daemon. For example an
upstart job with:

```bash
pre-start script
  mkdir -p /run/dbus
  chown messagebus:messagebus /run/dbus
  mkdir -p /var/lib/dbus
end script
```

Can be replaced with a `tmpfiles.d` file with:

```bash
d= /run/dbus 0755 messagebus messagebus
d= /var/lib/dbus 0755 root root
```

This configuration will take care of creating the listed paths with the correct
type, ownership, permissions, and SELinux labels. If the type is wrong (e.g. a
symlink instead of a directory) the path will be recreated. If the path already
exists with the wrong ownership or permissions they will be changed to match the
configuration. Remember the root-fs is read-only and uses verity for integrity
checking so you cannot create or change paths on it without building a new
image.

Also, tmpfiles.d checks to make sure symlinks in the parent directories paths do
not cross from lower privilege to higher privilege. Directories owned by root
are allowed to contain symlinks to directories owned by a different user id. If
the parent directory is not owned by root and the symlink points to a path owned
by a different user, it is treated as an unsafe transition. Currently, an unsafe
transition in a configured path will cause tmpfiles.d to fail with an error and
chromeos_startup will trigger a cleanup of the stateful partition.

This file should have the `.conf` extension and be installed to
`/usr/lib/tmpfiles.d` using `dotmpfiles` or `newtmpfiles` from
[tmpfiles.eclass]. For more information about the `conf` format see the
[upstream documentation](https://www.freedesktop.org/software/systemd/man/tmpfiles.d.html).

***note
**Note:**
The = action is still in the process of being upstreamed so upstream
documentation may not exist yet. It enables a feature that checks file types
for the path if it exists or the first existing parent path. If the type check
fails, the offending path is removed before executing the creation rule.

It does not apply to all rules, but specifically to ones that create or open
file-system objects as opposed to ones that change permissions for or delete a
glob path.
***

The preferred location of these config files in the source tree is a
subdirectory of the parent project named `tmpfiles.d`.

## Testing

Currently, an error in a tmpfiles.d config installed to /usr/lib/tmpfiles.d will
result in a stateful repair boot-loop. To avoid this when testing, copy your
config file to a different path and invoke it manually (or from an upstart job)
with:

```sh
/bin/systemd-tmpfiles --boot --create --remove --clean <your-tmpfiles-d.conf>
```

Generally, no errors are printed on success. If extra verbosity is desired, use:

```sh
export SYSTEMD_LOG_LEVEL=debug
```

## Common Obstacles

Here are some common errors with known resolutions.

### Paths Missing SELinux File Context Entries

If you see errors that resemble something like:

```
Failed to determine SELinux security context for /run/rsyslogd: Resource temporarily unavailable
Failed to create directory or subvolume "/run/rsyslogd": Resource temporarily unavailable
Failed to determine SELinux security context for /var/log/bluetooth.log: Resource temporarily unavailable
Unable to fix SELinux security context of /var/log/bluetooth.log (/var/log/bluetooth.log): Resource temporarily unavailable
```

The problem is usually missing SELinux file context entries. This occurs because
tmpfiles.d tries to restore the SELinux labels of the path. The restore
operation depends on having a file context entry. In some cases the path may
already have an existing label applied through a context transition policy rule,
but without the file context entry tmpfiles.d will still fail.

Here are example entries to resolve the above errors from
[chromeos_file_contexts]:

```
/run/rsyslogd             u:object_r:cros_run_rsyslogd:s0
/var/log/bluetooth.log    u:object_r:cros_var_log_bluetooth:s0
```

*** note
**Note:** the context label (e.g. `cros_var_log_bluetooth`) needs to be defined.
Most are located in [file.te].
***

More information about defining the SELinux policy can be found in the
[SELinux documentation].

[chromeos_file_contexts]: /sepolicy/file_contexts/chromeos_file_contexts
[file.te]: /sepolicy/policy/base/file.te
[SELinux documentation]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/security/selinux.md
[tmpfiles.eclass]: https://chromium.googlesource.com/chromiumos/overlays/portage-stable/+/HEAD/eclass/tmpfiles.eclass
