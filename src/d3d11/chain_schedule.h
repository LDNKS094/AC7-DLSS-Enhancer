#pragma once
#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace ac7 {
// Policy only: no GPU ownership, waits or diagnostics. Retire in-flight GPU work before
// ending a dropped frame; both model histories reset after any gap or failed stage.
class ChainSchedule {
public:
    using Clock = std::chrono::steady_clock;
    enum class Stage { idle, prepare, nr, restore, sr };
    bool begin(uint64_t frame, Clock::time_point deadline, Clock::time_point now = Clock::now()) {
        if (seen_ && frame <= lastSeen_) throw std::runtime_error("Non-monotonic chain frame");
        if (seen_ && frame - lastSeen_ != 1) resetNext_ = true;
        seen_ = true; lastSeen_ = frame;
        if (stage_ != Stage::idle) { invalid_ = resetNext_ = true; return false; }
        frame_ = frame; deadline_ = deadline; reset_ = resetNext_; resetNext_ = true;
        invalid_ = now >= deadline; stage_ = invalid_ ? Stage::idle : Stage::prepare;
        return !invalid_;
    }
    bool ready(Stage expected, Clock::time_point now = Clock::now()) {
        require(expected);
        invalid_ = invalid_ || now >= deadline_;
        return !invalid_;
    }
    void nrSubmitted() { require(Stage::prepare); stage_ = Stage::nr; }
    bool nrRetired(bool published, Clock::time_point now = Clock::now()) {
        const bool usable = ready(Stage::nr, now) && published;
        stage_ = usable ? Stage::restore : Stage::idle;
        return usable;
    }
    void srSubmitted() { require(Stage::restore); stage_ = Stage::sr; }
    bool srRetired(bool published, Clock::time_point now = Clock::now()) {
        const bool usable = ready(Stage::sr, now) && published;
        stage_ = Stage::idle; resetNext_ = !usable;
        return usable;
    }
    void drop() {
        if (stage_ == Stage::nr || stage_ == Stage::sr) throw std::runtime_error("Retire pending model before dropping chain");
        stage_ = Stage::idle; resetNext_ = true;
    }
    Stage stage() const { return stage_; }
    bool reset() const { return reset_; }
    bool busy() const { return stage_ != Stage::idle; }
    uint64_t frame() const { return frame_; }
    Clock::time_point deadline() const { return deadline_; }
private:
    void require(Stage expected) const {
        if (stage_ != expected) throw std::runtime_error("Invalid chain stage");
    }
    Stage stage_ = Stage::idle;
    Clock::time_point deadline_{};
    uint64_t frame_ = 0, lastSeen_ = 0;
    bool seen_ = false, reset_ = true, resetNext_ = true, invalid_ = false;
};
}
