# Kerberos

This directory contains the Kerberos service, which provides functionality for
getting and managing Kerberos tickets (including auto-renewing tickets). It is
used by Chrome to provide Kerberos Single-SignOn (SSO). Think of it as a fancy
 wrapper around kinit, kpasswd and klist.

The service is started by Chrome on demand, e.g. when the user navigates to the
Kerberos Accounts settings page or when accounts are added by the
KerberosAccounts user policy. Therefore, the service runs within a user session
only.

Note that Chrome only uses this service if the kerberos.enabled pref is
enabled. The KerberosEnabled policy maps to that pref. As a consequence,
Kerberos Service is restricted to managed devices, for the time being.
