/**
 * @file src/platform/windows/PolicyConfig.h
 * @brief Undocumented COM-interface IPolicyConfig.
 * @details Use for setting default audio render endpoint.
 * @author EreTIk
 * @see https://kitere.github.io/
 */

#pragma once

#include <mmdeviceapi.h>

#ifdef __MINGW32__
  #undef DEFINE_GUID
  #ifdef __cplusplus
    #define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) EXTERN_C const GUID DECLSPEC_SELECTANY name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
  #else
    #define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) const GUID DECLSPEC_SELECTANY name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
  #endif

DEFINE_GUID(IID_IPolicyConfig, 0xf8679f50, 0x850a, 0x41cf, 0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8);
DEFINE_GUID(CLSID_CPolicyConfigClient, 0x870af99c, 0x171d, 0x4f9e, 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9);

#endif

interface DECLSPEC_UUID("f8679f50-850a-41cf-9c72-430f290290c8") IPolicyConfig;
class DECLSPEC_UUID("870af99c-171d-4f9e-af0d-e63df40c2bc9") CPolicyConfigClient;
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
  virtual HRESULT
  GetMixFormat(
    PCWSTR,
    WAVEFORMATEX **);

  virtual HRESULT STDMETHODCALLTYPE
  GetDeviceFormat(
    PCWSTR,
    INT,
    WAVEFORMATEX **);

  virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(
    PCWSTR);

  virtual HRESULT STDMETHODCALLTYPE
  SetDeviceFormat(
    PCWSTR,
    WAVEFORMATEX *,
    WAVEFORMATEX *);

  virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(
    PCWSTR,
    INT,
    PINT64,
    PINT64);

  virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(
    PCWSTR,
    PINT64);

  virtual HRESULT STDMETHODCALLTYPE
  GetShareMode(
    PCWSTR,
    struct DeviceShareMode *);

  virtual HRESULT STDMETHODCALLTYPE
  SetShareMode(
    PCWSTR,
    struct DeviceShareMode *);

  virtual HRESULT STDMETHODCALLTYPE
  GetPropertyValue(
    PCWSTR,
    const PROPERTYKEY &,
    PROPVARIANT *);

  virtual HRESULT STDMETHODCALLTYPE
  SetPropertyValue(
    PCWSTR,
    const PROPERTYKEY &,
    PROPVARIANT *);

  virtual HRESULT STDMETHODCALLTYPE
  SetDefaultEndpoint(
    PCWSTR wszDeviceId,
    ERole eRole);

  virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(
    PCWSTR,
    INT);
};
