/*
  +----------------------------------------------------------------------+
  | Swoole                                                               |
  +----------------------------------------------------------------------+
  | This source file is subject to version 2.0 of the Apache license,    |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.apache.org/licenses/LICENSE-2.0.html                      |
  | If you did not receive a copy of the Apache2.0 license and are unable|
  | to obtain it through the world-wide-web, please send a note to       |
  | license@swoole.com so we can mail you a copy immediately.            |
  +----------------------------------------------------------------------+
  | Author: Tianfeng Han  <mikan.tenny@gmail.com>                        |
  |         Twosee  <twose@qq.com>                                       |
  +----------------------------------------------------------------------+
*/

#pragma once

#include "swoole_api.h"
#include "swoole_string.h"
#include "swoole_socket.h"
#include "swoole_reactor.h"
#include "swoole_timer.h"
#include "swoole_async.h"

#include "swoole_coroutine_context.h"

#include <limits.h>

#include <functional>
#include <string>
#include <unordered_map>

namespace swoole {
class Coroutine {
  public:
    void resume();
    void yield();

    void resume_naked();
    void yield_naked();

    constexpr static int STACK_ALIGNED_SIZE = (4 * 1024);
    constexpr static int MIN_STACK_SIZE = (64 * 1024);
    constexpr static int MAX_STACK_SIZE = (16 * 1024 * 1024);
    constexpr static long MAX_NUM_LIMIT = LONG_MAX;

    enum State {
        STATE_INIT = 0,
        STATE_WAITING,
        STATE_RUNNING,
        STATE_END,
    };

    enum Error {
        ERR_END = 0,
        ERR_LIMIT = -1,
        ERR_INVALID = -2,
    };

    typedef void (*SwapCallback)(void *);
    typedef void (*BailoutCallback)();

    inline enum State get_state() {
        return state;
    }

    inline long get_init_msec() {
        return init_msec;
    }

    inline long get_cid() {
        return cid;
    }

    inline Coroutine *get_origin() {
        return origin;
    }

    inline long get_origin_cid() {
        return sw_likely(origin) ? origin->get_cid() : -1;
    }

    inline void *get_task() {
        return task;
    }

    inline bool is_end() {
        return ctx.is_end();
    }

    inline void set_task(void *_task) {
        task = _task;
    }

    static std::unordered_map<long, Coroutine *> coroutines;

    static void set_on_yield(SwapCallback func);
    static void set_on_resume(SwapCallback func);
    static void set_on_close(SwapCallback func);
    static void bailout(BailoutCallback func);

    static inline long create(const coroutine_func_t &fn, void *args = nullptr) {
        return (new Coroutine(fn, args))->run();
    }

    static inline Coroutine *get_current() {
        return current;
    }

    static inline Coroutine *get_current_safe() {
        if (sw_unlikely(!current)) {
            swFatalError(SW_ERROR_CO_OUT_OF_COROUTINE, "API must be called in the coroutine");
        }
        return current;
    }

    static inline void *get_current_task() {
        return sw_likely(current) ? current->get_task() : nullptr;
    }

    static inline long get_current_cid() {
        return sw_likely(current) ? current->get_cid() : -1;
    }

    static inline Coroutine *get_by_cid(long cid) {
        auto i = coroutines.find(cid);
        return sw_likely(i != coroutines.end()) ? i->second : nullptr;
    }

    static inline void *get_task_by_cid(long cid) {
        Coroutine *co = get_by_cid(cid);
        return sw_likely(co) ? co->get_task() : nullptr;
    }

    static inline size_t get_stack_size() {
        return stack_size;
    }

    static inline void set_stack_size(size_t size) {
        stack_size = SW_MEM_ALIGNED_SIZE_EX(SW_MAX(MIN_STACK_SIZE, SW_MIN(size, MAX_STACK_SIZE)), STACK_ALIGNED_SIZE);
    }

    static inline long get_last_cid() {
        return last_cid;
    }

    static inline size_t count() {
        return coroutines.size();
    }

    static inline uint64_t get_peak_num() {
        return peak_num;
    }

    static inline long get_elapsed(long cid) {
        Coroutine *co = cid == 0 ? get_current() : get_by_cid(cid);
        return sw_likely(co) ? Timer::get_absolute_msec() - co->get_init_msec() : -1;
    }

    static void print_list();

  protected:
    static Coroutine *current;
    static long last_cid;
    static uint64_t peak_num;
    static size_t stack_size;
    static SwapCallback on_yield;   /* before yield */
    static SwapCallback on_resume;  /* before resume */
    static SwapCallback on_close;   /* before close */
    static BailoutCallback on_bailout; /* when bailout */

    enum State state = STATE_INIT;
    long cid;
    long init_msec = Timer::get_absolute_msec();
    void *task = nullptr;
    coroutine::Context ctx;
    Coroutine *origin;

    Coroutine(const coroutine_func_t &fn, void *private_data) : ctx(stack_size, fn, private_data) {
        cid = ++last_cid;
        coroutines[cid] = this;
        if (sw_unlikely(count() > peak_num)) {
            peak_num = count();
        }
    }

    inline long run() {
        long cid = this->cid;
        origin = current;
        current = this;
        ctx.swap_in();
        check_end();
        return cid;
    }

    inline void check_end() {
        if (ctx.is_end()) {
            close();
        } else if (sw_unlikely(on_bailout)) {
            SW_ASSERT(current == nullptr);
            on_bailout();
            // expect that never here
            exit(1);
        }
    }

    void close();
};
//-------------------------------------------------------------------------------
namespace coroutine {
bool async(async::Handler handler, AsyncEvent &event, double timeout = -1);
bool async(const std::function<void(void)> &fn, double timeout = -1);
bool run(const coroutine_func_t &fn, void *arg = nullptr);
}  // namespace coroutine
//-------------------------------------------------------------------------------
}  // namespace swoole

/**
 * for gdb
 */
swoole::Coroutine *swoole_coro_iterator_each();
void swoole_coro_iterator_reset();
swoole::Coroutine *swoole_coro_get(long cid);
size_t swoole_coro_count();
