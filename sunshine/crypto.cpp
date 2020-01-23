//
// Created by loki on 5/31/19.
//

#include <openssl/pem.h>
#include "crypto.h"
namespace crypto {
using big_num_t = util::safe_ptr<BIGNUM, BN_free>;
//using rsa_t = util::safe_ptr<RSA, RSA_free>;
using asn1_string_t = util::safe_ptr<ASN1_STRING, ASN1_STRING_free>;

cert_chain_t::cert_chain_t() : _certs {}, _cert_ctx {X509_STORE_CTX_new() } {}
void cert_chain_t::add(x509_t &&cert) {
  x509_store_t x509_store { X509_STORE_new() };

  X509_STORE_add_cert(x509_store.get(), cert.get());
  _certs.emplace_back(std::make_pair(std::move(cert), std::move(x509_store)));
}

/*
 * When certificates from two or more instances of Moonlight have been added to x509_store_t,
 * only one of them will be verified by X509_verify_cert, resulting in only a single instance of
 * Moonlight to be able to use Sunshine
 *
 * To circumvent this, x509_store_t instance will be created for each instance of the certificates.
 */
const char *cert_chain_t::verify(x509_t::element_type *cert) {
  int err_code = 0;
  for(auto &[_,x509_store] : _certs) {
    auto fg = util::fail_guard([this]() {
      X509_STORE_CTX_cleanup(_cert_ctx.get());
    });

    X509_STORE_CTX_init(_cert_ctx.get(), x509_store.get(), nullptr, nullptr);
    X509_STORE_CTX_set_cert(_cert_ctx.get(), cert);

    auto err = X509_verify_cert(_cert_ctx.get());

    if (err == 1) {
      return nullptr;
    }

    err_code = X509_STORE_CTX_get_error(_cert_ctx.get());

    //FIXME: Checking for X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY is a temporary workaround to get mmonlight-embedded to work on the raspberry pi
    if(err_code == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY) {
      return nullptr;
    }
    if (err_code != X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT && err_code != X509_V_ERR_INVALID_CA) {
      return X509_verify_cert_error_string(err_code);
    }
  }

  return X509_verify_cert_error_string(err_code);
}

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

std::string pem(x509_t &x509) {
  bio_t bio { BIO_new(BIO_s_mem()) };

  PEM_write_bio_X509(bio.get(), x509.get());
  BUF_MEM *mem_ptr;
  BIO_get_mem_ptr(bio.get(), &mem_ptr);

  return { mem_ptr->data, mem_ptr->length };
}

std::string pem(pkey_t &pkey) {
  bio_t bio { BIO_new(BIO_s_mem()) };

  PEM_write_bio_PrivateKey(bio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr);
  BUF_MEM *mem_ptr;
  BIO_get_mem_ptr(bio.get(), &mem_ptr);

  return { mem_ptr->data, mem_ptr->length };
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

creds_t gen_creds(const std::string_view &cn, std::uint32_t key_bits) {
  x509_t x509 { X509_new() };
  pkey_t pkey { EVP_PKEY_new() };

  big_num_t big_num { BN_new() };
  BN_set_word(big_num.get(), RSA_F4);

  auto rsa = RSA_new();
  RSA_generate_key_ex(rsa, key_bits, big_num.get(), nullptr);
  EVP_PKEY_assign_RSA(pkey.get(), rsa);

  X509_set_version(x509.get(), 2);
  ASN1_INTEGER_set(X509_get_serialNumber(x509.get()), 0);

  constexpr auto year = 60 * 60 * 24 * 365;
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  X509_gmtime_adj(X509_get_notBefore(x509.get()), 0);
  X509_gmtime_adj(X509_get_notAfter(x509.get()), 20 * year);
#else
  asn1_string_t not_before { ASN1_STRING_dup(X509_get0_notBefore(x509.get())) };
  asn1_string_t not_after { ASN1_STRING_dup(X509_get0_notAfter(x509.get())) };

  X509_gmtime_adj(not_before.get(), 0);
  X509_gmtime_adj(not_after.get(), 20 * year);

  X509_set1_notBefore(x509.get(), not_before.get());
  X509_set1_notAfter(x509.get(), not_after.get());
#endif

  X509_set_pubkey(x509.get(), pkey.get());

  auto name = X509_get_subject_name(x509.get());
  X509_NAME_add_entry_by_txt(name,"CN", MBSTRING_ASC,
    (const std::uint8_t*)cn.data(), cn.size(),
    -1, 0);

  X509_set_issuer_name(x509.get(), name);
  X509_sign(x509.get(), pkey.get(), EVP_sha256());

  return { pem(x509), pem(pkey) };
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
