/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * gthread.c: posix thread system implementation
 * Copyright 1998 Sebastian Wilhelmi; University of Karlsruhe
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

/* The GMutex, GCond and GPrivate implementations in this file are some
 * of the lowest-level code in GLib.  All other parts of GLib (messages,
 * memory, slices, etc) assume that they can freely use these facilities
 * without risking recursion.
 *
 * As such, these functions are NOT permitted to call any other part of
 * GLib.
 *
 * The thread manipulation functions (create, exit, join, etc.) have
 * more freedom -- they can do as they please.
 */

#include "config.h"

#include "gthread.h"

#include "gthreadprivate.h"
#include "gslice.h"
#include "gmessages.h"
#include "gstrfuncs.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_SCHED_H
#include <sched.h>
#endif


static void
g_thread_abort (gint         status,
                const gchar *function)
{
  fprintf (stderr, "GLib (gthread-posix.c): Unexpected error from C library during '%s': %s.  Aborting.\n",
           strerror (status), function);
  abort ();
}

/* {{{1 GMutex */

/**
 * g_mutex_init:
 * @mutex: an uninitialized #GMutex
 *
 * Initializes a #GMutex so that it can be used.
 *
 * This function is useful to initialize a mutex that has been
 * allocated on the stack, or as part of a larger structure.
 * It is not necessary to initialize a mutex that has been
 * created with g_mutex_new(). Also see #G_MUTEX_INIT for an
 * alternative way to initialize statically allocated mutexes.
 *
 * |[
 *   typedef struct {
 *     GMutex m;
 *     ...
 *   } Blob;
 *
 * Blob *b;
 *
 * b = g_new (Blob, 1);
 * g_mutex_init (&b->m);
 * ]|
 *
 * To undo the effect of g_mutex_init() when a mutex is no longer
 * needed, use g_mutex_clear().
 *
 * Calling g_mutex_init() on an already initialized #GMutex leads
 * to undefined behaviour.
 *
 * Since: 2.32
 */
void
g_mutex_init (GMutex *mutex)
{
  gint status;
  pthread_mutexattr_t *pattr = NULL;
#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
  pthread_mutexattr_t attr;
  pthread_mutexattr_init (&attr);
  pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
  pattr = &attr;
#endif

  if G_UNLIKELY ((status = pthread_mutex_init (&mutex->impl, pattr)) != 0)
    g_thread_abort (status, "pthread_mutex_init");

#ifdef PTHREAD_ADAPTIVE_MUTEX_NP
  pthread_mutexattr_destroy (&attr);
#endif
}

/**
 * g_mutex_clear:
 * @mutex: an initialized #GMutex
 *
 * Frees the resources allocated to a mutex with g_mutex_init().
 *
 * #GMutexes that have have been created with g_mutex_new() should
 * be freed with g_mutex_free() instead.
 *
 * Calling g_mutex_clear() on a locked mutex leads to undefined
 * behaviour.
 *
 * Sine: 2.32
 */
void
g_mutex_clear (GMutex *mutex)
{
  gint status;

  if G_UNLIKELY ((status = pthread_mutex_destroy (&mutex->impl)) != 0)
    g_thread_abort (status, "pthread_mutex_destroy");
}

/**
 * g_mutex_lock:
 * @mutex: a #GMutex
 *
 * Locks @mutex. If @mutex is already locked by another thread, the
 * current thread will block until @mutex is unlocked by the other
 * thread.
 *
 * This function can be used even if g_thread_init() has not yet been
 * called, and, in that case, will do nothing.
 *
 * <note>#GMutex is neither guaranteed to be recursive nor to be
 * non-recursive, i.e. a thread could deadlock while calling
 * g_mutex_lock(), if it already has locked @mutex. Use
 * #GRecMutex if you need recursive mutexes.</note>
 */
void
g_mutex_lock (GMutex *mutex)
{
  gint status;

  if G_UNLIKELY ((status = pthread_mutex_lock (&mutex->impl)) != 0)
    g_thread_abort (status, "pthread_mutex_lock");
}

/**
 * g_mutex_unlock:
 * @mutex: a #GMutex
 *
 * Unlocks @mutex. If another thread is blocked in a g_mutex_lock()
 * call for @mutex, it will become unblocked and can lock @mutex itself.
 *
 * Calling g_mutex_unlock() on a mutex that is not locked by the
 * current thread leads to undefined behaviour.
 *
 * This function can be used even if g_thread_init() has not yet been
 * called, and, in that case, will do nothing.
 */
