#ifndef UI_AUDIO_H
#define UI_AUDIO_H

#include <stdint.h>

typedef enum UiAudioEvent {
  UI_AUDIO_NAV = 0,
  UI_AUDIO_SCAN_TOGGLE = 1,
  UI_AUDIO_HOST_NEW = 2,
  UI_AUDIO_ERROR = 3,
  UI_AUDIO_EXPORT_OK = 4,
  UI_AUDIO_EXPORT_FAIL = 5
} UiAudioEvent;

typedef struct UiAudio {
  int port;
  int enabled;
} UiAudio;

void ui_audio_init(UiAudio *audio);
void ui_audio_term(UiAudio *audio);
void ui_audio_event(UiAudio *audio, UiAudioEvent event);

#endif
