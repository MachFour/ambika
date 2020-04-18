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
// Special UI page for displaying versioning information.

#ifndef CONTROLLER_UI_PAGES_VERSION_MANAGER_H_
#define CONTROLLER_UI_PAGES_VERSION_MANAGER_H_

#include "controller/ui_pages/ui_page.h"

namespace ambika {

class VersionManager : public UiPage {
 public:
  VersionManager() = default;
  
  static void OnInit(PageInfo* info);
  
  static uint8_t OnKey(uint8_t key);
  static void UpdateScreen();
  static void UpdateLeds();

  static constexpr EventHandlers event_handlers_ PROGMEM = {
      OnInit,
      SetActiveControl,
      OnIncrement,
      OnClick,
      OnPot,
      OnKey,
      nullptr,
      OnIdle,
      UpdateScreen,
      UpdateLeds,
      OnDialogClosed,
  };

 private:
  DISALLOW_COPY_AND_ASSIGN(VersionManager);
};

}  // namespace ambika

#endif  // CONTROLLER_UI_PAGES_VERSION_MANAGER_H_
