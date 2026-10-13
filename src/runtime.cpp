#include "deskpet/runtime.hpp"
#include "deskpet/backends.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace deskpet {
struct Runtime::Impl {
  Config config; Emit emit; Display display;
  std::mutex mutex; std::condition_variable cv;
  std::atomic_bool quitting{false}, captureFault{false}, playbackFault{false}, motionFault{false};
  std::atomic_uint64_t motionGeneration{0};
  std::atomic_bool stopFault{false};
  struct Capture {
    Packet start; std::optional<Packet> stop; std::atomic_bool cancel{false};
  };
  struct Playback {
    Packet begin; Format format; std::deque<Bytes> chunks; size_t queuedBytes = 0;
    std::optional<Packet> finish, stop; std::atomic_bool cancel{false};
  };
  std::shared_ptr<Capture> capture;
  std::shared_ptr<Playback> playback;
  std::deque<std::pair<Packet, uint64_t>> motionCommands;
  bool motionAvailable = false, motionEnabled = false, calibration = false;
  std::string motionError = "initializing";
  int leftUs = 1500, rightUs = 1500;
  std::thread inputThread, outputThread, motionThread;
  Impl(const Config& c, Emit e) : config(c), emit(std::move(e)), display(c) {
    inputThread = std::thread([this]{ captureLoop(); });
    outputThread = std::thread([this]{ playbackLoop(); });
    motionThread = std::thread([this]{ servoLoop(); });
  }
  ~Impl() {
    quitting = true; disconnect(); cv.notify_all();
    inputThread.join(); outputThread.join(); motionThread.join();
  }
  void disconnect() {
    { std::lock_guard lock(mutex);
      if (capture) capture->cancel = true;
      if (playback) playback->cancel = true;
      ++motionGeneration; motionCommands.clear();
    }
    cv.notify_all(); display.reset();
  }
  void captureLoop() {
    for (;;) {
      std::shared_ptr<Capture> job;
      { std::unique_lock lock(mutex); cv.wait(lock, [&]{return quitting || capture;});
        if (quitting) return;
        job = capture; }
      bool acknowledged = false;
      std::string reason = "stopped";
      try {
        auto input = makeInput(config); checkCancelled(job->cancel);
        emit(reply(job->start, {{"format", Format{}.json()}, {"frameMs", 20}, {"simulated", config.mock}})); acknowledged = true;
        uint64_t sequence = 0;
        while (!quitting && !job->cancel) {
          if (captureFault.exchange(false)) throw Error("AUDIO_DEVICE_ERROR", "Injected microphone disconnection");
          // Use the actual negotiated buffer size for a conservative earliest
          // capture bound, not the later timestamp at socket emission.
          auto earliest = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count() - input->bufferNs();
          auto bytes = input->read(job->cancel);
          if (bytes.empty()) { reason = "eof"; break; }
          auto p = event(job->start, "audio.frame", {{"sequence", sequence++}, {"captureEarliestMonotonicNs", earliest}, {"format", Format{}.json()}, {"simulated", config.mock}}, std::move(bytes));
          if (!emit(std::move(p))) throw Error("AUDIO_DISCONTINUITY", "Capture queue overflow; restart the stream and invalidate its STT session");
        }
      } catch (const Error& e) {
        reason = e.code;
        if (!acknowledged) emit(failure(job->start, e.code, e.what()));
        if (e.code != "CANCELLED") emit(event(job->start, "device.error", {{"device", "capture"}, {"code", e.code}, {"message", e.what()}}));
      } catch (const std::exception& e) {
        reason = "AUDIO_DEVICE_ERROR";
        if (!acknowledged) emit(failure(job->start, reason, e.what()));
        emit(event(job->start, "device.error", {{"device", "capture"}, {"code", reason}, {"message", e.what()}}));
      }
      std::optional<Packet> stop;
      { std::lock_guard lock(mutex); stop = job->stop; if (capture == job) capture.reset(); }
      emit(event(job->start, "capture.stopped", {{"reason", reason}}));
      if (stop) emit(reply(*stop));
    }
  }
  void playbackLoop() {
    for (;;) {
      std::shared_ptr<Playback> job;
      { std::unique_lock lock(mutex); cv.wait(lock, [&]{return quitting || playback;});
        if (quitting) return;
        job = playback; }
      bool acknowledged = false, wrote = false, started = false, completed = false;
      std::unique_ptr<AudioOutput> output;
      std::string code, message;
      try {
        output = makeOutput(config, job->format); checkCancelled(job->cancel);
        emit(reply(job->begin, {{"ready", true}, {"simulated", config.mock}})); acknowledged = true;
        for (;;) {
          Bytes bytes; bool finish = false;
          { std::unique_lock lock(mutex);
            cv.wait(lock, [&]{return quitting || job->cancel || playbackFault || !job->chunks.empty() || job->finish;});
            checkCancelled(job->cancel);
            if (quitting) throw Error("CANCELLED", "Shutting down");
            if (playbackFault.exchange(false)) throw Error("AUDIO_OUTPUT_ERROR", "Injected speaker failure");
            if (!job->chunks.empty()) { bytes = std::move(job->chunks.front()); job->chunks.pop_front(); job->queuedBytes -= bytes.size(); }
            else finish = job->finish.has_value();
          }
          if (!bytes.empty()) {
            output->write(bytes, job->cancel);
            wrote = true;
            if (!started && output->started()) { emit(event(job->begin, "audio.started", {{"simulated", config.mock}})); started = true; }
          }
          if (finish) {
            if (!wrote) throw Error("NO_AUDIO", "An empty audio job cannot be delivered");
            output->finishInput(job->cancel);
            if (!started) {
              checkCancelled(job->cancel); output->start();
              emit(event(job->begin, "audio.started", {{"simulated", config.mock}})); started = true;
            }
            output->drain(job->cancel); completed = true; break;
          }
        }
      } catch (const Error& e) { code = e.code; message = e.what(); }
      catch (const std::exception& e) { code = "AUDIO_OUTPUT_ERROR"; message = e.what(); }
      bool stoppedConfirmed = completed || !output || output->drop();
      if (stopFault.exchange(false)) stoppedConfirmed = false;
      std::optional<Packet> finish, stop;
      { std::lock_guard lock(mutex);
        // Cancellation wins if it arrived before finalization acquired the state lock.
        if (job->cancel) { completed = false; code = "CANCELLED"; message = "Playback cancelled"; }
        finish = job->finish; stop = job->stop; if (playback == job) playback.reset();
      }
      if (completed) {
        emit(event(job->begin, "audio.completed", {{"simulated", config.mock}}));
        if (finish) emit(reply(*finish, {{"completed", true}, {"simulated", config.mock}}));
      } else {
        if (!acknowledged) emit(failure(job->begin, code, message));
        if (finish) emit(failure(*finish, code, message));
        emit(event(job->begin, code == "CANCELLED" ? "audio.cancelled" : "audio.failed", {{"code", code}, {"message", message}, {"stoppedConfirmed", stoppedConfirmed}, {"simulated", config.mock}}));
      }
      if (stop) {
        if (stoppedConfirmed) emit(reply(*stop, {{"stopped", true}}));
        else emit(failure(*stop, "AUDIO_STOP_UNCONFIRMED", "ALSA drop failed; keep STT gated"));
      }
    }
  }
  void servoLoop() {
    std::unique_ptr<ServoBus> bus;
    try {
      bus = makeServo(config); bus->initialize(); bus->disable();
      std::lock_guard lock(mutex); motionAvailable = true; motionError.clear();
      leftUs = config.left.neutralUs; rightUs = config.right.neutralUs;
    } catch (const std::exception& e) {
      std::lock_guard lock(mutex); motionError = e.what(); bus.reset();
    }
    uint64_t observedGeneration = motionGeneration;
    auto disable = [&] {
      if (!bus) return;
      try { bus->disable(); std::lock_guard lock(mutex); motionEnabled = false; calibration = false; }
      catch (const std::exception& e) { std::lock_guard lock(mutex); motionAvailable = false; motionError = e.what(); }
    };
    for (;;) {
      std::optional<std::pair<Packet, uint64_t>> entry;
      { std::unique_lock lock(mutex);
        cv.wait_for(lock, std::chrono::milliseconds(20), [&]{return quitting || !motionCommands.empty() || motionGeneration != observedGeneration;});
        if (!motionCommands.empty()) { entry = std::move(motionCommands.front()); motionCommands.pop_front(); }
      }
      if (quitting) { disable(); return; }
      if (motionGeneration != observedGeneration) { disable(); observedGeneration = motionGeneration; }
      if (!entry) continue;
      auto [cmd, generation] = std::move(*entry);
      auto check = [&] {
        if (quitting || motionGeneration != generation) throw Error("CANCELLED", "Motion was cancelled");
        if (motionFault.exchange(false)) throw Error("SERVO_ERROR", "Injected servo I2C failure");
      };
      try {
        check();
        { std::lock_guard lock(mutex); if (!motionAvailable || !bus) throw Error("SERVO_ERROR", motionError); }
        const auto op = cmd.meta.at("command").get<std::string>();
        if (op == "motion.disable" || op == "motion.calibration.end") {
          bus->disable(); std::lock_guard lock(mutex); motionEnabled = false; calibration = false;
        } else if (op == "motion.enable" || op == "motion.calibration.begin") {
          if (op == "motion.enable" && !config.calibrated) throw Error("CALIBRATION_REQUIRED", "Calibrate both ears before enabling motion");
          bus->disable(); bus->pulses(config.left.neutralUs, config.right.neutralUs); check(); bus->enable();
          std::lock_guard lock(mutex); motionEnabled = true; calibration = op == "motion.calibration.begin";
          leftUs = config.left.neutralUs; rightUs = config.right.neutralUs;
        } else {
          bool calibrating;
          { std::lock_guard lock(mutex); if (!motionEnabled) throw Error("MOTION_DISABLED", "Enable motion first"); calibrating = calibration; }
          std::vector<std::pair<int, int>> poses;
          if (op == "motion.calibration.set") {
            if (!calibrating) throw Error("CALIBRATION_REQUIRED", "Start a calibration session first");
            int l = cmd.meta.at("leftUs").get<int>(), r = cmd.meta.at("rightUs").get<int>();
            (void)pwmTicks(l); (void)pwmTicks(r); poses.push_back({l, r});
          } else {
            if (calibrating) throw Error("CALIBRATION_REQUIRED", "End calibration before normal motion");
            const auto preset = cmd.meta.value("preset", "");
            std::vector<std::pair<double, double>> positions;
            if (preset == "neutral") positions = {{0, 0}};
            else if (preset == "perk") positions = {{1, 1}};
            else if (preset == "droop") positions = {{-1, -1}};
            else if (preset == "wiggle") positions = {{0.5, -0.5}, {-0.5, 0.5}, {0, 0}};
            else if (preset.empty()) positions = {{cmd.meta.at("left").get<double>(), cmd.meta.at("right").get<double>()}};
            else throw Error("INVALID_COMMAND", "Unknown motion preset");
            for (auto [l, r] : positions) poses.push_back({config.left.pulse(l), config.right.pulse(r)});
          }
          for (auto [l, r] : poses) {
            for (;;) {
              check(); int nextL, nextR;
              { std::lock_guard lock(mutex);
                nextL = leftUs + std::clamp(l - leftUs, -config.stepUs, config.stepUs);
                nextR = rightUs + std::clamp(r - rightUs, -config.stepUs, config.stepUs); }
              bus->pulses(nextL, nextR);
              { std::lock_guard lock(mutex); leftUs = nextL; rightUs = nextR; }
              if (nextL == l && nextR == r) break;
              std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
          }
        }
        Json state;
        { std::lock_guard lock(mutex); state = {{"enabled", motionEnabled}, {"leftUs", leftUs}, {"rightUs", rightUs}, {"positionVerified", false}, {"simulated", config.mock}}; }
        emit(event(cmd, "motion.commanded", state)); emit(reply(cmd, state));
      } catch (const Error& e) {
        // A driver failure stops motion; a rejected command does not alter the business result.
        if (e.code == "SERVO_ERROR" || e.code == "CANCELLED" || e.code == "SERVO_STOP_UNCONFIRMED") disable();
        emit(failure(cmd, e.code, e.what()));
        emit(event(cmd, "device.error", {{"device", "motion"}, {"code", e.code}, {"message", e.what()}}));
      } catch (const std::exception& e) { disable(); emit(failure(cmd, "INVALID_COMMAND", e.what())); }
    }
  }
  void command(Packet cmd) {
    try {
      (void)identifiers(cmd.meta);
      const auto op = requiredString(cmd.meta, "command");
      if (!cmd.data.empty() && op != "speaker.write") throw Error("INVALID_COMMAND", "Only speaker.write accepts a binary payload");
      if (op == "hello") {
        emit(reply(cmd, {{"protocolVersion", 1}, {"mode", config.mock ? "mock" : "real"}, {"leaseMs", config.leaseMs},
          {"encoding", "s16le"}, {"maxPayloadBytes", 65536}, {"captureFrameMs", 20}}));
      } else if (op == "ping") emit(reply(cmd));
      else if (op == "health") {
        Json j;
        { std::lock_guard lock(mutex); j = {{"captureActive", bool(capture)}, {"playbackActive", bool(playback)},
          {"motion", {{"available", motionAvailable}, {"enabled", motionEnabled}, {"error", motionError}, {"leftUs", leftUs}, {"rightUs", rightUs}, {"calibrated", config.calibrated}}}}; }
        j["display"] = display.health(); j["mode"] = config.mock ? "mock" : "real"; emit(reply(cmd, j));
      } else if (op == "display.show" || op == "display.idle") {
        if (op == "display.show") { (void)requiredString(cmd.meta, "requestId"); (void)requiredString(cmd.meta, "outputId"); }
        // Preserve the command envelope so validation failures retain commandId/session.
        display.submit(cmd, emit);
      } else if (op == "display.snapshot") {
        display.snapshot(cmd.meta.at("path").get<std::string>()); emit(reply(cmd));
      } else if (op == "capture.start") {
        (void)requiredString(cmd.meta, "captureId");
        std::lock_guard lock(mutex);
        if (capture) throw Error("BUSY", "A capture stream is already active");
        capture = std::make_shared<Capture>(); capture->start = cmd; cv.notify_all();
      } else if (op == "capture.stop") {
        std::lock_guard lock(mutex);
        if (!capture || cmd.meta.value("captureId", "") != capture->start.meta.at("captureId").get<std::string>()) throw Error("UNKNOWN_CAPTURE", "No matching capture stream");
        if (capture->stop) throw Error("BUSY", "Capture stop is already pending");
        capture->cancel = true; capture->stop = cmd; cv.notify_all();
      } else if (op == "speaker.begin") {
        (void)requiredString(cmd.meta, "jobId"); (void)requiredString(cmd.meta, "requestId"); (void)requiredString(cmd.meta, "outputId");
        auto format = Format::from(cmd.meta.at("format"));
        std::lock_guard lock(mutex);
        if (playback) throw Error("BUSY", "A speaker job is already active");
        playback = std::make_shared<Playback>(); playback->begin = cmd; playback->format = format; cv.notify_all();
      } else if (op == "speaker.write" || op == "speaker.finish" || op == "speaker.stop") {
        std::lock_guard lock(mutex);
        if (!playback || cmd.meta.value("jobId", "") != playback->begin.meta.at("jobId").get<std::string>()) throw Error("UNKNOWN_JOB", "No matching speaker job");
        if (playback->cancel) throw Error("CANCELLED", "Speaker job is stopping");
        if (op == "speaker.write") {
          if (playback->finish) throw Error("JOB_FINISHING", "No writes are accepted after finish");
          if (cmd.data.empty() || cmd.data.size() % playback->format.frameBytes()) throw Error("INVALID_PCM", "PCM payload is empty or not frame aligned");
          if (playback->queuedBytes + cmd.data.size() > playback->format.bytesPerSecond()) throw Error("QUEUE_FULL", "Speaker queue is limited to one second; retry this unaccepted chunk later");
          playback->queuedBytes += cmd.data.size(); playback->chunks.push_back(std::move(cmd.data)); emit(reply(cmd, {{"accepted", true}}));
        } else if (op == "speaker.finish") {
          if (playback->finish) throw Error("JOB_FINISHING", "Finish is already pending");
          playback->finish = cmd;
        } else { playback->cancel = true; playback->stop = cmd; }
        cv.notify_all();
      } else if (op == "motion.enable" || op == "motion.disable" || op == "motion.set" ||
                 op == "motion.calibration.begin" || op == "motion.calibration.set" || op == "motion.calibration.end") {
        std::lock_guard lock(mutex);
        if (op == "motion.disable" || op == "motion.calibration.end") {
          ++motionGeneration;
          for (auto& p : motionCommands) emit(failure(p.first, "CANCELLED", "Motion disabled"));
          motionCommands.clear();
        }
        if (motionCommands.size() >= 16) throw Error("QUEUE_FULL", "Motion queue is full");
        motionCommands.push_back({cmd, motionGeneration.load()}); cv.notify_all();
      } else if (op == "mock.fail") {
        if (!config.mock) throw Error("MOCK_ONLY", "Fault injection is disabled in real mode");
        const auto device = cmd.meta.at("device").get<std::string>();
        if (device == "capture") captureFault = true;
        else if (device == "speaker") playbackFault = true;
        else if (device == "speaker-stop") stopFault = true;
        else if (device == "motion") motionFault = true;
        else if (device == "display") display.injectFailure();
        else throw Error("INVALID_COMMAND", "Unknown device");
        cv.notify_all(); emit(reply(cmd));
      } else throw Error("UNKNOWN_COMMAND", "Unsupported command: " + op);
    } catch (const Error& e) { emit(failure(cmd, e.code, e.what())); }
    catch (const std::exception& e) { emit(failure(cmd, "INVALID_COMMAND", e.what())); }
  }
};
Runtime::Runtime(const Config& c, Emit e) : impl_(std::make_unique<Impl>(c, std::move(e))) {}
Runtime::~Runtime() = default;
void Runtime::command(Packet p) { impl_->command(std::move(p)); }
void Runtime::disconnect() { impl_->disconnect(); }
bool Runtime::tick() { return impl_->display.tick(impl_->emit); }
}
