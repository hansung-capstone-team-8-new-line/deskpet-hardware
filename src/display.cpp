#include "deskpet/display.hpp"
#include <cmath>
#include <filesystem>
#include <set>
#ifdef DESKPET_SDL
#include <SDL.h>
#include <SDL_ttf.h>
#endif

namespace deskpet {
struct Display::Impl {
  Config config;
  Json view = {{"displayMode", "face"}, {"activity", "idle"}};
  std::optional<Packet> pending;
  uint64_t revision = 0;
  bool revisionSet = false, failNext = false;
  std::string error;
  Clock::time_point last = Clock::now() - std::chrono::seconds(1), start = Clock::now();
#ifdef DESKPET_SDL
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* canvas = nullptr;
  SDL_Texture* presented = nullptr;
  TTF_Font* font = nullptr;
  bool sdl = false, ttf = false;
  std::vector<std::string> lines;
  ~Impl() {
    if (font) TTF_CloseFont(font);
    if (canvas) SDL_DestroyTexture(canvas);
    if (presented) SDL_DestroyTexture(presented);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (ttf) TTF_Quit();
    if (sdl) SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
  void color(int r, int g, int b) { SDL_SetRenderDrawColor(renderer, uint8_t(r), uint8_t(g), uint8_t(b), 255); }
  void ellipse(float cx, float cy, float rx, float ry) {
    for (int y = -int(ry); y <= int(ry); ++y) {
      float x = rx * std::sqrt(std::max(0.0f, 1.0f - float(y * y) / (ry * ry)));
      SDL_RenderDrawLineF(renderer, cx - x, cy + y, cx + x, cy + y);
    }
  }
  void text(const std::string& s, int x, int y) {
    if (s.empty()) return;
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, s.c_str(), SDL_Color{223, 237, 242, 255});
    if (!surface) throw Error("DISPLAY_ERROR", TTF_GetError());
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect dst{x, y, surface->w, surface->h}; SDL_FreeSurface(surface);
    if (!texture) throw Error("DISPLAY_ERROR", SDL_GetError());
    int rc = SDL_RenderCopy(renderer, texture, nullptr, &dst); SDL_DestroyTexture(texture);
    if (rc < 0) throw Error("DISPLAY_ERROR", SDL_GetError());
  }
  std::vector<std::string> wrap(const std::string& s) {
    std::vector<std::string> result; std::string line;
    const int available = config.width - 160;
    for (size_t i = 0; i < s.size();) {
      const auto lead = uint8_t(s[i]);
      size_t n = lead < 0x80 ? 1 : (lead < 0xe0 ? 2 : (lead < 0xf0 ? 3 : 4));
      const auto point = s.substr(i, n); i += n;
      if (point == "\n") { result.push_back(line); line.clear(); continue; }
      int width = 0;
      if (TTF_SizeUTF8(font, (line + point).c_str(), &width, nullptr) < 0) throw Error("DISPLAY_ERROR", TTF_GetError());
      if (width > available && !line.empty()) { result.push_back(line); line.clear(); }
      line += point;
    }
    if (!line.empty()) result.push_back(line);
    if (int(result.size()) * TTF_FontLineSkip(font) > config.height - 200)
      throw Error("DISPLAY_TEXT_TOO_LONG", "Text does not fit; the pipeline must provide a shorter displayText");
    return result;
  }
  void draw() {
    if (SDL_SetRenderTarget(renderer, canvas) < 0) throw Error("DISPLAY_ERROR", SDL_GetError());
    color(10, 19, 29); SDL_RenderClear(renderer);
    if (view.value("displayMode", "face") == "text") {
      color(71, 211, 186); SDL_Rect bar{80, 74, 56, 6}; SDL_RenderFillRect(renderer, &bar);
      int y = 130;
      for (const auto& line : lines) { text(line, 80, y); y += TTF_FontLineSkip(font); }
    } else {
      const float w = float(config.width), h = float(config.height);
      const auto activity = view.value("activity", "idle");
      const auto status = view.value("actionStatus", "");
      const auto emotion = view.value("emotion", "neutral");
      const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
      const float blink = std::fmod(seconds + 1.0, 5.1) < 0.14 ? 0.1f : 1.0f;
      const bool sad = status == "failed" || emotion == "sad";
      const bool happy = status == "succeeded" || emotion == "happy";
      if (sad) color(241, 142, 152);
      else if (status == "unknown") color(247, 199, 106);
      else color(112, 238, 215);
      float eyeHeight = h * (happy ? 0.07f : 0.12f) * blink;
      if (activity == "listening") eyeHeight *= 1.15f;
      if (activity == "processing") eyeHeight *= 0.6f;
      float offset = activity == "processing" ? float(std::sin(seconds * 2) * 18) : 0;
      ellipse(w * 0.34f + offset, h * 0.40f, w * 0.05f, eyeHeight);
      ellipse(w * 0.66f + offset, h * (activity == "awaiting_answer" ? 0.37f : 0.40f), w * 0.05f, eyeHeight);
      if (activity == "speaking") ellipse(w * 0.5f, h * 0.67f, w * 0.034f, h * float(0.025 + 0.035 * (1 + std::sin(seconds * 12))));
      else {
        for (int t = -3; t <= 3; ++t) {
          float previousX = w * 0.43f, previousY = h * 0.64f;
          for (int i = 1; i <= 50; ++i) {
            float x = w * (0.43f + 0.14f * float(i) / 50);
            float y = h * (0.64f + (sad ? -0.035f : 0.035f) * std::sin(float(i) / 50 * 3.14159f));
            SDL_RenderDrawLineF(renderer, previousX, previousY + t, x, y + t); previousX = x; previousY = y;
          }
        }
      }
      std::string label = "";
      if (activity == "listening") label = "듣고 있어요";
      if (activity == "processing") label = "생각하고 있어요";
      if (activity == "awaiting_answer") label = "대답을 기다려요";
      if (status == "unknown") label = "결과를 확인해야 해요";
      if (!label.empty() && font) {
        int tw = 0; TTF_SizeUTF8(font, label.c_str(), &tw, nullptr);
        text(label, (config.width - tw) / 2, int(h * 0.83f));
      }
    }
    if (SDL_SetRenderTarget(renderer, presented) < 0) throw Error("DISPLAY_ERROR", SDL_GetError());
    color(10, 19, 29); SDL_RenderClear(renderer);
    const bool sideways = config.rotation == 90 || config.rotation == 270;
    const int pw = sideways ? config.height : config.width, ph = sideways ? config.width : config.height;
    SDL_Rect dst{(pw - config.width) / 2, (ph - config.height) / 2, config.width, config.height};
    if (SDL_RenderCopyEx(renderer, canvas, nullptr, &dst, config.rotation, nullptr, SDL_FLIP_NONE) < 0)
      throw Error("DISPLAY_ERROR", SDL_GetError());
    if (SDL_SetRenderTarget(renderer, nullptr) < 0) throw Error("DISPLAY_ERROR", SDL_GetError());
    if (SDL_RenderCopy(renderer, presented, nullptr, nullptr) < 0) throw Error("DISPLAY_ERROR", SDL_GetError());
    SDL_RenderPresent(renderer); // A submitted frame, not optical confirmation from the panel.
  }
#endif
};
Display::Display(const Config& c) : impl_(std::make_unique<Impl>()) {
  auto& d = *impl_; d.config = c;
  if (c.headless) { if (!c.mock) d.error = "Headless display is only available in mock mode"; return; }
#ifdef DESKPET_SDL
  try {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) throw Error("DISPLAY_ERROR", SDL_GetError());
    d.sdl = true;
    if (TTF_Init() < 0) throw Error("DISPLAY_ERROR", TTF_GetError());
    d.ttf = true;
    auto font = c.font;
    if (font.empty()) for (auto path : {"/System/Library/Fonts/AppleSDGothicNeo.ttc", "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"})
      if (std::filesystem::exists(path)) { font = path; break; }
    d.font = TTF_OpenFont(font.c_str(), c.fontSize);
    if (!d.font) throw Error("DISPLAY_ERROR", "Korean font missing: configure display.font");
    const bool sideways = c.rotation == 90 || c.rotation == 270;
    const int pw = sideways ? c.height : c.width, ph = sideways ? c.width : c.height;
    d.window = SDL_CreateWindow(c.mock ? "DeskPet · 모의 실행" : "DeskPet", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      c.fullscreen ? pw : pw / 2, c.fullscreen ? ph : ph / 2, c.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE);
    if (!d.window) throw Error("DISPLAY_ERROR", SDL_GetError());
    d.renderer = SDL_CreateRenderer(d.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!d.renderer) d.renderer = SDL_CreateRenderer(d.window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!d.renderer) throw Error("DISPLAY_ERROR", SDL_GetError());
    SDL_RenderSetLogicalSize(d.renderer, pw, ph);
    d.canvas = SDL_CreateTexture(d.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, c.width, c.height);
    if (!d.canvas) throw Error("DISPLAY_ERROR", SDL_GetError());
    d.presented = SDL_CreateTexture(d.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, pw, ph);
    if (!d.presented) throw Error("DISPLAY_ERROR", SDL_GetError());
  } catch (const std::exception& e) { d.error = e.what(); }
#else
  d.error = "SDL renderer was not built; use --headless only for mock runs";
#endif
}
Display::~Display() = default;
void Display::submit(Packet cmd, Emit emit) {
  auto& d = *impl_;
  if (!d.error.empty()) throw Error("DISPLAY_ERROR", d.error);
  if (d.failNext) { d.failNext = false; throw Error("DISPLAY_ERROR", "Injected display failure"); }
  const uint64_t rev = cmd.meta.at("revision").get<uint64_t>();
  if (!cmd.meta.at("revision").is_number_unsigned()) throw Error("INVALID_COMMAND", "revision must be a nonnegative integer");
  if (d.revisionSet && rev <= d.revision) throw Error("STALE_OUTPUT", "Display revision is no longer current");
  Json view = cmd.meta.value("view", Json::object());
  if (cmd.meta.at("command") == "display.idle") view = {{"displayMode", "face"}, {"activity", "idle"}};
  const auto mode = view.value("displayMode", "face");
  const auto activity = view.value("activity", "idle");
  static const std::set<std::string> activities{"idle", "listening", "processing", "speaking", "awaiting_answer"};
  if (!activities.count(activity) || (mode != "face" && mode != "text")) throw Error("INVALID_COMMAND", "Unsupported display mode or activity");
  if (mode == "text") {
    auto text = view.value("displayText", "");
    if (text.empty() || text.size() > 4096) throw Error("INVALID_COMMAND", "displayText must contain 1..4096 UTF-8 bytes");
#ifdef DESKPET_SDL
    if (!d.config.headless) d.lines = d.wrap(text);
#endif
  }
  if (d.pending) emit(failure(*d.pending, "SUPERSEDED", "A newer display command replaced this frame"));
  d.revision = rev; d.revisionSet = true; d.view = std::move(view); d.pending = std::move(cmd);
  d.last = Clock::now() - std::chrono::seconds(1);
}
bool Display::tick(Emit emit) {
  auto& d = *impl_;
#ifdef DESKPET_SDL
  if (d.sdl) { SDL_Event e; while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) return false; }
