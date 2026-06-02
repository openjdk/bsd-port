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

#include "os_perf_bsd.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/os.hpp"
#include "runtime/os_perf.hpp"
#include "runtime/vm_version.hpp"
#include "utilities/globalDefinitions.hpp"

#include <sys/sched.h>
#include <sys/resource.h>
#define NET_RT_IFLIST2 NET_RT_IFLIST
#define RTM_IFINFO2    RTM_IFINFO

#ifdef __NetBSD__
  #include <uvm/uvm_extern.h>
#else
  #include <sys/user.h>
#endif
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/times.h>

CPUPerformanceInterface::CPUPerformance::CPUPerformance() {
  _num_procs = 0;
  _stathz = 0;
  _jvm_ticks = JVMTicks();
  _cpus = nullptr;
  _total_csr_nanos= 0;
  _jvm_context_switches = 0;
}

bool CPUPerformanceInterface::CPUPerformance::initialize() {
  _num_procs = os::processor_count();
  if (_num_procs < 1) {
    return false;
  }

  if (init_stathz() != OS_OK) {
    return false;
  }

  size_t cpus_array_count = _num_procs + 1;
  _cpus = NEW_C_HEAP_ARRAY_RETURN_NULL(CPUTicks, cpus_array_count, mtInternal);
  if (_cpus == nullptr) {
    return false;
  }
  memset(_cpus, 0, cpus_array_count * sizeof(*_cpus));

  // For the CPU load total
  if (get_cpu_ticks(&_cpus[_num_procs], -1) != OS_OK) {
    FREE_C_HEAP_ARRAY(_cpus);
    _cpus = nullptr;
    return false;
  }

  // For each CPU. Ignoring errors.
  for (int i = 0; i < _num_procs; i++) {
    get_cpu_ticks(&_cpus[i], i);
  }

  // For JVM load
  if (get_jvm_ticks(&_jvm_ticks) != OS_OK) {
    FREE_C_HEAP_ARRAY(_cpus);
    _cpus = nullptr;
    return false;
  }
  return true;
}

CPUPerformanceInterface::CPUPerformance::~CPUPerformance() {
  if (_cpus != nullptr) {
    FREE_C_HEAP_ARRAY(_cpus);
  }
}

int CPUPerformanceInterface::CPUPerformance::init_stathz(void) {
  struct clockinfo ci;
  size_t length = sizeof(ci);
  int mib[] = { CTL_KERN, KERN_CLOCKRATE };
  const u_int miblen = sizeof(mib) / sizeof(mib[0]);

  if (sysctl(mib, miblen, &ci, &length, nullptr, 0) == -1) {
    return OS_ERR;
  }

  _stathz = ci.stathz;

  return OS_OK;
}

int CPUPerformanceInterface::CPUPerformance::get_cpu_ticks(CPUTicks *ticks, int which_logical_cpu) {
#if defined(__NetBSD__)
  uint64_t cpu_load_info[CPUSTATES];
#else
  long cpu_load_info[CPUSTATES];
#endif
  size_t length = sizeof(cpu_load_info);

  if (which_logical_cpu == -1) {
#if defined(__OpenBSD__)
    int mib[] = { CTL_KERN, KERN_CPTIME };
    const u_int miblen = sizeof(mib) / sizeof(mib[0]);

    if (sysctl(mib, miblen, &cpu_load_info, &length, nullptr, 0) == -1) {
      return OS_ERR;
    }
    // OpenBSD returns the sum/_num_procs. Unify with other stat units
    for (size_t i=0; i < CPUSTATES; i++) {
       cpu_load_info[i] *= _num_procs;
    }
#else
    if (sysctlbyname("kern.cp_time", &cpu_load_info, &length, nullptr, 0) == -1) {
      return OS_ERR;
    }
#endif
  } else {
#if defined(__OpenBSD__)
    int mib[] = { CTL_KERN, KERN_CPTIME2, which_logical_cpu };
    const u_int miblen = sizeof(mib) / sizeof(mib[0]);

    if (sysctl(mib, miblen, &cpu_load_info, &length, nullptr, 0) == -1) {
      return OS_ERR;
    }
#elif defined(__FreeBSD__)
    size_t alllength = length * _num_procs;
    long *allcpus = NEW_C_HEAP_ARRAY(long, CPUSTATES * _num_procs, mtInternal);

    if (sysctlbyname("kern.cp_times", allcpus, &alllength, nullptr, 0) == -1) {
      FREE_C_HEAP_ARRAY(allcpus);
      return OS_ERR;
    }

    memcpy(cpu_load_info, &allcpus[which_logical_cpu * CPUSTATES], sizeof(long) * CPUSTATES);
    FREE_C_HEAP_ARRAY(allcpus);
#else
    char name[24];
    os::snprintf_checked(name, sizeof(name), "kern.cp_time.%d", which_logical_cpu);
    if (sysctlbyname(name, &cpu_load_info, &length, nullptr, 0) == -1) {
      return OS_ERR;
    }
#endif
  }

  ticks->totalTicks = 0;
  for (size_t i=0; i < CPUSTATES; i++) {
     ticks->totalTicks += cpu_load_info[i];
  }
  ticks->usedTicks = ticks->totalTicks - cpu_load_info[CP_IDLE];

  return OS_OK;
}

