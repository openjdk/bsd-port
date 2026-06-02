/*
 * Copyright (c) 2012, 2025, Oracle and/or its affiliates. All rights reserved.
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

#include "os_perf_macos.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/os.hpp"
#include "runtime/os_perf.hpp"
#include "runtime/vm_version.hpp"
#include "utilities/globalDefinitions.hpp"

#import <libproc.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/times.h>

CPUPerformanceInterface::CPUPerformance::CPUPerformance() {
  _jvm_real = 0;
  _total_csr_nanos= 0;
  _jvm_context_switches = 0;
  _jvm_user = 0;
  _jvm_system = 0;
  _used_ticks = 0;
  _total_ticks = 0;
  _active_processor_count = 0;
}

bool CPUPerformanceInterface::CPUPerformance::initialize() {
  return true;
}

CPUPerformanceInterface::CPUPerformance::~CPUPerformance() {
}

int CPUPerformanceInterface::CPUPerformance::cpu_load(int which_logical_cpu, double* cpu_load) {
  return FUNCTIONALITY_NOT_IMPLEMENTED;
}

int CPUPerformanceInterface::CPUPerformance::cpu_load_total_process(double* cpu_load) {
  host_name_port_t host = mach_host_self();
  host_flavor_t flavor = HOST_CPU_LOAD_INFO;
  mach_msg_type_number_t host_info_count = HOST_CPU_LOAD_INFO_COUNT;
  host_cpu_load_info_data_t cpu_load_info;

  kern_return_t kr = host_statistics(host, flavor, (host_info_t)&cpu_load_info, &host_info_count);
  if (kr != KERN_SUCCESS) {
    return OS_ERR;
  }

  long used_ticks  = cpu_load_info.cpu_ticks[CPU_STATE_USER] + cpu_load_info.cpu_ticks[CPU_STATE_NICE] + cpu_load_info.cpu_ticks[CPU_STATE_SYSTEM];
  long total_ticks = used_ticks + cpu_load_info.cpu_ticks[CPU_STATE_IDLE];

  if (_used_ticks == 0 || _total_ticks == 0) {
    // First call, just set the values
    _used_ticks  = used_ticks;
    _total_ticks = total_ticks;
    return OS_ERR;
  }

  long used_delta  = used_ticks - _used_ticks;
  long total_delta = total_ticks - _total_ticks;

  _used_ticks  = used_ticks;
  _total_ticks = total_ticks;

  if (total_delta == 0) {
    // Avoid division by zero
    return OS_ERR;
  }

  *cpu_load = (double)used_delta / total_delta;

  return OS_OK;
}

int CPUPerformanceInterface::CPUPerformance::cpu_loads_process(double* pjvmUserLoad, double* pjvmKernelLoad, double* psystemTotalLoad) {
  int result = cpu_load_total_process(psystemTotalLoad);

  struct tms buf;
  clock_t jvm_real = times(&buf);
  if (jvm_real == (clock_t) (-1)) {
    return OS_ERR;
  }

  int active_processor_count = os::active_processor_count();
  uint64_t jvm_user = buf.tms_utime;
  uint64_t jvm_system = buf.tms_stime;

  if (active_processor_count != _active_processor_count) {
    // Change in active processor count
    result = OS_ERR;
  } else {
    uint64_t delta = active_processor_count * (jvm_real - _jvm_real);
    if (delta == 0) {
      // Avoid division by zero
      return OS_ERR;
    }

    *pjvmUserLoad = normalize((double)(jvm_user - _jvm_user) / delta);
    *pjvmKernelLoad = normalize((double)(jvm_system - _jvm_system) / delta);
  }

  _active_processor_count = active_processor_count;
  _jvm_real = jvm_real;
  _jvm_user = jvm_user;
  _jvm_system = jvm_system;

  return result;
}

int CPUPerformanceInterface::CPUPerformance::context_switch_rate(double* rate) {
  mach_port_t task = mach_task_self();
  mach_msg_type_number_t task_info_count = TASK_INFO_MAX;
  task_info_data_t task_info_data;
  kern_return_t kr = task_info(task, TASK_EVENTS_INFO, (task_info_t)task_info_data, &task_info_count);
  if (kr != KERN_SUCCESS) {
    return OS_ERR;
  }

  long jvm_context_switches = ((task_events_info_t)task_info_data)->csw;

  int result = OS_OK;
  if (_total_csr_nanos == 0 || _jvm_context_switches == 0) {
    // First call just set initial values.
    result = OS_ERR;
  }

  uint64_t total_csr_nanos;
  if(!now_in_nanos(&total_csr_nanos)) {
    return OS_ERR;
  }
  double delta_in_sec = (double)(total_csr_nanos - _total_csr_nanos) / (double)NANOS_PER_SEC;
  if (delta_in_sec == 0.0) {
    // Avoid division by zero
    return OS_ERR;
  }

  *rate = (jvm_context_switches - _jvm_context_switches) / delta_in_sec;

  _jvm_context_switches = jvm_context_switches;
  _total_csr_nanos = total_csr_nanos;

  return result;
}
