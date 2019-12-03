//
// Created by loki on 8-2-19.
//

#ifndef T_MAN_UUID_H
#define T_MAN_UUID_H

#include <random>

union uuid_t {
  std::uint8_t b8[16];
  std::uint16_t b16[8];
  std::uint32_t b32[4];
  std::uint64_t b64[2];

  static uuid_t generate(std::default_random_engine &engine) {
    std::uniform_int_distribution<std::uint8_t> dist(0, std::numeric_limits<std::uint8_t>::max());

    uuid_t buf;
    for(auto &el : buf.b8) {
      el = dist(engine);
    }

    buf.b8[7] &= (std::uint8_t) 0b00101111;
    buf.b8[9] &= (std::uint8_t) 0b10011111;

    return buf;
  }

  static uuid_t generate() {
    std::random_device r;

    std::default_random_engine engine { r() };

    return generate(engine);
  }

  constexpr bool operator==(const uuid_t &other) const {
    return b64[0] == other.b64[0] && b64[1] == other.b64[1];
  }

  constexpr bool operator<(const uuid_t &other) const {
    return (b64[0] < other.b64[0] || (b64[0] == other.b64[0] && b64[1] < other.b64[1]));
  }

  constexpr bool operator>(const uuid_t &other) const {
    return (b64[0] > other.b64[0] || (b64[0] == other.b64[0] && b64[1] > other.b64[1]));
  }
};
#endif //T_MAN_UUID_H