uint64_t CPUPerformanceInterface::CPUPerformance::tvtoticks(struct timeval tv) {
  uint64_t ticks = 0;
  ticks += (uint64_t)tv.tv_sec * _stathz;
  ticks += (uint64_t)tv.tv_usec * _stathz / MICROS_PER_SEC;
  return ticks;
}

int CPUPerformanceInterface::CPUPerformance::get_jvm_ticks(JVMTicks *jvm_ticks) {
  struct rusage usage;

  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return OS_ERR;
  }

  if (get_cpu_ticks(&jvm_ticks->cpuTicks, -1) != OS_OK) {
    return OS_ERR;
  }

  jvm_ticks->userTicks = tvtoticks(usage.ru_utime);
  jvm_ticks->systemTicks = tvtoticks(usage.ru_stime);

  // ensure values are consistent with each other
  if (jvm_ticks->userTicks + jvm_ticks->systemTicks > jvm_ticks->cpuTicks.usedTicks)
    jvm_ticks->cpuTicks.usedTicks = jvm_ticks->userTicks + jvm_ticks->systemTicks;

  if (jvm_ticks->cpuTicks.usedTicks > jvm_ticks->cpuTicks.totalTicks)
    jvm_ticks->cpuTicks.totalTicks = jvm_ticks->cpuTicks.usedTicks;

  return OS_OK;
}

int CPUPerformanceInterface::CPUPerformance::cpu_load(int which_logical_cpu, double* cpu_load) {
  CPUTicks curCPUTicks, *prevCPUTicks;
  uint64_t cpuUsedDelta, cpuTotalDelta;

  *cpu_load = 0.0;

  if (_cpus == nullptr) {
    return OS_ERR;
  }

  if (which_logical_cpu < -1 || which_logical_cpu >= _num_procs) {
    return OS_ERR;
  }

  if (get_cpu_ticks(&curCPUTicks, which_logical_cpu) != OS_OK) {
    return OS_ERR;
  }

  const int cpu_idx = (which_logical_cpu == -1) ? _num_procs : which_logical_cpu;
  prevCPUTicks = &_cpus[cpu_idx];

  cpuUsedDelta = curCPUTicks.usedTicks > prevCPUTicks->usedTicks ?
    curCPUTicks.usedTicks - prevCPUTicks->usedTicks : 0;
  cpuTotalDelta = curCPUTicks.totalTicks > prevCPUTicks->totalTicks ?
    curCPUTicks.totalTicks - prevCPUTicks->totalTicks : 0;

  prevCPUTicks->usedTicks = curCPUTicks.usedTicks;
  prevCPUTicks->totalTicks = curCPUTicks.totalTicks;

  if (cpuTotalDelta == 0)
    return OS_ERR;

  if (cpuUsedDelta > cpuTotalDelta)
    cpuTotalDelta = cpuUsedDelta;

  *cpu_load = (double)cpuUsedDelta/cpuTotalDelta;

  return OS_OK;
}

