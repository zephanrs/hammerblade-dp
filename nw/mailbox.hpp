#ifndef NW_MAILBOX_HPP
#define NW_MAILBOX_HPP

#include <bsg_manycore.h>

inline void hb_wait_flag(volatile int *flag) {
  int rdy = bsg_lr((int *)flag);
  if (rdy == 0) {
    bsg_lr_aq((int *)flag);
  }
  asm volatile("" ::: "memory");
}

template <typename Payload>
struct hb_mailbox_port_t {
  Payload data = {};
  volatile int full = 0;
};

template <typename Payload>
struct hb_mailbox_state_t {
  hb_mailbox_port_t<Payload> left = {};
  hb_mailbox_port_t<Payload> right = {};
  hb_mailbox_port_t<Payload> top = {};
  hb_mailbox_port_t<Payload> bottom = {};

  volatile int left_ready = 1;
  volatile int right_ready = 1;
  volatile int top_ready = 1;
  volatile int bottom_ready = 1;

  hb_mailbox_port_t<Payload> *left_mailbox = 0;
  hb_mailbox_port_t<Payload> *right_mailbox = 0;
  hb_mailbox_port_t<Payload> *top_mailbox = 0;
  hb_mailbox_port_t<Payload> *bottom_mailbox = 0;

  volatile int *left_ready_flag = 0;
  volatile int *right_ready_flag = 0;
  volatile int *top_ready_flag = 0;
  volatile int *bottom_ready_flag = 0;
};

template <typename Payload>
inline void init_mailboxes(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->left_mailbox =
    (hb_mailbox_port_t<Payload> *)bsg_remote_ptr(__bsg_x, __bsg_y - 1, &mailboxes->right);
  mailboxes->right_mailbox =
    (hb_mailbox_port_t<Payload> *)bsg_remote_ptr(__bsg_x, __bsg_y + 1, &mailboxes->left);
  mailboxes->top_mailbox =
    (hb_mailbox_port_t<Payload> *)bsg_remote_ptr(__bsg_x - 1, __bsg_y, &mailboxes->bottom);
  mailboxes->bottom_mailbox =
    (hb_mailbox_port_t<Payload> *)bsg_remote_ptr(__bsg_x + 1, __bsg_y, &mailboxes->top);

  mailboxes->left_ready_flag =
    (volatile int *)bsg_remote_ptr(__bsg_x, __bsg_y - 1, (void *)&mailboxes->right_ready);
  mailboxes->right_ready_flag =
    (volatile int *)bsg_remote_ptr(__bsg_x, __bsg_y + 1, (void *)&mailboxes->left_ready);
  mailboxes->top_ready_flag =
    (volatile int *)bsg_remote_ptr(__bsg_x - 1, __bsg_y, (void *)&mailboxes->bottom_ready);
  mailboxes->bottom_ready_flag =
    (volatile int *)bsg_remote_ptr(__bsg_x + 1, __bsg_y, (void *)&mailboxes->top_ready);
}

template <typename Payload>
inline void reset_mailboxes(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->left.full = 0;
  mailboxes->right.full = 0;
  mailboxes->top.full = 0;
  mailboxes->bottom.full = 0;

  mailboxes->left_ready = 1;
  mailboxes->right_ready = 1;
  mailboxes->top_ready = 1;
  mailboxes->bottom_ready = 1;
}

template <typename Payload>
inline void check_left(hb_mailbox_state_t<Payload> *mailboxes) {
  hb_wait_flag(&mailboxes->left.full);
}

template <typename Payload>
inline void check_right(hb_mailbox_state_t<Payload> *mailboxes) {
  hb_wait_flag(&mailboxes->right.full);
}

template <typename Payload>
inline void check_top(hb_mailbox_state_t<Payload> *mailboxes) {
  hb_wait_flag(&mailboxes->top.full);
}

template <typename Payload>
inline void check_bottom(hb_mailbox_state_t<Payload> *mailboxes) {
  hb_wait_flag(&mailboxes->bottom.full);
}

