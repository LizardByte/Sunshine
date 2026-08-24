/**
 * @file src/platform/windows/PolicyConfig.h
 * @brief Undocumented COM-interface IPolicyConfig.
 * @details Use for setting default audio render endpoint.
 * @author EreTIk
 * @see https://kitere.github.io/
 */

#pragma once

// platform includes
#include <mmdeviceapi.h>

#ifdef __MINGW32__
  #undef DEFINE_GUID
  #ifdef __cplusplus
    #define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) EXTERN_C const GUID DECLSPEC_SELECTANY name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
  #else
    #define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) const GUID DECLSPEC_SELECTANY name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
  #endif

DEFINE_GUID(IID_IPolicyConfig, 0xf8679f50, 0x850a, 0x41cf, 0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8);
DEFINE_GUID(CLSID_CPolicyConfigClient, 0x870af99c, 0x171d, 0x4f9e, 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9);

#endif

/**
 * @brief Policy configuration COM interface.
 */
#ifdef DOXYGEN
class IPolicyConfig;
#else
interface DECLSPEC_UUID("f8679f50-850a-41cf-9c72-430f290290c8") IPolicyConfig;
#endif
/**
 * @brief Policy configuration COM class.
 */
#ifdef DOXYGEN
class CPolicyConfigClient;
#else
class DECLSPEC_UUID("870af99c-171d-4f9e-af0d-e63df40c2bc9") CPolicyConfigClient;
#endif
// ----------------------------------------------------------------------------
// class CPolicyConfigClient
// {870af99c-171d-4f9e-af0d-e63df40c2bc9}
//
// interface IPolicyConfig
// {f8679f50-850a-41cf-9c72-430f290290c8}
//
// Query interface:
// CComPtr<IPolicyConfig> PolicyConfig;
// PolicyConfig.CoCreateInstance(__uuidof(CPolicyConfigClient));
//
// @compatible: Windows 7 and Later
// ----------------------------------------------------------------------------
interface IPolicyConfig: public IUnknown {
public:
  /**
   * @brief Get the mix format for an endpoint.
   *
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT GetMixFormat(
    PCWSTR,
    WAVEFORMATEX **
  );

  /**
   * @brief Get the device format for an endpoint.
   *
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(
    PCWSTR,
    INT,
    WAVEFORMATEX **
  );

  /**
   * @brief Reset the device format for an endpoint.
   *
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(
    PCWSTR
  );

  /**
   * @brief Set the device format for an endpoint.
   *
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT STDMETHODCALLTYPE
    SetDeviceFormat(
      PCWSTR,
      WAVEFORMATEX *,
      WAVEFORMATEX *
    );

  /**
   * @brief Get the processing period for an endpoint.
   *
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(
    PCWSTR,
    INT,
    PINT64,
    PINT64
  );

  /**
   * @brief Set the processing period for an endpoint.
   *
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(
    PCWSTR,
    PINT64
  );

  /**
   * @brief Get the share mode for an endpoint.
   *
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT STDMETHODCALLTYPE GetShareMode(
    PCWSTR,
    struct DeviceShareMode *
  );

  /**
   * @brief Set the share mode for an endpoint.
   *
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT STDMETHODCALLTYPE SetShareMode(
    PCWSTR,
    struct DeviceShareMode *
  );

  /**
   * @brief Get a property value for an endpoint.
   *
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(
    PCWSTR,
    const PROPERTYKEY &,
    PROPVARIANT *
  );

  /**
   * @brief Set a property value for an endpoint.
   *
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(
    PCWSTR,
    const PROPERTYKEY &,
    PROPVARIANT *
  );

  /**
   * @brief Set the default endpoint for a role.
   *
   * @param wszDeviceId Endpoint device identifier.
   * @param eRole Endpoint role.
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(
    PCWSTR wszDeviceId,
    ERole eRole
  );

  /**
   * @brief Set endpoint visibility.
   *
   * @return HRESULT or platform status returned by the wrapped API.
   */
  virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(
    PCWSTR,
    INT
  );
};
