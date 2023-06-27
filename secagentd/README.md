# Secagentd

## Overview

Secagentd is a daemon responsible for detecting and reporting security related
events through ERP (Encrypted Reporting Pipeline) for forensic analysis.

It only works on Linux Kernel >= 5.10, in which the Berkeley Packet Filter
syscalls are available.

## Logging

Secagentd logs are located in /var/log/secagentd.log.
