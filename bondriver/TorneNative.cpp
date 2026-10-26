/* Native port of the locally verified torne 1.10 protocol.
 * Firmware, authentication tables and media IV remain in external user files.
 * All entry points are called under the BonDriver's USB lock.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "TorneNative.h"
#include <winusb.h>
#include <setupapi.h>
#include <bcrypt.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <new>
#include <cstdlib>
#include <cstdint>
#include <cwctype>
#include <cstdarg>
#include <cmath>

using Bytes = std::vector<BYTE>;
static void require(bool ok, DWORD error = ERROR_INVALID_DATA) { if (!ok) throw error; }
static void wincheck(BOOL ok) { if (!ok) throw GetLastError(); }
static void cngcheck(NTSTATUS status) { require(status >= 0, ERROR_ENCRYPTION_FAILED); }
static unsigned be16(const BYTE *p) { return unsigned(p[0]) << 8 | p[1]; }
static DWORD be32(const BYTE *p) { return DWORD(be16(p)) << 16 | be16(p + 2); }
static ULONGLONG be64(const BYTE *p) { return ULONGLONG(be32(p)) << 32 | be32(p + 4); }
static void put16(BYTE *p, unsigned n) { p[0] = BYTE(n >> 8); p[1] = BYTE(n); }
static void put32(BYTE *p, DWORD n) { put16(p, n >> 16); put16(p + 2, n); }
static DWORD ts_crc32(const BYTE *packet) {
    static const auto table = [] {
        std::array<DWORD,256> result{};
        for (DWORD n=0; n<256; ++n) {
            DWORD crc=n;
            for (unsigned bit=0; bit<8; ++bit) crc=(crc>>1)^((crc&1) ? 0xedb88320u : 0u);
            result[n]=crc;
        }
        return result;
    }();
    DWORD crc=0xffffffffu;
    for (unsigned i=0; i<188; ++i) crc=(crc>>8)^table[(crc^packet[i])&255];
    return crc^0xffffffffu;
}
#include "TornePsi.h"
static void random_bytes(BYTE *p, size_t n) {
    cngcheck(BCryptGenRandom(NULL, p, ULONG(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}
static Bytes digest(const WCHAR *algorithm, const Bytes &data) {
    BCRYPT_ALG_HANDLE provider = NULL;
    cngcheck(BCryptOpenAlgorithmProvider(&provider, algorithm, NULL, 0));
    BCRYPT_HASH_HANDLE hash = NULL;
    try {
        ULONG length = 0, got = 0;
        cngcheck(BCryptGetProperty(provider, BCRYPT_HASH_LENGTH, (PUCHAR)&length, sizeof(length), &got, 0));
        cngcheck(BCryptCreateHash(provider, &hash, NULL, 0, NULL, 0, 0));
        cngcheck(BCryptHashData(hash, const_cast<BYTE *>(data.data()), ULONG(data.size()), 0));
        Bytes out(length);
        cngcheck(BCryptFinishHash(hash, out.data(), length, 0));
        BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(provider, 0);
        return out;
    } catch (...) {
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(provider, 0); throw;
    }
}
static void check_digest(const Bytes &data, const WCHAR *algorithm, const char *expected) {
    Bytes value = digest(algorithm, data);
    std::string hex;
    for (BYTE n : value) { hex += "0123456789abcdef"[n >> 4]; hex += "0123456789abcdef"[n & 15]; }
    require(hex == expected, ERROR_FILE_INVALID);
}
class Aes {
    BCRYPT_ALG_HANDLE provider = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
public:
    Aes(const Bytes &material, bool ecb = false) {
        try {
            cngcheck(BCryptOpenAlgorithmProvider(&provider, BCRYPT_AES_ALGORITHM, NULL, 0));
            const WCHAR *mode = ecb ? BCRYPT_CHAIN_MODE_ECB : BCRYPT_CHAIN_MODE_CBC;
            cngcheck(BCryptSetProperty(provider, BCRYPT_CHAINING_MODE, (PUCHAR)mode,
                                      ULONG((wcslen(mode) + 1) * sizeof(WCHAR)), 0));
            cngcheck(BCryptGenerateSymmetricKey(provider, &key, NULL, 0,
                                              const_cast<BYTE *>(material.data()), ULONG(material.size()), 0));
        } catch (...) { if (provider) BCryptCloseAlgorithmProvider(provider, 0); throw; }
    }
    ~Aes() { if (key) BCryptDestroyKey(key); if (provider) BCryptCloseAlgorithmProvider(provider, 0); }
    Aes(const Aes &) = delete;
    Aes &operator=(const Aes &) = delete;
    void crypt(BYTE *data, ULONG size, bool encrypt, const BYTE *initial = NULL, bool ecb = false) {
        if (!size) return;
        BYTE iv[16] = {};
        if (initial) memcpy(iv, initial, 16);
        ULONG got = 0;
        auto fn = encrypt ? BCryptEncrypt : BCryptDecrypt;
        cngcheck(fn(key, data, size, NULL, ecb ? NULL : iv, ecb ? 0 : 16, data, size, &got, 0));
        require(got == size);
        SecureZeroMemory(iv, sizeof(iv));
    }
};
static Bytes read_file(const std::wstring &path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    require(file != INVALID_HANDLE_VALUE, GetLastError());
    try {
        LARGE_INTEGER length;
        wincheck(GetFileSizeEx(file, &length));
        require(length.QuadPart > 0 && length.QuadPart <= 32 * 1024 * 1024, ERROR_FILE_INVALID);
        Bytes data(size_t(length.QuadPart)); DWORD got = 0;
        wincheck(ReadFile(file, data.data(), DWORD(data.size()), &got, NULL));
        require(got == data.size(), ERROR_HANDLE_EOF);
        CloseHandle(file); return data;
    } catch (...) { CloseHandle(file); throw; }
}
class Elf {
    Bytes data;
    struct Segment { ULONGLONG va, offset, size, memory; };
    std::vector<Segment> segments;
public:
    Elf(const std::wstring &path, bool ppc) : data(read_file(path)) {
        check_digest(data, ppc ? BCRYPT_SHA256_ALGORITHM : BCRYPT_MD5_ALGORITHM,
                     ppc ? "49b0b5c068efa7808cc4c13cb6bb7a909e33802ee5dfbf5a1a7c2ccbfedcbf5e" : "59f5649fe320fc5cc5b3889a4499caa2");
        require(data.size() >= 64 && memcmp(data.data(), "\177ELF", 4) == 0 && data[5] == 2);
        require(data[4] == (ppc ? 2 : 1));
        ULONGLONG off = ppc ? be64(data.data() + 32) : be32(data.data() + 28);
        unsigned stride = be16(data.data() + (ppc ? 54 : 42));
        unsigned count = be16(data.data() + (ppc ? 56 : 44));
        require(stride >= (ppc ? 56u : 32u) && off <= data.size() && count <= (data.size() - off) / stride);
        for (unsigned i = 0; i < count; ++i) {
            const BYTE *p = data.data() + size_t(off) + i * stride;
            if (be32(p) != 1) continue;
            Segment s = ppc ? Segment{be64(p + 16), be64(p + 8), be64(p + 32), be64(p + 40)}
                            : Segment{be32(p + 8), be32(p + 4), be32(p + 16), be32(p + 20)};
            require(s.offset <= data.size() && s.size <= data.size() - s.offset && s.size <= s.memory);
            segments.push_back(s);
        }
    }
    Bytes read(ULONGLONG address, size_t length) const {
        for (const auto &s : segments) {
            if (address < s.va || address - s.va > s.memory || length > s.memory - (address - s.va)) continue;
            Bytes out(length);
            size_t available = address - s.va < s.size ? size_t(std::min<ULONGLONG>(length, s.size - (address - s.va))) : 0;
            if (available) memcpy(out.data(), data.data() + size_t(s.offset + address - s.va), available);
            return out;
        }
        throw DWORD(ERROR_FILE_INVALID);
    }
    DWORD pointer(int offset) const { auto b = read(0x45830 + offset, 4); return be32(b.data()); }
};

struct TorneNative {
    HANDLE device = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE usb = NULL;
    Elf module, media;
    Bytes idle, operational, streamkey, media_key, iv, pending_media;
    Aes *media_cbc = NULL, *media_ecb = NULL;
    TorneStatus status = {};
    bool prepared = false, media_open = false, authenticated = false, protocol_started = false;
    bool framing_locked = false;
    unsigned packet_stride = 192, packet_prefix = 4;
    TornePsi psi;
    unsigned api_pending = 0, rf_baseline = 0;
    DWORD auth_counter = 1;
    ULONGLONG last_auth = 0, last_data = 0, last_rf_lock = 0;
    Bytes rf_config;
    DWORD operational_variant = 0;
    TorneLogCallback logger = NULL;
    void *logger_context = NULL;
    unsigned i2c_device = 0, i2c_register = 0;
    bool i2c_active = false;

    TorneNative(const WCHAR *firmware, const WCHAR *pvr, const WCHAR *aes)
        : module(pvr, true), media(aes, false),
          idle(read_file(std::wstring(firmware) + L"\\mb86h47_aac_idle.bin")),
          operational(read_file(std::wstring(firmware) + L"\\mb86h47_aac_trc.bin")), iv(media.read(0x6b50, 16)) {
        check_digest(idle, BCRYPT_SHA256_ALGORITHM, "d9acae79f9e0ecaecb4b491af8d30e95ba7ca949adf5c2b6397d25a00508691d");
        check_digest(operational, BCRYPT_SHA256_ALGORITHM, "653f2377c838a4e7276b447fd992084d5e6e84a90ac4d389851f82309b662a04");
    }
    ~TorneNative() {
        delete media_cbc; delete media_ecb;
        if (usb) WinUsb_Free(usb);
        if (device != INVALID_HANDLE_VALUE) CloseHandle(device);
        for (Bytes *b : {&streamkey, &media_key, &pending_media})
            if (!b->empty()) SecureZeroMemory(b->data(), b->size());
    }
    void use_firmware(const WCHAR *path) {
        require(!usb && !prepared, ERROR_BUSY);
        Bytes candidate = read_file(path);
        Bytes value = digest(BCRYPT_SHA256_ALGORITHM, candidate);
        std::string hex;
        for (BYTE n : value) { hex += "0123456789abcdef"[n >> 4]; hex += "0123456789abcdef"[n & 15]; }
        DWORD variant;
        if (hex == "653f2377c838a4e7276b447fd992084d5e6e84a90ac4d389851f82309b662a04") variant = 0;
        else if (hex == "04e2c3b996346a0752d8bec8bae573a9fb2e4aa9cb8250d9fec8956eb6266d4f") variant = 1;
        else if (hex == "5aa34270a7647264a5676867fdbbdd22b2c0cb6bb0644cf7c136e5d60bf7edcc") variant = 2;
        else if (hex == "fc9374fdb5e47b656d2ae90b6d700a53c0384ccb6f2bb71f27f37a36c82c9ccb") variant = 3;
        else if (hex == "89fab06aa1917eea9b9b6abd3abe10e515bae16410f62125e53257c6738aad87") variant = 4;
        else if (hex == "2e8ef9a926373168c82729675b68386c18191e89720776f0f38e23ba5c3ab866") variant = 5;
        else if (hex == "8c6b6844123ad674a41ce9697973b69df5bd6bf17837575419c97a6adda7b4e1") variant = 6;
        else if (hex == "d8edfa3be512078f22c25a3c19bbc350b48616030b0778972912e6f27d9106a2") variant = 7;
        else if (hex == "c54d4389210705c5dfd96abd5834e6eb9e9ffb42e4a5d2c48ca323811e014fc1") variant = 8;
        else if (hex == "932416c9d443e7e46afb4531c7a96f74e2e1d549f01bf12be4c867fefa49e0e4") variant = 10;
        else if (hex == "43a51ea145910786a44728fff4f073010e8e9662c5b2b359432a7ce7a019ec56") variant = 9;
        else if (hex == "6d342cd246aedc09ea1635059b626b8407db7aec1eef11623416e0c1d3f9f893") variant = 11;
        else if (hex == "d85aa19d47331a77b51d38063347b01e7daa04a38734908b051dcddd45bf1fea") variant = 12;
        else throw DWORD(ERROR_FILE_INVALID);
        operational.swap(candidate);
        operational_variant = variant;
        trace("firmware_selected", "sha256=%s variant=%lu", hex.c_str(), (unsigned long)variant);
    }
    void stage(const char *name) { strncpy(status.operation, name, sizeof(status.operation) - 1); }
    void trace(const char *event, const char *format, ...) {
        if (!logger) return;
        char data[768];
        va_list args; va_start(args, format); vsnprintf(data, sizeof(data), format, args); va_end(args);
        data[sizeof(data) - 1] = 0;
        logger(logger_context, event, data);
    }
    void reason(const char *name) { snprintf(status.failure_reason, sizeof(status.failure_reason), "%s", name); }
    void trace_error(DWORD error) {
        WCHAR wide[256] = {}; char message[512] = {};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error,
                       MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), wide, 256, NULL);
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, message, sizeof(message), NULL, NULL);
        for (char *p = message; *p; ++p) if (*p == '\r' || *p == '\n' || *p == '\t') *p = ' ';
        const char *name = error == ERROR_TIMEOUT ? "ERROR_TIMEOUT" : error == ERROR_BAD_COMMAND ? "ERROR_BAD_COMMAND" :
            error == ERROR_INVALID_DATA ? "ERROR_INVALID_DATA" : error == ERROR_GEN_FAILURE ? "ERROR_GEN_FAILURE" :
            error == ERROR_NOT_READY ? "ERROR_NOT_READY" : error == ERROR_ACCESS_DENIED ? "ERROR_ACCESS_DENIED" : "WIN32_ERROR";
        trace("failure", "error=%lu errorName=%s operation=%s reason=%s state=0x%04lx streaming=%d prepared=%d lastIO=%s ioError=%lu message=%s",
            (unsigned long)error, name, status.operation, status.failure_reason, (unsigned long)status.state,
            status.streaming, prepared, status.io_operation, (unsigned long)status.io_error, message);
    }
    Bytes control(BYTE kind, BYTE request, USHORT value, USHORT index, Bytes data) {
        require(data.size() <= 64);
        WINUSB_SETUP_PACKET setup = {kind, request, value, index, USHORT(data.size())};
        ULONG got = 0;
        snprintf(status.io_operation, sizeof(status.io_operation), "control type=0x%02x request=0x%02x value=0x%04x index=0x%04x length=%lu",
                 kind, request, value, index, (unsigned long)data.size());
        if (!WinUsb_ControlTransfer(usb, setup, data.data(), ULONG(data.size()), &got, NULL)) {
            status.io_error = GetLastError(); reason("usb_control_transfer_failed");
            trace("usb_error", "operation=%s io=%s actual=%lu error=%lu api=0x%02lx apiSequence=%lu",
                status.operation, status.io_operation, (unsigned long)got, (unsigned long)status.io_error,
                (unsigned long)status.api_command, (unsigned long)status.api_sequence);
            if (i2c_active) trace("i2c_error", "device=0x%02x register=0x%04x", i2c_device, i2c_register);
            throw status.io_error;
        }
        if (got != data.size()) { reason("usb_control_short_transfer"); trace("usb_short_transfer", "io=%s actual=%lu", status.io_operation, (unsigned long)got); }
        require(got == data.size(), ERROR_HANDLE_EOF); return data;
    }
    Bytes read(DWORD address, size_t length = 2) {
        return control(0xc0, 0xbc, USHORT(((address >> 16) & 15) << 8), USHORT(address), Bytes(length));
    }
    unsigned word(DWORD address) { auto b = read(address); return be16(b.data()); }
    void write(DWORD address, const Bytes &data) {
        control(0x40, 0xbc, USHORT(((address >> 16) & 15) << 8), USHORT(address), data);
    }
    void write_word(DWORD address, unsigned n) { Bytes b(2); put16(b.data(), n); write(address, b); }
    Bytes pipe(BYTE id, size_t length) {
        Bytes data(length); ULONG got = 0;
        snprintf(status.io_operation, sizeof(status.io_operation), "read_pipe endpoint=0x%02x length=%lu", id, (unsigned long)length);
        if (!WinUsb_ReadPipe(usb, id, data.data(), ULONG(length), &got, NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_SEM_TIMEOUT) return {};
            status.io_error = error; reason("usb_read_pipe_failed");
            trace("usb_error", "operation=%s io=%s actual=%lu error=%lu", status.operation, status.io_operation, (unsigned long)got, (unsigned long)error);
            throw error;
        }
        require(got <= length); data.resize(got); return data;
    }
    unsigned pending() {
        unsigned bits = word(0x90074) & 0x0ee4;
        read(0x901b0);
        for (unsigned bit = 1; bit <= 0x8000; bit <<= 1) if (bits & bit) write_word(0x90074, bit);
        return bits;
    }
    void wait_state(unsigned expected) {
        trace("state_wait", "operation=%s expected=0x%04x", status.operation, expected);
        ULONGLONG deadline = GetTickCount64() + 3000;
        do {
            status.state = word(0x82008);
            if (status.state == expected) { trace("state_reached", "state=0x%04lx", (unsigned long)status.state); pending(); return; }
            pending(); pipe(0x83, 16);
        } while (GetTickCount64() < deadline);
        reason("firmware_state_timeout");
        trace("state_timeout", "operation=%s expected=0x%04x observed=0x%04lx", status.operation, expected, (unsigned long)status.state);
        throw DWORD(ERROR_TIMEOUT);
    }
    Bytes api(BYTE command, BYTE arg = 0, USHORT word_value = 0) {
        if (command == 10) {
            require(operational_variant != 12, ERROR_NOT_SUPPORTED);
            ++status.card_api_calls;
        }
        api_pending = 0;
        status.api_command = command; status.api_sequence = status.sequence;
        trace("api_begin", "operation=%s command=0x%02x sequence=%lu arg=0x%02x word=0x%04x", status.operation, command, (unsigned long)status.sequence, arg, word_value);
        Bytes payload = {0, BYTE(status.sequence), command, arg, BYTE(word_value >> 8), BYTE(word_value)};
        control(0x40, 0xb8, 0, 0, payload);
        ULONGLONG deadline = GetTickCount64() + 3000;
        do {
            Bytes b = pipe(0x83, 16);
            if (!b.empty() && b[0] == 0x10) {
                trace("api_completion", "command=0x%02x sequence=%lu length=%lu receivedSequence=%u receivedCommand=%u result=%u",
                    command, (unsigned long)status.sequence, (unsigned long)b.size(), b.size() > 3 ? b[3] : 256u, b.size() > 4 ? b[4] : 256u, b.size() > 5 ? b[5] : 256u);
                if (b.size() < 6 || b[3] != status.sequence || b[4] != command || b[5] != 0) reason("api_completion_mismatch_or_rejected");
                require(b.size() >= 6 && b[3] == status.sequence && b[4] == command && b[5] == 0);
                status.sequence = (status.sequence + 1) & 63; return b;
            }
            if (!b.empty() && b[0] == 0x20) api_pending |= pending();
        } while (GetTickCount64() < deadline);
        reason("api_completion_timeout");
        trace("api_timeout", "operation=%s command=0x%02x sequence=%lu", status.operation, command, (unsigned long)status.sequence);
        throw DWORD(ERROR_TIMEOUT);
    }
    Bytes seqcom(unsigned command, const Bytes &payload = {}, BYTE flags = 0) {
        require(!(flags & ~6) && payload.size() % 4 == 0 && payload.size() <= 480);
        require(!flags || authenticated);
        size_t words = payload.size() / 4;
        size_t wire = flags & 4 ? 12 + ((payload.size() + 19) & ~size_t(15)) : 16 + ((payload.size() + 15) & ~size_t(15));
        Bytes packet((wire + 63) & ~size_t(63));
        put16(packet.data(), command); packet[2] = flags; put32(packet.data() + 8, DWORD(words));
        std::copy(payload.begin(), payload.end(), packet.begin() + 16);
        if (flags & 4) {
            size_t length = (payload.size() + 19) & ~size_t(15);
            random_bytes(packet.data() + 12, 4);
            random_bytes(packet.data() + 16 + payload.size(), length - 4 - payload.size());
            Aes cipher(streamkey); cipher.crypt(packet.data() + 12, ULONG(length), true);
        }
        for (size_t off = 0; off < packet.size(); off += 64)
            write(0x83000 + DWORD(off), Bytes(packet.begin() + off, packet.begin() + off + 64));
        api(8);
        Bytes response = read(0x83000, 64);
        require(be16(response.data()) == command && be32(response.data() + 4) == 0);
        DWORD rxwords = be32(response.data() + 8);
        require(rxwords <= 120 && !(response[2] & ~6));
        size_t size = response[2] & 2 ? 12 + ((rxwords * 4 + 19) & ~size_t(15)) : 16 + ((rxwords * 4 + 15) & ~size_t(15));
        require(size <= 512);
        for (size_t off = 64; off < size; off += 64) {
            Bytes b = read(0x83000 + DWORD(off), 64); response.insert(response.end(), b.begin(), b.end());
        }
        if (response[2] & 2) {
            require(authenticated); Aes cipher(streamkey);
            cipher.crypt(response.data() + 12, ULONG((rxwords * 4 + 19) & ~15u), false);
        }
        response.resize(16 + rxwords * 4);
        return response;
    }
    void transfer(const Bytes &data) {
        for (size_t off = 0; off < data.size(); off += 65536) {
            ULONG count = ULONG(std::min<size_t>(65536, data.size() - off)), got = 0;
            wincheck(WinUsb_WritePipe(usb, 2, const_cast<BYTE *>(data.data()) + off, count, &got, NULL));
            require(got == count, ERROR_WRITE_FAULT);
        }
    }
    void authenticate() {
        stage("authenticate");
        DWORD base = module.pointer(-0x7dcc);
        Bytes response = seqcom(0x101, module.read(base + 0x20, 32));
        require(response.size() >= 48);
        Bytes data = module.read(base + 0x40, 28);
        data.push_back(response[0]); data.push_back(module.read(base + 0x25, 1)[0]);
        unsigned index = digest(BCRYPT_SHA1_ALGORITHM, data).back() & 15;
        Bytes table = module.read(base + 0x5c, 512);
        Aes cipher(module.read(module.pointer(-0x7db0), 16)); cipher.crypt(table.data(), ULONG(table.size()), false);
        Bytes key(table.begin() + index * 32, table.begin() + (index + 1) * 32);
        data.assign(response.begin() + 16, response.begin() + 48); data.insert(data.end(), key.begin(), key.end());
        Bytes hash = digest(BCRYPT_SHA1_ALGORITHM, data); seqcom(0x102, hash);
        data = hash; data.insert(data.end(), key.begin(), key.end()); hash = digest(BCRYPT_SHA1_ALGORITHM, data);
        streamkey.clear();
        for (unsigned off : {16u, 6u, 11u, 0u}) streamkey.insert(streamkey.end(), hash.begin() + off, hash.begin() + off + 4);
        SecureZeroMemory(table.data(), table.size()); SecureZeroMemory(key.data(), key.size());
        SecureZeroMemory(data.data(), data.size()); SecureZeroMemory(hash.data(), hash.size());
        authenticated = true; auth_counter = 1;
        api(1); wait_state(0x11);
    }
    void runtime_auth() {
        stage("runtime_authenticate");
        Bytes response = seqcom(0x201, {}, 2); require(response.size() >= 48);
        Bytes data(response.begin() + 16, response.begin() + 48);
        Bytes secret = module.read(module.pointer(-0x7dcc), 28);
        data.insert(data.end(), secret.begin(), secret.end());
        size_t pos = data.size(); data.resize(pos + 4); put32(data.data() + pos, auth_counter);
        seqcom(0x202, digest(BCRYPT_SHA1_ALGORITHM, data), 4);
        SecureZeroMemory(secret.data(), secret.size()); SecureZeroMemory(data.data(), data.size());
        if (!++auth_counter) auth_counter = 1;
        ++status.authentications; last_auth = GetTickCount64();
    }
    void card_startup() {
        stage("card_startup"); pending(); api(10);
        int last = -1; unsigned retries = 0;
        for (unsigned n = 0; n < 16; ++n) {
            pipe(0x83, 16); unsigned bits = pending(), header = word(0x822ac);
            if (!(bits & 0x200) && int(header) == last) continue;
            last = int(header);
            switch (header >> 12) {
            case 1: api(10, 1, 1); break;
            case 3: return;
            case 4: require(retries++ < 3, ERROR_NOT_READY); require(api(10, 1, 0).size() == 6); api(10, 1, 1); break;
            case 0: case 5: throw DWORD(ERROR_NOT_READY);
            default: break;
            }
        }
        throw DWORD(ERROR_TIMEOUT);
    }
    void card_initial_setting() {
        stage("card_initial_setting");
        Bytes data(16); random_bytes(data.data(), 4);
        const BYTE apdu[] = {0x90, 0x30, 0, 0, 0}; memcpy(data.data() + 4, apdu, sizeof(apdu));
        Aes cipher(streamkey); cipher.crypt(data.data(), ULONG(data.size()), true);
        pending(); write(0x8219c, data); api(10, 0x10, 9);
        bool notified = (api_pending & 0x400) != 0;
        ULONGLONG deadline = GetTickCount64() + 3000;
        do {
            notified |= (pending() & 0x400) != 0;
            if (notified) {
                Bytes reply = read(0x822ac, 64);
                if (reply[0] == 0x30 && reply[1] >= 4) {
                    size_t length = reply[1], total = 2 + ((length + 15) & ~size_t(15));
                    for (size_t off = 64; off < total; off += 64) {
                        Bytes b = read(0x822ac + DWORD(off), std::min<size_t>(64, total - off));
                        reply.insert(reply.end(), b.begin(), b.end());
                    }
                    cipher.crypt(reply.data() + 2, ULONG(total - 2), false); pending();
                    require(length >= 61 && reply[10] == 0x21 && reply[11] == 0 &&
                            reply[length] == 0x90 && reply[length + 1] == 0, ERROR_NOT_READY);
                    SecureZeroMemory(reply.data(), reply.size()); return;
                }
            }
            pipe(0x83, 16);
        } while (GetTickCount64() < deadline);
        throw DWORD(ERROR_TIMEOUT);
    }
    void gpio(DWORD address, unsigned bit, bool enabled) {
        DWORD mask = 1u << bit, value = enabled ? mask : 0;
        Bytes b(8); put16(b.data(), value); put16(b.data() + 2, value >> 16);
        put16(b.data() + 4, mask); put16(b.data() + 6, mask >> 16);
        control(0x40, 0xbc, USHORT((((address >> 16) & 15) << 8) | 1), USHORT(address), b);
    }
    unsigned i2c_read(unsigned dev, unsigned reg) {
        i2c_device = dev; i2c_register = reg; i2c_active = true;
        Bytes b(2); put16(b.data(), reg); control(0x40, 0xbd, 0, USHORT(dev << 7), b);
        b = control(0xc0, 0xbd, 0, USHORT(dev << 7), Bytes(2));
        Bytes bus = control(0xc0, 0xbd, 15, 0, Bytes(2));
        if (be16(bus.data()) != 0x0808) { reason("i2c_read_status_failed"); trace("i2c_error", "device=0x%02x register=0x%04x busStatus=0x%04x", dev, reg, be16(bus.data())); }
        require(be16(bus.data()) == 0x0808, ERROR_IO_DEVICE); i2c_active = false;
        return be16(b.data());
    }
    void i2c_write(unsigned dev, unsigned reg, unsigned value) {
        i2c_device = dev; i2c_register = reg; i2c_active = true;
        Bytes b(4); put16(b.data(), reg); put16(b.data() + 2, value);
        control(0x40, 0xbd, 0, USHORT(dev << 7), b);
        b = control(0xc0, 0xbd, 15, 0, Bytes(2));
        if (b[0] != 8) { reason("i2c_write_status_failed"); trace("i2c_error", "device=0x%02x register=0x%04x busStatus=0x%04x", dev, reg, be16(b.data())); }
        require(b[0] == 8, ERROR_IO_DEVICE); i2c_active = false;
    }
    unsigned dr(unsigned reg) { return i2c_read(0x90, reg); }
    void dw(unsigned reg, unsigned value) { i2c_write(0x90, reg, value); }
    void dg(unsigned bit, bool on) { dw(0x405, dr(0x405) & ~(1u << bit)); dw(0x406, (dr(0x406) & ~(1u << bit)) | (on ? 1u << bit : 0)); }
    void mode(unsigned n) {
        unsigned a = dr(0x38b), b = dr(0x38c);
        if (n == 4) a &= 0x7fff;
        else if (n == 2) { a &= 0x0fff; b &= 3; }
        else if (n == 0) { dw(0x38c, b | 3); b = (b | 3) & ~2u; }
        else if (n == 1) b |= 3;
        else throw DWORD(ERROR_INVALID_PARAMETER);
        dw(0x38b, a); dw(0x38c, b);
    }
    void demod_output(bool on) {
        unsigned a = dr(0x384), b = dr(0x500);
        dw(0x306, on ? 0x3fff : 0); dw(0x307, on ? 0xffff : 0); dw(0x308, on ? 0xffff : 0);
        dw(0x384, on ? a | 3 : a & ~3u); dw(0x500, b & 255);
    }
    void table(DWORD cursor, bool tuner) {
        for (unsigned group = 0; group < 100; ++group) {
            Bytes b = module.read(cursor, 4); unsigned count = be16(b.data()), reg = be16(b.data() + 2);
            if (!count) return;
            require(count <= 256, ERROR_FILE_INVALID); cursor += 4;
            b = module.read(cursor, count * 2);
            for (unsigned n = 0; n < count; ++n) {
                if (tuner) tw((reg + n) & 255, be16(b.data() + n * 2));
                else dw(reg + n, be16(b.data() + n * 2));
            }
            cursor += count * 2;
        }
        throw DWORD(ERROR_FILE_INVALID);
    }
    void demod_init() {
        stage("demodulator_initialize");
        gpio(0x90018, 0, false); gpio(0x90014, 0, true);
        gpio(0x90018, 6, true); gpio(0x90014, 6, true); Sleep(10); gpio(0x90018, 0, true);
        unsigned found = 0;
        for (unsigned dev : {0x90u, 0x12u}) {
            try {
                i2c_write(dev, 0x507, 3); i2c_read(dev, 0x380);
                unsigned ident = i2c_read(dev, 0x380), revision = i2c_read(dev, 0x381);
                if (ident == 0x1b3 && (revision & 0xfffc) == 0x8000 && (revision & 3) != 3) { found = dev; break; }
            } catch (DWORD) { }
        }
        require(found != 0, ERROR_NOT_READY);
        i2c_write(found, 0x506, 0x500); i2c_write(found, 0x505, (0x90 << 2) | 2);
        dr(0x380); unsigned ident = dr(0x380), revision = dr(0x381);
        require(ident == 0x1b3 && (revision & 0xfffc) == 0x8000 && (revision & 3) != 3);
        dw(0x505, 0x90 << 2); dw(0x506, 0);
        Bytes def = module.read(module.pointer(-0x7cf0) + 0x84, 32);
        DWORD base = module.pointer(-0x7d04);
        dw(0x507, 3); require(dr(0x380) == 0x1b3);
        dw(0x401, 0x100); dw(0x403, 60000 / 0x154); dw(0x404, 0);
        demod_output(false); mode(4);
        Bytes rows = module.read(base + 0x2c, 48);
        for (unsigned n = 0; n < 48; n += 4) dw(be16(rows.data() + n), be16(rows.data() + n + 2));
        dw(0x38a, 0x2d98); dw(0x385, be16(def.data() + 8));
        unsigned value = def[15] << 8 | def[14] << 5 | def[11] | def[13] << 4 | def[10] << 1 | 8;
        dw(0x386, value); dw(0x386, (value & ~8u) | def[12] << 3);
        dw(0x388, def[17] << 8 | (def[16] ? def[16] << 7 | 0x1c00 : 0));
        DWORD clock = be32(def.data()) * 1000, flags = be32(def.data() + 20);
        dw(0x17, clock >> 16); dw(0x18, clock); dw(0x1b, flags >> 16 & 0x1ff); dw(0x1c, flags); dw(0x1a, flags >> 25 & 3);
        dw(0x39a, be16(def.data() + 18)); dw(0x405, 0xffff); dw(0x406, 0); dw(0x408, 0xffff); dw(0x40d, 0);
        dw(0x12b, (dr(0x12b) & 0x50) | 0x22); dw(0x12c, 0x700); dw(0x506, 0); dw(0x50b, 0x205); dw(0x28, flags ? 0x1f55 : 0x755);
        table(base + 0x5c, false); dw(0x387, 0x31); dw(0x505, dr(0x505) & ~2u);
        DWORD tsclock = be32(def.data() + 24); dw(0x1d, tsclock >> 16); dw(0x1e, tsclock); mode(0);
        dw(0x39b, 0); dw(0x39c, 0x308); dw(0x39b, 1); dw(0x39b, 0); Sleep(1); mode(1);
        dw(0x11d, (dr(0x11d) & 0xff9f) | 0x20); demod_output(true);
        dg(0, false); Sleep(10); dg(5, false); Sleep(10); dg(5, true); Sleep(10);
    }
    void bridge_wait() {
        ULONGLONG end = GetTickCount64() + 2000;
        for (unsigned n = 0; n < 100 && GetTickCount64() < end; ++n) {
            unsigned s = dr(0x402);
            if (s & 0x100) { require((s & 0x80) != 0, ERROR_IO_DEVICE); return; }
        }
        throw DWORD(ERROR_TIMEOUT);
    }
    unsigned tr(unsigned reg) {
        dr(0x402); dw(0x400, reg << 8); dw(0x401, 0xc180 | 4 | 0x20); bridge_wait();
        dw(0x401, 0xc182 | 8 | 0x20 | 0x40); bridge_wait(); return dr(0x400);
    }
    void tw(unsigned reg, unsigned value) {
        value &= 65535;
        dr(0x402); dw(0x400, reg << 8 | value >> 8); dw(0x400, (value & 255) << 8);
        dw(0x401, 0xc180 | 12 | 0x20 | 0x40); bridge_wait();
    }
    int calibration_difference() {
        tw(0x1f, 7); Sleep(10); int first = int(tr(0x1d)); tw(0x1f, 4); Sleep(10);
        return int(int16_t(first - int(tr(0x1d))));
    }
    unsigned calibrate(unsigned reg, unsigned shift, bool invert) {
        Sleep(5); int difference = calibration_difference();
        unsigned index = invert ? (difference < 0 ? 16 : 0) : ((DWORD((difference < 0 ? -1 : 0) - difference) >> 27) & 16);
        unsigned base = tr(reg) & ~(31u << shift) & 65535;
        int best = abs(difference);
        while (best < 1023 && index != 15 && index != 31) {
            tw(reg, base | (++index << shift)); difference = calibration_difference();
            bool improved = abs(difference) < best; best = abs(difference); if (!improved) break;
        }
        unsigned value = (base | (((index - 1) & 255) << shift)) & 65535; tw(reg, value); return value;
    }
    void tuner_init() {
        stage("tuner_initialize");
        rf_config = Bytes(32); Bytes def = module.read(module.pointer(-0x7cf0), 12);
        std::copy(def.begin(), def.end(), rf_config.begin());
        put32(rf_config.data() + 4, be32(rf_config.data() + 4) | 0x80000000);
        put32(rf_config.data() + 16, DWORD(-143)); rf_config[20] = 0x80; rf_config[21] = 1; rf_config[22] = 0xc0;
        put16(rf_config.data() + 24, 100); put16(rf_config.data() + 26, 500); rf_config[28] = rf_config[29] = 1;
        tw(0x24, 0x20); tw(0x1b, 0xa2); tw(0x20, (((rf_config[6] - 1) << 11) | 0x300) & 0xfb00);
        DWORD word14 = be32(rf_config.data() + 20), word4 = be32(rf_config.data() + 4);
        tw(0x23, ((rf_config[29] << 5) | ((word14 >> 27) & 16) | ((rf_config[21] ? -0x4000 : 0) + 0x4600)) & 0xfff0);
        tw(0x21, ((word4 >> 16 & 0x8000) | (word4 >> 30 & 1) << 12 | (word4 >> 24 & 63) | (word4 >> 18 & 63) << 6 | 0x2000) & 0xbfff);
        unsigned identity = tr(0x1a), kind = (identity & 255) >> 5;
        require((identity >> 8 & 3) && (kind == 1 || kind == 4 || kind == 6 || kind == 7) && (identity & 31));
        table(module.pointer(-0x7cf8) + 0x2a0, true); tw(0x14, be32(rf_config.data()) < 24000 ? 2 : 1);
        tw(7, 0x48d); tw(0x24, 0x2ed); tw(1, 0x20d0); calibrate(6, 5, true);
        tw(7, 0x40d); unsigned field7 = calibrate(7, 11, true);
        tw(1, 0x3dd0); tw(7, field7 | 0x80); calibrate(6, 0, false);
        tw(7, field7 & 0xff7f); calibrate(6, 10, false); tw(7, field7 & 0xff77); tw(0x1f, 7);
        unsigned saved = tr(0x24), saved10 = tr(0x10); tw(0x10, (saved10 & 0xfb0f) | 0x400);
        tw(0x24, 0x2e9); Sleep(9); rf_baseline = tr(0x1d); tw(0x24, saved);
    }
    void rf_tune(DWORD frequency) {
        stage("rf_tune"); DWORD base = module.pointer(-0x7cf8); Bytes analog, divider;
        for (unsigned n = 0; n < 15; ++n) { Bytes b = module.read(base + 0x0c + n * 16, 16); if (be32(b.data()) >= frequency) { analog = b; break; } }
        for (unsigned n = 0; n < 18; ++n) { Bytes b = module.read(base + 0xfc + n * 8, 8); if (be32(b.data()) >= frequency) { divider = b; break; } }
        require(!analog.empty() && !divider.empty() && divider[7]);
        DWORD reference = be32(rf_config.data()); require(reference >= 10);
        DWORD vco = frequency * (divider[6] << 1) / divider[7], integer = vco / reference, remainder = vco % reference, numerator = 0;
        if (remainder > 99) {
            numerator = 0x13ec00;
            if (remainder > 199) {
                if (remainder > reference - 100) { ++integer; numerator = 0; }
                else numerator = std::min(remainder, reference - 200) * 0x1980;
            }
        }
        DWORD fraction = numerator / (reference / 10);
        unsigned reg19 = fraction ? (divider[4] ? 0x49e : 0x41d) : (divider[4] ? 0x49f : 0x41f);
        tw(0x0b, analog[4] | 0xb800); tw(0x15, integer); tw(0x16, fraction ? 0xff01 : 0x101); tw(0x17, fraction);
        tw(0x19, divider[5] << 11 | divider[4] << 7 | reg19); tw(0x1c, fraction ? 0xff2e : 0xfff8);
        tw(0x24, (be16(analog.data() + 14) & 0xff9f) | 6); Sleep(10);
        tw(0x10, 0x2b1); tw(0x1e, 0x32); Sleep(20);
        int step = 64, current = 64, best_value = 64, best_error = 3000, target = fraction ? 400 : 200;
        for (unsigned n = 0; n < 7; ++n) {
            step /= 2; tw(0x18, 0xe900 | (current & 65535)); Sleep(10);
            int sample = int(tr(0x1d)), error = abs(sample - target);
            if (error < best_error) { best_error = error; best_value = current; }
            current = int(int8_t(current + (sample < target ? step : -step))); Sleep(15);
        }
        tw(0x18, 0xe900 | (best_value & 65535)); Sleep(10); tw(0x1e, 0x7ff); tw(0x10, 0x9f83);
        tw(9, (analog[5] << 5 | analog[6]) & 0x1fff);
        for (unsigned n = 0; n < 3; ++n) tw(0x0c + n, be16(analog.data() + 8 + n * 2));
        Sleep(10); tw(0x0f, 0x7c00); tw(1, 0x8000); Sleep(10);
        Bytes gain = module.read(base + 0x28a, 22), limiter = module.read(base + 0x266, 14);
        tw(0x2a, 0xffff);
        for (unsigned n = 0; n < 6; ++n) tw(0x2c + n, be16(gain.data() + 6 + n * 2));
        for (unsigned n = 0; n < 2; ++n) tw(0x3e + n, be16(gain.data() + 18 + n * 2));
        tw(0x33, 0xffff);
        for (unsigned n = 0; n < 4; ++n) tw(0x35 + n, be16(limiter.data() + 6 + n * 2));
        tw(0x32, be16(gain.data()) ? 0x1800 : 0); tw(0x39, 0x400);
    }
    void agc(DWORD frequency) {
        stage("demodulator_agc"); mode(2);
        Bytes thresholds = module.read(module.pointer(-0x7cf0) + 0x0c, 24);
        unsigned offset = 0; bool found = false;
        for (unsigned n = 0; n < 24; n += 4) if (be16(thresholds.data() + n) * 1000u >= frequency) { offset = be16(thresholds.data() + n + 2); found = true; break; }
        require(found);
        unsigned override_value = std::min(4095u, ((rf_baseline * 144 / 33 + 1) / 2) + offset); dw(0x6a, override_value);
        Bytes g = module.read(module.pointer(-0x7cf0) + 0x24, 48); require(g[0] & 2);
        auto half = [&](unsigned n) { return be16(g.data() + n); };
        dw(0x4c, half(2)); dw(0x4d, half(4)); dw(0x4e, half(6)); dw(0x65, g[8] << 12 | half(10));
        dw(0x66, g[35] << 5 | g[36]); dw(0x67, g[37] << 6 | g[38]); dw(0x6a, override_value ? override_value : half(14));
        dw(0x6b, ((g[17] & 127) << 9 | g[39] << 8) & 0xff00);
        const unsigned offsets[] = {18,20,22,24,26,29,31,33};
        for (unsigned n = 0; n < 8; ++n) dw(0x6c + n, half(offsets[n]));
        dw(0x4b, g[28]); dw(0x39b, (dr(0x39b) & 0xffe3) | g[12] << 4 | g[16] << 2);
        Sleep(70); dw(0x302, 0xa00); dw(0x302, 0); Sleep(50); dg(6, true); Sleep(70);
        require(!g[39] || !half(42), ERROR_NOT_SUPPORTED);
    }
    struct Layer { unsigned segments, modulation, rate, interleave; };
    struct Tmcc { unsigned mode, guard, inversion, partial, parameter; std::array<Layer,3> layers; };
    Tmcc tmcc() {
        unsigned raw = dr(0x23a), code = raw >> 4 & 3;
        Tmcc t = {code == 1 ? 0u : code == 2 ? 2u : 1u, raw & 3, raw >> 6 & 1, dr(0x1f9) & 1, dr(0x1fc) & 1, {}};
        for (unsigned n = 0; n < 3; ++n) {
            unsigned rate = dr(0x1e1 + n), mod = dr(0x1e7 + n), segments = dr(0x1ed + n), interleave = dr(0x1f3 + n);
            t.layers[n] = {segments & 15, mod == 0 ? 3u : mod == 1 ? 0u : mod == 2 ? 1u : 2u, rate & 7, interleave & 3};
        }
        return t;
    }
    bool locked() {
        unsigned s = dr(0x238); bool viterbi = false;
        status.rf_snapshot_valid = FALSE;
        for (unsigned n = 0; n < 3; ++n) { status.rf_viterbi[n] = dr(0x22a + n); viterbi |= (status.rf_viterbi[n] & 1) != 0; }
        status.rf_status = s; status.rf_snapshot_tick = GetTickCount64(); status.rf_snapshot_valid = TRUE;
        return (s & 0x2000) && (s & 0x100) && ((s >> 1 & 15) == 15) && (s & 0x1000) && (s >> 5 & 7) && viterbi;
    }
    // DiB8000 (8000/8001/8002): noise 542, signal 543; 8-bit mantissa,
    // signed 6-bit binary exponent. Independent floating-point S/N decoding.
    static bool decode_cnr(unsigned noise, unsigned signal, float &db) {
        unsigned nm = noise >> 6 & 255, sm = signal >> 6 & 255;
        if (!nm || !sm) return false;
        int ne = int(noise & 63), se = int(signal & 63);
        if (ne >= 32) ne -= 64;
        if (se >= 32) se -= 64;
        double value = 10.0 * (std::log10(double(sm) / nm) + (se - ne) * std::log10(2.0));
        if (!std::isfinite(value)) return false;
        db = float(value); return true;
    }
    void sample_signal(bool rf_locked) {
        status.cnr_valid = FALSE; status.cnr_error = 0;
        if (!rf_locked) return;
        TorneStatus saved = status;
        try {
            unsigned noise = dr(0x21e), signal = dr(0x21f);
            status.cnr_noise_raw = noise; status.cnr_signal_raw = signal;
            status.cnr_tick = GetTickCount64();
            status.cnr_valid = decode_cnr(noise, signal, status.cnr_db);
            trace("signal_sample", "valid=%d cnrDb=%.3f noise21e=0x%04x signal21f=0x%04x",
                  status.cnr_valid, status.cnr_valid ? double(status.cnr_db) : 0.0, noise, signal);
        } catch (DWORD error) {
            trace("signal_sample_failed", "error=%lu io=%s", (unsigned long)error, status.io_operation);
            status = saved; status.cnr_error = error; i2c_active = false;
        }
    }
    void acquire(const Tmcc *captured = NULL) {
        stage(captured ? "final_acquisition" : "auto_acquisition");
        Bytes def = module.read(module.pointer(-0x7cf0) + 0x84, 32); require(be32(def.data() + 20) == 0, ERROR_NOT_SUPPORTED);
        Tmcc p = captured ? *captured : Tmcc{1,2,0,1,0, {{{13,2,2,0},{0,2,255,255},{0,2,255,255}}}};
        require(!p.parameter && p.guard < 4, ERROR_NOT_SUPPORTED);
        for (const auto &l : p.layers) require(!l.segments || l.modulation != 3, ERROR_NOT_SUPPORTED);
        unsigned fft = p.mode == 0 ? 1 : p.mode == 2 ? 2 : 3;
        DWORD clock = be32(def.data() + 24); dw(0x1d, clock >> 16); dw(0x1e, clock);
        dw(0x11d, dr(0x11d) & 0x60); dw(0x1a, (dr(0x1a) & 1) ^ p.inversion);
        dw(0x1b, 0); dw(0x1c, 0); dw(0x1a, 0); dw(10, captured ? 0 : 0x70); dw(1, 0x3ffc | p.guard);
        unsigned active = 3;
        for (unsigned n = 0; n < 3; ++n) {
            const auto &l = p.layers[n];
            unsigned coding = l.rate == 1 ? 8 : l.rate == 2 ? 16 : l.rate == 3 ? 24 : l.rate == 5 ? 40 : 56;
            unsigned interleave = l.interleave < 4 ? l.interleave : 0;
            const unsigned mod_bits[] = {0x400,0x800,0xc00,0};
            require(l.modulation < 4);
            dw(2 + n, l.segments << 6 | mod_bits[l.modulation] | coding | interleave);
            if (l.segments) {
                if (active == 1 && l.modulation == 2) active = 2;
                else if ((active == 0 || active == 3) && (l.modulation == 1 || l.modulation == 2)) active = l.modulation;
            }
        }
        unsigned base = p.mode == 0 ? 64 : p.mode == 2 ? 128 : 256;
        dw(0x112, (dr(0x112) & 0xffcf) | p.partial << 5); dw(0x110, 0x200);
        dw(0x111, (dr(0x111) & 0xfff0) | 6); dw(0x10e, 1); dw(0x10f, 0);
        dw(0, fft << 13); dw(0x10c, 0x427); dw(0x160, 0); dw(0x161, 0x1fff);
        dw(0x15f, 0xd5); dw(0xb4, 0x409); dw(0xbb, 0x4102);
        for (unsigned reg = 0xb5; reg < 0xbb; ++reg) dw(reg, 0x2800);
        dw(0x152, 0x1461); dw(0x154, 0x206); dw(0x155, 0x27); dw(0xb2, 0x20);
        dw(0xbd, 0xe000); dw(0xc0, 0xe000); dw(0xe1, 0xe000); dw(0x10a, 0xe040); dw(0x11f, 0xf000);
        dw(0x120, captured ? 0 : 0x1fff); dw(0xd3, 0x1fff); dw(0x20, (9 - fft) << 12 | 0x680); dw(0x25, (8 - fft) | 0x60);
        dw(0x111, (dr(0x111) & 15) | ((base << p.guard) * 3 / 2 + 0x90) << 4);
        Bytes address = module.read(0x3db34, 4); DWORD tab = be32(address.data());
        table(tab + (active == 2 ? 0x1be : active == 1 ? 0x1cc : 0x290), false);
        dw(0x74, active == 1 || active == 2 ? 7 : 0);
        Bytes b = module.read(tab + 0x1da, 20);
        for (unsigned n = 0; n < 10; ++n) dw(0x50 + n, be16(b.data() + n * 2) - (active == 1 || active == 2 ? 0 : 0x163));
        b = module.read(tab + 0x1f0, 48);
        for (unsigned n = 0; n < 24; ++n) dw(0x75 + n, be16(b.data() + n * 2));
        unsigned segments = 0; for (const auto &l : p.layers) segments += l.segments;
        dw(0xde, 1); dw(0xdf, 0x1000); dw(0xe4, 0); dw(0x109, 0x1f); dw(0xcd, 0x200f);
        dw(0x122, segments << 7); dw(0x123, segments << 7); dw(0x124, segments << 7); dw(0xfa, 0xcd5);
        if (captured) {
            dw(0x302, 0x4000); dw(0x302, 0); Sleep(245);
            dw(0x20, ((11 - fft) << 12 | 0x680) & 0xf6e0); dw(0x25, (10 - fft) | (fft + 5) << 5);
            if (dr(0x238) & 0x800) { dw(0x1d, dr(0x1b3)); dw(0x1e, dr(0x1b4)); }
            dw(6, 0x200); unsigned v = dr(0x387); dw(0x387, v & ~8u); Sleep(1); dw(0x387, v | 8);
        } else {
            clock = be32(def.data()); dw(6, 4); dw(7, 8); dw(8, 0x1000);
            dw(0x0b, clock * 50 >> 16); dw(0x0c, clock * 50); dw(0x0d, clock * 100 >> 16); dw(0x0e, clock * 100);
            dw(0x0f, clock * 1000 >> 16); dw(0x10, clock * 1000);
            unsigned original = dr(0); dw(0, original | 0x8000); dr(0x504); dw(0, original);
        }
    }
    void tune_rf(DWORD physical) {
        DWORD frequency = 473143 + (physical - 13) * 6000;
        ULONGLONG begin = GetTickCount64(); status.rf_snapshot_valid = FALSE;
        trace("rf_tune_begin", "physicalChannel=%lu frequencyKHz=%lu", (unsigned long)physical, (unsigned long)frequency);
        demod_output(false); mode(2); mode(0); dg(0, false); dg(9, true); dg(8, false); dg(7, true);
        rf_tune(frequency); agc(frequency); acquire();
        ULONGLONG deadline = GetTickCount64() + 5000; unsigned search = 0;
        do { search = dr(0x504); status.rf_search = search; if (search & 3) break; Sleep(50); } while (GetTickCount64() < deadline);
        Tmcc t = tmcc(); if (!(search & 1) && (search & 2)) acquire(&t);
        deadline = GetTickCount64() + 5000; unsigned consecutive = 0;
        do { consecutive = locked() ? consecutive + 1 : 0; if (consecutive >= 3) break; Sleep(100); } while (GetTickCount64() < deadline);
        trace("rf_acquisition", "physicalChannel=%lu elapsedMs=%llu stableLock=%s search504=0x%04lx status238=0x%04lx viterbi22a=0x%04lx viterbi22b=0x%04lx viterbi22c=0x%04lx tmccMode=%u guard=%u partial=%u layerSegments=%u,%u,%u",
            (unsigned long)physical, (unsigned long long)(GetTickCount64() - begin), consecutive >= 3 ? "true" : "false",
            (unsigned long)status.rf_search, (unsigned long)status.rf_status,
            (unsigned long)status.rf_viterbi[0], (unsigned long)status.rf_viterbi[1], (unsigned long)status.rf_viterbi[2],
            t.mode, t.guard, t.partial, t.layers[0].segments, t.layers[1].segments, t.layers[2].segments);
    }
    void set_output(bool enabled) {
        dw(0x12b, (dr(0x12b) & 0x50) | 0x22); dw(0x12c, 0x700); dw(0x506, enabled ? 0x400 : 0); dw(0x50b, 0x205);
    }
    void start_stream() {
        if (!locked()) {
            status.cnr_valid = FALSE;
            reason("no_rf_lock");
            trace("no_rf_lock", "search504=0x%04lx status238=0x%04lx viterbi=0x%04lx,0x%04lx,0x%04lx",
                (unsigned long)status.rf_search, (unsigned long)status.rf_status,
                (unsigned long)status.rf_viterbi[0], (unsigned long)status.rf_viterbi[1], (unsigned long)status.rf_viterbi[2]);
            return;
        }
        stage("start_stream"); require(word(0x82008) == 1);
        // FR 0x802da348: bits 11..8 of the BE word at 0x1018 select PMT ES.
        // Mode 4 passes DSM-CC and source PSI; retain the other setting bits.
        Bytes selection = read(0x101a, 2); require(selection.size() == 2);
        selection[0] = BYTE((selection[0] & 0xf0) | 4);
        write(0x101a, selection); require(read(0x101a, 2) == selection);
        // FR 0x802da628: a distinct output PMT filter, default mode 3 drops
        // DSM-CC even after the input ES selector admits its packets.
        // Mode 4 keeps every ES. This firmware still repacketizes the TS.
        Bytes pmt = read(0x1136, 2); require(pmt.size() == 2);
        pmt[1] = BYTE((pmt[1] & ~0x70) | 0x40);
        write(0x1136, pmt); require(read(0x1136, 2) == pmt);
        status.es_selection_mode = selection[0] & 15;
        status.pmt_output_mode = pmt[1] >> 4 & 7;
        status.psi_settings_verified = TRUE;
        write_word(0x82000, word(0x82000) & ~0x20u); set_output(true); pending(); write_word(0x1018, 0);
        api(4, 0x20); wait_state(1); api(5, 0, 2); wait_state(2);
        status.streaming = TRUE; last_data = last_rf_lock = GetTickCount64(); pending_media.clear();
        sample_signal(true);
        framing_locked = false;
        status.input_packet_bytes = 0;
        psi.reset();
        status.valid_pmt_sections = status.invalid_psi_crc = status.fullseg_bml_programs =
            status.oneseg_bml_programs = status.dsmcc_es = 0;
    }
    void stop_stream() {
        status.cnr_valid = FALSE;
        stage("stop_stream.read_state"); unsigned state = word(0x82008); status.state = state;
        trace("stop_begin", "firmwareState=0x%04x streaming=%d", state, status.streaming);
        if (state == 2) {
            stage("stop_stream.request_stop");
            api(5, 0, 4); ULONGLONG end = GetTickCount64() + 3000;
            stage("stop_stream.drain_until_stopped");
            while (word(0x82008) == 2 && GetTickCount64() < end) { pipe(0x81, 32768); pending(); }
            wait_state(4); state = 4;
        }
        if (state != 0xffff) {
            stage("stop_stream.disable_output"); set_output(false);
            if (state == 3 || state == 4) { stage("stop_stream.enter_standby"); api(5, 0, 1); wait_state(1); }
        }
        status.state = word(0x82008); status.streaming = FALSE; pending_media.clear();
        if (status.state != 1) reason(status.state == 0xffff ? "firmware_fatal_state" : "unexpected_stop_state");
        require(status.state == 1);
        // Firmware can reach standby while the bulk endpoint still has data.
        ULONGLONG end = GetTickCount64() + 3000;
        stage("stop_stream.drain_bulk");
        while (!pipe(0x81, 32768).empty()) {
            if (GetTickCount64() >= end) reason("stop_bulk_drain_timeout");
            require(GetTickCount64() < end, ERROR_TIMEOUT);
        }
        stage("stop_stream.flush_bulk");
        snprintf(status.io_operation, sizeof(status.io_operation), "flush_pipe endpoint=0x81");
        if (!WinUsb_FlushPipe(usb, 0x81)) { status.io_error = GetLastError(); reason("usb_flush_pipe_failed"); throw status.io_error; }
        trace("stop_complete", "firmwareState=0x%04lx", (unsigned long)status.state);
    }
    void prepare() {
        stage("prepare"); unsigned state = word(0x82008); status.state = state;
        trace("prepare_begin", "firmwareState=0x%04x variant=%lu", state, (unsigned long)operational_variant);
        if (state >= 2 && state <= 4) { stop_stream(); state = word(0x82008); }
        if (state == 0) {
            stage("idle_firmware"); status.sequence = 0; pipe(0x83, 16);
            write_word(0x90078, 0x0ee4); read(0x90078); write_word(0x901b4, 0);
            bool irq = false; ULONGLONG end = GetTickCount64() + 3000;
            do { Bytes b = pipe(0x83, 16); if (!b.empty() && b[0] == 0x20) { irq = true; break; } } while (GetTickCount64() < end);
            if (!irq) { unsigned bits = pending(); require(bits & 4, ERROR_TIMEOUT); }
            transfer(idle); write_word(0x90070, 4); wait_state(0x10);
        } else if (state == 0xffff) { pending(); api(3); status.sequence = 0; wait_state(0x10); }
        else if (state == 1) { api(1); wait_state(0x11); api(2, 1); wait_state(0x12); api(2); wait_state(0x10); }
        else require(state == 0x10);
        authenticate(); stage("operational_firmware"); write_word(0x90000, 0);
        for (auto row : {std::array<int,3>{-0x7d5c,0x500,0x1000}, std::array<int,3>{-0x7d58,0x210,0x1500}}) {
            Bytes data = module.read(module.pointer(row[0]), size_t(row[1]));
            for (size_t off = 0; off < data.size(); off += 64) {
                size_t length = std::min<size_t>(64, data.size() - off);
                write(DWORD(row[2]) + DWORD(off), Bytes(data.begin() + off, data.begin() + off + length));
            }
        }
        api(4); transfer(operational); api(4, 0x20); wait_state(1);
        status.firmware_variant = operational_variant;
        status.bcas_skipped = operational_variant == 12;
        runtime_auth();
        if (!status.bcas_skipped) { card_startup(); card_initial_setting(); }
        runtime_auth();
        stage("media_session_open"); Bytes b(12); put32(b.data(), 3); Bytes reply = seqcom(0x701, b, 6);
        media_open = true; status.media_closed = FALSE; require(reply.size() >= 32);
        media_key.assign(reply.begin() + 16, reply.begin() + 32);
        media_cbc = new Aes(media_key); media_ecb = new Aes(media_key, true);
        SecureZeroMemory(reply.data(), reply.size());
        demod_init(); tuner_init(); prepared = true;
    }
    void convert(const Bytes &chunk, BYTE *out, DWORD capacity, DWORD &size) {
        const bool verify_crc = operational_variant == 4 || operational_variant == 5 || operational_variant == 6 || operational_variant == 7 || operational_variant == 8 || operational_variant == 9 || operational_variant == 10 || operational_variant == 11 || operational_variant == 12;
        const bool preserve_header = operational_variant == 3 || verify_crc;
        size = 0;
        pending_media.insert(pending_media.end(), chunk.begin(), chunk.end());
        if (!framing_locked) {
            // A new USB session may start within the final old media packet.
            if (pending_media.size() < 188 * 3) return;
            size_t offset = 0; bool found = false;
            for (unsigned stride : {192u, 188u}) {
                if (preserve_header && stride != 192) continue;
                unsigned prefix = stride == 192 ? 4 : 0;
                for (offset = 0; offset < stride; ++offset) {
                    if (offset + 2 * stride + prefix + 4 > pending_media.size()) break;
                    bool valid = true;
                    for (size_t n = 0; n < 3; ++n) {
                        const BYTE *p = pending_media.data() + offset + n * stride + prefix;
                        valid &= p[0] == 0x47 && (preserve_header || (p[3] & 0x30) != 0);
                    }
                    if (valid) { packet_stride = stride; packet_prefix = prefix; found = true; break; }
                }
                if (found) break;
            }
            require(found);
            status.discarded_bytes += offset;
            pending_media.erase(pending_media.begin(), pending_media.begin() + offset);
            framing_locked = true;
            status.input_packet_bytes = packet_stride;
        }
        size_t complete = pending_media.size() / packet_stride;
        require(complete * 188 <= capacity, ERROR_INSUFFICIENT_BUFFER);
        size = 0;
        for (size_t n = 0; n < complete; ++n) {
            BYTE *packet = pending_media.data() + n * packet_stride + packet_prefix;
            require(packet[0] == 0x47);
            unsigned control = packet[3] >> 4 & 3;
            require(preserve_header || control != 0);
            unsigned start = 4 + (control & 2 ? 1 + packet[4] : 0);
            if (preserve_header) start = std::min(start, 188u); else require(start <= 188);
            unsigned scramble = packet[3] >> 6;
            if (!preserve_header) require(packet_stride == 188 ? scramble != 1 : scramble < 2, ERROR_NOT_SUPPORTED);
            unsigned pid = (unsigned(packet[1] & 31) << 8) | packet[2];
            bool encrypted = preserve_header ? pid != 0x1fff && control != 2 && start < 188
                                             : packet_stride == 192 && scramble == 1 && (control & 1);
            if (encrypted) {
                unsigned length = 188 - start, full = length & ~15u;
                BYTE previous[16]; memcpy(previous, full ? packet + start + full - 16 : iv.data(), 16);
                media_cbc->crypt(packet + start, full, false, iv.data());
                if (length != full) {
                    media_ecb->crypt(previous, 16, true, NULL, true);
                    for (unsigned i = 0; i < length - full; ++i) packet[start + full + i] ^= previous[i];
                }
                SecureZeroMemory(previous, sizeof(previous));
                if (!preserve_header) packet[3] &= 0x3f;
                ++status.encrypted_packets;
            }
            if (verify_crc) {
                require(ts_crc32(packet) == be32(pending_media.data()+n*packet_stride));
                ++status.crc_verified_packets;
            }
            psi.feed(packet);
            memcpy(out + size, packet, 188); size += 188; ++status.packets;
        }
        status.valid_pmt_sections = psi.valid_pmts; status.invalid_psi_crc = psi.invalid_crc;
        status.fullseg_bml_programs = psi.fullseg_programs; status.oneseg_bml_programs = psi.oneseg_programs;
        status.dsmcc_es = psi.dsm_es;
        pending_media.erase(pending_media.begin(), pending_media.begin() + complete * packet_stride);
    }
};

static void open_interface(TorneNative &s, const GUID &guid, std::wstring &selected) {
    HDEVINFO devices = SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    require(devices != INVALID_HANDLE_VALUE, GetLastError());
    try {
        for (DWORD n = 0;; ++n) {
            SP_DEVICE_INTERFACE_DATA info = {}; info.cbSize = sizeof(info);
            if (!SetupDiEnumDeviceInterfaces(devices, NULL, &guid, n, &info)) {
                require(GetLastError() == ERROR_NO_MORE_ITEMS, GetLastError()); break;
            }
            DWORD needed = 0; SetupDiGetDeviceInterfaceDetailW(devices, &info, NULL, 0, &needed, NULL);
            require(needed >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W));
            Bytes detail(needed); auto p = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(detail.data()); p->cbSize = sizeof(*p);
            wincheck(SetupDiGetDeviceInterfaceDetailW(devices, &info, p, needed, NULL, NULL));
            std::wstring path(p->DevicePath), lower = path;
            std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
            if (lower.find(L"vid_054c&pid_04b2") == std::wstring::npos) continue;
            require(selected.empty() || selected == path, ERROR_DUP_NAME); selected = path;
        }
        SetupDiDestroyDeviceInfoList(devices);
    } catch (...) { SetupDiDestroyDeviceInfoList(devices); throw; }
    (void)s;
}
static void discover(TorneNative &s) {
    HKEY root = NULL;
    LSTATUS result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_054C&PID_04B2", 0, KEY_READ, &root);
    require(result == ERROR_SUCCESS, DWORD(result));
    std::wstring selected;
    try {
        for (DWORD n = 0;; ++n) {
            WCHAR name[256]; DWORD count = 256;
            result = RegEnumKeyExW(root, n, name, &count, NULL, NULL, NULL, NULL);
            if (result == ERROR_NO_MORE_ITEMS) break;
            require(result == ERROR_SUCCESS, DWORD(result));
            HKEY key = NULL; std::wstring sub = std::wstring(name) + L"\\Device Parameters";
            result = RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &key);
            if (result == ERROR_FILE_NOT_FOUND) continue;
            require(result == ERROR_SUCCESS, DWORD(result));
            DWORD type = 0, bytes = 0;
            result = RegQueryValueExW(key, L"DeviceInterfaceGUIDs", NULL, &type, NULL, &bytes);
            if (result != ERROR_SUCCESS) { RegCloseKey(key); continue; }
            if ((type != REG_MULTI_SZ && type != REG_SZ) || bytes > 65536) { RegCloseKey(key); continue; }
            std::vector<WCHAR> values(bytes / sizeof(WCHAR) + 2, 0);
            result = RegQueryValueExW(key, L"DeviceInterfaceGUIDs", NULL, &type, reinterpret_cast<BYTE *>(values.data()), &bytes);
            RegCloseKey(key); require(result == ERROR_SUCCESS, DWORD(result));
            for (const WCHAR *p = values.data(); *p; p += wcslen(p) + 1) {
                GUID guid; require(SUCCEEDED(CLSIDFromString(p, &guid)));
                open_interface(s, guid, selected);
            }
        }
        RegCloseKey(root);
    } catch (...) { RegCloseKey(root); throw; }
    require(!selected.empty(), ERROR_DEVICE_NOT_CONNECTED);
    s.device = CreateFileW(selected.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    require(s.device != INVALID_HANDLE_VALUE, GetLastError());
    wincheck(WinUsb_Initialize(s.device, &s.usb));
    USB_DEVICE_DESCRIPTOR descriptor = {}; ULONG got = 0;
    wincheck(WinUsb_GetDescriptor(s.usb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, reinterpret_cast<BYTE *>(&descriptor), sizeof(descriptor), &got));
    require(got == sizeof(descriptor) && descriptor.idVendor == 0x054c && descriptor.idProduct == 0x04b2);
    USB_INTERFACE_DESCRIPTOR iface = {}; wincheck(WinUsb_QueryInterfaceSettings(s.usb, 0, &iface));
    unsigned pipes = 0;
    for (BYTE i = 0; i < iface.bNumEndpoints; ++i) {
        WINUSB_PIPE_INFORMATION pipe = {}; wincheck(WinUsb_QueryPipe(s.usb, 0, i, &pipe));
        if (pipe.PipeId == 0x81 && pipe.PipeType == UsbdPipeTypeBulk) pipes |= 1;
        if (pipe.PipeId == 0x02 && pipe.PipeType == UsbdPipeTypeBulk) pipes |= 2;
        if (pipe.PipeId == 0x83 && pipe.PipeType == UsbdPipeTypeInterrupt) pipes |= 4;
    }
    require(pipes == 7);
    for (auto policy : {std::array<ULONG,2>{0,1000}, {2,3000}, {0x83,500}, {0x84,1000}, {0x81,250}}) {
        ULONG timeout = policy[1]; wincheck(WinUsb_SetPipePolicy(s.usb, UCHAR(policy[0]), PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout));
    }
}
template<class F> static BOOL guarded(TorneNative *s, F fn) {
    s->reason(""); s->status.io_error = 0; s->i2c_active = false;
    try { fn(); s->status.error = 0; return TRUE; }
    catch (DWORD error) { s->status.error = error; }
    catch (const std::bad_alloc &) { s->status.error = ERROR_NOT_ENOUGH_MEMORY; }
    catch (...) { s->status.error = ERROR_GEN_FAILURE; }
    s->trace_error(s->status.error);
    return FALSE;
}
extern "C" void torne_set_logger(TorneNative *s, TorneLogCallback logger, void *context) {
    s->logger = logger; s->logger_context = context;
    s->trace("native_session", "variant=%lu originalFirmwareSHA256=653f2377c838a4e7276b447fd992084d5e6e84a90ac4d389851f82309b662a04", (unsigned long)s->operational_variant);
}
extern "C" TorneNative *torne_create(const WCHAR *firmware, const WCHAR *module, const WCHAR *media, DWORD *error) {
    try { auto s = new TorneNative(firmware, module, media); *error = 0; return s; }
    catch (DWORD e) { *error = e; }
    catch (...) { *error = ERROR_NOT_ENOUGH_MEMORY; }
    return NULL;
}
extern "C" BOOL torne_open(TorneNative *s) { return guarded(s, [&] { s->stage("open_winusb"); discover(*s); }); }
extern "C" BOOL torne_use_firmware(TorneNative *s, const WCHAR *path) {
    return guarded(s, [&] { s->use_firmware(path); });
}
extern "C" BOOL torne_tune(TorneNative *s, DWORD physical) {
    return guarded(s, [&] {
        require(physical >= 13 && physical <= 52, ERROR_INVALID_PARAMETER);
        s->trace("tune_begin", "physicalChannel=%lu prepared=%d state=0x%04lx", (unsigned long)physical, s->prepared, (unsigned long)s->status.state);
        s->protocol_started = true;
        if (!s->prepared) s->prepare(); else s->stop_stream();
        s->runtime_auth(); s->tune_rf(physical); s->runtime_auth(); s->start_stream();
        s->last_data = s->last_rf_lock = GetTickCount64(); s->stage("receiving");
        s->trace("tune_result", "physicalChannel=%lu streaming=%d reason=%s state=0x%04lx variant=%lu", (unsigned long)physical,
            s->status.streaming, s->status.failure_reason, (unsigned long)s->status.state, (unsigned long)s->operational_variant);
    });
}
extern "C" BOOL torne_receive(TorneNative *s, BYTE *data, DWORD capacity, DWORD *size) {
    *size = 0;
    return guarded(s, [&] {
        if (!s->prepared) return;
        if (GetTickCount64() - s->last_auth >= 1000) {
            s->runtime_auth();
            if (!s->status.streaming) s->start_stream();
            else {
                bool rf_locked = s->locked();
                if (rf_locked) s->last_rf_lock = GetTickCount64();
                s->sample_signal(rf_locked);
                s->stage("rf_lock_watchdog");
                if (GetTickCount64() - s->last_rf_lock > 5000) s->reason("rf_lock_lost_for_5s");
                require(GetTickCount64() - s->last_rf_lock <= 5000, ERROR_TIMEOUT);
            }
        }
        s->stage("receiving");
        if (!s->status.streaming) {
            s->reason("no_rf_lock_stream_not_started");
            require(GetTickCount64() - s->last_data <= 5000, ERROR_TIMEOUT);
            return;
        }
        Bytes chunk = s->pipe(0x81, 32768);
        if (!chunk.empty()) {
            s->status.usb_bytes += chunk.size(); s->convert(chunk, data, capacity, *size);
            if (*size) s->last_data = GetTickCount64();
        }
        if (GetTickCount64() - s->last_data > 5000) s->reason("no_ts_output_for_5s");
        require(GetTickCount64() - s->last_data <= 5000, ERROR_TIMEOUT);
    });
}
extern "C" BOOL torne_purge(TorneNative *s) {
    return guarded(s, [&] {
        // Retain the tuner, card and media session. The established stop path
        // drains firmware stop traffic, empties the USB pipe, flushes WinUSB
        // and clears partial host framing before another stream starts.
        if (!s->prepared) return;
        const bool restart = s->status.streaming;
        s->stop_stream();
        if (restart) { s->runtime_auth(); s->start_stream(); }
        s->stage("purged");
    });
}
extern "C" BOOL torne_close(TorneNative *s) {
    BOOL ok = TRUE;
    if (s->usb) {
        ok = guarded(s, [&] {
            if (!s->protocol_started) { s->stage("closed"); return; }
            unsigned state = s->word(0x82008);
            if (state >= 1 && state <= 4) s->stop_stream();
            if (s->media_open && s->word(0x82008) != 0xffff) {
                s->stage("media_session_close"); Bytes b(8); put32(b.data(), 3); s->seqcom(0x703, b, 4);
                s->media_open = false; s->status.media_closed = TRUE;
            }
            s->status.state = s->word(0x82008);
            Bytes errors = s->read(0x80080, 16); require(std::all_of(errors.begin(), errors.end(), [](BYTE b) { return b == 0; }));
            s->stage("closed");
        });
        WinUsb_Free(s->usb); s->usb = NULL;
    }
    if (s->device != INVALID_HANDLE_VALUE) { CloseHandle(s->device); s->device = INVALID_HANDLE_VALUE; }
    return ok;
}
extern "C" void torne_destroy(TorneNative *s) { delete s; }
extern "C" void torne_status(TorneNative *s, TorneStatus *status) { *status = s->status; }
