#ifndef SUNSHINE_RAND_H
#define SUNSHINE_RAND_H

#include <random>

namespace util {

static int32_t generate_int32(std::default_random_engine &engine, int32_t min, int32_t max) {
  std::uniform_int_distribution<std::int32_t> dist(min, max);

  return dist(engine);
}

static int32_t generate_int32(int32_t min, int32_t max) {
  std::random_device r;

  std::default_random_engine engine { r() };

  return util::generate_int32(engine, min, max);
}

} // namespace util
#endif // SUNSHINE_RAND_H
