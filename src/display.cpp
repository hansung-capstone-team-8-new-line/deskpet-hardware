#include "deskpet/display.hpp"
#include <cmath>
#include <filesystem>
#include <functional>
#include <set>
#ifdef DESKPET_SDL
#include <SDL.h>
#include <SDL_ttf.h>
#endif

namespace deskpet {
#ifdef DESKPET_SDL
namespace {
// Faces are signed distance fields in a 1024x600 design space, sampled per dot.
// The catalog page docs/face-catalog/index.html renders the same parameters.
constexpr float kPi = 3.14159265f, kDesignW = 1024, kDesignH = 600;
struct Rgb { uint8_t r, g, b; };
constexpr Rgb kMint{112, 238, 215}, kPink{241, 142, 152}, kYellow{247, 199, 106}, kSpark{232, 246, 250};
struct Layer {
  Rgb color; float alpha, x0, y0, x1, y1;
  std::function<float(float, float)> distance;
};
struct Face { std::vector<Layer> layers; std::string label; };

float sdBox(float x, float y, float cx, float cy, float hw, float hh, float r) {
  const float qx = std::abs(x - cx) - hw + r, qy = std::abs(y - cy) - hh + r;
  return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
}
float sdCircle(float x, float y, float cx, float cy, float r) { return std::hypot(x - cx, y - cy) - r; }
float sdSegment(float x, float y, float ax, float ay, float bx, float by) {
  const float px = x - ax, py = y - ay, vx = bx - ax, vy = by - ay;
  const float h = std::clamp((px * vx + py * vy) / (vx * vx + vy * vy), 0.0f, 1.0f);
  return std::hypot(px - vx * h, py - vy * h);
}
float sdArc(float x, float y, float cx, float cy, float r, float a0, float a1, float th) {
  const float dx = x - cx, dy = y - cy, angle = std::atan2(dy, dx);
  if (angle >= a0 && angle <= a1) return std::abs(std::hypot(dx, dy) - r) - th;
  return std::min(std::hypot(x - cx - r * std::cos(a0), y - cy - r * std::sin(a0)),
                  std::hypot(x - cx - r * std::cos(a1), y - cy - r * std::sin(a1))) - th;
}
constexpr float kUp0 = -kPi + 0.5f, kUp1 = -0.5f, kDown0 = 0.35f, kDown1 = kPi - 0.35f;

Layer arc(float cx, float cy, float r, float th, float a0, float a1, Rgb c, float alpha = 1) {
  return {c, alpha, cx - r - th, cy - r - th, cx + r + th, cy + r + th,
          [=](float x, float y) { return sdArc(x, y, cx, cy, r, a0, a1, th); }};
}
Layer segment(float ax, float ay, float bx, float by, float th, Rgb c, float alpha) {
  return {c, alpha, std::min(ax, bx) - th, std::min(ay, by) - th, std::max(ax, bx) + th, std::max(ay, by) + th,
          [=](float x, float y) { return sdSegment(x, y, ax, ay, bx, by) - th; }};
}
Layer eye(float cx, float cy, float hw, float hh, Rgb c, bool glint, bool sad, float side) {
  const float r = std::min(hw, hh);
  // Sad eyes drop toward the outside: cut the top with a slanted half-plane.
  const float ix = cx - side * hw * 1.15f, iy = cy - hh * 0.8f, ox = cx + side * hw * 1.15f, oy = cy - hh * 0.05f;
  const float len = std::hypot(ox - ix, oy - iy);
  float nx = (oy - iy) / len, ny = -(ox - ix) / len;
  if (ny > 0) { nx = -nx; ny = -ny; }
  const bool shine = glint && hh > hw * 0.55f;
  const float gx = sad ? cx - side * hw * 0.25f : cx + hw * 0.32f, gy = sad ? cy + hh * 0.3f : cy - hh * 0.42f, gr = r * 0.3f;
  return {c, 1, cx - hw, cy - hh, cx + hw, cy + hh, [=](float x, float y) {
    float d = sdBox(x, y, cx, cy, hw, hh, r);
    if (sad) d = std::max(d, (x - ix) * nx + (y - iy) * ny);
    if (shine) d = std::max(d, -sdCircle(x, y, gx, gy, gr));
    return d;
  }};
}
float glance(float v) { return (v < 0 ? -1.0f : 1.0f) * std::clamp((std::abs(v) - 0.75f) / 0.2f, 0.0f, 1.0f); }

Face buildFace(const std::string& activity, const std::string& emotion, const std::string& status,
               float t, bool whiskers, const std::string& mouthMode) {
  Face face;
  auto& L = face.layers;
  const bool sad = emotion == "sad" || status == "failed";
  const bool petted = emotion == "petted", sleepy = emotion == "sleepy", surprised = emotion == "surprised";
  const bool happy = petted || emotion == "happy" || status == "succeeded";
  const Rgb col = sad ? kPink : status == "unknown" ? kYellow : kMint;
  float hw = 51, hh = 72, cy = 240, lookX = 0, lookY = 0, raiseRight = 0;
  float bob = std::sin(t * 1.6f) * 3, lift = 0, twitch = std::sin(t * 1.3f) * 1.5f;
  bool glint = true;
  const float phase = std::fmod(t + 1.0f, 5.1f);
  float blink = phase < 0.2f ? std::max(0.07f, std::abs(std::cos(phase / 0.2f * kPi))) : 1;

  if (activity == "idle") lookX = 26 * glance(std::sin(t * 0.37f));
  if (activity == "listening") { hh *= 1.15f; hw *= 1.08f; lift = -12; bob = std::sin(t * 2.4f) * 2; }
  if (activity == "processing") { hh *= 0.6f; lookX = std::sin(t * 2.2f) * 46; glint = false; twitch = std::sin(t * 9) * 4; }
  if (activity == "speaking") bob = -std::abs(std::sin(t * 6.5f)) * 6;
  if (activity == "awaiting_answer") { raiseRight = -18; lookX = 12; lookY = -8; twitch = std::sin(t * 2.5f) * 4; }
  if (sad) {
    hh *= 0.9f; cy += 10; lift += 24; bob *= 0.5f;
    blink = phase < 0.3f ? std::max(0.07f, std::abs(std::cos(phase / 0.3f * kPi))) : 1;
  }
  if (status == "unknown") { lookX = 26 + std::sin(t * 1.1f) * 6; lookY = -12; }
  if (petted) { twitch = std::sin(t * 14) * 6; lift = -6; }
  if (surprised) lift = -22;
  if (sleepy) { lift = 14; bob = std::sin(t * 0.9f) * 5; }

  for (const float side : {-1.0f, 1.0f}) {
    const float cx = (side < 0 ? 348.0f : 676.0f) + lookX, ey = cy + bob + lookY + (side > 0 ? raiseRight : 0);
    if (happy && activity != "processing") {
      const float hop = -std::abs(std::sin(t * (petted ? 5.0f : 3.0f))) * 6;
      L.push_back(arc(cx, ey + 26 + hop, hw * 1.1f, 11, kUp0, kUp1, col));
    } else if (surprised) {
      const float s = 62 * (1 + std::max(0.0f, std::sin(t * 4)) * 0.04f);
      L.push_back(eye(cx, ey - 6, s, s * blink, col, true, false, side));
    } else if (sleepy) {
      L.push_back(eye(cx, ey + 34, hw * 1.05f, 7 + std::max(0.0f, std::sin(t * 0.7f + 1)) * 9, col, false, false, side));
    } else {
      L.push_back(eye(cx, ey, hw, hh * blink, col, glint && blink > 0.6f, sad, side));
    }
  }

  if (whiskers) for (const float side : {-1.0f, 1.0f}) for (const float k : {-1.0f, 0.0f, 1.0f}) {
    const float ix = 512 + side * 252, iy = 388 + k * 24 + lift * 0.25f;
    const float wobble = twitch * (k == 0 ? 1 : -1) * side * 0.5f + twitch * 0.5f;
    L.push_back(segment(ix, iy, 512 + side * 432, iy + k * 28 + lift + wobble, 3.2f, col, 0.55f));
  }

  const auto mouth = [&](float open) {
    L.push_back(arc(497, 394, 15, 3.6f, kDown0, kDown1, col));
    L.push_back(arc(527, 394, 15, 3.6f, kDown0, kDown1, col));
    if (open > 0.12f) {
      const float my = 412 + open * 7, mh = 4 + open * 9;
      L.push_back({col, 0.9f, 495, 404, 529, 440, [=](float x, float y) { return sdBox(x, y, 512, my, 12, mh, std::min(mh, 12.0f)); }});
    }
  };
  if (mouthMode != "none") {
    if (surprised) L.push_back(arc(512, 412, 13, 4, -kPi, kPi, col));
    else if (activity == "speaking")
      mouth(std::clamp(0.5f + 0.5f * (std::sin(t * 10.5f) * 0.7f + std::sin(t * 6.3f + 1) * 0.5f), 0.0f, 1.0f));
    else if (petted) mouth(0);
    else if (mouthMode == "always") {
      if (sad) L.push_back(arc(512, 428, 20, 3.6f, kUp0, kUp1, col));
      else if (!sleepy) mouth(0);
    }
  }

  if (status == "succeeded") {
    const float spots[3][3] = {{792, 140, 0}, {852, 205, 1.7f}, {210, 160, 3.1f}};
    for (const auto& p : spots) {
      const float s = 6 + 12 * std::max(0.0f, std::sin(t * 2.6f + p[2])), x = p[0], y = p[1];
      if (s > 7) L.push_back({kSpark, 1, x - s, y - s, x + s, y + s, [=](float px, float py) {
        return std::min(sdBox(px, py, x, y, s * 0.17f, s, s * 0.17f), sdBox(px, py, x, y, s, s * 0.17f, s * 0.17f)); }});
    }
  }
  if (status == "unknown") {
    const float x = 430, y = 150 + std::fmod(t * 18, 30.0f);
    L.push_back({kSpark, 0.85f, x - 12, y - 22, x + 12, y + 18, [=](float px, float py) {
      return std::min(sdCircle(px, py, x, y + 4, 10), sdSegment(px, py, x, y - 14, x, y + 2) - 3.5f - (py - y + 14) * 0.2f); }});
  }
  if (petted) for (const float x : {320.0f, 704.0f})
    L.push_back({kPink, 0.45f, x - 30, 308, x + 30, 368, [=](float px, float py) { return sdCircle(px, py, x, 338, 30); }});
  if (sleepy) for (int i = 0; i < 2; ++i) {
    const float q = std::fmod(t * 0.35f + i * 0.5f, 1.0f), x = 760 + q * 70 + i * 20, y = 210 - q * 110, h = (22 + q * 18 + i * 6) / 2;
    L.push_back({col, std::sin(q * kPi), x - h - 4, y - h - 4, x + h + 4, y + h + 4, [=](float px, float py) {
      return std::min({sdSegment(px, py, x - h, y - h, x + h, y - h), sdSegment(px, py, x + h, y - h, x - h, y + h),
                       sdSegment(px, py, x - h, y + h, x + h, y + h)}) - 3.5f; }});
  }

  if (activity == "listening") face.label = "듣고 있어요";
  if (activity == "processing") face.label = "생각하고 있어요";
  if (activity == "awaiting_answer") face.label = "대답을 기다려요";
  if (status == "unknown") face.label = "결과를 확인해야 해요";
  return face;
}
}
#endif

struct Display::Impl {
  Config config;
  Json view = {{"displayMode", "face"}, {"activity", "idle"}};
  std::optional<Packet> pending;
  std::optional<Packet> touchSubscription;
  std::map<std::string, Json> contacts, moved;
  Clock::time_point touchWindow = Clock::now();
  size_t touchEdges = 0;
  void touch(std::string phase, const std::string& id, double x, double y, bool simulated, Emit emit) {
    if (!touchSubscription) return;
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1 || id.empty() || id.size() > 128)
      throw Error("INVALID_TOUCH", "Invalid contact or normalized coordinates");
    if (phase != "down" && phase != "move" && phase != "up") throw Error("INVALID_TOUCH", "Expected down/move/up");
    if (phase == "down" && contacts.count(id)) return;
    if (phase != "down" && !contacts.count(id)) return;
    if (Clock::now() - touchWindow >= std::chrono::seconds(1)) { touchWindow = Clock::now(); touchEdges = 0; }
    if ((phase != "move" && ++touchEdges > 64) || (phase == "down" && contacts.size() >= 10)) {
      emit(event(*touchSubscription, "touch.event", {{"phase", "cancel"}, {"contactId", "*"}, {"x", 0}, {"y", 0}, {"simulated", simulated}, {"reason", "TOUCH_OVERFLOW"}}));
      contacts.clear(); moved.clear(); touchSubscription.reset(); return;
    }
    // SDL normalizes touch against the logical viewport; invert our canvas rotation.
    const double ox = x, oy = y;
    if (config.rotation == 90) { x = oy; y = 1 - ox; }
    if (config.rotation == 180) { x = 1 - ox; y = 1 - oy; }
    if (config.rotation == 270) { x = 1 - oy; y = ox; }
    Json fields{{"phase", phase}, {"contactId", id}, {"x", x}, {"y", y}, {"simulated", simulated}};
    if (phase == "move") { contacts[id] = fields; moved[id] = fields; return; }
    if (moved.count(id)) { emit(event(*touchSubscription, "touch.event", moved[id])); moved.erase(id); }
    emit(event(*touchSubscription, "touch.event", fields));
    if (phase == "down") contacts[id] = fields; else contacts.erase(id);
  }
  uint64_t revision = 0;
  bool revisionSet = false, failNext = false;
  std::string error;
  Clock::time_point last = Clock::now() - std::chrono::seconds(1), start = Clock::now();
#ifdef DESKPET_SDL
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* canvas = nullptr;
  SDL_Texture* presented = nullptr;
  SDL_Texture* dot = nullptr;    // white anti-aliased disc, tinted per dot
  SDL_Texture* unlit = nullptr;  // background with the dim dot grid
  SDL_Texture* glow = nullptr;   // half-resolution lit dots, stretched for bloom
  TTF_Font* font = nullptr;
  bool sdl = false, ttf = false;
  std::vector<std::string> lines;
  std::vector<float> level;
  std::vector<Rgb> tint;
  ~Impl() {
    if (dot) SDL_DestroyTexture(dot);
    if (unlit) SDL_DestroyTexture(unlit);
    if (glow) SDL_DestroyTexture(glow);
    if (font) TTF_CloseFont(font);
    if (canvas) SDL_DestroyTexture(canvas);
    if (presented) SDL_DestroyTexture(presented);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (ttf) TTF_Quit();
    if (sdl) SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
  void color(int r, int g, int b) { SDL_SetRenderDrawColor(renderer, uint8_t(r), uint8_t(g), uint8_t(b), 255); }
  struct Grid { int cols, rows; float cell, top; };
  Grid grid() const {
    const int cols = std::max(1, int(std::lround(float(config.width) / float(config.dotSize))));
    const float cell = float(config.width) / float(cols);
    const int rows = int(float(config.height) / cell);
    return {cols, rows, cell, (float(config.height) - float(rows) * cell) / 2};
  }
  void stamp(float cx, float cy, float r, Rgb c, uint8_t alpha) {
    SDL_SetTextureColorMod(dot, c.r, c.g, c.b); SDL_SetTextureAlphaMod(dot, alpha);
    SDL_FRect dst{cx - r, cy - r, 2 * r, 2 * r};
    SDL_RenderCopyF(renderer, dot, nullptr, &dst);
  }
  void prepareDots(const Grid& g) {
    constexpr int size = 64;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) throw Error("DISPLAY_ERROR", SDL_GetError());
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
      const float d = std::hypot(x + 0.5f - size / 2.0f, y + 0.5f - size / 2.0f);
      const auto a = uint32_t(std::clamp(size / 2.0f - d + 0.5f, 0.0f, 1.0f) * 255);
      static_cast<uint32_t*>(s->pixels)[y * s->pitch / 4 + x] = (a << 24) | 0xffffff;
    }
    dot = SDL_CreateTextureFromSurface(renderer, s); SDL_FreeSurface(s);
    if (!dot) throw Error("DISPLAY_ERROR", SDL_GetError());
    SDL_SetTextureBlendMode(dot, SDL_BLENDMODE_BLEND);
    unlit = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, config.width, config.height);
    if (!unlit || SDL_SetRenderTarget(renderer, unlit) < 0) throw Error("DISPLAY_ERROR", SDL_GetError());
    color(10, 19, 29); SDL_RenderClear(renderer);
    for (int j = 0; j < g.rows; ++j) for (int i = 0; i < g.cols; ++i)
      stamp((i + 0.5f) * g.cell, g.top + (j + 0.5f) * g.cell, g.cell * 0.4f, {60, 92, 112}, 46);
    glow = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, (g.cols + 1) / 2, (g.rows + 1) / 2);
    if (!glow) throw Error("DISPLAY_ERROR", SDL_GetError());
    SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(glow, SDL_ScaleModeLinear);
  }
  // Sample each layer at dot centres; edge dots get smaller and dimmer instead of aliasing.
  void drawFace(const Face& face) {
    const Grid g = grid();
    const size_t n = size_t(g.cols) * size_t(g.rows);
    level.assign(n, 0); tint.resize(n);
    const float sx = kDesignW / float(config.width), sy = kDesignH / float(config.height), soft = g.cell * sx * 1.35f;
    for (const auto& layer : face.layers) {
      const int i0 = std::max(0, int(layer.x0 / sx / g.cell) - 2), i1 = std::min(g.cols - 1, int(layer.x1 / sx / g.cell) + 2);
      const int j0 = std::max(0, int((layer.y0 / sy - g.top) / g.cell) - 2), j1 = std::min(g.rows - 1, int((layer.y1 / sy - g.top) / g.cell) + 2);
      for (int j = j0; j <= j1; ++j) {
        const float y = (g.top + (j + 0.5f) * g.cell) * sy;
        for (int i = i0; i <= i1; ++i) {
          float c = 0.5f - layer.distance((i + 0.5f) * g.cell * sx, y) / soft;
          if (c <= 0) continue;
          c = std::min(c, 1.0f) * layer.alpha;
          const size_t k = size_t(j) * size_t(g.cols) + size_t(i);
          if (c > level[k]) { level[k] = c; tint[k] = layer.color; }
        }
      }
    }
    SDL_RenderCopy(renderer, unlit, nullptr, nullptr);
    if (config.glow) {
      int gw = 0, gh = 0, pitch = 0; void* pixels = nullptr;
      SDL_QueryTexture(glow, nullptr, nullptr, &gw, &gh);
      if (SDL_LockTexture(glow, nullptr, &pixels, &pitch) == 0) {
        for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
          float best = 0; Rgb c{0, 0, 0};
          for (int j = 2 * y; j < std::min(2 * y + 2, g.rows); ++j) for (int i = 2 * x; i < std::min(2 * x + 2, g.cols); ++i) {
            const size_t k = size_t(j) * size_t(g.cols) + size_t(i);
            if (level[k] > best) { best = level[k]; c = tint[k]; }
          }
          static_cast<uint32_t*>(pixels)[y * pitch / 4 + x] = (uint32_t(best * 255) << 24) | (c.r << 16) | (c.g << 8) | c.b;
        }
        SDL_UnlockTexture(glow);
        SDL_SetTextureAlphaMod(glow, 140);
        SDL_FRect dst{0, g.top, float(gw * 2) * g.cell, float(gh * 2) * g.cell};
        SDL_RenderCopyF(renderer, glow, nullptr, &dst);
      }
    }
    for (size_t k = 0; k < n; ++k) {
      if (level[k] < 0.06f) continue;
      const float f = float(std::min(7, int(level[k] * 8)) + 1) / 8;
      const int i = int(k % size_t(g.cols)), j = int(k / size_t(g.cols));
      stamp((i + 0.5f) * g.cell, g.top + (j + 0.5f) * g.cell, g.cell * 0.44f * (0.45f + 0.55f * f), tint[k], uint8_t((0.3f + 0.7f * f) * 255));
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
    if (!dot) prepareDots(grid());
    if (SDL_SetRenderTarget(renderer, canvas) < 0) throw Error("DISPLAY_ERROR", SDL_GetError());
    color(10, 19, 29); SDL_RenderClear(renderer);
    if (view.value("displayMode", "face") == "text") {
      color(71, 211, 186); SDL_Rect bar{80, 74, 56, 6}; SDL_RenderFillRect(renderer, &bar);
      int y = 130;
      for (const auto& line : lines) { text(line, 80, y); y += TTF_FontLineSkip(font); }
    } else {
      const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
      const Face face = buildFace(view.value("activity", "idle"), view.value("emotion", "neutral"), view.value("actionStatus", ""),
                                  float(seconds), config.whiskers, config.mouth);
      drawFace(face);
      if (!face.label.empty() && font) {
        int tw = 0; TTF_SizeUTF8(font, face.label.c_str(), &tw, nullptr);
        text(face.label, (config.width - tw) / 2, int(float(config.height) * 0.83f));
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
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
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
  if (d.sdl) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) return false;
      if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && d.touchSubscription) {
        emit(event(*d.touchSubscription, "touch.event", {{"phase", "cancel"}, {"contactId", "*"}, {"x", 0}, {"y", 0}, {"simulated", d.config.mock}, {"reason", "FOCUS_LOST"}}));
        d.contacts.clear(); d.moved.clear();
      }
      if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP) {
        // SDL may report points outside a letterboxed viewport. Ignore those points.
        if (e.type != SDL_FINGERUP && (e.tfinger.x < 0 || e.tfinger.x > 1 || e.tfinger.y < 0 || e.tfinger.y > 1)) continue;
        d.touch(e.type == SDL_FINGERDOWN ? "down" : e.type == SDL_FINGERUP ? "up" : "move",
          std::to_string(e.tfinger.touchId) + ":" + std::to_string(e.tfinger.fingerId), std::clamp(e.tfinger.x, 0.0f, 1.0f), std::clamp(e.tfinger.y, 0.0f, 1.0f), false, emit);
      }
      if (d.config.mock && (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION)) {
        const bool moving = e.type == SDL_MOUSEMOTION;
        if ((moving ? e.motion.which : e.button.which) == SDL_TOUCH_MOUSEID) continue;
        if (!moving && e.button.button != SDL_BUTTON_LEFT) continue;
        const bool sideways = d.config.rotation == 90 || d.config.rotation == 270;
        const double x = double(moving ? e.motion.x : e.button.x) / (sideways ? d.config.height : d.config.width);
        const double y = double(moving ? e.motion.y : e.button.y) / (sideways ? d.config.width : d.config.height);
        d.touch(moving ? "move" : e.type == SDL_MOUSEBUTTONDOWN ? "down" : "up", "mouse", std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0), true, emit);
      }
    }
  }
#endif
  if (!d.error.empty() || Clock::now() - d.last < std::chrono::milliseconds(33)) return true;
  d.last = Clock::now();
  if (d.touchSubscription) for (const auto& [id, fields] : d.moved) emit(event(*d.touchSubscription, "touch.event", fields));
  d.moved.clear();
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
  d.touchSubscription.reset(); d.contacts.clear(); d.moved.clear();
  d.view = {{"displayMode", "face"}, {"activity", "idle"}};
}
void Display::subscribeTouch(const Packet& command) {
  auto& d = *impl_; d.contacts.clear(); d.moved.clear(); d.touchEdges = 0; d.touchWindow = Clock::now();
  if (command.meta.value("enabled", true)) d.touchSubscription = command; else d.touchSubscription.reset();
}
void Display::injectTouch(const Packet& command, Emit emit) {
  if (!impl_->config.mock) throw Error("MOCK_ONLY", "Touch injection requires mock mode");
  impl_->touch(command.meta.at("phase").get<std::string>(), requiredString(command.meta, "contactId"),
    command.meta.at("x").get<double>(), command.meta.at("y").get<double>(), true, emit);
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
