#define main recovery_tests_main
#include "test-recovery.c"
#undef main

static void read_log(const WCHAR *path, char *out, DWORD capacity) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    DWORD size = 0; assert(ReadFile(file, out, capacity - 1, &size, NULL)); out[size] = 0; CloseHandle(file);
}
static void inject_error(Driver *d, const char *marker, DWORD error, const char *event) {
    d->last_error = error; d->status_native.error = error;
    strcpy(d->status_native.operation, "stop_stream.read_state");
    strcpy(d->status_native.failure_reason, "usb_control_transfer_failed");
    strcpy(d->status_native.io_operation, "control type=0xc0 request=0xbc value=0x0800 index=0x2008 length=2");
    d->status_native.io_error = error;
    begin_log_batch(d); diagnostic_log(d, "usb_error", marker); log_recovery(d, event);
}

int wmain(int argc, WCHAR **argv) {
    assert(argc == 2);
    assert(recovery_tests_main() == 0);
    Driver d; init_driver(&d);
    assert(_snwprintf(d.diagnostic_log, MAX_PATH, L"%ls\\.tmp\\diagnostic-writer-test.log", argv[1]) > 0);
    HANDLE file = CreateFileW(d.diagnostic_log, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    LARGE_INTEGER end; end.QuadPart = 8 * 1024 * 1024 + 1;
    assert(SetFilePointerEx(file, end, NULL, FILE_BEGIN) && SetEndOfFile(file)); CloseHandle(file);
    d.detailed_logging = TRUE;
    diagnostic_log(&d, "writer_test", "error=22 reason=usb_control_transfer_failed");
    d.detailed_logging = FALSE;
    diagnostic_log(&d, "disabled_marker", "must_not_be_written");
    file = CreateFileW(d.diagnostic_log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    char data[2048] = {0}; DWORD size = 0;
    assert(ReadFile(file, data, sizeof(data) - 1, &size, NULL)); CloseHandle(file);
    assert(size > 0 && strstr(data, "event=writer_test error=22"));
    assert(!strstr(data, "disabled_marker"));
    WCHAR backup[MAX_PATH]; assert(_snwprintf(backup, MAX_PATH, L"%ls.1", d.diagnostic_log) > 0);
    assert(GetFileAttributesW(backup) != INVALID_FILE_ATTRIBUTES);
    assert(_snwprintf(d.recovery_log, MAX_PATH, L"%ls\\.tmp\\dedup-recovery-test.log", argv[1]) > 0);
    file = CreateFileW(d.recovery_log, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(file != INVALID_HANDLE_VALUE); CloseHandle(file);
    d.detailed_logging = TRUE; strcpy(d.session_id, "unit-session-1");
    inject_error(&d, "FULL_ERROR_ONLY_ONCE", 22, "receive_failed");
    inject_error(&d, "MUST_NOT_REPEAT_DETAILS", 22, "retry_failed");
    inject_error(&d, "MUST_NOT_REPEAT_DETAILS", 22, "retry_failed");
    inject_error(&d, "DIFFERENT_ERROR_DETAILS", 1460, "retry_failed");
    d.last_error = 0; log_recovery(&d, "stream_started");
    inject_error(&d, "MUST_NOT_REPEAT_DETAILS", 22, "retry_failed");
    char logs[12000]; read_log(d.diagnostic_log, logs, sizeof(logs));
    assert(strstr(logs, "FULL_ERROR_ONLY_ONCE") && !strstr(logs, "MUST_NOT_REPEAT_DETAILS"));
    assert(strstr(logs, "DIFFERENT_ERROR_DETAILS"));
    assert(strstr(logs, "error_retry error_id=1 retries=1") && strstr(logs, "error_retry error_id=1 retries=3"));
    read_log(d.recovery_log, logs, sizeof(logs));
    const char *first = strstr(logs, "error=22"); assert(first && !strstr(first + 1, "error=22"));
    assert(strstr(logs, "error=1460") && strstr(logs, "error_retry error_id=1 retries=3"));
    free_error_records(&d); d.next_error_id = 0; restore_error_history(&d);
    inject_error(&d, "MUST_NOT_REPEAT_AFTER_RELOAD", 22, "retry_failed");
    read_log(d.diagnostic_log, logs, sizeof(logs)); assert(!strstr(logs, "MUST_NOT_REPEAT_AFTER_RELOAD"));
    assert(strstr(logs, "error_retry error_id=1 retries=4"));
    fake_status.error = 0; fake_status.streaming = TRUE; fake_status.cnr_valid = TRUE;
    fake_status.cnr_db = 32.456f; fake_status.cnr_tick = GetTickCount64(); refresh_status(&d);
    float value = signal_level(&d); assert(value > 32.45f && value < 32.46f);
    fake_status.cnr_tick = GetTickCount64() - 4000; refresh_status(&d); assert(signal_level(&d) == 0.0f);
    fake_status.cnr_valid = FALSE; refresh_status(&d); assert(signal_level(&d) == 0.0f);
    // A separate session must record the full failure again.
    free_error_records(&d); d.next_error_id = 0; strcpy(d.session_id, "unit-session-2");
    restore_error_history(&d); assert(!d.errors);
    inject_error(&d, "NEW_SESSION_FULL_DETAILS", 22, "receive_failed");
    read_log(d.diagnostic_log, logs, sizeof(logs)); assert(strstr(logs, "NEW_SESSION_FULL_DETAILS"));
    dispose_driver(&d);
    puts("PASS: log rotation/disable, session error dedup and retry counts, changed causes, new session; cached C/N value and stale/invalid rejection");
    return 0;
}
