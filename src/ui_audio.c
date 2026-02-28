#include "ui_audio.h"

#include <math.h>
#include <psp2/audioout.h>
#include <stdint.h>
#include <string.h>

enum {
  UI_AUDIO_SAMPLES = 256,
  UI_AUDIO_RATE = 48000
};

static void fill_tone(int16_t *pcm, float freq_hz, float gain) {
  for (int i = 0; i < UI_AUDIO_SAMPLES; i++) {
    const float t = (float)i / (float)UI_AUDIO_RATE;
    const float s = sinf(2.0f * 3.14159265f * freq_hz * t) * gain;
    const int16_t v = (int16_t)(s * 32767.0f);
    pcm[i * 2 + 0] = v;
    pcm[i * 2 + 1] = v;
  }
}

void ui_audio_init(UiAudio *audio) {
  memset(audio, 0, sizeof(*audio));
  audio->port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, UI_AUDIO_SAMPLES, UI_AUDIO_RATE, SCE_AUDIO_OUT_MODE_STEREO);
  audio->enabled = (audio->port >= 0) ? 1 : 0;
}

void ui_audio_term(UiAudio *audio) {
  if (audio->port >= 0) {
    sceAudioOutReleasePort(audio->port);
  }
  audio->port = -1;
  audio->enabled = 0;
}

void ui_audio_event(UiAudio *audio, UiAudioEvent event) {
  if (!audio->enabled || audio->port < 0) {
    return;
  }

  float freq = 440.0f;
  float gain = 0.12f;
  if (event == UI_AUDIO_NAV) {
    freq = 520.0f;
  } else if (event == UI_AUDIO_SCAN_TOGGLE) {
    freq = 620.0f;
  } else if (event == UI_AUDIO_HOST_NEW) {
    freq = 740.0f;
  } else if (event == UI_AUDIO_ERROR) {
    freq = 220.0f;
    gain = 0.16f;
  } else if (event == UI_AUDIO_EXPORT_OK) {
    freq = 680.0f;
  } else if (event == UI_AUDIO_EXPORT_FAIL) {
    freq = 180.0f;
    gain = 0.16f;
  }

  int16_t pcm[UI_AUDIO_SAMPLES * 2];
  fill_tone(pcm, freq, gain);
  (void)sceAudioOutOutput(audio->port, pcm);
}
