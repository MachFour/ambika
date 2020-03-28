// Copyright 2012 Emilie Gillet.
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

#include "voicecard/oscillator.h"

#include "voicecard/voicecard.h"

namespace ambika {

#define _UPDATE_PHASE(sync_input, sync_output) \
  if (*(sync_input)) { \
    phase.integral = 0; \
    phase.fractional = 0; \
  } \
  (sync_input)++; \
  phase = U24AddC(phase, phase_increment_); \
  *(sync_output) = phase.carry; \
  (sync_output)++;

#define UPDATE_PHASE _UPDATE_PHASE(sync_input_, sync_output_)
// This variant has a larger register width, but yields faster code.
#define UPDATE_PHASE_WITH_TMP _UPDATE_PHASE(sync_input_tmp, sync_output_tmp)

#define SAMPLE_LOOP_PROLOGUE \
  uint24c_t phase {.carry = 0, .integral = phase_.integral, .fractional = phase_.fractional}; \
  uint8_t size = kAudioBlockSize; \

#define WHILE_LOOP_OPEN while (size--) {

#define BEGIN_SAMPLE_LOOP \
  SAMPLE_LOOP_PROLOGUE \
  WHILE_LOOP_OPEN \

#define BEGIN_SAMPLE_LOOP_WITH_TMP \
  SAMPLE_LOOP_PROLOGUE \
  uint8_t* sync_input_tmp = sync_input_; \
  uint8_t* sync_output_tmp = sync_output_; \
  WHILE_LOOP_OPEN \

#define END_SAMPLE_LOOP \
  } \
  phase_.integral = phase.integral; \
  phase_.fractional = phase.fractional;

// ------- Silence (useful when processing external signals) -----------------
void Oscillator::RenderSilence(uint8_t* buffer) {
  uint8_t size = kAudioBlockSize;
  while (size--) {
    *buffer++ = 128;
  }
}

// ------- Band-limited PWM --------------------------------------------------
void Oscillator::RenderBandlimitedPwm(uint8_t* buffer) {
  uint8_t balance_index = U8Swap4(note_ /* - 12 play safe with Aliasing */);
  uint8_t gain_2 = byteAnd(balance_index, 0xf0);
  uint8_t gain_1 = ~gain_2;

  uint8_t wave_index = byteAnd(balance_index, 0xf0);
  const uint8_t* wave_1 = waveform_table[WAV_RES_BANDLIMITED_SAW_1 + wave_index];
  wave_index = U8AddClip(wave_index, 1, kNumZonesHalfSampleRate);
  const uint8_t* wave_2 = waveform_table[WAV_RES_BANDLIMITED_SAW_1 + wave_index];

  uint16_t shift = (parameter_ + 128u) << 8u;
  // For higher pitched notes, simply use 128
  uint8_t scale = 192u - (parameter_ / 2);
  if (note_ > 52) {
    scale = U8Mix(scale, 102, U8(note_ - 52) * 4);
    scale = U8Mix(scale, 102, U8(note_ - 52) * 4);
  }
  phase_increment_ = U24ShiftLeft(phase_increment_);
  BEGIN_SAMPLE_LOOP
    phase = U24AddC(phase, phase_increment_);
    *sync_output_++ = phase.carry;
    *sync_output_++ = 0;
    if (sync_input_[0] || sync_input_[1]) {
      phase.integral = 0;
      phase.fractional = 0;
    }
    sync_input_ += 2;
    
    uint8_t a = InterpolateTwoTables(wave_1, wave_2, phase.integral, gain_1, gain_2);
    uint8_t b = InterpolateTwoTables(wave_1, wave_2, phase.integral + shift, gain_1, gain_2);
    a = U8U8MulShift8(a, scale);
    b = U8U8MulShift8(b, scale);
    a = a - b + 128;
    *buffer++ = a;
    *buffer++ = a;
    size--;
  END_SAMPLE_LOOP
}

// ------- Interpolation between two waveforms from two wavetables -----------
// The position is determined by the note pitch, to prevent aliasing.

