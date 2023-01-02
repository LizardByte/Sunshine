#ifndef SUNSHINE_RAND_H
#define SUNSHINE_RAND_H

#include <random>

namespace util {

  static uint32_t generate_uint32(std::default_random_engine &engine, uint32_t min, uint32_t max) {
    std::uniform_int_distribution<std::uint32_t> dist(min, max);

    return dist(engine);
  }

  static uint32_t generate_uint32(uint32_t min, uint32_t max) {
    std::random_device r;

    std::default_random_engine engine { r() };

    return util::generate_uint32(engine, min, max);
  }

} // namespace util
#endif // SUNSHINE_RAND_H
