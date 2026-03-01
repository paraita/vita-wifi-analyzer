#include "ui_fx.h"

#include <math.h>
#include <string.h>

void ui_fx_init(UiFxState *fx, int screen) {
  memset(fx, 0, sizeof(*fx));
  fx->from_screen = screen;
  fx->to_screen = screen;
}

void ui_fx_on_screen_change(UiFxState *fx, int from, int to, uint64_t now_us) {
  fx->from_screen = from;
  fx->to_screen = to;
  fx->transitioning = 1;
  fx->transition_start_us = now_us;
  fx->transition_t = 0.0f;
}

void ui_fx_tick(UiFxState *fx, uint64_t now_us) {
  const float t = (float)(now_us % 1000000ULL) / 1000000.0f;
  fx->pulse = 0.5f + 0.5f * sinf(t * 6.283185f);
  fx->scanline_phase = (float)(now_us % 2000000ULL) / 2000000.0f;

  if (fx->transitioning) {
    const uint64_t elapsed = now_us - fx->transition_start_us;
    fx->transition_t = (float)elapsed / 150000.0f;
    if (fx->transition_t >= 1.0f) {
      fx->transition_t = 1.0f;
      fx->transitioning = 0;
    }
  }
}
