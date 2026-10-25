#include "deskpet/runtime.hpp"
#include "deskpet/protocol.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <set>

namespace deskpet {
namespace {
struct FD {
  int n = -1;
  ~FD() { if (n >= 0) ::close(n); }
};
void nonblocking(int fd) {
  if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) throw Error("SOCKET_ERROR", std::strerror(errno));
  fcntl(fd, F_SETFD, FD_CLOEXEC);
}
bool sameUser(int fd) {
#ifdef __linux__
  struct ucred c{}; socklen_t size = sizeof(c);
  return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &c, &size) == 0 && c.uid == getuid();
#else
  uid_t uid; gid_t gid; return getpeereid(fd, &uid, &gid) == 0 && uid == getuid();
#endif
}
}
int runServer(const Config& config, std::atomic_bool& interrupted) {
  config.validate();
  const auto parent = std::filesystem::path(config.socketPath).parent_path();
  if (!std::filesystem::exists(parent)) {
    if (::mkdir(parent.c_str(), 0700) < 0) throw Error("SOCKET_ERROR", "Cannot create private socket directory");
  }
  struct stat st{};
  if (lstat(parent.c_str(), &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 0022))
    throw Error("SOCKET_ERROR", "Socket parent must be owned by this user and not writable by other users");
  FD lock;
  lock.n = ::open((config.socketPath + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (lock.n < 0 || flock(lock.n, LOCK_EX | LOCK_NB) < 0) throw Error("ALREADY_RUNNING", "Another daemon owns this socket");
  if (lstat(config.socketPath.c_str(), &st) == 0) {
    if (!S_ISSOCK(st.st_mode) || st.st_uid != getuid()) throw Error("SOCKET_ERROR", "Refusing to replace a non-socket or foreign path");
    ::unlink(config.socketPath.c_str());
  }
  FD listener; listener.n = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener.n < 0) throw Error("SOCKET_ERROR", std::strerror(errno));
  nonblocking(listener.n);
  sockaddr_un address{}; address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, config.socketPath.c_str(), config.socketPath.size() + 1);
  if (bind(listener.n, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || chmod(config.socketPath.c_str(), 0600) < 0 || listen(listener.n, 4) < 0)
    throw Error("SOCKET_ERROR", std::strerror(errno));
  struct Cleanup { std::string path; ~Cleanup(){ ::unlink(path.c_str()); } } cleanup{config.socketPath};
  std::mutex outputMutex; std::deque<Packet> output;
  size_t queuedBytes = 0, queuedFrames = 0;
  std::atomic_bool outputOverflow{false};
  std::atomic_uint64_t currentSession{0};
  std::atomic_bool touchSuppressed{false};
  auto emit = [&](Packet p) {
    std::lock_guard guard(outputMutex);
    if (p.session == 0 || p.session != currentSession) return false;
    if (p.meta.value("event", "") == "touch.event") {
      if (touchSuppressed) return true;
      if (p.meta.value("phase", "") == "move") {
        for (auto i = output.rbegin(); i != output.rend(); ++i) {
          if (i->meta.value("event", "") == "touch.event" && i->meta.value("contactId", "") == p.meta.value("contactId", "")) {
            if (i->meta.value("phase", "") == "move") { *i = std::move(p); return true; }
            break;
          }
        }
      }
      size_t touches = 0;
      for (const auto& queued : output) if (queued.meta.value("event", "") == "touch.event") ++touches;
      if (touches >= 32) {
        std::erase_if(output, [](const Packet& item) { return item.meta.value("event", "") == "touch.event"; });
        p.meta.update({{"phase", "cancel"}, {"contactId", "*"}, {"reason", "TOUCH_OVERFLOW"}, {"x", 0}, {"y", 0}});
        touchSuppressed = true; // Reserve the rest of the queue for device/control events.
      }
    }
    const bool frame = p.meta.value("event", "") == "audio.frame";
    if (frame && queuedFrames >= 50) return false;
    if (output.size() >= 256 || queuedBytes + p.data.size() > 2 * 1024 * 1024) {
      if (!frame) outputOverflow = true;
      return false;
    }
    if (frame) ++queuedFrames;
    queuedBytes += p.data.size(); output.push_back(std::move(p)); return true;
  };
  Runtime runtime(config, emit);
  FD client; uint64_t serial = 0;
  Decoder decoder; Bytes sending; size_t sent = 0;
  std::set<std::string> seen; std::deque<std::string> seenOrder;
  bool greeted = false;
  auto lastReceived = Clock::now();
  auto disconnect = [&] {
    currentSession = 0;
    touchSuppressed = false;
    if (client.n >= 0) { ::close(client.n); client.n = -1; }
    runtime.disconnect(); decoder.clear(); sending.clear(); sent = 0; greeted = false;
    seen.clear(); seenOrder.clear();
    std::lock_guard guard(outputMutex); output.clear(); queuedBytes = 0; queuedFrames = 0; outputOverflow = false;
  };
  std::cout << Json({{"status", "listening"}, {"socket", config.socketPath}, {"mode", config.mock ? "mock" : "real"}}).dump() << std::endl;
  while (!interrupted) {
    if (!runtime.tick()) break;
    if (client.n >= 0 && (outputOverflow || Clock::now() - lastReceived > std::chrono::milliseconds(config.leaseMs))) disconnect();
    pollfd descriptors[2]{{listener.n, POLLIN, 0}, {client.n, short(POLLIN | (!sending.empty() ? POLLOUT : 0)), 0}};
    int ready = poll(descriptors, 2, 5);
    if (ready < 0 && errno != EINTR) throw Error("SOCKET_ERROR", std::strerror(errno));
    if (descriptors[0].revents & POLLIN) {
      int fd = accept(listener.n, nullptr, nullptr);
      if (fd >= 0) {
        if (client.n >= 0 || !sameUser(fd)) ::close(fd);
        else { client.n = fd; nonblocking(fd); currentSession = ++serial; lastReceived = Clock::now(); }
      }
    }
    if (client.n < 0) continue;
    if (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) { disconnect(); continue; }
    if (descriptors[1].revents & POLLIN) {
      uint8_t bytes[8192]; const auto n = recv(client.n, bytes, sizeof(bytes), 0);
      if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) { disconnect(); continue; }
      if (n > 0) {
        try {
          auto packets = decoder.feed({bytes, size_t(n)});
          for (auto& p : packets) {
            p.session = currentSession;
            if (p.meta.value("version", 0) != 1 || p.meta.value("type", "") != "command") throw Error("PROTOCOL_ERROR", "Expected a version 1 command");
            const auto id = requiredString(p.meta, "commandId"); const auto op = requiredString(p.meta, "command");
            if (!greeted && op != "hello") throw Error("PROTOCOL_ERROR", "hello must be the first command");
            lastReceived = Clock::now();
            if (!seen.insert(id).second) { emit(failure(p, "DUPLICATE_COMMAND", "Use a unique commandId; commands are not replayed")); continue; }
            seenOrder.push_back(id); if (seenOrder.size() > 512) { seen.erase(seenOrder.front()); seenOrder.pop_front(); }
            greeted = true;
            if (op == "touch.subscribe") touchSuppressed = false;
            runtime.command(std::move(p));
          }
        } catch (const std::exception& e) { std::cerr << "Protocol connection closed: " << e.what() << '\n'; disconnect(); continue; }
      }
    }
    if (sending.empty()) {
      std::lock_guard guard(outputMutex);
      while (!output.empty()) {
        auto p = std::move(output.front()); output.pop_front(); queuedBytes -= p.data.size();
        if (p.meta.value("event", "") == "audio.frame") --queuedFrames;
        if (p.session != currentSession) continue;
        sending = encode(p); sent = 0; break;
      }
    }
    if (!sending.empty()) {
      const auto n = send(client.n, sending.data() + sent, sending.size() - sent, 0);
      if (n > 0) { sent += size_t(n); if (sent == sending.size()) { sending.clear(); sent = 0; } }
      else if (n < 0 && errno != EAGAIN && errno != EINTR) { disconnect(); continue; }
    }
  }
  disconnect(); return 0;
}
}
