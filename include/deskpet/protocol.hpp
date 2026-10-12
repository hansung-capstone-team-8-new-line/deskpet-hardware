#pragma once
#include "deskpet/types.hpp"
#include <span>

namespace deskpet {
constexpr size_t maxJsonBytes = 16384, maxPayloadBytes = 65536;
Bytes encode(const Packet& p);
class Decoder {
  Bytes buffer_;
public:
  std::vector<Packet> feed(std::span<const uint8_t> data);
  void clear() { buffer_.clear(); }
};
}
