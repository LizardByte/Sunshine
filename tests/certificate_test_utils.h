/**
 * @file tests/certificate_test_utils.h
 * @brief Certificate fixtures shared by Sunshine security tests.
 */
#pragma once

// standard includes
#include <algorithm>

// lib includes
#include <openssl/x509v3.h>

// local includes
#include <src/crypto.h>

namespace test_utils::certificates {
  /**
   * @brief Generate a self-signed certificate that OpenSSL may use as a certificate authority.
   *
   * @param common_name Common name for the generated certificate.
   * @return PEM-encoded certificate-authority credentials.
   */
  inline crypto::creds_t generate_ca_credentials(const std::string_view common_name = "Sunshine Test Client CA") {
    auto credentials = crypto::gen_creds(common_name, 2048);
    auto certificate = crypto::x509(credentials.x509);
    auto private_key = crypto::pkey(credentials.pkey);

    util::safe_ptr<BASIC_CONSTRAINTS, &BASIC_CONSTRAINTS_free> constraints {BASIC_CONSTRAINTS_new()};
    constraints->ca = 1;
    X509_add1_ext_i2d(certificate.get(), NID_basic_constraints, constraints.get(), 1, X509V3_ADD_DEFAULT);
    X509_sign(certificate.get(), private_key.get(), EVP_sha256());
    credentials.x509 = crypto::pem(certificate);
    return credentials;
  }

  /**
   * @brief Generate a leaf certificate signed by the supplied certificate authority.
   *
   * @param issuer_credentials PEM-encoded issuer certificate and private key.
   * @return PEM-encoded leaf credentials.
   */
  inline crypto::creds_t generate_derived_leaf(const crypto::creds_t &issuer_credentials) {
    auto credentials = crypto::gen_creds("Sunshine Test Derived Client", 2048);
    auto certificate = crypto::x509(credentials.x509);
    auto issuer_certificate = crypto::x509(issuer_credentials.x509);
    auto issuer_private_key = crypto::pkey(issuer_credentials.pkey);

    X509_set_issuer_name(certificate.get(), X509_get_subject_name(issuer_certificate.get()));
    X509_sign(certificate.get(), issuer_private_key.get(), EVP_sha256());
    credentials.x509 = crypto::pem(certificate);
    return credentials;
  }

  /**
   * @brief Change a certificate's validity period so it is already expired.
   *
   * @param credentials PEM-encoded certificate and its signing key.
   * @return The supplied credentials with an expired, re-signed certificate.
   */
  inline crypto::creds_t expire_credentials(crypto::creds_t credentials) {
    auto certificate = crypto::x509(credentials.x509);
    auto private_key = crypto::pkey(credentials.pkey);
    constexpr long two_hours = 2 * 60 * 60;
    constexpr long one_hour = 60 * 60;

    X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -two_hours);
    X509_gmtime_adj(X509_getm_notAfter(certificate.get()), -one_hour);
    X509_sign(certificate.get(), private_key.get(), EVP_sha256());
    credentials.x509 = crypto::pem(certificate);
    return credentials;
  }

  /**
   * @brief Convert canonical PEM line endings to CRLF without changing certificate identity.
   *
   * @param pem Canonical PEM text.
   * @return Equivalent PEM text using CRLF line endings.
   */
  inline std::string to_crlf_pem(const std::string_view pem) {
    std::string converted;
    converted.reserve(pem.size() + std::ranges::count(pem, '\n'));
    for (const char character : pem) {
      if (character == '\n') {
        converted.push_back('\r');
      }
      converted.push_back(character);
    }
    return converted;
  }
}  // namespace test_utils::certificates
