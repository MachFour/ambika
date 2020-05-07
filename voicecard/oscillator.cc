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
    phase_tmp.integral = 0; \
    phase_tmp.fractional = 0; \
  } \
  (sync_input)++; \
  phase_tmp = U24AddC(phase_tmp, phase_increment); \
  *(sync_output) = phase_tmp.carry; \
  (sync_output)++;

#define UPDATE_PHASE _UPDATE_PHASE(sync_input, sync_output)
// This variant has a larger register width, but yields faster code.
#define UPDATE_PHASE_WITH_TMP _UPDATE_PHASE(sync_input_tmp, sync_output_tmp)

#define SAMPLE_LOOP_PROLOGUE \
  uint24c_t phase_tmp {.carry = 0, .integral = phase.integral, .fractional = phase.fractional};

#define SAMPLE_LOOP_PROLOGUE_WITH_TMP \
  SAMPLE_LOOP_PROLOGUE \
  uint8_t* sync_input_tmp = sync_input; \
  uint8_t* sync_output_tmp = sync_output; \

#define SAMPLE_LOOP_EPILOGUE phase.integral = phase_tmp.integral; phase.fractional = phase_tmp.fractional;

// ------- Silence (useful when processing external signals) -----------------
void Oscillator::RenderSilence(uint8_t* buffer) {
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    *buffer++ = 128;
  }
}

// ------- Band-limited PWM --------------------------------------------------
void Oscillator::RenderBandlimitedPwm(uint8_t* buffer) {
  uint8_t balance_index = U8Swap4(note /* - 12 play safe with Aliasing */);
  uint8_t gain_2 = highNibbleUnshifted(balance_index);
  uint8_t gain_1 = byteInverse(gain_2);

  const uint8_t wave_index_1 = lowNibble(balance_index);
  const uint8_t wave_index_2 = U8AddClip(wave_index_1, 1, kNumZonesHalfSampleRate);

  const uint8_t* wave_1 = waveform_table[WAV_RES_BANDLIMITED_SAW_1 + wave_index_1];
  const uint8_t* wave_2 = waveform_table[WAV_RES_BANDLIMITED_SAW_1 + wave_index_2];

  uint16_t shift = word(parameter + 128u, 0);

  // For higher pitched notes, simply use 128
  uint8_t scale = 192u - (parameter / 2);

  if (note > 52) {
    scale = U8Mix(scale, 102, U8(note - 52) * 4);
    scale = U8Mix(scale, 102, U8(note - 52) * 4);
  }
  phase_increment = U24ShiftLeft(phase_increment);

  SAMPLE_LOOP_PROLOGUE
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    phase_tmp = U24AddC(phase_tmp, phase_increment);
    *sync_output++ = phase_tmp.carry;
    *sync_output++ = 0;
    if (sync_input[0] || sync_input[1]) {
      phase_tmp.integral = 0;
      phase_tmp.fractional = 0;
    }
      sync_input += 2;
    
    uint8_t a = InterpolateTwoTables(wave_1, wave_2, phase_tmp.integral, gain_1, gain_2);
    uint8_t b = InterpolateTwoTables(wave_1, wave_2, phase_tmp.integral + shift, gain_1, gain_2);
    a = U8U8MulShift8(a, scale);
    b = U8U8MulShift8(b, scale);
    a = a - b + 128;
    *buffer++ = a;
    *buffer++ = a;

    // second decrement
    samples_left--;
  }
  SAMPLE_LOOP_EPILOGUE
}

// ------- Interpolation between two waveforms from two wavetables -----------
// The position is determined by the note pitch, to prevent aliasing.

void Oscillator::RenderSimpleWavetable(uint8_t* buffer) {
  uint8_t balance_index = U8Swap4(note);
  uint8_t gain_2 = highNibbleUnshifted(balance_index);
  uint8_t gain_1 = byteInverse(gain_2);
  uint8_t wave_1_index, wave_2_index;
  if (shape == WAVEFORM_SINE) {
    wave_1_index = WAV_RES_SINE;
    wave_2_index = WAV_RES_SINE;
  } else {
    uint8_t wave_index = lowNibble(balance_index);
    uint8_t base_resource_id;
    switch (shape) {
      default:
        base_resource_id = WAV_RES_BANDLIMITED_TRIANGLE_0;
        break;
      case WAVEFORM_SAW:
        base_resource_id = WAV_RES_BANDLIMITED_SAW_0;
        break;
      case WAVEFORM_SQUARE:
        base_resource_id = WAV_RES_BANDLIMITED_SQUARE_0;
        break;
    }
    wave_1_index = base_resource_id + wave_index;
    wave_2_index = base_resource_id + U8AddClip(wave_index, 1, kNumZonesFullSampleRate);
  }
  const uint8_t* wave_1 = waveform_table[wave_1_index];
  const uint8_t* wave_2 = waveform_table[wave_2_index];

  SAMPLE_LOOP_PROLOGUE_WITH_TMP
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE_WITH_TMP
    uint8_t sample = InterpolateTwoTables(wave_1, wave_2, phase_tmp.integral, gain_1, gain_2);

    if (sample < parameter) {
      // The waveshaper for the triangle is different.
      sample = (shape == WAVEFORM_TRIANGLE) ? parameter : sample + parameter / 2;
    }
    *buffer++ = sample;
  }
  SAMPLE_LOOP_EPILOGUE
}

