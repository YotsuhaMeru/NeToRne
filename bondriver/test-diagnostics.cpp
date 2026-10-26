/* Exercise real native error reporting with fake WinUSB calls. No device access. */
#include <windows.h>
#include <winusb.h>
#undef NDEBUG
#include <cassert>
#include <cstring>
#include <string>
static DWORD control_error = 0, read_error = 0;
static unsigned i2c_reg = 0;
static unsigned noise_raw = 1u << 6, signal_raw = 100u << 6;
static bool mismatched_completion = false, fast_clock = false;
static ULONGLONG ticks = 0;
static BOOL WINAPI fake_control(WINUSB_INTERFACE_HANDLE, WINUSB_SETUP_PACKET setup, PUCHAR data,
                               ULONG length, PULONG got, LPOVERLAPPED) {
    *got = 0;
    if (control_error) { SetLastError(control_error); return FALSE; }
    if (setup.RequestType == 0x40 && setup.Request == 0xbd && length >= 2) i2c_reg = (data[0] << 8) | data[1];
    if (setup.RequestType & 0x80) {
        memset(data, 0, length);
        if (setup.Request == 0xbd && length == 2) {
            unsigned value = setup.Value == 15 ? 0x0808 : i2c_reg == 0x238 ? 0x0100 :
                i2c_reg == 0x21e ? noise_raw : i2c_reg == 0x21f ? signal_raw : 0;
            data[0] = BYTE(value >> 8); data[1] = BYTE(value);
        }
    }
    *got = length; return TRUE;
}
static BOOL WINAPI fake_read(WINUSB_INTERFACE_HANDLE, UCHAR, PUCHAR data, ULONG length, PULONG got, LPOVERLAPPED) {
    *got = 0;
    if (read_error) { SetLastError(read_error); return FALSE; }
    if (mismatched_completion && length >= 6) {
        memset(data, 0, 6); data[0] = 0x10; data[3] = 1; data[4] = 3; *got = 6;
    }
    return TRUE;
}
static ULONGLONG WINAPI fake_tick() { if (fast_clock) return ticks += 1000; return GetTickCount64(); }
#define WinUsb_ControlTransfer fake_control
#define WinUsb_ReadPipe fake_read
#define GetTickCount64 fake_tick
#include "TorneNative.cpp"
#undef WinUsb_ControlTransfer
#undef WinUsb_ReadPipe
#undef GetTickCount64

static void capture(void *context, const char *event, const char *detail) {
    auto &out = *static_cast<std::string *>(context);
    out += event; out += ' '; out += detail; out += '\n';
}
int wmain(int argc, WCHAR **argv) {
    assert(argc == 2);
    std::wstring root(argv[1]);
    TorneNative s((root + L"\\BCJB95006\\USRDIR").c_str(), (root + L"\\research\\decrypted\\PvrRecSvr.prx").c_str(),
                  (root + L"\\research\\decrypted\\libM2tsAes.elf").c_str());
    std::string log;
    torne_set_logger(&s, capture, &log);
    control_error = ERROR_BAD_COMMAND;
    assert(!guarded(&s, [&] { s.stop_stream(); }));
    assert(s.status.error == 22);
    assert(strcmp(s.status.failure_reason, "usb_control_transfer_failed") == 0);
    assert(log.find("operation=stop_stream.read_state") != std::string::npos);
    assert(log.find("request=0xbc") != std::string::npos && log.find("index=0x2008") != std::string::npos);
    assert(log.find("ERROR_BAD_COMMAND") != std::string::npos);
    assert(!guarded(&s, [&] { s.dr(0x238); }));
    assert(log.find("i2c_error device=0x90 register=0x0238") != std::string::npos);
    control_error = 0; read_error = ERROR_BAD_COMMAND;
    assert(!guarded(&s, [&] { s.pipe(0x81, 32768); }));
    assert(log.find("read_pipe endpoint=0x81") != std::string::npos);
    read_error = 0;
    assert(guarded(&s, [&] { s.start_stream(); }));
    assert(!s.status.streaming && s.status.rf_snapshot_valid && s.status.rf_status == 0x100);
    assert(strcmp(s.status.failure_reason, "no_rf_lock") == 0);
    assert(log.find("no_rf_lock search504=") != std::string::npos);
    s.sample_signal(true);
    assert(s.status.cnr_valid && std::fabs(s.status.cnr_db - 20.0f) < 0.001f);
    assert(s.status.cnr_noise_raw == noise_raw && s.status.cnr_signal_raw == signal_raw);
    float db = 0;
    assert(TorneNative::decode_cnr((10u << 6) | 54u, 100u << 6, db));
    assert(std::fabs(db - 40.1029996f) < 0.001f);
    assert(TorneNative::decode_cnr(100u << 6, 10u << 6, db) && std::fabs(db + 10.0f) < 0.001f);
    assert(TorneNative::decode_cnr((1u << 6) | 32u, (1u << 6) | 31u, db));
    assert(std::fabs(db - 189.648897f) < 0.001f);
    assert(!TorneNative::decode_cnr(0, signal_raw, db) && !TorneNative::decode_cnr(noise_raw, 0, db));
    control_error = ERROR_BAD_COMMAND;
    s.status.error = 0; s.sample_signal(true);
    assert(!s.status.cnr_valid && s.status.cnr_error == ERROR_BAD_COMMAND && s.status.error == 0);
    control_error = 0; s.sample_signal(false); assert(!s.status.cnr_valid);
    mismatched_completion = true;
    assert(!guarded(&s, [&] { s.api(3); }));
    assert(strcmp(s.status.failure_reason, "api_completion_mismatch_or_rejected") == 0);
    mismatched_completion = false; fast_clock = true;
    assert(!guarded(&s, [&] { s.api(3); }));
    assert(s.status.error == ERROR_TIMEOUT && strcmp(s.status.failure_reason, "api_completion_timeout") == 0);
    assert(log.find("api_timeout") != std::string::npos);
    fast_clock = false; control_error = ERROR_BAD_COMMAND;
    const char *payload = "FORBIDDEN_TEST_PAYLOAD";
    assert(!guarded(&s, [&] { s.control(0x40, 0xbc, 0, 0, Bytes(payload, payload + strlen(payload))); }));
    assert(log.find(payload) == std::string::npos);
    puts("PASS: USB/stop/I2C diagnostics, API timeout, payload omission; C/N register conversion, signed exponent boundaries, invalid samples, optional read failure; no USB access");
}
