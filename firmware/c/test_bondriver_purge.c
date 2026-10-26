/* Execute the real adapter's purge/receive synchronization with a controlled
 * backend. No USB, firmware, card or key processing is simulated here. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../../bondriver/BonDriver_Torne.c"
#include <assert.h>

struct TorneNative { TorneStatus status; LONG reads; BOOL fail; HANDLE entered, complete; };
TorneNative *torne_create(const WCHAR *a, const WCHAR *b, const WCHAR *c, DWORD *error) {
    (void)a; (void)b; (void)c; *error=ERROR_NOT_SUPPORTED; return NULL;
}
BOOL torne_use_firmware(TorneNative *s, const WCHAR *p) { (void)s; (void)p; return FALSE; }
BOOL torne_open(TorneNative *s) { (void)s; return FALSE; }
BOOL torne_tune(TorneNative *s, DWORD p) { (void)s; (void)p; return FALSE; }
BOOL torne_close(TorneNative *s) { s->status.streaming=FALSE; s->status.state=1; return TRUE; }
void torne_destroy(TorneNative *s) { (void)s; }
void torne_status(TorneNative *s, TorneStatus *status) { *status=s->status; }
BOOL torne_purge(TorneNative *s) {
    s->status.error=s->fail ? ERROR_IO_DEVICE : 0;
    return !s->fail;
}
BOOL torne_receive(TorneNative *s, BYTE *data, DWORD capacity, DWORD *size) {
    assert(capacity >= 188);
    LONG n=InterlockedIncrement(&s->reads);
    if (n == 1) {
        SetEvent(s->entered);
        assert(WaitForSingleObject(s->complete, 2000) == WAIT_OBJECT_0);
    }
    memset(data, 0, 188); data[0]=0x47; data[2]=(BYTE)(n == 1 ? 0x11 : 0x22); data[3]=0x10;
    *size=188; Sleep(1); return TRUE;
}
static DWORD WINAPI request_purge(LPVOID p) { purge((Driver*)p); return 0; }
static void initialize(Driver *d, TorneNative *native) {
    memset(d, 0, sizeof(*d));
    InitializeCriticalSection(&d->lock); InitializeCriticalSection(&d->usb_lock);
    InitializeCriticalSection(&d->lifecycle_lock);
    d->ready=CreateEventW(NULL, TRUE, FALSE, NULL);
    d->stop_event=CreateEventW(NULL, TRUE, FALSE, NULL);
    d->receive_gate=CreateEventW(NULL, TRUE, TRUE, NULL);
    d->queue=HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, QUEUE_COUNT*sizeof(Block));
    assert(d->ready && d->stop_event && d->receive_gate && d->queue);
    d->native=native; d->opened=TRUE; d->running=1;
}
static void dispose(Driver *d) {
    close_tuner(d);
    CloseHandle(d->ready); CloseHandle(d->stop_event); CloseHandle(d->receive_gate);
    DeleteCriticalSection(&d->lock); DeleteCriticalSection(&d->usb_lock);
    DeleteCriticalSection(&d->lifecycle_lock); HeapFree(GetProcessHeap(), 0, d->queue);
}
int main(void) {
    for (unsigned trial=0; trial<32; trial++) {
        TorneNative native={0}; Driver d;
        native.entered=CreateEventW(NULL, TRUE, FALSE, NULL);
        native.complete=CreateEventW(NULL, TRUE, FALSE, NULL);
        native.status.streaming=TRUE; initialize(&d, &native);
        d.thread=CreateThread(NULL, 0, receive_thread, &d, 0, NULL); assert(d.thread);
        assert(WaitForSingleObject(native.entered, 2000) == WAIT_OBJECT_0);
        HANDLE purger=CreateThread(NULL, 0, request_purge, &d, 0, NULL); assert(purger);
        ULONGLONG deadline=GetTickCount64()+2000;
        while (WaitForSingleObject(d.receive_gate, 0) != WAIT_TIMEOUT) {
            assert(GetTickCount64() < deadline); Sleep(1);
        }
        // Finish a read that began before purge acquired the USB lock.
        SetEvent(native.complete);
        assert(WaitForSingleObject(purger, 2000) == WAIT_OBJECT_0); CloseHandle(purger);
        assert(d.device_purges == 1);
        BYTE packet[1316]; DWORD size=0, remain=0;
        for (unsigned n=0; n<16; n++) {
            assert(wait_stream(&d, 2000) == WAIT_OBJECT_0);
            assert(get_stream_copy(&d, packet, &size, &remain));
            assert(size == 188 && packet[2] == 0x22);
        }
        dispose(&d); CloseHandle(native.entered); CloseHandle(native.complete);
    }
    TorneNative native={0}; Driver d; native.fail=TRUE; initialize(&d, &native);
    d.queue[0].size=188; d.count=1; SetEvent(d.ready);
    purge(&d);
    assert(!d.running && !d.count && d.last_error == ERROR_IO_DEVICE && !d.device_purges);
    assert(wait_stream(&d, 0) == WAIT_OBJECT_0); // failure wakes the consumer
    dispose(&d);
    puts("Purge: 32 in-flight read races, failure wakeup and closed cleanup passed; no USB access");
    return 0;
}
