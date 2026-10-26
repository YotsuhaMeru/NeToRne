/* Standalone native verification: no Python interpreter is loaded or launched. */
#include "../bondriver/TorneNative.cpp"

static Bytes hex(const char *text) {
    Bytes b;
    while (*text) {
        unsigned n = 0; require(sscanf(text, "%2x", &n) == 1);
        b.push_back(BYTE(n)); text += 2;
    }
    return b;
}
static void crypto_tests() {
    Bytes key = hex("2b7e151628aed2a6abf7158809cf4f3c"), iv = hex("000102030405060708090a0b0c0d0e0f");
    Bytes plain = hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710");
    Bytes encrypted = hex("7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b273bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7");
    Aes aes(key); Bytes b = plain; aes.crypt(b.data(), ULONG(b.size()), true, iv.data()); require(b == encrypted);
    aes.crypt(b.data(), ULONG(b.size()), false, iv.data()); require(b == plain);
    Aes ecb(key, true); b.assign(plain.begin(), plain.begin() + 16); ecb.crypt(b.data(), 16, true, NULL, true);
    require(b == hex("3ad77bb40d7a3660a89ecaf32466ef97"));
    check_digest(Bytes{'a','b','c'}, BCRYPT_SHA1_ALGORITHM, "a9993e364706816aba3e25717850c26c9cd0d89d");
    check_digest(Bytes{'a','b','c'}, BCRYPT_SHA256_ALGORITHM, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
static void media_tests(TorneNative &s) {
    s.media_key = hex("000102030405060708090a0b0c0d0e0f");
    s.iv = hex("101112131415161718191a1b1c1d1e1f");
    s.media_cbc = new Aes(s.media_key); s.media_ecb = new Aes(s.media_key, true);
    /* Fixture produced independently by PyCryptodome, including the CBC tail. */
    Bytes cipher = hex("954f64f2e4e86e9eee82d20216684899a93b9ddb22e8ab104c61e728831d6d5a1f51afc19343249ef3b4495be12dc4f3e0b296f5c91be273a66c1216a66da5fd24dbfbf2953d5862f57436198ada0a437e92bdee860d80b1c4014a69edecddc6f5251d19ef0ccd3192c0516cd6d54744971c1b6a1ac2fbef3d3275522402fa92c6a46856f35678eb9422d2fbad27f763b7d9b91567cecb9702177e22e0ed5e98c33d82965259ab55ee513a1d62f535f933507524b23f0ab6");
    Bytes packet(192); packet[4] = 0x47; packet[5] = 0x40; packet[6] = 0x20; packet[7] = 0x50;
    std::copy(cipher.begin(), cipher.end(), packet.begin() + 8);
    BYTE out[1024]; DWORD size = 0;
    s.framing_locked = true;
    for (unsigned split = 1; split < 192; ++split) {
        s.pending_media.clear();
        s.convert(Bytes(packet.begin(), packet.begin() + split), out, sizeof(out), size); require(size == 0);
        s.convert(Bytes(packet.begin() + split, packet.end()), out, sizeof(out), size); require(size == 188 && s.pending_media.empty());
        require(out[0] == 0x47 && out[3] == 0x10);
        for (unsigned n = 0; n < 184; ++n) require(out[4 + n] == n);
    }
    Bytes pair = packet; pair.insert(pair.end(), packet.begin(), packet.end());
    s.convert(pair, out, sizeof(out), size); require(size == 376 && memcmp(out, out + 188, 188) == 0);
    // Payloads shorter than one block use AES(IV) as their mask.
    Bytes short_packet(192); short_packet[4] = 0x47; short_packet[7] = 0x70; short_packet[8] = 175;
    Bytes mask = hex("07feef74e1d5036e900eee118e949293");
    for (unsigned n = 0; n < 8; ++n) short_packet[184 + n] = BYTE(n) ^ mask[n];
    s.convert(short_packet, out, sizeof(out), size); require(size == 188 && out[3] == 0x30);
    for (unsigned n = 0; n < 8; ++n) require(out[180 + n] == n);
    for (unsigned kind = 0; kind < 4; ++kind) {
        Bytes bad = packet;
        if (kind == 0) bad[4] = 0;
        if (kind == 1) bad[7] = 0;
        if (kind == 2) { bad[7] = 0x30; bad[8] = 184; }
        if (kind == 3) bad[7] = 0x90;
        bool rejected = false;
        try { s.convert(bad, out, sizeof(out), size); } catch (DWORD) { rejected = true; }
        require(rejected); s.pending_media.clear();
    }
    bool bounded = false;
    try { s.convert(packet, out, 187, size); } catch (DWORD e) { bounded = e == ERROR_INSUFFICIENT_BUFFER; }
    require(bounded); s.pending_media.clear();
    for (unsigned offset = 0; offset < 192; ++offset) {
        s.framing_locked = false; s.pending_media.clear();
        Bytes start(offset, 0); for (unsigned n = 0; n < 3; ++n) start.insert(start.end(), packet.begin(), packet.end());
        s.convert(start, out, sizeof(out), size); require(size == 564 && s.pending_media.empty());
    }
}
static Bytes section_packet(unsigned pid, unsigned cc, const Bytes &payload, bool start, bool adapted) {
    Bytes p(188, 0xff); p[0] = 0x47; put16(p.data() + 1, pid | (start ? 0x4000 : 0));
    p[3] = BYTE((adapted ? 0x30 : 0x10) | cc);
    size_t at = 4;
    if (adapted) { p[4] = BYTE(183 - payload.size()); p[5] = 0; at = 5 + p[4]; }
    require(payload.size() <= 188 - at); memcpy(p.data() + at, payload.data(), payload.size());
    return p;
}
static void psi_tests(TorneNative &s) {
    // Sections use independently generated MPEG-2 CRC fixtures. Fragment the
    // PMT across adaptation fields and exercise every input chunk boundary.
    Bytes pat = hex("00b00d0001c100000001e100e8f95e7d");
    Bytes pmt = hex("02b01b0001c10000e101f00002e101f0000de140f004fd02000c6b8ad3e9");
    pat.insert(pat.begin(), 0);
    Bytes first{0}; first.insert(first.end(), pmt.begin(), pmt.begin() + 10);
    Bytes raw = section_packet(0, 15, pat, true, false);
    for (const auto &p : {section_packet(0x100, 0, first, true, true),
                         section_packet(0x100, 1, Bytes(pmt.begin() + 10, pmt.end()), false, true)})
        raw.insert(raw.end(), p.begin(), p.end());
    BYTE out[2048]; DWORD size;
    for (unsigned split = 1; split < 188; ++split) {
        s.pending_media.clear(); s.framing_locked = false; s.psi.reset(); Bytes result;
        for (size_t at = 0; at < raw.size(); at += split) {
            s.convert(Bytes(raw.begin() + at, raw.begin() + std::min(at + split, raw.size())), out, sizeof(out), size);
            result.insert(result.end(), out, out + size);
        }
        require(result == raw && s.status.input_packet_bytes == 188);
        require(s.status.valid_pmt_sections == 1 && s.status.invalid_psi_crc == 0 &&
                s.status.fullseg_bml_programs == 1 && s.status.oneseg_bml_programs == 0 && s.status.dsmcc_es == 1);
    }
    // Corrupt a section without changing framing: CRC fails, bytes still pass.
    raw.back() ^= 1; s.pending_media.clear(); s.framing_locked = false; s.psi.reset();
    s.convert(raw, out, sizeof(out), size);
    require(size == raw.size() && memcmp(raw.data(), out, size) == 0 && s.status.invalid_psi_crc == 1 && !s.status.valid_pmt_sections);
    // 188-byte broadcast scrambling must never be interpreted as USB AES.
    Bytes scrambled(188, 0xff); scrambled[0] = 0x47; scrambled[1] = 0x01; scrambled[2] = 1; scrambled[3] = 0x90;
    raw.clear(); for (unsigned n = 0; n < 3; ++n) raw.insert(raw.end(), scrambled.begin(), scrambled.end());
    s.pending_media.clear(); s.framing_locked = false; s.psi.reset();
    s.convert(raw, out, sizeof(out), size); require(size == raw.size() && memcmp(raw.data(), out, size) == 0);
}
static void standalone_live(const std::wstring &root, bool long_capture = false) {
    std::wstring path = root + L"\\bondriver\\dist\\BonDriver_Torne.dll";
    HMODULE dll = LoadLibraryW(path.c_str()); require(dll != NULL, GetLastError());
    const BYTE *base = reinterpret_cast<const BYTE *>(dll);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    const auto *imports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    bool winusb = false, bcrypt = false;
    for (; imports->Name; ++imports) {
        std::string name(reinterpret_cast<const char *>(base + imports->Name));
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(tolower(c)); });
        require(name.find("python") == std::string::npos && name != "ws2_32.dll" &&
                name.find("libstdc++") == std::string::npos && name.find("libgcc") == std::string::npos && name.find("libc++") == std::string::npos);
        winusb |= name == "winusb.dll"; bcrypt |= name == "bcrypt.dll";
        printf("DLL import: %s\n", name.c_str());
    }
    require(winusb && bcrypt);
    auto factory = reinterpret_cast<void *(WINAPI *)(void)>(reinterpret_cast<void *>(GetProcAddress(dll, "CreateBonDriver")));
    auto diagnostic = reinterpret_cast<DWORD (WINAPI *)(char *, DWORD)>(reinterpret_cast<void *>(GetProcAddress(dll, "TorneGetDiagnostics")));
    require(factory && diagnostic); void *driver = factory(); require(driver != NULL);
    void **methods = *static_cast<void ***>(driver);
    auto open = reinterpret_cast<BOOL (*)(void *)>(methods[0]);
    auto close = reinterpret_cast<void (*)(void *)>(methods[1]);
    auto tune = reinterpret_cast<BOOL (*)(void *, DWORD, DWORD)>(methods[14]);
    auto get = reinterpret_cast<BOOL (*)(void *, BYTE **, DWORD *, DWORD *)>(methods[6]);
    auto wait = reinterpret_cast<DWORD (*)(void *, DWORD)>(methods[4]);
    auto release = reinterpret_cast<void (*)(void *)>(methods[9]);
    bool opened = false;
    try {
        // Reuse the same BonDriver instance through two complete USB sessions.
        for (unsigned session = long_capture ? 1 : 0; session < 2; ++session) {
            Bytes capture;
            unsigned channel = session == 0 ? 7 : 9;
            require(open(driver)); opened = true; require(tune(driver, 0, channel)); require(tune(driver, 0, channel));
            require(!tune(driver, 1, 7) && !tune(driver, 0, 40));
            ULONGLONG deadline = GetTickCount64() + (long_capture ? 30000 : 8000), total = 0;
            do {
                wait(driver, 250); BYTE *p = NULL; DWORD length = 0, remain = 0;
                if (get(driver, &p, &length, &remain)) {
                    require(length && length % 188 == 0);
                    for (DWORD n = 0; n < length; n += 188) require(p[n] == 0x47 && !(p[n + 3] & 0xc0));
                    total += length;
                    capture.insert(capture.end(), p, p + length);
                }
            } while (GetTickCount64() < deadline);
            require(total > 188 * 100);
            close(driver); opened = false;
            char data[8192]; require(diagnostic(data, sizeof(data)) != 0);
            bool custom = strstr(data, "\"firmwareVariant\":\"psi-passthrough\"") != NULL;
            {
                const WCHAR *name = custom
                    ? (session == 0 ? L"native-firmware-psi-20ch.ts" :
                       (long_capture ? L"native-firmware-psi-22ch-30s.ts" : L"native-firmware-psi-22ch.ts"))
                    : (session == 0 ? L"native-data-broadcast.ts" : L"native-metele-data-broadcast.ts");
                FILE *sample = _wfopen((root + L"\\diagnostics\\" + name).c_str(), L"wb");
                require(sample != NULL);
                bool written = fwrite(capture.data(), 1, capture.size(), sample) == capture.size();
                fclose(sample); require(written);
            }
            require(strstr(data, "\"lastError\":0") && strstr(data, "\"state\":1") && strstr(data, "\"mediaSessionClosed\":true"));
            require(strstr(data, "\"pmtOutputMode\":4") && strstr(data, "\"psiSettingsVerified\":true") &&
                    strstr(data, "\"fullsegBmlPrograms\":") && !strstr(data, "\"fullsegBmlPrograms\":0"));
            if (custom) require(strstr(data, "\"tsProcessing\":\"firmware-psi-passthrough-remux\"") &&
                                !strstr(data, "\"onesegBmlPrograms\":0"));
            const WCHAR *report = custom
                ? (session == 0 ? L"native-firmware-psi-20ch.json" :
                   (long_capture ? L"native-firmware-psi-22ch-30s.json" : L"native-firmware-psi-22ch.json"))
                : (session == 0 ? L"native-psi-20ch.json" : L"native-psi-22ch.json");
            FILE *json = _wfopen((root + L"\\diagnostics\\" + report).c_str(), L"wb"); require(json != NULL);
            bool written = fwrite(data, 1, strlen(data), json) == strlen(data); fclose(json); require(written);
            printf("Standalone live session %u: %llu TS bytes; %s\n", session + 1, (unsigned long long)total, data);
        }
        release(driver); FreeLibrary(dll);
    } catch (...) {
        if (opened) close(driver);
        char data[8192] = {}; diagnostic(data, sizeof(data)); fprintf(stderr, "%s\n", data);
        release(driver); FreeLibrary(dll); throw;
    }
}
int wmain(int argc, WCHAR **argv) {
    try {
        require(argc == 2 || argc == 3, ERROR_INVALID_PARAMETER);
        crypto_tests();
        std::wstring root(argv[1]);
        TorneNative session((root + L"\\BCJB95006\\USRDIR").c_str(),
                            (root + L"\\research\\decrypted\\PvrRecSvr.prx").c_str(),
                            (root + L"\\research\\decrypted\\libM2tsAes.elf").c_str());
        session.use_firmware((root + L"\\firmware\\build\\mb86h47_aac_trc-preserve-es.bin").c_str());
        require(session.operational_variant == 1);
        session.use_firmware((root + L"\\firmware\\build\\mb86h47_aac_trc-psi-passthrough.bin").c_str());
        require(session.operational_variant == 2);
        Bytes accepted = session.operational;
        bool rejected = false;
        try { session.use_firmware((root + L"\\research\\decrypted\\PvrRecSvr.prx").c_str()); }
        catch (DWORD error) { rejected = error == ERROR_FILE_INVALID; }
        require(rejected && session.operational == accepted && session.operational_variant == 2);
        session.use_firmware((root + L"\\BCJB95006\\USRDIR\\mb86h47_aac_trc.bin").c_str());
        require(session.operational_variant == 0);
        media_tests(session);
        psi_tests(session);
        DWORD error = 0;
        auto missing = torne_create((root + L"\\.tmp\\missing-firmware").c_str(),
                                    (root + L"\\research\\decrypted\\PvrRecSvr.prx").c_str(),
                                    (root + L"\\research\\decrypted\\libM2tsAes.elf").c_str(), &error);
        require(missing == NULL && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND));
        printf("Native offline checks passed (AES/SHA, 188/192 framing, every chunk split, unchanged TS, fragmented PSI, CRC rejection, capacity).\n");
        if (argc == 3) {
            bool long_capture = wcscmp(argv[2], L"--execute-long") == 0;
            require(long_capture || wcscmp(argv[2], L"--execute") == 0);
            standalone_live(root, long_capture);
        }
        return 0;
    } catch (DWORD error) { fprintf(stderr, "Native test failed: Win32 error %lu\n", (unsigned long)error); return 1; }
    catch (...) { fprintf(stderr, "Native test failed: exception\n"); return 1; }
}
