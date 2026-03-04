#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>
#include <cstdint>

// helper functions;
inline int abs(int a) {
  return (a > 0) ? a : -a;
}

inline int max(int a, int b) {
  return (a > b) ? a : b;
}

inline int min(int a, int b) {
  return (a < b) ? a : b;
}

// mailbox struct for inter-core communication;
struct mail_t {
  int score; // sentinel = -1
  int x;
  int y;
};

// per-core state;
static mail_t mail = { -1, 0, 0 };
static int next_rdy = 1; // has next core consumed my last write?

extern "C" int kernel(int* loc_query, int* loc_ref, int* scores, int pod_id)
{
  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();

  int col = __bsg_x;
  int row = __bsg_y;

  // only first row, first LOOKBACK cores participate;
  if (row != 0 || col >= LOOKBACK) {
    bsg_barrier_tile_group_sync();
    bsg_cuda_print_stat_kernel_end();
    return 0;
  }

  // remote pointers (ring);
  int next_col = (col + 1) % LOOKBACK;
  int prev_col = (col - 1 + LOOKBACK) % LOOKBACK;
  mail_t *next_mail      = (mail_t*) bsg_remote_ptr(next_col, 0, &mail);
  int    *prev_next_rdy  = (int*)    bsg_remote_ptr(prev_col, 0, &next_rdy);

  // constants;
  const int k = 15;
  const float beta_mul = 0.01f;

  // per-core state;
  int my_idx = col;
  int counter = col;
  int max_sc = k;

  // cache my target's position (in-use);
  int my_x = loc_ref[my_idx];
  int my_y = loc_query[my_idx];

  // prefetch next pair (in-flight);
  int pf_idx = my_idx + LOOKBACK;
  register int pf_x asm("s4");
  register int pf_y asm("s5");
  if (pf_idx < CHAIN_LEN) {
    pf_x = loc_ref[pf_idx];
    pf_y = loc_query[pf_idx];
  }

  // main loop;
  for (int iter = 0; iter < CHAIN_LEN; iter++) {
    if (counter == 0) {
      // COMPUTE: max_sc already accumulated from receives;
      scores[my_idx] = max_sc;

      // wait for next core to be ready;
      int rdy = bsg_lr(&next_rdy);
      if (!rdy) bsg_lr_aq(&next_rdy);
      asm volatile("" ::: "memory");

      // broadcast to next core;
      next_rdy = 0;
      next_mail->x = my_x;
      next_mail->y = my_y;
      asm volatile("" ::: "memory");
      next_mail->score = max_sc;

      // prepare for next index;
      int next_idx = my_idx + LOOKBACK;
      int new_max = k;

      // self-predecessor for next index (consume prefetched, in-use);
      if (next_idx < CHAIN_LEN) {
        int dx = pf_x - my_x;
        int dy = pf_y - my_y;
        if (dx > 0 && dy > 0) {
          int alpha = min(min(dx, dy), k);
          int l = abs(dy - dx);
          int beta = (int)(beta_mul * k * l);
          new_max = max(k, max_sc + alpha - beta);
        }
        my_x = pf_x;
        my_y = pf_y;
      }

      // prefetch for next round (in-flight);
      int next_pf_idx = next_idx + LOOKBACK;
      if (next_pf_idx < CHAIN_LEN) {
        pf_x = loc_ref[next_pf_idx];
        pf_y = loc_query[next_pf_idx];
      }

      max_sc = new_max;
      my_idx = next_idx;
      counter = LOOKBACK - 1;

    } else {
      // RECEIVE: wait for mail;
      int sc;
      sc = bsg_lr(&mail.score);
      if (sc == -1) sc = bsg_lr_aq(&mail.score);
      asm volatile("" ::: "memory");

      // grab values into registers immediately;
      int rx = mail.x;
      int ry = mail.y;
      mail.score = -1; // reset sentinel

      // tell sender we consumed;
      *prev_next_rdy = 1;

      // forward to next core (unless last in cascade);
      bool is_last = (next_col == (iter % LOOKBACK));
      if (!is_last) {
        // wait for next core to be ready;
        int rdy = bsg_lr(&next_rdy);
        if (!rdy) bsg_lr_aq(&next_rdy);
        asm volatile("" ::: "memory");

        next_rdy = 0;
        next_mail->x = rx;
        next_mail->y = ry;
        asm volatile("" ::: "memory");
        next_mail->score = sc;
      }

      // update max_sc using received predecessor;
      int dx = my_x - rx;
      int dy = my_y - ry;
      if (dx > 0 && dy > 0) {
        int alpha = min(min(dx, dy), k);
        int l = abs(dy - dx);
        int beta = (int)(beta_mul * k * l);
        max_sc = max(max_sc, sc + alpha - beta);
      }

      counter--;
    }
  }

  // kernel end;
  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
