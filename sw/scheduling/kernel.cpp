#include <bsg_manycore.h>
#include <bsg_manycore_atomic.h>
#include <bsg_cuda_lite_barrier.h>
#include "bsg_barrier_multipod.h"
#include "scheduler.hpp"
#include "unroll.hpp"
#include <cstdint>

// smith-waterman scoring constants.
#define MATCH     1
#define MISMATCH -1
#define GAP       1

#ifndef PREFETCH
#define PREFETCH 0
#endif

inline int max(int a, int b) {
  return (a > b) ? a : b;
}

inline int max(int a, int b, int c) {
  return max(a, max(b,c));
}

inline int max(int a, int b, int c, int d) {
  return max(max(a,b), max(c,d));
}

struct mailbox_t {
  int      dp_val;
  volatile int full;
  int      max_val;
  uint8_t  qry_char;
};

// seq_id is written last and also serves as the wake word for this mailbox.
struct launch_mailbox_t {
  int lane_id;
  int team_size;
  int pred_core_id;
  int succ_core_id;
  volatile int primed_seq;
  volatile int seq_id; // write last to wake the target core
};

mailbox_t mailbox = {0, 0, 0, 0};
launch_mailbox_t launch_mailbox = {0, 0, -1, -1, -2, -2};
volatile int next_is_ready = 1;

// these buffers hold one reference chunk and two rolling dp rows per core.
uint8_t refbuf[CORE_THRESHOLD + 1];
int H1[CORE_THRESHOLD + 1];
int H2[CORE_THRESHOLD + 1];

// the scheduler stores one packed core id instead of separate x and y coordinates.
inline int pack_core_id(int x, int y) {
  return (x * bsg_tiles_Y) + y;
}

inline int core_x(int core_id) {
  return core_id / bsg_tiles_Y;
}

inline int core_y(int core_id) {
  return core_id % bsg_tiles_Y;
}

// the scheduler state handoff is serialized with a single amo-based lock.
inline void sched_lock(volatile int* lock) {
  while (bsg_amoswap_aq((int*)lock, 1) != 0) {
  }
}

inline void sched_unlock(volatile int* lock) {
  bsg_amoswap_rl((int*)lock, 0);
}

// these helpers map a packed core id onto that core's local scratchpad state.
inline mailbox_t* remote_mailbox_ptr(int core_id) {
  return (mailbox_t*)bsg_remote_ptr(core_x(core_id), core_y(core_id), &mailbox);
}

inline volatile int* remote_ready_ptr(int core_id) {
  return (volatile int*)bsg_remote_ptr(core_x(core_id), core_y(core_id), (void*)&next_is_ready);
}

inline launch_mailbox_t* remote_launch_mailbox_ptr(int core_id) {
  return (launch_mailbox_t*)bsg_remote_ptr(core_x(core_id), core_y(core_id), &launch_mailbox);
}

// the launcher first primes every team member with static metadata.
inline void prime_launch_mailbox(int core_id,
                                 int lane_id,
                                 int team_size,
                                 int pred_core_id,
                                 int succ_core_id,
                                 int seq_id) {
  launch_mailbox_t* remote = remote_launch_mailbox_ptr(core_id);
  remote->lane_id = lane_id;
  remote->team_size = team_size;
  remote->pred_core_id = pred_core_id;
  remote->succ_core_id = succ_core_id;
  asm volatile("" ::: "memory");
  remote->primed_seq = seq_id;
}

// waking a core is one final seq_id store after its metadata is ready.
inline void wake_launch_mailbox(int core_id, int seq_id) {
  launch_mailbox_t* remote = remote_launch_mailbox_ptr(core_id);
  asm volatile("" ::: "memory");
  remote->seq_id = seq_id;
}

struct launch_info_t {
  int seq_id;
  int team_size;
  int team_core_ids[NUM_TILES];
};

