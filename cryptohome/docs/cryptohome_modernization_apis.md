# Cryptohome Modernization APIs Reference

Authors: hardikgoyal@chromium.org, kerrnel@chromium.org

## Objective

This document describes the external behavior of the new APIs introduced for the
cryptohome modernization project. These APIs are being implemented as of
January 2022, and expected to be complete by June 2022.

## Background

These APIs cover three large areas of functionality.

1.  **Storage** - The storage APIs create a user's backing store on disk, and
    mount the decrypted files using supplied credentials.
2.  **AuthSessions** - AuthSessions represent a user in cryptohome, and allow
    multiple API calls to work with an authenticated user. Rather than passing
    the credentials to each API call, the credential is passed once to
    Authenticate, and the unique auth session ID is passed to each other call.
3.  **AuthFactors** - AuthFactors represent the various types of credentials
    used on Chrome OS. The simplest is the password. There are also smart cards,
    Android phones (SmartLock), PINs, etc.

### Backing Storage Information

There are two separate backing key stores on Chrome OS. VaultKeyset is the
legacy keystore, soon to be replaced by UserSecretStash. A user has a copy of a
VaultKeyset per credential, making it hard to scale these key stores.
UserSecretStash is a universal key store which each credential wraps.

## Storage APIs

The user storage APIs are now split into two operations: Create and Prepare,
with Create being available for persistent users. All the users (Guest,
Ephemeral and Persistent) will call Prepare<type>Vault. The Prepare<type> calls
used to be called Mount, but Prepare better reflects that the call is doing many
things (setting up device mappers, registering file system keys, bind mounting)
and not only a mount.

Here is more about the APIs:

1.  **PrepareGuestVault** - This request is intended to happen when a user wants
    to login to ChromeOS as a guest.
    -   *Input*: None
    -   *Reply*:
                  - CryptohomeErrorCode - Returns if any error occurred.
                  - sanitized_username: A hash of the username, which is part of
                    the path of the mounted homedir.
2.  **PrepareEphemeralVault** - This request is intended when a policy (either
    device or enterprise) has enabled ephemeral users. An ephemeral user is created
    in a memory filesystem only and is never actually persisted to disk.
    -   *Input*: auth_session_id
    -   *Reply*:
                  - CryptohomeErrorCode - Returns if any error occurred.
                  - sanitized_username: A hash of the username, which is part of
                    the path of the mounted homedir.
3.  **CreatePersistentUser** - This will create user directories needed to store
    keys and download policies. This will usually be called when a new user is
   registering.
    -   *Input*: auth_session_id
    -   *Reply*: CryptohomeErrorCode - Returns if any error occurred.
4.  **PreparePersistentVault** - This makes available user directories for them
    to use.
    -   *Input*:
        -   auth_sesion_id: AuthSession Id of an Authenticated AuthSession.
        -   block_ecryptfs: Whether ecryptfs mount should be prohibited.
        -   encryption_type - Expected encryption_type.
    -   *Reply*:
                  - CryptohomeErrorCode - Returns if any error occurred.
                  - sanitized_username: A hash of the username, which is part of
                    the path of the mounted homedir.
5.  **PrepareVaultForMigration** - Mounts vault in order to perform the
    migration of encryption. StartMigration can work only with the vault
    mounted with PrepareVaultForMigration.
    -   *Input*:
        -   auth_session_id: AuthSession Id of an Authenticated AuthSession.
    -   *Reply*:
        - CryptohomeErrorCode - Returns if any error occurred.
        - sanitized_username: A hash of the username, which is part of
          the path of the mounted homedir.

## AuthSession APIs

These are the APIs that AuthSession supports:

1. **StartAuthSession** - This will start an AuthSession for a given user.
    - *Input*: - Account_id - user’s username
      - flags - These determine how an AuthSession is configured. There is
  only one flag configured as of now, which is for ephemeral users.
    - *Reply*: - CryptohomeErrorCode - Returns if any error occurred.
      - Auth_session_id - This will be used to identify AuthSession after this
      call.
      - user_exists - Returns if the user exists.
      - map<string, cryptohome.KeyData> - Map to return label and public key
      data for the available keysets.
    - *Note*: - By default this AuthSession is valid for 5 minutes from the time the
      session reaches an Authenticated State. ExtendAuthSession can be called to extend
      the session or StartAuthSession to start over.
2. **InvalidateAuthSession** - This call is used to invalidate an AuthSession
once the need for one no longer exists.
    - *Input*: auth_session_id - AuthSession to invalidate.
    - *Reply*: CryptohomeErrorCode - Returns if there is any error.
