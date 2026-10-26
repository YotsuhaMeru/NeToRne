#include "torne_core.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fake { int calls[8], fail, locked; };
static int call(void *p, int n) {
    struct fake *f = (struct fake *)p; f->calls[n]++;
    return f->fail == n ? 123 : 0;
}
static int init(void *p) { return call(p, 0); }
static int stop(void *p) { return call(p, 1); }
static int drain(void *p) { return call(p, 2); }
static int tune(void *p, tr_u8 channel) { assert(channel >= 13 && channel <= 52); return call(p, 3); }
static int lock(void *p, tr_u8 *locked) { *locked = (tr_u8)((struct fake *)p)->locked; return call(p, 4); }
static int start(void *p, tr_u32 generation) { assert(generation); return call(p, 5); }
static int service(void *p) { return call(p, 6); }
static int shutdown(void *p) { return call(p, 7); }
static void packet(tr_u8 *p, unsigned pid) {
    unsigned i;
    for (i = 0; i < 188; i++) p[i] = (tr_u8)(i + pid);
    p[0] = 0x47; p[1] = 0xe0 | (pid >> 8); p[2] = (tr_u8)pid; p[3] = 0xf7;
}
int main(void) {
    tr_u8 storage[188 * 4], data[188 * 2], channel;
    tr_u32 old;
    unsigned pid;
    int stage;
    struct tr_queue q;
    struct tr_controller c;
    struct fake f = {{0}, -1, 1};
    struct tr_board b = {&f, init, stop, drain, tune, lock, start, service, shutdown};
    assert(tr_queue_init(&q, storage, 4) == TR_OK);
    for (pid = 0; pid < 8192; pid++) {
        packet(data, pid);
        assert(tr_queue_feed(&q, 0, data, 188) == TR_OK);
        assert(memcmp(tr_queue_front(&q), data, 188) == 0);
        assert(tr_queue_commit(&q));
    }
    assert(tr_queue_init(&q, storage, 1) == TR_OK);
    packet(data, 1); packet(data + 188, 2);
    assert(tr_queue_feed(&q, 0, data, 188) == TR_OK);
    assert(tr_queue_feed(&q, 0, data + 188, 188) == TR_FULL);
    assert(memcmp(tr_queue_front(&q), data, 188) == 0);
    assert(tr_queue_feed(&q, 0, data, 189) == TR_PARTIAL);
    data[188] = 0;
    assert(tr_queue_feed(&q, 0, data, 376) == TR_SYNC);
    assert(q.count == 1);
    assert(tr_queue_reset(&q) == TR_OK);
    assert(tr_queue_feed(&q, 0, data, 188) == TR_STALE);
    q.generation = 0xffffffffu;
    assert(tr_queue_reset(&q) == TR_EXHAUSTED);
    assert(q.generation == 0xffffffffu);
    assert(tr_queue_init(&q, 0, 0) == TR_OK);
    assert(tr_queue_feed(&q, 0, 0, 0) == TR_OK);
    assert(tr_queue_feed(&q, 0, data, 188) == TR_FULL);
    assert(tr_channel_from_bon(0, 0, &channel) == TR_OK && channel == 13);
    assert(tr_channel_from_bon(0, 39, &channel) == TR_OK && channel == 52);
    assert(tr_channel_from_bon(0, 40, &channel) == TR_INVALID);
    assert(tr_channel_from_bon(1, 0, &channel) == TR_INVALID);
    assert(tr_frequency_khz(13) == 473143);
    assert(tr_frequency_khz(52) == 707143);
    assert(tr_frequency_khz(53) == 0);
    assert(tr_controller_init(&c, &b, storage, 4) == TR_OK);
    assert(tr_initialize(&c) == TR_OK);
    assert(tr_tune(&c, 20) == TR_OK);
    old = c.queue.generation;
    assert(tr_feed(&c, old, data, 188) == TR_OK);
    assert(tr_tune(&c, 22) == TR_OK);
    assert(c.channel == 22 && c.queue.count == 0);
    assert(tr_feed(&c, old, data, 188) == TR_STALE);
    old = c.queue.generation;
    assert(tr_feed(&c, old, data, 188) == TR_OK);
    assert(tr_purge(&c) == TR_OK && c.queue.count == 0);
    assert(tr_feed(&c, old, data, 188) == TR_STALE);
    assert(f.calls[0] == 1 && f.calls[7] == 0); /* card session preserved */
    assert(tr_tick(&c) == TR_OK && f.calls[6] == 1);
    f.locked = 0;
    assert(tr_tick(&c) == TR_UNLOCKED && c.state == TR_STANDBY);
    assert(tr_front(&c) == 0 && c.channel == 0);
    f.locked = 1;
    assert(tr_tune(&c, 20) == TR_OK);
    assert(tr_stop(&c) == TR_OK);
    assert(tr_tick(&c) == TR_OK);
    assert(tr_close(&c) == TR_OK && f.calls[7] == 1);
    assert(tr_close(&c) == TR_OK && f.calls[7] == 1);
    assert(tr_initialize(&c) == TR_OK);
    for (stage = 0; stage <= 5; stage++) {
        f.fail = stage;
        assert(tr_controller_init(&c, &b, storage, 4) == TR_OK);
        if (!stage) assert(tr_initialize(&c) == TR_BOARD);
        else { assert(tr_initialize(&c) == TR_OK); assert(tr_tune(&c, 20) == TR_BOARD); }
        assert(c.state == TR_FAULT && c.channel == 0 && tr_front(&c) == 0);
        f.fail = -1;
        assert(tr_close(&c) == TR_OK);
    }
    assert(tr_initialize(&c) == TR_OK);
    c.queue.generation = 0xffffffffu;
    assert(tr_tune(&c, 20) == TR_EXHAUSTED && c.state == TR_FAULT);
    assert(tr_close(&c) == TR_OK && c.state == TR_COLD);
    puts("C core: all 8192 PIDs identical; lifecycle, stale DMA, pressure and failure checks passed");
    return 0;
}
