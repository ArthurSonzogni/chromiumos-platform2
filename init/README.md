# CrOS Upstart configuration

This document attempts to address the most common questions when adding
an Upstart configuration file to CrOS.

## Where should your .conf file go?

Ideally not in this directory.

It is far better if your Upstart configuration file accompany your source,
either along with the source code under src/platform2/<project>/init/ or
src/platform or src/third_party if appropriate, or in the files/ directory
alongside your ebuild in src/third_party/chromiumos-overlay.

## What should the 'start on'/'stop on' lines be?

> See [CrOS User-Land Boot Design](https://www.chromium.org/chromium-os/chromiumos-design-docs/boot-design/)
> for more details.

The majority of services will use:

```
start on starting system-services
stop on stopping system-services
```

"System Services" can expect the standard virtual filesystems to be
mounted, the stateful partition to be writable and services such as udev
and D-Bus to be running.

A special case is if your service should also be run in the case of the
UI failing to come up, this is particularly important for services that
would aid debugging (such as the SSH server on test images). These
services may assume the same dependencies and may instead use:

```
start on starting failsafe
stop on stopping failsafe
```

In both cases your service will not be started until the login prompt
is visible, so doesn't hold up the boot.

Some services might need to hold up the boot; you should think carefully
about this, even if you only add 100ms to the boot time, if ten people
do that we slip from 8s to 9s. These services can still depend on the
standard virtual filesystems to be mounted, the stateful partition to be
writable but can only depend on udev to be running - no other service.
They may use:

```
start on starting boot-services
stop on stopping boot-services
```

The UI itself is one of these services.

On the other end of the spectrum, you might have something to run after
the boot; for example because it examines log files and soforth. This
can be run with:

```
start on started system-services
```

Note the use of 'started' rather than 'starting', that's important!

## Should I put 'task' in my configuration?

Probably not.

To illustrate what 'task' does, here is an example configuration file:

```
start on starting boot-services
script
  :
end script
```

Note that this has no 'stop on' line, this script finishes by itself and
should not be interrupted; and only has a single 'script' block without
any pre-start, post-stop, etc. If your configuration file does not look
like this, you should not use 'task'.

When the boot-services milestone is reached, your script will begin
running alongside all other boot-services starting up. Your script might
take longer than that, and the system-services may begin starting too.

If this is not what you want, because your script does something that
all system services are likely to depend on, then 'task' may be
appropriate. But it may be better to have the other jobs depend on your
script finishing with 'and stopped your-script-name'.

Finally you should be aware that 'task' only affects the 'starting'
event; if your script is 'start on started system-services' then 'task'
has no effect so should not be used.

## Which of 'script', 'pre-start', 'exec', etc. should I use?

The most common case is that you are defining a service, but need to do
some setup work before and cleanup work after. In which case your
configuration file would look like:

```
pre-start script
  # setup work here
end script

exec # daemon command-line here

post-stop script
  # cleanup work here
end script
```

You should omit the pre-start or post-stop script if you're not using
them.

Another common case is where the daemon takes a little more to get going
than just exec'ing a path with arguments, in which case you'd want to
use 'script' instead of 'exec'. A good pattern is:

```
script
  # setup work here
  # work needed to get going here

  # daemon command-line here, forcing it to stay in the foreground

  # clean-up work here
end script
```

In this case we need the daemon to stay in the foreground, otherwise the
clean-up work would happen while it's running. You cannot use the 'expect'
stanza with this style of configuration.

There's also a middle-ground:

```
script
  # setup work here
  # work needed to get going here

  exec # daemon command-line here, but again in the foreground
end script

post-stop script
  # cleanup work here
end script
```

This merges the 'pre-start' into the 'script' but keeps the 'post-stop'
separate. Note the use of 'exec' for the daemon, this ensures that it
has the same PID as the script that ran it, which is worthwhile. You
also cannot use 'expect' with this style.

Finally there's another style of configuration that is sometimes useful;
this is for when you have no need of a process running but only need to
perform work on startup and on shutdown - for example changing settings.
This looks like:

```
pre-start script
  # work on startup here
end script

post-stop script
  # work on shutdown here
end script
```

Upstart fully supports this, and it's known upstream as the "state
pattern". You may also see the "run once pattern" which consists
only of a 'pre-start'.
