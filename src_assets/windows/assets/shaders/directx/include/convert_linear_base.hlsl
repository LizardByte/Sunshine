#include "include/common.hlsl"

float3 CONVERT_FUNCTION(float3 input)
{
    return ApplySRGBCurve(saturate(input));
}