void
g_mutex_unlock (GMutex *mutex)
{
  gint status;

  if G_UNLIKELY ((status = pthread_mutex_unlock (&mutex->impl)) != 0)
    g_thread_abort (status, "pthread_mutex_lock");
}

/**
 * g_mutex_trylock:
 * @mutex: a #GMutex
 *
 * Tries to lock @mutex. If @mutex is already locked by another thread,
 * it immediately returns %FALSE. Otherwise it locks @mutex and returns
 * %TRUE.
 *
 * This function can be used even if g_thread_init() has not yet been
 * called, and, in that case, will immediately return %TRUE.
 *
 * <note>#GMutex is neither guaranteed to be recursive nor to be
 * non-recursive, i.e. the return value of g_mutex_trylock() could be
 * both %FALSE or %TRUE, if the current thread already has locked
 * @mutex. Use #GRecMutex if you need recursive mutexes.</note>

 * Returns: %TRUE if @mutex could be locked
 */
gboolean
g_mutex_trylock (GMutex *mutex)
{
  gint status;

  if G_LIKELY ((status = pthread_mutex_trylock (&mutex->impl)) == 0)
    return TRUE;

  if G_UNLIKELY (status != EBUSY)
    g_thread_abort (status, "pthread_mutex_trylock");

  return FALSE;
}

/* {{{1 GRecMutex */

static pthread_mutex_t *
g_rec_mutex_impl_new (void)
{
  pthread_mutexattr_t attr;
  pthread_mutex_t *mutex;

  mutex = g_slice_new (pthread_mutex_t);
  pthread_mutexattr_init (&attr);
  pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init (mutex, &attr);
  pthread_mutexattr_destroy (&attr);

  return mutex;
}

static void
g_rec_mutex_impl_free (pthread_mutex_t *mutex)
{
  pthread_mutex_destroy (mutex);
  g_slice_free (pthread_mutex_t, mutex);
}

static pthread_mutex_t *
g_rec_mutex_get_impl (GRecMutex *mutex)
{
  pthread_mutex_t *impl = mutex->impl;

  if G_UNLIKELY (mutex->impl == NULL)
    {
      impl = g_rec_mutex_impl_new ();
      if (!g_atomic_pointer_compare_and_exchange (&mutex->impl, NULL, impl))
        g_rec_mutex_impl_free (impl);
      impl = mutex->impl;
    }

  return impl;
}

/**
 * g_rec_mutex_init:
 * @rec_mutex: an uninitialized #GRecMutex
 *
 * Initializes a #GRecMutex so that it can be used.
 *
 * This function is useful to initialize a recursive mutex
 * that has been allocated on the stack, or as part of a larger
 * structure.
 * It is not necessary to initialize a recursive mutex that has
 * been created with g_rec_mutex_new(). Also see #G_REC_MUTEX_INIT
 * for an alternative way to initialize statically allocated
 * recursive mutexes.
 *
 * |[
 *   typedef struct {
 *     GRecMutex m;
 *     ...
 *   } Blob;
 *
 * Blob *b;
 *
 * b = g_new (Blob, 1);
 * g_rec_mutex_init (&b->m);
 * ]|
 *
 * Calling g_rec_mutex_init() on an already initialized #GRecMutex
 * leads to undefined behaviour.
 *
 * To undo the effect of g_rec_mutex_init() when a recursive mutex
 * is no longer needed, use g_rec_mutex_clear().
 *
 * Since: 2.32
 */
void
g_rec_mutex_init (GRecMutex *rec_mutex)
{
  rec_mutex->impl = g_rec_mutex_impl_new ();
}

/**
 * g_rec_mutex_clear:
 * @rec_mutex: an initialized #GRecMutex
 *
 * Frees the resources allocated to a recursive mutex with
 * g_rec_mutex_init().
 *
 * #GRecMutexes that have have been created with g_rec_mutex_new()
 * should be freed with g_rec_mutex_free() instead.
 *
 * Calling g_rec_mutex_clear() on a locked recursive mutex leads
 * to undefined behaviour.
 *
 * Sine: 2.32
 */
