#include "deskpet/display.hpp"
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <array>
#include <functional>
#include <map>
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
struct Face { std::vector<Layer> layers; float tilt = 0, scale = 1, bob = 0; };

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

enum Key { LookX, LookY, Scale, Tilt, Bob, EyeW, EyeH, LidTop, LidTopR, LidSad, LidHappy,
           MouthCat, MouthSmile, MouthOpen, MouthWidth, MouthX, MouthWave, Whisk, Blush, Zz, kKeys };
using Pose = std::array<float, kKeys>;
constexpr Pose kRest{0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0.3f, 0, 1, 0, 0, 0, 0, 0};
float stiffness(int k) {
  switch (k) {
    case MouthOpen: return 700;
    case LookX: case LookY: return 220;
    case Tilt: return 70;
    case Bob: return 110;
    case LidSad: return 60;
    case LidHappy: return 90;
    case Blush: return 40;
    case Zz: return 30;
    default: return 150;
  }
}

// Random values fixed when a variation starts: s = sign, k = -1..1, side = the activity's chosen side.
struct Pick { float s, k, side; };
using Apply = std::function<void(float u, const Pick& p, Pose& d)>;
struct Variation { const char* name; float min, max, weight; int blinks; bool track; Apply apply; };
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
struct Mood { std::vector<std::pair<Key, float>> set, add; std::vector<Variation> vars; };