3. **ExtendAuthSession** - This call is used to extend the duration of
AuthSession that it should be valid for.
    - *Input*:
      - auth_session_id - AuthSession that needs extending.
      - extend_duration - The number of seconds to extend the AuthSession by.
      If nothing is provided, this would default to 60 seconds.
    - *Reply*: Cryptohome Error Code

## AuthFactor APIs

Here are the APIs that are used to work with AuthFactors .
1. **AddAuthFactor** - This call adds an AuthFactor for a user. The call goes
through an authenticated AuthSession.
    - *Input*: - auth_session_id - AuthSession used to identify users.
    The AuthSession needs to be authenticated for this call to be a success.
      - auth_factor - AuthFactor information about the new factor to be added.
      - auth_input - This is set if any input is required for the AuthFactor,
      such as text for a Password AuthFactor.
    - *Reply*: CryptohomeErrorCode - Returns if there is any error
2. **AuthenticateAuthFactor** - This will Authenticate an existing AuthFactor.
This call will authenticate an AuthSession.
    - *Input*:
      - auth_session_id - AuthSession used to identify users. The AuthSession
      needs to be authenticated for this call to be a success.
      - auth_factor_label - The label that will be used to identify an
      AuthFactor to authenticate.
      - auth_input - This is set if any input is required for the AuthFactor,
      such as text for a PasswordAuthFactor.
   - *Reply*: CryptohomeErrorCode - Returns if there is any error
3. **UpdateAuthFactor** - This call will be used in the case of a user wanting
to update an AuthFactor. (E.g. Changing pin or password).
    - *Input*:
      -auth_session_id - AuthSession used to identify users. The AuthSession
      needs to be authenticated for this call to be a success.
      - old_auth_factor_label - The label that will be used to identify an
      AuthFactor to update.
      - auth_factor - AuthFactor information about the new factor that will
      replace the existing.
      - auth_input - This is set if any input is required for the AuthFactor,
      such as text for a Password AuthFactor.
    - *Reply* - CryptohomeErrorCode - Returns if there is any error
4. **RemoveAuthFactor** - This is called when a user wants to remove an
AuthFactor.
    - *Input*:
      - auth_session_id - AuthSession used to identify users. The AuthSession
      needs to be authenticated for this call to be a success.
      - auth_factor_label - The label that will be used to identify an
      AuthFactor to remove.
    - *Reply* - CryptohomeErrorCode - Returns if there is any error

## Order of Operations

### Add New User to Chromebook

1.  `StartAuthSession` – Chrome initiates an AuthSession. If the user does not
    exist already, this will come back in an authenticated state.
2.  `CreatePersistentUser`
3.  `PreparePersistentVault` - At this point it is safe to fetch any userpolicy.
4.  `AddAuthFactor`
    -   Upon adding the first AuthFactor, cryptohome will save the USS to disk.
        If there is a crash before this call is successful, the user is not
        persisted.
    -   This can be called multiple times. If there is a need
        `ExtendAuthSession` should be used.
5.  `InvalidateAuthSession`

### Chrome Sign in an Existing User

1.  `StartAuthSession` – Chrome initiates an AuthSession. This time
    `user_exists` should return `true` and AuthSession will not start in an
    authenticated state.
2.  `AuthenticateAuthFactor`
3.  If this call is a success, then move to the next step.
4.  `PreparePersistentVault`
5.  At this point it is safe to fetch any userpolicy.
6.  `InvalidateAuthSession`

### Chrome Adds an AuthFactor for an Existing User

1.  `StartAuthSession` – Chrome initiates an AuthSession. This time
    `user_exists` should return `true` and AuthSession will not start in an
    authenticated state.
2.  `AuthenticateAuthFactor`
    -   If this call is a success, then move to the next step.
3.  `AddAuthFactor`
    -   This can be called multiple times. If there is a need
        `ExtendAuthSession` should be used.
4.  `InvalidateAuthSession`

### Chrome starts a Guest Session
There will not be any requirement for AuthSession here as this account is totally temporary.

1.  `PrepareGuestVault`

### Chrome starts an Ephemeral User Session

1.  `StartAuthSession` – Chrome initiates an AuthSession.
2.  `PrepareEpehemeralVault`
3.  `AddAuthFactor`
    -   This is in case of managed guest session (adding a temporary pin).
4.  `InvalidateAuthSession`

Note that if the global ephemeral policy is enabled, Chrome will need to remove
any persistent users present.
