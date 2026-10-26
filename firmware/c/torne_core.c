#include "torne_core.h"

int tr_queue_init(struct tr_queue *q, tr_u8 *storage, tr_u32 capacity) {
    if (!q || (!storage && capacity) || capacity > 0xffffffffu / TR_TS_BYTES) return TR_INVALID;
    q->storage = storage; q->capacity = capacity; q->head = 0; q->count = 0; q->generation = 0;
    return TR_OK;
}
int tr_queue_reset(struct tr_queue *q) {
    if (q->generation == 0xffffffffu) return TR_EXHAUSTED;
    q->generation++; q->head = 0; q->count = 0;
    return TR_OK;
}
int tr_queue_feed(struct tr_queue *q, tr_u32 generation, const tr_u8 *bytes, tr_u32 length) {
    tr_u32 offset, count, tail, i;
    tr_u8 *target;
    if (generation != q->generation) return TR_STALE;
    if (length % TR_TS_BYTES) return TR_PARTIAL;
    if (!bytes && length) return TR_INVALID;
    for (offset = 0; offset < length; offset += TR_TS_BYTES) if (bytes[offset] != 0x47) return TR_SYNC;
    count = length / TR_TS_BYTES;
    if (count > q->capacity - q->count) return TR_FULL;
    for (offset = 0; offset < length; offset += TR_TS_BYTES) {
        tail = (q->head + q->count) % q->capacity;
        target = q->storage + tail * TR_TS_BYTES;
        for (i = 0; i < TR_TS_BYTES; i++) target[i] = bytes[offset + i];
        q->count++;
    }
    return TR_OK;
}
const tr_u8 *tr_queue_front(const struct tr_queue *q) {
    return q->count ? q->storage + q->head * TR_TS_BYTES : (const tr_u8 *)0;
}
int tr_queue_commit(struct tr_queue *q) {
    if (!q->count) return 0;
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return 1;
}
int tr_channel_from_bon(tr_u32 space, tr_u32 index, tr_u8 *physical) {
    if (space || index >= 40 || !physical) return TR_INVALID;
    *physical = (tr_u8)(index + 13);
    return TR_OK;
}
tr_u32 tr_frequency_khz(tr_u8 physical) {
    return physical >= 13 && physical <= 52 ? 473143u + (physical - 13u) * 6000u : 0;
}

static int active(const struct tr_controller *c) {
    return c->state == TR_STANDBY || c->state == TR_STREAMING;
}
static int board_result(struct tr_controller *c, int result) {
    c->board_error = result;
    if (!result) return TR_OK;
    c->state = TR_FAULT; c->channel = 0; c->lock = 0;
    return TR_BOARD;
}
static int quiesce(struct tr_controller *c, int advance_generation) {
    int result;
    c->state = TR_STANDBY; c->lock = 0;
    result = board_result(c, c->board.stop_transport(c->board.context));
    if (result) return result;
    result = board_result(c, c->board.drain_transport(c->board.context));
    if (result) return result;
    if (advance_generation) result = tr_queue_reset(&c->queue);
    else { c->queue.head = 0; c->queue.count = 0; result = TR_OK; }
    if (result) { c->state = TR_FAULT; c->channel = 0; }
    return result;
}
static int start(struct tr_controller *c) {
    int result = board_result(c, c->board.locked(c->board.context, &c->lock));
    if (result) return result;
    if (!c->lock) return TR_UNLOCKED;
    result = board_result(c, c->board.start_transport(c->board.context, c->queue.generation));
    if (!result) c->state = TR_STREAMING;
    return result;
}
int tr_controller_init(struct tr_controller *c, const struct tr_board *board, tr_u8 *storage, tr_u32 capacity) {
    int result;
    if (!c || !board || !board->initialize_legacy || !board->stop_transport || !board->drain_transport
        || !board->tune || !board->locked || !board->start_transport || !board->service_legacy
        || !board->shutdown_legacy) return TR_INVALID;
    result = tr_queue_init(&c->queue, storage, capacity);
    if (result) return result;
    c->board.context = board->context;
    c->board.initialize_legacy = board->initialize_legacy;
    c->board.stop_transport = board->stop_transport;
    c->board.drain_transport = board->drain_transport;
    c->board.tune = board->tune;
    c->board.locked = board->locked;
    c->board.start_transport = board->start_transport;
    c->board.service_legacy = board->service_legacy;
    c->board.shutdown_legacy = board->shutdown_legacy;
    c->state = TR_COLD; c->channel = 0; c->lock = 0; c->board_error = 0;
    return TR_OK;
}
int tr_initialize(struct tr_controller *c) {
    int result;
    if (c->state != TR_COLD) return TR_STATE;
    result = board_result(c, c->board.initialize_legacy(c->board.context));
    if (!result) c->state = TR_STANDBY;
    return result;
}
int tr_tune(struct tr_controller *c, tr_u8 physical) {
    int result;
    if (physical < 13 || physical > 52) return TR_INVALID;
    if (!active(c)) return TR_STATE;
    result = quiesce(c, 1);
    if (result) return result;
    c->channel = 0;
    result = board_result(c, c->board.tune(c->board.context, physical));
    if (result) return result;
    result = start(c);
    if (!result) c->channel = physical;
    return result;
}
int tr_stop(struct tr_controller *c) {
    return active(c) ? quiesce(c, 1) : TR_STATE;
}
int tr_purge(struct tr_controller *c) {
    int result, restart;
    if (!active(c)) return TR_STATE;
    restart = c->state == TR_STREAMING;
    result = quiesce(c, 1);
    if (!result && restart) {
        result = start(c);
        if (result) c->channel = 0;
    }
    return result;
}
int tr_tick(struct tr_controller *c) {
    int result;
    tr_u8 locked = 0;
    if (!active(c)) return TR_STATE;
    result = board_result(c, c->board.service_legacy(c->board.context));
    if (result) return result;
    result = board_result(c, c->board.locked(c->board.context, &locked));
    if (result) return result;
    if (c->state == TR_STREAMING && !locked) {
        result = quiesce(c, 1);
        c->channel = 0;
        return result ? result : TR_UNLOCKED;
    }
    c->lock = locked;
    return TR_OK;
}
int tr_feed(struct tr_controller *c, tr_u32 generation, const tr_u8 *bytes, tr_u32 length) {
    return c->state == TR_STREAMING ? tr_queue_feed(&c->queue, generation, bytes, length) : TR_STATE;
}
const tr_u8 *tr_front(const struct tr_controller *c) {
    return c->state == TR_STREAMING ? tr_queue_front(&c->queue) : (const tr_u8 *)0;
}
int tr_commit(struct tr_controller *c) {
    return c->state == TR_STREAMING && tr_queue_commit(&c->queue);
}
int tr_close(struct tr_controller *c) {
    int result;
    if (c->state == TR_COLD) return TR_OK;
    result = quiesce(c, 0);
    if (result) return result;
    result = board_result(c, c->board.shutdown_legacy(c->board.context));
    if (!result) { c->state = TR_COLD; c->channel = 0; }
    return result;
}