const Activity& activityFor(const std::string& name) {
  using P = const Pick&;
  static const std::map<std::string, Activity> table{
    {"idle", {1.2f, 3.6f, false, [](float, Pose&) {}, {
      var("두리번", 1.6f, 2.8f, [](float u, P p, Pose& d) { d[LookX] += p.s * 48 * envelope(u); d[LookY] += p.k * 10 * envelope(u); }),
      var("양옆 살피기", 2.6f, 3.4f, [](float u, P, Pose& d) { d[LookX] += 44 * std::sin(u * kPi * 2) * envelope(u, 0.1f, 0.9f); }),
      var("느긋한 눈감기", 1.4f, 2.2f, [](float u, P, Pose& d) { d[LidTop] += 0.5f * envelope(u, 0.35f, 0.65f); d[MouthSmile] += 0.2f * envelope(u); }),
      var("갸웃", 1.8f, 2.6f, [](float u, P p, Pose& d) { d[Tilt] += p.s * 0.08f * envelope(u); d[LookX] += p.s * 10 * envelope(u); }),
      var("하품", 2.0f, 2.6f, [](float u, P, Pose& d) {
        d[MouthOpen] += 0.75f * envelope(u, 0.3f, 0.6f); d[MouthCat] -= envelope(u, 0.2f, 0.8f);
        d[LidHappy] += 0.8f * envelope(u, 0.3f, 0.6f); d[Scale] += 0.03f * envelope(u); }, 0.5f),
      var("수염 씰룩", 0.7f, 1.0f, [](float u, P, Pose& d) { d[Whisk] += 7 * std::sin(u * kPi * 8) * envelope(u, 0.1f, 0.9f); }),
      var("살짝 다가옴", 1.6f, 2.4f, [](float u, P, Pose& d) { d[Scale] += 0.045f * envelope(u); d[LookY] += 6 * envelope(u); }),
    }, ""}},
    {"listening", {0.8f, 2.4f, false, [](float, Pose& g) {
      g[Scale] = 1.05f; g[EyeH] = 1.12f; g[EyeW] = 1.04f; g[LookY] = -4; g[MouthCat] = 0; g[MouthSmile] = 0.15f; g[MouthWidth] = 0.45f; g[Whisk] = -8; }, {
      var("끄덕", 0.6f, 0.8f, nod(1, 12)),
      var("응응", 1.0f, 1.3f, nod(2, 9)),
      var("갸웃하며 듣기", 1.8f, 2.6f, [](float u, P p, Pose& d) { d[Tilt] += p.s * 0.06f * envelope(u); }),
      var("집중", 1.4f, 2.0f, [](float u, P, Pose& d) { d[EyeH] += 0.08f * envelope(u); d[Scale] += 0.03f * envelope(u); }),
      var("귀 기울이기", 1.4f, 2.0f, [](float u, P p, Pose& d) {
        d[LookX] += p.s * 16 * envelope(u); d[Tilt] += p.s * 0.04f * envelope(u); d[Whisk] -= 6 * envelope(u); }),
    }, "듣고 있어요"}},
    // Thinking looks up and away, as people do; the side is picked at random on entry.
    {"processing", {0.5f, 2.0f, false, [](float s, Pose& g) {
      g[LookX] = 84 * s; g[LookY] = -58; g[EyeH] = 0.9f; g[EyeW] = 0.96f; g[MouthCat] = 0; g[MouthSmile] = -0.15f;
      g[MouthWidth] = 0.5f; g[MouthX] = 16 * s; g[Tilt] = 0.03f * s; }, {
      var("휙휙", 0.5f, 0.8f, [](float u, P p, Pose& d) { d[LookX] -= p.side * 40 * envelope(u, 0.08f, 0.6f); }),
      var("반대편 보기", 1.8f, 2.6f, [](float u, P p, Pose& d) {
        const float e = envelope(u, 0.15f, 0.8f); d[LookX] -= p.side * 168 * e; d[MouthX] -= p.side * 32 * e; d[Tilt] -= p.side * 0.06f * e; }),
      var("음…", 1.3f, 1.8f, [](float u, P, Pose& d) { d[MouthX] += 8 * std::sin(u * kPi * 2); d[MouthWave] += 0.7f * envelope(u); }),
      var("골똘", 1.6f, 2.4f, [](float u, P, Pose& d) { d[LidTop] += 0.24f * envelope(u); d[EyeW] -= 0.05f * envelope(u); d[Scale] -= 0.02f * envelope(u); }),
      var("갸우뚱 생각", 1.8f, 2.4f, [](float u, P p, Pose& d) { d[Tilt] += p.side * 0.06f * envelope(u); }),
      var("아래 훑기", 1.2f, 1.6f, [](float u, P, Pose& d) { d[LookY] += 80 * envelope(u); }, 0.6f),
    }, "생각하고 있어요"}},
    {"speaking", {0.8f, 2.4f, true, [](float, Pose& g) { g[MouthCat] = 0; g[MouthSmile] = 0.25f; g[MouthWidth] = 0.8f; }, {
      var("강조", 0.45f, 0.6f, [](float u, P, Pose& d) { d[Scale] += 0.04f * bump(u); d[EyeH] += 0.06f * bump(u); }),
      var("말하며 시선 돌림", 0.9f, 1.4f, [](float u, P p, Pose& d) { d[LookX] += p.s * 30 * envelope(u); d[LookY] -= 8 * envelope(u); }),
      var("눈 크게", 0.8f, 1.1f, [](float u, P, Pose& d) { d[EyeH] += 0.1f * envelope(u); }),
      var("끄덕이며 말하기", 0.6f, 0.8f, nod(1, 9)),
      var("갸웃하며 말하기", 1.2f, 1.8f, [](float u, P p, Pose& d) { d[Tilt] += p.s * 0.05f * envelope(u); }),
    }, ""}},
    {"awaiting_answer", {1.0f, 3.0f, false, [](float s, Pose& g) {
      g[Tilt] = 0.13f * s; g[EyeH] = 1.06f; g[MouthCat] = 0; g[MouthSmile] = 0.55f; g[MouthWidth] = 0.55f; g[LookY] = -4; }, {
      var("반대로 갸웃", 2.2f, 3.0f, [](float u, P p, Pose& d) { d[Tilt] -= 2 * p.side * 0.13f * envelope(u, 0.25f, 0.75f); }),
      var("빤히", 1.4f, 2.0f, [](float u, P, Pose& d) { d[Scale] += 0.045f * envelope(u); d[EyeH] += 0.05f * envelope(u); }),
      var("깜박깜박", 0.6f, 0.8f, [](float, P, Pose&) {}, 1, 2),
      var("통", 0.45f, 0.6f, [](float u, P, Pose& d) { d[Bob] -= 10 * bump(u); }),
      var("기대", 1.4f, 2.0f, [](float u, P, Pose& d) { d[MouthSmile] += 0.3f * envelope(u); d[LidHappy] += 0.3f * envelope(u); }),
    }, "대답을 기다려요"}},
  };
  const auto it = table.find(name);
  return it == table.end() ? table.at("idle") : it->second;
}

