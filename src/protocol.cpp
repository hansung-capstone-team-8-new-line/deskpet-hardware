#include "deskpet/protocol.hpp"
#include <algorithm>

namespace deskpet {
static uint32_t read32(const uint8_t* b) {
  return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
}
static void put32(Bytes& b, uint32_t x) {
  for (int i = 3; i >= 0; --i) b.push_back(uint8_t(x >> (i * 8)));
}
Bytes encode(const Packet& p) {
  const auto s = p.meta.dump();
  if (s.empty() || s.size() > maxJsonBytes || p.data.size() > maxPayloadBytes)
    throw Error("MESSAGE_TOO_LARGE", "Protocol frame exceeds its size limit");
  Bytes b; b.reserve(8 + s.size() + p.data.size());
  put32(b, uint32_t(s.size())); put32(b, uint32_t(p.data.size()));
  b.insert(b.end(), s.begin(), s.end()); b.insert(b.end(), p.data.begin(), p.data.end()); return b;
}
std::vector<Packet> Decoder::feed(std::span<const uint8_t> data) {
  // The socket reader feeds at most 8192 bytes per call. Bound storage before appending.
  if (buffer_.size() + data.size() > maxJsonBytes + maxPayloadBytes + 8192 + 8)
    throw Error("MESSAGE_TOO_LARGE", "Receive buffer exceeded");
  buffer_.insert(buffer_.end(), data.begin(), data.end());
  std::vector<Packet> packets;
  while (buffer_.size() >= 8) {
    const size_t jn = read32(buffer_.data()), pn = read32(buffer_.data() + 4);
    if (jn == 0 || jn > maxJsonBytes || pn > maxPayloadBytes) throw Error("MESSAGE_TOO_LARGE", "Invalid frame lengths");
    const auto total = 8 + jn + pn;
    if (buffer_.size() < total) break;
    Json j;
    try { j = Json::parse(buffer_.begin() + 8, buffer_.begin() + 8 + jn); }
    catch (const Json::exception&) { throw Error("INVALID_JSON", "Invalid JSON metadata"); }
    if (!j.is_object()) throw Error("INVALID_JSON", "Metadata must be an object");
    packets.push_back({std::move(j), Bytes(buffer_.begin() + 8 + jn, buffer_.begin() + total), 0});
    buffer_.erase(buffer_.begin(), buffer_.begin() + total);
    if (packets.size() > 128) throw Error("QUEUE_FULL", "Too many commands in one read");
  }
  return packets;
}
}
