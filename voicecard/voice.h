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

#ifndef VOICECARD_VOICE_H_
#define VOICECARD_VOICE_H_

#include "voicecard/voicecard.h"

#include "common/lfo.h"
#include "common/patch.h"

#include "voicecard/envelope.h"

namespace ambika {

// Used for MIDI -> oscillator increment conversion.
static constexpr int16_t kLowestNote = 0 * 128;
static constexpr int16_t kHighestNote = 120 * 128;
static constexpr int16_t kOctave = 12 * 128;
static constexpr int16_t kPitchTableStart = 116 * 128;

// This mirrors the beginning of the Part data structure in the controller.
// Has only the bits of PartData that the voice needs to care about.
struct VoicePart {
  struct Parameters {
    uint8_t volume;
    uint8_t padding[4]; // to match Part bytes
    uint8_t legato;
    uint8_t portamento_time;
  };

private:
  union Data {
    Parameters params;
    uint8_t bytes[sizeof(Parameters)];
  };

  Data data;
  Parameters& p = data.params;

public:
  VoicePart() : data() {};

  inline uint8_t& volume() {
    return p.volume;
  }
  inline uint8_t& legato() {
    return p.legato;
  }
  inline uint8_t& portamento_time() {
    return p.portamento_time;
  }
  inline uint8_t* bytes() {
    return data.bytes;
  }
  inline void setData(uint8_t address, uint8_t value) {
    data.bytes[address] = value;
  }

};

class Voice {
 public:
  Voice() = default;

  static void Init();

  // Called whenever a new note is played, manually or through the arpeggiator.
  static void Trigger(uint16_t note, uint8_t velocity, uint8_t legato);

  // Move this voice to the release stage.
  static void Release();

  // Move this voice to the release stage.
  static void Kill() { TriggerEnvelope(Envelope::Stage::DEAD); }

  static void ProcessBlock();

  // Called whenever a write to the CV analog outputs has to be made.
  static inline uint8_t cutoff()  {
    return get_mod_dest_value(MOD_DST_FILTER_CUTOFF);
  }
  static inline uint8_t vca()  {
    return get_mod_dest_value(MOD_DST_VCA);
  }
  static inline uint8_t crush()  {
    return get_mod_dest_value(MOD_DST_MIX_CRUSH);
  }
  static inline uint8_t resonance()  {
    return get_mod_dest_value(MOD_DST_FILTER_RESONANCE);
  }
  static inline uint8_t get_mod_source_value(ModSource i) {
    return mod_source_value[i];
  }
  static inline uint8_t get_mod_dest_value(ModDestination dest) {
    return modulation_destinations[dest];
  }
  static inline void set_mod_source_value(ModSource source, uint8_t value) {
    mod_source_value[source] = value;
  }
  static inline void set_mod_dest_value(ModDestination dest, int8_t value) {
    modulation_destinations[dest] = value;
  }
  

  static Patch& patch() { return patch_object; }
  static VoicePart& part() { return part_object; }

  static void TriggerEnvelope(Envelope::Stage s);
  static void TriggerEnvelope(uint8_t index, Envelope::Stage s);
  
  static void ResetAllControllers();

 private:
  static inline void LoadSources();
  static inline void ProcessModulationMatrix();
  static inline void UpdateDestinations();
  static inline void RenderOscillators();

  static Patch patch_object;
  static VoicePart part_object;
  
  // Envelope generators.
  static Envelope envelope[kNumEnvelopes];
  static uint8_t gate;
  static Lfo voice_lfo;
  static uint8_t mod_source_value[kNumModulationSources];
  static int8_t modulation_destinations[kNumModulationDestinations];
  static int16_t dst[kNumModulationDestinations];

  // Counters/phases for the pitch envelope generator (portamento).
  // Pitches are stored on 14 bits, the 7 highest bits are the MIDI note value,
  // the 7 lowest bits are used for fine-tuning.
  static int16_t pitch_increment;
  static int16_t pitch_target;
  static int16_t pitch_value;
  
  static uint8_t buffer[kAudioBlockSize];
  static uint8_t osc2_buffer[kAudioBlockSize];
  static bool sync_state[kAudioBlockSize];
  static bool no_sync[kAudioBlockSize];
  static bool dummy_sync_state[kAudioBlockSize];

  DISALLOW_COPY_AND_ASSIGN(Voice);
};

extern Voice voice;

}  // namespace ambika

#endif  // VOICECARD_VOICE_H_
