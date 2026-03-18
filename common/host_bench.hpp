#ifndef COMMON_HOST_BENCH_HPP
#define COMMON_HOST_BENCH_HPP

#include "repeat_config.hpp"

#include <cstdio>
#include <time.h>

inline double elapsed_seconds(const timespec& start, const timespec& end) {
  return static_cast<double>(end.tv_sec - start.tv_sec) +
         (static_cast<double>(end.tv_nsec - start.tv_nsec) / 1000000000.0);
}

inline void print_kernel_launch_time(const timespec& start, const timespec& end) {
  std::printf("kernel_launch_time_sec=%.9f\n", elapsed_seconds(start, end));
}

#endif
