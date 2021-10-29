// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implement support for managing hibernate metadata.

use std::convert::TryFrom;
use std::io::{Read, Write};

use anyhow::{Context, Result};
use log::info;
use openssl::symm::{Cipher, Crypter, Mode};
use serde::{Deserialize, Serialize};
use sys_util::rand::{rand_bytes, Source};

use crate::diskfile::BouncedDiskFile;
use crate::hiberutil::HibernateError;

/// Magic value used to recognize a hibernate metadata struct.
const META_MAGIC: u64 = 0x6174654D72626948;

/// Version of the structure contents. Bump this up whenever the
/// structure changes.
const META_VERSION: u32 = 1;

// Define hibernate metadata flags.
/// This flag is set if the hibernate image is valid and ready to be resumed to.
pub const META_FLAG_VALID: u32 = 0x00000001;

/// This flag is set if the image has already been attempted for resume. When
/// this flag is set the VALID flag is cleared.
pub const META_FLAG_RESUME_STARTED: u32 = 0x00000002;

/// This flag is set if the image was fully loaded and a resume launch was
/// attempted.
pub const META_FLAG_RESUME_LAUNCHED: u32 = 0x00000004;

/// This flag is set if the image has already been resumed into, but the resume
/// attempt failed. The RESUMED flag will also be set.
pub const META_FLAG_RESUME_FAILED: u32 = 0x00000008;

/// This flag is set if the image is encrypted.
pub const META_FLAG_ENCRYPTED: u32 = 0x00000010;

/// Define the mask of all valid flags.
pub const META_VALID_FLAGS: u32 = META_FLAG_VALID
    | META_FLAG_RESUME_STARTED
    | META_FLAG_RESUME_LAUNCHED
    | META_FLAG_RESUME_FAILED
    | META_FLAG_ENCRYPTED;

/// Define the size of the hash field in the metadata.
pub const META_HASH_SIZE: usize = 32;

/// Define the size of the hibernate data symmetric encryption key.
pub const META_SYMMETRIC_KEY_SIZE: usize = 16;
pub const META_SYMMETRIC_IV_SIZE: usize = META_SYMMETRIC_KEY_SIZE;

/// Define the reserved size of the unencrypted public area.
const META_PUBLIC_SIZE: usize = 0x1000;

/// Define the size of the encrypted private area. Bump this up (and
/// bump the version) if PrivateHibernateMetadata outgrows it.
pub const META_PRIVATE_SIZE: usize = 0x1000;

/// Define the size of the asymmetric key pairs used to encrypt the hibernate
/// metadata.
pub const META_ASYMMETRIC_KEY_SIZE: usize = 32;

/// Define the software representation of the hibernate metadata.
pub struct HibernateMetadata {
    /// The size of the hibernate image data.
    pub image_size: u64,
    /// Flags. See META_FLAG_* definitions.
    pub flags: u32,
    /// Number of pages in the image's header and pagemap.
    pub pagemap_pages: u32,
    /// Hash of the header pages.
    pub header_hash: [u8; META_HASH_SIZE],
    /// Hibernate symmetric encryption key.
    pub data_key: [u8; META_SYMMETRIC_KEY_SIZE],
    /// Hibernate symmetric encryption IV (chosen randomly).
    pub data_iv: [u8; META_SYMMETRIC_IV_SIZE],
    /// The first byte of data, in plaintext. This is needed to coerce the kernel
    /// into doing its image allocation. Random IV used for metadata encryption.
    pub first_data_byte: u8,
    /// Public side of the ephemeral key pair used in Diffie-Hellman to derive
    /// the metadata key.
    pub meta_eph_public: [u8; META_ASYMMETRIC_KEY_SIZE],
    /// Random IV used for metadata encryption.
    meta_iv: [u8; META_SYMMETRIC_IV_SIZE],
    /// The not-yet-decrypted private data.
    private_blob: Option<[u8; META_PRIVATE_SIZE]>,
    /// The key used to decrypt private metadata.
    meta_key: Option<[u8; META_SYMMETRIC_KEY_SIZE]>,
    /// Define whether or not to save private data to disk anymore or not. This
    /// can be cleared when the metadata is only being written out as a debugging
    /// breadcrumb.
    save_private_data: bool,
}

