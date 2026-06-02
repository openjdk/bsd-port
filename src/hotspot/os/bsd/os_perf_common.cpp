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

#if defined(__APPLE__)
#include "os_perf_macos.hpp"
#import <libproc.h>
#else
#include "os_perf_bsd.hpp"
#endif
#include "memory/resourceArea.hpp"
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

CPUPerformanceInterface::CPUPerformanceInterface() {
  _impl = nullptr;
}

bool CPUPerformanceInterface::initialize() {
  _impl = new CPUPerformanceInterface::CPUPerformance();
  return _impl->initialize();
}

CPUPerformanceInterface::~CPUPerformanceInterface() {
  if (_impl != nullptr) {
    delete _impl;
  }
}

int CPUPerformanceInterface::cpu_load(int which_logical_cpu, double* cpu_load) const {
  return _impl->cpu_load(which_logical_cpu, cpu_load);
}

int CPUPerformanceInterface::cpu_load_total_process(double* cpu_load) const {
  return _impl->cpu_load_total_process(cpu_load);
}

int CPUPerformanceInterface::cpu_loads_process(double* pjvmUserLoad, double* pjvmKernelLoad, double* psystemTotalLoad) const {
  return _impl->cpu_loads_process(pjvmUserLoad, pjvmKernelLoad, psystemTotalLoad);
}

int CPUPerformanceInterface::context_switch_rate(double* rate) const {
  return _impl->context_switch_rate(rate);
}

class SystemProcessInterface::SystemProcesses : public CHeapObj<mtInternal> {
  friend class SystemProcessInterface;
 private:
  SystemProcesses();
  bool initialize();
  NONCOPYABLE(SystemProcesses);
  ~SystemProcesses();

  //information about system processes
  int system_processes(SystemProcess** system_processes, int* no_of_sys_processes) const;
};

SystemProcessInterface::SystemProcesses::SystemProcesses() {
}

bool SystemProcessInterface::SystemProcesses::initialize() {
  return true;
}

SystemProcessInterface::SystemProcesses::~SystemProcesses() {
}

int SystemProcessInterface::SystemProcesses::system_processes(SystemProcess** system_processes, int* no_of_sys_processes) const {
  assert(system_processes != nullptr, "system_processes pointer is null!");
  assert(no_of_sys_processes != nullptr, "system_processes counter pointer is null!");
  pid_t* pids = nullptr;
  int pid_count = 0;
  ResourceMark rm;

  int try_count = 0;
  while (pids == nullptr) {
    // Find out buffer size
    size_t pids_bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (pids_bytes <= 0) {
      return OS_ERR;
    }
    pid_count = pids_bytes / sizeof(pid_t);
    pids = NEW_RESOURCE_ARRAY(pid_t, pid_count);
    memset(pids, 0, pids_bytes);

    pids_bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, pids_bytes);
    if (pids_bytes <= 0) {
       // couldn't fit buffer, retry.
      FREE_RESOURCE_ARRAY(pids, pid_count);
      pids = nullptr;
      try_count++;
      if (try_count > 3) {
      return OS_ERR;
      }
    } else {
      pid_count = pids_bytes / sizeof(pid_t);
    }
  }

  int process_count = 0;
  SystemProcess* next = nullptr;
  for (int i = 0; i < pid_count; i++) {
    pid_t pid = pids[i];
    if (pid != 0) {
      char buffer[PROC_PIDPATHINFO_MAXSIZE];
      memset(buffer, 0 , sizeof(buffer));
      if (proc_pidpath(pid, buffer, sizeof(buffer)) != -1) {
        int length = strlen(buffer);
        if (length > 0) {
          SystemProcess* current = new SystemProcess();
          char * path = NEW_C_HEAP_ARRAY(char, length + 1, mtInternal);
          strcpy(path, buffer);
          current->set_path(path);
          current->set_pid((int)pid);
          current->set_next(next);
          next = current;
          process_count++;
        }
      }
    }
  }

  *no_of_sys_processes = process_count;
  *system_processes = next;

  return OS_OK;
}

int SystemProcessInterface::system_processes(SystemProcess** system_procs, int* no_of_sys_processes) const {
  return _impl->system_processes(system_procs, no_of_sys_processes);
}

SystemProcessInterface::SystemProcessInterface() {
  _impl = nullptr;
}

bool SystemProcessInterface::initialize() {
  _impl = new SystemProcessInterface::SystemProcesses();
  return _impl->initialize();
}

SystemProcessInterface::~SystemProcessInterface() {
  if (_impl != nullptr) {
    delete _impl;
 }
}

CPUInformationInterface::CPUInformationInterface() {
  _cpu_info = nullptr;
}

