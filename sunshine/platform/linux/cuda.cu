// #include <algorithm>
#include <helper_math.h>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>

#include "cuda.h"

using namespace std::literals;

#define SUNSHINE_STRINGVIEW_HELPER(x) x##sv
#define SUNSHINE_STRINGVIEW(x) SUNSHINE_STRINGVIEW_HELPER(x)

#define CU_CHECK(x, y) \
  if(check((x), SUNSHINE_STRINGVIEW(y ": "))) return -1

#define CU_CHECK_VOID(x, y) \
  if(check((x), SUNSHINE_STRINGVIEW(y ": "))) return;

#define CU_CHECK_PTR(x, y) \
  if(check((x), SUNSHINE_STRINGVIEW(y ": "))) return nullptr;

#define CU_CHECK_IGNORE(x, y) \
  check((x), SUNSHINE_STRINGVIEW(y ": "))

using namespace std::literals;

//////////////////// Special desclarations
/**
 * NVCC segfaults when including <chrono>
 * Therefore, some declarations need to be added explicitely
 */
namespace platf {
struct img_t {
public:
  std::uint8_t *data {};
  std::int32_t width {};
  std::int32_t height {};
  std::int32_t pixel_pitch {};
  std::int32_t row_pitch {};

  virtual ~img_t() = default;
};
} // namespace platf

namespace video {
using __float4 = float[4];
using __float3 = float[3];
using __float2 = float[2];

struct __attribute__((__aligned__(16))) color_t {
  float4 color_vec_y;
  float4 color_vec_u;
  float4 color_vec_v;
  float2 range_y;
  float2 range_uv;
};

struct __attribute__((__aligned__(16))) color_extern_t {
  __float4 color_vec_y;
  __float4 color_vec_u;
  __float4 color_vec_v;
  __float2 range_y;
  __float2 range_uv;
};

static_assert(sizeof(video::color_t) == sizeof(video::color_extern_t), "color matrix struct mismatch");

extern color_t colors[4];
} // namespace video

//////////////////// End special declarations

