#ifndef SW_SCHEDULING_SCHEDULER_HPP
#define SW_SCHEDULING_SCHEDULER_HPP

#define NUM_TILES (bsg_tiles_X * bsg_tiles_Y)

#ifndef MAX_SEQ_LEN
#define MAX_SEQ_LEN SEQ_LEN
#endif

#ifndef CORE_THRESHOLD
#define CORE_THRESHOLD (((MAX_SEQ_LEN) + NUM_TILES - 1) / NUM_TILES)
#endif

struct queue_slot_t {
  int core_id;
  volatile int ticket;
};

struct sched_state_t {
  volatile int lock;
  volatile int launch_head;
  volatile int wait_tail;
  volatile int pending_seq;
  volatile int remaining;
  queue_slot_t queue[NUM_TILES];
};

inline int team_size_for_length(int ref_len) {
  int need = (ref_len + CORE_THRESHOLD - 1) / CORE_THRESHOLD;
  if (need < 1) {
    need = 1;
  }
  if (need > NUM_TILES) {
    need = NUM_TILES;
  }
  return need;
}

#endif