// this function atomically performs one check-in and elects the last arrival as launcher.
inline bool check_in_and_maybe_launch(sched_state_t* sched,
                                      const int* ref_lens,
                                      int num_seq,
                                      int my_core_id,
                                      launch_info_t* launch) {
  sched_lock(&sched->lock);

  const int ticket = sched->wait_tail;
  sched->wait_tail = ticket + 1;
  sched->queue[ticket % NUM_TILES].core_id = my_core_id;

  // remaining reaches zero exactly when the current pending launch is ready.
  const int seq_id = sched->pending_seq;
  const int old_remaining = sched->remaining;
  sched->remaining = old_remaining - 1;

  if (old_remaining != 1) {
    sched_unlock(&sched->lock);
    return false;
  }

  const int team_size = (seq_id >= 0) ? team_size_for_length(ref_lens[seq_id]) : NUM_TILES;
  const int base_ticket = sched->launch_head;
  for (int i = 0; i < team_size; i++) {
    launch->team_core_ids[i] = sched->queue[(base_ticket + i) % NUM_TILES].core_id;
  }
  // once the active team is snapshotted, publish the next pending item before unlocking.
  sched->launch_head = base_ticket + team_size;

  if ((seq_id >= 0) && ((seq_id + 1) < num_seq)) {
    sched->pending_seq = seq_id + 1;
    sched->remaining = team_size_for_length(ref_lens[seq_id + 1]);
  } else {
    sched->pending_seq = -1;
    sched->remaining = NUM_TILES;
  }

  sched_unlock(&sched->lock);

  launch->seq_id = seq_id;
  launch->team_size = team_size;
  return true;
}

// seq_id is monotonic, so it also acts like a mailbox generation number.
inline void wait_for_launch(int* seen_seq) {
  const int ready = bsg_lr((int*)&launch_mailbox.seq_id);
  if (ready == *seen_seq) {
    bsg_lr_aq((int*)&launch_mailbox.seq_id);
  }
  asm volatile("" ::: "memory");
  *seen_seq = launch_mailbox.seq_id;
}