#endif
  if (!d.error.empty() || Clock::now() - d.last < std::chrono::milliseconds(33)) return true;
  d.last = Clock::now();
  try {
#ifdef DESKPET_SDL
    if (!d.config.headless) d.draw();
#endif
    if (d.pending) {
      emit(event(*d.pending, "display.displayed", {{"revision", d.revision}, {"simulated", d.config.mock}}));
      emit(reply(*d.pending, {{"displayed", true}, {"simulated", d.config.mock}})); d.pending.reset();
    }
  } catch (const std::exception& e) {
    d.error = e.what();
    if (d.pending) { emit(failure(*d.pending, "DISPLAY_ERROR", d.error)); d.pending.reset(); }
  }
  return true;
}
void Display::reset() {
  auto& d = *impl_; d.pending.reset(); d.revisionSet = false;
  d.view = {{"displayMode", "face"}, {"activity", "idle"}};
}
void Display::injectFailure() { impl_->failNext = true; }
Json Display::health() const { return {{"available", impl_->error.empty()}, {"error", impl_->error}, {"simulated", impl_->config.mock}}; }
void Display::snapshot(const std::string& path) {
#ifdef DESKPET_SDL
  auto& d = *impl_;
  if (!d.config.mock || d.config.headless || !d.error.empty()) throw Error("DISPLAY_ERROR", "Snapshots require a working mock SDL display");
  int w = 0, h = 0; SDL_QueryTexture(d.presented, nullptr, nullptr, &w, &h);
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) throw Error("DISPLAY_ERROR", SDL_GetError());
  // The retained composition includes rotation; never read an undefined post-Present backbuffer.
  SDL_SetRenderTarget(d.renderer, d.presented);
  int rc = SDL_RenderReadPixels(d.renderer, nullptr, s->format->format, s->pixels, s->pitch);
  SDL_SetRenderTarget(d.renderer, nullptr);
  if (rc == 0) rc = SDL_SaveBMP(s, path.c_str());
  SDL_FreeSurface(s);
  if (rc < 0) throw Error("DISPLAY_ERROR", SDL_GetError());
#else
  (void)path; throw Error("DISPLAY_ERROR", "SDL was not built");
#endif
}
}