void
g_rec_mutex_clear (GRecMutex *rec_mutex)
{
  if (rec_mutex->impl)
    g_rec_mutex_impl_free (rec_mutex->impl);
}

/**
 * g_rec_mutex_lock:
 * @rec_mutex: a #GRecMutex
 *
 * Locks @rec_mutex. If @rec_mutex is already locked by another
 * thread, the current thread will block until @rec_mutex is
 * unlocked by the other thread. If @rec_mutex is already locked
 * by the current thread, the 'lock count' of @rec_mutex is increased.
 * The mutex will only become available again when it is unlocked
 * as many times as it has been locked.
 *
 * Since: 2.32
 */
void
g_rec_mutex_lock (GRecMutex *mutex)
{
  pthread_mutex_lock (g_rec_mutex_get_impl (mutex));
}

/**
 * g_rec_mutex_unlock:
 * @rec_mutex: a #RecGMutex
 *
 * Unlocks @rec_mutex. If another thread is blocked in a
 * g_rec_mutex_lock() call for @rec_mutex, it will become unblocked
 * and can lock @rec_mutex itself.
 *
 * Calling g_rec_mutex_unlock() on a recursive mutex that is not
 * locked by the current thread leads to undefined behaviour.
 *
 * Since: 2.32
 */
void
g_rec_mutex_unlock (GRecMutex *rec_mutex)
{
  pthread_mutex_unlock (rec_mutex->impl);
}

/**
 * g_rec_mutex_trylock:
 * @rec_mutex: a #GRecMutex
 *
 * Tries to lock @rec_mutex. If @rec_mutex is already locked
 * by another thread, it immediately returns %FALSE. Otherwise
 * it locks @rec_mutex and returns %TRUE.
 *
 * Returns: %TRUE if @rec_mutex could be locked
 *
 * Since: 2.32
 */
gboolean
g_rec_mutex_trylock (GRecMutex *rec_mutex)
{
  if (pthread_mutex_trylock (g_rec_mutex_get_impl (rec_mutex)) != 0)
    return FALSE;

  return TRUE;
}

/* {{{1 GRWLock */

/**
 * g_rw_lock_init:
 * @lock: an uninitialized #GRWLock
 *
 * Initializes a #GRWLock so that it can be used.
 *
 * This function is useful to initialize a lock that has been
 * allocated on the stack, or as part of a larger structure.
 * Also see #G_RW_LOCK_INIT for an alternative way to initialize
 * statically allocated locks.
 *
 * |[
 *   typedef struct {
 *     GRWLock l;
 *     ...
 *   } Blob;
 *
 * Blob *b;
 *
 * b = g_new (Blob, 1);
 * g_rw_lock_init (&b->l);
 * ]|
 *
 * To undo the effect of g_rw_lock_init() when a lock is no longer
 * needed, use g_rw_lock_clear().
 *
 * Calling g_rw_lock_init() on an already initialized #GRWLock leads
 * to undefined behaviour.
 *
 * Since: 2.32
 */
void
g_rw_lock_init (GRWLock *lock)
{
  pthread_rwlock_init (&lock->impl, NULL);
}

/**
 * g_rw_lock_clear:
 * @lock: an initialized #GRWLock
 *
 * Frees the resources allocated to a lock with g_rw_lock_init().
 *
 * Calling g_rw_lock_clear() when any thread holds the lock
 * leads to undefined behaviour.
 *
 * Sine: 2.32
 */
void
g_rw_lock_clear (GRWLock *lock)
{
  pthread_rwlock_destroy (&lock->impl);
}

/**
 * g_rw_lock_writer_lock:
 * @lock: a #GRWLock
 *
 * Obtain a write lock on @lock. If any thread already holds
 * a read or write lock on @lock, the current thread will block
 * until all other threads have dropped their locks on @lock.
 *
 * Since: 2.32
 */
void
g_rw_lock_writer_lock (GRWLock *lock)
{
  pthread_rwlock_wrlock (&lock->impl);
}

/**
 * g_rw_lock_writer_trylock:
 * @lock: a #GRWLock
 *
 * Tries to obtain a write lock on @lock. If any other thread holds
 * a read or write lock on @lock, it immediately returns %FALSE.
 * Otherwise it locks @lock and returns %TRUE.
 *
 * Returns: %TRUE if @lock could be locked
 *
 * Since: 2.32
 */
