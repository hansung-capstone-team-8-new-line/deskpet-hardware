#include "deskpet/runtime.hpp"
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
#ifdef DESKPET_LINUX
#include <alsa/asoundlib.h>
#endif

using namespace deskpet;
static std::atomic_bool interrupted{false};
static void signalHandler(int) { interrupted.store(true); }
static std::string defaultSocket() {
  const char* xdg = std::getenv("XDG_RUNTIME_DIR");
  return xdg ? std::string(xdg) + "/deskpet-hardware.sock" : "/tmp/deskpet-" + std::to_string(getuid()) + "/hardware.sock";
}
static void doctor() {
  Json result{{"mode", "read-only diagnostics"}, {"socketDefault", defaultSocket()}};
#ifdef DESKPET_LINUX
  result["alsa"] = Json::array(); void** hints = nullptr;
  if (snd_device_name_hint(-1, "pcm", &hints) >= 0) {
    for (auto h = hints; *h; ++h) {
      char* name = snd_device_name_get_hint(*h, "NAME"); char* desc = snd_device_name_get_hint(*h, "DESC");
      if (name) result["alsa"].push_back({{"name", name}, {"description", desc ? desc : ""}});
      free(name); free(desc);
    }
    snd_device_name_free_hint(hints);
  }
  result["i2c"] = Json::array();
  if (std::filesystem::exists("/sys/class/i2c-dev")) for (const auto& entry : std::filesystem::directory_iterator("/sys/class/i2c-dev")) {
    std::ifstream in(entry.path() / "name"); std::string name; std::getline(in, name);
    result["i2c"].push_back({{"device", "/dev/" + entry.path().filename().string()}, {"name", name}});
  }
  result["servo"] = {{"oeMode", "grounded"}, {"stopMethod", "PCA9685 FULL_OFF"}};
  std::ifstream release("/etc/nv_tegra_release"); std::string version; std::getline(release, version); result["jetsonRelease"] = version;
#else
  result["hardware"] = "Linux drivers are not built. Mock operation is available.";
#endif
#ifdef DESKPET_SDL
  result["sdlBuilt"] = true;
#else
  result["sdlBuilt"] = false;
#endif
  std::cout << result.dump(2) << '\n';
}
int main(int argc, char** argv) {
  try {
    std::string configFile; std::string socketOverride; bool headless = false, mock = false;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      auto value = [&]() -> std::string { if (++i >= argc) throw Error("ARGUMENT_ERROR", "Missing value for " + a); return argv[i]; };
      if (a == "--config") configFile = value();
      else if (a == "--socket") socketOverride = value();
      else if (a == "--headless") headless = true;
      else if (a == "--mock") mock = true;
      else if (a == "doctor") { doctor(); return 0; }
      else if (a == "--help") { std::cout << "deskpet-hardware [--config FILE] [--socket PATH] [--mock] [--headless]\ndeskpet-hardware doctor\n"; return 0; }
      else throw Error("ARGUMENT_ERROR", "Unknown argument: " + a);
    }
    Config c = Config::load(configFile);
    if (!socketOverride.empty()) c.socketPath = socketOverride;
    if (c.socketPath.empty()) c.socketPath = defaultSocket();
    if (headless) c.headless = true;
    if (mock) c.mock = true;
    std::signal(SIGINT, signalHandler); std::signal(SIGTERM, signalHandler); std::signal(SIGPIPE, SIG_IGN);
    return runServer(c, interrupted);
  } catch (const Error& e) { std::cerr << Json({{"error", e.code}, {"message", e.what()}}).dump() << '\n'; }
  catch (const std::exception& e) { std::cerr << Json({{"error", "STARTUP_ERROR"}, {"message", e.what()}}).dump() << '\n'; }
  return 1;
}