int CPUPerformanceInterface::CPUPerformance::cpu_load_total_process(double* cpu_load) {
  double jvmUserLoad, jvmKernelLoad, systemTotalLoad;

  if (cpu_loads_process(&jvmUserLoad, &jvmKernelLoad, &systemTotalLoad) != OS_OK) {
    *cpu_load = 0.0;
    return OS_ERR;
  }

  *cpu_load = jvmUserLoad + jvmKernelLoad;
  return OS_OK;
}

int CPUPerformanceInterface::CPUPerformance::cpu_loads_process(double* pjvmUserLoad, double* pjvmKernelLoad, double* psystemTotalLoad) {
  JVMTicks curJVMTicks;
  CPUTicks *curCPUTicks, *prevCPUTicks;

  uint64_t jvmUserDelta, jvmSystemDelta, cpuUsedDelta, cpuTotalDelta;

  *pjvmUserLoad = 0.0;
  *pjvmKernelLoad = 0.0;
  *psystemTotalLoad = 0.0;

  if (_cpus == nullptr) {
    return OS_ERR;
  }

  if (get_jvm_ticks(&curJVMTicks) != OS_OK) {
    return OS_ERR;
  }

  curCPUTicks = &curJVMTicks.cpuTicks;
  prevCPUTicks = &_jvm_ticks.cpuTicks;

  jvmUserDelta = curJVMTicks.userTicks > _jvm_ticks.userTicks ?
    curJVMTicks.userTicks - _jvm_ticks.userTicks : 0;
  jvmSystemDelta = curJVMTicks.systemTicks > _jvm_ticks.systemTicks ?
    curJVMTicks.systemTicks - _jvm_ticks.systemTicks : 0;

  cpuUsedDelta = curCPUTicks->usedTicks > prevCPUTicks->usedTicks ?
    curCPUTicks->usedTicks - prevCPUTicks->usedTicks : 0;
  cpuTotalDelta = curCPUTicks->totalTicks > prevCPUTicks->totalTicks ?
    curCPUTicks->totalTicks - prevCPUTicks->totalTicks : 0;

  _jvm_ticks.userTicks = curJVMTicks.userTicks;
  _jvm_ticks.systemTicks = curJVMTicks.systemTicks;
  prevCPUTicks->usedTicks = curCPUTicks->usedTicks;
  prevCPUTicks->totalTicks = curCPUTicks->totalTicks;

  // ensure values are consistent with each other
  if (jvmUserDelta + jvmSystemDelta > cpuUsedDelta)
    cpuUsedDelta = jvmUserDelta + jvmSystemDelta;

  if (cpuUsedDelta > cpuTotalDelta)
    cpuTotalDelta = cpuUsedDelta;

  if (cpuTotalDelta == 0) {
    return OS_ERR;
  }

  *pjvmUserLoad = (double)jvmUserDelta/cpuTotalDelta;
  *pjvmKernelLoad = (double)jvmSystemDelta/cpuTotalDelta;
  *psystemTotalLoad = (double)cpuUsedDelta/cpuTotalDelta;

  return OS_OK;
}

int CPUPerformanceInterface::CPUPerformance::context_switch_rate(double* rate) {
#if defined(__FreeBSD__)
  unsigned int jvm_context_switches = 0;
  size_t length = sizeof(jvm_context_switches);
  if (sysctlbyname("vm.stats.sys.v_swtch", &jvm_context_switches, &length, nullptr, 0) == -1) {
    return OS_ERR;
  }
#else
# if defined(__OpenBSD__)
  struct uvmexp js;
  int mib[] = { CTL_VM, VM_UVMEXP };
# elif defined(__NetBSD__)
  struct uvmexp_sysctl js;
  int mib[] = { CTL_VM, VM_UVMEXP2 };
# endif
  size_t jslength = sizeof(js);
  const u_int miblen = sizeof(mib) / sizeof(mib[0]);
  unsigned int jvm_context_switches = 0;
  if (sysctl(mib, miblen, &js, &jslength, nullptr, 0) != 0) {
    return OS_ERR;
  }

  jvm_context_switches = (unsigned int)js.swtch;

#endif // __FreeBSD__

  int result = OS_OK;
  if (_total_csr_nanos == 0 || _jvm_context_switches == 0) {
    // First call just set initial values.
    result = OS_ERR;
  }

  uint64_t total_csr_nanos;
  if(!now_in_nanos(&total_csr_nanos)) {
    return OS_ERR;
  }
  double delta_in_sec = (double)(total_csr_nanos - _total_csr_nanos) / NANOS_PER_SEC;
  if (delta_in_sec == 0.0) {
    // Avoid division by zero
    return OS_ERR;
  }

  *rate = (jvm_context_switches - _jvm_context_switches) / delta_in_sec;

  _jvm_context_switches = jvm_context_switches;
  _total_csr_nanos = total_csr_nanos;

  return result;
}