const Mood* moodFor(const std::string& name) {
  using P = const Pick&;
  static const std::map<std::string, Mood> table{
    {"happy", {{{LidHappy, 0.85f}, {MouthSmile, 0.9f}, {MouthCat, 0}, {MouthWidth, 0.95f}}, {{MouthOpen, 0.12f}}, {
      var("통통", 0.8f, 1.0f, [](float u, P, Pose& d) { d[Bob] -= 10 * std::abs(std::sin(u * kPi * 2)); }),
      var("좌우 흔들", 1.2f, 1.6f, [](float u, P, Pose& d) { d[Tilt] += 0.07f * std::sin(u * kPi * 4) * envelope(u, 0.1f, 0.9f); }),
      var("방긋", 0.9f, 1.2f, [](float u, P, Pose& d) { d[MouthOpen] += 0.3f * envelope(u); d[Scale] += 0.03f * envelope(u); }),
    }}},
    {"sad", {{{LidSad, 0.8f}, {MouthSmile, -0.75f}, {MouthCat, 0}, {MouthWidth, 0.7f}}, {{Bob, 14}, {LookY, 10}, {EyeH, -0.08f}, {Whisk, 10}}, {
      var("한숨", 2.0f, 2.6f, [](float u, P, Pose& d) { d[LidTop] += 0.35f * envelope(u, 0.3f, 0.6f); d[Bob] += 6 * envelope(u, 0.3f, 0.6f); }),
      var("힐끔", 1.2f, 1.6f, [](float u, P p, Pose& d) { d[LookY] -= 16 * envelope(u); d[LookX] += p.s * 18 * envelope(u); }),
      var("울먹", 1.4f, 1.8f, [](float u, P, Pose& d) { d[MouthWave] += 0.5f * envelope(u); }),
    }}},
    {"unknown", {{{LidTopR, 0.3f}, {MouthWave, 0.8f}, {MouthCat, 0}, {MouthWidth, 0.8f}, {MouthSmile, 0}}, {{LookX, 30}, {LookY, -12}}, {
      var("어리둥절", 1.8f, 2.4f, [](float u, P, Pose& d) { d[LookX] -= 64 * envelope(u); }),
      var("갸웃", 1.6f, 2.2f, [](float u, P p, Pose& d) { d[Tilt] += p.s * 0.08f * envelope(u); }),
    }}},
    {"petted", {{{LidHappy, 1}, {Blush, 1}, {MouthCat, 1}, {MouthSmile, 0.4f}}, {{Whisk, 6}}, {
      var("비비기", 1.2f, 1.6f, [](float u, P, Pose& d) { d[Tilt] += 0.08f * std::sin(u * kPi * 4) * envelope(u, 0.1f, 0.9f); }),
      var("수염 씰룩", 0.6f, 0.9f, [](float u, P, Pose& d) { d[Whisk] += 7 * std::sin(u * kPi * 8); }),
      var("좋아", 1.0f, 1.4f, [](float u, P, Pose& d) { d[Scale] += 0.035f * envelope(u); d[Bob] -= 6 * envelope(u); }),
    }}},
    {"sleepy", {{{LidTop, 0.68f}, {MouthCat, 0}, {MouthSmile, 0.1f}, {MouthWidth, 0.4f}, {MouthOpen, 0.05f}}, {{Bob, 10}, {Whisk, 8}}, {
      var("꾸벅", 2.6f, 3.4f, [](float u, P, Pose& d) { d[LidTop] += 0.32f * envelope(u, 0.55f, 0.85f); d[Bob] += 22 * envelope(u, 0.6f, 0.85f); }),
      var("하품", 2.0f, 2.6f, [](float u, P, Pose& d) {
        d[MouthOpen] += 0.7f * envelope(u, 0.3f, 0.6f); d[LidTop] += 0.2f * envelope(u, 0.3f, 0.6f); d[Scale] += 0.03f * envelope(u); }),
      var("Z", 2.4f, 3.0f, [](float u, P, Pose& d) { d[Zz] += envelope(u, 0.15f, 0.7f); }, 1, 0, true),
    }}},
    {"surprised", {{{EyeW, 1.18f}, {EyeH, 0.92f}, {MouthCat, 0}, {MouthOpen, 0.65f}, {MouthWidth, 0.85f}}, {{Scale, 0.05f}, {Whisk, -10}}, {
      var("깜짝 깜박", 0.6f, 0.8f, [](float, P, Pose&) {}, 1, 2),
      var("부르르", 0.5f, 0.6f, [](float u, P, Pose& d) { d[LookX] += 5 * std::sin(u * kPi * 12) * envelope(u, 0.1f, 0.9f); }),
    }}},
  };
  const auto it = table.find(name);
  return it == table.end() ? nullptr : &it->second;
}