gboolean
g_rw_lock_writer_trylock (GRWLock *lock)
{
  if (pthread_rwlock_trywrlock (&lock->impl) != 0)
    return FALSE;

  return TRUE;
}

/**
 * g_rw_lock_writer_unlock:
 * @lock: a #GRWLock
 *
 * Release a write lock on @lock.
 *
 * Calling g_rw_lock_writer_unlock() on a lock that is not held
 * by the current thread leads to undefined behaviour.
 *
 * Since: 2.32
 */
void
g_rw_lock_writer_unlock (GRWLock *lock)
{
  pthread_rwlock_unlock (&lock->impl);
}

/**
 * g_rw_lock_reader_lock:
 * @lock: a #GRWLock
 *
 * Obtain a read lock on @lock. If another thread currently holds
 * the write lock on @lock or blocks waiting for it, the current
 * thread will block. Read locks can be taken recursively.
 *
 * It is implementation-defined how many threads are allowed to
 * hold read locks on the same lock simultaneously.
 *
 * Since: 2.32
 */
void
g_rw_lock_reader_lock (GRWLock *lock)
{
  pthread_rwlock_rdlock (&lock->impl);
}

/**
 * g_rw_lock_reader_trylock:
 * @lock: a #GRWLock
 *
 * Tries to obtain a read lock on @lock and returns %TRUE if
 * the read lock was successfully obtained. Otherwise it
 * returns %FALSE.
 *
 * Returns: %TRUE if @lock could be locked
 *
 * Since: 2.32
 */
gboolean
g_rw_lock_reader_trylock (GRWLock *lock)
{
  if (pthread_rwlock_tryrdlock (&lock->impl) != 0)
    return FALSE;

  return TRUE;
}

/**
 * g_rw_lock_reader_unlock:
 * @lock: a #GRWLock
 *
 * Release a read lock on @lock.
 *
 * Calling g_rw_lock_reader_unlock() on a lock that is not held
 * by the current thread leads to undefined behaviour.
 *
 * Since: 2.32
 */
void
g_rw_lock_reader_unlock (GRWLock *lock)
{
  pthread_rwlock_unlock (&lock->impl);
}

/* {{{1 GCond */

/**
 * g_cond_init:
 * @cond: an uninitialized #GCond
 *
 * Initialized a #GCond so that it can be used.
 *
 * This function is useful to initialize a #GCond that has been
 * allocated on the stack, or as part of a larger structure.
 * It is not necessary to initialize a #GCond that has been
 * created with g_cond_new(). Also see #G_COND_INIT for an
 * alternative way to initialize statically allocated #GConds.
 *
 * To undo the effect of g_cond_init() when a #GCond is no longer
 * needed, use g_cond_clear().
 *
 * Calling g_cond_init() on an already initialized #GCond leads
 * to undefined behaviour.
 *
 * Since: 2.32
 */
void
g_cond_init (GCond *cond)
{
  gint status;

  if G_UNLIKELY ((status = pthread_cond_init (&cond->impl, NULL)) != 0)
    g_thread_abort (status, "pthread_cond_init");
}

/**
 * g_cond_clear:
 * @cond: an initialized #GCond
 *
 * Frees the resources allocated to a #GCond with g_cond_init().
 *
 * #GConds that have been created with g_cond_new() should
 * be freed with g_cond_free() instead.
 *
 * Calling g_cond_clear() for a #GCond on which threads are
 * blocking leads to undefined behaviour.
 *
 * Since: 2.32
 */
void
g_cond_clear (GCond *cond)
{
  gint status;

  if G_UNLIKELY ((status = pthread_cond_destroy (&cond->impl)) != 0)
    g_thread_abort (status, "pthread_cond_destroy");
}

/**
 * g_cond_wait:
 * @cond: a #GCond
 * @mutex: a #GMutex that is currently locked
 *
 * Waits until this thread is woken up on @cond. The @mutex is unlocked
 * before falling asleep and locked again before resuming.
 *
 * This function can be used even if g_thread_init() has not yet been
 * called, and, in that case, will immediately return.
 */
void
g_cond_wait (GCond  *cond,
             GMutex *mutex)
{
  gint status;

  if G_UNLIKELY ((status = pthread_cond_wait (&cond->impl, &mutex->impl)) != 0)
    g_thread_abort (status, "pthread_cond_wait");
}