// ------- Casio CZ-like synthesis -------------------------------------------
void Oscillator::RenderCzSaw(uint8_t* buffer) {
  SAMPLE_LOOP_PROLOGUE_WITH_TMP
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE_WITH_TMP
    uint8_t phi = highByte(phase_tmp.integral);
    // thanks to https://github.com/bjoeri/ambika/
    uint8_t clipped_phi = phase.integral < 0x2000 ? phase.integral >> 5u : 0xff;
    // Interpolation causes more aliasing here.
    *buffer++ = ReadSample(wav_res_sine, U8MixU16(phi, clipped_phi, parameter * 2));
  }
  SAMPLE_LOOP_EPILOGUE
}

void Oscillator::RenderCzResoSaw(uint8_t* buffer) {
  const uint8_t wave_type = shape - WAVEFORM_CZ_SAW_LP;
  const uint8_t isBPorHP = byteAnd(wave_type, 2); // used as boolean (nonzero is true)
  uint16_t phase_2 = data.secondary_phase;
  uint16_t increment = phase_increment.integral + U16((phase_increment.integral * U16(parameter)) / 4);
  SAMPLE_LOOP_PROLOGUE_WITH_TMP
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE_WITH_TMP
    if (phase_tmp.carry) {
      phase_2 = ResourcesManager::Lookup<uint16_t, uint8_t>(lut_res_cz_phase_reset, byteAnd(wave_type, 0x03));
    }
    phase_2 += increment;
    uint8_t carrier = ReadSample(wav_res_sine, phase_2);
    uint8_t window = ~highByte(phase_tmp.integral);
    if (isBPorHP) {
      *buffer++ = S8U8MulShift8(carrier + 128, window) + 128;
    } else {
      *buffer++ = U8U8MulShift8(carrier, window);
    }
  }
  SAMPLE_LOOP_EPILOGUE
  data.secondary_phase = phase_2;
}

void Oscillator::RenderCzResoPulse(uint8_t* buffer) {
  using rs = ResourcesManager;
  const uint8_t wave_type = shape - WAVEFORM_CZ_SAW_LP;
  const uint8_t isBPorHP = byteAnd(wave_type, 2); // != 0;
  uint16_t increment = phase_increment.integral + U16((phase_increment.integral * U16(parameter)) / 4);
  uint16_t phase_2 = data.secondary_phase;
  SAMPLE_LOOP_PROLOGUE_WITH_TMP
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE_WITH_TMP
    if (phase_tmp.carry) {
      phase_2 = rs::Lookup<uint16_t, uint8_t>(lut_res_cz_phase_reset, byteAnd(wave_type, 0x03));
    }
    phase_2 += increment;
    uint8_t carrier = ReadSample(wav_res_sine, phase_2);
    uint8_t window = 0;
    if (phase_tmp.integral < 0x4000) {
      window = 255;
    } else if (phase_tmp.integral < 0x8000) {
      window = U16(~(phase_tmp.integral - 0x4000u)) >> 6u;
    }
    if (wave_type == 5) {
      // WAVEFORM_CZ_PLS_PK
      carrier >>= 1u;
      carrier += 128;
    }
    if (isBPorHP) {
      *buffer++ = S8U8MulShift8(carrier + 128, window) + 128;
    } else {
      *buffer++ = U8U8MulShift8(carrier, window);
    }
  }
  SAMPLE_LOOP_EPILOGUE
  data.secondary_phase = phase_2;
}