/// Define the structure of the public hibernate metadata, which is written
/// out to disk unencrypted.
#[derive(Serialize, Deserialize, Debug)]
pub struct PublicHibernateMetadata {
    /// This must be set to META_MAGIC.
    magic: u64,
    /// This must be set to META_VERSION.
    version: u32,
    /// Number of pages in the image's header and pagemap.
    pagemap_pages: u32,
    /// The size of the hibernate image data.
    image_size: u64,
    /// Flags. See META_FLAG_* definitions.
    flags: u32,
    /// The first byte of data, needed to coerce the kernel into doing its big
    /// allocation.
    first_data_byte: u8,
    /// Public side of the ephemeral key pair used in Diffie-Hellman to derive
    /// the metadata key.
    meta_eph_public: [u8; META_ASYMMETRIC_KEY_SIZE],
    /// IV used for private portion of metadata.
    private_iv: [u8; META_SYMMETRIC_IV_SIZE],
}

/// Define the structure of the private hibernate metadata, which is written
/// out to disk encrypted.
#[derive(Serialize, Deserialize, Debug)]
pub struct PrivateHibernateMetadata {
    /// This must be set to META_VERSION.
    version: u32,
    /// Number of pages in the image's header and pagemap.
    pagemap_pages: u32,
    /// The size of the hibernate image data.
    image_size: u64,
    /// Flags. See META_FLAG_* definitions.
    flags: u32,
    /// Hibernate symmetric encryption key.
    data_key: [u8; META_SYMMETRIC_KEY_SIZE],
    /// Hibernate symmetric encryption IV (chosen randomly).
    data_iv: [u8; META_SYMMETRIC_IV_SIZE],
    /// Hash of the header pages.
    header_hash: [u8; META_HASH_SIZE],
}

impl HibernateMetadata {
    /// Create a new metadata structure. The data key, data IV, and metadata IV
    /// will be initialized with data from /dev/urandom.
    pub fn new() -> Result<Self> {
        let mut data_key = [0u8; META_SYMMETRIC_KEY_SIZE];
        Self::fill_random(&mut data_key)?;
        let mut data_iv = [0u8; META_SYMMETRIC_IV_SIZE];
        Self::fill_random(&mut data_iv)?;
        let mut meta_iv = [0u8; META_SYMMETRIC_IV_SIZE];
        Self::fill_random(&mut meta_iv)?;
        // Initialize the other keys with random junk as well to avoid bugs
        // where zeroed keys get used. These should never actually get used with
        // the random data (they'd be undecryptable if they were).
        let mut meta_eph_public = [0u8; META_ASYMMETRIC_KEY_SIZE];
        Self::fill_random(&mut meta_eph_public)?;
        Ok(Self {
            image_size: 0,
            flags: 0,
            pagemap_pages: 0,
            header_hash: [0u8; META_HASH_SIZE],
            data_key,
            data_iv,
            first_data_byte: 0,
            meta_iv,
            meta_eph_public,
            private_blob: None,
            meta_key: None,
            save_private_data: true,
        })
    }

    /// Load the metadata from disk, and populates the structure based on the
    /// public data. The private data is left in a blob.
    pub fn load_from_reader<R: Read>(mut reader: R) -> Result<Self> {
        // Read the public data area.
        let mut public_buf = vec![0u8; META_PUBLIC_SIZE];
        reader
            .read_exact(&mut public_buf)
            .context("Cannot read hibernate metadata")?;

        // Read the private data area.
        let mut private_buf = [0u8; META_PRIVATE_SIZE];
        reader
            .read_exact(&mut private_buf)
            .context("Cannot read hibernate metadata")?;

        // Deserialize the public metadata.
        let end = public_buf
            .iter()
            .position(|&b| b == b'\0')
            .context("Could not find trailing null in public metadata")?;
        let public_data: PublicHibernateMetadata = serde_json::from_slice(&public_buf[..end])
            .context("Could not deserialize public metadata")?;
        let mut metadata = Self::try_from(public_data)?;
        // Plunk in the private blob for decryption later.
        metadata.private_blob = Some(private_buf);
        Ok(metadata)
    }

    /// Set the key used to encrypt/decrypt the private metadata.
    pub fn set_metadata_key(&mut self, key: [u8; META_SYMMETRIC_KEY_SIZE]) {
        self.meta_key = Some(key);
    }

