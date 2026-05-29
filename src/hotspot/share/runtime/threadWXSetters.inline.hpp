/*
 * Copyright (c) 1997, 2021, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2021, Azul Systems, Inc. All rights reserved.
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

#ifndef SHARE_RUNTIME_THREADWXSETTERS_INLINE_HPP
#define SHARE_RUNTIME_THREADWXSETTERS_INLINE_HPP

// No threadWXSetters.hpp

#ifdef MACOS_AARCH64

#include "classfile/classLoader.hpp"
#include "runtime/perfData.inline.hpp"
#include "runtime/thread.inline.hpp"

class ThreadWXEnable  {
  Thread* _thread;
  WXMode _old_mode;
  WXMode *_this_wx_mode;
  ThreadWXEnable *_prev;
public:
  ThreadWXEnable(WXMode* new_mode, Thread* thread) :
    _thread(thread), _this_wx_mode(new_mode) {
    NOT_PRODUCT(SafePerfTraceTime ptt(ClassLoader::perf_change_wx_time());)
    JavaThread* javaThread
      = _thread && _thread->is_Java_thread()
                ? JavaThread::cast(_thread) : nullptr;
    _prev = javaThread != nullptr ? javaThread->_cur_wx_enable: nullptr;
    _old_mode = _thread != nullptr ? _thread->enable_wx(*new_mode) : WXWrite;
    if (javaThread != nullptr) {
      javaThread->_cur_wx_enable = this;
      javaThread->_cur_wx_mode = new_mode;
    }
  }
  ThreadWXEnable(WXMode new_mode, Thread* thread) :
    _thread(thread), _this_wx_mode(nullptr) {
    NOT_PRODUCT(SafePerfTraceTime ptt(ClassLoader::perf_change_wx_time());)
    JavaThread* javaThread
      = _thread && _thread->is_Java_thread()
        ? JavaThread::cast(_thread) : nullptr;
    _prev = javaThread != nullptr ? javaThread->_cur_wx_enable: nullptr;
    _old_mode = _thread != nullptr ? _thread->enable_wx(new_mode) : WXWrite;
    if (javaThread) {
      javaThread->_cur_wx_enable = this;
      javaThread->_cur_wx_mode = nullptr;
    }
  }

  ~ThreadWXEnable() {
    NOT_PRODUCT(SafePerfTraceTime ptt(ClassLoader::perf_change_wx_time());)
    if (_thread) {
      _thread->enable_wx(_old_mode);
      JavaThread* javaThread
        = _thread && _thread->is_Java_thread()
          ? JavaThread::cast(_thread) : nullptr;
      if (javaThread != nullptr) {
        javaThread->_cur_wx_enable = _prev;
        javaThread->_cur_wx_mode = _prev != nullptr ? _prev->_this_wx_mode : nullptr;
      }
    }
  }

  static bool test(address p);
};
#elif defined(_BSDONLY_SOURCE) && defined(AARCH64)

//
// Per https://cr.openjdk.org/~jrose/jvm/hotspot-cmc.html, HotSpot
// will cross-modify code.
//
// To workaround the potential subtle race issues outlined in the
// paper linked to above, HotSpot calls pthread_jit_write_protect_np()
// on Darwin/aarch64.  On other OSes, different workarounds are applied,
// but some of these appear Cortex specific and are not sufficient on
// NetBSD and FreeBSD when running on Apple Silicon (see
// https://mail-index.netbsd.org/tech-pkg/2025/07/12/msg031385.html and
// https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=265284).
//
// Using pthread_jit_write_protect_np() will temporarily write-protect
// every executable page in the process space for the calling thread,
// so that the thread can execute code in pages that are otherwise
// writable.  The important part, from our perspective, is that the
// function happens to issue data and instruction memory barriers
// (i.e. DSB and ISB).  These are enough to address the issues on
// NetBSD and FreeBSD.
//
// To that end, issue these memory barriers every time Darwin would
// call pthread_jit_write_protect_np().  This is implemented below and
// by the calls to BSD_AARCH64_ONLY(ThreadWXEnable wx(WXWrite, <thread>)
// throughout the code (formerly MACOS_AARCH64_ONLY).
//
// Notes:
// 1. This has a negative performance impact on Cortex chips, but we'll
//    accept that in favour of correctness overall
// 2. OpenBSD doesn't seem to be impacted by this, although it is not
//    clear why that is
//

class ThreadWXEnable  {
  WXMode _new_mode;
public:
  ThreadWXEnable(WXMode * new_mode, Thread*) :
    _new_mode(*new_mode)
  {
    if (_new_mode == WXExec) {
      // We are going to execute some code that has been potentially
      // modified.
      __asm__ __volatile__ ("dsb\tsy\n"
                            "isb\tsy" : : : "memory");
    }
  }

  ThreadWXEnable(WXMode new_mode, Thread * t)
  {
    ThreadWXEnable(&new_mode, t);
  }

  ~ThreadWXEnable() {
    if (_new_mode == WXWrite) {
      // We may have modified some code that is going to be executed
      // outside of this block.
      __asm__ __volatile__ ("dsb\tsy\n"
                            "isb\tsy" : : : "memory");
    }
  }
};
#endif // MACOS_AARCH64

#endif // SHARE_RUNTIME_THREADWXSETTERS_INLINE_HPP