void Oscillator::RenderCzResoTri(uint8_t* buffer) {
  using rs = ResourcesManager;
  const uint8_t cz_wave_type = shape - WAVEFORM_CZ_SAW_LP; // == 8 for ztri
  // this computation depends on order of waves specified in patch.h
  // 0 corresponds to LP, 1 to PK, 2 to BP, 3 to HP
  const uint8_t filter_type = byteAnd(cz_wave_type, 3); // == wave_type % 4
  const uint8_t isBPorHP = byteAnd(cz_wave_type, 2); // == (filter_type >= 2) in boolean expressions
  uint16_t increment = phase_increment.integral + ((phase_increment.integral * U16(parameter)) / 4);
  uint16_t phase_2 = data.secondary_phase;
  SAMPLE_LOOP_PROLOGUE_WITH_TMP
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE_WITH_TMP
    if (phase_tmp.carry) {
      phase_2 = rs::Lookup<uint16_t, uint8_t>(lut_res_cz_phase_reset, filter_type);
    }
    phase_2 += increment;
    uint8_t carrier = ReadSample(wav_res_sine, phase_2);
    uint8_t window = phase_tmp.integral >> 7u;
    if (wordAnd(phase_tmp.integral, 0x8000)) {
      window = byteInverse(window);
    }
    if (isBPorHP) {
      *buffer++ = S8U8MulShift8(carrier + 128, window) + 128;
    } else {
      *buffer++ = U8U8MulShift8(carrier, window);
    }
  }
  SAMPLE_LOOP_EPILOGUE
  data.secondary_phase = phase_2;
}

/*
 * Merges RenderCzResoTri, RenderCzResoPulse, RenderCzResoSaw
 */

void Oscillator::RenderCzResoWave(uint8_t* buffer) {
  using rs = ResourcesManager;
  const uint8_t cz_wave_type = shape - WAVEFORM_CZ_SAW_LP; // == 8 for ztri
  const uint8_t cz_wave_shape = cz_wave_type / 4; // == 0 for saw, 1, for pulse, 2 for tri
  // this computation depends on order of waves specified in patch.h
  // 0 corresponds to LP, 1 to PK, 2 to BP, 3 to HP
  const uint8_t filter_type = cz_wave_type % 4; // == byteAnd(cz_wave_type, 3); (optimiser should take care of this)
  const uint8_t isBPorHP = byteAnd(cz_wave_type, 2); // == (filter_type >= 2) in boolean expressions
  const uint16_t increment = phase_increment.integral + ((phase_increment.integral * U16(parameter)) / 4);
  uint16_t phase_2 = data.secondary_phase;

  SAMPLE_LOOP_PROLOGUE_WITH_TMP
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE_WITH_TMP
    bool phase_reset = phase_tmp.carry != 0;
    if (phase_reset) {
      phase_2 = rs::Lookup<uint16_t, uint8_t>(lut_res_cz_phase_reset, filter_type);
    }
    phase_2 += increment;
    uint8_t carrier = ReadSample(wav_res_sine, phase_2);
    uint8_t window;
    // TODO is having the switch statement inside the loop incredibad?
    switch (cz_wave_shape) {
      case 0: // saw
        window = byteInverse(highByte(phase_tmp.integral));
        break;
      case 1: // pulse
        window = 0;
        if (phase_tmp.integral < 0x4000) {
          window = 255;
        } else if (phase_tmp.integral < 0x8000) {
          window = U16(~(phase_tmp.integral - 0x4000u)) >> 6u;
        }
        if (cz_wave_type == 5) { // WAVEFORM_CZ_PLS_PK
          carrier = carrier / 2 + 128;
        }
        break;
      case 2: // tri
        window = phase_tmp.integral >> 7u;
        if (byteAnd(highByte(phase_tmp.integral), 0x80)) {
          window = byteInverse(window);
        }
        break;
    }
    if (isBPorHP) {
      *buffer++ = S8U8MulShift8(carrier + 128, window) + 128;
    } else {
      *buffer++ = U8U8MulShift8(carrier, window);
    }
  }
  SAMPLE_LOOP_EPILOGUE
  data.secondary_phase = phase_2;
}

// ------- FM ----------------------------------------------------------------
void Oscillator::RenderFm(uint8_t* buffer) {
  uint8_t offset = fm_parameter < 24 ? 0 : (fm_parameter > 48 ? 24 : fm_parameter - 24);
  auto multiplier = ResourcesManager::Lookup<uint16_t, uint8_t>(lut_res_fm_frequency_ratios, offset);
  uint16_t increment = U32(phase_increment.integral * U16(multiplier)) >> 8u;
  parameter *= 2;
  
  uint16_t phase_2 = data.secondary_phase;
  SAMPLE_LOOP_PROLOGUE
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE
    phase_2 += increment;
    uint8_t modulator = InterpolateSample(wav_res_sine, phase_2);
    uint16_t modulation = modulator * parameter;
    *buffer++ = InterpolateSample(wav_res_sine, phase_tmp.integral + modulation);
  }
  SAMPLE_LOOP_EPILOGUE
  data.secondary_phase = phase_2;
}