extern "C" int kernel(uint8_t* qry,
                      uint8_t* ref,
                      int* qry_lens,
                      int* ref_lens,
                      sched_state_t* sched,
                      int num_seq,
                      int* output,
                      int pod_id)
{
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  // every core repeatedly checks in, sleeps, runs one assignment, then checks in again.
  const int my_core_id = pack_core_id(__bsg_x, __bsg_y);
  int seen_seq = -2;
  mailbox.full = 0;
  next_is_ready = 1;
  launch_mailbox.primed_seq = -2;
  launch_mailbox.seq_id = -2;
  bsg_barrier_tile_group_sync();

  while (1) {
    launch_info_t launch = {};
    // only the last arrival returns true and becomes the launcher for this round.
    if (check_in_and_maybe_launch(sched, ref_lens, num_seq, my_core_id, &launch)) {
      const int lane0_succ = (launch.team_size > 1) ? launch.team_core_ids[1] : -1;
      prime_launch_mailbox(launch.team_core_ids[0],
                           0,
                           launch.team_size,
                           -1,
                           lane0_succ,
                           launch.seq_id);
      bsg_fence();
      wake_launch_mailbox(launch.team_core_ids[0], launch.seq_id);

      // lane 0 can now begin while the launcher primes the rest of the team.
      for (int lane = 1; lane < launch.team_size; lane++) {
        const int pred_core_id = launch.team_core_ids[lane - 1];
        const int succ_core_id = (lane + 1 < launch.team_size) ? launch.team_core_ids[lane + 1] : -1;
        prime_launch_mailbox(launch.team_core_ids[lane],
                             lane,
                             launch.team_size,
                             pred_core_id,
                             succ_core_id,
                             launch.seq_id);
      }
    }

    wait_for_launch(&seen_seq);

    // shutdown uses the same relay structure, but with seq_id = -1.
    if (launch_mailbox.seq_id < 0) {
      if (launch_mailbox.succ_core_id >= 0) {
        launch_mailbox_t* succ_launch = remote_launch_mailbox_ptr(launch_mailbox.succ_core_id);
        while (succ_launch->primed_seq != -1) {
        }
        wake_launch_mailbox(launch_mailbox.succ_core_id, -1);
      }
      bsg_barrier_tile_group_sync();
      break;
    }

    const int s = launch_mailbox.seq_id;
    const int qry_len = qry_lens[s];
    const int ref_len = ref_lens[s];
    const int team_size = launch_mailbox.team_size;
    const int lane_id = launch_mailbox.lane_id;
    const bool has_pred = launch_mailbox.pred_core_id >= 0;
    // lane_id maps directly onto a contiguous reference chunk.
    const int ref_start = lane_id * CORE_THRESHOLD;
    int ref_len_local = ref_len - ref_start;
    if (ref_len_local > CORE_THRESHOLD) {
      ref_len_local = CORE_THRESHOLD;
    }
    if (ref_len_local < 0) {
      ref_len_local = 0;
    }

    const int pred_core_id = launch_mailbox.pred_core_id;
    const int succ_core_id = launch_mailbox.succ_core_id;
    mailbox_t* succ_mailbox = nullptr;
    volatile int* pred_next_is_ready = nullptr;
    const uint8_t* const qry_seq = &qry[s * MAX_SEQ_LEN];
    const uint8_t* const ref_seq = &ref[(s * MAX_SEQ_LEN) + ref_start];

    // after launch, successor routing is fixed for the entire sequence.
    if (succ_core_id >= 0) {
      succ_mailbox = remote_mailbox_ptr(succ_core_id);
    }

    if (pred_core_id >= 0) {
      pred_next_is_ready = remote_ready_ptr(pred_core_id);
    }

    // startup mirrors the steady-state mailbox protocol before row 0 begins.
    mailbox.full = 0;
    next_is_ready = (succ_core_id >= 0) ? 0 : 1;
    if (pred_next_is_ready != nullptr) {
      *pred_next_is_ready = 1;
    }
    if (succ_core_id >= 0) {
      launch_mailbox_t* succ_launch = remote_launch_mailbox_ptr(succ_core_id);
      while (succ_launch->primed_seq != s) {
      }
      wake_launch_mailbox(succ_core_id, s);
    }

    int *H_curr = H1;
    int *H_prev = H2;
    for (int k = 0; k <= ref_len_local; k++) {
      H_prev[k] = 0;
    }

    // preload the local reference chunk using the same batched style as the 1d kernel.
    int maxv = 0;
    int k = 0;
    for (; k + 8 <= ref_len_local; k += 8) {
      unrolled_load<uint8_t, 8>(&refbuf[k + 1], &ref_seq[k]);
    }
    for (; k < ref_len_local; k++) {
      refbuf[k + 1] = ref_seq[k];
    }

#if PREFETCH
    register uint8_t next_qry asm("s4");
    if ((!has_pred) && (qry_len > 0)) {
      next_qry = qry_seq[0];
    }
#endif

    // after startup, this inner loop stays close to the original 1d datapath.
    for (int i = 0; i < qry_len; i++) {
      uint8_t qry_char;
      if (!has_pred) {
#if PREFETCH
        qry_char = next_qry;
#else
        qry_char = qry_seq[i];
#endif
        H_curr[0] = 0;
      } else {
        // non-head lanes consume query characters and boundary values from the left mailbox.
        int ready = bsg_lr((int*)&mailbox.full);
        if (ready == 0) {
          bsg_lr_aq((int*)&mailbox.full);
        }
        asm volatile("" ::: "memory");

        H_curr[0] = mailbox.dp_val;
        qry_char = mailbox.qry_char;
        if (mailbox.max_val > maxv) {
          maxv = mailbox.max_val;
        }

        asm volatile("" ::: "memory");
        mailbox.full = 0;
        if (pred_next_is_ready != nullptr) {
          *pred_next_is_ready = 1;
        }
      }

      // this loop computes the local smith-waterman recurrence on the owned chunk.
      for (int k = 1; k <= ref_len_local; k++) {
        const int match = (qry_char == refbuf[k]) ? MATCH : MISMATCH;
        const int score_diag = H_prev[k - 1] + match;
        const int score_up = H_prev[k] - GAP;
        const int score_left = H_curr[k - 1] - GAP;
        int val = max(0, score_diag, score_up, score_left);
        H_curr[k] = val;
        if (val > maxv) {
          maxv = val;
        }
      }

      // the right boundary and running max move to the successor lane here.
      if (succ_core_id >= 0) {
        int ready = bsg_lr((int*)&next_is_ready);
        if (ready == 0) {
          bsg_lr_aq((int*)&next_is_ready);
        }
        asm volatile("" ::: "memory");

        next_is_ready = 0;
        succ_mailbox->dp_val = H_curr[ref_len_local];
        succ_mailbox->max_val = maxv;
        succ_mailbox->qry_char = qry_char;
        asm volatile("" ::: "memory");
        succ_mailbox->full = 1;
      }

      int *tmp = H_curr;
      H_curr = H_prev;
      H_prev = tmp;

#if PREFETCH
      // only the head lane reads query memory directly, so only it prefetches.
      if ((!has_pred) && (i + 1 < qry_len)) {
        next_qry = qry_seq[i + 1];
      }
#endif
    }

    // the last active lane owns the final score for the sequence.
    if (lane_id == (team_size - 1)) {
      output[s] = maxv;
    }
  }

  // all tiles leave through the same barrier to keep kernel shutdown symmetric.
  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