    /// Decrypt the private metadata contents via a key set previously by
    /// set_metadata_key(), and populate it into the current object.
    pub fn load_private_data(&mut self) -> Result<()> {
        if self.meta_key.is_none() {
            return Err(HibernateError::MetadataError(
                "Cannot load private data without meta key".to_string(),
            ))
            .context("Cannot load private metadata");
        }

        // Decrypt the private data.
        let cipher = Cipher::aes_128_cbc();
        let mut crypter = Crypter::new(
            cipher,
            Mode::Decrypt,
            &self.meta_key.unwrap(),
            Some(&self.meta_iv),
        )
        .unwrap();
        crypter.pad(true);
        let mut private_buf = vec![0u8; META_PRIVATE_SIZE + cipher.block_size()];
        let mut decrypt_size = crypter
            .update(&self.private_blob.unwrap(), &mut private_buf)
            .context("Failed to decrypt private data")?;

        decrypt_size += crypter
            .finalize(&mut private_buf[decrypt_size..])
            .context("Failed to decrypt private metadata")?;

        if decrypt_size != META_PRIVATE_SIZE - 1 {
            return Err(HibernateError::MetadataError(format!(
                "Private metadata was {:x?} bytes, expected at least {:x?}",
                decrypt_size,
                META_PRIVATE_SIZE - 1
            )))
            .context("Cannot load private metadata");
        }

        let end = private_buf
            .iter()
            .position(|&b| b == b'\0')
            .context("Could not find trailing null in private metadata")?;
        let private_data: PrivateHibernateMetadata = serde_json::from_slice(&private_buf[..end])
            .context("Could not deserialize private data")?;
        self.apply_private_data(&private_data)
    }

    /// Apply private metadata info from a given C struct into the current
    /// object.
    fn apply_private_data(&mut self, privdata: &PrivateHibernateMetadata) -> Result<()> {
        if privdata.version != META_VERSION {
            return Err(HibernateError::MetadataError(format!(
                "Invalid private metadata version: {:x?}, expected {:x?}",
                privdata.version, META_VERSION
            )))
            .context("Cannot apply private metadata");
        }

        if self.image_size != privdata.image_size {
            return Err(HibernateError::MetadataError(format!(
                "Mismatch in public private image size: {:x?} vs {:x?}",
                privdata.image_size, self.image_size
            )))
            .context("Cannot apply private metadata");
        }

        if self.pagemap_pages != privdata.pagemap_pages {
            return Err(HibernateError::MetadataError(format!(
                "Mismatch in pagemap count: {:x?} vs {:x?}",
                privdata.pagemap_pages, self.pagemap_pages
            )))
            .context("Cannot apply private metadata");
        }

        self.header_hash = privdata.header_hash;
        self.data_key = privdata.data_key;
        self.data_iv = privdata.data_iv;
        self.flags = privdata.flags;
        Ok(())
    }

    /// Save the current metadata contents to disk. If dont_save_private_data()
    /// has been called, then only the public portions are saved, and the
    /// private portion is zeroed out. This is useful on resume when the caller
    /// wants to update flags and clear the private area.
    pub fn write_to_disk(&self, disk_file: &mut BouncedDiskFile) -> Result<()> {
        let mut buf = vec![0u8; META_PUBLIC_SIZE + META_PRIVATE_SIZE];

        // Check the flags being written in case somebody added a flag and
        // forgot to add it to the valid mask.
        if (self.flags & !META_VALID_FLAGS) != 0 {
            return Err(HibernateError::MetadataError(format!(
                "Invalid flags: {:x?}, valid mask {:x?}",
                self.flags, META_VALID_FLAGS
            )))
            .context("Cannot save hibernate metadata");
        }

        let public_data = self.build_public_data(self.save_private_data)?;
        let serialized_public_string =
            serde_json::to_string(&public_data).context("Could not serialize public data")?;
        let serialized_public = serialized_public_string.as_bytes();
        let public_len = serialized_public.len();
        info!("Public data size: {}/{}", public_len, META_PUBLIC_SIZE);

        assert!(
            public_len < META_PUBLIC_SIZE,
            "serialized public data {} doesn't fit in {}",
            public_len,
            META_PUBLIC_SIZE
        );

        buf[0..public_len].copy_from_slice(serialized_public);
        if self.save_private_data {
            let private = self.build_private_buffer()?;
            let private_len = private.len();
            let end = META_PUBLIC_SIZE + private_len;
            buf[META_PUBLIC_SIZE..end].copy_from_slice(&private);
        }

        disk_file
            .write_all(&buf[..])
            .context("Cannot write hibernate metadata")?;

        Ok(())
    }

    /// Stop including the private metadata when saving to disk. This is useful
    /// upon resume when we want to update the flags, but there will be no more
    /// resume attempts with this image (because the attempt is in progress), so
    /// the private metadata can be cleared.
    pub fn dont_save_private_data(&mut self) {
        self.save_private_data = false;
    }

    /// Create the public C struct from the current object contents.
    fn build_public_data(&self, include_private: bool) -> Result<PublicHibernateMetadata> {
        let private_iv = if include_private {
            self.meta_iv
        } else {
            [0u8; META_SYMMETRIC_IV_SIZE]
        };

        let meta_eph_public = if include_private {
            self.meta_eph_public
        } else {
            [0u8; META_ASYMMETRIC_KEY_SIZE]
        };

        Ok(PublicHibernateMetadata {
            magic: META_MAGIC,
            version: META_VERSION,
            pagemap_pages: self.pagemap_pages,
            image_size: self.image_size,
            flags: self.flags,
            first_data_byte: self.first_data_byte,
            meta_eph_public,
            private_iv,
        })
    }