// ------- 8-bit land --------------------------------------------------------
void Oscillator::Render8BitLand(uint8_t* buffer) {
  const uint8_t x = parameter;
  const uint8_t x_shift_left = x << 1u;
  const uint8_t x_shift_right = x >> 1u;
  SAMPLE_LOOP_PROLOGUE
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE
    uint8_t basic_saw_sample = highByte(phase_tmp.integral);
    *buffer++ = byteAnd(basic_saw_sample ^ x_shift_left, ~x_shift_left) + x_shift_right;
  }
  SAMPLE_LOOP_EPILOGUE
}

void Oscillator::RenderVowel(uint8_t* buffer) {
  using rs = ResourcesManager;
  data.vw.update = (data.vw.update + 1) % 4; //byteAnd(data.vw.update + 1, 0x3);
  if (!data.vw.update) {
    uint8_t offset_1 = highNibble(parameter);
    uint8_t balance = lowNibble(parameter);
    offset_1 = U8U8Mul(offset_1, 7);
    uint8_t offset_2 = offset_1 + 7; // == 8 * (original value of) offset 1

    // Interpolate formant frequencies.
    for (uint8_t i = 0; i < 3; ++i) {
      auto freq_a = rs::Lookup<uint8_t, uint8_t>(wav_res_vowel_data, offset_1 + i);
      auto freq_b = rs::Lookup<uint8_t, uint8_t>(wav_res_vowel_data, offset_2 + i);
      data.vw.formant_increment[i] = U8U4MixU12(freq_a, freq_b, balance) << 3u;
    }
    
    // Interpolate formant amplitudes.
    // formant_amplitude[3] is noise_modulation
    for (uint8_t i = 0; i < 4; ++i) {
      const auto index_a = offset_1 + 3 + i;
      const auto index_b = offset_2 + 3 + i;
      auto amplitude_a = rs::Lookup<uint8_t, uint8_t>(wav_res_vowel_data, index_a);
      auto amplitude_b = rs::Lookup<uint8_t, uint8_t>(wav_res_vowel_data, index_b);
      data.vw.formant_amplitude[i] = U8U4MixU8(amplitude_a, amplitude_b, balance);
    }
  }
  auto noise_modulation = data.vw.formant_amplitude[3];
  const int16_t phase_noise = S8S8Mul(Random::state_msb(), noise_modulation);

  SAMPLE_LOOP_PROLOGUE
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    int8_t result = 0;
    uint8_t phaselet;

    data.vw.formant_phase[0] += data.vw.formant_increment[0];
    phaselet = highNibbleUnshifted(highByte(data.vw.formant_phase[0]));
    result = rs::Lookup<uint8_t, uint8_t>(wav_res_formant_sine, phaselet | data.vw.formant_amplitude[0]);

    data.vw.formant_phase[1] += data.vw.formant_increment[1];
    phaselet = highNibbleUnshifted(highByte(data.vw.formant_phase[1]));
    result += rs::Lookup<uint8_t, uint8_t>(wav_res_formant_sine, phaselet | data.vw.formant_amplitude[1]);

    data.vw.formant_phase[2] += data.vw.formant_increment[2];
    phaselet = highNibbleUnshifted(highByte(data.vw.formant_phase[2]));
    result += rs::Lookup<uint8_t, uint8_t>(wav_res_formant_square, phaselet | data.vw.formant_amplitude[2]);
    
    result = S8U8MulShift8(result, highByte(phase_tmp.integral));
    phase_tmp.integral -= phase_increment.integral;
    if ((phase_tmp.integral + phase_noise) < phase_increment.integral) {
      data.vw.formant_phase[0] = 0;
      data.vw.formant_phase[1] = 0;
      data.vw.formant_phase[2] = 0;
    }
    // (-32, 32) -> (0, 255)
    //uint8_t x = S16ClipS8(4 * result) + 128;
    uint8_t x;
    if (result <= -32) {
      x = 0;
    } else if (result >= 32) {
      x = 255;
    } else {
      x = U8(result + 32) << 2u;
    }
    *buffer++ = x;
    *buffer++ = x;
    samples_left--; // second decrement
  }
  SAMPLE_LOOP_EPILOGUE
}

// ------- Dirty Pwm (kills kittens) -----------------------------------------
void Oscillator::RenderDirtyPwm(uint8_t* buffer) {
  const uint8_t flip_point = 127u + parameter;
  SAMPLE_LOOP_PROLOGUE
    for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE
    uint8_t current_phase_byte = highByte(phase_tmp.integral);
    *buffer++ = current_phase_byte < flip_point ? 0 : 255;
  }
  SAMPLE_LOOP_EPILOGUE
}

