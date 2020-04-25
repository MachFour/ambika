// Copyright 2009 Emilie Gillet.
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
//
// -----------------------------------------------------------------------------
//
// LFO (cheap oscillator).
//
// Contrary to oscillators which are templatized "static singletons", to
// generate the fastest, most specialized code, LFOs are less
// performance-sensitive and are thus implemented as a traditional class.

#ifndef COMMON_LFO_H_
#define COMMON_LFO_H_

#include "avrlib/base.h"
#include "avrlib/op.h"
#include "avrlib/random.h"
#include "common/patch.h"

using avrlib::InterpolateSample;
using avrlib::Random;

namespace ambika {

extern const uint8_t wav_res_lfo_waveforms[] PROGMEM;

class Lfo {
 public:
  Lfo() = default;

  uint8_t Render(LfoWave shape) {
    phase += phase_increment;
    is_looped = phase < phase_increment;
    
    // Compute the LFO value.
    uint8_t value;
    switch (shape) {
      case LFO_WAVEFORM_RAMP:
        value = highByte(phase);
        break;
      case LFO_WAVEFORM_S_H:
        if (is_looped) {
          s_h_value = Random::GetByte();
        }
        value = s_h_value;
        break;
      case LFO_WAVEFORM_TRIANGLE:
        value = (phase & 0x8000u) ? phase >> 7u : byteInverse(phase >> 7u);
        break;
      case LFO_WAVEFORM_SQUARE:
        value = (phase & 0x8000u) ? 255 : 0;
        break;
      default:
        {
#ifndef DISABLE_WAVETABLE_LFOS
          uint8_t shape_offset = shape - LFO_WAVEFORM_WAVE_1;
          uint16_t offset = word(shape_offset, shape_offset);
          value = InterpolateSample(wav_res_lfo_waveforms + offset, phase);
#else
        new_value = 0;
#endif  // DISABLE_WAVETABLE_LFOS
        }
        break;
    }
    return value;
  }

  void set_phase(uint16_t new_phase) {
    is_looped = (new_phase <= phase);
    phase = new_phase;
  }
  

  void set_phase_increment(uint16_t new_phase_increment) {
    phase_increment = new_phase_increment;
  }
  
  inline uint8_t looped() const {
    return is_looped;
  }

 private:
  // Phase increment.
  uint16_t phase_increment;

  // Current phase of the lfo.
  uint16_t phase;
  uint8_t is_looped;

  // Current value of S&H.
  uint8_t s_h_value;
  //uint8_t step;
  
  DISALLOW_COPY_AND_ASSIGN(Lfo);
};

}  // namespace ambika

#endif  // COMMON_LFO_H_
