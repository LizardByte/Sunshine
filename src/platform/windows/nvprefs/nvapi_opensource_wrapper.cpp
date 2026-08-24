/**
 * @file src/platform/windows/nvprefs/nvapi_opensource_wrapper.cpp
 * @brief Definitions for the NVAPI wrapper.
 */
// standard includes
#include <map>

// local includes
#include "driver_settings.h"
#include "nvprefs_common.h"

// special nvapi header that should be the last include
#include <nvapi_interface.h>

namespace {

  std::map<const char *, void *> interfaces;
  HMODULE dll = nullptr;

  template<typename Func, typename... Args>
  NvAPI_Status call_interface(const char *name, Args... args) {
    auto func = (Func *) interfaces[name];

    if (!func) {
      return interfaces.empty() ? NVAPI_API_NOT_INITIALIZED : NVAPI_NOT_SUPPORTED;
    }

    return func(args...);
  }

}  // namespace

#undef NVAPI_INTERFACE
/**
 * @def NVAPI_INTERFACE
 * @brief Macro for NVAPI INTERFACE.
 */
#define NVAPI_INTERFACE NvAPI_Status __cdecl

/**
 * @brief NVAPI export used to resolve function pointers by interface ID.
 *
 * @param id NVAPI interface ID from the generated interface table.
 * @return Function pointer for the requested NVAPI interface, or nullptr.
 */
extern void *__cdecl nvapi_QueryInterface(NvU32 id);

NVAPI_INTERFACE

/**
 * @brief Load NVAPI and resolve the subset of interfaces used by Sunshine.
 *
 * @return NVAPI_OK on success, or NVAPI_LIBRARY_NOT_FOUND when the DLL cannot be loaded.
 */
NvAPI_Initialize() {
  if (dll) {
    return NVAPI_OK;
  }

#ifdef _WIN64
  auto dll_name = "nvapi64.dll";
#else
  auto dll_name = "nvapi.dll";
#endif

  if ((dll = LoadLibraryEx(dll_name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))) {
    if (auto query_interface = (decltype(nvapi_QueryInterface) *) GetProcAddress(dll, "nvapi_QueryInterface")) {
      for (const auto &item : nvapi_interface_table) {
        interfaces[item.func] = query_interface(item.id);
      }
      return NVAPI_OK;
    }
  }

  NvAPI_Unload();
  return NVAPI_LIBRARY_NOT_FOUND;
}

/**
 * @brief Unload NVAPI and clear all resolved interface pointers.
 *
 * @return NVAPI_OK after cleanup.
 */
NVAPI_INTERFACE NvAPI_Unload() {
  if (dll) {
    interfaces.clear();
    FreeLibrary(dll);
    dll = nullptr;
  }
  return NVAPI_OK;
}

