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

struct mail_t {
  int tag; // source-generation number
  int score;
  int x;
  int y;
};

static mail_t mail_even[LOOKBACK];
static mail_t mail_odd[LOOKBACK];
static int child_rdy[LOOKBACK];

static inline int highest_tree_stride(int count) {
  int stride = 0;
  for (int bit = 1; bit < count; bit <<= 1) {
    stride = bit;
  }
  return stride;
}

static inline void wait_until_ready(int *ready) {
  int rdy = bsg_lr(ready);
  if (!rdy) {
    bsg_lr_aq(ready);
  }
  asm volatile("" ::: "memory");
}

static inline void wait_for_tag(volatile int *tag, int expected) {
  int seen = bsg_lr((int*)tag);
  while (seen != expected) {
    seen = bsg_lr_aq((int*)tag);
  }
  asm volatile("" ::: "memory");
}

extern "C" int kernel(int* loc_query, int* loc_ref, int* scores, int pod_id)
{
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  const int col = __bsg_x;
  const int row = __bsg_y;
  const int num_lanes = min(LOOKBACK, CHAIN_LEN);

  if (row == 0 && col < LOOKBACK) {
    for (int src = 0; src < LOOKBACK; src++) {
      mail_even[src].tag = -1;
      mail_odd[src].tag = -1;
    }
    for (int dest = 0; dest < LOOKBACK; dest++) {
      child_rdy[dest] = 1;
    }
  }

  bsg_barrier_tile_group_sync();

  if (row != 0 || col >= num_lanes) {
    bsg_barrier_tile_group_sync();
    bsg_cuda_print_stat_kernel_end();
    return 0;
  }

  mail_t *peer_mail_even[LOOKBACK][LOOKBACK];
  mail_t *peer_mail_odd[LOOKBACK][LOOKBACK];
  int *parent_ack[LOOKBACK];
  for (int src = 0; src < num_lanes; src++) {
    for (int peer = 0; peer < num_lanes; peer++) {
      peer_mail_even[src][peer] = (mail_t*) bsg_remote_ptr(peer, 0, &mail_even[src]);
      peer_mail_odd[src][peer] = (mail_t*) bsg_remote_ptr(peer, 0, &mail_odd[src]);
    }
  }
  for (int peer = 0; peer < num_lanes; peer++) {
    parent_ack[peer] = (int*) bsg_remote_ptr(peer, 0, &child_rdy[col]);
  }

  const int top_stride = highest_tree_stride(num_lanes);
  const int k = 15;
  const float beta_mul = 0.01f;

  int my_idx = col;
  int counter = col;
  int max_sc = k;

  int my_x = loc_ref[my_idx];
  int my_y = loc_query[my_idx];

  int pf_idx = my_idx + LOOKBACK;
  register int pf_x asm("s4");
  register int pf_y asm("s5");
  if (pf_idx < CHAIN_LEN) {
    pf_x = loc_ref[pf_idx];
    pf_y = loc_query[pf_idx];
  }

  for (int iter = 0; iter < CHAIN_LEN; iter++) {
    const int src = iter % num_lanes;
    const int gen = iter / num_lanes;
    const int slot = gen & 0x1;
    const int rel = (col - src + num_lanes) % num_lanes;
    mail_t *local_mail = slot ? &mail_odd[src] : &mail_even[src];

    int pkt_sc = 0;
    int pkt_x = 0;
    int pkt_y = 0;
    bool have_pkt = false;

    int next_idx = my_idx;
    int next_x = my_x;
    int next_y = my_y;
    int next_max = max_sc;

    if (counter == 0) {
      scores[my_idx] = max_sc;

      pkt_sc = max_sc;
      pkt_x = my_x;
      pkt_y = my_y;
      have_pkt = true;

      next_idx = my_idx + LOOKBACK;
      next_max = k;

      if (next_idx < CHAIN_LEN) {
        int dx = pf_x - my_x;
        int dy = pf_y - my_y;
        if (dx > 0 && dy > 0) {
          int alpha = min(min(dx, dy), k);
          int l = abs(dy - dx);
          int beta = (int)(beta_mul * k * l);
          next_max = max(k, max_sc + alpha - beta);
        }
        next_x = pf_x;
        next_y = pf_y;
      }

      int next_pf_idx = next_idx + LOOKBACK;
      if (next_pf_idx < CHAIN_LEN) {
        pf_x = loc_ref[next_pf_idx];
        pf_y = loc_query[next_pf_idx];
      }
    }

    for (int stride = top_stride; stride > 0; stride >>= 1) {
      const int span = stride << 1;

      if (!have_pkt && ((rel % span) == stride)) {
        wait_for_tag(&local_mail->tag, gen);

        pkt_sc = local_mail->score;
        pkt_x = local_mail->x;
        pkt_y = local_mail->y;
        have_pkt = true;

        int parent_rel = rel - stride;
        int parent = src + parent_rel;
        if (parent >= num_lanes) {
          parent -= num_lanes;
        }
        *parent_ack[parent] = 1;
      }

      if (have_pkt && ((rel % span) == 0)) {
        int child_rel = rel + stride;
        if (child_rel < num_lanes) {
          int child = src + child_rel;
          if (child >= num_lanes) {
            child -= num_lanes;
          }

          wait_until_ready(&child_rdy[child]);
          child_rdy[child] = 0;
          mail_t *child_mail = slot ? peer_mail_odd[src][child] : peer_mail_even[src][child];
          child_mail->x = pkt_x;
          child_mail->y = pkt_y;
          child_mail->score = pkt_sc;
          asm volatile("" ::: "memory");
          child_mail->tag = gen;
        }
      }
    }

    if (counter == 0) {
      max_sc = next_max;
      my_idx = next_idx;
      my_x = next_x;
      my_y = next_y;
      counter = LOOKBACK - 1;
    } else {
      int dx = my_x - pkt_x;
      int dy = my_y - pkt_y;
      if (dx > 0 && dy > 0) {
        int alpha = min(min(dx, dy), k);
        int l = abs(dy - dx);
        int beta = (int)(beta_mul * k * l);
        max_sc = max(max_sc, pkt_sc + alpha - beta);
      }

      counter--;
    }
  }

  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