template <typename Payload>
inline Payload read_left(hb_mailbox_state_t<Payload> *mailboxes) {
  return mailboxes->left.data;
}

template <typename Payload>
inline Payload read_right(hb_mailbox_state_t<Payload> *mailboxes) {
  return mailboxes->right.data;
}

template <typename Payload>
inline Payload read_top(hb_mailbox_state_t<Payload> *mailboxes) {
  return mailboxes->top.data;
}

template <typename Payload>
inline Payload read_bottom(hb_mailbox_state_t<Payload> *mailboxes) {
  return mailboxes->bottom.data;
}

template <typename Payload>
inline Payload peek_right(hb_mailbox_state_t<Payload> *mailboxes) {
  return mailboxes->right.data;
}

template <typename Payload>
inline void clear_left_full(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->left.full = 0;
}

template <typename Payload>
inline void clear_right_full(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->right.full = 0;
}

template <typename Payload>
inline void clear_top_full(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->top.full = 0;
}

template <typename Payload>
inline void clear_bottom_full(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->bottom.full = 0;
}

template <typename Payload>
inline void check_and_clear_left(hb_mailbox_state_t<Payload> *mailboxes) {
  check_left(mailboxes);
  clear_left_full(mailboxes);
}

template <typename Payload>
inline void check_and_clear_right(hb_mailbox_state_t<Payload> *mailboxes) {
  check_right(mailboxes);
  clear_right_full(mailboxes);
}

template <typename Payload>
inline void check_and_clear_top(hb_mailbox_state_t<Payload> *mailboxes) {
  check_top(mailboxes);
  clear_top_full(mailboxes);
}

template <typename Payload>
inline void check_and_clear_bottom(hb_mailbox_state_t<Payload> *mailboxes) {
  check_bottom(mailboxes);
  clear_bottom_full(mailboxes);
}

template <typename Payload>
inline void set_left_full(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->left_mailbox->full = 1;
}

template <typename Payload>
inline void set_right_full(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->right_mailbox->full = 1;
}

template <typename Payload>
inline void set_top_full(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->top_mailbox->full = 1;
}

template <typename Payload>
inline void set_bottom_full(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->bottom_mailbox->full = 1;
}

template <typename Payload>
inline void check_left_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  hb_wait_flag(&mailboxes->left_ready);
}

template <typename Payload>
inline void check_right_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  hb_wait_flag(&mailboxes->right_ready);
}

template <typename Payload>
inline void check_top_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  hb_wait_flag(&mailboxes->top_ready);
}

template <typename Payload>
inline void check_bottom_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  hb_wait_flag(&mailboxes->bottom_ready);
}

template <typename Payload>
inline void clear_left_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->left_ready = 0;
}

template <typename Payload>
inline void clear_right_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->right_ready = 0;
}

template <typename Payload>
inline void clear_top_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->top_ready = 0;
}

template <typename Payload>
inline void clear_bottom_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  mailboxes->bottom_ready = 0;
}

template <typename Payload>
inline void check_and_clear_left_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  check_left_ready(mailboxes);
  clear_left_ready(mailboxes);
}

template <typename Payload>
inline void check_and_clear_right_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  check_right_ready(mailboxes);
  clear_right_ready(mailboxes);
}

template <typename Payload>
inline void check_and_clear_top_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  check_top_ready(mailboxes);
  clear_top_ready(mailboxes);
}

template <typename Payload>
inline void check_and_clear_bottom_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  check_bottom_ready(mailboxes);
  clear_bottom_ready(mailboxes);
}

template <typename Payload>
inline void set_left_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  *mailboxes->left_ready_flag = 1;
}

template <typename Payload>
inline void set_right_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  *mailboxes->right_ready_flag = 1;
}

template <typename Payload>
inline void set_top_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  *mailboxes->top_ready_flag = 1;
}

template <typename Payload>
inline void set_bottom_ready(hb_mailbox_state_t<Payload> *mailboxes) {
  *mailboxes->bottom_ready_flag = 1;
}

#endif
