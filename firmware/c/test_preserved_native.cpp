/* Synthetic media-format test for the actual native BonDriver converter.
 * Opens only local pinned input files; never opens a USB interface. */
#undef NDEBUG
#include <cassert>
#include "../../bondriver/TorneNative.cpp"

static Bytes encode(const Bytes &p, Aes &cbc, Aes &ecb, const Bytes &iv) {
    Bytes result(4, 0);
#ifdef TR_NATIVE_VERIFY_CRC
    put32(result.data(), ts_crc32(p.data()));
#endif
    result.insert(result.end(), p.begin(), p.end());
    unsigned af = p[3] >> 4 & 3, pid = (unsigned(p[1] & 31) << 8) | p[2];
    if (pid == 8191 || af == 2) return result;
    unsigned start = af == 3 ? std::min(188u, 5u + p[4]) : 4;
    unsigned length = 188 - start, full = length / 16 * 16;
    cbc.crypt(result.data()+4+start, full, true, iv.data());
    BYTE previous[16];
    memcpy(previous, full ? result.data()+4+start+full-16 : iv.data(), 16);
    ecb.crypt(previous, 16, true, NULL, true);
    for (unsigned i=full; i<length; ++i) result[4+start+i] ^= previous[i-full];
    return result;
}

int wmain(int argc, wchar_t **argv) {
    assert(argc == 2);
    std::wstring root(argv[1]);
    try {
        TorneNative session((root+L"/BCJB95006/USRDIR").c_str(),
                            (root+L"/research/decrypted/PvrRecSvr.prx").c_str(),
                            (root+L"/research/decrypted/libM2tsAes.elf").c_str());
#ifdef TR_NATIVE_SCRAMBLED
        session.use_firmware((root+L"/firmware/build/mb86h47_aac_trc-c-raw-scrambled.bin").c_str());
        assert(session.operational_variant == 12 && session.usb == NULL);
        bool card_rejected = false;
        try { session.api(10); } catch (DWORD error) { card_rejected = error == ERROR_NOT_SUPPORTED; }
        assert(card_rejected && session.status.card_api_calls == 0);
#elif defined(TR_NATIVE_AF)
        session.use_firmware((root+L"/firmware/build/mb86h47_aac_trc-c-raw-af.bin").c_str());
        assert(session.operational_variant == 10 && session.usb == NULL);
#elif defined(TR_NATIVE_EVENTS)
        session.use_firmware((root+L"/firmware/build/mb86h47_aac_trc-c-raw-events.bin").c_str());
        assert(session.operational_variant == 9 && session.usb == NULL);
#elif defined(TR_NATIVE_COMMANDS)
        session.use_firmware((root+L"/firmware/build/mb86h47_aac_trc-c-raw-commands.bin").c_str());
        assert(session.operational_variant == 8 && session.usb == NULL);
#elif defined(TR_NATIVE_CONTROL)
        session.use_firmware((root+L"/firmware/build/mb86h47_aac_trc-c-raw-control.bin").c_str());
        assert(session.operational_variant == 7 && session.usb == NULL);
#elif defined(TR_NATIVE_INIT)
        session.use_firmware((root+L"/firmware/build/mb86h47_aac_trc-c-raw-init.bin").c_str());
        assert(session.operational_variant == 6 && session.usb == NULL);
#elif defined(TR_NATIVE_ROUTING)
        session.use_firmware((root+L"/firmware/build/mb86h47_aac_trc-c-raw-routing.bin").c_str());
        assert(session.operational_variant == 5 && session.usb == NULL);
#elif defined(TR_NATIVE_VERIFY_CRC)
        session.use_firmware((root+L"/firmware/build/mb86h47_aac_trc-c-raw-verified.bin").c_str());
        assert(session.operational_variant == 4 && session.usb == NULL);
#else
        session.use_firmware((root+L"/firmware/build/mb86h47_aac_trc-c-raw-preserved.bin").c_str());
        assert(session.operational_variant == 3 && session.usb == NULL);
#endif
        Bytes key(16), iv(16);
        for (unsigned i=0; i<16; ++i) { key[i]=BYTE(i); iv[i]=BYTE(i+16); }
        session.iv=iv;
        session.media_cbc=new Aes(key); session.media_ecb=new Aes(key, true);
        Aes encrypt(key), encrypt_ecb(key, true);
        unsigned count=0;
        for (unsigned pid : {0u, 256u, 8191u})
        for (unsigned sc=0; sc<4; ++sc)
        for (unsigned af=0; af<4; ++af)
        for (unsigned length=0; length<256; ++length) {
            Bytes p(188);
            for (unsigned i=0; i<188; ++i) p[i]=BYTE(i*73+length);
            p[0]=0x47; p[1]=BYTE(0xe0 | pid>>8); p[2]=BYTE(pid);
            p[3]=BYTE(sc<<6 | af<<4 | length%16); p[4]=BYTE(length);
            Bytes wire=encode(p, encrypt, encrypt_ecb, iv), repeated;
            for (unsigned i=0; i<3; ++i) repeated.insert(repeated.end(), wire.begin(), wire.end());
            session.pending_media.clear(); session.framing_locked=false;
            Bytes output(564); DWORD got=0;
            session.convert(repeated, output.data(), DWORD(output.size()), got);
            assert(got == 564 && session.pending_media.empty());
            for (unsigned i=0; i<3; ++i) assert(std::equal(p.begin(), p.end(), output.begin()+i*188));
            ++count;
        }
        Bytes expected, wire;
        for (unsigned i=0; i<32; ++i) {
            Bytes p(188, BYTE(i)); p[0]=0x47; p[1]=1; p[2]=BYTE(i); p[3]=0xd0;
            expected.insert(expected.end(), p.begin(), p.end());
            Bytes frame=encode(p, encrypt, encrypt_ecb, iv);
            wire.insert(wire.end(), frame.begin(), frame.end());
        }
        session.pending_media.clear(); session.framing_locked=false;
        Bytes restored, block(32768);
        for (size_t offset=0; offset<wire.size(); offset+=197) {
            Bytes chunk(wire.begin()+offset, wire.begin()+std::min(offset+197, wire.size()));
            DWORD got=0; session.convert(chunk, block.data(), DWORD(block.size()), got);
            restored.insert(restored.end(), block.begin(), block.begin()+got);
        }
        assert(restored == expected && session.pending_media.empty() && session.usb == NULL);
#ifdef TR_NATIVE_VERIFY_CRC
        assert(session.status.crc_verified_packets == count*3+32);
        for (unsigned where : {0u, 7u, 9u, 191u}) {
            Bytes corrupted(wire.begin(), wire.begin()+192);
            corrupted[where] ^= 1;
            session.pending_media.clear(); session.framing_locked=true;
            DWORD got=0; bool rejected=false;
            try { session.convert(corrupted, block.data(), DWORD(block.size()), got); }
            catch (DWORD error) { assert(error == ERROR_INVALID_DATA); rejected=true; }
            assert(rejected && got == 0);
        }
#endif
        printf("Native converter: %u header/adaptation cases and split reads preserve exact TS; no USB access\n", count);
        return 0;
    } catch (DWORD error) { printf("Native converter test error: %lu\n", (unsigned long)error); return 1; }
}