#if defined(__FreeBSD__) || defined(__OpenBSD__)
#define KINFO_PROC_TYPE struct kinfo_proc
#define KERN_PROC2 KERN_PROC
#elif defined(__NetBSD__)
#define KINFO_PROC_TYPE struct kinfo_proc2
#endif

#if defined(__FreeBSD__)
#define KINFO_PROC_PID(ki) ki.ki_pid
#elif defined(__OpenBSD__) || defined(__NetBSD__)
#define KINFO_PROC_PID(ki) ki.p_pid
#endif

size_t proc_listpids(int flag, int unused, pid_t * pids, size_t pids_bytes) {
  KINFO_PROC_TYPE *lproc;
  int mib[] = { CTL_KERN, KERN_PROC2, KERN_PROC_ALL,
#if defined(__OpenBSD__) || defined(__NetBSD__)
    0, sizeof(*lproc), 0
#endif
  };

  const u_int miblen = sizeof(mib) / sizeof(mib[0]);
  size_t length = pids_bytes;

  if (sysctl(mib, miblen, nullptr, &length, nullptr, 0) == -1) {
    return 0;
  }

  if (pids != nullptr) {
    lproc = NEW_C_HEAP_ARRAY_RETURN_NULL(KINFO_PROC_TYPE, length, mtInternal);
    if (lproc == nullptr) {
      return 0;
    }

#if defined(__OpenBSD__) || defined(__NetBSD__)
    mib[5] = length / sizeof(*lproc);
#endif

    if (sysctl(mib, miblen, lproc, &length, nullptr, 0) == -1) {
      FREE_C_HEAP_ARRAY(lproc);
      return 0;
    }

    int pid_count = length / sizeof(*lproc);
    for (int i = 0; i < pid_count; i++) {
      pids[i] = KINFO_PROC_PID(lproc[i]);
    }

    FREE_C_HEAP_ARRAY(lproc);
  }

  return length;
}

#if defined(__NetBSD__) && !defined(KERN_PROC_PATHNAME)
#define KERN_PROC_PATHNAME 5
#endif

int proc_pidpath(pid_t pid, char * pbuf, size_t plen) {
#if defined(__FreeBSD__)
  int pmib[] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, pid };
  const u_int pmiblen = sizeof(pmib) / sizeof(pmib[0]);
  if (sysctl(pmib, pmiblen, pbuf, &plen, nullptr, 0) == -1) {
    return -1;
  }
#elif defined(__OpenBSD__)
  int pmib[] = { CTL_KERN, KERN_PROC_ARGS, pid, KERN_PROC_ARGV };
  const u_int pmiblen = sizeof(pmib) / sizeof(pmib[0]);
  size_t length;

  if (sysctl(pmib, pmiblen, nullptr, &length, nullptr, 0) == -1) {
    return -1;
  }

  // Allocate space for args and get the arguments
  char **argv = NEW_C_HEAP_ARRAY_RETURN_NULL(char*, length, mtInternal);
  if (argv == nullptr) {
    return -1;
  }

  if (sysctl(pmib, pmiblen, argv, &length, nullptr, 0) == -1) {
    FREE_C_HEAP_ARRAY(argv);
    return -1;
  }

  if (argv[0] == nullptr) {
    FREE_C_HEAP_ARRAY(argv);
    return -1;
  }

  strlcpy(pbuf, argv[0], plen);

#elif defined(__NetBSD__)
  int pmib[] = { CTL_KERN, KERN_PROC_ARGS, pid, KERN_PROC_PATHNAME };
  const u_int pmiblen = sizeof(pmib) / sizeof(pmib[0]);
  if (sysctl(pmib, pmiblen, pbuf, &plen, nullptr, 0) == -1) {
    return -1;
  }
#endif

  return 0;
}
