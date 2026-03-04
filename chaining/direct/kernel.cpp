#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include <cstdint>

inline int abs(int a) {
  return (a > 0) ? a : -a;
}

inline int max(int a, int b) {
  return (a > b) ? a : b;
}

inline int min(int a, int b) {
  return (a < b) ? a : b;
}

struct inbox_t {
  int score;
  int x;
  int y;
  int gen; // sentinel = -1
};

// Local inboxes keyed by source lane and generation parity.
static inbox_t inbox_even[LOOKBACK];
static inbox_t inbox_odd[LOOKBACK];

static inline void wait_for_gen(volatile int *gen_ptr, int expected) {
  int seen = bsg_lr((int*)gen_ptr);
  while (seen != expected) {
    seen = bsg_lr_aq((int*)gen_ptr);
  }
  asm volatile("" ::: "memory");
}

static inline void publish_to_peers(
  int num_lanes,
  inbox_t **peers,
  int lane,
  int gen,
  int score,
  int x,
  int y)
{
  for (int dest = 0; dest < num_lanes; dest++) {
    if (dest == lane) {
      continue;
    }

    inbox_t *remote = peers[dest];
    remote->x = x;
    remote->y = y;
    remote->score = score;
    asm volatile("" ::: "memory");
    remote->gen = gen;
  }
}

extern "C" int kernel(int* loc_query, int* loc_ref, int* scores, int pod_id)
{
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  const int col = __bsg_x;
  const int row = __bsg_y;
  const int num_lanes = min(LOOKBACK, CHAIN_LEN);
  const bool lane_active = (row == 0) && (col < num_lanes);

  // Reset inbox state so generation polling never observes stale data.
  if (row == 0 && col < LOOKBACK) {
    for (int src = 0; src < LOOKBACK; src++) {
      inbox_even[src].gen = -1;
      inbox_odd[src].gen = -1;
    }
  }

  bsg_barrier_tile_group_sync();

  if (!lane_active) {
    bsg_barrier_tile_group_sync();
    bsg_cuda_print_stat_kernel_end();
    return 0;
  }

  inbox_t *peer_even[LOOKBACK];
  inbox_t *peer_odd[LOOKBACK];
  for (int dest = 0; dest < num_lanes; dest++) {
    peer_even[dest] = (inbox_t*) bsg_remote_ptr(dest, row, &inbox_even[col]);
    peer_odd[dest] = (inbox_t*) bsg_remote_ptr(dest, row, &inbox_odd[col]);
  }

  const int k = 15;
  const float beta_mul = 0.01f;

  int my_idx = col;
  int my_x = loc_ref[my_idx];
  int my_y = loc_query[my_idx];
  int max_sc = k;

  int pf_idx = my_idx + LOOKBACK;
  register int pf_x asm("s4") = 0;
  register int pf_y asm("s5") = 0;
  if (pf_idx < CHAIN_LEN) {
    pf_x = loc_ref[pf_idx];
    pf_y = loc_query[pf_idx];
  }

  for (int gen = 0; my_idx < CHAIN_LEN; gen++) {
    const int slot = gen & 0x1;
    const int active_curr = min(LOOKBACK, CHAIN_LEN - (gen * LOOKBACK));

    for (int src = 0; src < col && src < active_curr; src++) {
      inbox_t *src_inbox = slot ? &inbox_odd[src] : &inbox_even[src];
      wait_for_gen(&src_inbox->gen, gen);

      const int rx = src_inbox->x;
      const int ry = src_inbox->y;
      const int sc = src_inbox->score;

      const int dx = my_x - rx;
      const int dy = my_y - ry;
      if (dx > 0 && dy > 0) {
        const int alpha = min(min(dx, dy), k);
        const int l = abs(dy - dx);
        const int beta = (int)(beta_mul * k * l);
        max_sc = max(max_sc, sc + alpha - beta);
      }
    }

    scores[my_idx] = max_sc;
    inbox_t **peers = slot ? peer_odd : peer_even;
    publish_to_peers(num_lanes, peers, col, gen, max_sc, my_x, my_y);

    const int next_idx = my_idx + LOOKBACK;
    if (next_idx >= CHAIN_LEN) {
      break;
    }

    int next_x = pf_x;
    int next_y = pf_y;
    int next_max = k;

    const int self_dx = next_x - my_x;
    const int self_dy = next_y - my_y;
    if (self_dx > 0 && self_dy > 0) {
      const int alpha = min(min(self_dx, self_dy), k);
      const int l = abs(self_dy - self_dx);
      const int beta = (int)(beta_mul * k * l);
      next_max = max(k, max_sc + alpha - beta);
    }

    for (int src = col + 1; src < active_curr; src++) {
      inbox_t *src_inbox = slot ? &inbox_odd[src] : &inbox_even[src];
      wait_for_gen(&src_inbox->gen, gen);

      const int rx = src_inbox->x;
      const int ry = src_inbox->y;
      const int sc = src_inbox->score;

      const int dx = next_x - rx;
      const int dy = next_y - ry;
      if (dx > 0 && dy > 0) {
        const int alpha = min(min(dx, dy), k);
        const int l = abs(dy - dx);
        const int beta = (int)(beta_mul * k * l);
        next_max = max(next_max, sc + alpha - beta);
      }
    }

    const int next_pf_idx = next_idx + LOOKBACK;
    if (next_pf_idx < CHAIN_LEN) {
      pf_x = loc_ref[next_pf_idx];
      pf_y = loc_query[next_pf_idx];
    }

    my_idx = next_idx;
    my_x = next_x;
    my_y = next_y;
    max_sc = next_max;
  }

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
