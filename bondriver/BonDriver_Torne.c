/* x64 BonDriver2 adapter for the locally analyzed CECH-ZD1J receiver.
 * MSVC RTTI layout adapted from Chinachu/BonDriver_Mirakurun/src/rtti.rs.
 * See THIRD_PARTY_LICENSES.txt for its MIT license.
 * Native WinUSB protocol and media conversion are in TorneNative.cpp.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include "TorneNative.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>

#ifndef _WIN64
#error This BonDriver implements the Windows x64 ABI only.
#endif
#define QUEUE_COUNT 2048
#define BLOCK_SIZE 1316

typedef struct Driver Driver;
typedef struct ErrorRecord ErrorRecord;
struct ErrorRecord {
    ErrorRecord *next;
    DWORD id;
    ULONGLONG repeats;
    uint64_t signature;
    char key[512];
};
typedef struct { DWORD size; BYTE data[BLOCK_SIZE]; } Block;
struct Driver {
    void **vtable;
    CRITICAL_SECTION lock, usb_lock, lifecycle_lock;
    HANDLE ready, thread, owner, stop_event, receive_gate;
    TorneNative *native;
    TorneStatus status_native;
    volatile LONG running;
    BOOL opened, acquired;
    DWORD channel, last_error;
    DWORD head, count;
    ULONGLONG received, dropped, device_purges;
    ULONGLONG no_signal_recovery_attempts;
    DWORD last_recovery_error;
    BOOL recovery_pending;
    BOOL recovery_awaiting_ts;
    WCHAR recovery_log[MAX_PATH];
    WCHAR diagnostic_log[MAX_PATH];
    BOOL detailed_logging;
    BOOL log_batch;
    DWORD log_batch_size, log_batch_dropped, next_error_id;
    char log_batch_data[32768], session_id[80];
    ErrorRecord *errors;
    volatile LONG signal_millidb;
    volatile LONG64 signal_tick;
    Block *queue;
    BYTE last[BLOCK_SIZE];
    WCHAR firmware[MAX_PATH], pvr[MAX_PATH], media[MAX_PATH], operational[MAX_PATH];
    WCHAR channel_names[40][32];
};
static HMODULE module;
static SRWLOCK factory_lock = SRWLOCK_INIT;
static Driver *instance;
static BYTE rtti[0x160];
static INIT_ONCE vtable_once = INIT_ONCE_STATIC_INIT;

static BOOL open_tuner(Driver *d);
static void close_tuner(Driver *d);
static void close_tuner_locked(Driver *d);
static BOOL set_channel(Driver *d, DWORD space, DWORD channel);
static void clear_queue(Driver *d);
static void free_error_records(Driver *d) {
    while (d->errors) { ErrorRecord *next = d->errors->next; HeapFree(GetProcessHeap(), 0, d->errors); d->errors = next; }
}
static uint64_t error_signature(const char *key) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)key; *p; ++p) hash = (hash ^ *p) * UINT64_C(1099511628211);
    return hash;
}
static void restore_error_history(Driver *d) {
    // PID plus process creation time identifies one TVTest launch, including
    // driver/DLL reloads. Restore only that session's IDs and retry counters.
    FILE *file = _wfopen(d->recovery_log, L"rb");
    if (!file) return;
    char session[100], line[1600]; snprintf(session, sizeof(session), "session=%s ", d->session_id);
    while (fgets(line, sizeof(line), file)) {
        if (!strstr(line, session)) continue;
        const char *signature_text = strstr(line, " signature="), *id_text = strstr(line, " error_id=");
        if (!signature_text || !id_text) continue;
        uint64_t signature = _strtoui64(signature_text + 11, NULL, 16);
        DWORD id = (DWORD)strtoul(id_text + 10, NULL, 10);
        if (!id) continue;
        ErrorRecord *record;
        for (record = d->errors; record && record->signature != signature; record = record->next) { }
        if (!record) {
            record = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*record));
            if (!record) break;
            record->id = id; record->signature = signature; record->next = d->errors; d->errors = record;
        }
        const char *count = strstr(line, " retries=");
        if (count) record->repeats = _strtoui64(count + 9, NULL, 10);
        if (id > d->next_error_id) d->next_error_id = id;
    }
    fclose(file);
}
static void write_diagnostic(Driver *d, const char *data, DWORD size) {
    if (!d->detailed_logging || !d->diagnostic_log[0]) return;
    HANDLE file = CreateFileW(d->diagnostic_log, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER length;
    if (GetFileSizeEx(file, &length) && length.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(file);
        WCHAR previous[MAX_PATH];
        if (_snwprintf(previous, MAX_PATH, L"%ls.1", d->diagnostic_log) >= 0)
            MoveFileExW(d->diagnostic_log, previous, MOVEFILE_REPLACE_EXISTING);
        file = CreateFileW(d->diagnostic_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) return;
    }
    DWORD written;
    WriteFile(file, data, size, &written, NULL);
    CloseHandle(file);
}
static void diagnostic_log(void *context, const char *event, const char *detail) {
    Driver *d = context;
    if (!d->detailed_logging) return;
    SYSTEMTIME now; GetLocalTime(&now);
    char line[1600];
    int size = snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u.%03u session=%s pid=%lu tid=%lu event=%s %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        d->session_id, (unsigned long)GetCurrentProcessId(), (unsigned long)GetCurrentThreadId(), event, detail);
    if (size <= 0 || size >= (int)sizeof(line)) return;
    if (d->log_batch) {
        if (d->log_batch_size + (DWORD)size <= sizeof(d->log_batch_data)) {
            memcpy(d->log_batch_data + d->log_batch_size, line, (size_t)size);
            d->log_batch_size += (DWORD)size;
        } else d->log_batch_dropped++;
    } else write_diagnostic(d, line, (DWORD)size);
}
static void begin_log_batch(Driver *d) {
    d->log_batch = TRUE; d->log_batch_size = d->log_batch_dropped = 0;
}
static void finish_log_batch(Driver *d, BOOL keep) {
    d->log_batch = FALSE;
    if (keep && d->log_batch_size) write_diagnostic(d, d->log_batch_data, d->log_batch_size);
    if (keep && d->log_batch_dropped) diagnostic_log(d, "batch_truncated", "diagnostic event buffer limit reached");
    d->log_batch_size = d->log_batch_dropped = 0;
}
static void refresh_status(Driver *d) {
    torne_status(d->native, &d->status_native);
    TorneStatus *s = &d->status_native;
    InterlockedExchange64(&d->signal_tick, (LONG64)s->cnr_tick);
    InterlockedExchange(&d->signal_millidb, !s->error && s->streaming && s->cnr_valid ? (LONG)(s->cnr_db * 1000.0f) : LONG_MIN);
}
static void log_recovery(Driver *d, const char *event) {
    if (!strcmp(event, "retry_started")) return;
    BOOL failure = strcmp(event, "stream_started") && strcmp(event, "ts_resumed");
    BOOL repeated = FALSE;
    ErrorRecord *record = NULL;
    DWORD error = d->last_error ? d->last_error : ERROR_TIMEOUT;
    if (failure) {
        char key[512];
        snprintf(key, sizeof(key), "%lu|%lu|%s|%s|%s", (unsigned long)d->channel, (unsigned long)error,
            d->status_native.operation, d->status_native.failure_reason,
            d->status_native.io_error ? d->status_native.io_operation : "");
        uint64_t signature = error_signature(key);
        for (record = d->errors; record && (record->signature != signature || (record->key[0] && strcmp(record->key, key))); record = record->next) { }
        if (record) { record->repeats++; repeated = TRUE; }
        else {
            record = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*record));
            if (record) {
                memcpy(record->key, key, strlen(key) + 1); record->id = ++d->next_error_id;
                record->signature = signature;
                record->next = d->errors; d->errors = record;
            }
        }
    }
    finish_log_batch(d, !repeated);
    SYSTEMTIME now; GetLocalTime(&now);
    char line[800];
    int size;
    if (repeated) size = snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u session=%s error_retry error_id=%lu retries=%llu signature=%016llx\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, d->session_id,
        (unsigned long)record->id, (unsigned long long)record->repeats, (unsigned long long)record->signature);
    else size = snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u session=%s %s error_id=%lu signature=%016llx channel=%lu attempt=%llu error=%lu streaming=%d operation=%s reason=%s state=0x%04lx\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        d->session_id, event, (unsigned long)(record ? record->id : 0), (unsigned long long)(record ? record->signature : 0), (unsigned long)(d->channel < 40 ? d->channel + 13 : 0),
        (unsigned long long)d->no_signal_recovery_attempts, (unsigned long)(failure ? error : 0),
        d->status_native.streaming, d->status_native.operation, d->status_native.failure_reason, (unsigned long)d->status_native.state);
    if (size > 0 && size < (int)sizeof(line)) {
        line[size - 2] = 0; diagnostic_log(d, "recovery", line); line[size - 2] = '\r';
    }
    DWORD written;
    HANDLE file = CreateFileW(d->recovery_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    if (size > 0 && size < (int)sizeof(line)) WriteFile(file, line, (DWORD)size, &written, NULL);
    CloseHandle(file);
}
static BOOL recover_reception(Driver *d) {
    // Keep retries interruptible by close, diagnostics and a new channel.
    BOOL first_attempt = TRUE;
    for (;;) {
        if (!InterlockedCompareExchange(&d->running, 0, 0)) return FALSE;
        if (!first_attempt && WaitForSingleObject(d->stop_event, 3000) == WAIT_OBJECT_0) return FALSE;
        first_attempt = FALSE;
        EnterCriticalSection(&d->usb_lock);
        if (!InterlockedCompareExchange(&d->running, 0, 0) ||
            WaitForSingleObject(d->stop_event, 0) == WAIT_OBJECT_0) {
            LeaveCriticalSection(&d->usb_lock);
            return FALSE;
        }
        if (WaitForSingleObject(d->receive_gate, 0) != WAIT_OBJECT_0) {
            LeaveCriticalSection(&d->usb_lock);
            return TRUE;
        }
        DWORD channel = d->channel;
        d->no_signal_recovery_attempts++;
        log_recovery(d, "retry_started");
        InterlockedExchange(&d->signal_millidb, LONG_MIN);
        begin_log_batch(d);
        BOOL ok = channel < 40 && torne_tune(d->native, channel + 13);
        refresh_status(d);
        if (ok && d->status_native.streaming) {
            d->last_error = 0;
            d->last_recovery_error = 0;
            d->recovery_pending = FALSE;
            d->recovery_awaiting_ts = TRUE;
            clear_queue(d);
            log_recovery(d, "stream_started");
            LeaveCriticalSection(&d->usb_lock);
            return TRUE;
        }
        d->last_recovery_error = d->status_native.error ? d->status_native.error : ERROR_TIMEOUT;
        d->last_error = d->last_recovery_error;
        d->recovery_pending = TRUE;
        log_recovery(d, "retry_failed");
        LeaveCriticalSection(&d->usb_lock);
    }
}
static void clear_queue(Driver *d) {
    EnterCriticalSection(&d->lock);
    d->head = d->count = 0;
    ResetEvent(d->ready);
    LeaveCriticalSection(&d->lock);
}
static DWORD WINAPI receive_thread(LPVOID parameter) {
    Driver *d = parameter;
    BYTE data[65536];
    while (InterlockedCompareExchange(&d->running, 0, 0)) {
        HANDLE events[] = {d->stop_event, d->receive_gate};
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
        DWORD length = 0;
        EnterCriticalSection(&d->usb_lock);
        if (!InterlockedCompareExchange(&d->running, 0, 0)) { LeaveCriticalSection(&d->usb_lock); break; }
        if (WaitForSingleObject(d->receive_gate, 0) != WAIT_OBJECT_0) { LeaveCriticalSection(&d->usb_lock); continue; }
        BOOL was_pending = d->recovery_pending;
        begin_log_batch(d);
        BOOL ok = !was_pending && torne_receive(d->native, data, sizeof(data), &length);
        refresh_status(d);
        if (!ok) {
            d->last_error = d->status_native.error ? d->status_native.error : ERROR_TIMEOUT;
            if (d->channel < 40) {
                d->recovery_pending = TRUE;
                if (!was_pending) log_recovery(d, "receive_failed");
                else finish_log_batch(d, FALSE);
                clear_queue(d);
                LeaveCriticalSection(&d->usb_lock);
                if (recover_reception(d)) continue;
                if (!InterlockedCompareExchange(&d->running, 0, 0)) break;
                InterlockedExchange(&d->running, 0);
                SetEvent(d->ready);
                break;
            }
            InterlockedExchange(&d->running, 0);
            log_recovery(d, "receive_failed");
            LeaveCriticalSection(&d->usb_lock);
            SetEvent(d->ready);
            break;
        }
        if (length && d->recovery_awaiting_ts) {
            d->recovery_awaiting_ts = FALSE;
            log_recovery(d, "ts_resumed");
        }
        finish_log_batch(d, FALSE);
        EnterCriticalSection(&d->lock);
        for (DWORD offset = 0; offset < length; offset += BLOCK_SIZE) {
            DWORD size = length - offset;
            if (size > BLOCK_SIZE) size = BLOCK_SIZE;
            if (d->count == QUEUE_COUNT) {
                d->dropped += d->queue[d->head].size;
                d->head = (d->head + 1) % QUEUE_COUNT;
                d->count--;
            }
            Block *block = &d->queue[(d->head + d->count) % QUEUE_COUNT];
            memcpy(block->data, data + offset, size);
            block->size = size;
            d->count++;
            d->received += size;
        }
        if (length) SetEvent(d->ready);
        LeaveCriticalSection(&d->lock);
        LeaveCriticalSection(&d->usb_lock);
        if (!length) WaitForSingleObject(d->stop_event, 25);
    }
    return 0;
}
static BOOL open_tuner(Driver *d) {
    EnterCriticalSection(&d->lifecycle_lock);
    if (d->opened) { LeaveCriticalSection(&d->lifecycle_lock); return TRUE; }
    d->last_error = 0;
    d->owner = CreateMutexW(NULL, FALSE, L"Local\\BonDriverTorne-054c04b2");
    if (!d->owner) { d->last_error = GetLastError(); LeaveCriticalSection(&d->lifecycle_lock); return FALSE; }
    DWORD ownership = WaitForSingleObject(d->owner, 0);
    if (ownership != WAIT_OBJECT_0 && ownership != WAIT_ABANDONED) {
        d->last_error = ERROR_BUSY;
        CloseHandle(d->owner); d->owner = NULL; LeaveCriticalSection(&d->lifecycle_lock); return FALSE;
    }
    d->acquired = TRUE;
    begin_log_batch(d);
    diagnostic_log(d, "driver_open", "build=2026-10-16-diagnostics-v2 architecture=x64 diagnosticFormat=2");
    const WCHAR *config_paths[] = {d->firmware, d->pvr, d->media, d->operational};
    const char *config_names[] = {"firmware_directory", "pvr_module", "media_module", "operational_firmware"};
    for (unsigned i = 0; i < 4; ++i) {
        char value[MAX_PATH * 3];
        if (WideCharToMultiByte(CP_UTF8, 0, config_paths[i], -1, value, sizeof(value), NULL, NULL)) diagnostic_log(d, config_names[i], value);
    }
    d->native = torne_create(d->firmware, d->pvr, d->media, &d->last_error);
    if (d->native && d->detailed_logging) torne_set_logger(d->native, diagnostic_log, d);
    if (!d->native || (d->operational[0] && !torne_use_firmware(d->native, d->operational)) || !torne_open(d->native)) {
        if (d->native) { refresh_status(d); d->last_error = d->status_native.error; }
        char failure[256]; snprintf(failure, sizeof(failure), "error=%lu operation=%s reason=%s", (unsigned long)d->last_error, d->status_native.operation, d->status_native.failure_reason);
        diagnostic_log(d, "driver_open_failed", failure);
        log_recovery(d, "open_failed");
        close_tuner_locked(d); LeaveCriticalSection(&d->lifecycle_lock); return FALSE;
    }
    ResetEvent(d->stop_event);
    SetEvent(d->receive_gate);
    InterlockedExchange(&d->running, 1);
    finish_log_batch(d, TRUE);
    d->thread = CreateThread(NULL, 0, receive_thread, d, 0, NULL);
    if (!d->thread) { d->last_error = GetLastError(); close_tuner_locked(d); LeaveCriticalSection(&d->lifecycle_lock); return FALSE; }
    d->opened = TRUE;
    LeaveCriticalSection(&d->lifecycle_lock); return TRUE;
}
static void close_tuner(Driver *d) {
    EnterCriticalSection(&d->lifecycle_lock);
    close_tuner_locked(d);
    LeaveCriticalSection(&d->lifecycle_lock);
}
static void close_tuner_locked(Driver *d) {
    InterlockedExchange(&d->signal_millidb, LONG_MIN);
    InterlockedExchange(&d->running, 0);
    SetEvent(d->stop_event);
    if (d->thread) { WaitForSingleObject(d->thread, INFINITE); CloseHandle(d->thread); d->thread = NULL; }
    EnterCriticalSection(&d->usb_lock);
    if (d->native) {
        begin_log_batch(d);
        BOOL ok = torne_close(d->native);
        refresh_status(d);
        if (!ok && !d->last_error) d->last_error = d->status_native.error;
        if (!ok) log_recovery(d, "close_failed"); else finish_log_batch(d, TRUE);
        torne_destroy(d->native); d->native = NULL;
    }
    LeaveCriticalSection(&d->usb_lock);
    d->opened = FALSE;
    if (d->owner) {
        if (d->acquired) ReleaseMutex(d->owner);
        CloseHandle(d->owner); d->owner = NULL; d->acquired = FALSE;
    }
    d->channel = 0xffffffff;
    d->recovery_pending = FALSE;
    d->recovery_awaiting_ts = FALSE;
    clear_queue(d);
}
static BOOL set_channel(Driver *d, DWORD space, DWORD channel) {
    if (space || channel >= 40) return FALSE;
    EnterCriticalSection(&d->lifecycle_lock);
    if (!d->opened || !InterlockedCompareExchange(&d->running, 0, 0)) { LeaveCriticalSection(&d->lifecycle_lock); return FALSE; }
    ResetEvent(d->receive_gate);
    EnterCriticalSection(&d->usb_lock);
    if (channel == d->channel && !d->recovery_pending && d->status_native.streaming && !d->last_error) { LeaveCriticalSection(&d->usb_lock); SetEvent(d->receive_gate); LeaveCriticalSection(&d->lifecycle_lock); return TRUE; }
    clear_queue(d);
    InterlockedExchange(&d->signal_millidb, LONG_MIN);
    begin_log_batch(d);
    BOOL ok = torne_tune(d->native, channel + 13);
    refresh_status(d);
    d->last_error = d->status_native.error;
    d->channel = channel;
    d->recovery_pending = !ok || !d->status_native.streaming;
    if (d->recovery_pending) log_recovery(d, "tune_pending");
    else finish_log_batch(d, TRUE);
    clear_queue(d);
    LeaveCriticalSection(&d->usb_lock);
    SetEvent(d->receive_gate);
    LeaveCriticalSection(&d->lifecycle_lock); return ok;
}
static void purge(Driver *d) {
    EnterCriticalSection(&d->lifecycle_lock);
    ResetEvent(d->receive_gate);
    EnterCriticalSection(&d->usb_lock);
    // The receive worker completes its current read before this lock is
    // granted. No old completion can enqueue after the final queue clear.
    clear_queue(d);
    if (d->native && d->opened && InterlockedCompareExchange(&d->running, 0, 0)) {
        InterlockedExchange(&d->signal_millidb, LONG_MIN);
        begin_log_batch(d);
        BOOL ok = torne_purge(d->native);
        refresh_status(d);
        d->last_error = d->status_native.error;
        if (ok) { d->device_purges++; finish_log_batch(d, TRUE); }
        else { d->recovery_pending = TRUE; log_recovery(d, "purge_failed"); }
    }
    clear_queue(d);
    // A failed purge must wake WaitTsStream even though the queue is empty.
    if (d->opened && !InterlockedCompareExchange(&d->running, 0, 0)) SetEvent(d->ready);
    LeaveCriticalSection(&d->usb_lock);
    SetEvent(d->receive_gate);
    LeaveCriticalSection(&d->lifecycle_lock);
}
static BOOL set_channel_byte(Driver *d, BYTE physical) {
    return physical >= 13 && physical <= 52 && set_channel(d, 0, physical - 13);
}
static float signal_level(Driver *d) {
    // The TVTest UI never waits for USB or for a retune: it reads only a cache.
    LONG value = InterlockedCompareExchange(&d->signal_millidb, 0, 0);
    LONG64 tick = InterlockedCompareExchange64(&d->signal_tick, 0, 0);
    if (value == LONG_MIN || tick <= 0 || GetTickCount64() - (ULONGLONG)tick > 3000) return 0.0f;
    return (float)value / 1000.0f;
}
static DWORD wait_stream(Driver *d, DWORD timeout) {
    return d->opened ? WaitForSingleObject(d->ready, timeout) : WAIT_ABANDONED;
}
static DWORD ready_count(Driver *d) {
    EnterCriticalSection(&d->lock); DWORD count = d->count; LeaveCriticalSection(&d->lock); return count;
}
static BOOL get_stream_ptr(Driver *d, BYTE **data, DWORD *size, DWORD *remain) {
    if (!data || !size || !remain) return FALSE;
    EnterCriticalSection(&d->lock);
    if (!d->count) { *data = NULL; *size = *remain = 0; LeaveCriticalSection(&d->lock); return FALSE; }
    Block *block = &d->queue[d->head];
    memcpy(d->last, block->data, block->size);
    *data = d->last; *size = block->size;
    d->head = (d->head + 1) % QUEUE_COUNT;
    *remain = --d->count;
    if (!d->count) ResetEvent(d->ready);
    LeaveCriticalSection(&d->lock);
    return TRUE;
}
static BOOL get_stream_copy(Driver *d, BYTE *data, DWORD *size, DWORD *remain) {
    BYTE *source;
    if (!data || !get_stream_ptr(d, &source, size, remain)) return FALSE;
    memcpy(data, source, *size); return TRUE;
}
static void release_driver(Driver *d) {
    AcquireSRWLockExclusive(&factory_lock);
    close_tuner(d);
    DeleteCriticalSection(&d->lock);
    DeleteCriticalSection(&d->usb_lock);
    DeleteCriticalSection(&d->lifecycle_lock);
    CloseHandle(d->ready);
    CloseHandle(d->stop_event);
    CloseHandle(d->receive_gate);
    HeapFree(GetProcessHeap(), 0, d->queue);
    free_error_records(d);
    if (instance == d) instance = NULL;
    HeapFree(GetProcessHeap(), 0, d);
    ReleaseSRWLockExclusive(&factory_lock);
}
static const WCHAR *tuner_name(Driver *d) { (void)d; return L"Sony torne CECH-ZD1J"; }
static BOOL is_open(Driver *d) { return d->opened; }
static const WCHAR *enum_space(Driver *d, DWORD space) { (void)d; return space ? NULL : L"地デジ UHF"; }
static const WCHAR *enum_channel(Driver *d, DWORD space, DWORD channel) { return space || channel >= 40 ? NULL : d->channel_names[channel]; }
static DWORD cur_space(Driver *d) { return d->channel == 0xffffffff ? 0xffffffff : 0; }
static DWORD cur_channel(Driver *d) { return d->channel; }

static void put32(size_t offset, uint32_t value) { memcpy(rtti + offset, &value, 4); }
static BOOL CALLBACK init_vtable(PINIT_ONCE once, PVOID parameter, PVOID *context) {
    (void)once; (void)parameter; (void)context;
    void *methods[17] = {(void*)open_tuner, (void*)close_tuner, (void*)set_channel_byte,
        (void*)signal_level, (void*)wait_stream, (void*)ready_count, (void*)get_stream_ptr,
        (void*)get_stream_copy, (void*)purge, (void*)release_driver, (void*)tuner_name,
        (void*)is_open, (void*)enum_space, (void*)enum_channel, (void*)set_channel,
        (void*)cur_space, (void*)cur_channel};
    void *locator = rtti + 0x90;
    memcpy(rtti, &locator, 8); memcpy(rtti + 8, methods, sizeof(methods));
    put32(0x90, 1); put32(0x9c, 0x110); put32(0xa0, 0xa8); put32(0xa4, 0x90);
    put32(0xb0, 2); put32(0xb4, 0xb8); put32(0xb8, 0xc0); put32(0xbc, 0xdc);
    put32(0xc0, 0x110); put32(0xc4, 1); put32(0xcc, 0xffffffff); put32(0xd4, 0x40); put32(0xd8, 0xa8);
    put32(0xdc, 0x138); put32(0xe8, 0xffffffff); put32(0xf0, 0x40); put32(0xf4, 0xf8);
    put32(0x100, 1); put32(0x104, 0x108); put32(0x108, 0xdc);
    memcpy(rtti + 0x120, ".?AVIBonDriver2@@", sizeof(".?AVIBonDriver2@@"));
    memcpy(rtti + 0x148, ".?AVIBonDriver@@", sizeof(".?AVIBonDriver@@"));
    return TRUE;
}
static BOOL config_path(const WCHAR *ini, const WCHAR *key, const WCHAR *fallback,
                        const WCHAR *base, WCHAR *out) {
    WCHAR configured[MAX_PATH], combined[MAX_PATH];
    DWORD length = GetPrivateProfileStringW(L"Torne", key, fallback, configured, MAX_PATH, ini);
    if (!length || length >= MAX_PATH - 1) return FALSE;
    if (configured[1] == L':' || configured[0] == L'\\') wcscpy(combined, configured);
    else if (_snwprintf(combined, MAX_PATH, L"%ls\\%ls", base, configured) < 0) return FALSE;
    length = GetFullPathNameW(combined, MAX_PATH, out, NULL);
    return length && length < MAX_PATH;
}
static BOOL paths(Driver *d) {
    WCHAR directory[MAX_PATH], ini[MAX_PATH], root[MAX_PATH];
    DWORD length = GetModuleFileNameW(module, directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return FALSE;
    FILETIME created, exited, kernel, user;
    ULARGE_INTEGER creation; creation.QuadPart = 0;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) { creation.LowPart = created.dwLowDateTime; creation.HighPart = created.dwHighDateTime; }
    snprintf(d->session_id, sizeof(d->session_id), "%lu-%llu", (unsigned long)GetCurrentProcessId(), (unsigned long long)creation.QuadPart);
    WCHAR *slash = wcsrchr(directory, L'\\');
    if (!slash) return FALSE;
    *slash = 0;
    if (_snwprintf(d->recovery_log, MAX_PATH, L"%ls\\BonDriver_Torne-recovery.log", directory) < 0) return FALSE;
    if (_snwprintf(d->diagnostic_log, MAX_PATH, L"%ls\\BonDriver_Torne-diagnostic.log", directory) < 0) return FALSE;
    restore_error_history(d);
    if (_snwprintf(ini, MAX_PATH, L"%ls\\BonDriver_Torne.ini", directory) < 0) return FALSE;
#ifdef TORNE_DIAGNOSTICS
    const UINT diagnostic_default = 1;
#else
    const UINT diagnostic_default = 0;
#endif
    d->detailed_logging = GetPrivateProfileIntW(L"Torne", L"DiagnosticLog", diagnostic_default, ini) != 0;
    BOOL valid = config_path(ini, L"Root", L"..\\..", directory, root) &&
           config_path(ini, L"FirmwareDirectory", L"BCJB95006\\USRDIR", root, d->firmware) &&
           config_path(ini, L"PvrModule", L"research\\decrypted\\PvrRecSvr.prx", root, d->pvr) &&
           config_path(ini, L"MediaModule", L"research\\decrypted\\libM2tsAes.elf", root, d->media);
    if (!valid) return FALSE;
    WCHAR selected[MAX_PATH];
    length = GetPrivateProfileStringW(L"Torne", L"OperationalFirmware", L"", selected, MAX_PATH, ini);
    if (length >= MAX_PATH - 1) return FALSE;
    return !length || config_path(ini, L"OperationalFirmware", L"", root, d->operational);
}
__declspec(dllexport) Driver * WINAPI CreateBonDriver(void) {
    AcquireSRWLockExclusive(&factory_lock);
    if (instance) { ReleaseSRWLockExclusive(&factory_lock); return instance; }
    Driver *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Driver));
    if (!d) { ReleaseSRWLockExclusive(&factory_lock); return NULL; }
    d->channel = 0xffffffff;
    InitializeCriticalSection(&d->lock);
    InitializeCriticalSection(&d->usb_lock);
    InitializeCriticalSection(&d->lifecycle_lock);
    d->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    d->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    d->receive_gate = CreateEventW(NULL, TRUE, TRUE, NULL);
    d->queue = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Block) * QUEUE_COUNT);
    if (!d->ready || !d->stop_event || !d->receive_gate || !d->queue || !paths(d)) {
        if (d->ready) CloseHandle(d->ready);
        if (d->stop_event) CloseHandle(d->stop_event);
        if (d->receive_gate) CloseHandle(d->receive_gate);
        if (d->queue) HeapFree(GetProcessHeap(), 0, d->queue);
        DeleteCriticalSection(&d->lock); DeleteCriticalSection(&d->usb_lock);
        DeleteCriticalSection(&d->lifecycle_lock); HeapFree(GetProcessHeap(), 0, d);
        ReleaseSRWLockExclusive(&factory_lock); return NULL;
    }
    for (DWORD ch = 0; ch < 40; ch++) _snwprintf(d->channel_names[ch], 32, L"UHF %lu ch", (unsigned long)ch+13);
    InitOnceExecuteOnce(&vtable_once, init_vtable, NULL, NULL);
    d->vtable = (void**)(rtti + 8);
    instance = d;
    ReleaseSRWLockExclusive(&factory_lock);
    return d;
}
__declspec(dllexport) DWORD WINAPI TorneGetDiagnostics(char *data, DWORD capacity) {
    if (!data || !capacity) return 0;
    AcquireSRWLockShared(&factory_lock);
    if (!instance) { ReleaseSRWLockShared(&factory_lock); return 0; }
    Driver *d = instance;
    EnterCriticalSection(&d->lifecycle_lock);
    ResetEvent(d->receive_gate);
    EnterCriticalSection(&d->usb_lock);
    EnterCriticalSection(&d->lock);
    TorneStatus *s = &d->status_native;
    char cnr_value[40] = "null";
    BOOL cnr_fresh = s->cnr_valid && s->streaming && !s->error && s->cnr_tick && GetTickCount64() - s->cnr_tick <= 3000;
    if (cnr_fresh) snprintf(cnr_value, sizeof(cnr_value), "%.3f", (double)s->cnr_db);
    int size = snprintf(data, capacity, "{\"backend\":\"native-winusb\",\"physicalChannel\":%lu,\"queueBlocks\":%lu,\"receivedBytes\":%llu,\"queueDroppedBytes\":%llu,\"lastError\":%lu,\"nativeError\":%lu,\"noSignalRecoveryAttempts\":%llu,\"lastRecoveryError\":%lu,\"preDMACRCVerifiedPackets\":%llu,\"state\":%lu,\"nextSequence\":%lu,\"streaming\":%s,\"mediaSessionClosed\":%s,\"usbBytes\":%llu,\"mediaPackets\":%llu,\"encryptedPackets\":%llu,\"runtimeAuthentications\":%llu,\"discardedMediaBytes\":%llu,\"inputPacketBytes\":%lu,\"esSelectionMode\":%lu,\"pmtOutputMode\":%lu,\"psiSettingsVerified\":%s,\"tsProcessing\":\"%s\",\"validPmtSections\":%lu,\"invalidPsiCrc\":%lu,\"fullsegBmlPrograms\":%lu,\"onesegBmlPrograms\":%lu,\"dsmccEs\":%lu,\"firmwareVariant\":\"%s\",\"operation\":\"%s\",\"devicePurgeCount\":%llu,\"bcasStartupSkipped\":%s,\"cardAPICalls\":%lu}",
        (unsigned long)(d->channel == 0xffffffff ? 0 : d->channel+13), (unsigned long)d->count,
        (unsigned long long)d->received, (unsigned long long)d->dropped, (unsigned long)d->last_error,
        (unsigned long)s->error, (unsigned long long)d->no_signal_recovery_attempts, (unsigned long)d->last_recovery_error,
        (unsigned long long)s->crc_verified_packets, (unsigned long)s->state, (unsigned long)s->sequence,
        s->streaming ? "true" : "false", s->media_closed ? "true" : "false",
        (unsigned long long)s->usb_bytes, (unsigned long long)s->packets,
        (unsigned long long)s->encrypted_packets, (unsigned long long)s->authentications,
        (unsigned long long)s->discarded_bytes, (unsigned long)s->input_packet_bytes,
        (unsigned long)s->es_selection_mode, (unsigned long)s->pmt_output_mode,
        s->psi_settings_verified ? "true" : "false",
        s->firmware_variant == 12 ? "c-generic-ts-scrambled-crc" : s->firmware_variant == 11 ? "c-generic-ts-upstream-crc" : s->firmware_variant == 10 ? "c-generic-ts-af-events-commands-control-init-dispatch-routing-crc" : s->firmware_variant == 9 ? "c-generic-ts-events-commands-control-init-dispatch-routing-crc" : s->firmware_variant == 8 ? "c-generic-ts-commands-control-init-dispatch-routing-crc" : s->firmware_variant == 7 ? "c-generic-ts-control-init-dispatch-routing-crc" : s->firmware_variant == 6 ? "c-generic-ts-init-dispatch-routing-crc" : s->firmware_variant == 5 ? "c-generic-ts-routing-crc" : s->firmware_variant == 4 ? "c-generic-ts-crc-verified" : s->firmware_variant == 3 ? "c-generic-ts-header-preserved" : !s->psi_settings_verified ? "unknown" : s->firmware_variant == 2 ? "firmware-psi-passthrough-remux" : s->pmt_output_mode == 4 || s->pmt_output_mode == 7
            ? "firmware-remux-es-preserved" : "firmware-pmt-filtered",
        (unsigned long)s->valid_pmt_sections, (unsigned long)s->invalid_psi_crc,
        (unsigned long)s->fullseg_bml_programs, (unsigned long)s->oneseg_bml_programs,
        (unsigned long)s->dsmcc_es, s->firmware_variant == 12 ? "c-raw-scrambled" : s->firmware_variant == 11 ? "c-raw-upstream" : s->firmware_variant == 10 ? "c-raw-af" : s->firmware_variant == 9 ? "c-raw-events" : s->firmware_variant == 8 ? "c-raw-commands" : s->firmware_variant == 7 ? "c-raw-control" : s->firmware_variant == 6 ? "c-raw-init" : s->firmware_variant == 5 ? "c-raw-routing" : s->firmware_variant == 4 ? "c-raw-verified" : s->firmware_variant == 3 ? "c-raw-preserved" : s->firmware_variant == 2 ? "psi-passthrough" : s->firmware_variant == 1 ? "preserve-es" : "original", s->operation, (unsigned long long)d->device_purges, s->bcas_skipped ? "true" : "false", (unsigned long)s->card_api_calls);
    if (size > 0 && size < (int)capacity) {
        int added = snprintf(data + size - 1, capacity - (DWORD)size + 1,
            ",\"driverBuild\":\"2026-10-16-diagnostics-v2\",\"diagnosticsEnabled\":%s,\"nativeReason\":\"%s\",\"lastUsbOperation\":\"%s\",\"lastUsbError\":%lu,\"rfSnapshotValid\":%s,\"rfStatus238\":%lu,\"rfSearch504\":%lu,\"rfViterbi\":[%lu,%lu,%lu],\"rfSnapshotTick\":%llu,\"cnrDb\":%s,\"cnrValid\":%s,\"cnrNoiseRaw\":%lu,\"cnrSignalRaw\":%lu,\"cnrReadError\":%lu,\"cnrTick\":%llu}",
            d->detailed_logging ? "true" : "false", s->failure_reason, s->io_operation, (unsigned long)s->io_error,
            s->rf_snapshot_valid ? "true" : "false", (unsigned long)s->rf_status, (unsigned long)s->rf_search,
            (unsigned long)s->rf_viterbi[0], (unsigned long)s->rf_viterbi[1], (unsigned long)s->rf_viterbi[2],
            (unsigned long long)s->rf_snapshot_tick, cnr_value, cnr_fresh ? "true" : "false",
            (unsigned long)s->cnr_noise_raw, (unsigned long)s->cnr_signal_raw,
            (unsigned long)s->cnr_error, (unsigned long long)s->cnr_tick);
        size = added > 0 && added < (int)(capacity - (DWORD)size + 1) ? size + added - 1 : -1;
    }
    LeaveCriticalSection(&d->lock);
    LeaveCriticalSection(&d->usb_lock);
    SetEvent(d->receive_gate);
    LeaveCriticalSection(&d->lifecycle_lock);
    ReleaseSRWLockShared(&factory_lock);
    return size > 0 && size < (int)capacity ? (DWORD)size : 0;
}
BOOL WINAPI DllMain(HINSTANCE handle, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) { module = handle; DisableThreadLibraryCalls(handle); }
    return TRUE;
}