// ------- Quad saw (mit aliasing) -------------------------------------------
void Oscillator::RenderQuadSawPad(uint8_t* buffer) {
  uint16_t phase_spread = U32(phase_increment.integral * U16(parameter)) >> 13u;
  ++phase_spread;
  uint16_t phase_increment_tmp = phase_increment.integral;
  uint16_t increments[3];
  for (uint8_t i = 0; i < 3; ++i) {
    phase_increment_tmp += phase_spread;
    increments[i] = phase_increment_tmp;
  }

  SAMPLE_LOOP_PROLOGUE
    for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE
      data.qs.phase[0] += increments[0];
      data.qs.phase[1] += increments[1];
      data.qs.phase[2] += increments[2];
    uint8_t value = (phase_tmp.integral >> 10u);
    value += (data.qs.phase[0] >> 10u);
    value += (data.qs.phase[1] >> 10u);
    value += (data.qs.phase[2] >> 10u);
    *buffer++ = value;
  }
  SAMPLE_LOOP_EPILOGUE
}

// ------- Low-passed, then high-passed white noise --------------------------
void Oscillator::RenderFilteredNoise(uint8_t* buffer) {
  uint16_t rng_state = data.no.rng_state;
  if (rng_state == 0) {
    ++rng_state;
  }
  uint8_t filter_coefficient = parameter * 4 ;
  if (filter_coefficient <= 4) {
    filter_coefficient = 4;
  }
  SAMPLE_LOOP_PROLOGUE
    for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    if (*sync_input++) {
      rng_state = data.no.rng_reset_value;
    }
    rng_state = U16(rng_state >> 1u) ^ (-(rng_state & 1u) & 0xb400u);
    uint8_t noise_sample = highByte(rng_state);
    // This trick is used to avoid having a DC component (no innovation) when
    // the parameter is set to its minimal or maximal value.
    data.no.lp_noise_sample = U8Mix(data.no.lp_noise_sample, noise_sample, filter_coefficient);
    if (parameter >= 64) {
      *buffer++ = noise_sample - data.no.lp_noise_sample - 128;
    } else {
      *buffer++ = data.no.lp_noise_sample;
    }
  }
  SAMPLE_LOOP_EPILOGUE
  data.no.rng_state = rng_state;
}

// The position is freely determined by the parameter
void Oscillator::RenderInterpolatedWavetable(uint8_t* buffer) {
  const uint8_t* which_wavetable = wav_res_wavetables + U8(shape - WAVEFORM_WAVETABLE_1) * U8(18);

  // Get a 8:8 value with the wave index in the first byte, and the
  // balance amount in the second byte.
  auto num_steps = ResourcesManager::Lookup<uint8_t, uint8_t>(which_wavetable, 0);
  uint16_t pointer = U8(parameter * 2) * num_steps;
  auto wave_index_1 = ResourcesManager::Lookup<uint8_t, uint8_t>(which_wavetable, 1 + highByte(pointer));
  auto wave_index_2 = ResourcesManager::Lookup<uint8_t, uint8_t>(which_wavetable, 2 + highByte(pointer));
  uint8_t gain = lowByte(pointer);
  const uint8_t* wave_1 = wav_res_waves + U8U8Mul(wave_index_1, 129);
  const uint8_t* wave_2 = wav_res_waves + U8U8Mul(wave_index_2, 129);
  SAMPLE_LOOP_PROLOGUE_WITH_TMP
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE_WITH_TMP
    *buffer++ = InterpolateTwoTables(wave_1, wave_2, phase_tmp.integral / 2, ~gain, gain);
  }
  SAMPLE_LOOP_EPILOGUE
}

// The position is freely determined by the parameter
void Oscillator::RenderWavequence(uint8_t* buffer) {
  const uint8_t* wave = wav_res_waves + U8U8Mul(parameter, 129);
  SAMPLE_LOOP_PROLOGUE
    for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE
    *buffer++ = InterpolateSample(wave, phase_tmp.integral / 2);
  }
  SAMPLE_LOOP_EPILOGUE
}


#define CALCULATE_DIVISION_FACTOR(divisor, result_quotient, result_quotient_shifts) \
  uint16_t div_table_index = divisor; \
  int8_t result_quotient_shifts = 0; \
  while (div_table_index > 255) { \
    div_table_index >>= 1; \
    --result_quotient_shifts; \
  } \
  while (div_table_index < 128) { \
    div_table_index <<= 1; \
    ++result_quotient_shifts; \
  } \
  div_table_index -= 128; \
  using rs = ResourcesManager; \
  uint8_t result_quotient = rs::Lookup<uint8_t, uint8_t>(wav_res_division_table, div_table_index);

