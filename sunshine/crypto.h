//
// Created by loki on 6/1/19.
//

#ifndef SUNSHINE_CRYPTO_H
#define SUNSHINE_CRYPTO_H

#include <cassert>
#include <array>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/rand.h>

#include "utility.h"

namespace crypto {
struct creds_t {
  std::string x509;
  std::string pkey;
};
constexpr std::size_t digest_size = 256;

void md_ctx_destroy(EVP_MD_CTX *);

using sha256_t = std::array<std::uint8_t, SHA256_DIGEST_LENGTH>;

using aes_t = std::array<std::uint8_t, 16>;
using x509_t = util::safe_ptr<X509, X509_free>;
using x509_store_t = util::safe_ptr<X509_STORE, X509_STORE_free>;
using x509_store_ctx_t = util::safe_ptr<X509_STORE_CTX, X509_STORE_CTX_free>;
using cipher_ctx_t = util::safe_ptr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>;
using md_ctx_t = util::safe_ptr<EVP_MD_CTX, md_ctx_destroy>;
using bio_t = util::safe_ptr<BIO, BIO_free_all>;
using pkey_t = util::safe_ptr<EVP_PKEY, EVP_PKEY_free>;

sha256_t hash(const std::string_view &plaintext);
aes_t gen_aes_key(const std::array<uint8_t, 16> &salt, const std::string_view &pin);

x509_t x509(const std::string_view &x);
pkey_t pkey(const std::string_view &k);
std::string pem(x509_t &x509);
std::string pem(pkey_t &pkey);

std::vector<uint8_t> sign256(const pkey_t &pkey, const std::string_view &data);
bool verify256(const x509_t &x509, const std::string_view &data, const std::string_view &signature);

creds_t gen_creds(const std::string_view &cn, std::uint32_t key_bits);

std::string_view signature(const x509_t &x);

std::string rand(std::size_t bytes);

class cert_chain_t {
public:
  KITTY_DECL_CONSTR(cert_chain_t)

  void add(x509_t &&cert);

  const char *verify(x509_t::element_type *cert);
private:
  std::vector<std::pair<x509_t, x509_store_t>> _certs;
  x509_store_ctx_t _cert_ctx;
};

class cipher_t {
public:
  cipher_t(const aes_t &key);
  cipher_t(cipher_t&&) noexcept = default;
  cipher_t &operator=(cipher_t&&) noexcept = default;

  int encrypt(const std::string_view &plaintext, std::vector<std::uint8_t> &cipher);

  int decrypt_gcm(aes_t &iv, const std::string_view &cipher, std::vector<std::uint8_t> &plaintext);
  int decrypt(const std::string_view &cipher, std::vector<std::uint8_t> &plaintext);
private:
  cipher_ctx_t ctx;
  aes_t key;

public:
  bool padding;
};
}

#endif //SUNSHINE_CRYPTO_H
