// Copyright 2011 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef VOICECARD_ENVELOPE_H_
#define VOICECARD_ENVELOPE_H_

#include "avrlib/base.h"
#include "avrlib/op.h"

#include "common/patch.h"

#include "voicecard/voicecard.h"
#include "voicecard/resources.h"

using namespace avrlib;

namespace ambika {



class Envelope {
 public:
  enum Stage : uint8_t {
    ATTACK = 0,
    DECAY = 1,
    SUSTAIN = 2,
    RELEASE = 3,
    DEAD = 4,
    NUM_STAGES,
  };
  Envelope() = default;

  void Init() {
    stage_target_[ATTACK] = 255;
    stage_target_[RELEASE] = 0;
    stage_target_[DEAD] = 0;
    stage_phase_increment_[SUSTAIN] = 0;
    stage_phase_increment_[DEAD] = 0;
  }

  uint8_t stage() { return stage_; }

  void Trigger(Stage s) {
    if (s == DEAD) {
      value_ = 0;
    }
    a_ = highByte(value_);
    b_ = stage_target_[s];
    stage_ = s;
    phase_ = 0;
    phase_increment_ = stage_phase_increment_[s];
  }
  
  inline void Update(uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release) {
    using rm = ResourcesManager;
    auto portamento_incr = lut_res_env_portamento_increments;
    stage_phase_increment_[ATTACK] = rm::Lookup<uint16_t, uint8_t>(portamento_incr, attack);
    stage_phase_increment_[DECAY] = rm::Lookup<uint16_t, uint8_t>(portamento_incr, decay);
    stage_phase_increment_[RELEASE] = rm::Lookup<uint16_t, uint8_t>(portamento_incr, release);
    stage_target_[DECAY] = sustain * 2;
    stage_target_[SUSTAIN] = stage_target_[DECAY];
  }

  uint8_t Render() {
    phase_ += phase_increment_;
    if (phase_ < phase_increment_) {
      value_ = U8MixU16(a_, b_, 255);
      stage_ = Stage(stage_ + 1);
      Trigger(stage_);
    }
    if (phase_increment_) {
      uint8_t step = InterpolateSample(wav_res_env_expo, phase_);
      value_ = U8MixU16(a_, b_, step);
    }
    return highByte(value_);
  }

 private:
  // Phase increments for each stage.
  uint16_t stage_phase_increment_[NUM_STAGES];
  // Value that needs to be reached at the end of each stage.
  uint8_t stage_target_[NUM_STAGES];
  // Current stage.
  Stage stage_;

  // Start and end value of the current segment.
  uint8_t a_;
  uint8_t b_;

  // Phase and phase increment.
  uint16_t phase_increment_;
  uint16_t phase_;

  // Current value of the envelope.
  uint16_t value_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace ambika

#endif  // VOICECARD_ENVELOPE_H_
