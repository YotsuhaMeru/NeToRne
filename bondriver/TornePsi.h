#ifndef TORNE_PSI_H
#define TORNE_PSI_H
#include <map>
#include <set>

// Observe PSI without changing or discarding any transport packet. The output
// mode is established separately by firmware register readback: a complete
// PMT alone cannot prove byte-identical reception of the broadcast multiplex.
class TornePsi {
    struct Assembly { Bytes data; int cc = -1; };
    struct Program { unsigned full = 0, one = 0, dsm = 0; };
    std::map<unsigned, Assembly> assemblies;
    std::set<unsigned> pmt_pids;
    std::map<unsigned, Program> programs;
    void section(unsigned pid, const Bytes &b) {
        if (b.size() < 8 || !(b[1] & 0x80) || !(b[5] & 1)) return;
        unsigned crc = 0xffffffffu;
        for (BYTE value : b) {
            crc ^= unsigned(value) << 24;
            for (unsigned n = 0; n < 8; ++n)
                crc = crc & 0x80000000u ? (crc << 1) ^ 0x04c11db7u : crc << 1;
        }
        if (crc) { ++invalid_crc; return; }
        if (pid == 0 && b[0] == 0 && b.size() >= 12) {
            for (size_t at = 8; at + 4 <= b.size() - 4; at += 4) {
                if (be16(b.data() + at) && pmt_pids.size() < 64)
                    pmt_pids.insert(be16(b.data() + at + 2) & 0x1fff);
            }
        } else if (pmt_pids.count(pid) && b[0] == 2 && b.size() >= 16) {
            Program value;
            size_t at = 12 + (be16(b.data() + 10) & 0xfff);
            if (at > b.size() - 4) return;
            while (at + 5 <= b.size() - 4) {
                size_t end = at + 5 + (be16(b.data() + at + 3) & 0xfff);
                if (end > b.size() - 4) return;
                if (b[at] == 0x0d) ++value.dsm;
                for (size_t d = at + 5; d < end;) {
                    if (d + 2 > end || d + 2 + b[d + 1] > end) return;
                    if (b[at] == 0x0d && b[d] == 0xfd && b[d + 1] >= 2) {
                        unsigned id = be16(b.data() + d + 2);
                        if (id == 0x000c) ++value.full;
                        if (id == 0x000d) ++value.one;
                    }
                    d += 2 + b[d + 1];
                }
                at = end;
            }
            if (at != b.size() - 4 || b[6] != 0 || b[7] != 0) return;
            unsigned program = be16(b.data() + 3);
            if (programs.size() < 64 || programs.count(program)) programs[program] = value;
            ++valid_pmts;
            fullseg_programs = oneseg_programs = dsm_es = 0;
            for (const auto &p : programs) {
                fullseg_programs += p.second.full != 0;
                oneseg_programs += p.second.one != 0;
                dsm_es += p.second.dsm;
            }
        }
    }
    void append(unsigned pid, const BYTE *data, size_t count, bool allow_new) {
        auto &a = assemblies[pid];
        while (count) {
            if (a.data.empty() && (!allow_new || *data == 0xff)) return;
            size_t need = a.data.size() < 3 ? 3 - a.data.size()
                : 3 + (be16(a.data.data() + 1) & 0xfff) - a.data.size();
            size_t take = std::min(need, count);
            a.data.insert(a.data.end(), data, data + take); data += take; count -= take;
            if (a.data.size() < 3) continue;
            size_t length = 3 + (be16(a.data.data() + 1) & 0xfff);
            if (length < 8 || length > 4096) { a.data.clear(); return; }
            if (a.data.size() == length) { section(pid, a.data); a.data.clear(); }
        }
    }
public:
    unsigned valid_pmts = 0, invalid_crc = 0, fullseg_programs = 0, oneseg_programs = 0, dsm_es = 0;
    void reset() {
        assemblies.clear(); pmt_pids.clear(); programs.clear();
        valid_pmts = invalid_crc = fullseg_programs = oneseg_programs = dsm_es = 0;
    }
    void feed(const BYTE *p) {
        unsigned pid = be16(p + 1) & 0x1fff;
        if (pid != 0 && !pmt_pids.count(pid)) return;
        auto &a = assemblies[pid];
        unsigned control = p[3] >> 4 & 3;
        if (p[1] & 0x80 || p[3] & 0xc0 || !control) { a.data.clear(); a.cc = -1; return; }
        unsigned at = 4;
        if (control & 2) {
            at += 1 + p[4];
            if (at > 188) { a.data.clear(); a.cc = -1; return; }
            if (p[4] && p[5] & 0x80) { a.data.clear(); a.cc = -1; }
        }
        if (!(control & 1) || at >= 188) return;
        int cc = p[3] & 15;
        if (cc == a.cc) return;
        if (a.cc >= 0 && cc != (a.cc + 1) % 16) a.data.clear();
        a.cc = cc;
        if (p[1] & 0x40) {
            unsigned pointer = p[at++];
            if (at + pointer > 188) { a.data.clear(); return; }
            if (!a.data.empty()) append(pid, p + at, pointer, false);
            a.data.clear(); at += pointer;
            append(pid, p + at, 188 - at, true);
        } else if (!a.data.empty()) append(pid, p + at, 188 - at, false);
    }
};
#endif
