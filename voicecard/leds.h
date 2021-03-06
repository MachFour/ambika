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
//
// Declaration of the LEDs.

#ifndef VOICECARD_LEDS_H_
#define VOICECARD_LEDS_H_

#include "avrlib/avrlib.h"
#include "avrlib/bitops.h"
#include <avr/io.h>

namespace ambika {


/*
// specialised templates for the LEDs, using PORTC, PINC, DDRC
template<uint8_t bit, uint8_t bitFlag = staticBitFlag(bit)>
struct PortCPin {
  static inline void outputMode() {
    DDRC |= bitFlag;
  }
  static inline void inputMode() {
    DDRC &= byteInverse(bitFlag);
  }
  static inline void high() {
    PORTC |= bitFlag;
  }
  static inline void low() {
    PORTC &= byteInverse(bitFlag);
  }
  static inline void toggle() {
    PINC |= bitFlag;
  }
};
*/

using RxLed = avrlib::PortCPin<4>;
using NoteLed = avrlib::PortCPin<5>;

}  // namespace ambika

#endif  // VOICECARD_LEDS_H_