/**
 * g_cond_signal:
 * @cond: a #GCond
 *
 * If threads are waiting for @cond, at least one of them is unblocked.
 * If no threads are waiting for @cond, this function has no effect.
 * It is good practice to hold the same lock as the waiting thread
 * while calling this function, though not required.
 *
 * This function can be used even if g_thread_init() has not yet been
 * called, and, in that case, will do nothing.
 */
void
g_cond_signal (GCond *cond)
{
  gint status;

  if G_UNLIKELY ((status = pthread_cond_signal (&cond->impl)) != 0)
    g_thread_abort (status, "pthread_cond_signal");
}

/**
 * g_cond_broadcast:
 * @cond: a #GCond
 *
 * If threads are waiting for @cond, all of them are unblocked.
 * If no threads are waiting for @cond, this function has no effect.
 * It is good practice to lock the same mutex as the waiting threads
 * while calling this function, though not required.
 *
 * This function can be used even if g_thread_init() has not yet been
 * called, and, in that case, will do nothing.
 */
void
g_cond_broadcast (GCond *cond)
{
  gint status;

  if G_UNLIKELY ((status = pthread_cond_broadcast (&cond->impl)) != 0)
    g_thread_abort (status, "pthread_cond_broadcast");
}

/**
 * g_cond_timed_wait:
 * @cond: a #GCond
 * @mutex: a #GMutex that is currently locked
 * @abs_time: a #GTimeVal, determining the final time
 *
 * Waits until this thread is woken up on @cond, but not longer than
 * until the time specified by @abs_time. The @mutex is unlocked before
 * falling asleep and locked again before resuming.
 *
 * If @abs_time is %NULL, g_cond_timed_wait() acts like g_cond_wait().
 *
 * This function can be used even if g_thread_init() has not yet been
 * called, and, in that case, will immediately return %TRUE.
 *
 * To easily calculate @abs_time a combination of g_get_current_time()
 * and g_time_val_add() can be used.
 *
 * Returns: %TRUE if @cond was signalled, or %FALSE on timeout
 */
gboolean
g_cond_timed_wait (GCond    *cond,
                   GMutex   *mutex,
                   GTimeVal *abs_time)
{
  struct timespec end_time;
  gint status;

  if (abs_time == NULL)
    {
      g_cond_wait (cond, mutex);
      return TRUE;
    }

  end_time.tv_sec = abs_time->tv_sec;
  end_time.tv_nsec = abs_time->tv_usec * 1000;

  if ((status = pthread_cond_timedwait (&cond->impl, &mutex->impl, &end_time)) == 0)
    return TRUE;

  if G_UNLIKELY (status != ETIMEDOUT)
    g_thread_abort (status, "pthread_cond_timedwait");

  return FALSE;
}

/**
 * g_cond_timedwait:
 * @cond: a #GCond
 * @mutex: a #GMutex that is currently locked
 * @abs_time: the final time, in microseconds
 *
 * A variant of g_cond_timed_wait() that takes @abs_time
 * as a #gint64 instead of a #GTimeVal.
 * See g_cond_timed_wait() for details.
 *
 * Returns: %TRUE if @cond was signalled, or %FALSE on timeout
 *
 * Since: 2.32
 */
gboolean
g_cond_timedwait (GCond  *cond,
                  GMutex *mutex,
                  gint64  abs_time)
{
  struct timespec end_time;
  gint status;

  end_time.tv_sec = abs_time / 1000000;
  end_time.tv_nsec = (abs_time % 1000000) * 1000;

  if ((status = pthread_cond_timedwait (&cond->impl, &mutex->impl, &end_time)) == 0)
    return TRUE;

  if G_UNLIKELY (status != ETIMEDOUT)
    g_thread_abort (status, "pthread_cond_timedwait");

  return FALSE;
}

/* {{{1 GPrivate */

void
g_private_init (GPrivate       *key,
                GDestroyNotify  notify)
{
  pthread_key_create (&key->key, notify);
  key->ready = TRUE;
}

