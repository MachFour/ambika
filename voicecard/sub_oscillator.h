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
//
// -----------------------------------------------------------------------------

#ifndef VOICECARD_SUB_OSCILLATOR_H_
#define VOICECARD_SUB_OSCILLATOR_H_

#include "avrlib/op.h"

#include "common/patch.h"

using namespace avrlib;

namespace ambika {

class SubOscillator {
 public:
  SubOscillator() = default;

  static inline void set_increment(uint24_t increment) {
    phase_increment = increment;
  }

  static inline void Render(uint8_t shape, uint8_t* buffer, uint8_t amount) {
    uint24_t increment = phase_increment;
    if (shape >= 3) {
      increment >>= 1;
      shape -= 3;
    }
    uint8_t size = kAudioBlockSize;
    uint8_t pulse_width = shape == 0 ? 0x80 : 0x40;
    uint8_t sub_gain = amount;
    uint8_t mix_gain = ~sub_gain;
    while (size--) {
      phase += increment;
      uint8_t v;
      if (shape != 1) {
        v = highByte24(phase) < pulse_width ? 0 : 255;
      } else {
        uint8_t tri = highWord24(phase) >> 7u;
        v = highByte24(phase) & 0x80u ? tri : ~tri;
      }
      *buffer = U8Mix(*buffer, v, mix_gain, sub_gain);
      ++buffer;
    }
  }

 private:
  // Current phase of the oscillator.
  static uint24_t phase;
  static uint24_t phase_increment;

  DISALLOW_COPY_AND_ASSIGN(SubOscillator);
};

/* static */
uint24_t SubOscillator::phase;

/* static */
uint24_t SubOscillator::phase_increment;

}  // namespace ambika

#endif  // VOICECARD_SUB_OSCILLATOR_H_