    /// Construct the encrypted private buffer area.
    fn build_private_buffer(&self) -> Result<[u8; META_PRIVATE_SIZE]> {
        let private_data = PrivateHibernateMetadata {
            version: META_VERSION,
            pagemap_pages: self.pagemap_pages,
            image_size: self.image_size,
            flags: self.flags,
            data_key: self.data_key,
            data_iv: self.data_iv,
            header_hash: self.header_hash,
        };

        let cipher = Cipher::aes_128_cbc();
        let serialized_private_string =
            serde_json::to_string(&private_data).context("Could not serialize private data")?;
        // Encrypt a fixed number of bytes regardless of the serialized size.
        let mut serialized_private = [0u8; META_PRIVATE_SIZE];
        let serialized_private_bytes = serialized_private_string.as_bytes();
        let serialized_bytes_length = serialized_private_bytes.len();

        // Only N-1 bytes are going to be encrypted, so ensure the serialized
        // data fits in there.
        assert!(serialized_bytes_length < META_PRIVATE_SIZE);

        serialized_private[..serialized_bytes_length].copy_from_slice(serialized_private_bytes);
        if self.meta_key.is_none() {
            return Err(HibernateError::MetadataError(
                "Cannot build private metadata without meta key".to_string(),
            ))
            .context("Cannot build private metadata");
        }

        // Prepare to encrypt it into the buffer.
        let mut crypter = Crypter::new(
            cipher,
            Mode::Encrypt,
            &self.meta_key.unwrap(),
            Some(&self.meta_iv),
        )
        .unwrap();
        crypter.pad(true);
        // Crypter demands that the output must be one block bigger than the
        // input.
        let mut ciphertext = vec![0u8; META_PRIVATE_SIZE + cipher.block_size()];
        // Encrypt N - 1 bytes, so padding brings us up to N.
        let mut encrypt_size = crypter
            .update(
                &serialized_private[..META_PRIVATE_SIZE - 1],
                &mut ciphertext,
            )
            .context("Cannot encrypt private metadata")?;

        encrypt_size += crypter
            .finalize(&mut ciphertext[encrypt_size..])
            .context("Cannot encrypt private metadata")?;

        assert!(
            encrypt_size == META_PRIVATE_SIZE,
            "Encrypted {} bytes, expected {}",
            encrypt_size,
            META_PRIVATE_SIZE
        );

        // Copy back into a correctly sized buffer and return that.
        serialized_private.copy_from_slice(&ciphertext[..META_PRIVATE_SIZE]);
        Ok(serialized_private)
    }

    /// Fill a buffer with random bytes, given an open file to /dev/urandom.
    fn fill_random(buf: &mut [u8]) -> Result<()> {
        rand_bytes(buf, Source::Pseudorandom)
            .context("Cannot get random bytes for hibernate metadata")?;
        Ok(())
    }
}

impl TryFrom<PublicHibernateMetadata> for HibernateMetadata {
    type Error = anyhow::Error;

    fn try_from(pubdata: PublicHibernateMetadata) -> std::result::Result<Self, Self::Error> {
        if pubdata.magic != META_MAGIC {
            return Err(HibernateError::MetadataError(format!(
                "Invalid metadata magic: {:x?}, expected {:x?}",
                pubdata.magic, META_MAGIC
            )))
            .context("Cannot load hibernate metadata");
        }

        if pubdata.version != META_VERSION {
            return Err(HibernateError::MetadataError(format!(
                "Invalid public metadata version: {:x?}, expected {:x?}",
                pubdata.version, META_VERSION
            )))
            .context("Cannot loada hibernate metadata");
        }

        if (pubdata.flags & !META_VALID_FLAGS) != 0 {
            return Err(HibernateError::MetadataError(format!(
                "Invalid flags: {:x?}, valid mask {:x?}",
                pubdata.flags, META_VALID_FLAGS
            )))
            .context("Cannot load hibernate metadata");
        }

        Ok(Self {
            image_size: pubdata.image_size,
            flags: pubdata.flags,
            pagemap_pages: pubdata.pagemap_pages,
            header_hash: [0u8; META_HASH_SIZE],
            data_key: [0u8; META_SYMMETRIC_KEY_SIZE],
            data_iv: [0u8; META_SYMMETRIC_IV_SIZE],
            first_data_byte: pubdata.first_data_byte,
            meta_iv: pubdata.private_iv,
            meta_key: None,
            meta_eph_public: pubdata.meta_eph_public,
            private_blob: None,
            save_private_data: true,
        })
    }
}