// function version of CALCULATE_BLEP_INDEX from github.com/bjoeri/ambika/voicecard/oscillator.cc
  uint8_t calculate_blep_index(uint16_t increment, uint8_t quotient, int8_t quotient_shifts) {
    uint16_t blep_index = increment;
    if (quotient_shifts < 0) {
      blep_index >>= U8(-quotient_shifts);
    } else {
      blep_index <<= U8(quotient_shifts);
    }
    // approximate division by multiplication with inverse
    blep_index = U16U8MulShift8(blep_index, quotient);
    return highByte(blep_index);
  }


  /* ------- Polyblep Saw ------------------------------------------------------
 * Implementation adapted from https://github.com/bjoeri/ambika
 * His description:
 * Heavily inspired by Oliviers experimental implementation for STM but
 * dumbed down and much less generic (does not do polyblep for sync etc)
 */
void Oscillator::RenderPolyBlepSaw(uint8_t* buffer) {
  using rs = ResourcesManager;

  // calculate (1/increment) for later multiplication with current phase
  CALCULATE_DIVISION_FACTOR(phase_increment.integral, quotient, quotient_shifts)

  // Revert to pure saw (=single blep) to avoid cpu overload for high notes
  uint8_t mod_parameter = note > 107 ? 0 : parameter;
  uint8_t phase_exceeds = phase.integral >= 0x8000;

  uint8_t next_sample = data.output_sample;
  SAMPLE_LOOP_PROLOGUE
    for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE
    uint8_t this_sample = next_sample;

    // Compute naive waveform
    next_sample = highByte(phase_tmp.integral);

    if (next_sample >= 0x80) {
      next_sample -= mod_parameter;
    }

    uint16_t blep_index;
    uint8_t mix_parameter;

    if (phase_tmp.carry) {
      phase_exceeds = false;
      blep_index = calculate_blep_index(phase_tmp.integral, quotient, quotient_shifts);
      mix_parameter = 255 - mod_parameter;

    } else if (mod_parameter && !phase_exceeds && phase_tmp.integral >= 0x8000) {
      phase_exceeds = true;
      blep_index = calculate_blep_index(phase_tmp.integral - 0x8000, quotient, quotient_shifts);
      mix_parameter = mod_parameter;
    } else {
      // don't update this_sample and next_sample
      *buffer++ = this_sample;
      continue;
    }

    auto blep_lookup = rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, blep_index);
    this_sample -= U8U8MulShift8(blep_lookup, mix_parameter); // scale blep to size of edge

    auto blep_lookup_inv = rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, 127 - blep_index);
    next_sample += U8U8MulShift8(blep_lookup_inv, mix_parameter); // scale blep to size of edge

    *buffer++ = this_sample;
  } SAMPLE_LOOP_EPILOGUE

  data.output_sample = next_sample;
}

/* ------- Polyblep Pwm ------------------------------------------------------
 * Implementation adapted from https://github.com/bjoeri/ambika
 * His description:
 * Heavily inspired by Oliviers experimental implementation for STM but
 * dumbed down and much less generic (does not do polyblep for sync etc)
 */
void Oscillator::RenderPolyBlepPwm(uint8_t* buffer) {
  using rs = ResourcesManager;

  // calculate (1/increment) for later multiplication with current phase
  CALCULATE_DIVISION_FACTOR(phase_increment.integral, quotient, quotient_shifts)

  // Revert to pure saw (=single blep) to avoid cpu overload for high notes
  bool revert_to_saw = note > 107;

  // PWM modulation (constrained to extend over at least one increment)
  uint8_t pwm_limit = 127 - highByte(phase_increment.integral);
  // prevent dual bleps at same increment

  uint8_t pwm_phase_offset = (parameter < pwm_limit) ? parameter : pwm_limit; // == min(parameter, pwm_limit)
  uint16_t pwm_phase = word(127 + pwm_phase_offset, 0);
  bool phase_exceeds = (phase.integral >= pwm_phase);
  uint8_t next_sample = data.output_sample;

  SAMPLE_LOOP_PROLOGUE
    for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE
    uint8_t this_sample = next_sample;

    // Compute naive waveform
    if (revert_to_saw) {
      next_sample = highByte(phase_tmp.integral);
    } else if (phase_tmp.integral < pwm_phase) {
      next_sample = 0;
    } else {
      next_sample = 255;
    }

    if (phase_tmp.carry) {
      phase_exceeds = false;
      uint8_t blep_index = calculate_blep_index(phase_tmp.integral, quotient, quotient_shifts);

      this_sample -= rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, blep_index);
      next_sample += rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, 127 - blep_index);

    } else if (!revert_to_saw && /* no positive edge for pure saw */
             phase_tmp.integral >= pwm_phase && !phase_exceeds) {
      phase_exceeds = true;
      uint8_t blep_index = calculate_blep_index(phase_tmp.integral - pwm_phase, quotient, quotient_shifts);

      this_sample += rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, blep_index);
      next_sample -= rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, 127 - blep_index);
    }

    *buffer++ = this_sample;
  } SAMPLE_LOOP_EPILOGUE

  data.output_sample = next_sample;
}

