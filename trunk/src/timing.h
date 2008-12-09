#ifndef TIMING_H
#define TIMING_H

/*
 * Exports and imports from timing.c.
 *
 * schedule_timer() asks the front end to schedule a callback to a
 * timer function in a given number of ticks. The returned value is
 * the time (in ticks since an arbitrary offset) at which the
 * callback can be expected. This value will also be passed as the
 * `now' parameter to the callback function. Hence, you can (for
 * example) schedule an event at a particular time by calling
 * schedule_timer() and storing the return value in your context
 * structure as the time when that event is due. The first time a
 * callback function gives you that value or more as `now', you do
 * the thing.
 * 
 * expire_timer_context() drops all current timers associated with
 * a given value of ctx (for when you're about to free ctx).
 * 
 * run_timers() is called from the front end when it has reason to
 * think some timers have reached their moment, or when it simply
 * needs to know how int to wait next. We pass it the time we
 * think it is. It returns true and places the time when the next
 * timer needs to go off in `next', or alternatively it returns
 * false if there are no timers at all pending.
 * 
 * timer_change_cb() must be supplied by the front end; it
 * notifies the front end that a new timer has been added to the
 * list which is sooner than any existing ones. It provides the
 * time when that timer needs to go off.
 * 
 * *** FRONT END IMPLEMENTORS NOTE:
 * 
 * There's an important subtlety in the front-end implementation of
 * the timer interface. When a front end is given a `next' value,
 * either returned from run_timers() or via timer_change_cb(),
 * it should ensure that it really passes _that value_ as the `now'
 * parameter to its next run_timers call. It should _not_ simply
 * call GETTICKCOUNT() to get the `now' parameter when invoking
 * run_timers().
 * 
 * The reason for this is that an OS's system clock might not agree
 * exactly with the timing mechanisms it supplies to wait for a
 * given interval. I'll illustrate this by the simple example of
 * Unix Plink, which uses timeouts to select() in a way which for
 * these purposes can simply be considered to be a wait() function.
 * Suppose, for the sake of argument, that this wait() function
 * tends to return early by 1%. Then a possible sequence of actions
 * is:
 * 
 *  - run_timers() tells the front end that the next timer firing
 *    is 10000ms from now.
 *  - Front end calls wait(10000ms), but according to
 *    GETTICKCOUNT() it has only waited for 9900ms.
 *  - Front end calls run_timers() again, passing time T-100ms as
 *    `now'.
 *  - run_timers() does nothing, and says the next timer firing is
 *    still 100ms from now.
 *  - Front end calls wait(100ms), which only waits for 99ms.
 *  - Front end calls run_timers() yet again, passing time T-1ms.
 *  - run_timers() says there's still 1ms to wait.
 *  - Front end calls wait(1ms).
 * 
 * If you're _lucky_ at this point, wait(1ms) will actually wait
 * for 1ms and you'll only have woken the program up three times.
 * If you're unlucky, wait(1ms) might do nothing at all due to
 * being below some minimum threshold, and you might find your
 * program spends the whole of the last millisecond tight-looping
 * between wait() and run_timers().
 * 
 * Instead, what you should do is to _save_ the precise `next'
 * value provided by run_timers() or via timer_change_cb(), and
 * use that precise value as the input to the next run_timers()
 * call. So:
 * 
 *  - run_timers() tells the front end that the next timer firing
 *    is at time T, 10000ms from now.
 *  - Front end calls wait(10000ms).
 *  - Front end then immediately calls run_timers() and passes it
 *    time T, without stopping to check GETTICKCOUNT() at all.
 * 
 * This guarantees that the program wakes up only as many times as
 * there are actual timer actions to be taken, and that the timing
 * mechanism will never send it into a tight loop.
 * 
 * (It does also mean that the timer action in the above example
 * will occur 100ms early, but this is not generally critical. And
 * the hypothetical 1% error in wait() will be partially corrected
 * for anyway when, _after_ run_timers() returns, you call
 * GETTICKCOUNT() and compare the result with the returned `next'
 * value to find out how int you have to make your next wait().)
 */
typedef void (*timer_fn_t) (int now);
int schedule_timer(int ticks, timer_fn_t fn);
int run_timers(int now, int *next);
void timer_change_cb(int next);

#endif
