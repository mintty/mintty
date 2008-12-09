#include "timing.h"

#include "tree234.h"
#include "platform.h"

/*
 * timing.c
 * 
 * This module tracks any timers set up by schedule_timer(). It
 * keeps all the currently active timers in a list; it informs the
 * front end of when the next timer is due to go off if that
 * changes; and, very importantly, it tracks the context pointers
 * passed to schedule_timer(), so that if a context is freed all
 * the timers associated with it can be immediately annulled.
 */

/*
 * On some versions of Windows, it has been known for WM_TIMER to
 * occasionally get its callback time simply wrong, and call us
 * back several minutes early. Defining these symbols enables
 * compensation code in timing.c.
 */
#define TIMING_SYNC
#define TIMING_SYNC_TICKCOUNT

struct timer {
  timer_fn_t fn;
  int now;
};

static tree234 *timers = null;
static int now = 0L;

static int
compare_timers(void *av, void *bv)
{
  struct timer *a = (struct timer *) av;
  struct timer *b = (struct timer *) bv;
  int at = a->now - now;
  int bt = b->now - now;

  if (at < bt)
    return -1;
  else if (at > bt)
    return +1;

 /*
  * Failing that, compare on the other two fields, just so that
  * we don't get unwanted equality.
  */
  {
    int c = memcmp(&a->fn, &b->fn, sizeof (a->fn));
    if (c < 0)
      return -1;
    else if (c > 0)
      return +1;
  }

 /*
  * Failing _that_, the two entries genuinely are equal, and we
  * never have a need to store them separately in the tree.
  */
  return 0;
}

static void
init_timers(void)
{
  if (!timers) {
    timers = newtree234(compare_timers);
    now = get_tick_count();
  }
}

int
schedule_timer(int ticks, timer_fn_t fn)
{
  int time;
  struct timer *t, *first;

  init_timers();

  time = ticks + get_tick_count();

 /*
  * Just in case our various defences against timing skew fail
  * us: if we try to schedule a timer that's already in the
  * past, we instead schedule it for the immediate future.
  */
  if (time - now <= 0)
    time = now + 1;

  t = new(struct timer);
  t->fn = fn;
  t->now = time;

  if (t != add234(timers, t)) {
    free(t);    /* identical timer already exists */
  }

  first = (struct timer *) index234(timers, 0);
  if (first == t) {
   /*
    * This timer is the very first on the list, so we must
    * notify the front end.
    */
    timer_change_cb(first->now);
  }

  return time;
}

/*
 * Call to run any timers whose time has reached the present.
 * Returns the time (in ticks) expected until the next timer after
 * that triggers.
 */
int
run_timers(int anow, int *next)
{
  struct timer *first;

  init_timers();

#ifdef TIMING_SYNC
 /*
  * In this ifdef I put some code which deals with the
  * possibility that `anow' disagrees with GETTICKCOUNT by a
  * significant margin. Our strategy for dealing with it differs
  * depending on platform, because on some platforms
  * GETTICKCOUNT is more likely to be right whereas on others
  * `anow' is a better gold standard.
  */
  {
    int tnow = get_tick_count();

    if (tnow + ticks_per_sec / 50 - anow < 0 ||
        anow + ticks_per_sec / 50 - tnow < 0) {
#if defined TIMING_SYNC_ANOW
     /*
      * If anow is accurate and the tick count is wrong,
      * this is likely to be because the tick count is
      * derived from the system clock which has changed (as
      * can occur on Unix). Therefore, we resolve this by
      * inventing an offset which is used to adjust all
      * future output from GETTICKCOUNT.
      * 
      * A platform which defines TIMING_SYNC_ANOW is
      * expected to have also defined this offset variable.
      * Therefore we can simply reference it here and assume
      * that it will exist.
      */
      tickcount_offset += anow - tnow;
#elif defined TIMING_SYNC_TICKCOUNT
     /*
      * If the tick count is more likely to be accurate, we
      * simply use that as our time value, which may mean we
      * run no timers in this call (because we got called
      * early), or alternatively it may mean we run lots of
      * timers in a hurry because we were called late.
      */
      anow = tnow;
#else
     /*
      * Any platform which defines TIMING_SYNC must also define one of the two
      * auxiliary symbols TIMING_SYNC_ANOW and TIMING_SYNC_TICKCOUNT, to
      * indicate which measurement to trust when the two disagree.
      */
#error TIMING_SYNC definition incomplete
#endif
    }
  }
#endif

  now = anow;

  while (1) {
    first = (struct timer *) index234(timers, 0);

    if (!first)
      return false;     /* no timers remaining */

    if (first->now - now <= 0) {
     /*
      * This timer is active and has reached its running
      * time. Run it.
      */
      delpos234(timers, 0);
      first->fn(first->now);
      free(first);
    }
    else {
     /*
      * This is the first still-active timer that is in the
      * future. Return how long it has yet to go.
      */
      *next = first->now;
      return true;
    }
  }
}
