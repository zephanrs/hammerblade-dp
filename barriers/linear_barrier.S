//  AMOADD barrier

//  a0 = amo lock addr (in DRAM). Initialized to 0
//  a1 = sense word addr (in DMEM).  Initialized to 1

//  void bsg_barrier_amoadd(int*, int*);

.text
.globl bsg_barrier_amoadd
bsg_barrier_amoadd:
  // t0 - sense val
  // t1 - amo result
  // t2 - check val
  // t3 - wakeup val

  // send amoadd
  lw t0, 0(a1)
  amoadd.w t1, t0, 0(a0)
  li t5, 0x20000000
  add t5, t5, a1

  // is sense -1 or +1?
  // set wakeup val
  sub t3, x0, t0
 
  // set check val
  andi t2, t3, (bsg_tiles_X*bsg_tiles_Y)-1

bsg_barrier_amoadd_check:
  bne t2, t1, bsg_barrier_amoadd_sleep
  // wakeup core 0
  sw  t3, 0(t5)

bsg_barrier_amoadd_sleep:
  lr.w t0, 0(a1)
  beq t3, t0, bsg_barrier_amoadd_wakeup
  // we need to check this, in order to support the reservation
  // being cleared by a context switch
  lr.w.aq t0, 0(a1)
  beq t3, t0, bsg_barrier_amoadd_sleep

bsg_barrier_return:
  ret

bsg_barrier_amoadd_wakeup:
  // t0 - x addr
  // t1 - y addr
  // t2 - base address
  lw  t0, __bsg_x
  lw  t1, __bsg_y

  slli t6, t0, 18
  add  t2, t5, t6

  // t4 - wake-up distance
  // t5 - shamt

  li  t4, (bsg_tiles_X - 1) 
  li  t5, 18

  // if y=0, wake-up in x dim
  bne  x0, t1, bsg_barrier_return
  bne  x0, t0, bsg_barrier_wakeup_y
  jal  a2, bsg_barrier_wakeup

bsg_barrier_wakeup_y:
  // wake-up in y dim
  li  t4, (bsg_tiles_Y - 1)
  li  t5, 24
  mv  a2, ra

bsg_barrier_wakeup:
  sll  t6, t4, t5
  add  t6, t2, t6
  sw   t3, 0(t6)
  addi t4, t4, -1
  bne  t4, x0, bsg_barrier_wakeup 
  jr   a2