bool CPUInformationInterface::initialize() {
  _cpu_info = new CPUInformation();
  VM_Version::initialize_cpu_information();
  _cpu_info->set_number_of_hardware_threads(VM_Version::number_of_threads());
  _cpu_info->set_number_of_cores(VM_Version::number_of_cores());
  _cpu_info->set_number_of_sockets(VM_Version::number_of_sockets());
  _cpu_info->set_cpu_name(VM_Version::cpu_name());
  _cpu_info->set_cpu_description(VM_Version::cpu_description());
  return true;
}

CPUInformationInterface::~CPUInformationInterface() {
  if (_cpu_info != nullptr) {
    if (_cpu_info->cpu_name() != nullptr) {
      const char* cpu_name = _cpu_info->cpu_name();
      FREE_C_HEAP_ARRAY(cpu_name);
      _cpu_info->set_cpu_name(nullptr);
    }
    if (_cpu_info->cpu_description() != nullptr) {
      const char* cpu_desc = _cpu_info->cpu_description();
      FREE_C_HEAP_ARRAY(cpu_desc);
      _cpu_info->set_cpu_description(nullptr);
    }
    delete _cpu_info;
  }
}

int CPUInformationInterface::cpu_information(CPUInformation& cpu_info) {
  if (nullptr == _cpu_info) {
    return OS_ERR;
  }

  cpu_info = *_cpu_info; // shallow copy assignment
  return OS_OK;
}


class NetworkPerformanceInterface::NetworkPerformance : public CHeapObj<mtInternal> {
  friend class NetworkPerformanceInterface;
 private:
  NetworkPerformance();
  NONCOPYABLE(NetworkPerformance);
  bool initialize();
  ~NetworkPerformance();
  int network_utilization(NetworkInterface** network_interfaces) const;
};

NetworkPerformanceInterface::NetworkPerformance::NetworkPerformance() {
}

bool NetworkPerformanceInterface::NetworkPerformance::initialize() {
  return true;
}

NetworkPerformanceInterface::NetworkPerformance::~NetworkPerformance() {
}

int NetworkPerformanceInterface::NetworkPerformance::network_utilization(NetworkInterface** network_interfaces) const {
  size_t len;
  int mib[] = {CTL_NET, PF_ROUTE, /* protocol number */ 0, /* address family */ 0, NET_RT_IFLIST2, /* NET_RT_FLAGS mask*/ 0};
  if (sysctl(mib, sizeof(mib) / sizeof(mib[0]), nullptr, &len, nullptr, 0) != 0) {
    return OS_ERR;
  }
  uint8_t* buf = NEW_RESOURCE_ARRAY(uint8_t, len);
  if (sysctl(mib, sizeof(mib) / sizeof(mib[0]), buf, &len, nullptr, 0) != 0) {
    return OS_ERR;
  }

  size_t index = 0;
  NetworkInterface* ret = nullptr;
  while (index < len) {
    if_msghdr* msghdr = reinterpret_cast<if_msghdr*>(buf + index);
    index += msghdr->ifm_msglen;

    if (msghdr->ifm_type != RTM_IFINFO2) {
      continue;
    }

#if defined(__APPLE__)
    if_msghdr2* msghdr2 = reinterpret_cast<if_msghdr2*>(msghdr);
    sockaddr_dl* sockaddr = reinterpret_cast<sockaddr_dl*>(msghdr2 + 1);
#else
    sockaddr_dl* sockaddr = reinterpret_cast<sockaddr_dl*>(msghdr + 1);
#endif

    // The interface name is not necessarily NUL-terminated
    char name_buf[128];
    size_t name_len = MIN2(sizeof(name_buf) - 1, static_cast<size_t>(sockaddr->sdl_nlen));
    strlcpy(name_buf, sockaddr->sdl_data, name_len + 1);

#if defined(__APPLE__)
    uint64_t bytes_in = msghdr2->ifm_data.ifi_ibytes;
    uint64_t bytes_out = msghdr2->ifm_data.ifi_obytes;
#else
    uint64_t bytes_in = msghdr->ifm_data.ifi_ibytes;
    uint64_t bytes_out = msghdr->ifm_data.ifi_obytes;
#endif

    NetworkInterface* cur = new NetworkInterface(name_buf, bytes_in, bytes_out, ret);
    ret = cur;
  }

  *network_interfaces = ret;

  return OS_OK;
}
NetworkPerformanceInterface::NetworkPerformanceInterface() {
  _impl = nullptr;
}

NetworkPerformanceInterface::~NetworkPerformanceInterface() {
  if (_impl != nullptr) {
    delete _impl;
  }
}

bool NetworkPerformanceInterface::initialize() {
  _impl = new NetworkPerformanceInterface::NetworkPerformance();
  return _impl->initialize();
}

int NetworkPerformanceInterface::network_utilization(NetworkInterface** network_interfaces) const {
  return _impl->network_utilization(network_interfaces);
}
