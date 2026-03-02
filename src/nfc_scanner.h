#ifndef NFC_SCANNER_H
#define NFC_SCANNER_H

#include <stdint.h>

#define NFC_SCANNER_MAX_TAGS 32
#define NFC_TAG_UID_MAX      10

typedef struct NfcTagRecord {
  uint8_t  uid[NFC_TAG_UID_MAX];
  uint8_t  uid_len;
  uint8_t  tag_type;    /* 1=NFC-A, 2=NFC-B, 3=NFC-F, 0=unknown */
  uint16_t atqa;
  uint8_t  sak;
  uint64_t last_seen_us;
  uint32_t seen_count;
  char     uid_str[32]; /* pre-formatted "XX:XX:XX:XX:XX:XX:XX" */
} NfcTagRecord;

typedef struct NfcScannerMetrics {
  uint8_t      hw_available;
  uint8_t      detecting;
  uint32_t     tag_count;
  uint32_t     total_seen;
  NfcTagRecord tags[NFC_SCANNER_MAX_TAGS];
  int          last_error;
} NfcScannerMetrics;

typedef struct NfcScanner {
  NfcScannerMetrics metrics;
  int      fd;           /* sceNfc fd, -1 if not open */
  uint64_t next_poll_us;
} NfcScanner;

void nfc_scanner_init(NfcScanner *s);
void nfc_scanner_term(NfcScanner *s);
void nfc_scanner_poll(NfcScanner *s, uint64_t now_us);
int  nfc_scanner_set_detecting(NfcScanner *s, int enabled);
void nfc_scanner_clear(NfcScanner *s);
void nfc_scanner_get_metrics(const NfcScanner *s, NfcScannerMetrics *out);

#endif
