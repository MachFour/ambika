// Copyright 2011 Emilie Gillet.
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
// Simple SPI update bootloader.
//
// Blink patterns:
// 
// R&G blink together on incoming SPI data: firmware update requested, waiting
// for confirmation.
// 
// G on, O blink: confirmation received, recording firmware data into flash.
// 
// R&G blink together very fast for 1.2s: update successful!

#include <avr/boot.h>
#include <avr/pgmspace.h>

#include "avrlib/op.h"
#include "avrlib/spi.h"
#include "avrlib/time.h"
#include "avrlib/watchdog_timer.h"

#include "common/protocol.h"

#include "voicecard/leds.h"
#include "voicecard/voicecard.h"

using namespace avrlib;
using namespace ambika;

SpiSlave<MSB_FIRST, true> spi;

static uint8_t rx_buffer[SPM_PAGESIZE];

int main() __attribute__ ((naked)) __attribute__ ((section (".init9")));

int main() {
  ResetWatchdog();
  cli();
  SP = RAMEND;
  asm volatile ("clr __zero_reg__");
  RxLed::outputMode();
  NoteLed::outputMode();
  
  uint8_t update_flag = eeprom_read_byte(kFirmwareUpdateFlagPtr);
  if (update_flag == 0xff) {
    update_flag = 0;
  }

  // Flash LED to indicate boot error.
  if (update_flag >= FIRMWARE_UPDATE_PROBING_BOOT) {
    for (uint8_t i = 0; i < 10; ++i) {
      RxLed::toggle();
      ConstantDelay(200);
    }
  }
  
  // After 3 unsuccessful boots, switch to upgrade mode.
  if (update_flag >= FIRMWARE_UPDATE_PROBING_BOOT_LAST_TRY) {
    update_flag = FIRMWARE_UPDATE_REQUESTED;
  }
  if (update_flag == FIRMWARE_UPDATE_REQUESTED) {
    spi.Init();
    
    // Sync phase: wait for a sequence of at least 240 reset commands.
    uint8_t reset_command_counter = 0;
    while (1) {
      NoteLed::low();
      RxLed::low();
      spi.Reply(SPM_PAGESIZE >> 4);
      uint8_t byte = spi.Read();
      NoteLed::high();
      RxLed::high();
      
      if (byte == COMMAND_FIRMWARE_UPDATE_MODE) {
        ++reset_command_counter;
      } else {
        if (reset_command_counter >= 240) {
          rx_buffer[0] = byte & 0x0f;
          spi.Reply(0);
          rx_buffer[0] |= U8Swap4(spi.Read() & 0x0f);
          break;
        } else {
          reset_command_counter = 0;
        }
      }
    }
    
    // Update phase: record incoming nibbles to flash until reset command is
    // seen in the stream.
    uint8_t total_read = 0;
    uint16_t read = 1;
    uint8_t done = 0;
    uint16_t page = 0;
    NoteLed::high();
    while (!done) {
      // Blink the LED.
      RxLed::low();
      uint8_t byte = spi.Read();
      RxLed::high();
      
      // Oops, we have found a stop byte.
      if (byte == COMMAND_FIRMWARE_UPDATE_MODE) {
        // If there is a pending block, pad it.
        if (read) {
          read = SPM_PAGESIZE;
        }
        done = 1;
      } else {
        // Write a pair of nibbles.
        rx_buffer[read] = byte & 0x0f;
        ++total_read;
        spi.Reply(total_read);
        RxLed::low();
        rx_buffer[read++] |= U8Swap4(spi.Read() & 0x0f);
        RxLed::high();
      }
      // A page is full (or has been padded).
      if (read == SPM_PAGESIZE) {
        uint16_t i;
        const uint8_t* p = rx_buffer;
        eeprom_busy_wait();

        boot_page_erase(page);
        boot_spm_busy_wait();

        for (i = 0; i < SPM_PAGESIZE; i += 2) {
          Word w;
          w.bytes[0] = *p++;
          w.bytes[1] = *p++;
          boot_page_fill(page + i, w.value);
        }

        boot_page_write(page);
        boot_spm_busy_wait();
        boot_rww_enable();

        page += SPM_PAGESIZE;
        read = 0;
      }
    }
    RxLed::low();
    // A little dance to say that we are good.
    for (uint8_t i = 0; i < 16; ++i) {
      NoteLed::toggle();
      RxLed::toggle();
      ConstantDelay(75);
    }
  }
  uint8_t new_update_flag = FIRMWARE_UPDATE_PROBING_BOOT;
  if (update_flag >= 2 && update_flag < FIRMWARE_UPDATE_PROBING_BOOT_LAST_TRY) {
    new_update_flag = update_flag + 1;
  }
  eeprom_write_byte(kFirmwareUpdateFlagPtr, new_update_flag);
  void (*main_entry_point)() = nullptr; // 0x0 aka start of RAM
  main_entry_point();
  return 0;
}