void Oscillator::RenderSimpleWavetable(uint8_t* buffer) {
  uint8_t balance_index = U8Swap4(note_);
  uint8_t gain_2 = byteAnd(balance_index, 0xf0);
  uint8_t gain_1 = ~gain_2;
  uint8_t wave_1_index, wave_2_index;
  if (shape_ != WAVEFORM_SINE) {
    uint8_t wave_index = byteAnd(balance_index, 0x0f);
    uint8_t base_resource_id = shape_ == WAVEFORM_SAW ?
        WAV_RES_BANDLIMITED_SAW_0 :
        (shape_ == WAVEFORM_SQUARE ? WAV_RES_BANDLIMITED_SQUARE_0  : 
        WAV_RES_BANDLIMITED_TRIANGLE_0);
    wave_1_index = base_resource_id + wave_index;
    wave_index = U8AddClip(wave_index, 1, kNumZonesFullSampleRate);
    wave_2_index = base_resource_id + wave_index;
  } else {
    wave_1_index = WAV_RES_SINE;
    wave_2_index = WAV_RES_SINE;
  }
  const uint8_t* wave_1 = waveform_table[wave_1_index];
  const uint8_t* wave_2 = waveform_table[wave_2_index];

  if (shape_ != WAVEFORM_TRIANGLE) {
    BEGIN_SAMPLE_LOOP_WITH_TMP
      UPDATE_PHASE_WITH_TMP
      uint8_t sample = InterpolateTwoTables(wave_1, wave_2, phase.integral, gain_1, gain_2);
      if (sample < parameter_) {
        sample += parameter_ / 2;
      }
      *buffer++ = sample;
    END_SAMPLE_LOOP
  } else {
    // The waveshaper for the triangle is different.
    BEGIN_SAMPLE_LOOP_WITH_TMP
      UPDATE_PHASE_WITH_TMP
      uint8_t sample = InterpolateTwoTables(wave_1, wave_2, phase.integral, gain_1, gain_2);
      if (sample < parameter_) {
        sample = parameter_;
      }
      *buffer++ = sample;
    END_SAMPLE_LOOP
  }
}

// ------- Casio CZ-like synthesis -------------------------------------------
void Oscillator::RenderCzSaw(uint8_t* buffer) {
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    uint8_t phi = highByte(phase.integral);
    uint8_t clipped_phi = phi < 0x20 ? phi << 3u : 0xff;
    // Interpolation causes more aliasing here.
    *buffer++ = ReadSample(wav_res_sine, U8MixU16(phi, clipped_phi, parameter_ * 2));
  END_SAMPLE_LOOP
}

void Oscillator::RenderCzResoSaw(uint8_t* buffer) {
  uint16_t increment = phase_increment_.integral + U16((phase_increment_.integral * U16(parameter_)) / 4);
  uint8_t type = shape_ - WAVEFORM_CZ_SAW_LP;
  uint16_t phase_2 = data_.secondary_phase;
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    if (phase.carry) {
      phase_2 = ResourcesManager::Lookup<uint16_t, uint8_t>(lut_res_cz_phase_reset, byteAnd(type, 0x03));
    }
    phase_2 += increment;
    uint8_t carrier = ReadSample(wav_res_sine, phase_2);
    uint8_t window = ~highByte(phase.integral);
    if (byteAnd(type, 2)) {
      *buffer++ = S8U8MulShift8(carrier + 128, window) + 128;
    } else {
      *buffer++ = U8U8MulShift8(carrier, window);
    }
  END_SAMPLE_LOOP
  data_.secondary_phase = phase_2;  
}

