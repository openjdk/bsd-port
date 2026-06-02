/*
 * Copyright (c) 2012, 2026, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2026, The FreeBSD Foundation
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "runtime/os_perf.hpp"

static const double NANOS_PER_SEC = 1000000000.0;
static const time_t MICROS_PER_SEC = 1000000LL;

class CPUPerformanceInterface::CPUPerformance : public CHeapObj<mtInternal> {
   friend class CPUPerformanceInterface;
 private:
  uint64_t _jvm_real;
  uint64_t _total_csr_nanos;
  uint64_t _jvm_user;
  uint64_t _jvm_system;
  long _jvm_context_switches;
  long _used_ticks;
  long _total_ticks;
  int  _active_processor_count;

  bool now_in_nanos(uint64_t* resultp) {
    struct timespec tp;
    int status = clock_gettime(CLOCK_REALTIME, &tp);
    assert(status == 0, "clock_gettime error: %s", os::strerror(errno));
    if (status != 0) {
      return false;
    }
    *resultp = tp.tv_sec * (uint64_t)NANOS_PER_SEC + tp.tv_nsec;
    return true;
  }
  double normalize(double value) {
    return MIN2<double>(MAX2<double>(value, 0.0), 1.0);
  }

  int cpu_load(int which_logical_cpu, double* cpu_load);
  int context_switch_rate(double* rate);
  int cpu_load_total_process(double* cpu_load);
  int cpu_loads_process(double* pjvmUserLoad, double* pjvmKernelLoad, double* psystemTotalLoad);

  NONCOPYABLE(CPUPerformance);

 public:
  CPUPerformance();
  bool initialize();
  ~CPUPerformance();
};