/**
 * @brief Forward NvAPI_GetErrorMessage to the loaded NVAPI DLL.
 *
 * @param nr Controller index assigned by the client.
 * @param szDesc Output buffer receiving the human-readable error string.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_GetErrorMessage(NvAPI_Status nr, NvAPI_ShortString szDesc) {
  return call_interface<decltype(NvAPI_GetErrorMessage)>("NvAPI_GetErrorMessage", nr, szDesc);
}

// This is only a subset of NvAPI_DRS_* functions, more can be added if needed

/**
 * @brief Forward NvAPI_DRS_CreateSession to the loaded NVAPI DLL.
 *
 * @param phSession Output handle for the created DRS session.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_CreateSession(NvDRSSessionHandle *phSession) {
  return call_interface<decltype(NvAPI_DRS_CreateSession)>("NvAPI_DRS_CreateSession", phSession);
}

/**
 * @brief Forward NvAPI_DRS_DestroySession to the loaded NVAPI DLL.
 *
 * @param hSession DRS session handle to destroy.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_DestroySession(NvDRSSessionHandle hSession) {
  return call_interface<decltype(NvAPI_DRS_DestroySession)>("NvAPI_DRS_DestroySession", hSession);
}

/**
 * @brief Forward NvAPI_DRS_LoadSettings to the loaded NVAPI DLL.
 *
 * @param hSession DRS session whose settings are loaded.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_LoadSettings(NvDRSSessionHandle hSession) {
  return call_interface<decltype(NvAPI_DRS_LoadSettings)>("NvAPI_DRS_LoadSettings", hSession);
}

/**
 * @brief Forward NvAPI_DRS_SaveSettings to the loaded NVAPI DLL.
 *
 * @param hSession DRS session whose settings are saved.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_SaveSettings(NvDRSSessionHandle hSession) {
  return call_interface<decltype(NvAPI_DRS_SaveSettings)>("NvAPI_DRS_SaveSettings", hSession);
}

/**
 * @brief Forward NvAPI_DRS_CreateProfile to the loaded NVAPI DLL.
 *
 * @param hSession DRS session that owns the profile.
 * @param pProfileInfo Profile definition to create.
 * @param phProfile Output handle for the created profile.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_CreateProfile(NvDRSSessionHandle hSession, NVDRS_PROFILE *pProfileInfo, NvDRSProfileHandle *phProfile) {
  return call_interface<decltype(NvAPI_DRS_CreateProfile)>("NvAPI_DRS_CreateProfile", hSession, pProfileInfo, phProfile);
}

/**
 * @brief Forward NvAPI_DRS_FindProfileByName to the loaded NVAPI DLL.
 *
 * @param hSession DRS session to search.
 * @param profileName Profile name.
 * @param phProfile Output handle for the matched profile.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_FindProfileByName(NvDRSSessionHandle hSession, NvAPI_UnicodeString profileName, NvDRSProfileHandle *phProfile) {
  return call_interface<decltype(NvAPI_DRS_FindProfileByName)>("NvAPI_DRS_FindProfileByName", hSession, profileName, phProfile);
}

/**
 * @brief Forward NvAPI_DRS_CreateApplication to the loaded NVAPI DLL.
 *
 * @param hSession DRS session that owns the profile.
 * @param hProfile Profile receiving the application entry.
 * @param pApplication Application entry to create.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_CreateApplication(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, NVDRS_APPLICATION *pApplication) {
  return call_interface<decltype(NvAPI_DRS_CreateApplication)>("NvAPI_DRS_CreateApplication", hSession, hProfile, pApplication);
}

/**
 * @brief Forward NvAPI_DRS_GetApplicationInfo to the loaded NVAPI DLL.
 *
 * @param hSession DRS session that owns the profile.
 * @param hProfile Profile containing the application entry.
 * @param appName App name.
 * @param pApplication Output application information.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_GetApplicationInfo(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, NvAPI_UnicodeString appName, NVDRS_APPLICATION *pApplication) {
  return call_interface<decltype(NvAPI_DRS_GetApplicationInfo)>("NvAPI_DRS_GetApplicationInfo", hSession, hProfile, appName, pApplication);
}

/**
 * @brief Forward NvAPI_DRS_SetSetting to the loaded NVAPI DLL.
 *
 * @param hSession DRS session that owns the profile.
 * @param hProfile Profile receiving the setting.
 * @param pSetting Setting value to apply.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_SetSetting(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, NVDRS_SETTING *pSetting) {
  return call_interface<decltype(NvAPI_DRS_SetSetting)>("NvAPI_DRS_SetSetting", hSession, hProfile, pSetting);
}

/**
 * @brief Forward NvAPI_DRS_GetSetting to the loaded NVAPI DLL.
 *
 * @param hSession DRS session that owns the profile.
 * @param hProfile Profile containing the setting.
 * @param settingId Setting ID.
 * @param pSetting Output setting value.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_GetSetting(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, NvU32 settingId, NVDRS_SETTING *pSetting) {
  return call_interface<decltype(NvAPI_DRS_GetSetting)>("NvAPI_DRS_GetSetting", hSession, hProfile, settingId, pSetting);
}

/**
 * @brief Forward NvAPI_DRS_DeleteProfileSetting to the loaded NVAPI DLL.
 *
 * @param hSession DRS session that owns the profile.
 * @param hProfile Profile containing the setting.
 * @param settingId Setting ID.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_DeleteProfileSetting(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, NvU32 settingId) {
  return call_interface<decltype(NvAPI_DRS_DeleteProfileSetting)>("NvAPI_DRS_DeleteProfileSetting", hSession, hProfile, settingId);
}

/**
 * @brief Forward NvAPI_DRS_GetBaseProfile to the loaded NVAPI DLL.
 *
 * @param hSession DRS session to query.
 * @param phProfile Output handle for the base profile.
 * @return NVAPI status returned by the loaded function.
 */
NVAPI_INTERFACE NvAPI_DRS_GetBaseProfile(NvDRSSessionHandle hSession, NvDRSProfileHandle *phProfile) {
  return call_interface<decltype(NvAPI_DRS_GetBaseProfile)>("NvAPI_DRS_GetBaseProfile", hSession, phProfile);
}