/* ------- Polyblep CS-80 Saw ------------------------------------------------
 * Implementation adapted from https://github.com/bjoeri/ambika
 * His descriptions:
 * Heavily inspired by Emilies experimental implementation for STM but
 * dumbed down and much less generic (does not do polyblep for sync etc)
 */
void Oscillator::RenderPolyBlepCSaw(uint8_t* buffer) {
  using rs = ResourcesManager;

  // calculate (1/increment) for later multiplication with current phase
  CALCULATE_DIVISION_FACTOR(phase_increment.integral, quotient, quotient_shifts)

  // Revert to pure saw (=single blep) to avoid cpu overload for high notes
  bool revert_to_saw = note > 107;

  // PWM modulation (constrained to extend over at least one increment)
  uint8_t pwm_limit = highByte(phase_increment.integral);

  uint8_t pwm_phase_offset = (parameter > 0 && parameter < pwm_limit) ? pwm_limit : parameter;
  uint16_t pwm_phase = word(pwm_phase_offset, 0);

  bool phase_exceeds = (phase.integral >= pwm_phase);

  uint8_t next_sample = data.output_sample;
  SAMPLE_LOOP_PROLOGUE
    for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE
    uint8_t this_sample = next_sample;

    // Compute naive waveform
    next_sample = (revert_to_saw || phase_tmp.integral >= pwm_phase) ? highByte(phase_tmp.integral) : 0;

    if (phase_tmp.carry) {
      phase_exceeds = false;
      uint16_t blep_index = calculate_blep_index(phase_tmp.integral, quotient, quotient_shifts);
      this_sample -= rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, blep_index);
      next_sample += rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, 127-blep_index);
    }
    else if (!revert_to_saw && /* no positive edge for pure saw */
             phase_tmp.integral >= pwm_phase && !phase_exceeds) {
      phase_exceeds = true;
      uint16_t blep_index = calculate_blep_index(phase_tmp.integral - pwm_phase, quotient, quotient_shifts);
      // scale bleps to size of edge
      this_sample += U8U8MulShift8(rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, blep_index), parameter);
      next_sample -= U8U8MulShift8(rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, 127-blep_index), parameter);
    }

    *buffer++ = this_sample;
  } SAMPLE_LOOP_EPILOGUE

  data.output_sample = next_sample;
}


// Combined polyblep saw/pwm/csaw
/*
 * Polyblep is implemented with one sample of delay:
 * in order to know if there's a discontinuity between samples, we need to keep track
 * of two samples at once: 'this sample' and 'next' sample'.
 * Hence a 'last output sample' is needed to be stored as part of the oscillator state
 */
