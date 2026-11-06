#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <cstdint>
#include <ostream>
#include <string>
#include <VideoToolbox/VideoToolbox.h>

namespace ca {

  // Display FourCC error codes, VideoToolbox error constants, and fallback to integer.
  // Usage: BOOST_LOG(error) << ca::Status(err);

  static std::string VTErrorName(OSStatus status) {
    switch (status) {
      case 0:
        return "noErr";

#define VT_ERROR_CASE(name) \
  case name: \
    return #name
        VT_ERROR_CASE(kVTPropertyNotSupportedErr);
        VT_ERROR_CASE(kVTPropertyReadOnlyErr);
        VT_ERROR_CASE(kVTParameterErr);
        VT_ERROR_CASE(kVTInvalidSessionErr);
        VT_ERROR_CASE(kVTAllocationFailedErr);
        VT_ERROR_CASE(kVTPixelTransferNotSupportedErr);
        VT_ERROR_CASE(kVTCouldNotFindVideoDecoderErr);
        VT_ERROR_CASE(kVTCouldNotCreateInstanceErr);
        VT_ERROR_CASE(kVTCouldNotFindVideoEncoderErr);
        VT_ERROR_CASE(kVTVideoDecoderBadDataErr);
        VT_ERROR_CASE(kVTVideoDecoderUnsupportedDataFormatErr);
        VT_ERROR_CASE(kVTVideoDecoderMalfunctionErr);
        VT_ERROR_CASE(kVTVideoEncoderMalfunctionErr);
        VT_ERROR_CASE(kVTVideoDecoderNotAvailableNowErr);
        VT_ERROR_CASE(kVTPixelRotationNotSupportedErr);
        VT_ERROR_CASE(kVTVideoEncoderNotAvailableNowErr);
        VT_ERROR_CASE(kVTFormatDescriptionChangeNotSupportedErr);
        VT_ERROR_CASE(kVTInsufficientSourceColorDataErr);
        VT_ERROR_CASE(kVTCouldNotCreateColorCorrectionDataErr);
        VT_ERROR_CASE(kVTColorSyncTransformConvertFailedErr);
        VT_ERROR_CASE(kVTVideoDecoderAuthorizationErr);
        VT_ERROR_CASE(kVTVideoEncoderAuthorizationErr);
        VT_ERROR_CASE(kVTColorCorrectionPixelTransferFailedErr);
        VT_ERROR_CASE(kVTMultiPassStorageIdentifierMismatchErr);
        VT_ERROR_CASE(kVTMultiPassStorageInvalidErr);
        VT_ERROR_CASE(kVTFrameSiloInvalidTimeStampErr);
        VT_ERROR_CASE(kVTFrameSiloInvalidTimeRangeErr);
        VT_ERROR_CASE(kVTCouldNotFindTemporalFilterErr);
        VT_ERROR_CASE(kVTPixelTransferNotPermittedErr);
        VT_ERROR_CASE(kVTColorCorrectionImageRotationFailedErr);
        VT_ERROR_CASE(kVTVideoDecoderRemovedErr);
        VT_ERROR_CASE(kVTSessionMalfunctionErr);
        VT_ERROR_CASE(kVTVideoDecoderNeedsRosettaErr);
        VT_ERROR_CASE(kVTVideoEncoderNeedsRosettaErr);
        VT_ERROR_CASE(kVTVideoDecoderReferenceMissingErr);
        VT_ERROR_CASE(kVTVideoDecoderCallbackMessagingErr);
        VT_ERROR_CASE(kVTVideoDecoderUnknownErr);
        VT_ERROR_CASE(kVTExtensionDisabledErr);
        VT_ERROR_CASE(kVTVideoEncoderMVHEVCVideoLayerIDsMismatchErr);
        VT_ERROR_CASE(kVTCouldNotOutputTaggedBufferGroupErr);
        VT_ERROR_CASE(kVTCouldNotFindExtensionErr);
        VT_ERROR_CASE(kVTExtensionConflictErr);
        VT_ERROR_CASE(kVTVideoEncoderAutoWhiteBalanceNotLockedErr);
        VT_ERROR_CASE(kVTLogTransferFunctionMismatchErr);
#undef VT_ERROR_CASE

      default:
        return std::to_string(static_cast<int32_t>(status));
    }
  }

  inline std::string OSStatusToString(OSStatus error) {
    const uint32_t val = static_cast<uint32_t>(error);
    const unsigned char c1 = static_cast<unsigned char>((val >> 24) & 0xFF);
    const unsigned char c2 = static_cast<unsigned char>((val >> 16) & 0xFF);
    const unsigned char c3 = static_cast<unsigned char>((val >> 8) & 0xFF);
    const unsigned char c4 = static_cast<unsigned char>((val >> 0) & 0xFF);

    auto is_printable = [](unsigned char c) -> bool {
      return c >= 32 && c <= 126;
    };

    if (is_printable(c1) && is_printable(c2) && is_printable(c3) && is_printable(c4)) {
      char buf[8] = {};
      buf[0] = '\'';
      buf[1] = static_cast<char>(c1);
      buf[2] = static_cast<char>(c2);
      buf[3] = static_cast<char>(c3);
      buf[4] = static_cast<char>(c4);
      buf[5] = '\'';
      buf[6] = '\0';
      return std::string(buf);
    }

    return VTErrorName(error);
  }

  namespace detail {
    /**
     * @brief Small wrapper for displaying CoreAudio OSStatus values.
     */
    struct StatusView {
      OSStatus e;  ///< E.
    };

    inline std::ostream &operator<<(std::ostream &os, StatusView v) {
      return os << OSStatusToString(v.e);
    }
  }  // namespace detail

  inline detail::StatusView Status(OSStatus e) {
    return detail::StatusView {e};
  }

}  // namespace ca
