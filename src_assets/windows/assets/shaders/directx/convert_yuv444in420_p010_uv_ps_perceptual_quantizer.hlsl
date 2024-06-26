#include "include/convert_perceptual_quantizer_base.hlsl"

#define P010
#define RECOMBINED444_V_SAMPLING
#include "include/convert_yuv444in420_ps_base.hlsl"