void Oscillator::RenderPolyBlepWave(uint8_t *buffer) {
  using rs = ResourcesManager;
  const OscillatorAlgorithm wave_type = static_cast<OscillatorAlgorithm>(shape - 3);

  // calculate (1/increment) for later multiplication with current phase
  CALCULATE_DIVISION_FACTOR(phase_increment.integral, quotient, quotient_shifts)

  // Revert to pure saw (=single blep) for high notes, to avoid cpu overload
  const bool use_simple_saw = (note > 107);

  uint8_t saw_parameter = parameter;

  // Where in the cycle (8 bit precision) does our wave have a step discontinuity?
  // NB for all waves, there is also a step discontinuity at zero,
  // i.e whenever the phase_tmp overflows / phase_tmp.carry != 0
  uint8_t step_phase_byte;

  // setup parameters for each wave type
  switch (wave_type) {
    case WAVEFORM_POLYBLEP_SAW:
      // parameter value is used to shift the second half of the sawtooth downwards
      step_phase_byte = 127;
      break;
    case WAVEFORM_POLYBLEP_PWM:
    {
      // For pwm: where to flip from low to high. 127 means (approx) 50% duty cycle
      // First, limit the pwm range so each half of the cycle lasts at least one increment.
      // I'm assuming that highByte(phase_increment.integral) is never more than 127?
      const uint8_t max_pwm_deviation = 127 - highByte(phase_increment.integral);

      // PWM point is calculated so that parameter value of 0 corresponds to 50% duty cycle
      // Also, this prevents dual bleps at same increment... somehow
      const uint8_t pwm_step_phase = 127 + ((parameter < max_pwm_deviation) ? parameter : max_pwm_deviation);
      // == 127 + min(parameter, pwm_limit)
      step_phase_byte = pwm_step_phase;
    }
    break;
    case WAVEFORM_POLYBLEP_CSAW:
    {
      // The PWM point for Csaw works a bit differently: the parameter value is nominally directly
      // tied to the flip point, but the actual duty cycle constrained to be in the interval
      // [highByte(phase_increment.integral)/255, 127/255]
      // This also ensures that the PWM modulation does not get smaller than one increment
      const uint8_t csaw_min_pwm_phase = highByte(phase_increment.integral);
      const uint8_t csaw_step_phase = (parameter < csaw_min_pwm_phase) ? csaw_min_pwm_phase : parameter;
      // == max(parameter, pwm_limit)
      step_phase_byte = csaw_step_phase;
      break;
    }
    default:
      step_phase_byte = 0;
      break;
  }

  // where does the first sample start in the cycle?
  bool already_past_step_point = highByte(phase.integral) >= step_phase_byte;

  // 16-bit version of step_phase_byte, for higher precision blep calculations
  const uint16_t step_phase = word(step_phase_byte, 0);

  uint8_t next_sample = data.output_sample;

  SAMPLE_LOOP_PROLOGUE
  for (uint8_t samples_left = kAudioBlockSize; samples_left > 0; samples_left--) {
    UPDATE_PHASE
    // move one sample forward ('the future is now')
    uint8_t this_sample = next_sample;

    uint16_t current_phase = phase_tmp.integral;
    uint16_t current_phase_byte = highByte(current_phase);
    bool past_step_point = current_phase_byte >= step_phase_byte; // small optimisation: 8 bit compare

    // Naive waveform for next sample
    if (use_simple_saw) {
      next_sample = current_phase_byte; // nothing else to do
    } else {
      switch (wave_type) {
        case WAVEFORM_POLYBLEP_SAW:
          // Shift the upper half cycle of the sawtooth wave downwards by the parameter value
          next_sample = (past_step_point) ? current_phase_byte - saw_parameter : current_phase_byte;
          break;
        case WAVEFORM_POLYBLEP_PWM:
          next_sample = (past_step_point) ? 255 : 0;
          break;
        case WAVEFORM_POLYBLEP_CSAW:
          next_sample = (past_step_point) ? current_phase_byte : 0;
          break;
        default:
          break;
      }
    }
    // Every time the phase resets, all waveforms have a negative step
    // discontinuity.

    // Provided use_simple_saw == false, all waveforms have
    // a step discontinuity the first time that the phase exceeds step_phase,
    // aka when past_step_point == true but already_past_step_point == false.
    // For the basic saw, this is a negative step, for the other two waves it's positive.

    bool phase_reset = phase_tmp.carry != 0;
    bool just_reached_step_point = past_step_point && !already_past_step_point;

    uint16_t phase_offset_from_step;

    if (phase_reset) {
      // it's a negative edge
      already_past_step_point = false;
      // remember: phase has just reset, so current_phase should be small
      phase_offset_from_step = current_phase;
    } else if (!use_simple_saw && just_reached_step_point) {
      already_past_step_point = true;
      // at this point, we must have current_phase >= step_phase
      uint16_t phase_excess = current_phase_byte - step_phase;
      phase_offset_from_step = phase_excess;
    } else {
      // we're done
      *buffer++ = this_sample;
      continue;
    }

    uint8_t blep_index = calculate_blep_index(phase_offset_from_step, quotient, quotient_shifts);

    auto blep_residual_this = rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, blep_index);
    auto blep_residual_next = rs::Lookup<uint8_t, uint8_t>(wav_res_blep_table, 127 - blep_index);

    bool step_is_negative = next_sample < this_sample;
    uint8_t step_size = step_is_negative ? this_sample - next_sample : next_sample - this_sample;

    // Scale bleps to size of edge:
    blep_residual_this = highByte(blep_residual_this * step_size);
    blep_residual_next = highByte(blep_residual_next * step_size);

    if (step_is_negative) {
      this_sample -= blep_residual_this;
      next_sample += blep_residual_next;
    } else {
      this_sample += blep_residual_this;
      next_sample -= blep_residual_next;
    }
    *buffer++ = this_sample;
  }
  SAMPLE_LOOP_EPILOGUE

  data.output_sample = next_sample;
}

}  // namespace
