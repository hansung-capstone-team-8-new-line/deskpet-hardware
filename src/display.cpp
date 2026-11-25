#include "deskpet/display.hpp"
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <set>
#ifdef DESKPET_SDL
#include <SDL.h>
#include <SDL_ttf.h>
#endif

namespace deskpet {
#ifdef DESKPET_SDL
namespace {
// A face is a set of numbers (a pose) that springs toward a target. Each activity has a base pose and
// a pool of small variations picked at random; emotions only shift lids, mouth and colour.
// Faces are signed distance fields in a 1024x600 design space, sampled per dot.
// The catalog page docs/face-catalog/index.html runs the same poses, variations and shapes.
constexpr float kPi = 3.14159265f, kDesignW = 1024, kDesignH = 600;
struct Rgb { uint8_t r, g, b; };
constexpr Rgb kBlue{100, 206, 250}, kPink{241, 142, 152}, kYellow{247, 199, 106};
struct Layer {
  Rgb color; float alpha, x0, y0, x1, y1;
  std::function<float(float, float)> distance;
  std::function<float(float, float)> shade;  // optional brightness multiplier
};
// Layers are in face space; tilt, scale and bob move the whole face around (512, 300).
struct Face {
  std::vector<Layer> layers; float tilt = 0, scale = 1, bob = 0;
  Rgb aura{0, 0, 0}; float auraAmount = 0;  // state colour washed over the unlit dots
};

float smooth(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }
float envelope(float u, float a = 0.2f, float b = 0.8f) { return u < a ? smooth(u / a) : u > b ? smooth((1 - u) / (1 - b)) : 1; }
float bump(float u) { return std::sin(kPi * std::clamp(u, 0.0f, 1.0f)); }
float sdBox(float x, float y, float cx, float cy, float hw, float hh, float r) {
  const float qx = std::abs(x - cx) - hw + r, qy = std::abs(y - cy) - hh + r;
  return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
}
float sdCircle(float x, float y, float cx, float cy, float r) { return std::hypot(x - cx, y - cy) - r; }
float sdTaper(float x, float y, float ax, float ay, float bx, float by, float t0, float t1) {
  const float px = x - ax, py = y - ay, vx = bx - ax, vy = by - ay, len = vx * vx + vy * vy;
  const float h = len > 0 ? std::clamp((px * vx + py * vy) / len, 0.0f, 1.0f) : 0;
  return std::hypot(px - vx * h, py - vy * h) - (t0 + (t1 - t0) * h);
}
float sdArc(float x, float y, float cx, float cy, float r, float a0, float a1, float th) {
  const float dx = x - cx, dy = y - cy, angle = std::atan2(dy, dx);
  if (angle >= a0 && angle <= a1) return std::abs(std::hypot(dx, dy) - r) - th;
  return std::min(std::hypot(x - cx - r * std::cos(a0), y - cy - r * std::sin(a0)),
                  std::hypot(x - cx - r * std::cos(a1), y - cy - r * std::sin(a1))) - th;
}
constexpr float kDown0 = 0.35f, kDown1 = kPi - 0.35f;
// Shapes for the screen effects, centred at (cx, cy) with size s in px.
float sdHeart(float x, float y, float cx, float cy, float s) {  // point at the bottom
  const float px = std::abs(x - cx) / s, py = (cy + 0.5f * s - y) / s;
  if (py + px > 1) return (std::hypot(px - 0.25f, py - 0.75f) - std::sqrt(2.0f) / 4) * s;
  const float m = 0.5f * std::max(px + py, 0.0f);
  return std::min(std::hypot(px, py - 1), std::hypot(px - m, py - m)) * (px - py < 0 ? -1.0f : 1.0f) * s;
}
float sdStar4(float x, float y, float cx, float cy, float s) {  // four-point sparkle: two long thin diamonds
  const float px = std::abs(x - cx), py = std::abs(y - cy), w = s, n = s * 0.22f, k = std::hypot(w, n);
  return std::min((px * n + py * w - w * n) / k, (px * w + py * n - w * n) / k);
}
float sdTaper(float x, float y, float ax, float ay, float bx, float by, float t0, float t1);
float sdDrop(float x, float y, float cx, float cy, float s) { return sdTaper(x, y, cx, cy - 1.3f * s, cx, cy, 0.12f * s, 0.55f * s); }
float sdQuestion(float x, float y, float cx, float cy, float s) {
  const float r = 0.32f * s, ay = cy - 0.3f * s, th = 0.085f * s, ex = cx + r * std::cos(0.9f), ey = ay + r * std::sin(0.9f);
  return std::min({sdArc(x, y, cx, ay, r, -kPi + 0.45f, 0.9f, th), sdTaper(x, y, ex, ey, cx, cy + 0.22f * s, th, th),
                   sdCircle(x, y, cx, cy + 0.5f * s, 0.1f * s)});
}
float sdPuff(float x, float y, float cx, float cy, float s, float dir) {  // a "hmph" puff: three small cloud lobes
  return std::min({sdCircle(x, y, cx, cy, 0.45f * s), sdCircle(x, y, cx + dir * 0.42f * s, cy - 0.2f * s, 0.35f * s),
                   sdCircle(x, y, cx + dir * 0.78f * s, cy + 0.05f * s, 0.27f * s)});
}
float sdNote(float x, float y, float cx, float cy, float s) {  // a quaver
  const float hx = cx - 0.18f * s, hy = cy + 0.38f * s;
  return std::min({std::hypot((x - hx) / 1.25f, y - hy) - 0.17f * s, sdTaper(x, y, cx, cy + 0.36f * s, cx, cy - 0.5f * s, 0.05f * s, 0.05f * s),
                   sdTaper(x, y, cx, cy - 0.5f * s, cx + 0.3f * s, cy - 0.22f * s, 0.07f * s, 0.04f * s)});
}

enum Key { LookX, LookY, Scale, Tilt, Bob, EyeSize, LidTop, LidTopR, LidSad, LidHappy,
           MouthSmile, MouthOpen, MouthX, MouthWave, Whisk, Zz, EyeAsym,
           EarL, EarR,  // ear servos: -1 down, 0 neutral, +1 up
           Cross, Wink,  // eyes drawn toward the nose (px); one eye closed (+ right, - left)
           kKeys };
using Pose = std::array<float, kKeys>;
// The resting upper lid (0.13) is a slightly drowsy, relaxed eye; happy, sad and surprised open it fully.
constexpr Pose kRest{0, 0, 1, 0, 0, 1, 0.13f, 0, 0, 0, 0.3f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
float stiffness(int k) {
  switch (k) {
    case MouthOpen: return 700;
    case LookX: case LookY: return 220;
    case Tilt: return 70;
    case Bob: return 110;
    case LidSad: return 60;
    case LidHappy: return 90;
    case Zz: return 30;
    case EarL: case EarR: return 55;
    default: return 150;
  }
}
// Ears are underdamped so they settle a beat after a nod or tilt, overshooting a little (follow-through).
float damping(int k) { return k == EarL || k == EarR ? 0.5f : 0.78f; }
void addEars(Pose& d, float left, float right) { d[EarL] += left; d[EarR] += right; }
void addEars(Pose& d, float both) { addEars(d, both, both); }

// Random values fixed when a variation starts: s = sign, k = -1..1, side = the activity's chosen side.
struct Pick { float s, k, side; };
using Apply = std::function<void(float u, const Pick& p, Pose& d)>;
struct Variation { const char* name; float min, max, weight; int blinks; bool track; Apply apply; float slowBlink = 0; };
Variation var(const char* name, float min, float max, Apply apply, float weight = 1, int blinks = 0, bool track = false) {
  return {name, min, max, weight, blinks, track, std::move(apply)};
}
Apply nod(int n, float depth) {
  return [=](float u, const Pick&, Pose& d) { d[Bob] += depth * std::max(0.0f, std::sin(u * kPi * 2 * float(n))) * envelope(u, 0.05f, 0.9f); };
}
struct Activity {
  float gap0, gap1; bool talk;
  std::function<void(float side, Pose&)> base;
  std::vector<Variation> vars;
  std::string label;
};
// Emotions change the shape only a little; they mostly change how the face moves: tempo scales spring
// stiffness, blink is the blink length, micro the micro-saccade range, gap scales the rest between variations.
struct Mood {
  std::vector<std::pair<Key, float>> set, add; std::vector<Variation> vars;
  float tempo = 1, blink = 0.17f, micro = 5, gap = 1;
};
// The slow blink cats give when relaxed and trusting: the eyes close and reopen over 1.1 s. A squint made
// with the upper lid looked like a flat, unimpressed -_- stare, so it uses the same rounded fold as a blink.
Variation slowBlink(float weight) {
  Variation v = var("느린 눈인사", 1.6f, 2.0f, [](float u, const Pick&, Pose& d) { d[LidHappy] += 0.1f * envelope(u); }, weight);
  v.slowBlink = 1.1f;
  return v;
}

const Activity& activityFor(const std::string& name) {
  using P = const Pick&;
  static const std::map<std::string, Activity> table{
    {"idle", {1.2f, 3.6f, false, [](float, Pose&) {}, {
      var("두리번", 1.6f, 2.8f, [](float u, P p, Pose& d) { d[LookX] += p.s * 48 * envelope(u); d[LookY] += p.k * 10 * envelope(u); }),
      var("양옆 살피기", 2.6f, 3.4f, [](float u, P, Pose& d) { d[LookX] += 44 * std::sin(u * kPi * 2) * envelope(u, 0.1f, 0.9f); }),
      var("느긋한 눈감기", 1.4f, 2.2f, [](float u, P, Pose& d) { d[LidTop] += 0.5f * envelope(u, 0.35f, 0.65f); d[MouthSmile] += 0.2f * envelope(u); addEars(d, -0.2f * envelope(u)); }),
      var("갸웃", 1.8f, 2.6f, [](float u, P p, Pose& d) { d[Tilt] += p.s * 0.08f * envelope(u); d[LookX] += p.s * 10 * envelope(u); }),
      var("하품", 2.0f, 2.6f, [](float u, P, Pose& d) {
        d[MouthOpen] += 0.75f * envelope(u, 0.3f, 0.6f);
        d[LidHappy] += 0.8f * envelope(u, 0.3f, 0.6f); d[Scale] += 0.03f * envelope(u); addEars(d, -0.5f * envelope(u, 0.3f, 0.6f)); }, 0.5f),
      var("수염 씰룩", 0.7f, 1.0f, [](float u, P, Pose& d) { d[Whisk] += 7 * std::sin(u * kPi * 8) * envelope(u, 0.1f, 0.9f); }),
      var("살짝 다가옴", 1.6f, 2.4f, [](float u, P, Pose& d) { d[Scale] += 0.045f * envelope(u); d[LookY] += 6 * envelope(u); }),
      var("궁금", 1.6f, 2.2f, [](float u, P p, Pose& d) {
        d[EyeAsym] += p.s * 0.05f * envelope(u); d[Tilt] += p.s * 0.05f * envelope(u); d[LookY] -= 6 * envelope(u);
        addEars(d, p.s < 0 ? 0.5f * envelope(u) : 0, p.s > 0 ? 0.5f * envelope(u) : 0); }, 0.7f),
    }, ""}},
    {"listening", {0.8f, 2.4f, false, [](float, Pose& g) {
      g[Scale] = 1.05f; g[EyeSize] = 1.08f; g[LookY] = -4; g[MouthSmile] = 0.15f; g[Whisk] = -8; g[EarL] = g[EarR] = 0.55f; }, {
      var("끄덕", 0.6f, 0.8f, nod(1, 12)),
      var("응응", 1.0f, 1.3f, nod(2, 9)),
      var("갸웃하며 듣기", 1.8f, 2.6f, [](float u, P p, Pose& d) { d[Tilt] += p.s * 0.06f * envelope(u); }),
      var("집중", 1.4f, 2.0f, [](float u, P, Pose& d) { d[EyeSize] += 0.06f * envelope(u); d[Scale] += 0.03f * envelope(u); addEars(d, 0.15f * envelope(u)); }),
      var("귀 기울이기", 1.4f, 2.0f, [](float u, P p, Pose& d) {
        d[LookX] += p.s * 16 * envelope(u); d[Tilt] += p.s * 0.04f * envelope(u); d[Whisk] -= 6 * envelope(u);
        d[EyeAsym] += p.s * 0.03f * envelope(u); addEars(d, (p.s < 0 ? 0.3f : -0.1f) * envelope(u), (p.s > 0 ? 0.3f : -0.1f) * envelope(u)); }),
    }, "듣고 있어요"}},
    // Thinking looks up and away, as people do; the side is picked at random on entry.
    {"processing", {0.5f, 2.0f, false, [](float s, Pose& g) {
      g[LookX] = 72 * s; g[LookY] = -44; g[EyeSize] = 0.93f; g[MouthSmile] = -0.15f;
      g[MouthX] = 6 * s; g[Tilt] = 0.03f * s; g[EarL] = s < 0 ? 0.35f : -0.05f; g[EarR] = s > 0 ? 0.35f : -0.05f; }, {
      var("휙휙", 0.5f, 0.8f, [](float u, P p, Pose& d) { d[LookX] -= p.side * 40 * envelope(u, 0.08f, 0.6f); }),
      var("반대편 보기", 1.8f, 2.6f, [](float u, P p, Pose& d) {
        const float e = envelope(u, 0.15f, 0.8f); d[LookX] -= p.side * 144 * e; d[MouthX] -= p.side * 12 * e; d[Tilt] -= p.side * 0.06f * e; }),
      var("음…", 1.3f, 1.8f, [](float u, P, Pose& d) { d[MouthX] += 8 * std::sin(u * kPi * 2); d[MouthWave] += 0.7f * envelope(u); }),
      var("골똘", 1.6f, 2.4f, [](float u, P, Pose& d) { d[LidTop] += 0.24f * envelope(u); d[EyeSize] -= 0.04f * envelope(u); d[Scale] -= 0.02f * envelope(u); addEars(d, -0.15f * envelope(u)); }),
      var("갸우뚱 생각", 1.8f, 2.4f, [](float u, P p, Pose& d) { d[Tilt] += p.side * 0.06f * envelope(u); }),
      var("아래 훑기", 1.2f, 1.6f, [](float u, P, Pose& d) { d[LookY] += 80 * envelope(u); }, 0.6f),
    }, "생각하고 있어요"}},
    {"speaking", {0.8f, 2.4f, true, [](float, Pose& g) { g[MouthSmile] = 0.25f; g[EarL] = g[EarR] = 0.15f; }, {
      var("강조", 0.45f, 0.6f, [](float u, P, Pose& d) { d[Scale] += 0.04f * bump(u); d[EyeSize] += 0.05f * bump(u); addEars(d, 0.25f * bump(u)); }),
      var("말하며 시선 돌림", 0.9f, 1.4f, [](float u, P p, Pose& d) { d[LookX] += p.s * 30 * envelope(u); d[LookY] -= 8 * envelope(u); }),
      var("눈 크게", 0.8f, 1.1f, [](float u, P, Pose& d) { d[EyeSize] += 0.08f * envelope(u); }),
      var("끄덕이며 말하기", 0.6f, 0.8f, nod(1, 9)),
      var("갸웃하며 말하기", 1.2f, 1.8f, [](float u, P p, Pose& d) { d[Tilt] += p.s * 0.05f * envelope(u); }),
    }, ""}},
    {"awaiting_answer", {1.0f, 3.0f, false, [](float s, Pose& g) {
      g[Tilt] = 0.13f * s; g[EyeSize] = 1.04f; g[MouthSmile] = 0.55f; g[LookY] = -4; g[EarL] = g[EarR] = 0.35f; }, {
      var("반대로 갸웃", 2.2f, 3.0f, [](float u, P p, Pose& d) { d[Tilt] -= 2 * p.side * 0.13f * envelope(u, 0.25f, 0.75f); }),
      var("빤히", 1.4f, 2.0f, [](float u, P, Pose& d) { d[Scale] += 0.045f * envelope(u); d[EyeSize] += 0.04f * envelope(u); }),
      var("깜박깜박", 0.6f, 0.8f, [](float, P, Pose&) {}, 1, 2),
      var("통", 0.45f, 0.6f, [](float u, P, Pose& d) { d[Bob] -= 10 * bump(u); }),
      var("기대", 1.4f, 2.0f, [](float u, P, Pose& d) { d[MouthSmile] += 0.3f * envelope(u); d[LidHappy] += 0.3f * envelope(u); addEars(d, 0.2f * envelope(u)); }),
    }, "대답을 기다려요"}},
  };
  const auto it = table.find(name);
  return it == table.end() ? table.at("idle") : it->second;
}

const Mood* moodFor(const std::string& name) {
  using P = const Pick&;
  static const std::map<std::string, Mood> table{
    // Happy lifts the resting lid and opens big, full ovals: narrowing the eyes from below reads as looking
    // down on someone. The joy is in the wide-open eyes, the smile and the brisk motion, with frequent ^ ^ beams.
    {"happy", {{{LidTop, 0}, {LidHappy, 0.06f}, {MouthSmile, 0.75f}, {EyeSize, 1.08f}, {EarL, 0.5f}, {EarR, 0.5f}}, {{LookY, -4}}, {
      var("통통", 0.8f, 1.0f, [](float u, P, Pose& d) { d[Bob] -= 6 * std::abs(std::sin(u * kPi * 2)); }),
      var("좌우 흔들", 1.2f, 1.6f, [](float u, P, Pose& d) { d[Tilt] += 0.04f * std::sin(u * kPi * 4) * envelope(u, 0.1f, 0.9f); }),
      var("방긋", 0.9f, 1.2f, [](float u, P, Pose& d) {
        d[MouthOpen] += 0.18f * envelope(u); d[LidHappy] += 0.6f * envelope(u, 0.25f, 0.7f); d[Scale] += 0.02f * envelope(u); d[Bob] -= 4 * bump(u); addEars(d, 0.3f * bump(u)); }, 1.6f),
      slowBlink(0.5f),
    }, 1.15f}},
    // A task that succeeded: calm pride rather than bouncing joy (a raised chin looked arrogant, so it nods).
    {"proud", {{{LidHappy, 0.05f}, {MouthSmile, 0.6f}, {EyeSize, 1.03f}, {EarL, 0.45f}, {EarR, 0.45f}}, {{Bob, -4}, {Scale, 0.02f}}, {
      var("끄덕", 0.6f, 0.8f, nod(1, 8)),
      var("으쓱", 0.7f, 0.9f, [](float u, P, Pose& d) { d[Scale] += 0.03f * bump(u); d[Bob] -= 8 * bump(u); addEars(d, 0.3f * bump(u)); }),
      slowBlink(0.7f),
    }}},
    {"sad", {{{LidTop, 0}, {LidSad, 1}, {MouthSmile, -0.9f}, {EarL, -0.55f}, {EarR, -0.55f}}, {{Bob, 16}, {LookY, 24}, {EyeSize, -0.09f}, {Whisk, 16}}, {
      var("한숨", 2.0f, 2.6f, [](float u, P, Pose& d) {
        const float e = envelope(u, 0.3f, 0.6f); d[LidSad] += 0.12f * e; d[EyeSize] -= 0.03f * e; d[Bob] += 8 * e; addEars(d, -0.15f * e); }),
      var("힐끔", 1.4f, 1.8f, [](float u, P p, Pose& d) { d[LookY] -= 18 * envelope(u); d[LookX] += p.s * 16 * envelope(u); addEars(d, 0.3f * envelope(u)); }),
      var("울먹", 1.4f, 1.8f, [](float u, P, Pose& d) { d[MouthWave] += 0.4f * envelope(u); }),
      var("시선 떨굼", 1.8f, 2.4f, [](float u, P p, Pose& d) { d[LookY] += 14 * envelope(u); d[LookX] += p.s * 20 * envelope(u); addEars(d, -0.1f * envelope(u)); }),
    }, 0.6f, 0.26f, 2.5f, 1.5f}},
    // A task that failed: a lighter, sheepish sorry rather than sadness.
    {"sorry", {{{LidTop, 0}, {LidSad, 0.4f}, {MouthSmile, -0.25f}, {EarL, -0.5f}, {EarR, -0.3f}}, {{LookY, 12}, {EyeSize, -0.03f}, {Whisk, 6}, {Tilt, 0.04f}}, {
      var("힐끔", 1.2f, 1.6f, [](float u, P p, Pose& d) { d[LookY] -= 16 * envelope(u); d[LookX] += p.s * 14 * envelope(u); }),
      var("머쓱 갸웃", 1.6f, 2.2f, [](float u, P p, Pose& d) { d[Tilt] += p.s * 0.06f * envelope(u); d[LookX] += p.s * 18 * envelope(u); }),
      var("작은 한숨", 1.6f, 2.2f, [](float u, P, Pose& d) { d[LidTop] += 0.15f * envelope(u, 0.3f, 0.6f); d[Bob] += 4 * envelope(u, 0.3f, 0.6f); }),
    }, 0.8f, 0.22f, 3}},
    // Unsure: no clipped lid, just slightly mismatched eyes and a head tilt.
    {"unknown", {{{MouthWave, 0.6f}, {MouthSmile, 0}, {EyeAsym, 0.06f}, {EarL, 0.45f}, {EarR, -0.25f}}, {{LookX, 30}, {LookY, -12}, {Tilt, 0.05f}}, {
      var("어리둥절", 1.8f, 2.4f, [](float u, P, Pose& d) { d[LookX] -= 64 * envelope(u); }),
      var("갸웃", 1.6f, 2.2f, [](float u, P p, Pose& d) { d[Tilt] += p.s * 0.08f * envelope(u); d[EyeAsym] -= 0.06f * envelope(u); addEars(d, -0.5f * envelope(u), 0.5f * envelope(u)); }),
    }}},
    {"petted", {{{LidTop, 0}, {LidHappy, 0.28f}, {MouthSmile, 0.5f}, {EarL, -0.35f}, {EarR, -0.35f}}, {{Whisk, 4}}, {
      var("비비기", 1.2f, 1.6f, [](float u, P, Pose& d) {
        const float w = std::sin(u * kPi * 4) * envelope(u, 0.1f, 0.9f); d[Tilt] += 0.045f * w; addEars(d, 0.25f * w, -0.25f * w); }),
      var("수염 씰룩", 0.6f, 0.9f, [](float u, P, Pose& d) { d[Whisk] += 4 * std::sin(u * kPi * 8); }),
      var("좋아", 1.0f, 1.4f, [](float u, P, Pose& d) {
        d[Scale] += 0.02f * envelope(u); d[Bob] -= 4 * envelope(u); d[LidHappy] += 0.2f * envelope(u); addEars(d, -0.15f * envelope(u)); }),
      slowBlink(1.5f),
    }, 0.8f, 0.24f}},
    {"sleepy", {{{LidTop, 0.68f}, {MouthSmile, 0.1f}, {MouthOpen, 0.05f}, {EarL, -0.5f}, {EarR, -0.5f}}, {{Bob, 10}, {Whisk, 8}}, {
      var("꾸벅", 2.6f, 3.4f, [](float u, P, Pose& d) { d[LidTop] += 0.32f * envelope(u, 0.55f, 0.85f); d[Bob] += 22 * envelope(u, 0.6f, 0.85f); addEars(d, -0.25f * envelope(u, 0.55f, 0.85f)); }),
      var("하품", 2.0f, 2.6f, [](float u, P, Pose& d) {
        d[MouthOpen] += 0.7f * envelope(u, 0.3f, 0.6f); d[LidTop] += 0.2f * envelope(u, 0.3f, 0.6f); d[Scale] += 0.03f * envelope(u); }),
      var("Z", 2.4f, 3.0f, [](float u, P, Pose& d) { d[Zz] += envelope(u, 0.15f, 0.7f); }, 1, 0, true),
    }, 0.7f, 0.32f, 1.5f}},
    // Surprise pops once on entry (the startle reaction), then rests with slightly bigger eyes.
    {"surprised", {{{LidTop, 0}, {EyeSize, 1.08f}, {MouthOpen, 0.4f}, {EarL, 0.7f}, {EarR, 0.7f}}, {{Scale, 0.04f}, {Whisk, -10}, {LookY, -6}}, {
      var("깜짝 깜박", 0.6f, 0.8f, [](float, P, Pose&) {}, 1, 2),
      var("부르르", 0.5f, 0.6f, [](float u, P, Pose& d) { d[LookX] += 3 * std::sin(u * kPi * 12) * envelope(u, 0.1f, 0.9f); }),
    }, 1.3f}},
  };
  const auto it = table.find(name);
  return it == table.end() ? nullptr : &it->second;
}

// What the face can sense besides the view the pipeline sends.
struct Senses {
  std::optional<float> speech;  // mouth opening of the speech playing now
  std::optional<float> voice;   // microphone level now, 0..1
  bool boredom = false;         // long idle makes the face bored, then doze
};
constexpr float kBoredAfter = 300, kDozeAfter = 600;  // seconds alone in idle before getting bored, then dozing
// How often (seconds) and how strongly the ears twitch by themselves: the mood's values, else the activity's.
// Listening has none: the ears stay up and still while the pet concentrates, which also keeps servo noise off the mic.
std::optional<std::pair<float, float>> earGap(const std::string& activity, const std::string& mood) {
  static const std::map<std::string, std::pair<float, float>> moods{
    {"happy", {2, 5}}, {"proud", {2.5f, 6}}, {"sad", {8, 16}}, {"sorry", {5, 10}}, {"unknown", {1.5f, 3.5f}},
    {"petted", {3, 6}}, {"sleepy", {10, 20}}, {"surprised", {1, 2.5f}}};
  static const std::map<std::string, std::pair<float, float>> activities{
    {"idle", {3, 8}}, {"processing", {1.5f, 4}}, {"speaking", {2, 5}}, {"awaiting_answer", {2.5f, 6}}};
  if (activity == "listening") return std::nullopt;
  if (const auto m = moods.find(mood); m != moods.end()) return m->second;
  if (const auto a = activities.find(activity); a != activities.end()) return a->second;
  return std::nullopt;
}
float earAmplitude(const std::string& mood) {
  static const std::map<std::string, float> table{{"sad", 0.5f}, {"sleepy", 0.5f}, {"sorry", 0.7f}, {"petted", 0.8f}, {"unknown", 1.2f}, {"surprised", 1.2f}};
  const auto it = table.find(mood);
  return it == table.end() ? 1.0f : it->second;
}

// Picks variations, blinks, micro-saccades and touch reactions, and springs the pose toward its target.
class Director {
public:
  Director() { set("idle", "neutral", ""); }
  void set(const std::string& activity, const std::string& emotion, const std::string& status) {
    saved_ = {activity, emotion, status}; petting_ = false;
    apply(activity, emotion, status);
  }
  // Touch in design coordinates (1024x600). Holding looks at the finger and a long hold leans in with closed eyes;
  // taps blink an eye, open the mouth or twitch a whisker; a double tap beams, rapid taps startle, a swipe makes
  // the eyes chase it, and rubbing purrs (petted). Each touch sends a faint dot wave from the finger.
  void touch(const std::string& phase, float x, float y) {
    wake();
    if (phase == "down") {
      pressed_ = true; downAt_ = t_; down_[0] = gaze_[0] = x; down_[1] = gaze_[1] = y; moved_ = travel_ = 0; travelStart_ = t_;
      chinPress_ = std::abs(x - 512) < 90 && y > 445 && y < 530; chinStart_ = -9; lastTouchX_ = x;
      addWave(x, y, 0.6f, 170, 0.18f);
      return;
    }
    if (!pressed_) return;
    if (phase == "move") {
      const float d = std::hypot(x - gaze_[0], y - gaze_[1]);
      gaze_[0] = x; gaze_[1] = y; moved_ += d; lastTouchX_ = x;
      if (chinPress_ && moved_ > 20 && chinStart_ < 0) chinStart_ = t_;  // a chin scratch begins
      if (t_ - travelStart_ > 1.5f) { travel_ = 0; travelStart_ = t_; }
      travel_ += d;
      if (travel_ > 700 && !chinPress_) {  // rubbing back and forth reads as petting (a chin scratch stays a chin scratch)
        pettedUntil_ = t_ + 3;
        if (!petting_) { petting_ = true; apply("idle", "petted", ""); addWave(512, 300, 1.2f, 620, 0.25f, kPetAura); purrAt_ = 0; }
      }
      return;
    }
    pressed_ = false;
    const float held = t_ - downAt_, dx = x - down_[0], dy = y - down_[1];
    if (held < 0.35f && std::hypot(dx, dy) > 160) {  // a swipe: the eyes chase past the end of it
      gaze_[0] = std::clamp(x + dx * 0.5f, 0.0f, 1024.0f); gaze_[1] = std::clamp(y + dy * 0.5f, 0.0f, 600.0f);
      gazeUntil_ = t_ + 0.9f; swipeAt_ = t_; swipeSide_ = dx < 0 ? -1.0f : 1.0f;
      kick(dx < 0 ? 0.5f : 0, dx < 0 ? 0 : 0.5f, 0.5f, Shape::Snap);  // the ear on that side pricks up
      addWave(x, y, 0.8f, 300, 0.2f);
      return;
    }
    if (held >= 0.3f || moved_ >= 14) return;
    // A tap. Poking again within 1.5 s annoys more; never during a conversation, less when sad.
    const bool calm = activityName_ != "idle";
    if (!calm) annoy_ = std::clamp(annoy_ + (t_ - lastPokeAt_ < 1.5f ? 0.09f : 0.02f) * (low() ? 0.3f : 1.0f), 0.0f, 1.0f);
    lastPokeAt_ = t_;
    if (sulking_) { act(Act::Glance, 0.7f, down_[0] < 512 ? -1.0f : 1.0f); annoy_ = std::min(1.0f, annoy_ + 0.03f); return; }  // a glance, then away again
    taps_.erase(std::remove_if(taps_.begin(), taps_.end(), [&](float at) { return t_ - at > 1.2f; }), taps_.end());
    taps_.push_back(t_);
    if (taps_.size() >= 4) {  // startle wins over a beam
      startleAt_ = t_; beamAt_ = nomAt_ = -9; blink(0); taps_.clear(); lastTapAt_ = -9; kick(0.8f, 0.8f, 0.6f, Shape::Snap);
      spawn(Fx::Shock, 512, 80, 0, 0, 0.55f, 1); cue("pop"); if (!calm) annoy_ = std::min(1.0f, annoy_ + 0.15f); return;
    }
    if (t_ - lastTapAt_ < 0.35f && std::hypot(down_[0] - lastTap_[0], down_[1] - lastTap_[1]) < 80) {
      beamAt_ = t_; lastTapAt_ = -9; addWave(512, 300, 1.0f, 520, 0.25f);
      if (!low()) {
        kick(0.35f, -0.35f, 0.9f, Shape::Wiggle, 0, 2);  // a beam swishes the ears in opposite phase
        for (const float sx : {-1.0f, 0.0f, 1.0f})
          spawn(Fx::Heart, 512 + sx * range(140, 220), 330, sx * 30, -70, range(1.2f, 1.6f), range(44, 56), sx == 0 ? 0 : 0.12f);
        spawn(Fx::Spark, 348 + range(-60, 60), 140, 0, -6, 0.8f, 30); spawn(Fx::Spark, 676 + range(-60, 60), 140, 0, -6, 0.8f, 30, 0.1f);
        if (affection_ > 0.5f) for (const float sx : {-1.0f, 1.0f}) spawn(Fx::Heart, 512 + sx * range(250, 330), 280, sx * 20, -60, 1.4f, 40, 0.25f);
        affection_ = std::min(1.0f, affection_ + 0.05f); annoy_ = std::max(0.0f, annoy_ - 0.1f);
        cue("chirp");
      } else { spawn(Fx::Heart, 512 + range(-80, 80), 340, 0, -45, 1.6f, 38); cue("boop", 0.8f); }  // comforted: one small heart
      return;
    }
    lastTapAt_ = t_; lastTap_[0] = down_[0]; lastTap_[1] = down_[1];
    const float tx = down_[0], ty = down_[1], fromCentre = std::abs(tx - 512), sx = tx < 512 ? -1.0f : 1.0f, aff = affection_;
    const bool eye = std::abs(ty - 240) < 105 && (std::abs(tx - 348) < 85 || std::abs(tx - 676) < 85);
    if (calm) {  // during a conversation keep it small: blink at the eyes, otherwise just look
      if (eye) { blink(0); blink(0.22f); } else gazeUntil_ = t_ + 1.2f;
      return;
    }
    if (ty < 120 && fromCentre > 282) {  // the top corners sit right under the ears: shake it off, sometimes the head too
      earTap(sx); cue("flutter");
      if (pick("ear", {{"tap", 1}, {"shake", 0.5f}}) == "shake") act(Act::Shake, 0.6f, sx);
    } else if (ty < 120) {  // the crown: eyes ease shut, ears flatten, leaning into the hand
      act(Act::Pat, 0.8f, sx); kick(-0.45f, -0.45f, 0.6f, Shape::Snap); cue("pat");
      affection_ = std::min(1.0f, aff + 0.1f); annoy_ = std::max(0.0f, annoy_ - 0.06f);
    } else if (tx < 60 || tx > 964 || ty > 560) {  // an edge: peek toward it
      act(Act::Peek, 1.1f, tx < 60 ? -1.0f : tx > 964 ? 1.0f : 0.0f); cue("boop", 1.05f);
    } else if (eye) {  // eyes: double blink, wink, squeeze or widen
      const auto v = pick("eye", {{"blink", 1}, {"wink", 0.7f + 0.8f * aff}, {"squeeze", 0.5f + 0.5f * aff}, {"widen", 0.5f}});
      if (v == "blink") { blink(0); blink(0.22f); }
      else if (v == "wink") act(Act::Wink, 0.34f, std::abs(tx - 348) < 85 ? -1.0f : 1.0f);
      else act(v == "squeeze" ? Act::Squeeze : Act::Widen, v == "squeeze" ? 0.45f : 0.5f, sx);
      cue("boop", 1.15f);
    } else if (fromCentre < 40 && ty > 350 && ty < 386) {  // the nose: eyes cross onto it, sometimes a sneeze
      act(Act::Nose, 1.0f, sx);
      const bool again = t_ - lastNoseAt_ < 3; lastNoseAt_ = t_;
      if (again) annoy_ = std::min(1.0f, annoy_ + 0.06f);
      if (random() < (again ? 0.5f : 0.2f)) sneeze(0.45f); else cue("boop", 1.3f);
    } else if (fromCentre < 70 && ty >= 386 && ty < 445) {  // the mouth: nom, nom-nom or a mumble
      const auto v = pick("mouth", {{"nom", 1}, {"nom2", 0.6f}, {"mumble", 0.6f}});
      if (v == "mumble") { act(Act::Mumble, 0.6f, sx); cue("boop", 0.7f); }
      else { nomAt_ = t_; if (v == "nom2") act(Act::Nom, 0.6f, sx, 0.3f); cue("nom", 0.85f); }
      const float f = 0.2f * lift(); kick(f, f, 0.3f);
    } else if (fromCentre < 90 && ty >= 445 && ty < 530) {  // the chin: lifts it with a little smile
      act(Act::Chin, 0.7f, sx); cue("boop", 0.8f); affection_ = std::min(1.0f, aff + 0.04f);
    } else if (fromCentre > 110 && fromCentre < 220 && ty > 300 && ty < 362) {  // a cheek: turns away from it
      act(Act::Cheek, 0.9f, sx); blink(0); cue("boop", 0.9f); annoy_ = std::min(1.0f, annoy_ + 0.04f);
    } else if (fromCentre > 210 && fromCentre < 430 && std::abs(ty - 400) < 60) {  // whiskers: twitch or sweep, now and then a tickled sneeze
      const float f = 0.4f * lift(sx);
      kick(sx < 0 ? f : 0, sx < 0 ? 0 : f, 0.28f);  // and the ear on that side flicks
      if (pick("whisker", {{"twitch", 1}, {"sweep", 0.6f}}) == "sweep") act(Act::Sweep, 0.7f, sx);
      else { whiskAt_ = t_; whiskSide_ = sx; }
      if (random() < 0.12f) sneeze(0.4f); else cue("boop");
    } else {  // anywhere else: look, look with a tilt, or come closer to see
      gazeUntil_ = t_ + 1.2f;
      const auto v = pick("gaze", {{"look", 1}, {"tiltLook", 0.6f}, {"approach", 0.4f + 0.6f * aff}});
      if (v != "look") act(v == "tiltLook" ? Act::TiltLook : Act::Approach, v == "tiltLook" ? 1.2f : 1.0f, sx);
      cue("boop", 0.95f);
    }
  }
  // The face's own dot waves, newest last.
  struct Wave { float x, y, start, duration, radius, alpha; std::optional<Rgb> color; };
  const std::vector<Wave>& waves() const { return waves_; }
private:
  void apply(const std::string& activity, const std::string& emotion, const std::string& status) {
    if (status == "succeeded" && status_ != "succeeded") {  // a one-off pulse on success
      burstAt_ = t_ + 0.15f; addWave(512, 300, 0.9f, 640, 0.35f, Rgb{255, 236, 170}, 0.15f);
      kick(0.45f, 0.45f, 0.4f, Shape::Flick, 0.15f); kick(0.2f, -0.2f, 0.7f, Shape::Wiggle, 0.5f, 2);  // ears prick, then swish
      for (int i = 0; i < 6; ++i) {  // sparkles around the face
        const float a = range(-kPi, 0), r = range(260, 360);
        spawn(Fx::Spark, 512 + std::cos(a) * r * 1.3f, 300 + std::sin(a) * r * 0.75f, 0, -8, range(0.7f, 1.1f), range(28, 42), range(0, 0.35f));
      }
      cue("sparkle", 1, activity);
    }
    if (activity == "listening" && activityName_ != "listening") addWave(512, 300, 1.2f, 600, 0.18f);  // "I'm listening"
    activityName_ = activity; status_ = status;
    wake();
    // proud and sorry are internal moods picked from actionStatus, not interface values.
    mood_ = status == "failed" ? "sorry" : emotion == "sad" ? "sad" : status == "unknown" ? "unknown"
          : status == "succeeded" ? "proud" : emotion;
    activity_ = &activityFor(activity); moodTable_ = moodFor(mood_);
    if (mood_ == "surprised") { startleAt_ = t_ + 0.05f; spawn(Fx::Shock, 512, 80, 0, 0, 0.55f, 1); }  // surprise pops once on entry
    sweatAt_ = t_ + 0.6f; questionAt_ = t_ + 0.3f; noteAt_ = t_ + range(1, 3);
    side_ = sign(); current_.reset(); last_ = nullptr;
    nextVariation_ = t_ + range(0.6f, 1.4f);
    blink(0);  // a state change starts with a blink, the way animators hide a cut
    nextBlink_ = t_ + range(2, 5);
    label_ = status == "unknown" ? "결과를 확인해야 해요" : activity_->label;
    target_ = low() ? kPink : mood_ == "unknown" ? kYellow : kBlue;
    earSkew_ = range(-0.12f, 0.12f);  // the same state never holds its ears quite the same way twice
    if (begun_) { const float f = -0.15f * lift(); kick(f, f, 0.2f); }  // a change of state makes the ears flinch: noticed
    begun_ = true; nextTwitch_ = t_ + range(0.8f, 2.0f);
  }
  // Subdued moods answer touch by softening instead of beaming ^ ^.
  bool low() const { return mood_ == "sad" || mood_ == "sorry"; }
  // Any interaction resets the idle clock; coming back from boredom startles the pet awake.
  void wake() {
    const float alone = t_ - interactedAt_;
    if (activityName_ == "idle" && alone > kBoredAfter) {
      wakeAt_ = t_; blink(0); addWave(512, 300, 1.0f, 520, 0.2f);
      kick(0.6f, 0.6f, 0.6f, Shape::Snap);  // waking from a doze, the ears shoot up
      cue("mrrp");
    }
    interactedAt_ = t_;
  }
public:
  void update(float dt, const Senses& senses) {
    t_ += dt;
    if (petting_ && !pressed_ && t_ > pettedUntil_) { petting_ = false; apply(saved_[0], saved_[1], saved_[2]); }
    if (current_ && t_ > current_->start + current_->duration) {
      current_.reset(); nextVariation_ = t_ + range(activity_->gap0, activity_->gap1) * (moodTable_ ? moodTable_->gap : 1);
    }
    if (!current_ && t_ >= nextVariation_) start();
    Pose g = kRest;
    activity_->base(side_, g);
    if (moodTable_) { for (const auto& [k, v] : moodTable_->set) g[k] = v; for (const auto& [k, v] : moodTable_->add) g[k] += v; }
    // Without audio playing the mouth stays closed rather than guessing a rhythm.
    if (activity_->talk) g[MouthOpen] = senses.speech ? *senses.speech * (low() ? 0.7f : 1) : 0;
    // Living eyes never hold perfectly still.
    if (t_ >= nextMicro_) {
      const float amp = moodTable_ ? moodTable_->micro : 5;
      micro_[0] = range(-amp, amp); micro_[1] = range(-amp * 0.6f, amp * 0.6f); nextMicro_ = t_ + range(0.35f, 1.6f);
    }
    g[LookX] += micro_[0]; g[LookY] += micro_[1];
    if (current_) current_->variation->apply((t_ - current_->start) / current_->duration, current_->pick, g);
    react(g, senses, dt);
    // The ears follow the head's tilt, gaze and nods; the underdamped spring makes them settle a beat later.
    // While listening they barely follow (×0.3): the pet holds its ears still to concentrate, and the servos stay quiet.
    const float earFollow = activityName_ == "listening" ? 0.3f : 1;
    const float lean = g[Tilt] * 2.6f * earFollow, look = std::clamp(g[LookX], -120.0f, 120.0f) * earFollow;
    const float common = std::clamp(-g[LookY], -80.0f, 80.0f) * 0.002f - g[Bob] * 0.01f * earFollow + (g[Scale] - 1) * 1.5f + (g[EyeSize] - 1) * 0.8f;
    g[EarL] += earSkew_ + lean + common + (look < 0 ? -look * 0.0025f : -look * 0.001f);
    g[EarR] += -earSkew_ - lean + common + (look > 0 ? look * 0.0025f : look * 0.001f);
    const float h = std::min(dt, 1.0f / 20) / 2, tempo = moodTable_ ? moodTable_->tempo : 1;  // sad moves slowly, happy briskly
    for (int k = 0; k < kKeys; ++k) {
      if (snap_) { pose_[k] = g[k]; velocity_[k] = 0; continue; }
      const float K = stiffness(k) * (k == MouthOpen ? 1 : tempo), C = 2 * std::sqrt(K) * damping(k);
      for (int i = 0; i < 2; ++i) { velocity_[k] += (K * (g[k] - pose_[k]) - C * velocity_[k]) * h; pose_[k] += velocity_[k] * h; }
    }
    const float follow = snap_ ? 1 : std::min(1.0f, dt * 6);
    color_[0] += (target_.r - color_[0]) * follow; color_[1] += (target_.g - color_[1]) * follow; color_[2] += (target_.b - color_[2]) * follow;
    snap_ = false;
    if (t_ >= nextBlink_) {
      blink(0); if (random() < 0.22f) blink(0.28f); nextBlink_ = t_ + range(2.2f, 6);
      if (random() < 0.12f) { const float f = -0.1f * lift(); kick(f, f, 0.16f); }  // now and then the ears flinch with a blink
    }
    earLife(senses);
    effects(dt);
    // The posture is kept softly inside ±0.85 so a quick gesture always has room to show.
    const auto [kickL, kickR] = kickOffset();
    const auto soft = [](float x) { return 0.85f * std::tanh(x / 0.85f); };
    ears_ = {std::clamp(soft(pose_[EarL]) + kickL, -1.0f, 1.0f), std::clamp(soft(pose_[EarR]) + kickR, -1.0f, 1.0f)};
    blinks_.erase(std::remove_if(blinks_.begin(), blinks_.end(), [&](const auto& b) { return t_ > b.first + b.second + 0.05f; }), blinks_.end());
    closure_ = 0;
    for (const auto& [s, d] : blinks_) if (t_ >= s && t_ <= s + d) closure_ = std::max(closure_, std::sin(kPi * (t_ - s) / d));
    zzPhase_ = current_ && current_->variation->track ? (t_ - current_->start) / current_->duration : 0;
  }
  const Pose& pose() const { return pose_; }
  Rgb aura() const { return {uint8_t(std::lround(aura_[0])), uint8_t(std::lround(aura_[1])), uint8_t(std::lround(aura_[2]))}; }
  float auraAmount() const { return auraAmount_; }
  float listening() const { return listening_; }  // strength of the sound ripples beside the face
  float blinkClosure() const { return closure_; }
  // Ear servo targets, -1 down .. +1 up (left, right).
  std::array<float, 2> ears() const { return ears_; }
  // Screen-effect particles; kind: heart, sparkle, sweat drop, question mark, note, shock lines.
  enum class Fx { Heart, Spark, Sweat, Question, Note, Shock, Puff, Spray };
  struct Particle { Fx kind; float x, y, vx, vy, start, life, size, phase; };
  const std::vector<Particle>& particles() const { return parts_; }
  float thinking() const { return thinking_; }  // strength of the "..." dots while processing
  float side() const { return side_; }
  std::vector<SoundCue> takeSounds() {
    std::vector<SoundCue> due;
    sounds_.erase(std::remove_if(sounds_.begin(), sounds_.end(), [&](const Pending& p) {
      if (p.at > t_) return false;
      due.push_back(p.cue);
      return true;
    }), sounds_.end());
    return due;
  }
  float affection() const { return affection_; }
  float annoyance() const { return annoy_; }
  float zzPhase() const { return zzPhase_; }
  float time() const { return t_; }
  Rgb color() const { return {uint8_t(std::lround(color_[0])), uint8_t(std::lround(color_[1])), uint8_t(std::lround(color_[2]))}; }
  const std::string& label() const { return label_; }
private:
  // Touch, wake-up, boredom and the success pulse on top of the expression's own target.
  void react(Pose& g, const Senses& senses, float dt) {
    if (pressed_ || t_ < gazeUntil_) {
      g[LookX] = std::clamp((gaze_[0] - 512) * 0.3f, -120.0f, 120.0f); g[LookY] = std::clamp((gaze_[1] - 250) * 0.3f, -80.0f, 80.0f);
      g[Tilt] += std::clamp((gaze_[0] - 512) / 512, -1.0f, 1.0f) * 0.04f;
      if (pressed_) g[Scale] += 0.03f;
    }
    if (pressed_ && moved_ < 20 && t_ - downAt_ > 0.9f) {  // a long hold: lean in and close the eyes contentedly
      const float e = smooth((t_ - downAt_ - 0.9f) / 0.6f);
      g[Scale] += 0.02f * e; g[Whisk] += 4 * e;
      if (low()) { g[LidTop] += 0.45f * e; g[LidSad] -= 0.3f * e; } else g[LidHappy] += 0.75f * e;  // when sad, quietly close the eyes
      addEars(g, -(low() ? 0.2f : 0.4f) * e);  // leaning in, the ears relax back
    }
    if (const float u = (t_ - nomAt_) / 0.6f; u >= 0 && u < 1) { g[MouthOpen] += 0.6f * bump(u); g[MouthSmile] += 0.3f * envelope(u); }
    if (const float u = (t_ - whiskAt_) / 0.8f; u >= 0 && u < 1) { g[Whisk] += 8 * std::sin(u * kPi * 8) * (1 - u); g[Tilt] += whiskSide_ * 0.03f * bump(u); }
    if (const float u = (t_ - beamAt_) / 1.2f; u >= 0 && u < 1) {
      if (low()) {  // comforted: the lids soften and a small smile appears
        const float e = envelope(u, 0.2f, 0.7f);
        g[LidSad] -= 0.4f * e; g[LidHappy] += 0.3f * e; g[MouthSmile] += 0.5f * e; g[Bob] -= 4 * bump(u); addEars(g, 0.3f * e);
      } else {
        g[LidHappy] += 0.9f * envelope(u, 0.15f, 0.7f); g[MouthSmile] += 0.4f * envelope(u); g[Bob] -= 12 * std::abs(std::sin(u * kPi * 2)) * (1 - u);
      }
    }
    if (const float u = (t_ - startleAt_) / 0.8f; u >= 0 && u < 1) {
      g[EyeSize] += 0.15f * bump(u); g[Scale] += 0.05f * bump(u); g[LookY] -= 10 * bump(u); g[Whisk] -= 8 * bump(u);
    }
    if (const float u = (t_ - swipeAt_) / 0.9f; u >= 0 && u < 1) g[Tilt] += swipeSide_ * 0.05f * bump(u);
    const float wake = (t_ - wakeAt_) / 0.6f;
    if (wake >= 0 && wake < 1) { g[Scale] += 0.05f * bump(wake); g[EyeSize] += 0.08f * bump(wake); }
    if (senses.boredom && activityName_ == "idle" && !moodTable_) {
      const float alone = t_ - interactedAt_, bored = smooth((alone - kBoredAfter) / 60), doze = smooth((alone - kDozeAfter) / 60);
      g[LidTop] += 0.15f * bored + 0.45f * doze; g[LookY] += 12 * bored; g[Bob] += doze * (4 + 6 * std::sin(t_ * 0.8f));
      addEars(g, -0.15f * bored - 0.45f * doze);
    }
    if (pressed_ && chinPress_ && chinStart_ >= 0) {  // a chin scratch: chin up, eyes closing, slowly melting
      const float e = smooth((t_ - chinStart_) / 0.5f), melt = smooth((t_ - chinStart_ - 1.5f) / 1.0f);
      g[LookY] -= 22 * e; g[Bob] -= 6 * e; g[LidHappy] += 0.45f * e + 0.25f * melt; g[MouthSmile] += 0.3f * e; addEars(g, -0.3f * e);
    }
    for (const auto& a : acts_) {
      const float u = (t_ - a.at) / a.duration;
      if (u < 0 || u >= 1) continue;
      const float e = envelope(u, 0.15f, 0.7f), k = bump(u), s = a.side;
      switch (a.name) {
        case Act::Pat: g[LidHappy] += 0.65f * e; g[Bob] += 8 * k; g[MouthSmile] += 0.3f * e; g[Tilt] += s * 0.03f * k; break;
        case Act::Nose: g[Cross] += 34 * e; g[LookY] -= 6 * e; g[EyeSize] += 0.04f * e; break;
        case Act::Sneeze: {
          const float jerk = u < 0.25f ? -12 * smooth(u / 0.25f) : 12 * bump((u - 0.25f) / 0.75f);
          g[LidHappy] += 0.75f * envelope(u, 0.1f, 0.5f); g[Bob] += jerk; g[MouthOpen] += 0.5f * envelope(u, 0.15f, 0.4f); g[Whisk] -= 8 * k; break;
        }
        case Act::Cheek: g[LookX] -= s * 60 * e; g[Tilt] -= s * 0.06f * e; g[MouthWave] += 0.4f * e; break;
        case Act::Peek:
          if (s != 0) g[LookX] = s * 130 * e + g[LookX] * (1 - e); else g[LookY] += 60 * e;
          g[Scale] -= 0.04f * e; addEars(g, 0.3f * e); break;
        case Act::Wink: g[Wink] += s * 0.95f * k; g[MouthSmile] += 0.25f * k; break;
        case Act::Squeeze: g[LidHappy] += 0.8f * k; break;
        case Act::Widen: g[EyeSize] += 0.12f * k; addEars(g, 0.3f * k); break;
        case Act::Mumble: g[MouthWave] += 0.7f * e; g[MouthOpen] += 0.12f * std::abs(std::sin(u * kPi * 6)) * e; break;
        case Act::Nom: g[MouthOpen] += 0.6f * k; g[MouthSmile] += 0.3f * e; break;
        case Act::Chin: g[LookY] -= 14 * e; g[MouthSmile] += 0.35f * e; g[LidHappy] += 0.3f * e; break;
        case Act::Sweep: g[Whisk] += 14 * std::sin(u * kPi * 2) * (1 - u); g[Tilt] += s * 0.04f * k; break;
        case Act::Shake: g[Tilt] += 0.07f * std::sin(u * kPi * 6) * (1 - u); break;
        case Act::TiltLook: g[Tilt] += s * 0.07f * e; break;
        case Act::Approach: g[Scale] += 0.06f * e; g[EyeSize] += 0.04f * e; break;
        case Act::Glance: g[LookX] = g[LookX] * (1 - k) + s * 70 * k; break;
      }
    }
    // Mood: warmed up it always smiles a little and leans closer; annoyed, its ears go back and the mouth droops.
    g[MouthSmile] += 0.15f * affection_; if (pressed_) g[Scale] += 0.03f * affection_;
    const float sour = std::clamp((annoy_ - 0.35f) / 0.25f, 0.0f, 1.0f);
    addEars(g, -0.35f * sour); g[MouthSmile] -= 0.3f * sour;
    if (sulking_) {  // turned away with the ears back; a glance (above) briefly looks round
      const float e = smooth((t_ - sulkStart_) / 0.3f);
      g[LookX] = g[LookX] * (1 - e) + sulkSide_ * 110 * e; g[Tilt] += sulkSide_ * 0.05f * e;
      addEars(g, -0.45f * e); g[MouthSmile] -= 0.35f * e; g[EyeSize] -= 0.03f * e;
      for (const auto& a : acts_) if (a.name == Act::Glance) {
        const float u = (t_ - a.at) / a.duration;
        if (u >= 0 && u < 1) g[LookX] = g[LookX] * (1 - bump(u)) + a.side * 70 * bump(u);
      }
    }
    const float burst = (t_ - burstAt_) / 0.7f;
    if (burst >= 0 && burst < 1) { g[Bob] -= 12 * bump(burst); g[Scale] += 0.04f * bump(burst); }
    const float follow = std::min(1.0f, dt * 4);
    // State colours wash the unlit dots while the features stay blue: petted pink, listening sky blue,
    // thinking violet, waiting for an answer amber; other states none.
    const Rgb* tint = mood_ == "petted" ? &kPetAura : activityName_ == "listening" ? &kListenAura
                    : activityName_ == "processing" ? &kThinkAura : activityName_ == "awaiting_answer" ? &kWaitAura : nullptr;
    if (tint) {
      const float snap = auraAmount_ < 0.02f ? 1 : follow;
      aura_[0] += (tint->r - aura_[0]) * snap; aura_[1] += (tint->g - aura_[1]) * snap; aura_[2] += (tint->b - aura_[2]) * snap;
    }
    auraAmount_ += ((tint ? 1.0f : 0.0f) - auraAmount_) * follow;
    // Listening ripples breathe gently on their own and swell with the user's voice.
    voice_ += (senses.voice.value_or(0) - voice_) * std::min(1.0f, dt * 18);
    const float listen = activityName_ == "listening" ? std::max(0.25f + 0.08f * std::sin(t_ * 2.4f), voice_) : 0;
    listening_ += (listen - listening_) * std::min(1.0f, dt * 10);
  }
  static constexpr Rgb kListenAura{70, 165, 255}, kThinkAura{150, 110, 255}, kWaitAura{255, 180, 80}, kPetAura{255, 140, 175};
  // Quick ear gestures on top of the sprung posture. left/right are amplitudes (negative lowers the ear).
  // Flick: a quick twitch; Snap: up fast, down slowly; Swivel: slowly down and back; Wiggle: back and forth.
  enum class Shape { Flick, Snap, Swivel, Wiggle };
  struct Kick { float at, duration, left, right; Shape shape; int cycles; };
  void kick(float left, float right, float duration, Shape shape = Shape::Flick, float delay = 0, int cycles = 1) {
    if (kicks_.size() >= 8) kicks_.erase(kicks_.begin());
    kicks_.push_back({t_ + delay, duration, left, right, shape, cycles});
  }
  std::pair<float, float> kickOffset() {
    kicks_.erase(std::remove_if(kicks_.begin(), kicks_.end(), [&](const Kick& k) { return t_ >= k.at + k.duration; }), kicks_.end());
    float l = 0, r = 0;
    for (const auto& k : kicks_) {
      const float u = (t_ - k.at) / k.duration;
      if (u < 0) continue;
      const float f = k.shape == Shape::Snap ? (u < 0.15f ? smooth(u / 0.15f) : 1 - smooth((u - 0.15f) / 0.85f))
                    : k.shape == Shape::Swivel ? bump(u)
                    : k.shape == Shape::Wiggle ? std::sin(u * kPi * 2 * float(k.cycles)) * envelope(u, 0.1f, 0.8f)
                    : u < 0.3f ? smooth(u / 0.3f) : smooth((1 - u) / 0.7f);
      l += k.left * f; r += k.right * f;
    }
    return {l, r};
  }
  // A flick shows as a lift on a lowered ear and as a dip on an ear that already stands high.
  float lift(float side) const { return pose_[side < 0 ? EarL : EarR] > 0.35f ? -1.0f : 1.0f; }
  float lift() const { return (pose_[EarL] + pose_[EarR]) / 2 > 0.35f ? -1.0f : (pose_[EarL] + pose_[EarR]) / 2 < -0.6f ? -1.0f : 1.0f; }
  void spawn(Fx kind, float x, float y, float vx, float vy, float life, float size, float delay = 0) {
    if (parts_.size() >= 16) parts_.erase(parts_.begin());
    parts_.push_back({kind, x, y, vx, vy, t_ + delay, life, size, range(0, 6.28f)});
  }
  // Sound effects only in idle (petting included): never over the user's voice, thinking or speech.
  void cue(const char* name, float pitch = 1) { cue(name, pitch, activityName_); }
  void cue(const char* name, float pitch, const std::string& activity, float delay = 0) {
    if (activity != "idle") return;
    if (sounds_.size() >= 3) sounds_.erase(sounds_.begin());
    sounds_.push_back({{name, pitch * range(0.92f, 1.08f)}, t_ + delay});
  }
  // Touch reactions in flight; react() adds each by name. side: the side it reacts toward (-1 left, +1 right).
  enum class Act { Pat, Nose, Sneeze, Cheek, Peek, Wink, Squeeze, Widen, Mumble, Nom, Chin, Sweep, Shake, TiltLook, Approach, Glance };
  struct Action { Act name; float at, duration, side; };
  void act(Act name, float duration, float side = 1, float delay = 0) {
    if (acts_.size() >= 10) acts_.erase(acts_.begin());
    acts_.push_back({name, t_ + delay, duration, side});
  }
  // Pick a reaction for a body part by weight, never the same one twice in a row.
  std::string pick(const std::string& zone, std::vector<std::pair<std::string, float>> options) {
    const auto last = lastPick_[zone];
    if (options.size() > 1) options.erase(std::remove_if(options.begin(), options.end(), [&](const auto& o) { return o.first == last; }), options.end());
    float total = 0; for (const auto& o : options) total += o.second;
    float r = random() * total; std::string chosen = options.front().first;
    for (const auto& o : options) { r -= o.second; if (r <= 0) { chosen = o.first; break; } }
    return lastPick_[zone] = chosen;
  }
  // A sneeze: a moment later the eyes screw shut, the head jerks and a few droplets fly.
  void sneeze(float delay) {
    act(Act::Sneeze, 0.6f, 1, delay); cue("achoo", 1, activityName_, delay);
    for (int i = 0; i < 5; ++i) {
      const float a = range(-0.9f, 0.9f) - kPi / 2;
      spawn(Fx::Spray, 512 + range(-20, 20), 380, std::cos(a) * range(60, 140), std::sin(a) * range(40, 110) + 60, 0.6f, 9, delay + 0.08f);
    }
  }
  // Effects that come and go with the mood.
  void effects(float dt) {
    if (mood_ == "petted" && t_ >= heartAt_) {  // hearts rise beside the face while being petted
      const float sx = sign(); spawn(Fx::Heart, 512 + sx * range(170, 330), range(250, 360), sx * 12, -60, range(1.4f, 1.8f), range(40, 56));
      heartAt_ = t_ + range(0.45f, 0.8f);
    }
    const bool chin = pressed_ && chinPress_ && chinStart_ >= 0;
    if ((petting_ || chin) && t_ >= purrAt_) { cue("purr", chin ? 0.9f : 1); purrAt_ = t_ + 1.3f - 0.4f * affection_; }  // purrs come sooner as it warms up
    affection_ *= std::pow(0.5f, dt / 90); annoy_ *= std::pow(0.5f, dt / 12);
    if (petting_ && pressed_) { affection_ += 0.25f * dt; annoy_ -= 0.4f * dt; }
    if (chin) {
      affection_ += 0.2f * dt; annoy_ -= 0.3f * dt;
      if (t_ >= heartAt_) { spawn(Fx::Heart, 512 + sign() * range(90, 200), 430, 0, -55, 1.5f, 40); heartAt_ = t_ + range(0.7f, 1.1f); }
    }
    if (pressed_ && moved_ < 20 && t_ - downAt_ > 0.9f) affection_ += 0.12f * dt;
    affection_ = std::clamp(affection_, 0.0f, 1.0f); annoy_ = std::clamp(annoy_, 0.0f, 1.0f);
    // Sulking: past 0.6 annoyance it goes "hmph" and turns away for 2.5-4 s; petting soon makes up.
    if (activityName_ == "idle" && !sulking_ && annoy_ > 0.6f && t_ >= sulkCooldown_) {
      sulking_ = true; sulkStart_ = t_; sulkUntil_ = t_ + range(2.5f, 4); sulkSide_ = lastTouchX_ < 512 ? 1.0f : -1.0f;
      spawn(Fx::Puff, 512 + sulkSide_ * 120, 410, sulkSide_ * 50, -12, 1.0f, 46); cue("hmph"); kick(-0.5f, -0.5f, 0.5f, Shape::Snap);
    }
    if (sulking_ && (t_ >= sulkUntil_ || annoy_ < 0.25f || activityName_ != "idle")) {  // over it: looks back with a blink
      const bool forgiven = annoy_ < 0.25f && t_ < sulkUntil_;
      sulking_ = false; sulkUntil_ = -9; sulkCooldown_ = t_ + 4; annoy_ *= 0.5f;
      blink(0); act(Act::Glance, 0.6f, -sulkSide_);
      if (forgiven) { spawn(Fx::Heart, 512, 330, 0, -50, 1.4f, 40); kick(0.4f, 0.4f, 0.4f); }
    }
    acts_.erase(std::remove_if(acts_.begin(), acts_.end(), [&](const Action& a) { return t_ >= a.at + a.duration; }), acts_.end());
    if (pressed_ && moved_ < 20 && t_ - downAt_ > 0.9f && t_ >= holdHeartAt_) {  // leaning in: a slow heart
      if (holdHeartAt_ < downAt_) cue("purr", 0.9f);
      spawn(Fx::Heart, 512 + range(-120, 120), 330, 0, -40, 1.8f, 44); holdHeartAt_ = t_ + 1.2f;
    }
    if (mood_ == "sorry" && t_ >= sweatAt_) {  // sheepish: a sweat drop by the head
      spawn(Fx::Sweat, 512 + sign() * range(285, 330), 150, 0, 30, 1.6f, 34); sweatAt_ = t_ + range(6, 10);
    }
    if (mood_ == "unknown" && t_ >= questionAt_) {  // unsure: a question mark above the head
      spawn(Fx::Question, 512 + sign() * range(300, 360), 115, 0, -12, 1.7f, 70); questionAt_ = t_ + range(5, 9);
    }
    if (mood_ == "happy" && t_ >= noteAt_) {  // happy: a note floats up
      const float sx = sign(); spawn(Fx::Note, 512 + sx * range(300, 380), 250, sx * 10, -38, 1.9f, 56); noteAt_ = t_ + range(2.5f, 5);
    }
    thinking_ += ((activityName_ == "processing" ? 1.0f : 0.0f) - thinking_) * std::min(1.0f, dt * 4);
    parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [&](const Particle& p) { return t_ >= p.start + p.life; }), parts_.end());
  }
  // Touching just under an ear: that ear shakes it off with a double flick, the other twitches in sympathy.
  void earTap(float side) {
    const auto one = [&](float a) { return side < 0 ? std::pair{a, 0.0f} : std::pair{0.0f, a}; };
    const float s = lift(side), o = lift(-side);
    const auto [a, b] = one(0.55f * s); kick(a, b, 0.22f);
    const auto [c, d] = one(0.45f * s); kick(c, d, 0.22f, Shape::Flick, 0.2f);
    const auto [e, f] = one(0.25f * o); kick(f, e, 0.25f, Shape::Flick, 0.12f);
  }
  // A twitch of its own, more often on the side it is looking: a flick, a double flick, both ears, or a slow swivel.
  void twitch() {
    const float amp = earAmplitude(mood_), r = random(), look = pose_[LookX] >= 0 ? 1.0f : -1.0f;
    const float side = random() < 0.65f ? look : -look, dir = lift(side);
    const auto one = [&](float a) { return side < 0 ? std::pair{a, 0.0f} : std::pair{0.0f, a}; };
    if (r < 0.55f) { const auto [l, rr] = one(0.45f * amp * dir); kick(l, rr, range(0.22f, 0.32f)); }
    else if (r < 0.7f) {
      const auto [l, rr] = one(0.4f * amp * dir); kick(l, rr, 0.22f);
      const auto [l2, r2] = one(0.35f * amp * dir); kick(l2, r2, 0.22f, Shape::Flick, 0.24f);
    } else if (r < 0.82f) { const float f = 0.3f * amp * lift(); kick(f, f, 0.26f); }
    else { const auto [l, rr] = one(-0.4f * amp * dir); kick(l, rr, range(0.6f, 0.8f), Shape::Swivel); }
  }
  void earLife(const Senses& senses) {
    if (const auto gap = earGap(activityName_, mood_)) {
      if (t_ >= nextTwitch_) { twitch(); nextTwitch_ = t_ + range(gap->first, gap->second); }
    } else nextTwitch_ = std::max(nextTwitch_, t_ + 0.5f);
    if (activityName_ == "listening") {  // after a pause, the start of the user's voice flicks one ear (at most every 4 s)
      const float voice = senses.voice.value_or(0);
      if (voice < 0.15f) { if (quietFrom_ < 0) quietFrom_ = t_; }
      else if (voice > 0.45f) {
        if (quietFrom_ >= 0 && t_ - quietFrom_ > 0.8f && t_ - lastFlickAt_ > 4) {
          const float side = sign(), f = 0.3f * lift(side);
          if (side < 0) kick(f, 0, 0.26f); else kick(0, f, 0.26f);
          lastFlickAt_ = t_;
        }
        quietFrom_ = -1;
      }
    }
    if (activity_->talk) {  // a sudden loud syllable now and then pricks both ears (emphasis)
      const float speech = senses.speech.value_or(0);
      if (speech > 0.75f && prevSpeech_ < 0.4f && t_ - lastFlickAt_ > 1.5f && random() < 0.25f) {
        const float f = 0.22f * lift(); kick(f, f, 0.24f); lastFlickAt_ = t_;
      }
      prevSpeech_ = speech;
    }
  }
  void addWave(float x, float y, float duration, float radius, float alpha, std::optional<Rgb> color = std::nullopt, float delay = 0) {
    waves_.erase(std::remove_if(waves_.begin(), waves_.end(), [&](const Wave& w) { return t_ > w.start + w.duration; }), waves_.end());
    if (waves_.size() >= 6) waves_.erase(waves_.begin());
    waves_.push_back({x, y, t_ + delay, duration, radius, alpha, color});
  }
  struct Running { const Variation* variation; float start, duration; Pick pick; };
  float random() { return std::uniform_real_distribution<float>(0, 1)(rng_); }
  float range(float a, float b) { return a + random() * (b - a); }
  float sign() { return random() < 0.5f ? -1.0f : 1.0f; }
  void blink(float delay, float length = 0) { blinks_.push_back({t_ + delay, length > 0 ? length : moodTable_ ? moodTable_->blink : 0.17f}); }
  void start() {
    std::vector<const Variation*> pool;
    for (const auto& v : activity_->vars) pool.push_back(&v);
    if (moodTable_) for (const auto& v : moodTable_->vars) pool.push_back(&v);
    if (pool.size() > 1) pool.erase(std::remove(pool.begin(), pool.end(), last_), pool.end());  // never twice in a row
    float total = 0; for (const auto* v : pool) total += v->weight;
    float r = random() * total; const Variation* chosen = pool.front();
    for (const auto* v : pool) { r -= v->weight; if (r <= 0) { chosen = v; break; } }
    current_ = Running{chosen, t_, range(chosen->min, chosen->max), {sign(), range(-1, 1), side_}};
    last_ = chosen;
    for (int i = 0; i < chosen->blinks; ++i) blink(float(i) * 0.26f);
    if (chosen->slowBlink > 0) blink(0.2f, chosen->slowBlink);
  }
  std::mt19937 rng_{std::random_device{}()};
  float t_ = 0, side_ = 1, nextVariation_ = 0, nextBlink_ = 0, nextMicro_ = 0, closure_ = 0, zzPhase_ = 0;
  // Touch, wake-up and environment state.
  bool pressed_ = false, petting_ = false;
  float gaze_[2] = {512, 250}, down_[2] = {0, 0}, downAt_ = 0, moved_ = 0, travel_ = 0, travelStart_ = 0, gazeUntil_ = -9, pettedUntil_ = -9;
  float interactedAt_ = 0, wakeAt_ = -9, burstAt_ = -9;
  float lastTapAt_ = -9, lastTap_[2] = {0, 0}, nomAt_ = -9, whiskAt_ = -9, whiskSide_ = 1, beamAt_ = -9, startleAt_ = -9, swipeAt_ = -9, swipeSide_ = 1;
  std::vector<float> taps_;
  std::vector<Wave> waves_;
  float aura_[3] = {0, 0, 0}, auraAmount_ = 0, voice_ = 0, listening_ = 0;
  std::array<std::string, 3> saved_;
  std::string activityName_, status_;
  float micro_[2] = {0, 0}, color_[3] = {kBlue.r, kBlue.g, kBlue.b};
  // Ears: per-state left/right bias, quick gestures, next self-twitch, voice-onset detection, published targets.
  float earSkew_ = 0, nextTwitch_ = 0, quietFrom_ = -1, lastFlickAt_ = -9, prevSpeech_ = 0;
  bool begun_ = false;
  std::vector<Kick> kicks_;
  std::array<float, 2> ears_{0, 0};
  std::vector<Particle> parts_;
  struct Pending { SoundCue cue; float at; };
  std::vector<Pending> sounds_;
  // Touch memory: affection builds with gentle touch (half-life 90 s), annoyance with quick pokes (half-life 12 s).
  float affection_ = 0, annoy_ = 0, lastPokeAt_ = -9, lastNoseAt_ = -9, lastTouchX_ = 512;
  float sulkStart_ = 0, sulkUntil_ = -9, sulkSide_ = 1, sulkCooldown_ = 0, chinStart_ = -9;
  bool sulking_ = false, chinPress_ = false;
  std::vector<Action> acts_;
  std::map<std::string, std::string> lastPick_;
  float thinking_ = 0, heartAt_ = 0, holdHeartAt_ = 0, sweatAt_ = 0, questionAt_ = 0, noteAt_ = 0, purrAt_ = 0;
  bool snap_ = true;
  std::string mood_, label_;
  const Activity* activity_ = nullptr;
  const Mood* moodTable_ = nullptr;
  const Variation* last_ = nullptr;
  std::optional<Running> current_;
  std::vector<std::pair<float, float>> blinks_;
  Pose pose_ = kRest, velocity_{};
  Rgb target_ = kBlue;
};

Face buildFace(const Director& director, bool whiskers, const std::string& mouthMode, bool speaking) {
  Face face;
  auto& L = face.layers;
  const Pose& P = director.pose();
  const Rgb col = director.color();
  const float t = director.time(), blink = director.blinkClosure();
  face.tilt = P[Tilt]; face.scale = P[Scale]; face.bob = P[Bob];
  face.aura = director.aura(); face.auraAmount = director.auraAmount();
  for (const float side : {-1.0f, 1.0f}) {
    // The eye keeps one oval shape; only its size changes, so it never turns round. Its shape changes only
    // by being covered: the upper lid (LidTop), an upper lid drooping a little at the outer corner (LidSad),
    // and a cheek pushing up from below when smiling (LidHappy). Only a strong smile (LidHappy above ~0.56,
    // e.g. a beaming moment) turns it into a closed ^ arc.
    const float happy = std::clamp(P[LidHappy], 0.0f, 1.1f), arcK = smooth((happy - 0.56f) / 0.08f);
    const float cx = (side < 0 ? 348.0f : 676.0f) + P[LookX] - side * P[Cross], cy0 = 240 + P[LookY] - 6 * happy;
    const float closed = std::clamp(blink + std::max(0.0f, side * P[Wink]), 0.0f, 1.0f);  // a wink folds one eye
    const float breath = 1 + 0.012f * std::sin(t * 1.7f);  // the eyes breathe very slightly
    // A blink folds the rounded eye toward a point a third of the way up from its bottom rather than its
    // middle; the lids and cheek fold with it, so a drooping or smiling eye still closes cleanly.
    const float size = std::max(0.5f, P[EyeSize] * (1 + side * P[EyeAsym])) * breath, hw = 64 * size, full = 90 * size;
    const float hh = std::max(4.0f, full * (1 - 0.94f * closed)), cy = cy0 + 0.3f * full * closed, r = std::min(hw, hh);
    const float lid = std::clamp(P[LidTop] + (side > 0 ? P[LidTopR] : 0), 0.0f, 0.92f), yl = cy - hh + 2 * hh * lid;
    // Sad lid: the inner corner stays put and the lid droops toward the outer corner (at 1: 28% of the eye height
    // at the outer edge, 11% in the middle, ~26 degrees). Cutting over half the eye made a teardrop that looked wrong.
    const float sad = std::clamp(P[LidSad], 0.0f, 1.0f), sy = cy - hh + 0.22f * hh * sad;
    const float sm = 0.34f * hh * sad * side / hw, smn = std::hypot(1.0f, sm);
    // Cheek: a circle much wider than the eye rises from below and bends its bottom gently up into a smile.
    // A circle narrower than the eye would leave two pointed "feet" at the bottom, which looks wrong.
    const float cheek = 0.9f * hh * std::min(happy, 0.35f), cheekR = 3 * hw, cheekY = cy + hh - cheek + cheekR;
    const auto eyeDistance = [=](float x, float y) {
      float d = sdBox(x, y, cx, cy, hw, hh, r);
      if (lid > 0.001f) d = std::max(d, yl - y);
      if (sad > 0.001f) d = std::max(d, (sy + sm * (x - cx) - y) / smn);
      if (cheek > 0.5f) d = std::max(d, cheekR - std::hypot(x - cx, y - cheekY));
      return d;
    };
    if (arcK > 0.02f) {
      const float ay = 270 + P[LookY], ar = hw * 0.95f;
      L.push_back({col, arcK, cx - ar - 14, ay - ar - 14, cx + ar + 14, ay + 14,
                   [=](float x, float y) { return sdArc(x, y, cx, ay, ar, -kPi + 0.5f, -0.5f, 12); }, nullptr});
    }
    if (arcK > 0.98f) continue;
    // One small, pale catchlight: enough to look alive without reading as a pupil. The light is fixed,
    // so it drifts slightly against the gaze. It goes first so the eye cannot overwrite it on ties.
    if (hh > 34) {
      const float gx = cx + hw * 0.3f - P[LookX] * 0.08f, gy = cy - hh * 0.4f, gr = r * 0.3f;
      const Rgb glint{uint8_t(col.r * 0.35f + 240 * 0.65f), uint8_t(col.g * 0.35f + 250 * 0.65f), uint8_t(col.b * 0.35f + 255 * 0.65f)};
      L.push_back({glint, 1 - arcK, gx - gr, gy - gr, gx + gr, gy + gr,
                   [=](float x, float y) { return std::max(sdCircle(x, y, gx, gy, gr), eyeDistance(x, y) + 6); }, nullptr});
    }
    L.push_back({col, 1 - arcK, cx - hw, cy - hh, cx + hw, cy + hh, eyeDistance,
                 [=](float, float y) { return 1 - 0.28f * smooth((y - (cy - hh * 0.3f)) / (hh * 1.3f)); }});  // soft shade reads as volume
  }

  // The mouth is always the cat's ω, the same size in every state. It follows 75-80% of the gaze (whiskers
  // 65-75%), so the eyes lead and the whole face turns with them; following only 30% stretched the gap
  // between eyes and mouth when looking up, which looked like the face itself was deforming. Emotion tilts each lobe about the middle peak (up = smile, down = sad),
  // MouthWave wobbles the lobes, and speaking opens a small mouth below the peak.
  if (mouthMode == "always" || (mouthMode == "speaking" && speaking)) {
    const float mx = 512 + P[LookX] * 0.75f + P[MouthX], my = 392 + P[LookY] * 0.8f, py = my + 3;
    constexpr float ar = 27;
    for (const float s : {-1.0f, 1.0f}) {
      const float angle = -s * P[MouthSmile] * 0.4f, cs = std::cos(angle), sn = std::sin(angle);
      const float ax = mx + s * ar, ay = my - 6 + P[MouthWave] * 4 * std::sin(t * 6 + s * 1.5f);
      L.push_back({col, 1, mx + std::min(0.0f, 2 * s * ar) - 14, my - 30, mx + std::max(0.0f, 2 * s * ar) + 14, my + ar + 16,
                   [=](float x, float y) {
        // Undo the lobe's tilt about the peak, then measure the plain lower arc.
        const float dx = x - mx, dy = y - py;
        return sdArc(mx + cs * dx + sn * dy, py - sn * dx + cs * dy, ax, ay, ar, kDown0, kDown1, 5);
      }, nullptr});
    }
    const float open = std::clamp(P[MouthOpen], 0.0f, 1.0f);
    if (open > 0.03f) {
      const float oh = 4 + open * 20, ow = 14 + open * 6, oy = py + 2 + oh;
      L.push_back({col, smooth(open * 6), mx - ow, oy - oh, mx + ow, oy + oh,
                   [=](float x, float y) { return sdBox(x, y, mx, oy, ow, oh, std::min(ow, oh)); }, nullptr});
    }
  }

  // Whiskers: gently bowed strands that thin toward the tip and sway slowly.
  if (whiskers) for (const float side : {-1.0f, 1.0f}) for (const float k : {-1.0f, 0.0f, 1.0f}) {
    const float ix = 512 + side * 222 + P[LookX] * 0.65f, iy = 392 + k * 18 + P[Whisk] * 0.25f + P[LookY] * 0.75f;
    const float bx = 512 + side * 404 + P[LookX] * 0.65f, by = iy + k * 26 + P[Whisk] + std::sin(t * 1.1f + k * 0.9f + side) * 3;
    const float qx = (ix + bx) / 2, qy = (iy + by) / 2 + 9 + k * 4;
    float px = ix, py = iy;
    for (int i = 1; i <= 6; ++i) {
      const float u = float(i) / 6, v = 1 - u, t0 = 3 - 2 * float(i - 1) / 6, t1 = 3 - 2 * u;
      const float nx = v * v * ix + 2 * v * u * qx + u * u * bx, ny = v * v * iy + 2 * v * u * qy + u * u * by;
      L.push_back({col, 0.55f, std::min(px, nx) - 4, std::min(py, ny) - 4, std::max(px, nx) + 4, std::max(py, ny) + 4,
                   [=](float x, float y) { return sdTaper(x, y, px, py, nx, ny, t0, t1); }, nullptr});
      px = nx; py = ny;
    }
  }
  // Listening: up to three sound ripples beside the face, mirrored left and right.
  const float listen = director.listening();
  for (int k = 0; k < 3 && listen > 0.03f; ++k) {
    const float alpha = std::clamp((listen - float(k) * 0.25f) * 2.2f, 0.0f, 1.0f) * 0.75f, r = 70 + float(k) * 34;
    if (alpha < 0.03f) continue;
    const float x0 = 812 + 0.76f * r - 10, x1 = 812 + r + 10, y0 = 250 - 0.65f * r - 10, y1 = 250 + 0.65f * r + 10;
    L.push_back({col, alpha, x0, y0, x1, y1, [=](float x, float y) { return sdArc(x, y, 812, 250, r, -0.7f, 0.7f, 4); }, nullptr});
    L.push_back({col, alpha, 1024 - x1, y0, 1024 - x0, y1, [=](float x, float y) { return sdArc(1024 - x, y, 812, 250, r, -0.7f, 0.7f, 4); }, nullptr});
  }
  // Dot waves: a band of dots lights up as a ring spreads and fades, used sparingly for key moments.
  for (const auto& w : director.waves()) {
    const float age = (t - w.start) / w.duration;
    if (age < 0 || age >= 1) continue;
    const float r = w.radius * (1 - (1 - age) * (1 - age)), x = w.x, y = w.y;
    L.push_back({w.color.value_or(col), w.alpha * (1 - age), x - r - 12, y - r - 12, x + r + 12, y + r + 12,
                 [=](float px, float py) { return std::abs(sdCircle(px, py, x, y, r)) - 9; }, nullptr});
  }
  // Effect particles fade in over 15 % of their life and out over the last 40 %.
  for (const auto& q : director.particles()) {
    const float age = t - q.start;
    if (age < 0) continue;
    const float f = age / q.life, a = (f < 0.15f ? f / 0.15f : f > 0.6f ? (1 - f) / 0.4f : 1) * 0.9f;
    float x = q.x + q.vx * age, y = q.y + q.vy * age, s = q.size;
    using Fx = Director::Fx;
    const Rgb c = q.kind == Fx::Heart ? Rgb{255, 140, 175} : q.kind == Fx::Spark ? Rgb{255, 236, 170} : q.kind == Fx::Sweat ? Rgb{150, 210, 255}
                : q.kind == Fx::Puff ? Rgb{215, 228, 238} : q.kind == Fx::Spray ? Rgb{190, 225, 245} : col;
    if (q.kind == Fx::Heart) { x += 10 * std::sin(age * 3 + q.phase); s *= 0.85f + 0.15f * std::sin(age * 9 + q.phase); }
    if (q.kind == Fx::Spark) s *= 0.5f + 0.5f * bump(f);
    if (q.kind == Fx::Question) y += 4 * std::sin(age * 5);
    if (q.kind == Fx::Shock) {  // three lines above the head
      L.push_back({c, a * bump(f) / 0.9f, 360, 0, 664, 100, [](float px, float py) {
        return std::min({sdTaper(px, py, 420, 78, 380, 20, 8, 4), sdTaper(px, py, 512, 62, 512, 4, 8, 4), sdTaper(px, py, 604, 78, 644, 20, 8, 4)}); }, nullptr});
      continue;
    }
    const float r = s * 1.6f + 8;
    std::function<float(float, float)> d;
    switch (q.kind) {
      case Fx::Heart: d = [=](float px, float py) { return sdHeart(px, py, x, y, s); }; break;
      case Fx::Spark: d = [=](float px, float py) { return sdStar4(px, py, x, y, s); }; break;
      case Fx::Sweat: d = [=](float px, float py) { return sdDrop(px, py, x, y, s); }; break;
      case Fx::Question: d = [=](float px, float py) { return sdQuestion(px, py, x, y, s); }; break;
      case Fx::Puff: { const float ps = s * (0.8f + 0.4f * f), dir = q.vx < 0 ? -1.0f : 1.0f; d = [=](float px, float py) { return sdPuff(px, py, x, y, ps, dir); }; break; }
      case Fx::Spray: d = [=](float px, float py) { return sdCircle(px, py, x, y, s); }; break;
      default: d = [=](float px, float py) { return sdNote(px, py, x, y, s); }; break;
    }
    L.push_back({c, a, x - r, y - r - s, x + r, y + r, d, nullptr});
  }
  // Thinking: three dots above the side it looks to, lighting up in turn.
  if (const float k = director.thinking(); k > 0.03f) for (int i = 0; i < 3; ++i) {
    const float side = director.side(), x = 512 + side * (250 + float(i) * 46), y = 80 - float(i) * 14;
    const float pulse = 0.35f + 0.65f * std::max(0.0f, std::sin(t * 3.2f - float(i) * 0.7f));
    L.push_back({col, k * pulse * 0.85f, x - 20, y - 20, x + 20, y + 20, [=](float px, float py) { return sdCircle(px, py, x, y, 14); }, nullptr});
  }
  if (P[Zz] > 0.02f) for (int i = 0; i < 2; ++i) {
    const float q = std::clamp(director.zzPhase() * 1.3f - float(i) * 0.3f, 0.0f, 1.0f);
    const float x = 770 + q * 50 + float(i) * 28, y = 190 - q * 80 - float(i) * 30, h = 11 + float(i) * 4;
    L.push_back({col, P[Zz] * 0.8f * bump(q), x - h - 5, y - h - 5, x + h + 5, y + h + 5, [=](float px, float py) {
      return std::min({sdTaper(px, py, x - h, y - h, x + h, y - h, 3, 3), sdTaper(px, py, x + h, y - h, x - h, y + h, 3, 3),
                       sdTaper(px, py, x - h, y + h, x + h, y + h, 3, 3)}); }, nullptr});
  }
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
    // SDL normalizes touch against the logical viewport; invert our canvas rotation.
    const double ox = x, oy = y;
    if (config.rotation == 90) { x = oy; y = 1 - ox; }
    if (config.rotation == 180) { x = 1 - ox; y = 1 - oy; }
    if (config.rotation == 270) { x = 1 - oy; y = ox; }
#ifdef DESKPET_SDL
    // The face reacts to touch by itself, whether or not the pipeline has subscribed to touch events.
    if (std::isfinite(x) && std::isfinite(y) && x >= 0 && x <= 1 && y >= 0 && y <= 1 && (phase == "down" || phase == "move" || phase == "up"))
      director.touch(phase, float(x) * kDesignW, float(y) * kDesignH);
#endif
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
    Json fields{{"phase", phase}, {"contactId", id}, {"x", x}, {"y", y}, {"simulated", simulated}};
    if (phase == "move") { contacts[id] = fields; moved[id] = fields; return; }
    if (moved.count(id)) { emit(event(*touchSubscription, "touch.event", moved[id])); moved.erase(id); }
    emit(event(*touchSubscription, "touch.event", fields));
    if (phase == "down") contacts[id] = fields; else contacts.erase(id);
  }
  // Latest microphone level, for the listening ripples.
  struct Mic { mutable std::mutex mutex; float level = 0; Clock::time_point at; } mic;
  std::optional<float> voiceLevel(Clock::time_point now) const {
    std::lock_guard lock(mic.mutex);
    if (now - mic.at > std::chrono::milliseconds(250)) return std::nullopt;  // capture stopped or stalled
    return mic.level;
  }
  // Mouth opening per 20 ms of queued speech, placed on the playback clock.
  struct Speech {
    mutable std::mutex mutex;
    bool active = false, started = false;
    Format format; Clock::time_point start;
    std::vector<float> levels;
    double sum = 0; size_t count = 0;  // the window being filled
    void reset() { active = started = false; levels.clear(); sum = 0; count = 0; }
  } speech;
  std::optional<float> speechLevel(Clock::time_point now) const {
    std::lock_guard lock(speech.mutex);
    if (!speech.active) return std::nullopt;
    if (!speech.started) return 0.0f;
    const double ms = std::chrono::duration<double, std::milli>(now - speech.start).count() - config.lipSyncDelayMs;
    if (ms < 0) return 0.0f;
    const size_t i = size_t(ms / 20);
    if (i >= speech.levels.size()) return 0.0f;
    const float next = i + 1 < speech.levels.size() ? speech.levels[i + 1] : 0, f = float(ms / 20 - double(i));
    return speech.levels[i] + (next - speech.levels[i]) * f;
  }
  // Ear targets published by the render thread for the servo thread.
  std::atomic<float> earLeft{0}, earRight{0};
  std::atomic_bool earsLive{false};
  std::vector<SoundCue> sounds;  // asked for by the face, taken by the runtime on the same (main) thread
  uint64_t revision = 0;
  bool revisionSet = false, failNext = false;
  std::string error;
  Clock::time_point last = Clock::now() - std::chrono::seconds(1);
#ifdef DESKPET_SDL
  Director director;  // springs the face toward the current view and picks random variations
  Clock::time_point frameAt = Clock::now();
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* canvas = nullptr;
  SDL_Texture* presented = nullptr;
  SDL_Texture* dot = nullptr;    // white anti-aliased disc, tinted per dot
  SDL_Texture* unlit = nullptr;  // background with the dim dot grid
  SDL_Texture* glow = nullptr;   // half-resolution lit dots, stretched for bloom
  SDL_Texture* aura = nullptr;   // white unlit dots fading out from the centre, tinted per state
  TTF_Font* font = nullptr;
  bool sdl = false, ttf = false;
  std::vector<std::string> lines;
  std::vector<float> level;
  std::vector<Rgb> tint;
  ~Impl() {
    if (dot) SDL_DestroyTexture(dot);
    if (unlit) SDL_DestroyTexture(unlit);
    if (glow) SDL_DestroyTexture(glow);
    if (aura) SDL_DestroyTexture(aura);
    if (font) TTF_CloseFont(font);
    if (canvas) SDL_DestroyTexture(canvas);
    if (presented) SDL_DestroyTexture(presented);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (ttf) TTF_Quit();
    if (sdl) SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
  void color(int r, int g, int b) { SDL_SetRenderDrawColor(renderer, uint8_t(r), uint8_t(g), uint8_t(b), 255); }
  // A whole-pixel pitch lets every dot be the same bitmap; leftover pixels become an even margin.
  struct Grid { int cols, rows, cell, left, top; };
  Grid grid() const {
    const int cell = config.dotSize, cols = config.width / cell, rows = config.height / cell;
    return {cols, rows, cell, (config.width - cols * cell) / 2, (config.height - rows * cell) / 2};
  }
  void stamp(const Grid& g, int i, int j, Rgb c, uint8_t alpha) {
    SDL_SetTextureColorMod(dot, c.r, c.g, c.b); SDL_SetTextureAlphaMod(dot, alpha);
    SDL_Rect dst{g.left + i * g.cell, g.top + j * g.cell, g.cell, g.cell};
    SDL_RenderCopy(renderer, dot, nullptr, &dst);
  }
  void prepareDots(const Grid& g) {
    // Rasterize the disc at its final size (4x4 supersampled) so it is never rescaled.
    const int size = g.cell;
    const float r = float(size) * 0.42f, c = float(size) / 2;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) throw Error("DISPLAY_ERROR", SDL_GetError());
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
      int inside = 0;
      for (int v = 0; v < 4; ++v) for (int u = 0; u < 4; ++u)
        inside += std::hypot(float(x) + (float(u) + 0.5f) / 4 - c, float(y) + (float(v) + 0.5f) / 4 - c) <= r;
      static_cast<uint32_t*>(s->pixels)[y * s->pitch / 4 + x] = (uint32_t(inside * 255 / 16) << 24) | 0xffffff;
    }
    dot = SDL_CreateTextureFromSurface(renderer, s); SDL_FreeSurface(s);
    if (!dot) throw Error("DISPLAY_ERROR", SDL_GetError());
    SDL_SetTextureBlendMode(dot, SDL_BLENDMODE_BLEND);
    unlit = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, config.width, config.height);
    if (!unlit || SDL_SetRenderTarget(renderer, unlit) < 0) throw Error("DISPLAY_ERROR", SDL_GetError());
    color(10, 19, 29); SDL_RenderClear(renderer);
    for (int j = 0; j < g.rows; ++j) for (int i = 0; i < g.cols; ++i) stamp(g, i, j, {60, 92, 112}, 46);
    aura = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, config.width, config.height);
    if (!aura || SDL_SetRenderTarget(renderer, aura) < 0) throw Error("DISPLAY_ERROR", SDL_GetError());
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); SDL_RenderClear(renderer);
    for (int j = 0; j < g.rows; ++j) for (int i = 0; i < g.cols; ++i) {
      const float sx = kDesignW / float(config.width), sy = kDesignH / float(config.height);
      const float dx = (float(g.left) + (float(i) + 0.5f) * float(g.cell)) * sx - 512, dy = (float(g.top) + (float(j) + 0.5f) * float(g.cell)) * sy - 290;
      const float d = std::hypot(dx, dy) / 640, a = d < 0.6f ? 0.55f - 0.33f * d / 0.6f : std::max(0.0f, 0.22f * (1 - (d - 0.6f) / 0.4f));
      if (a > 0.004f) stamp(g, i, j, {255, 255, 255}, uint8_t(a * 255));
    }
    SDL_SetTextureBlendMode(aura, SDL_BLENDMODE_BLEND);
    glow = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, (g.cols + 1) / 2, (g.rows + 1) / 2);
    if (!glow) throw Error("DISPLAY_ERROR", SDL_GetError());
    SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(glow, SDL_ScaleModeLinear);
  }
  // Sample each layer at dot centres. Every dot keeps the same shape; edge dots are only dimmer.
  // Dot centres are mapped back through the face's tilt, scale and bob into face space.
  void drawFace(const Face& face) {
    const Grid g = grid();
    const size_t n = size_t(g.cols) * size_t(g.rows);
    level.assign(n, 0); tint.resize(n);
    const float sx = kDesignW / float(config.width), sy = kDesignH / float(config.height), soft = float(g.cell) * sx * 1.1f;
    constexpr float ox = 512, oy = 300;
    const float cs = std::cos(face.tilt), sn = std::sin(face.tilt), sc = face.scale;
    for (const auto& layer : face.layers) {
      float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
      for (const float lx : {layer.x0, layer.x1}) for (const float ly : {layer.y0, layer.y1}) {
        const float x = ox + sc * (cs * (lx - ox) - sn * (ly - oy)), y = oy + sc * (sn * (lx - ox) + cs * (ly - oy)) + face.bob;
        minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, y); maxY = std::max(maxY, y);
      }
      const auto cell = [&](float design, float scale, int origin) { return int(std::floor((design / scale - float(origin)) / float(g.cell))); };
      const int i0 = std::max(0, cell(minX, sx, g.left) - 2), i1 = std::min(g.cols - 1, cell(maxX, sx, g.left) + 2);
      const int j0 = std::max(0, cell(minY, sy, g.top) - 2), j1 = std::min(g.rows - 1, cell(maxY, sy, g.top) + 2);
      for (int j = j0; j <= j1; ++j) {
        const float dy = (float(g.top) + (float(j) + 0.5f) * float(g.cell)) * sy - oy - face.bob;
        for (int i = i0; i <= i1; ++i) {
          const float dx = (float(g.left) + (float(i) + 0.5f) * float(g.cell)) * sx - ox;
          const float lx = ox + (cs * dx + sn * dy) / sc, ly = oy + (-sn * dx + cs * dy) / sc;
          float c = 0.5f - layer.distance(lx, ly) * sc / soft;
          if (c <= 0) continue;
          c = std::min(c, 1.0f) * layer.alpha * (layer.shade ? layer.shade(lx, ly) : 1);
          const size_t k = size_t(j) * size_t(g.cols) + size_t(i);
          if (c > level[k]) { level[k] = c; tint[k] = layer.color; }
        }
      }
    }
    SDL_RenderCopy(renderer, unlit, nullptr, nullptr);
    if (face.auraAmount > 0.01f) {
      SDL_SetTextureColorMod(aura, face.aura.r, face.aura.g, face.aura.b);
      SDL_SetTextureAlphaMod(aura, uint8_t(std::clamp(face.auraAmount, 0.0f, 1.0f) * 255));
      SDL_RenderCopy(renderer, aura, nullptr, nullptr);
    }
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
        SDL_Rect dst{g.left, g.top, gw * 2 * g.cell, gh * 2 * g.cell};
        SDL_RenderCopy(renderer, glow, nullptr, &dst);
      }
    }
    for (size_t k = 0; k < n; ++k) {
      if (level[k] < 0.04f) continue;
      stamp(g, int(k % size_t(g.cols)), int(k / size_t(g.cols)), tint[k], uint8_t(level[k] * 255));
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
      const auto now = Clock::now();
      // config.timeOfDay now only turns idle boredom and dozing on; the evening/night lids and dimming are gone.
      const Senses senses{speechLevel(now), voiceLevel(now), config.timeOfDay};
      director.update(std::chrono::duration<float>(now - frameAt).count(), senses);
      frameAt = now;
      const auto ears = director.ears();
      earLeft = ears[0]; earRight = ears[1]; earsLive = true;
      for (auto& cue : director.takeSounds()) { if (sounds.size() >= 4) sounds.erase(sounds.begin()); sounds.push_back(std::move(cue)); }
      drawFace(buildFace(director, config.whiskers, config.mouth, view.value("activity", "idle") == "speaking"));
      if (font && !director.label().empty()) {
        int tw = 0; TTF_SizeUTF8(font, director.label().c_str(), &tw, nullptr);
        text(director.label(), (config.width - tw) / 2, int(float(config.height) * 0.83f));
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
    // Letterbox bars (e.g. a 16:10 Mac in fullscreen) are outside every copy; clear them or they show stale buffers.
    color(10, 19, 29); SDL_RenderClear(renderer);
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
    // display.screen picks the monitor on multi-head hosts; out-of-range indexes fall back to the first one.
    const int screen = c.screen < SDL_GetNumVideoDisplays() ? c.screen : 0;
    d.window = SDL_CreateWindow(c.mock ? "DeskPet · 모의 실행" : "DeskPet", SDL_WINDOWPOS_CENTERED_DISPLAY(screen), SDL_WINDOWPOS_CENTERED_DISPLAY(screen),
      c.fullscreen ? pw : pw / 2, c.fullscreen ? ph : ph / 2, SDL_WINDOW_ALLOW_HIGHDPI | (c.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE));
    if (!d.window) throw Error("DISPLAY_ERROR", SDL_GetError());
    // The device screen has no pointer; without a desktop the X cursor would sit over the face.
    if (!c.mock && c.fullscreen) SDL_ShowCursor(SDL_DISABLE);
    d.renderer = SDL_CreateRenderer(d.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!d.renderer) d.renderer = SDL_CreateRenderer(d.window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!d.renderer) throw Error("DISPLAY_ERROR", SDL_GetError());
    SDL_RenderSetLogicalSize(d.renderer, pw, ph);
    d.canvas = SDL_CreateTexture(d.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, c.width, c.height);
    if (!d.canvas) throw Error("DISPLAY_ERROR", SDL_GetError());
    d.presented = SDL_CreateTexture(d.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, pw, ph);
    if (!d.presented) throw Error("DISPLAY_ERROR", SDL_GetError());
    // Window scaling is rarely an integer factor; nearest sampling would give dots uneven shapes.
    SDL_SetTextureScaleMode(d.presented, SDL_ScaleModeLinear);
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
#ifdef DESKPET_SDL
  if (mode == "face" && view != d.view)
    d.director.set(activity, view.value("emotion", "neutral"), view.value("actionStatus", ""));
#endif
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
  if (!d.error.empty() || Clock::now() - d.last < std::chrono::milliseconds(16)) return true;
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
#ifdef DESKPET_SDL
  d.director.set("idle", "neutral", "");
#endif
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
void Display::speechBegin(const Format& format) {
  auto& s = impl_->speech; std::lock_guard lock(s.mutex);
  s.reset(); s.active = true; s.format = format;
}
void Display::speechAudio(const Bytes& pcm) {
  auto& s = impl_->speech; std::lock_guard lock(s.mutex);
  if (!s.active) return;
  // RMS per 20 ms window, mapped from -48..-14 dBFS to a closed..open mouth (before playback gain).
  const size_t window = size_t(s.format.rate / 50) * size_t(s.format.channels);
  for (size_t i = 0; i + 1 < pcm.size(); i += 2) {
    const double v = double(int16_t(uint16_t(pcm[i]) | uint16_t(pcm[i + 1]) << 8)) / 32768;
    s.sum += v * v;
    if (++s.count < window) continue;
    const double db = 20 * std::log10(std::sqrt(s.sum / double(s.count)) + 1e-9);
    s.levels.push_back(float(std::clamp((db + 48) / 34, 0.0, 1.0)));
    s.sum = 0; s.count = 0;
  }
}
void Display::speechStarted(Clock::time_point when) {
  auto& s = impl_->speech; std::lock_guard lock(s.mutex);
  if (s.active && !s.started) { s.started = true; s.start = when; }
}
void Display::micLevel(const Bytes& pcm) {
  // RMS of one capture frame, mapped from -50..-15 dBFS to 0..1.
  double sum = 0; size_t n = 0;
  for (size_t i = 0; i + 1 < pcm.size(); i += 2, ++n) {
    const double v = double(int16_t(uint16_t(pcm[i]) | uint16_t(pcm[i + 1]) << 8)) / 32768; sum += v * v;
  }
  if (!n) return;
  const double db = 20 * std::log10(std::sqrt(sum / double(n)) + 1e-9);
  auto& m = impl_->mic; std::lock_guard lock(m.mutex);
  m.level = float(std::clamp((db + 50) / 35, 0.0, 1.0)); m.at = Clock::now();
}
void Display::speechEnd() {
  auto& s = impl_->speech; std::lock_guard lock(s.mutex);
  s.reset();
}
std::optional<float> Display::speechLevel(Clock::time_point now) const { return impl_->speechLevel(now); }
std::vector<SoundCue> Display::takeSounds() { return std::exchange(impl_->sounds, {}); }
std::optional<std::array<float, 2>> Display::ears() const {
  if (!impl_->earsLive) return std::nullopt;
  return std::array<float, 2>{impl_->earLeft.load(), impl_->earRight.load()};
}
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
