#ifndef UI_FX_H
#define UI_FX_H

#include <stdint.h>

typedef struct UiFxState {
  int from_screen;
  int to_screen;
  uint8_t transitioning;
  uint64_t transition_start_us;
  float transition_t;
  float pulse;
  float scanline_phase;
} UiFxState;

void ui_fx_init(UiFxState *fx, int screen);
void ui_fx_on_screen_change(UiFxState *fx, int from, int to, uint64_t now_us);
void ui_fx_tick(UiFxState *fx, uint64_t now_us);

#endif
