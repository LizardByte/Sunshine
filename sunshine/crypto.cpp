//
// Created by loki on 5/31/19.
//

#include <openssl/pem.h>
#include "crypto.h"
namespace crypto {
cipher_t::cipher_t(const crypto::aes_t &key) : ctx { EVP_CIPHER_CTX_new() }, key { key }, padding { true } {}
int cipher_t::decrypt(const std::string_view &cipher, std::vector<std::uint8_t> &plaintext) {
  int len;

  auto fg = util::fail_guard([this]() {
    EVP_CIPHER_CTX_reset(ctx.get());
  });

  // Gen 7 servers use 128-bit AES ECB
  if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr) != 1) {
    return -1;
  }

  EVP_CIPHER_CTX_set_padding(ctx.get(), padding);

  plaintext.resize((cipher.size() + 15) / 16 * 16);
  auto size = (int)plaintext.size();
  // Encrypt into the caller's buffer, leaving room for the auth tag to be prepended
  if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &size, (const std::uint8_t*)cipher.data(), cipher.size()) != 1) {
    return -1;
  }

  if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data(), &len) != 1) {
    return -1;
  }

  plaintext.resize(len + size);
  return 0;
}

int cipher_t::decrypt_gcm(aes_t &iv, const std::string_view &tagged_cipher,
                          std::vector<std::uint8_t> &plaintext) {
  auto cipher = tagged_cipher.substr(16);
  auto tag    = tagged_cipher.substr(0, 16);

  auto fg = util::fail_guard([this]() {
    EVP_CIPHER_CTX_reset(ctx.get());
  });

  if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1) {
    return -1;
  }

  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) != 1) {
    return -1;
  }

  if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1) {
    return -1;
  }

  EVP_CIPHER_CTX_set_padding(ctx.get(), padding);
  plaintext.resize((cipher.size() + 15) / 16 * 16);

  int size;
  if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &size, (const std::uint8_t*)cipher.data(), cipher.size()) != 1) {
    return -1;
  }

  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, tag.size(), const_cast<char*>(tag.data())) != 1) {
    return -1;
  }

  int len = size;
  if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + size, &len) != 1) {
    return -1;
  }

  plaintext.resize(size + len);
  return 0;
}

int cipher_t::encrypt(const std::string_view &plaintext, std::vector<std::uint8_t> &cipher) {
  int len;

  auto fg = util::fail_guard([this]() {
    EVP_CIPHER_CTX_reset(ctx.get());
  });

  // Gen 7 servers use 128-bit AES ECB
  if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr) != 1) {
    return -1;
  }

  EVP_CIPHER_CTX_set_padding(ctx.get(), padding);

  cipher.resize((plaintext.size() + 15) / 16 * 16);
  auto size = (int)cipher.size();
  // Encrypt into the caller's buffer
  if (EVP_EncryptUpdate(ctx.get(), cipher.data(), &size, (const std::uint8_t*)plaintext.data(), plaintext.size()) != 1) {
    return -1;
  }

  if (EVP_EncryptFinal_ex(ctx.get(), cipher.data() + size, &len) != 1) {
    return -1;
  }

  cipher.resize(len + size);
  return 0;
}

aes_t gen_aes_key(const std::array<uint8_t, 16> &salt, const std::string_view &pin) {
  aes_t key;

  std::string salt_pin;
  salt_pin.reserve(salt.size() + pin.size());

  salt_pin.insert(std::end(salt_pin), std::begin(salt), std::end(salt));
  salt_pin.insert(std::end(salt_pin), std::begin(pin), std::end(pin));

  auto hsh = hash(salt_pin);

  std::copy(std::begin(hsh), std::begin(hsh) + key.size(), std::begin(key));

  return key;
}

sha256_t hash(const std::string_view &plaintext) {
  sha256_t hsh;

  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, plaintext.data(), plaintext.size());
  SHA256_Final(hsh.data(), &sha256);

  return hsh;
}

x509_t x509(const std::string_view &x) {
  bio_t io { BIO_new(BIO_s_mem()) };

  BIO_write(io.get(), x.data(), x.size());

  X509 *p = nullptr;
  PEM_read_bio_X509(io.get(), &p, nullptr, nullptr);

  return x509_t { p };
}

pkey_t pkey(const std::string_view &k) {
  bio_t io { BIO_new(BIO_s_mem()) };

  BIO_write(io.get(), k.data(), k.size());

  EVP_PKEY *p = nullptr;
  PEM_read_bio_PrivateKey(io.get(), &p, nullptr, nullptr);

  return pkey_t { p };
}


std::string_view signature(const x509_t &x) {
  // X509_ALGOR *_ = nullptr;

  const ASN1_BIT_STRING *asn1 = nullptr;
  X509_get0_signature(&asn1, nullptr, x.get());

  return { (const char*)asn1->data, (std::size_t)asn1->length };
}

std::string rand(std::size_t bytes) {
  std::string r;
  r.resize(bytes);

  RAND_bytes((uint8_t*)r.data(), r.size());

  return r;
}

std::vector<uint8_t> sign(const pkey_t &pkey, const std::string_view &data, const EVP_MD *md) {
  md_ctx_t ctx { EVP_MD_CTX_create() };

  if(EVP_DigestSignInit(ctx.get(), nullptr, md, nullptr, pkey.get()) != 1) {
    return {};
  }

  if(EVP_DigestSignUpdate(ctx.get(), data.data(), data.size()) != 1) {
    return {};
  }

  std::size_t slen = digest_size;

  std::vector<uint8_t> digest;
  digest.resize(slen);

  if(EVP_DigestSignFinal(ctx.get(), digest.data(), &slen) != 1) {
    return {};
  }

  return digest;
}

std::vector<uint8_t> sign256(const pkey_t &pkey, const std::string_view &data) {
  return sign(pkey, data, EVP_sha256());
}

bool verify(const x509_t &x509, const std::string_view &data, const std::string_view &signature, const EVP_MD *md) {
  auto pkey = X509_get_pubkey(x509.get());

  md_ctx_t ctx { EVP_MD_CTX_create() };

  if(EVP_DigestVerifyInit(ctx.get(), nullptr, md, nullptr, pkey) != 1) {
    return false;
  }

  if(EVP_DigestVerifyUpdate(ctx.get(), data.data(), data.size()) != 1) {
    return false;
  }

  if(EVP_DigestVerifyFinal(ctx.get(), (const uint8_t*)signature.data(), signature.size()) != 1) {
    return false;
  }

  return true;
}

bool verify256(const x509_t &x509, const std::string_view &data, const std::string_view &signature) {
  return verify(x509, data, signature, EVP_sha256());
}

void md_ctx_destroy(EVP_MD_CTX *ctx) {
  EVP_MD_CTX_destroy(ctx);
}
}