void Oscillator::RenderCzResoPulse(uint8_t* buffer) {
  uint16_t increment = phase_increment_.integral + U16((phase_increment_.integral * U16(parameter_)) / 4);
  uint8_t type = shape_ - WAVEFORM_CZ_SAW_LP;
  uint16_t phase_2 = data_.secondary_phase;
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    if (phase.carry) {
      phase_2 = ResourcesManager::Lookup<uint16_t, uint8_t>(lut_res_cz_phase_reset, byteAnd(type, 0x03));
    }
    phase_2 += increment;
    uint8_t carrier = ReadSample(wav_res_sine, phase_2);
    uint8_t window = 0;
    if (phase.integral < 0x4000) {
      window = 255;
    } else if (phase.integral < 0x8000) {
      window = U16(~(phase.integral - 0x4000u)) >> 6u;
    }
    if (type == 5) {
      carrier >>= 1u;
      carrier += 128;
    }
    if (byteAnd(type, 2)) {
      *buffer++ = S8U8MulShift8(carrier + 128, window) + 128;
    } else {
      *buffer++ = U8U8MulShift8(carrier, window);
    }
  END_SAMPLE_LOOP
  data_.secondary_phase = phase_2;  
}

void Oscillator::RenderCzResoTri(uint8_t* buffer) {
  uint16_t increment = phase_increment_.integral + ((phase_increment_.integral * U16(parameter_)) / 4);
  uint8_t type = shape_ - WAVEFORM_CZ_SAW_LP;
  uint16_t phase_2 = data_.secondary_phase;
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    if (phase.carry) {
      phase_2 = ResourcesManager::Lookup<uint16_t, uint8_t>(lut_res_cz_phase_reset, type & 0x03u);
    }
    phase_2 += increment;
    uint8_t carrier = ReadSample(wav_res_sine, phase_2);
    uint8_t window = phase.integral >> 7u;
    if (byteAnd(phase.integral, 0x8000)) {
      window = byteInverse(window);
    }
    if (byteAnd(type, 2)) {
      *buffer++ = S8U8MulShift8(carrier + 128, window) + 128;
    } else {
      *buffer++ = U8U8MulShift8(carrier, window);
    }
  END_SAMPLE_LOOP
  data_.secondary_phase = phase_2;  
}

// ------- FM ----------------------------------------------------------------
void Oscillator::RenderFm(uint8_t* buffer) {
  uint8_t offset = fm_parameter_;
  if (offset < 24) {
    offset = 0;
  } else if (offset > 48) {
    offset = 24;
  } else {
    offset = offset - 24;
  }
  auto multiplier = ResourcesManager::Lookup<uint16_t, uint8_t>(lut_res_fm_frequency_ratios, offset);
  uint16_t increment = U32(phase_increment_.integral * U16(multiplier)) >> 8u;
  parameter_ *= 2;
  
  uint16_t phase_2 = data_.secondary_phase;
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    phase_2 += increment;
    uint8_t modulator = InterpolateSample(wav_res_sine, phase_2);
    uint16_t modulation = modulator * parameter_;
    *buffer++ = InterpolateSample(wav_res_sine,
        phase.integral + modulation);
  END_SAMPLE_LOOP
  data_.secondary_phase = phase_2;
}

// ------- 8-bit land --------------------------------------------------------
void Oscillator::Render8BitLand(uint8_t* buffer) {
#ifdef ALTERNATIVE_CODE
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    uint8_t x = parameter_;
    *buffer++ = (U8(highByte(phase_.integral) ^ U8(x << 1u)) & byteInverse(x)) + (x >> 1u);
    // if x == 0: *buffer++ = highByte(phase_.integral);
  END_SAMPLE_LOOP
#else
  uint24c_t phase {0, phase_.integral, phase_.fractional};
  uint8_t size = kAudioBlockSize;
  while (size--) {
    if (*(sync_input_)++) {
      phase.integral = 0;
      phase.fractional = 0;
    }
    phase = U24AddC(phase, phase_increment_); \
    *(sync_output_)++ = phase.carry; \
    // x is 0
    *buffer++ = highByte(phase_.integral);
  }
  phase_.integral = phase.integral; \
  phase_.fractional = phase.fractional;
#endif
}