namespace cuda {
auto constexpr INVALID_TEXTURE = std::numeric_limits<cudaTextureObject_t>::max();

template<class T>
inline T div_align(T l, T r) {
  return (l + r - 1) / r;
}

void pass_error(const std::string_view &sv, const char *name, const char *description);
inline static int check(cudaError_t result, const std::string_view &sv) {
  if(result) {
    auto name        = cudaGetErrorName(result);
    auto description = cudaGetErrorString(result);

    pass_error(sv, name, description);
    return -1;
  }

  return 0;
}

template<class T>
ptr_t make_ptr() {
  void *p;
  CU_CHECK_PTR(cudaMalloc(&p, sizeof(T)), "Couldn't allocate color matrix");

  ptr_t ptr { p };

  return ptr;
}

void freeCudaPtr_t::operator()(void *ptr) {
  CU_CHECK_IGNORE(cudaFree(ptr), "Couldn't free cuda device pointer");
}

inline __device__ float3 bgra_to_rgb(uchar4 vec) {
  return make_float3((float)vec.z, (float)vec.y, (float)vec.x);
}

inline __device__ float2 calcUV(float3 pixel, const video::color_t *const color_matrix) {
  float4 vec_u = color_matrix->color_vec_u;
  float4 vec_v = color_matrix->color_vec_v;

  float u = dot(pixel, make_float3(vec_u)) + vec_u.w;
  float v = dot(pixel, make_float3(vec_v)) + vec_v.w;

  u = u * color_matrix->range_uv.x + color_matrix->range_uv.y;
  v = (v * color_matrix->range_uv.x + color_matrix->range_uv.y) * 224.0f / 256.0f + 0.0625f * 256.0f;

  return make_float2(u, v);
}

inline __device__ float calcY(float3 pixel, const video::color_t *const color_matrix) {
  float4 vec_y = color_matrix->color_vec_y;

  return (dot(pixel, make_float3(vec_y)) + vec_y.w) * color_matrix->range_y.x + color_matrix->range_y.y;
}

__global__ void RGBA_to_NV12(
  cudaTextureObject_t srcImage, std::uint8_t *dstY, std::uint8_t *dstUV,
  std::uint32_t dstPitchY, std::uint32_t dstPitchUV,
  const viewport_t viewport, const video::color_t *const color_matrix) {

  int idX = (threadIdx.x + blockDim.x * blockIdx.x) * 2;
  int idY = (threadIdx.y + blockDim.y * blockIdx.y);

  if(idX >= viewport.width) return;
  if(idY >= viewport.height) return;

  float x = (float)idX / (float)viewport.width / 4;
  float y = (float)idY / (float)viewport.height;

  idX += viewport.offsetX;
  idY += viewport.offsetY;

  dstY  = dstY + idX + idY * dstPitchY;
  dstUV = dstUV + idX + (idY / 2 * dstPitchUV);

  float3 rgb_l = bgra_to_rgb(tex2D<uchar4>(srcImage, x, y));
  float3 rgb_r = bgra_to_rgb(tex2D<uchar4>(srcImage, x + 0.25f / viewport.width, y + 1.0f / viewport.height));

  float2 uv = calcUV((rgb_l + rgb_r) * 0.5f, color_matrix);

  dstUV[0] = uv.x;
  dstUV[1] = uv.y;
  dstY[0]  = calcY(rgb_l, color_matrix);
  dstY[1]  = calcY(rgb_r, color_matrix);
}

sws_t::sws_t(int in_width, int in_height, int out_width, int out_height, int pitch, int threadsPerBlock, ptr_t &&color_matrix)
    : array {}, texture { INVALID_TEXTURE }, threadsPerBlock { threadsPerBlock }, color_matrix { std::move(color_matrix) } {
  auto format = cudaCreateChannelDesc<uchar4>();

  CU_CHECK_VOID(cudaMallocArray(&array, &format, pitch, in_height, cudaArrayDefault), "Couldn't allocate cuda array");

  cudaResourceDesc res {};
  res.resType         = cudaResourceTypeArray;
  res.res.array.array = array;

  cudaTextureDesc desc {};

  desc.readMode         = cudaReadModeElementType;
  desc.filterMode       = cudaFilterModePoint;
  desc.normalizedCoords = true;

  std::fill_n(std::begin(desc.addressMode), 2, cudaAddressModeClamp);

  CU_CHECK_VOID(cudaCreateTextureObject(&texture, &res, &desc, nullptr), "Couldn't create cuda texture");


  // Ensure aspect ratio is maintained
  auto scalar       = std::fminf(out_width / (float)in_width, out_height / (float)in_height);
  auto out_width_f  = in_width * scalar;
  auto out_height_f = in_height * scalar;

  // result is always positive
  auto offsetX_f = (out_width - out_width_f) / 2;
  auto offsetY_f = (out_height - out_height_f) / 2;

  viewport.width  = out_width_f;
  viewport.height = out_height_f;

  viewport.offsetX = offsetX_f;
  viewport.offsetY = offsetY_f;
}

sws_t::~sws_t() {
  if(texture != INVALID_TEXTURE) {
    CU_CHECK_IGNORE(cudaDestroyTextureObject(texture), "Couldn't deallocate cuda texture");

    texture = INVALID_TEXTURE;
  }

  if(array) {
    CU_CHECK_IGNORE(cudaFreeArray(array), "Couldn't deallocate cuda array");

    array = cudaArray_t {};
  }
}

std::unique_ptr<sws_t> sws_t::make(int in_width, int in_height, int out_width, int out_height, int pitch) {
  cudaDeviceProp props;
  int device;
  CU_CHECK_PTR(cudaGetDevice(&device), "Couldn't get cuda device");
  CU_CHECK_PTR(cudaGetDeviceProperties(&props, device), "Couldn't get cuda device properties");

  auto ptr = make_ptr<video::color_t>();
  if(!ptr) {
    return nullptr;
  }

  auto sws = std::make_unique<sws_t>(in_width, in_height, out_width, out_height, pitch, props.maxThreadsPerMultiProcessor / props.maxBlocksPerMultiProcessor / 2, std::move(ptr));

  if(sws->texture == INVALID_TEXTURE) {
    return nullptr;
  }

  return sws;
}

int sws_t::convert(std::uint8_t *Y, std::uint8_t *UV, std::uint32_t pitchY, std::uint32_t pitchUV) {
  return convert(Y, UV, pitchY, pitchUV, viewport);
}

int sws_t::convert(std::uint8_t *Y, std::uint8_t *UV, std::uint32_t pitchY, std::uint32_t pitchUV, const viewport_t &viewport) {
  int threadsX = viewport.width / 2;
  int threadsY = viewport.height;

  dim3 block(threadsPerBlock, threadsPerBlock);
  dim3 grid(div_align(threadsX, threadsPerBlock), div_align(threadsY, threadsPerBlock));

  RGBA_to_NV12<<<block, grid>>>(texture, Y, UV, pitchY, pitchUV, viewport, (video::color_t*)color_matrix.get());

  return CU_CHECK_IGNORE(cudaGetLastError(), "RGBA_to_NV12 failed");
}

void sws_t::set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) {
  video::color_t *color_p;
  switch(colorspace) {
  case 5: // SWS_CS_SMPTE170M
    color_p = &video::colors[0];
    break;
  case 1: // SWS_CS_ITU709
    color_p = &video::colors[2];
    break;
  case 9: // SWS_CS_BT2020
  default:
    color_p = &video::colors[0];
  };

  if(color_range > 1) {
    // Full range
    ++color_p;
  }

  auto color_matrix = *color_p;
  color_matrix.color_vec_y.w *= 256.0f;
  color_matrix.color_vec_u.w *= 256.0f;
  color_matrix.color_vec_v.w *= 256.0f;

  color_matrix.range_y.y *= 256.0f;
  color_matrix.range_uv.y *= 256.0f;

  CU_CHECK_IGNORE(cudaMemcpy(this->color_matrix.get(), &color_matrix, sizeof(video::color_t), cudaMemcpyHostToDevice), "Couldn't copy color matrix to cuda");
}

int sws_t::load_ram(platf::img_t &img) {
  return CU_CHECK_IGNORE(cudaMemcpy2DToArray(array, 0, 0, img.data, img.row_pitch, img.width * img.pixel_pitch, img.height, cudaMemcpyHostToDevice), "Couldn't copy to cuda array");
}

} // namespace cuda