#ifndef TORNE_CORE_H
#define TORNE_CORE_H
/* Freestanding C99, also accepted by the available GCC 3.3.6 FR30 compiler.
 * No USB command numbers or unverified MMIO addresses are invented here. */
typedef unsigned char tr_u8;
typedef unsigned int tr_u32;
typedef char tr_u32_must_be_32_bits[(sizeof(tr_u32) == 4) ? 1 : -1];
#define TR_TS_BYTES 188u
enum tr_result {
    TR_OK = 0, TR_INVALID = 1, TR_PARTIAL = 2, TR_SYNC = 3,
    TR_FULL = 4, TR_STALE = 5, TR_EXHAUSTED = 6, TR_STATE = 7,
    TR_BOARD = 8, TR_UNLOCKED = 9
};
enum tr_state { TR_COLD, TR_STANDBY, TR_STREAMING, TR_FAULT };
struct tr_queue {
    tr_u8 *storage;
    tr_u32 capacity, head, count, generation;
};
/* Storage is capacity * 188 bytes, owned by caller for the queue lifetime. */
int tr_queue_init(struct tr_queue *, tr_u8 *storage, tr_u32 capacity);
int tr_queue_reset(struct tr_queue *);
int tr_queue_feed(struct tr_queue *, tr_u32 generation, const tr_u8 *, tr_u32 bytes);
const tr_u8 *tr_queue_front(const struct tr_queue *);
int tr_queue_commit(struct tr_queue *);
int tr_channel_from_bon(tr_u32 space, tr_u32 index, tr_u8 *physical);
tr_u32 tr_frequency_khz(tr_u8 physical);

/* All callbacks return 0 on success. The board adapter is NOT implemented.
 * Card I/O, ECM/EMM, descrambling, auth and media framing remain legacy code.
 * stop MUST quiesce input, USB and DMA including any borrowed queue buffers.
 * drain MUST empty all staging queues before a new generation is started.
 * start connects descrambler output to this queue before any remux/filter.
 * Callbacks must not re-enter the controller; IRQ access is serialized by board.
 */
struct tr_board {
    void *context;
    int (*initialize_legacy)(void *);
    int (*stop_transport)(void *);
    int (*drain_transport)(void *);
    int (*tune)(void *, tr_u8 physical);
    int (*locked)(void *, tr_u8 *locked);
    int (*start_transport)(void *, tr_u32 generation);
    int (*service_legacy)(void *);
    int (*shutdown_legacy)(void *);
};
struct tr_controller {
    struct tr_board board;
    struct tr_queue queue;
    enum tr_state state;
    tr_u8 channel, lock;
    int board_error;
};
int tr_controller_init(struct tr_controller *, const struct tr_board *, tr_u8 *storage, tr_u32 capacity);
int tr_initialize(struct tr_controller *);
int tr_tune(struct tr_controller *, tr_u8 physical);
int tr_stop(struct tr_controller *);
int tr_purge(struct tr_controller *);
int tr_tick(struct tr_controller *);
int tr_feed(struct tr_controller *, tr_u32 generation, const tr_u8 *, tr_u32 bytes);
const tr_u8 *tr_front(const struct tr_controller *);
int tr_commit(struct tr_controller *);
int tr_close(struct tr_controller *);
#endif