void Oscillator::RenderVowel(uint8_t* buffer) {
  using rs = ResourcesManager;
  data_.vw.update = U8(data_.vw.update + 1u) & 3u;
  if (!data_.vw.update) {
    uint8_t offset_1 = U8ShiftRight4(parameter_);
    offset_1 = U8U8Mul(offset_1, 7);
    uint8_t offset_2 = offset_1 + 7;
    uint8_t balance = byteAnd(parameter_, 15);
    
    // Interpolate formant frequencies.
    for (uint8_t i = 0; i < 3; ++i) {
      data_.vw.formant_increment[i] = U8U4MixU12(
          rs::Lookup<uint8_t, uint8_t>(wav_res_vowel_data, offset_1 + i),
          rs::Lookup<uint8_t, uint8_t>(wav_res_vowel_data, offset_2 + i), balance);
      data_.vw.formant_increment[i] <<= 3u;
    }
    
    // Interpolate formant amplitudes.
    for (uint8_t i = 0; i < 4; ++i) {
      auto amplitude_a = rs::Lookup<uint8_t, uint8_t>(wav_res_vowel_data, offset_1 + 3 + i);
      auto amplitude_b = rs::Lookup<uint8_t, uint8_t>(wav_res_vowel_data, offset_2 + 3 + i);
      data_.vw.formant_amplitude[i] = U8U4MixU8(amplitude_a, amplitude_b, balance);
    }
  }
  // formant_amplitude[3] is noise_modulation
  int16_t phase_noise = S8S8Mul(Random::state_msb(), data_.vw.formant_amplitude[3]);
  BEGIN_SAMPLE_LOOP
    int8_t result = 0;
    uint8_t phaselet;
    
    data_.vw.formant_phase[0] += data_.vw.formant_increment[0];
    phaselet = highByte(data_.vw.formant_phase[0]) & 0xf0u;
    result = rs::Lookup<uint8_t, uint8_t>(wav_res_formant_sine, phaselet | data_.vw.formant_amplitude[0]);

    data_.vw.formant_phase[1] += data_.vw.formant_increment[1];
    phaselet = highByte(data_.vw.formant_phase[1]) & 0xf0u;
    result += rs::Lookup<uint8_t, uint8_t>(wav_res_formant_sine, phaselet | data_.vw.formant_amplitude[1]);
    
    data_.vw.formant_phase[2] += data_.vw.formant_increment[2];
    phaselet = highByte(data_.vw.formant_phase[2]) & 0xf0u;
    result += rs::Lookup<uint8_t, uint8_t>(wav_res_formant_square, phaselet | data_.vw.formant_amplitude[2]);
    
    result = S8U8MulShift8(result, highByte(phase.integral));
    phase.integral -= phase_increment_.integral;
    if ((phase.integral + phase_noise) < phase_increment_.integral) {
      data_.vw.formant_phase[0] = 0;
      data_.vw.formant_phase[1] = 0;
      data_.vw.formant_phase[2] = 0;
    }
    uint8_t x = S16ClipS8(4 * result) + 128;
    *buffer++ = x;
    *buffer++ = x;
    size--;
  END_SAMPLE_LOOP
}

// ------- Dirty Pwm (kills kittens) -----------------------------------------
void Oscillator::RenderDirtyPwm(uint8_t* buffer) {
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    *buffer++ = highByte(phase.integral) < 127u + parameter_ ? 0 : 255;
  END_SAMPLE_LOOP
}

// ------- Quad saw (mit aliasing) -------------------------------------------
void Oscillator::RenderQuadSawPad(uint8_t* buffer) {
  uint16_t phase_spread = U32(phase_increment_.integral * U16(parameter_)) >> 13u;
  ++phase_spread;
  uint16_t phase_increment = phase_increment_.integral;
  uint16_t increments[3];
  for (uint8_t i = 0; i < 3; ++i) {
    phase_increment += phase_spread;
    increments[i] = phase_increment;
  }
  
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    data_.qs.phase[0] += increments[0];
    data_.qs.phase[1] += increments[1];
    data_.qs.phase[2] += increments[2];
    uint8_t value = (phase.integral >> 10u);
    value += (data_.qs.phase[0] >> 10u);
    value += (data_.qs.phase[1] >> 10u);
    value += (data_.qs.phase[2] >> 10u);
    *buffer++ = value;
  END_SAMPLE_LOOP
}

