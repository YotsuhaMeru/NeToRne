#ifndef TORNE_NATIVE_H
#define TORNE_NATIVE_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct TorneNative TorneNative;
typedef struct {
    DWORD error, state, sequence;
    BOOL streaming, media_closed;
    ULONGLONG usb_bytes, packets, encrypted_packets, authentications, discarded_bytes;
    DWORD input_packet_bytes, es_selection_mode, pmt_output_mode;
    BOOL psi_settings_verified;
    DWORD valid_pmt_sections, invalid_psi_crc, fullseg_bml_programs, oneseg_bml_programs, dsmcc_es;
    DWORD firmware_variant; /* 0 original, 1 preserve-es, 2 psi-passthrough, 3 C raw preserved, 4 C CRC wire, 5 upstream routing + CRC */
    ULONGLONG crc_verified_packets;
    char operation[64];
    DWORD card_api_calls;
    BOOL bcas_skipped;
    char failure_reason[96], io_operation[128];
    DWORD io_error, api_command, api_sequence;
    DWORD rf_status, rf_search, rf_viterbi[3];
    BOOL rf_snapshot_valid;
    ULONGLONG rf_snapshot_tick;
    float cnr_db;
    BOOL cnr_valid;
    DWORD cnr_noise_raw, cnr_signal_raw, cnr_error;
    ULONGLONG cnr_tick;
} TorneStatus;
typedef void (*TorneLogCallback)(void *context, const char *event, const char *detail);
void torne_set_logger(TorneNative *session, TorneLogCallback logger, void *context);
TorneNative *torne_create(const WCHAR *firmware_directory, const WCHAR *module_path,
                          const WCHAR *media_path, DWORD *error);
BOOL torne_use_firmware(TorneNative *session, const WCHAR *path);
BOOL torne_open(TorneNative *session);
BOOL torne_tune(TorneNative *session, DWORD physical);
BOOL torne_receive(TorneNative *session, BYTE *data, DWORD capacity, DWORD *size);
BOOL torne_purge(TorneNative *session);
BOOL torne_close(TorneNative *session);
void torne_destroy(TorneNative *session);
void torne_status(TorneNative *session, TorneStatus *status);
#ifdef __cplusplus
}
#endif
#endif
