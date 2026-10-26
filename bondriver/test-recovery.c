/* Fault injection tests for the real BonDriver worker, without USB access. */
#include <windows.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "TorneNative.h"
static DWORD WINAPI fast_wait(HANDLE event, DWORD timeout) {
    return WaitForSingleObject(event, timeout == 3000 ? 10 : timeout);
}
#define WaitForSingleObject fast_wait
#include "BonDriver_Torne.c"
#undef WaitForSingleObject

static TorneStatus fake_status;
static volatile LONG tune_calls, receive_error, failed_tunes;
void torne_set_logger(TorneNative *s, TorneLogCallback logger, void *context) { (void)s; (void)logger; (void)context; }
TorneNative *torne_create(const WCHAR *a, const WCHAR *b, const WCHAR *c, DWORD *e) {
    (void)a; (void)b; (void)c; *e = 0; return (TorneNative *)1;
}
BOOL torne_open(TorneNative *s) { (void)s; return TRUE; }
BOOL torne_use_firmware(TorneNative *s, const WCHAR *p) { (void)s; (void)p; return TRUE; }
BOOL torne_tune(TorneNative *s, DWORD ch) {
    (void)s; (void)ch;
    LONG call = InterlockedIncrement(&tune_calls);
    BOOL ok = call > failed_tunes;
    fake_status.error = ok ? 0 : ERROR_GEN_FAILURE;
    fake_status.streaming = ok;
    return ok;
}
BOOL torne_receive(TorneNative *s, BYTE *p, DWORD n, DWORD *len) {
    (void)s; (void)p; (void)n; *len = 0;
    fake_status.error = tune_calls > failed_tunes ? 0 : (DWORD)receive_error;
    return fake_status.error == 0;
}
BOOL torne_purge(TorneNative *s) { (void)s; fake_status.error = ERROR_GEN_FAILURE; return FALSE; }
BOOL torne_close(TorneNative *s) { (void)s; return TRUE; }
void torne_destroy(TorneNative *s) { (void)s; }
void torne_status(TorneNative *s, TorneStatus *p) { (void)s; *p = fake_status; }

static void init_driver(Driver *d) {
    memset(d, 0, sizeof(*d)); memset(&fake_status, 0, sizeof(fake_status));
    InitializeCriticalSection(&d->lock); InitializeCriticalSection(&d->usb_lock);
    InitializeCriticalSection(&d->lifecycle_lock);
    d->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    d->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    d->receive_gate = CreateEventW(NULL, TRUE, TRUE, NULL);
    d->native = (TorneNative *)1; d->opened = TRUE; d->running = 1; d->channel = 7;
    tune_calls = 0; failed_tunes = 4; receive_error = ERROR_TIMEOUT;
}
static void dispose_driver(Driver *d) {
    close_tuner(d);
    free_error_records(d);
    CloseHandle(d->ready); CloseHandle(d->stop_event); CloseHandle(d->receive_gate);
    DeleteCriticalSection(&d->lock); DeleteCriticalSection(&d->usb_lock);
    DeleteCriticalSection(&d->lifecycle_lock);
}
static void wait_recovered(Driver *d) {
    ULONGLONG deadline = GetTickCount64() + 2000;
    BOOL recovered = FALSE;
    do {
        EnterCriticalSection(&d->usb_lock);
        recovered = tune_calls >= 5 && d->no_signal_recovery_attempts > 0 && !d->recovery_pending && d->status_native.streaming;
        LeaveCriticalSection(&d->usb_lock);
        if (recovered) break;
        Sleep(5);
    } while (GetTickCount64() < deadline);
    assert(recovered); assert(d->running); assert(d->last_error == 0);
}
int main(void) {
    Driver d;
    for (unsigned i = 0; i < 2; ++i) {
        init_driver(&d); receive_error = i ? ERROR_GEN_FAILURE : ERROR_TIMEOUT;
        d.thread = CreateThread(NULL, 0, receive_thread, &d, 0, NULL);
        wait_recovered(&d); dispose_driver(&d);
    }
    init_driver(&d);
    assert(!set_channel(&d, 0, 9));
    assert(d.running && d.channel == 9 && d.recovery_pending);
    d.thread = CreateThread(NULL, 0, receive_thread, &d, 0, NULL);
    wait_recovered(&d); dispose_driver(&d);
    init_driver(&d); fake_status.streaming = TRUE;
    purge(&d); assert(d.running && d.recovery_pending);
    d.thread = CreateThread(NULL, 0, receive_thread, &d, 0, NULL);
    wait_recovered(&d); dispose_driver(&d);
    init_driver(&d); failed_tunes = 1000000;
    d.thread = CreateThread(NULL, 0, receive_thread, &d, 0, NULL);
    Sleep(30); ULONGLONG start = GetTickCount64(); dispose_driver(&d);
    assert(GetTickCount64() - start < 1000);
    puts("PASS: timeout and USB errors retry beyond three failures; failed selection and purge recover; close interrupts retries");
    return 0;
}