// ------- Low-passed, then high-passed white noise --------------------------
void Oscillator::RenderFilteredNoise(uint8_t* buffer) {
  uint16_t rng_state = data_.no.rng_state;
  if (rng_state == 0) {
    ++rng_state;
  }
  uint8_t filter_coefficient = parameter_ * 4 ;
  if (filter_coefficient <= 4) {
    filter_coefficient = 4;
  }
  BEGIN_SAMPLE_LOOP
    if (*sync_input_++) {
      rng_state = data_.no.rng_reset_value;
    }
    rng_state = U16(rng_state >> 1u) ^ (-(rng_state & 1u) & 0xb400u);
    uint8_t noise_sample = highByte(rng_state);
    // This trick is used to avoid having a DC component (no innovation) when
    // the parameter is set to its minimal or maximal value.
    data_.no.lp_noise_sample = U8Mix(data_.no.lp_noise_sample, noise_sample, filter_coefficient);
    if (parameter_ >= 64) {
      *buffer++ = noise_sample - data_.no.lp_noise_sample - 128;
    } else {
      *buffer++ = data_.no.lp_noise_sample;
    }
  END_SAMPLE_LOOP
  data_.no.rng_state = rng_state;
}

// The position is freely determined by the parameter
void Oscillator::RenderInterpolatedWavetable(uint8_t* buffer) {
  const uint8_t* which_wavetable = wav_res_wavetables + U8(shape_ - WAVEFORM_WAVETABLE_1)*U8(18);

  // Get a 8:8 value with the wave index in the first byte, and the
  // balance amount in the second byte.
  auto num_steps = ResourcesManager::Lookup<uint8_t, uint8_t>(which_wavetable, 0);
  uint16_t pointer = U8(parameter_ * 2) * num_steps;
  auto wave_index_1 = ResourcesManager::Lookup<uint8_t, uint8_t>(which_wavetable, 1 + highByte(pointer));
  auto wave_index_2 = ResourcesManager::Lookup<uint8_t, uint8_t>(which_wavetable, 2 + highByte(pointer));
  uint8_t gain = byteAnd(pointer, 0xff);
  const uint8_t* wave_1 = wav_res_waves + U8U8Mul(wave_index_1, 129);
  const uint8_t* wave_2 = wav_res_waves + U8U8Mul(wave_index_2, 129);
  BEGIN_SAMPLE_LOOP_WITH_TMP
    UPDATE_PHASE_WITH_TMP
    *buffer++ = InterpolateTwoTables(wave_1, wave_2, phase.integral / 2, ~gain, gain);
  END_SAMPLE_LOOP
}

// The position is freely determined by the parameter
void Oscillator::RenderWavequence(uint8_t* buffer) {
  const uint8_t* wave = wav_res_waves + U8U8Mul(parameter_, 129);
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    *buffer++ = InterpolateSample(wave, phase.integral / 2);
  END_SAMPLE_LOOP
}

/* static */
const Oscillator::RenderFn Oscillator::fn_table_[] PROGMEM = {
  &Oscillator::RenderSilence,

  &Oscillator::RenderSimpleWavetable,
  &Oscillator::RenderBandlimitedPwm,
  &Oscillator::RenderSimpleWavetable,
  &Oscillator::RenderSimpleWavetable,

  &Oscillator::RenderCzSaw,
  &Oscillator::RenderCzResoSaw,
  &Oscillator::RenderCzResoSaw,
  &Oscillator::RenderCzResoSaw,
  &Oscillator::RenderCzResoSaw,
  &Oscillator::RenderCzResoPulse,
  &Oscillator::RenderCzResoPulse,
  &Oscillator::RenderCzResoPulse,
  &Oscillator::RenderCzResoPulse,
  &Oscillator::RenderCzResoTri,
  
  &Oscillator::RenderQuadSawPad,
  
  &Oscillator::RenderFm,
  
  &Oscillator::Render8BitLand,
  &Oscillator::RenderDirtyPwm,
  &Oscillator::RenderFilteredNoise,
  &Oscillator::RenderVowel,
  
  &Oscillator::RenderInterpolatedWavetable
};

}  // namespace
