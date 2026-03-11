#ifndef SW_SCHEDULING_SCHEDULER_HPP
#define SW_SCHEDULING_SCHEDULER_HPP

// the scheduler treats the whole tile group as one pool of workers.
#define NUM_TILES (bsg_tiles_X * bsg_tiles_Y)

#ifndef MAX_SEQ_LEN
#define MAX_SEQ_LEN SEQ_LEN
#endif

// one active core owns at most this many reference columns.
#ifndef CORE_THRESHOLD
#define CORE_THRESHOLD (((MAX_SEQ_LEN) + NUM_TILES - 1) / NUM_TILES)
#endif

struct queue_slot_t {
  // ticket order is implicit from the slot index, so we only store the core id.
  int core_id;
};

struct sched_state_t {
  // check-in and launch handoff are serialized with one global lock.
  volatile int lock;
  // launch_head points at the first queued core not yet assigned to a launch.
  volatile int launch_head;
  // wait_tail points at the next queue slot to hand out to an idle core.
  volatile int wait_tail;
  // pending_seq is the next sequence to launch, or -1 during shutdown.
  volatile int pending_seq;
  // remaining counts how many more check-ins are needed before launch.
  volatile int remaining;
  // the queue stores checked-in cores in ticket order.
  queue_slot_t queue[NUM_TILES];
};

inline int team_size_for_length(int ref_len) {
  // round up so the last partial chunk still gets a worker.
  int need = (ref_len + CORE_THRESHOLD - 1) / CORE_THRESHOLD;
  if (need < 1) {
    need = 1;
  }
  // never schedule more workers than the tile group owns.
  if (need > NUM_TILES) {
    need = NUM_TILES;
  }
  return need;
}

#endif