// Picks variations, blinks, syllables and micro-saccades, and springs the pose toward its target.
class Director {
public:
  Director() { set("idle", "neutral", ""); }
  void set(const std::string& activity, const std::string& emotion, const std::string& status) {
    mood_ = emotion == "sad" || status == "failed" ? "sad" : status == "unknown" ? "unknown"
          : emotion == "happy" || status == "succeeded" ? "happy" : emotion;
    activity_ = &activityFor(activity); moodTable_ = moodFor(mood_);
    side_ = sign(); current_.reset(); last_ = nullptr;
    nextVariation_ = t_ + range(0.6f, 1.4f);
    blink(0);  // a state change starts with a blink, the way animators hide a cut
    nextBlink_ = t_ + range(2, 5);
    talk_ = 0; nextTalk_ = 0;
    label_ = status == "unknown" ? "결과를 확인해야 해요" : activity_->label;
    target_ = mood_ == "sad" ? kPink : mood_ == "unknown" ? kYellow : kBlue;
  }
  void update(float dt) {
    t_ += dt;
    if (current_ && t_ > current_->start + current_->duration) { current_.reset(); nextVariation_ = t_ + range(activity_->gap0, activity_->gap1); }
    if (!current_ && t_ >= nextVariation_) start();
    Pose g = kRest;
    activity_->base(side_, g);
    if (moodTable_) { for (const auto& [k, v] : moodTable_->set) g[k] = v; for (const auto& [k, v] : moodTable_->add) g[k] += v; }
    if (activity_->talk) {
      if (t_ >= nextTalk_) {
        talk_ = random() < 0.16f ? 0 : range(0.25f, 1);
        nextTalk_ = t_ + (talk_ > 0 ? range(0.08f, 0.16f) : range(0.18f, 0.35f));
      }
      g[MouthOpen] = talk_ * (mood_ == "sad" ? 0.7f : 1);
    }
    // Living eyes never hold perfectly still.
    if (t_ >= nextMicro_) {
      const float amp = mood_ == "sleepy" ? 1.5f : 5;
      micro_[0] = range(-amp, amp); micro_[1] = range(-amp * 0.6f, amp * 0.6f); nextMicro_ = t_ + range(0.35f, 1.6f);
    }
    g[LookX] += micro_[0]; g[LookY] += micro_[1];
    if (current_) current_->variation->apply((t_ - current_->start) / current_->duration, current_->pick, g);
    const float h = std::min(dt, 1.0f / 20) / 2;
    for (int k = 0; k < kKeys; ++k) {
      if (snap_) { pose_[k] = g[k]; velocity_[k] = 0; continue; }
      const float K = stiffness(k), C = 2 * std::sqrt(K) * 0.78f;
      for (int i = 0; i < 2; ++i) { velocity_[k] += (K * (g[k] - pose_[k]) - C * velocity_[k]) * h; pose_[k] += velocity_[k] * h; }
    }
    const float follow = snap_ ? 1 : std::min(1.0f, dt * 6);
    color_[0] += (target_.r - color_[0]) * follow; color_[1] += (target_.g - color_[1]) * follow; color_[2] += (target_.b - color_[2]) * follow;
    snap_ = false;
    if (t_ >= nextBlink_) { blink(0); if (random() < 0.22f) blink(0.28f); nextBlink_ = t_ + range(2.2f, 6); }
    blinks_.erase(std::remove_if(blinks_.begin(), blinks_.end(), [&](const auto& b) { return t_ > b.first + b.second + 0.05f; }), blinks_.end());
    closure_ = 0;
    for (const auto& [s, d] : blinks_) if (t_ >= s && t_ <= s + d) closure_ = std::max(closure_, std::sin(kPi * (t_ - s) / d));
    zzPhase_ = current_ && current_->variation->track ? (t_ - current_->start) / current_->duration : 0;
  }
  const Pose& pose() const { return pose_; }
  float blinkClosure() const { return closure_; }
  float zzPhase() const { return zzPhase_; }
  float time() const { return t_; }
  Rgb color() const { return {uint8_t(std::lround(color_[0])), uint8_t(std::lround(color_[1])), uint8_t(std::lround(color_[2]))}; }
  const std::string& label() const { return label_; }
private:
  struct Running { const Variation* variation; float start, duration; Pick pick; };
  float random() { return std::uniform_real_distribution<float>(0, 1)(rng_); }
  float range(float a, float b) { return a + random() * (b - a); }
  float sign() { return random() < 0.5f ? -1.0f : 1.0f; }
  void blink(float delay) { blinks_.push_back({t_ + delay, mood_ == "sleepy" ? 0.32f : 0.17f}); }
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
  }
  std::mt19937 rng_{std::random_device{}()};
  float t_ = 0, side_ = 1, nextVariation_ = 0, nextBlink_ = 0, talk_ = 0, nextTalk_ = 0, nextMicro_ = 0, closure_ = 0, zzPhase_ = 0;
  float micro_[2] = {0, 0}, color_[3] = {kBlue.r, kBlue.g, kBlue.b};
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
  for (const float side : {-1.0f, 1.0f}) {
    const float cx = (side < 0 ? 348.0f : 676.0f) + P[LookX], cy = 240 + P[LookY];
    const float hw = 64 * std::max(0.5f, P[EyeW]), hh = std::max(4.0f, 90 * std::max(0.4f, P[EyeH]) * (1 - 0.94f * blink));
    const float r = std::min(hw, hh);
    const float lid = std::clamp(P[LidTop] + (side > 0 ? P[LidTopR] : 0), 0.0f, 0.92f);
    const float sad = std::clamp(P[LidSad], 0.0f, 1.0f), happy = std::clamp(P[LidHappy], 0.0f, 1.1f);
    // Upper lid: a half-plane, slanted down toward the outer corner when sad.
    const float yl = cy - hh + 2 * hh * lid + sad * 0.45f * hh, m = sad * 0.8f * hh * side / hw, mn = std::hypot(1.0f, m);
    // Lower lid: a large circle pushing up from below makes a smiling eye.
    const float R = hw * 1.3f, cyc = cy + hh + R - happy * 1.3f * hh;
    L.push_back({col, 1, cx - hw, cy - hh, cx + hw, cy + hh, [=](float x, float y) {
      float d = sdBox(x, y, cx, cy, hw, hh, r);
      if (lid > 0.001f || sad > 0.001f) d = std::max(d, (yl + m * (x - cx) - y) / mn);
      if (happy > 0.001f) d = std::max(d, -sdCircle(x, y, cx, cyc, R));
      return d;
    }, [=](float, float y) { return 1 - 0.28f * smooth((y - (cy - hh * 0.3f)) / (hh * 1.3f)); }});  // soft shade reads as volume
  }

  // The mouth follows only part of the gaze, which reads as a head turn.
  if (mouthMode == "always" || (mouthMode == "speaking" && speaking)) {
    const float mx = 512 + P[LookX] * 0.35f + P[MouthX], my = 392 + P[LookY] * 0.3f, mw = std::max(0.3f, P[MouthWidth]);
    const float open = std::clamp(P[MouthOpen], 0.0f, 1.0f), lip = 1 - smooth((open - 0.1f) / 0.14f), cat = std::clamp(P[MouthCat], 0.0f, 1.0f);
    if (lip > 0.02f) {
      if (cat > 0.02f) for (const float s : {-1.0f, 1.0f}) {
        const float ar = 27 * mw, ax = mx + s * ar;
        L.push_back({col, cat * lip, ax - ar - 6, my - 10, ax + ar + 6, my + ar + 6,
                     [=](float x, float y) { return sdArc(x, y, ax, my - 6, ar, kDown0, kDown1, 5); }, nullptr});
      }
      if (cat < 0.98f) {
        float px = 0, py = 0;
        for (int i = 0; i <= 10; ++i) {
          const float s = float(i) / 5 - 1, x = mx + s * 52 * mw;
          const float y = my + P[MouthSmile] * 14 * (1 - s * s) - P[MouthSmile] * 7 + P[MouthWave] * 5 * std::sin(s * kPi * 2.5f + t * 5);
          if (i > 0) L.push_back({col, (1 - cat) * lip, std::min(px, x) - 7, std::min(py, y) - 7, std::max(px, x) + 7, std::max(py, y) + 7,
                                  [=](float qx, float qy) { return sdTaper(qx, qy, px, py, x, y, 5, 5); }, nullptr});
          px = x; py = y;
        }
      }
    }
    if (open > 0.03f) {
      const float oh = 6 + open * 24, ow = std::max(16.0f, 40 * mw - open * 8), oy = my - 6 + oh;
      L.push_back({col, smooth(open * 6), mx - ow, oy - oh, mx + ow, oy + oh,
                   [=](float x, float y) { return sdBox(x, y, mx, oy, ow, oh, std::min(ow, oh)); }, nullptr});
    }
  }

  // Whiskers: gently bowed strands that thin toward the tip and sway slowly.
  if (whiskers) for (const float side : {-1.0f, 1.0f}) for (const float k : {-1.0f, 0.0f, 1.0f}) {
    const float ix = 512 + side * 222 + P[LookX] * 0.25f, iy = 392 + k * 18 + P[Whisk] * 0.25f + P[LookY] * 0.2f;
    const float bx = 512 + side * 404 + P[LookX] * 0.25f, by = iy + k * 26 + P[Whisk] + std::sin(t * 1.1f + k * 0.9f + side) * 3;
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
  if (P[Blush] > 0.02f) for (const float x : {318.0f, 706.0f})
    L.push_back({kPink, 0.32f * P[Blush], x - 30, 308, x + 30, 368, [=](float px, float py) { return sdCircle(px, py, x, 338, 28); }, nullptr});
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
      director.update(std::chrono::duration<float>(now - frameAt).count());
      frameAt = now;
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
    d.window = SDL_CreateWindow(c.mock ? "DeskPet · 모의 실행" : "DeskPet", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      c.fullscreen ? pw : pw / 2, c.fullscreen ? ph : ph / 2, SDL_WINDOW_ALLOW_HIGHDPI | (c.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE));
    if (!d.window) throw Error("DISPLAY_ERROR", SDL_GetError());
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