/**
 * g_private_get:
 * @private_key: a #GPrivate
 *
 * Returns the pointer keyed to @private_key for the current thread. If
 * g_private_set() hasn't been called for the current @private_key and
 * thread yet, this pointer will be %NULL.
 *
 * This function can be used even if g_thread_init() has not yet been
 * called, and, in that case, will return the value of @private_key
 * casted to #gpointer. Note however, that private data set
 * <emphasis>before</emphasis> g_thread_init() will
 * <emphasis>not</emphasis> be retained <emphasis>after</emphasis> the
 * call. Instead, %NULL will be returned in all threads directly after
 * g_thread_init(), regardless of any g_private_set() calls issued
 * before threading system initialization.
 *
 * Returns: the corresponding pointer
 */
gpointer
g_private_get (GPrivate *key)
{
  if (!key->ready)
    return key->single_value;

  /* quote POSIX: No errors are returned from pthread_getspecific(). */
  return pthread_getspecific (key->key);
}

/**
 * g_private_set:
 * @private_key: a #GPrivate
 * @data: the new pointer
 *
 * Sets the pointer keyed to @private_key for the current thread.
 *
 * This function can be used even if g_thread_init() has not yet been
 * called, and, in that case, will set @private_key to @data casted to
 * #GPrivate*. See g_private_get() for resulting caveats.
 */
void
g_private_set (GPrivate *key,
               gpointer  value)
{
  gint status;

  if (!key->ready)
    {
      key->single_value = value;
      return;
    }

  if G_UNLIKELY ((status = pthread_setspecific (key->key, value)) != 0)
    g_thread_abort (status, "pthread_setspecific");
}

/* {{{1 GThread */

#define posix_check_err(err, name) G_STMT_START{			\
  int error = (err); 							\
  if (error)	 		 		 			\
    g_error ("file %s: line %d (%s): error '%s' during '%s'",		\
           __FILE__, __LINE__, G_STRFUNC,				\
           g_strerror (error), name);					\
  }G_STMT_END

#define posix_check_cmd(cmd) posix_check_err (cmd, #cmd)

void
g_system_thread_create (GThreadFunc       thread_func,
                        gpointer          arg,
                        gulong            stack_size,
                        gboolean          joinable,
                        gpointer          thread,
                        GError          **error)
{
  pthread_attr_t attr;
  gint ret;

  g_return_if_fail (thread_func);

  posix_check_cmd (pthread_attr_init (&attr));

#ifdef HAVE_PTHREAD_ATTR_SETSTACKSIZE
  if (stack_size)
    {
#ifdef _SC_THREAD_STACK_MIN
      stack_size = MAX (sysconf (_SC_THREAD_STACK_MIN), stack_size);
#endif /* _SC_THREAD_STACK_MIN */
      /* No error check here, because some systems can't do it and
       * we simply don't want threads to fail because of that. */
      pthread_attr_setstacksize (&attr, stack_size);
    }
#endif /* HAVE_PTHREAD_ATTR_SETSTACKSIZE */

  posix_check_cmd (pthread_attr_setdetachstate (&attr,
          joinable ? PTHREAD_CREATE_JOINABLE : PTHREAD_CREATE_DETACHED));

  ret = pthread_create (thread, &attr, (void* (*)(void*))thread_func, arg);

  posix_check_cmd (pthread_attr_destroy (&attr));

  if (ret == EAGAIN)
    {
      g_set_error (error, G_THREAD_ERROR, G_THREAD_ERROR_AGAIN, 
		   "Error creating thread: %s", g_strerror (ret));
      return;
    }

  posix_check_err (ret, "pthread_create");
}

/**
 * g_thread_yield:
 *
 * Gives way to other threads waiting to be scheduled.
 *
 * This function is often used as a method to make busy wait less evil.
 * But in most cases you will encounter, there are better methods to do
 * that. So in general you shouldn't use this function.
 */
void
g_thread_yield (void)
{
  sched_yield ();
}

void
g_system_thread_join (gpointer thread)
{
  gpointer ignore;
  posix_check_cmd (pthread_join (*(pthread_t*)thread, &ignore));
}

void
g_system_thread_exit (void)
{
  pthread_exit (NULL);
}

void
g_system_thread_self (gpointer thread)
{
  *(pthread_t*)thread = pthread_self();
}

gboolean
g_system_thread_equal (gpointer thread1,
                       gpointer thread2)
{
  return (pthread_equal (*(pthread_t*)thread1, *(pthread_t*)thread2) != 0);
}

/* {{{1 Epilogue */
/* vim:set foldmethod=marker: */