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

//#include "common/patch.h"

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
    stage_target[ATTACK] = 255;
    stage_target[RELEASE] = 0;
    stage_target[DEAD] = 0;
    stage_phase_increment[SUSTAIN] = 0;
    stage_phase_increment[DEAD] = 0;
  }

  uint8_t getStage() { return stage; }

  void Trigger(Stage s) {
    if (s == DEAD) {
      value = 0;
    }
    segment_start_value = highByte(value);
    segment_end_value = stage_target[s];
    stage = s;
    phase = 0;
    phase_increment = stage_phase_increment[s];
  }
  
  inline void Update(uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release) {
    using rm = ResourcesManager;
    auto portamento_incr = lut_res_env_portamento_increments;
    stage_phase_increment[ATTACK] = rm::Lookup<uint16_t, uint8_t>(portamento_incr, attack);
    stage_phase_increment[DECAY] = rm::Lookup<uint16_t, uint8_t>(portamento_incr, decay);
    stage_phase_increment[RELEASE] = rm::Lookup<uint16_t, uint8_t>(portamento_incr, release);
    stage_target[DECAY] = sustain * 2;
    stage_target[SUSTAIN] = stage_target[DECAY];
  }

  uint8_t Render() {
    phase += phase_increment;
    if (phase < phase_increment) {
      value = U8MixU16(segment_start_value, segment_end_value, 255);
      stage = Stage(stage + 1);
      Trigger(stage);
    }
    if (phase_increment) {
      uint8_t step = InterpolateSample(wav_res_env_expo, phase);
      value = U8MixU16(segment_start_value, segment_end_value, step);
    }
    return highByte(value);
  }

 private:
  // Phase increments for each stage.
  uint16_t stage_phase_increment[NUM_STAGES];
  // Value that needs to be reached at the end of each stage.
  uint8_t stage_target[NUM_STAGES];
  // Current stage.
  Stage stage;

  // Start and end value of the current segment.
  uint8_t segment_start_value;
  uint8_t segment_end_value;

  // Phase and phase increment.
  uint16_t phase_increment;
  uint16_t phase;

  // Current value of the envelope.
  uint16_t value;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace ambika

#endif  // VOICECARD_ENVELOPE_H_
