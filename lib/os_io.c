/* File: "os_io.c", Time-stamp: <2007-09-11 23:51:31 feeley> */

/* Copyright (c) 1994-2007 by Marc Feeley, All Rights Reserved. */

/*
 * This module implements the operating system specific routines
 * related to I/O.
 */

#define ___INCLUDED_FROM_OS_IO
#define ___VERSION 400001
#include "gambit.h"

#include "os_base.h"
#include "os_io.h"
#include "os_tty.h"
#include "os_files.h"
#include "setup.h"
#include "c_intf.h"

/*---------------------------------------------------------------------------*/


___io_module ___io_mod =
{
  0

#ifdef ___IO_MODULE_INIT
  ___IO_MODULE_INIT
#endif
};


/*---------------------------------------------------------------------------*/

/* Device groups. */


___SCMOBJ ___device_group_setup
   ___P((___device_group **dgroup),
        (dgroup)
___device_group **dgroup;)
{
  ___SCMOBJ e;
  ___device_group *g;

  g = ___CAST(___device_group*,
              ___alloc_mem (sizeof (___device_group)));

  if (g == NULL)
    return ___FIX(___HEAP_OVERFLOW_ERR);

  g->list = NULL;

  *dgroup = g;

  return ___FIX(___NO_ERR);
}


void ___device_group_cleanup
   ___P((___device_group *dgroup),
        (dgroup)
___device_group *dgroup;)
{
  while (dgroup->list != NULL)
    if (___device_cleanup (dgroup->list) != ___FIX(___NO_ERR))
      break;

  ___free_mem (dgroup);
}


/*---------------------------------------------------------------------------*/

/* Nonblocking pipes */

#ifdef USE_PUMPS

___HIDDEN ___SCMOBJ ___nonblocking_pipe_setup
   ___P((___nonblocking_pipe *pipe,
         int size),
        (pipe,
         size)
___nonblocking_pipe *pipe;
int size;)
{
  ___U8 *buffer;
  HANDLE mutex;
  HANDLE revent;
  HANDLE wevent;

  buffer = ___CAST(___U8*,
                   ___alloc_mem (size));

  if (buffer == NULL)
    return ___FIX(___HEAP_OVERFLOW_ERR);

  mutex = CreateMutex (NULL,  /* can't inherit */
                       FALSE, /* unlocked */
                       NULL); /* no name */

  if (mutex == NULL)
    {
      ___SCMOBJ e = err_code_from_GetLastError ();
      ___free_mem (buffer);
      return e;
    }

  revent = CreateEvent (NULL,  /* can't inherit */
                        TRUE,  /* manual reset */
                        FALSE, /* not signaled */
                        NULL); /* no name */

  if (revent == NULL)
    {
      ___SCMOBJ e = err_code_from_GetLastError ();
      CloseHandle (mutex); /* ignore error */
      ___free_mem (buffer);
      return e;
    }

  wevent = CreateEvent (NULL,  /* can't inherit */
                        TRUE,  /* manual reset */
                        FALSE, /* not signaled */
                        NULL); /* no name */

  if (wevent == NULL)
    {
      ___SCMOBJ e = err_code_from_GetLastError ();
      CloseHandle (revent); /* ignore error */
      CloseHandle (mutex); /* ignore error */
      ___free_mem (buffer);
      return e;
    }

  pipe->mutex = mutex;
  pipe->revent = revent;
  pipe->wevent = wevent;
  pipe->rerr = ___FIX(___NO_ERR);
  pipe->werr = ___FIX(___NO_ERR);
  pipe->oob = 0;
  pipe->rd = 0;
  pipe->wr = 0;
  pipe->size = size;
  pipe->buffer = buffer;

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___nonblocking_pipe_cleanup
   ___P((___nonblocking_pipe *pipe),
        (pipe)
___nonblocking_pipe *pipe;)
{
  CloseHandle (pipe->wevent); /* ignore error */
  CloseHandle (pipe->revent); /* ignore error */
  CloseHandle (pipe->mutex); /* ignore error */
  ___free_mem (pipe->buffer);

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___nonblocking_pipe_set_reader_err
   ___P((___nonblocking_pipe *pipe,
         ___SCMOBJ err),
        (pipe,
         err)
___nonblocking_pipe *pipe;
___SCMOBJ err;)
{
  if (WaitForSingleObject (pipe->mutex, INFINITE) == WAIT_FAILED)
    return err_code_from_GetLastError ();

  /* note: the reader error indicator may get overwritten */

  pipe->rerr = err;

  SetEvent (pipe->wevent); /* ignore error */

  if (pipe->werr == ___FIX(___NO_ERR))
    ResetEvent (pipe->revent); /* ignore error */

  ReleaseMutex (pipe->mutex); /* ignore error */

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___nonblocking_pipe_set_writer_err
   ___P((___nonblocking_pipe *pipe,
         ___SCMOBJ err),
        (pipe,
         err)
___nonblocking_pipe *pipe;
___SCMOBJ err;)
{
  if (WaitForSingleObject (pipe->mutex, INFINITE) == WAIT_FAILED)
    return err_code_from_GetLastError ();

  /* note: the writer error indicator may get overwritten */

  pipe->werr = err;

  SetEvent (pipe->revent); /* ignore error */

  if (pipe->rerr == ___FIX(___NO_ERR))
    ResetEvent (pipe->wevent); /* ignore error */

  ReleaseMutex (pipe->mutex); /* ignore error */

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___nonblocking_pipe_write_oob
   ___P((___nonblocking_pipe *pipe,
         ___nonblocking_pipe_oob_msg *oob_msg),
        (pipe,
         oob_msg)
___nonblocking_pipe *pipe;
___nonblocking_pipe_oob_msg *oob_msg;)
{
  if (WaitForSingleObject (pipe->mutex, INFINITE) == WAIT_FAILED)
    return err_code_from_GetLastError ();

  if (pipe->werr != ___FIX(___NO_ERR) || pipe->oob)
    {
      ReleaseMutex (pipe->mutex); /* ignore error */
      return ___ERR_CODE_EAGAIN;
    }

  pipe->oob = 1;
  pipe->oob_msg = *oob_msg;

  if (pipe->rerr == ___FIX(___NO_ERR))
    {
      SetEvent (pipe->revent); /* ignore error */
      ResetEvent (pipe->wevent); /* ignore error */
    }

  ReleaseMutex (pipe->mutex); /* ignore error */

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___nonblocking_pipe_read_ready_wait
   ___P((___nonblocking_pipe *pipe),
        (pipe)
___nonblocking_pipe *pipe;)
{
  ___SCMOBJ werr;

  if (WaitForSingleObject (pipe->revent, INFINITE) == WAIT_FAILED ||
      WaitForSingleObject (pipe->mutex, INFINITE) == WAIT_FAILED)
    return err_code_from_GetLastError ();

  werr = pipe->werr;

  if (werr != ___FIX(___NO_ERR))
    {
      pipe->werr = ___FIX(___NO_ERR);

      if (pipe->rerr != ___FIX(___NO_ERR) ||
          (pipe->rd == pipe->wr && !pipe->oob))
        ResetEvent (pipe->revent); /* ignore error */
    }

  ReleaseMutex (pipe->mutex); /* ignore error */

  return werr;
}

___HIDDEN ___SCMOBJ ___nonblocking_pipe_write_ready_wait
   ___P((___nonblocking_pipe *pipe),
        (pipe)
___nonblocking_pipe *pipe;)
{
  ___SCMOBJ rerr;

  if (WaitForSingleObject (pipe->wevent, INFINITE) == WAIT_FAILED ||
      WaitForSingleObject (pipe->mutex, INFINITE) == WAIT_FAILED)
    return err_code_from_GetLastError ();

  rerr = pipe->rerr;

  if (rerr != ___FIX(___NO_ERR))
    {
      pipe->rerr = ___FIX(___NO_ERR);

      ResetEvent (pipe->wevent); /* ignore error */
    }

  ReleaseMutex (pipe->mutex); /* ignore error */

  return rerr;
}

___HIDDEN ___SCMOBJ ___nonblocking_pipe_read
   ___P((___nonblocking_pipe *pipe,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done,
         ___nonblocking_pipe_oob_msg *oob_msg),
        (pipe,
         buf,
         len,
         len_done,
         oob_msg)
___nonblocking_pipe *pipe;
___U8 *buf;
___stream_index len;
___stream_index *len_done;
___nonblocking_pipe_oob_msg *oob_msg;)
{
  ___SCMOBJ rerr;
  ___SCMOBJ werr;
  DWORD rd;
  DWORD wr;
  ___U8 *p;
  DWORD end;
  DWORD n;
  DWORD i;

  if (len <= 0)
    return ___FIX(___UNKNOWN_ERR);

  if (WaitForSingleObject (pipe->mutex, INFINITE) == WAIT_FAILED)
    return err_code_from_GetLastError ();

  rerr = pipe->rerr;
  werr = pipe->werr;
  rd = pipe->rd;
  wr = pipe->wr;

  if (rerr != ___FIX(___NO_ERR))
    {
      /* there is a reader error */

      if (werr == ___FIX(___NO_ERR))
        werr = ___ERR_CODE_EAGAIN;
      else
        {
          pipe->werr = ___FIX(___NO_ERR);
          ResetEvent (pipe->revent); /* ignore error */
        }
      ReleaseMutex (pipe->mutex); /* ignore error */
      return werr;
    }

  /* there is no reader error */

  if (rd == wr)
    {
      /* no bytes in FIFO buffer */

      if (pipe->oob)
        {
          /* out-of-band present */

          *oob_msg = pipe->oob_msg;
          pipe->oob = 0;
          if (werr == ___FIX(___NO_ERR))
            {
              ResetEvent (pipe->revent); /* ignore error */
#if 0
              /******************zzzzzzzzzzzzzz****/
              SetEvent (pipe->wevent); /* ignore error */
#endif
            }
          ReleaseMutex (pipe->mutex); /* ignore error */
          *len_done = 0;
          return ___FIX(___NO_ERR);
        }

      /* out-of-band not present */

      if (werr != ___FIX(___NO_ERR))
        {
          /* there is a writer error */

          pipe->werr = ___FIX(___NO_ERR);

          ResetEvent (pipe->revent); /* ignore error */
          ReleaseMutex (pipe->mutex); /* ignore error */
          return werr;
        }

      /* there is no writer error */

      SetEvent (pipe->wevent); /* ignore error */
      ReleaseMutex (pipe->mutex); /* ignore error */
      return ___ERR_CODE_EAGAIN;
    }

  /* at least one byte in FIFO buffer */

  end = pipe->size - rd; /* number of bytes from rd to end */

  n = wr + end; /* number of bytes available */
  if (n >= pipe->size)
    n -= pipe->size;

  if (n > ___CAST(DWORD,len)) /* don't transfer more than len */
    n = len;

  *len_done = n;

  p = pipe->buffer + rd; /* prepare transfer source */

  rd = rd + n;
  if (rd >= pipe->size)
    rd -= pipe->size;

  pipe->rd = rd;

  if (werr == ___FIX(___NO_ERR) && !pipe->oob)
    {
      /* there is no writer error and out-of-band not present */

      if (rd == wr) /* the FIFO will be empty? */
        ResetEvent (pipe->revent); /* ignore error */

      /* the FIFO will not be full */

      SetEvent (pipe->wevent); /* ignore error */
    }

  if (n <= end)
    {
      /* only need to transfer n bytes starting from original rd */

      for (i=n; i>0; i--)
        *buf++ = *p++;
    }
  else
    {
      /* need to transfer end bytes starting from original rd */

      for (i=end; i>0; i--)
        *buf++ = *p++;

      /* and to transfer n-end bytes starting from 0 */

      p = pipe->buffer;

      for (i=n-end; i>0; i--)
        *buf++ = *p++;
    }

  ReleaseMutex (pipe->mutex); /* ignore error */

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___nonblocking_pipe_write
   ___P((___nonblocking_pipe *pipe,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done),
        (pipe,
         buf,
         len,
         len_done)
___nonblocking_pipe *pipe;
___U8 *buf;
___stream_index len;
___stream_index *len_done;)
{
  ___SCMOBJ rerr;
  ___SCMOBJ werr;
  DWORD rd;
  DWORD wr;
  ___U8 *p;
  DWORD end;
  DWORD n;
  DWORD i;

  if (len <= 0)
    return ___FIX(___UNKNOWN_ERR);

  if (WaitForSingleObject (pipe->mutex, INFINITE) == WAIT_FAILED)
    return err_code_from_GetLastError ();

  rerr = pipe->rerr;
  werr = pipe->werr;
  rd = pipe->rd;
  wr = pipe->wr;

  if (werr != ___FIX(___NO_ERR))
    {
      /* there is a writer error */

      if (rerr == ___FIX(___NO_ERR))
        rerr = ___ERR_CODE_EAGAIN;
      else
        {
          pipe->rerr = ___FIX(___NO_ERR);
          ResetEvent (pipe->wevent); /* ignore error */
        }
      ReleaseMutex (pipe->mutex); /* ignore error */
      return rerr;
    }

  /* there is no writer error */

  if (rerr != ___FIX(___NO_ERR))
    {
      /* there is a reader error */

      pipe->rerr = ___FIX(___NO_ERR);

      if (rd != wr || pipe->oob) /* FIFO is not empty */
        SetEvent (pipe->revent); /* ignore error */

      ResetEvent (pipe->wevent); /* ignore error */
      ReleaseMutex (pipe->mutex); /* ignore error */
      return rerr;
    }

  /* there is no reader error */

  if (wr + 1 - rd == 0 || wr + 1 - rd == pipe->size || pipe->oob)
    {
      /* FIFO buffer is full or out-of-band present */

      ReleaseMutex (pipe->mutex); /* ignore error */
      return ___ERR_CODE_EAGAIN;
    }

  /* FIFO buffer is not full and out-of-band not present */

  end = pipe->size - wr; /* number of bytes from wr to end */

  n = rd + end - 1; /* number of bytes available */
  if (n >= pipe->size)
    n -= pipe->size;

  if (n > ___CAST(DWORD,len)) /* don't transfer more than len */
    n = len;

  *len_done = n;

  p = pipe->buffer + wr; /* prepare transfer source */

  wr = wr + n;
  if (wr >= pipe->size)
    wr -= pipe->size;

  pipe->wr = wr;

  if (wr + 1 - rd == 0 || wr + 1 - rd == pipe->size) /* FIFO will be full? */
    ResetEvent (pipe->wevent); /* ignore error */

  /* FIFO will not be empty */

  SetEvent (pipe->revent); /* ignore error */

  if (n <= end)
    {
      /* only need to transfer n bytes starting from original wr */

      for (i=n; i>0; i--)
        *p++ = *buf++;
    }
  else
    {
      /* need to transfer end bytes starting from original wr */

      for (i=end; i>0; i--)
        *p++ = *buf++;

      /* and to transfer n-end bytes starting from 0 */

      p = pipe->buffer;

      for (i=n-end; i>0; i--)
        *p++ = *buf++;
    }

  ReleaseMutex (pipe->mutex); /* ignore error */

  return ___FIX(___NO_ERR);
}

#endif


/*---------------------------------------------------------------------------*/

/* Operations on I/O devices. */

/*---------------------------------------------------------------------------*/

/* Generic device operations. */

___SCMOBJ ___device_select
   ___P((___device **devs,
         int nb_read_devs,
         int nb_write_devs,
         ___time timeout),
        (devs,
         nb_read_devs,
         nb_write_devs,
         timeout)
___device **devs;
int nb_read_devs;
int nb_write_devs;
___time timeout;)
{
  int nb_devs;
  ___device_select_state state;
  int pass;
  int dev_list;
  int i;
  int prev;
  ___time delta;

  nb_devs = nb_read_devs + nb_write_devs;

  state.devs = devs;

  state.timeout = timeout;
  state.relative_timeout = POS_INFINITY;

#ifdef USE_select

  state.highest_fd_plus_1 = 0;

  FD_ZERO(&state.readfds);
  FD_ZERO(&state.writefds);
  FD_ZERO(&state.exceptfds);

#endif

#ifdef USE_MsgWaitForMultipleObjects

  state.message_queue_mask = 0;
  state.message_queue_dev_pos = -1;

  state.wait_objs_buffer[0] = ___io_mod.abort_select;
  state.wait_objs_buffer[1] = ___time_mod.heartbeat_thread;

  state.nb_wait_objs = 2;

#endif

  if (nb_devs > 0)
    {
      state.devs_next[nb_devs-1] = -1;

      for (i=nb_devs-2; i>=0; i--)
        state.devs_next[i] = i+1;

      dev_list = 0;
    }
  else
    dev_list = -1;

  pass = ___SELECT_PASS_1;

  while (dev_list != -1)
    {
      i = dev_list;
      prev = -1;

      while (i != -1)
        {
          ___SCMOBJ e;
          ___device *d = devs[i];
          if ((e = ___device_select_virt
                     (d,
                      i>=nb_read_devs,
                      i,
                      pass,
                      &state))
              == ___FIX(___NO_ERR))
            {
              prev = i;
              i = state.devs_next[i];
            }
          else
            {
              int j;
              if (e != ___FIX(___SELECT_SETUP_DONE))
                return e;
              j = state.devs_next[i];
              if (prev == -1)
                dev_list = j;
              else
                state.devs_next[prev] = j;
#ifdef USE_MsgWaitForMultipleObjects
              state.devs_next[i] = -1;
#endif
              i = j;
            }
        }

      pass++;
    }

  ___absolute_time_to_relative_time (state.timeout, &delta);

  if (___time_less (state.relative_timeout, delta))
    {
      delta = state.relative_timeout;
      state.timeout = ___time_mod.time_neg_infinity;
    }
  else
    state.relative_timeout = NEG_INFINITY;

#ifdef USE_select

  {
    struct timeval delta_tv_struct;
    struct timeval *delta_tv = &delta_tv_struct;
    int select_result;

    ___absolute_time_to_nonnegative_timeval (delta, &delta_tv);

    select_result =
      select (state.highest_fd_plus_1,
              &state.readfds,
              &state.writefds,
              &state.exceptfds,
              delta_tv);

    if (select_result < 0)
      return err_code_from_errno ();

    state.timeout_reached = (select_result == 0);
  }

#endif

#ifdef USE_MsgWaitForMultipleObjects

  {
    DWORD delta_msecs;

    ___absolute_time_to_nonnegative_msecs (delta, &delta_msecs);

    state.timeout_reached = 0;

    if (delta_msecs == 0)
      goto timed_out; /* don't even call MsgWaitForMultipleObjects */

    while (state.nb_wait_objs > 0 || state.message_queue_mask != 0)
      {
        DWORD n;

        n = MsgWaitForMultipleObjects
              (state.nb_wait_objs,
               state.wait_objs_buffer,
               FALSE,
               delta_msecs,
               state.message_queue_mask);

        if (n == WAIT_FAILED)
          return err_code_from_GetLastError ();

        if ((n - WAIT_OBJECT_0) <= WAIT_OBJECT_0 + state.nb_wait_objs)
          n -= WAIT_OBJECT_0;
        else if (n >= WAIT_ABANDONED_0 &&
                 n <= WAIT_ABANDONED_0+state.nb_wait_objs-1)
          n -= WAIT_ABANDONED_0;
        else
          {
            /* n == WAIT_TIMEOUT */

            /*
             * The call to MsgWaitForMultipleObjects timed out.  Mark
             * the appropriate device "ready".
             */

            if (delta_msecs != 0)
              {
                /* first call to MsgWaitForMultipleObjects */

                timed_out:

                state.timeout_reached = 1;
              }

            break;
          }

        if (n == state.nb_wait_objs)
          {
            /*
             * The message queue contains a message that is of interest.
             * Mark the appropriate device "ready".
             */

            i = state.message_queue_dev_pos;
            if (i >= 0)
              state.devs_next[i] = 0;

            /*
             * Don't check if other devices are ready because this might
             * cause an infinite loop.
             */

            break;
          }
        else if (n == 0)
          {
            /*
             * The call to ___device_select must be aborted because the
             * abort_select event is set.  This occurs when an interrupt
             * (such as a CTRL-C user interrupt) needs to be serviced
             * promptly by the main program.
             */

            ResetEvent (___io_mod.abort_select); /* ignore error */

            return ___FIX(___ERRNO_ERR(EINTR));
          }
        else if (n == 1)
          {
            /*
             * The heartbeat thread has died.  This is normally due to
             * the program being terminated abruptly by the user (for
             * example by using the thread manager or the "shutdown"
             * item in the start menu).  When this happens we must
             * initiate a clean termination of the program.
             */

            return ___FIX(___UNKNOWN_ERR);
          }
        else
          {
            /* Mark the appropriate device "ready". */

            i = state.wait_obj_to_dev_pos[n];
            if (i >= 0)
              state.devs_next[i] = 0;

            /* Prepare to check remaining devices. */

            state.nb_wait_objs--;

            state.wait_objs_buffer[n] =
              state.wait_objs_buffer[state.nb_wait_objs];

            state.wait_obj_to_dev_pos[n] =
              state.wait_obj_to_dev_pos[state.nb_wait_objs];
          }

        delta_msecs = 0; /* next MsgWaitForMultipleObjects will only poll */
      }
  }

#endif

  for (i=nb_devs-1; i>=0; i--)
    {
      ___SCMOBJ e;
      ___device *d = devs[i];
      if (d != NULL)
        if ((e = ___device_select_virt
                   (d,
                    i>=nb_read_devs,
                    i,
                    ___SELECT_PASS_CHECK,
                    &state))
            != ___FIX(___NO_ERR))
          return e;
    }

  return ___FIX(___NO_ERR);
}


void ___device_select_add_relative_timeout
   ___P((___device_select_state *state,
         int i,
         ___F64 seconds),
        (state,
         i,
         seconds)
___device_select_state *state;
int i;
___F64 seconds;)
{
  if (seconds < state->relative_timeout)
    state->relative_timeout = seconds;
}


void ___device_select_add_timeout
   ___P((___device_select_state *state,
         int i,
         ___time timeout),
        (state,
         i,
         timeout)
___device_select_state *state;
int i;
___time timeout;)
{
  if (___time_less (timeout, state->timeout))
    state->timeout = timeout;
}


#ifdef USE_select

void ___device_select_add_fd
   ___P((___device_select_state *state,
         int fd,
         ___BOOL for_writing),
        (state,
         fd,
         for_writing)
___device_select_state *state;
int fd;
___BOOL for_writing;)
{
  if (for_writing)
    FD_SET(fd, &state->writefds);
  else
    FD_SET(fd, &state->readfds);

  if (fd >= state->highest_fd_plus_1)
    state->highest_fd_plus_1 = fd+1;
}

#endif


#ifdef USE_MsgWaitForMultipleObjects

void ___device_select_add_wait_obj
   ___P((___device_select_state *state,
         int i,
         HANDLE wait_obj),
        (state,
         i,
         wait_obj)
___device_select_state *state;
int i;
HANDLE wait_obj;)
{
  DWORD j = state->nb_wait_objs;

  if (j < MAXIMUM_WAIT_OBJECTS)
    {
      state->wait_objs_buffer[j] = wait_obj;
      state->wait_obj_to_dev_pos[j] = i;
      state->nb_wait_objs = j+1;
    }
}

#endif


___SCMOBJ ___device_flush_write
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___device_flush_write_virt (self);
}

___SCMOBJ ___device_close
   ___P((___device *self,
         int direction),
        (self,
         direction)
___device *self;
int direction;)
{
  return ___device_close_virt (self, direction);
}

___HIDDEN void device_add_to_group
   ___P((___device_group *dgroup,
         ___device *dev),
        (dgroup,
         dev)
___device_group *dgroup;
___device *dev;)
{
  ___device *head = dgroup->list;

  dev->group = dgroup;

  if (head == NULL)
    {
      dev->next = dev;
      dev->prev = dev;
      dgroup->list = dev;
    }
  else
    {
      ___device *tail = head->prev;
      dev->next = head;
      dev->prev = tail;
      tail->next = dev;
      head->prev = dev;
    }
}

___HIDDEN void device_remove_from_group
   ___P((___device *dev),
        (dev)
___device *dev;)
{
  ___device_group *dgroup = dev->group;
  ___device *prev = dev->prev;
  ___device *next = dev->next;

  if (prev == dev)
    dgroup->list = NULL;
  else
    {
      if (dgroup->list == dev)
        dgroup->list = next;
      prev->next = next;
      next->prev = prev;
      dev->next = dev;
      dev->prev = dev;
    }

  dev->group = NULL;
}

___HIDDEN void device_add_ref
   ___P((___device *self),
        (self)
___device *self;)
{
#ifdef USE_PUMPS
  InterlockedIncrement (&self->refcount);
#else
  self->refcount++;
#endif
}

___SCMOBJ ___device_release
   ___P((___device *self),
        (self)
___device *self;)
{
  ___SCMOBJ e = ___FIX(___NO_ERR);

  if (
#ifdef USE_PUMPS
      InterlockedDecrement (&self->refcount) == 0
#else
      --self->refcount == 0
#endif
      )
    {
      e = ___device_release_virt (self);
      ___free_mem (self);
    }

  return e;
}

___SCMOBJ ___device_cleanup
   ___P((___device *self),
        (self)
___device *self;)
{
  ___SCMOBJ e;
  ___device *devs[1];

  if (self->group == NULL)
    return ___FIX(___UNKNOWN_ERR);

  device_remove_from_group (self);

  for (;;)
    {
      e = ___device_close (self, ___DIRECTION_RD);
      if (e == ___FIX(___NO_ERR))
        break;
      if (e != ___ERR_CODE_EAGAIN)
        return e;

      devs[0] = self;
      e = ___device_select (devs, 1, 0, ___time_mod.time_pos_infinity);
      if (e != ___FIX(___NO_ERR))
        return e;
    }

  for (;;)
    {
      e = ___device_close (self, ___DIRECTION_WR);
      if (e == ___FIX(___NO_ERR))
        break;
      if (e != ___ERR_CODE_EAGAIN)
        return e;

      devs[0] = self;
      e = ___device_select (devs, 0, 1, ___time_mod.time_pos_infinity);
      if (e != ___FIX(___NO_ERR))
        return e;
    }

  return ___device_release (self);
}

/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Timer device. */

/*
 * Timer devices are not particularly useful, given that the scheduler
 * implements timeouts.  Use this as an example.
 */

___HIDDEN int device_timer_kind
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___TIMER_KIND;
}

___HIDDEN ___SCMOBJ device_timer_select_virt
   ___P((___device *self,
         ___BOOL for_writing,
         int i,
         int pass,
         ___device_select_state *state),
        (self,
         for_writing,
         i,
         pass,
         state)
___device *self;
___BOOL for_writing;
int i;
int pass;
___device_select_state *state;)
{
  ___device_timer *d = ___CAST(___device_timer*,self);

  if (pass == ___SELECT_PASS_1)
    {
      if (___time_less (d->expiry, state->timeout))
        state->timeout = d->expiry;
      return ___FIX(___SELECT_SETUP_DONE);
    }

  /* pass == ___SELECT_PASS_CHECK */

  if (state->timeout_reached)
    {
      if (___time_equal (d->expiry, state->timeout))
        state->devs[i] = NULL;
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ device_timer_release_virt
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ device_timer_flush_write_virt
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ device_timer_close_virt
   ___P((___device *self,
         int direction),
        (self,
         direction)
___device *self;
int direction;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___device_timer_vtbl ___device_timer_table =
{
  {
    device_timer_kind,
    device_timer_select_virt,
    device_timer_release_virt,
    device_timer_flush_write_virt,
    device_timer_close_virt
  }
};

___SCMOBJ ___device_timer_setup
   ___P((___device_timer **dev,
         ___device_group *dgroup),
        (dev,
         dgroup)
___device_timer **dev;
___device_group *dgroup;)
{
  ___device_timer *d;

  d = ___CAST(___device_timer*,
              ___alloc_mem (sizeof (___device_timer)));

  if (d == NULL)
    return ___FIX(___HEAP_OVERFLOW_ERR);

  d->base.vtbl = &___device_timer_table;
  d->base.refcount = 1;
  d->base.direction = ___DIRECTION_RD | ___DIRECTION_WR;
  d->base.read_stage = ___STAGE_OPEN;
  d->base.write_stage = ___STAGE_OPEN;

  d->expiry = ___time_mod.time_pos_infinity;

  *dev = d;

  device_add_to_group (dgroup, &d->base);

  return ___FIX(___NO_ERR);
}

void ___device_timer_set_expiry
   ___P((___device_timer *dev,
         ___time expiry),
        (dev,
         expiry)
___device_timer *dev;
___time expiry;)
{
  dev->expiry = expiry;
}

/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Byte stream devices. */

#ifdef USE_PUMPS

___HIDDEN ___SCMOBJ ___device_stream_pump_setup
   ___P((___device_stream_pump **pump,
         DWORD committed_stack_size,
         LPTHREAD_START_ROUTINE proc,
         LPVOID arg),
        (pump,
         committed_stack_size,
         proc,
         arg)
___device_stream_pump **pump;
DWORD committed_stack_size;
LPTHREAD_START_ROUTINE proc;
LPVOID arg;)
{
  ___SCMOBJ e;
  ___device_stream_pump *p;
  HANDLE thread_handle;
  DWORD thread_id;

  p = ___CAST(___device_stream_pump*,
              ___alloc_mem (sizeof (___device_stream_pump)));

  if (p == NULL)
    return ___FIX(___HEAP_OVERFLOW_ERR);

  e = ___nonblocking_pipe_setup (&p->pipe, PIPE_BUFFER_SIZE+1);

  if (e != ___FIX(___NO_ERR))
    {
      ___free_mem (p);
      return e;
    }

  *pump = p; /* set before thread created to avoid race condition */

  thread_handle =
    CreateThread (NULL,                 /* no security attributes       */
                  committed_stack_size, /* committed stack size         */
                  proc,                 /* thread procedure             */
                  arg,                  /* argument to thread procedure */
                  0,                    /* use default creation flags   */
                  &thread_id);

  if (thread_handle == NULL ||
      !SetThreadPriority (thread_handle, PUMP_PRIORITY))
    {
      e = err_code_from_GetLastError ();
      ___nonblocking_pipe_cleanup (&p->pipe);
      ___free_mem (p);
      *pump = NULL; /* make sure caller does not think a pump was created */
      return e;
    }

  p->thread = thread_handle;

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_stream_pump_reader_kill
   ___P((___device_stream_pump *pump),
        (pump)
___device_stream_pump *pump;)
{
  return ___nonblocking_pipe_set_reader_err
           (&pump->pipe,
            ___FIX(___KILL_PUMP));
}

___HIDDEN ___SCMOBJ ___device_stream_pump_writer_kill
   ___P((___device_stream_pump *pump),
        (pump)
___device_stream_pump *pump;)
{
  return ___nonblocking_pipe_set_writer_err
           (&pump->pipe,
            ___FIX(___KILL_PUMP));
}

___HIDDEN ___SCMOBJ ___device_stream_pump_wait
   ___P((___device_stream_pump *pump),
        (pump)
___device_stream_pump *pump;)
{
  DWORD code;

  code = WaitForSingleObject (pump->thread, 0);

  if (code == WAIT_FAILED)
    return err_code_from_GetLastError ();

  if (code == WAIT_TIMEOUT)
    return ___ERR_CODE_EAGAIN;

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_stream_pump_cleanup
   ___P((___device_stream_pump *pump),
        (pump)
___device_stream_pump *pump;)
{
  CloseHandle (pump->thread); /* ignore error */
  ___nonblocking_pipe_cleanup (&pump->pipe); /* ignore error */
  ___free_mem (pump);

  return ___FIX(___NO_ERR);
}

#endif

___SCMOBJ ___device_stream_select_virt
   ___P((___device *self,
         ___BOOL for_writing,
         int i,
         int pass,
         ___device_select_state *state),
        (self,
         for_writing,
         i,
         pass,
         state)
___device *self;
___BOOL for_writing;
int i;
int pass;
___device_select_state *state;)
{
  ___device_stream *d = ___CAST(___device_stream*,self);

#ifdef USE_PUMPS

  int stage = (for_writing
               ? d->base.write_stage
               : d->base.read_stage);
  ___device_stream_pump *p = (for_writing
                              ? d->write_pump
                              : d->read_pump);

  if (p != NULL)
    {
      if (pass == ___SELECT_PASS_1)
        {
          HANDLE wait_obj;

          if (stage != ___STAGE_OPEN)
            wait_obj = p->thread;
          else
            {
              if (for_writing)
                wait_obj = p->pipe.wevent;
              else
                wait_obj = p->pipe.revent;
            }

          ___device_select_add_wait_obj (state, i, wait_obj);

          return ___FIX(___SELECT_SETUP_DONE);
        }

      /* pass == ___SELECT_PASS_CHECK */

      if (stage != ___STAGE_OPEN)
        state->devs[i] = NULL;
      else
        {
          if (state->devs_next[i] != -1)
            state->devs[i] = NULL;
        }

      return ___FIX(___NO_ERR);
    }

#endif

  return ___device_stream_select_raw_virt
           (d,
            for_writing,
            i,
            pass,
            state);
}


___SCMOBJ ___device_stream_release_virt
   ___P((___device *self),
        (self)
___device *self;)
{
  ___SCMOBJ e;
  ___device_stream *d = ___CAST(___device_stream*,self);

  e = ___device_stream_release_raw_virt (d);

#ifdef USE_PUMPS

  {
    ___device_stream_pump *p;

    p = d->read_pump;
    if (p != NULL)
      ___device_stream_pump_cleanup (p); /* ignore error */

    p = d->write_pump;
    if (p != NULL)
      ___device_stream_pump_cleanup (p); /* ignore error */
  }

#endif

  return e;
}


___SCMOBJ ___device_stream_flush_write_virt
   ___P((___device *self),
        (self)
___device *self;)
{
  ___device_stream *d = ___CAST(___device_stream*,self);

#ifdef USE_PUMPS

  {
    ___device_stream_pump *p = d->write_pump;

    if (p != NULL)
      {
        ___nonblocking_pipe_oob_msg oob_msg;

        oob_msg.op = OOB_FLUSH_WRITE;

        return ___nonblocking_pipe_write_oob (&p->pipe, &oob_msg);
      }
  }

#endif

  return ___device_stream_flush_write_raw_virt (d);
}


___SCMOBJ ___device_stream_close_virt
   ___P((___device *self,
         int direction),
        (self,
         direction)
___device *self;
int direction;)
{
  ___device_stream *d = ___CAST(___device_stream*,self);

#ifdef USE_PUMPS

  if (direction & ___DIRECTION_RD)
    {
      ___device_stream_pump *p = d->read_pump;

      if (p != NULL)
        ___device_stream_pump_reader_kill (p);
    }

  if (direction & ___DIRECTION_WR)
    {
      ___device_stream_pump *p = d->write_pump;

      if (p != NULL)
        ___device_stream_pump_writer_kill (p);
    }

#endif

  return ___device_stream_close_raw_virt (d, direction);
}


___SCMOBJ ___device_stream_seek
   ___P((___device_stream *self,
         ___stream_index *pos,
         int whence),
        (self,
         pos,
         whence)
___device_stream *self;
___stream_index *pos;
int whence;)
{
#ifdef USE_PUMPS

  {
    ___device_stream_pump *p = self->write_pump;

    if (p != NULL)
      {
        ___nonblocking_pipe_oob_msg oob_msg;

        oob_msg.op = OOB_SEEK_ABS + whence;
        oob_msg.stream_index_param = *pos;

        return ___nonblocking_pipe_write_oob (&p->pipe, &oob_msg);
      }
  }

#endif

  return ___device_stream_seek_raw_virt (self, pos, whence);
}

___SCMOBJ ___device_stream_read
   ___P((___device_stream *self,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done),
        (self,
         buf,
         len,
         len_done)
___device_stream *self;
___U8 *buf;
___stream_index len;
___stream_index *len_done;)
{
#ifdef USE_PUMPS

  {
    ___device_stream_pump *p = self->read_pump;

    if (p != NULL)
      {
        ___nonblocking_pipe_oob_msg oob_msg;
        return ___nonblocking_pipe_read
                 (&p->pipe,
                  buf,
                  len,
                  len_done,
                  &oob_msg);
      }
  }

#endif

  return ___device_stream_read_raw_virt (self, buf, len, len_done);
}

___SCMOBJ ___device_stream_write
   ___P((___device_stream *self,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done),
        (self,
         buf,
         len,
         len_done)
___device_stream *self;
___U8 *buf;
___stream_index len;
___stream_index *len_done;)
{
#ifdef USE_PUMPS

  {
    ___device_stream_pump *p = self->write_pump;

    if (p != NULL)
      return ___nonblocking_pipe_write (&p->pipe, buf, len, len_done);
  }

#endif

  return ___device_stream_write_raw_virt (self, buf, len, len_done);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef USE_PUMPS

___HIDDEN DWORD WINAPI ___device_stream_read_pump_proc
   ___P((LPVOID param),
        (param)
LPVOID param;)
{
  ___device_stream *dev = ___CAST(___device_stream*,param);
  ___nonblocking_pipe *p = &dev->read_pump->pipe;
  ___SCMOBJ e;
  ___stream_index len;
  ___stream_index n;
  ___stream_index i;
  ___U8 buf[PIPE_BUFFER_SIZE];
  ___nonblocking_pipe_oob_msg oob_msg;

  for (;;)
    {
      /* wait until the reader needs some data */

      e = ___nonblocking_pipe_write_ready_wait (p);

      /* read some characters from device */

      if (e == ___FIX(___NO_ERR))
        e = ___device_stream_read_raw_virt (dev, buf, PIPE_BUFFER_SIZE, &len);

      if (e == ___FIX(___NO_ERR))
        {
          if (len == 0)
            {
              /* we reached the end-of-stream */

              oob_msg.op = OOB_EOS;

              while ((e = ___nonblocking_pipe_write_oob (p, &oob_msg))
                     == ___ERR_CODE_EAGAIN)
                {
                  /* suspend thread until operation can be performed */

                  e = ___nonblocking_pipe_write_ready_wait (p);
                  if (e != ___FIX(___NO_ERR))
                    break;
                }
              if (e != ___FIX(___NO_ERR))
                break;
            }
          else
            {
              /* write to the pipe the bytes that were read */

              i = 0;

              while (i < len)
                {
                  while ((e = ___nonblocking_pipe_write (p, buf+i, len-i, &n))
                         == ___ERR_CODE_EAGAIN)
                    {
                      /* suspend thread until operation can be performed */

                      e = ___nonblocking_pipe_write_ready_wait (p);
                      if (e != ___FIX(___NO_ERR))
                        break;
                    }
                  if (e != ___FIX(___NO_ERR))
                    break;
                  i += n;
                }
            }
        }

      if (e != ___FIX(___NO_ERR))
        {
          if (e == ___FIX(___KILL_PUMP)) /* terminate? */
            break;

          if (e == ___ERR_CODE_EAGAIN)
            continue;

          /* report the failure back through the pipe */

          e = ___nonblocking_pipe_set_writer_err (p, e);

          if (e != ___FIX(___NO_ERR))
            {
              /*
               * The failure could not be reported. To avoid an
               * infinite loop the thread is terminated.
               */

              ExitThread (0);
            }
        }
    }

  ___device_release (&dev->base); /* ignore error */

  return 0;
}

___HIDDEN DWORD WINAPI ___device_stream_write_pump_proc
   ___P((LPVOID param),
        (param)
LPVOID param;)
{
  ___device_stream *dev = ___CAST(___device_stream*,param);
  ___nonblocking_pipe *p = &dev->write_pump->pipe;
  ___SCMOBJ e;
  ___stream_index len;
  ___stream_index n;
  ___stream_index i;
  ___U8 buf[PIPE_BUFFER_SIZE];
  ___nonblocking_pipe_oob_msg oob_msg;

  for (;;)
    {
      /* get from the pipe some bytes to write to the device */

      while ((e = ___nonblocking_pipe_read
                    (p,
                     buf,
                     PIPE_BUFFER_SIZE,
                     &len,
                     &oob_msg))
             == ___ERR_CODE_EAGAIN)
        {
          /* suspend thread until operation can be performed */

          e = ___nonblocking_pipe_read_ready_wait (p);
          if (e != ___FIX(___NO_ERR))
            break;
        }

      if (e == ___FIX(___NO_ERR))
        {
          if (len > 0)
            {
              /* write to the device the bytes that were read from the pipe */

              i = 0;

              while (i < len)
                {
                  e = ___device_stream_write_raw_virt (dev, buf+i, len-i, &n);
                  if (e != ___FIX(___NO_ERR))
                    break;
                  i += n;
                }
            }
          else
            {
              switch (oob_msg.op)
                {
                case OOB_FLUSH_WRITE:
#ifdef ___DEBUG
                  ___printf ("***** got OOB_FLUSH_WRITE\n");
#endif
                  e = ___device_stream_flush_write_raw_virt (dev);
                  break;
                case OOB_SEEK_ABS:
                case OOB_SEEK_REL:
                case OOB_SEEK_REL_END:
#ifdef ___DEBUG
                  ___printf ("***** got OOB_SEEK %d %d\n",
                             oob_msg.stream_index_param,
                             oob_msg.op - OOB_SEEK_ABS);
#endif
                  e = ___device_stream_seek_raw_virt
                        (dev,
                         &oob_msg.stream_index_param,
                         oob_msg.op - OOB_SEEK_ABS);
                  break;
                case OOB_EOS:
#ifdef ___DEBUG
                  ___printf ("***** got OOB_EOS\n");
#endif
                  break;
                }
            }
        }

      if (e != ___FIX(___NO_ERR))
        {
          if (e == ___FIX(___KILL_PUMP)) /* terminate? */
            break;

          /* report the failure back through the pipe */

          e = ___nonblocking_pipe_set_reader_err (p, e);

          if (e != ___FIX(___NO_ERR))
            {
              /*
               * The failure could not be reported. To avoid an
               * infinite loop the thread is terminated.
               */

              ExitThread (0);
            }
        }
    }

  ___device_release (&dev->base); /* ignore error */

  return 0;
}

#endif

___SCMOBJ ___device_stream_setup
   ___P((___device_stream *dev,
         ___device_group *dgroup,
         int direction,
         int pumps_on),
        (dev,
         dgroup,
         direction,
         pumps_on)/*********************/
___device_stream *dev;
___device_group *dgroup;
int direction;
int pumps_on;)
{
  dev->base.refcount = 1;
  dev->base.direction = direction;
  dev->base.read_stage = ___STAGE_CLOSED;
  dev->base.write_stage = ___STAGE_CLOSED;

#ifdef USE_PUMPS
  dev->read_pump = NULL;
  dev->write_pump = NULL;
#endif

  device_add_to_group (dgroup, &dev->base);

  if (direction & ___DIRECTION_RD)
    {
      dev->base.read_stage = ___STAGE_OPEN;

#ifdef USE_PUMPS

      if (pumps_on & ___DIRECTION_RD)
        {
          ___SCMOBJ e;

          device_add_ref (&dev->base);

          if ((e = ___device_stream_pump_setup
                     (&dev->read_pump,
                      65536,
                      ___device_stream_read_pump_proc,
                      dev))
              != ___FIX(___NO_ERR))
            {
              ___device_release (&dev->base); /* ignore error */
              ___device_cleanup (&dev->base); /* ignore error */
              return e;
            }
        }

#endif
    }

  if (direction & ___DIRECTION_WR)
    {
      dev->base.write_stage = ___STAGE_OPEN;

#ifdef USE_PUMPS

      if (pumps_on & ___DIRECTION_WR)
        {
          ___SCMOBJ e;

          device_add_ref (&dev->base);

          if ((e = ___device_stream_pump_setup
                     (&dev->write_pump,
                      65536,
                      ___device_stream_write_pump_proc,
                      dev))
              != ___FIX(___NO_ERR))
            {
              ___device_release (&dev->base); /* ignore error */
              ___device_cleanup (&dev->base); /* ignore error */
              return e;
            }
        }

#endif
    }

  return ___FIX(___NO_ERR);
}


/*---------------------------------------------------------------------------*/

/* Serial stream device */

#ifdef USE_WIN32

typedef struct ___device_serial_struct
  {
    ___device_stream base;
    HANDLE h;
  } ___device_serial;

typedef struct ___device_serial_vtbl_struct
  {
    ___device_stream_vtbl base;
  } ___device_serial_vtbl;

___HIDDEN int ___device_serial_kind
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___SERIAL_DEVICE_KIND;
}

___HIDDEN ___SCMOBJ ___device_serial_close_raw_virt
   ___P((___device_stream *self,
         int direction),
        (self,
         direction)
___device_stream *self;
int direction;)
{
  ___device_serial *d = ___CAST(___device_serial*,self);
  int is_not_closed = 0;

  if (d->base.base.read_stage != ___STAGE_CLOSED)
    is_not_closed |= ___DIRECTION_RD;

  if (d->base.base.write_stage != ___STAGE_CLOSED)
    is_not_closed |= ___DIRECTION_WR;

  if (is_not_closed == 0)
    return ___FIX(___NO_ERR);

  if (is_not_closed == (___DIRECTION_RD|___DIRECTION_WR))
    {
      d->base.base.read_stage = ___STAGE_CLOSED;
      d->base.base.write_stage = ___STAGE_CLOSED;

      if (!CloseHandle (d->h))
        return err_code_from_GetLastError ();
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_serial_select_raw_virt
   ___P((___device_stream *self,
         ___BOOL for_writing,
         int i,
         int pass,
         ___device_select_state *state),
        (self,
         for_writing,
         i,
         pass,
         state)
___device_stream *self;
___BOOL for_writing;
int i;
int pass;
___device_select_state *state;)
{
  ___device_serial *d = ___CAST(___device_serial*,self);
  int stage = (for_writing
               ? d->base.base.write_stage
               : d->base.base.read_stage);

  if (pass == ___SELECT_PASS_1)
    {
      if (stage != ___STAGE_OPEN)
        state->timeout = ___time_mod.time_neg_infinity;
      return ___FIX(___SELECT_SETUP_DONE);
    }

  /* pass == ___SELECT_PASS_CHECK */

  if (stage != ___STAGE_OPEN)
    state->devs[i] = NULL;
  else
    {
      if (state->devs_next[i] != -1)
        state->devs[i] = NULL;
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_serial_release_raw_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_serial_flush_write_raw_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  ___device_serial *d = ___CAST(___device_serial*,self);

  if (d->base.base.write_stage == ___STAGE_OPEN)
    {
      if (!FlushFileBuffers (d->h))
        return err_code_from_GetLastError ();
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_serial_seek_raw_virt
   ___P((___device_stream *self,
         ___stream_index *pos,
         int whence),
        (self,
         pos,
         whence)
___device_stream *self;
___stream_index *pos;
int whence;)
{
  return ___FIX(___INVALID_OP_ERR);
}

___HIDDEN ___SCMOBJ ___device_serial_read_raw_virt
   ___P((___device_stream *self,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done),
        (self,
         buf,
         len,
         len_done)
___device_stream *self;
___U8 *buf;
___stream_index len;
___stream_index *len_done;)
{
  ___device_serial *d = ___CAST(___device_serial*,self);

  if (d->base.base.read_stage != ___STAGE_OPEN)
    return ___FIX(___CLOSED_DEVICE_ERR);

  {
    DWORD n;

    if (!ReadFile (d->h, buf, len, &n, NULL))
      return err_code_from_GetLastError ();

    if (n == 0)
      return ___ERR_CODE_EAGAIN;

    *len_done = n;
  }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_serial_write_raw_virt
   ___P((___device_stream *self,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done),
        (self,
         buf,
         len,
         len_done)
___device_stream *self;
___U8 *buf;
___stream_index len;
___stream_index *len_done;)
{
  ___device_serial *d = ___CAST(___device_serial*,self);

  if (d->base.base.write_stage != ___STAGE_OPEN)
    return ___FIX(___CLOSED_DEVICE_ERR);

  {
    DWORD n;

    if (!WriteFile (d->h, buf, len, &n, NULL))
      {
        /*
         * Even though WriteFile has reported a failure, the operation
         * was executed correctly (i.e. len_done contains the number
         * of bytes written) if GetLastError returns ERROR_SUCCESS.
         */

        if (GetLastError () != ERROR_SUCCESS)
          return err_code_from_GetLastError ();
      }

    *len_done = n;
  }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_serial_width_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  return ___FIX(80);
}

___HIDDEN ___SCMOBJ ___device_serial_default_options_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  int char_encoding = ___CHAR_ENCODING_ISO_8859_1;
  int eol_encoding = ___EOL_ENCODING_LF;
  int buffering = ___FULL_BUFFERING;

  return ___FIX(___STREAM_OPTIONS(char_encoding,
                                  eol_encoding,
                                  buffering,
                                  char_encoding,
                                  eol_encoding,
                                  buffering));
}


___HIDDEN ___SCMOBJ ___device_serial_options_set_virt
   ___P((___device_stream *self,
         ___SCMOBJ options),
        (self,
         options)
___device_stream *self;
___SCMOBJ options;)
{
  return ___FIX(___NO_ERR);
}


___HIDDEN ___device_serial_vtbl ___device_serial_table =
{
  {
    {
      ___device_serial_kind,
      ___device_stream_select_virt,
      ___device_stream_release_virt,
      ___device_stream_flush_write_virt,
      ___device_stream_close_virt
    },
    ___device_serial_select_raw_virt,
    ___device_serial_release_raw_virt,
    ___device_serial_flush_write_raw_virt,
    ___device_serial_close_raw_virt,
    ___device_serial_seek_raw_virt,
    ___device_serial_read_raw_virt,
    ___device_serial_write_raw_virt,
    ___device_serial_width_virt,
    ___device_serial_default_options_virt,
    ___device_serial_options_set_virt
  }
};


___HIDDEN ___SCMOBJ ___device_serial_set_comm_state
   ___P((___device_serial *dev,
         LPCTSTR def),
        (dev,
         def)
___device_serial *dev;
LPCTSTR def;)
{
  DCB dcb;

  FillMemory (&dcb, sizeof (dcb), 0);

  if (!GetCommState (dev->h, &dcb) ||
      !BuildCommDCB (def, &dcb) ||
      !SetCommState (dev->h, &dcb))
    return err_code_from_GetLastError ();

  return ___FIX(___NO_ERR);
}

___SCMOBJ ___device_serial_setup_from_handle
   ___P((___device_serial **dev,
         ___device_group *dgroup,
         HANDLE h,
         int direction),
        (dev,
         dgroup,
         h,
         direction)
___device_serial **dev;
___device_group *dgroup;
HANDLE h;
int direction;)
{
  ___device_serial *d;
  ___SCMOBJ e;
  COMMTIMEOUTS cto;

  d = ___CAST(___device_serial*,
              ___alloc_mem (sizeof (___device_serial)));

  if (d == NULL)
    return ___FIX(___HEAP_OVERFLOW_ERR);

  d->base.base.vtbl = &___device_serial_table;
  d->h = h;

  *dev = d;

  e = ___device_serial_set_comm_state (d, _T("baud=38400 parity=N data=8 stop=1"));

  if (e != ___FIX(___NO_ERR))
    {
      ___free_mem (d);
      return e;
    }

  /*
   * Setup serial device so that ReadFile will return as soon as a
   * character is available.
   */

  cto.ReadIntervalTimeout         = MAXDWORD;
  cto.ReadTotalTimeoutMultiplier  = MAXDWORD;
  cto.ReadTotalTimeoutConstant    = 1; /* wait no more than 1 ms */
  cto.WriteTotalTimeoutMultiplier = 0;
  cto.WriteTotalTimeoutConstant   = 0;

  if (!SetCommTimeouts (h, &cto)
#ifdef USE_BIG_SERIAL_BUFFERS
      || !SetupComm (h, 65536, 65536))
#endif
     )
    {
      e = err_code_from_GetLastError ();
      ___free_mem (d);
      return e;
    }

  return ___device_stream_setup
           (&d->base,
            dgroup,
            direction,
            ___DIRECTION_RD|___DIRECTION_WR);
}

#endif


/*---------------------------------------------------------------------------*/

/* Process stream device */

typedef struct ___device_process_struct
  {
    ___device_stream base;

#ifdef USE_POSIX
    pid_t pid;     /* pid of the process */
    int fd_stdin;  /* file descriptor corresponding to process' stdin */
    int fd_stdout; /* file descriptor corresponding to process' stdout */
#endif

#ifdef USE_WIN32
    PROCESS_INFORMATION pi; /* process information */
    HANDLE hstdin;          /* handle corresponding to process' stdin */
    HANDLE hstdout;         /* handle corresponding to process' stdout */
#endif

    int status;          /* process status */
    ___BOOL terminated;  /* has process terminated? */
  } ___device_process;

typedef struct ___device_process_vtbl_struct
  {
    ___device_stream_vtbl base;
  } ___device_process_vtbl;

___HIDDEN int ___device_process_kind
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___PROCESS_DEVICE_KIND;
}

___SCMOBJ ___device_process_cleanup
   ___P((___device_process *dev),
        (dev)
___device_process *dev;)
{
  if (!dev->terminated)
    {
      dev->terminated = 1;

#ifdef USE_POSIX
#endif

#ifdef USE_WIN32

      CloseHandle (dev->pi.hProcess); /* ignore error */
      CloseHandle (dev->pi.hThread); /* ignore error */

#endif
    }

  return ___FIX(___NO_ERR);
}


___SCMOBJ ___device_process_get_status
   ___P((___device_process *dev),
        (dev)
___device_process *dev;)
{
  if (!dev->terminated)
    {
#ifdef USE_POSIX

      /* 
       * The process status is updated asynchronously by
       * sigchld_signal_handler.
       */

#endif

#ifdef USE_WIN32

      DWORD status;

      if (!GetExitCodeProcess (dev->pi.hProcess, &status))
        return err_code_from_GetLastError ();

      dev->status = status << 8;

      if (status != STILL_ACTIVE)
        ___device_process_cleanup (dev); /* ignore error */

#endif
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_process_close_raw_virt
   ___P((___device_stream *self,
         int direction),
        (self,
         direction)
___device_stream *self;
int direction;)
{
  ___device_process *d = ___CAST(___device_process*,self);
  int is_not_closed = 0;

  if (d->base.base.read_stage != ___STAGE_CLOSED)
    is_not_closed |= ___DIRECTION_RD;

  if (d->base.base.write_stage != ___STAGE_CLOSED)
    is_not_closed |= ___DIRECTION_WR;

  if (is_not_closed == 0)
    return ___FIX(___NO_ERR);

  if (is_not_closed & direction & ___DIRECTION_RD)
    {
      /* Close process' stdout */

      d->base.base.read_stage = ___STAGE_CLOSED;

#ifdef USE_POSIX
      if (close (d->fd_stdout) < 0)
        return err_code_from_errno ();
#endif

#ifdef USE_WIN32
      if (!CloseHandle (d->hstdout))
        return err_code_from_GetLastError ();
#endif
    }

  if (is_not_closed & direction & ___DIRECTION_WR)
    {
      /* Close process' stdin */

      d->base.base.write_stage = ___STAGE_CLOSED;

#ifdef USE_POSIX
      if (close (d->fd_stdin) < 0)
        return err_code_from_errno ();
#endif

#ifdef USE_WIN32
      if (!CloseHandle (d->hstdin))
        return err_code_from_GetLastError ();
#endif
    }

  if (d->base.base.read_stage == ___STAGE_CLOSED &&
      d->base.base.write_stage == ___STAGE_CLOSED)
    {
      ___device_process_get_status (d); /* ignore error */
      ___device_process_cleanup (d); /* ignore error */
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_process_select_raw_virt
   ___P((___device_stream *self,
         ___BOOL for_writing,
         int i,
         int pass,
         ___device_select_state *state),
        (self,
         for_writing,
         i,
         pass,
         state)
___device_stream *self;
___BOOL for_writing;
int i;
int pass;
___device_select_state *state;)
{
  ___device_process *d = ___CAST(___device_process*,self);
  int stage = (for_writing
               ? d->base.base.write_stage
               : d->base.base.read_stage);

  if (pass == ___SELECT_PASS_1)
    {
      if (stage != ___STAGE_OPEN)
        state->timeout = ___time_mod.time_neg_infinity;
      else
        {
#ifdef USE_POSIX
          ___device_select_add_fd
            (state,
             for_writing ? d->fd_stdin : d->fd_stdout,
             for_writing);
#endif

#ifdef USE_WIN32
          ___device_select_add_wait_obj
            (state,
             i,
             for_writing ? d->hstdin : d->hstdout);
#endif
        }
      return ___FIX(___SELECT_SETUP_DONE);
    }

  /* pass == ___SELECT_PASS_CHECK */

  if (stage != ___STAGE_OPEN)
    state->devs[i] = NULL;
  else
    {
#ifdef USE_POSIX

      if (for_writing
           ? FD_ISSET(d->fd_stdin, &state->writefds)
           : FD_ISSET(d->fd_stdout, &state->readfds))
        state->devs[i] = NULL;

#endif

#ifdef USE_WIN32

      if (state->devs_next[i] != -1)
        state->devs[i] = NULL;

#endif
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_process_release_raw_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_process_flush_write_raw_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  ___device_process *d = ___CAST(___device_process*,self);

  if (d->base.base.write_stage == ___STAGE_OPEN)
    {
#if 0
      if (fsync (d->fd_stdin) < 0)/************only works on disk files!!!!!!!*/
        return err_code_from_errno ();
#endif
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_process_seek_raw_virt
   ___P((___device_stream *self,
         ___stream_index *pos,
         int whence),
        (self,
         pos,
         whence)
___device_stream *self;
___stream_index *pos;
int whence;)
{
  return ___FIX(___INVALID_OP_ERR);
}

___HIDDEN ___SCMOBJ ___device_process_read_raw_virt
   ___P((___device_stream *self,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done),
        (self,
         buf,
         len,
         len_done)
___device_stream *self;
___U8 *buf;
___stream_index len;
___stream_index *len_done;)
{
  ___device_process *d = ___CAST(___device_process*,self);

  if (d->base.base.read_stage != ___STAGE_OPEN)
    return ___FIX(___CLOSED_DEVICE_ERR);

#ifdef USE_POSIX

  {
    int n;
    if ((n = read (d->fd_stdout, buf, len)) < 0)
      {
#ifdef ___DEBUG

        ___printf ("process read returned errno=%d\n", errno);

#endif

        if (errno == EIO) errno = EAGAIN;
        return err_code_from_errno ();
      }

    *len_done = n;
  }

#endif

#ifdef USE_WIN32

  {
    DWORD n;

    if (!ReadFile (d->hstdout, buf, len, &n, NULL))
      {
#ifdef ___DEBUG

        ___printf ("process read returned GetLastError()=%d\n", GetLastError ());

#endif

        if (GetLastError() != ERROR_BROKEN_PIPE) /* handle like end-of-file */
          return err_code_from_GetLastError ();
      }

    *len_done = n;
  }

#endif

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_process_write_raw_virt
   ___P((___device_stream *self,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done),
        (self,
         buf,
         len,
         len_done)
___device_stream *self;
___U8 *buf;
___stream_index len;
___stream_index *len_done;)
{
  ___device_process *d = ___CAST(___device_process*,self);

  if (d->base.base.write_stage != ___STAGE_OPEN)
    return ___FIX(___CLOSED_DEVICE_ERR);

#ifdef USE_POSIX

  {
    int n;

    if ((n = write (d->fd_stdin, buf, len)) < 0)
      {
#ifdef ___DEBUG

        ___printf ("process write returned errno=%d\n", errno);

#endif

        return err_code_from_errno ();
      }

    *len_done = n;
  }

#endif

#ifdef USE_WIN32

  {
    DWORD n;

    if (!WriteFile (d->hstdin, buf, len, &n, NULL))
      return err_code_from_GetLastError ();

    *len_done = n;
  }

#endif

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_process_width_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  return ___FIX(80);
}

___HIDDEN ___SCMOBJ ___device_process_default_options_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  int char_encoding = ___CHAR_ENCODING_ISO_8859_1;
  int eol_encoding = ___EOL_ENCODING_LF;
  int buffering = ___FULL_BUFFERING;

  return ___FIX(___STREAM_OPTIONS(char_encoding,
                                  eol_encoding,
                                  buffering,
                                  char_encoding,
                                  eol_encoding,
                                  buffering));
}


___HIDDEN ___SCMOBJ ___device_process_options_set_virt
   ___P((___device_stream *self,
         ___SCMOBJ options),
        (self,
         options)
___device_stream *self;
___SCMOBJ options;)
{
  return ___FIX(___NO_ERR);
}


___HIDDEN ___device_process_vtbl ___device_process_table =
{
  {
    {
      ___device_process_kind,
      ___device_stream_select_virt,
      ___device_stream_release_virt,
      ___device_stream_flush_write_virt,
      ___device_stream_close_virt
    },
    ___device_process_select_raw_virt,
    ___device_process_release_raw_virt,
    ___device_process_flush_write_raw_virt,
    ___device_process_close_raw_virt,
    ___device_process_seek_raw_virt,
    ___device_process_read_raw_virt,
    ___device_process_write_raw_virt,
    ___device_process_width_virt,
    ___device_process_default_options_virt,
    ___device_process_options_set_virt
  }
};

#ifdef USE_POSIX

___SCMOBJ ___device_process_setup_from_pid
   ___P((___device_process **dev,
         ___device_group *dgroup,
         pid_t pid,
         int fd_stdin,
         int fd_stdout,
         int direction),
        (dev,
         dgroup,
         pid,
         fd_stdin,
         fd_stdout,
         direction)
___device_process **dev;
___device_group *dgroup;
pid_t pid;
int fd_stdin;
int fd_stdout;
int direction;)
{
  ___device_process *d;

  d = ___CAST(___device_process*,
              ___alloc_mem (sizeof (___device_process)));

  if (d == NULL)
    return ___FIX(___HEAP_OVERFLOW_ERR);

  /*
   * Setup file descriptors to perform nonblocking I/O.
   */

  if (((direction & ___DIRECTION_RD) &&
       (fcntl (fd_stdout, F_SETFL, O_NONBLOCK) < 0)) ||
      ((direction & ___DIRECTION_WR) &&
       (fcntl (fd_stdin, F_SETFL, O_NONBLOCK) < 0))) /* set nonblocking mode */
    {
      ___SCMOBJ e = err_code_from_errno ();
      ___free_mem (d);
      return e;
    }

  d->base.base.vtbl = &___device_process_table;
  d->pid = pid;
  d->fd_stdin = fd_stdin;
  d->fd_stdout = fd_stdout;
  d->status = -1;
  d->terminated = 0;

  *dev = d;

  return ___device_stream_setup
           (&d->base,
            dgroup,
            direction,
            0);
}

#endif

#ifdef USE_WIN32

___SCMOBJ ___device_process_setup_from_process
   ___P((___device_process **dev,
         ___device_group *dgroup,
         PROCESS_INFORMATION pi,
         HANDLE hstdin,
         HANDLE hstdout,
         int direction),
        (dev,
         dgroup,
         pi,
         hstdin,
         hstdout,
         direction)
___device_process **dev;
___device_group *dgroup;
PROCESS_INFORMATION pi;
HANDLE hstdin;
HANDLE hstdout;
int direction;)
{
  ___device_process *d;

  d = ___CAST(___device_process*,
              ___alloc_mem (sizeof (___device_process)));

  if (d == NULL)
    return ___FIX(___HEAP_OVERFLOW_ERR);

  d->base.base.vtbl = &___device_process_table;
  d->pi = pi;
  d->hstdin = hstdin;
  d->hstdout = hstdout;
  d->status = -1;
  d->terminated = 0;

  *dev = d;

  return ___device_stream_setup
           (&d->base,
            dgroup,
            direction,
            ___DIRECTION_RD|___DIRECTION_WR);
}

#endif


/*---------------------------------------------------------------------------*/

#ifdef USE_NETWORKING

/* Socket utilities */

#ifdef USE_POSIX
#define SOCKET_TYPE int
#define SOCKET_CALL_ERROR(s) ((s) < 0)
#define SOCKET_CALL_ERROR2(s) ((s) < 0)
#define CONNECT_IN_PROGRESS (errno == EINPROGRESS)
#define CONNECT_WOULD_BLOCK (errno == EAGAIN)
#define NOT_CONNECTED(e) ((e) == ___FIX(___ERRNO_ERR(ENOTCONN)))
#define CLOSE_SOCKET(s) close (s)
#define ERR_CODE_FROM_SOCKET_CALL err_code_from_errno ()
#define IOCTL_SOCKET(s,cmd,argp) ioctl (s,cmd,argp)
#define SOCKET_LEN_TYPE socklen_t
#endif

#ifdef USE_WIN32
#define SOCKET_TYPE SOCKET
#define SOCKET_CALL_ERROR(s) ((s) == SOCKET_ERROR)
#define SOCKET_CALL_ERROR2(s) ((s) == INVALID_SOCKET)
#define CONNECT_IN_PROGRESS ((WSAGetLastError () == WSAEALREADY) || \
(WSAGetLastError () == WSAEISCONN))
#define CONNECT_WOULD_BLOCK ((WSAGetLastError () == WSAEWOULDBLOCK) || \
(WSAGetLastError () == WSAEINVAL))
#define NOT_CONNECTED(e) ((e) == ___FIX(___WIN32_ERR(WSAENOTCONN)))
#define CLOSE_SOCKET(s) closesocket (s)
#define ERR_CODE_FROM_SOCKET_CALL err_code_from_WSAGetLastError ()
#define IOCTL_SOCKET(s,cmd,argp) ioctlsocket (s,cmd,argp)
#define SOCKET_LEN_TYPE int
#endif

#ifdef SHUT_RD
#define SHUTDOWN_RD SHUT_RD
#else
#ifdef SD_RECEIVE
#define SHUTDOWN_RD SD_RECEIVE
#else
#define SHUTDOWN_RD 0
#endif
#endif

#ifdef SHUT_WR
#define SHUTDOWN_WR SHUT_WR
#else
#ifdef SD_SEND
#define SHUTDOWN_WR SD_SEND
#else
#define SHUTDOWN_WR 1
#endif
#endif

#endif


/*---------------------------------------------------------------------------*/

#ifdef USE_NETWORKING

/* TCP client stream device */

typedef struct ___device_tcp_client_struct
  {
    ___device_stream base;
    SOCKET_TYPE s;
    struct sockaddr server_addr;
    SOCKET_LEN_TYPE server_addrlen;
    int try_connect_again;

#ifdef USE_POSIX

    int try_connect_interval_nsecs;

#endif

#ifdef USE_WIN32

    long io_events;   /* used by ___device_tcp_client_select_raw_virt */
    HANDLE io_event;  /* used by ___device_tcp_client_select_raw_virt */

#endif
  } ___device_tcp_client;


typedef struct ___device_tcp_client_vtbl_struct
  {
    ___device_stream_vtbl base;
  } ___device_tcp_client_vtbl;


___HIDDEN int try_connect
   ___P((___device_tcp_client *dev),
        (dev)
___device_tcp_client *dev;)
{
  if (!SOCKET_CALL_ERROR(connect (dev->s,
                                  &dev->server_addr,
                                  dev->server_addrlen)) ||
      CONNECT_IN_PROGRESS || /* establishing connection in background */
      dev->try_connect_again == 2) /* last connect attempt? */
    {
      dev->try_connect_again = 0; /* we're done waiting */
      return 0;
    }

  if (CONNECT_WOULD_BLOCK) /* connect can't be performed now */
    return 0;

  return -1;
}

___HIDDEN int ___device_tcp_client_kind
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___TCP_CLIENT_DEVICE_KIND;
}


___HIDDEN ___SCMOBJ ___device_tcp_client_close_raw_virt
   ___P((___device_stream *self,
         int direction),
        (self,
         direction)
___device_stream *self;
int direction;)
{
  ___device_tcp_client *d = ___CAST(___device_tcp_client*,self);
  int is_not_closed = 0;

  if (d->base.base.read_stage != ___STAGE_CLOSED)
    is_not_closed |= ___DIRECTION_RD;

  if (d->base.base.write_stage != ___STAGE_CLOSED)
    is_not_closed |= ___DIRECTION_WR;

  if (is_not_closed == 0)
    return ___FIX(___NO_ERR);

  if ((is_not_closed & ~direction) == 0)
    {
      /* Close socket when both sides are closed. */

      d->base.base.read_stage = ___STAGE_CLOSED; /* avoid multiple closes */
      d->base.base.write_stage = ___STAGE_CLOSED;

#ifdef USE_WIN32

      if (d->io_event != NULL)
        CloseHandle (d->io_event); /* ignore error */

#endif

      if (CLOSE_SOCKET(d->s) != 0)
        return ERR_CODE_FROM_SOCKET_CALL;
    }
  else if (is_not_closed & direction & ___DIRECTION_RD)
    {
      /* Shutdown receiving side. */

      if (shutdown (d->s, SHUTDOWN_RD) != 0)
        {
          ___SCMOBJ e = ERR_CODE_FROM_SOCKET_CALL;
          if (!NOT_CONNECTED(e))
            return e;
        }

      d->base.base.read_stage = ___STAGE_CLOSED;
    }
  else if (is_not_closed & direction & ___DIRECTION_WR)
    {
      /* Shutdown sending side. */

      if (shutdown (d->s, SHUTDOWN_WR) != 0)
        {
          ___SCMOBJ e = ERR_CODE_FROM_SOCKET_CALL;
          if (!NOT_CONNECTED(e))
            return e;
        }

      d->base.base.write_stage = ___STAGE_CLOSED;
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_tcp_client_select_raw_virt
   ___P((___device_stream *self,
         ___BOOL for_writing,
         int i,
         int pass,
         ___device_select_state *state),
        (self,
         for_writing,
         i,
         pass,
         state)
___device_stream *self;
___BOOL for_writing;
int i;
int pass;
___device_select_state *state;)
{
  ___device_tcp_client *d = ___CAST(___device_tcp_client*,self);
  int stage = (for_writing
               ? d->base.base.write_stage
               : d->base.base.read_stage);

  if (pass == ___SELECT_PASS_1)
    {
      if (stage != ___STAGE_OPEN)
        {
          state->timeout = ___time_mod.time_neg_infinity;
          return ___FIX(___SELECT_SETUP_DONE);
        }
      else
        {
#ifdef USE_POSIX

          if (d->try_connect_again != 0)
            {
              int interval = d->try_connect_interval_nsecs * 6 / 5;
              if (interval > 200000000) /* max interval = 0.2 sec */
                interval = 200000000;
              d->try_connect_interval_nsecs = interval;
              ___device_select_add_relative_timeout (state, i, interval * 1e-9);
            }
          else
            ___device_select_add_fd (state, d->s, for_writing);

          return ___FIX(___SELECT_SETUP_DONE);

#endif

#ifdef USE_WIN32

          d->io_events = 0;

          return ___FIX(___NO_ERR);

#endif
        }
    }

#ifdef USE_WIN32

  else if (pass == ___SELECT_PASS_2)
    {
      if (d->try_connect_again != 0)
        d->io_events = (FD_CONNECT | FD_CLOSE);
      else if (for_writing)
        d->io_events |= (FD_WRITE | FD_CLOSE);
      else
        d->io_events |= (FD_READ | FD_CLOSE);

      return ___FIX(___NO_ERR);
    }
  else if (pass == ___SELECT_PASS_3)
    {
      HANDLE wait_obj = d->io_event;

      ResetEvent (wait_obj); /* ignore error */

      WSAEventSelect (d->s, wait_obj, d->io_events);

      ___device_select_add_wait_obj (state, i, wait_obj);

      return ___FIX(___SELECT_SETUP_DONE);
    }

#endif

  /* pass == ___SELECT_PASS_CHECK */

  if (stage != ___STAGE_OPEN)
    state->devs[i] = NULL;
  else
    {
#ifdef USE_POSIX

      if (d->try_connect_again != 0 ||
          (for_writing
           ? FD_ISSET(d->s, &state->writefds)
           : FD_ISSET(d->s, &state->readfds)))
        state->devs[i] = NULL;

#endif

#ifdef USE_WIN32

      if (state->devs_next[i] != -1)
        {
          state->devs[i] = NULL;
          if (d->try_connect_again != 0)
            d->try_connect_again = 2;
        }

#endif
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_tcp_client_release_raw_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_tcp_client_flush_write_raw_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_tcp_client_seek_raw_virt
   ___P((___device_stream *self,
         ___stream_index *pos,
         int whence),
        (self,
         pos,
         whence)
___device_stream *self;
___stream_index *pos;
int whence;)
{
  return ___FIX(___INVALID_OP_ERR);
}

___HIDDEN ___SCMOBJ ___device_tcp_client_read_raw_virt
   ___P((___device_stream *self,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done),
        (self,
         buf,
         len,
         len_done)
___device_stream *self;
___U8 *buf;
___stream_index len;
___stream_index *len_done;)
{
  ___device_tcp_client *d = ___CAST(___device_tcp_client*,self);
  int n;

  if (d->base.base.read_stage != ___STAGE_OPEN)
    return ___FIX(___CLOSED_DEVICE_ERR);

  if (d->try_connect_again != 0)
    {
      if (try_connect (d) == 0)
        {
          if (d->try_connect_again != 0)
            return ___ERR_CODE_EAGAIN;
        }
      else
        return ERR_CODE_FROM_SOCKET_CALL;
    }

  if (SOCKET_CALL_ERROR(n = recv (d->s, ___CAST(char*,buf), len, 0)))
    {
      ___SCMOBJ e = ERR_CODE_FROM_SOCKET_CALL;
      if (NOT_CONNECTED(e))
        e = ___ERR_CODE_EAGAIN;
      return e;
    }

  *len_done = n;

  return ___FIX(___NO_ERR);
}


___HIDDEN ___SCMOBJ ___device_tcp_client_write_raw_virt
   ___P((___device_stream *self,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done),
        (self,
         buf,
         len,
         len_done)
___device_stream *self;
___U8 *buf;
___stream_index len;
___stream_index *len_done;)
{
  ___device_tcp_client *d = ___CAST(___device_tcp_client*,self);
  int n;

  if (d->base.base.write_stage != ___STAGE_OPEN)
    return ___FIX(___CLOSED_DEVICE_ERR);

  if (d->try_connect_again != 0)
    {
      if (try_connect (d) == 0)
        {
          if (d->try_connect_again != 0)
            return ___ERR_CODE_EAGAIN;
        }
      else
        return ERR_CODE_FROM_SOCKET_CALL;
    }

  if (SOCKET_CALL_ERROR(n = send (d->s, ___CAST(char*,buf), len, 0)))
    {
      ___SCMOBJ e = ERR_CODE_FROM_SOCKET_CALL;
      if (NOT_CONNECTED(e))
        e = ___ERR_CODE_EAGAIN;
      return e;
    }

  *len_done = n;

  return ___FIX(___NO_ERR);
}


___HIDDEN ___SCMOBJ ___device_tcp_client_width_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  return ___FIX(80);
}


___HIDDEN ___SCMOBJ ___device_tcp_client_default_options_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  int char_encoding = ___CHAR_ENCODING_ISO_8859_1;
  int eol_encoding = ___EOL_ENCODING_LF;
  int buffering = ___FULL_BUFFERING;

  return ___FIX(___STREAM_OPTIONS(char_encoding,
                                  eol_encoding,
                                  buffering,
                                  char_encoding,
                                  eol_encoding,
                                  buffering));
}


___HIDDEN ___SCMOBJ ___device_tcp_client_options_set_virt
   ___P((___device_stream *self,
         ___SCMOBJ options),
        (self,
         options)
___device_stream *self;
___SCMOBJ options;)
{
  return ___FIX(___NO_ERR);
}


___HIDDEN ___device_tcp_client_vtbl ___device_tcp_client_table =
{
  {
    {
      ___device_tcp_client_kind,
      ___device_stream_select_virt,
      ___device_stream_release_virt,
      ___device_stream_flush_write_virt,
      ___device_stream_close_virt
    },
    ___device_tcp_client_select_raw_virt,
    ___device_tcp_client_release_raw_virt,
    ___device_tcp_client_flush_write_raw_virt,
    ___device_tcp_client_close_raw_virt,
    ___device_tcp_client_seek_raw_virt,
    ___device_tcp_client_read_raw_virt,
    ___device_tcp_client_write_raw_virt,
    ___device_tcp_client_width_virt,
    ___device_tcp_client_default_options_virt,
    ___device_tcp_client_options_set_virt
  }
};


#define ___SOCK_KEEPALIVE_FLAG(options) (((options) & 1) != 0)
#define ___SOCK_NO_COALESCE_FLAG(options) (((options) & 2) != 0)
#define ___SOCK_REUSE_ADDRESS_FLAG(options) (((options) & 2048) != 0)


___HIDDEN ___SCMOBJ create_tcp_socket
   ___P((SOCKET_TYPE *sock,
         int options),
        (sock,
         options)
SOCKET_TYPE *sock;
int options;)
{
  int keepalive_flag = ___SOCK_KEEPALIVE_FLAG(options);
  int no_coalesce_flag = ___SOCK_NO_COALESCE_FLAG(options);
  int reuse_address_flag = ___SOCK_REUSE_ADDRESS_FLAG(options);
  SOCKET_TYPE s;

  if (SOCKET_CALL_ERROR2(s = socket (AF_INET, SOCK_STREAM, 0)))
    return ERR_CODE_FROM_SOCKET_CALL;

#ifndef TCP_NODELAY
#define TCP_NODELAY 1
#endif

  if ((keepalive_flag != 0 &&
       setsockopt (s, /* keep connection alive or not */
                   SOL_SOCKET,
                   SO_KEEPALIVE,
                   ___CAST(char*,&keepalive_flag),
                   sizeof (keepalive_flag)) != 0) ||
      (reuse_address_flag != 0 &&
       setsockopt (s, /* allow reusing the same address */
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   ___CAST(char*,&reuse_address_flag),
                   sizeof (reuse_address_flag)) != 0) ||
      (no_coalesce_flag != 0 &&
       setsockopt (s, /* enable or disable packet coalescing algorithm */
                   IPPROTO_TCP,
                   TCP_NODELAY,
                   ___CAST(char*,&no_coalesce_flag),
                   sizeof (no_coalesce_flag)) != 0))
    {
      ___SCMOBJ e = ERR_CODE_FROM_SOCKET_CALL;
      CLOSE_SOCKET(s); /* ignore error */
      return e;
    }

  *sock = s;

  return ___FIX(___NO_ERR);
}


___HIDDEN int set_socket_non_blocking
   ___P((SOCKET_TYPE s),
        (s)
SOCKET_TYPE s;)
{
#ifndef USE_ioctl
#undef FIONBIO
#endif

#ifdef FIONBIO

  unsigned long param = 1;

  return SOCKET_CALL_ERROR(IOCTL_SOCKET(s, FIONBIO, &param));

#else

  int fl;

  if ((fl = fcntl (s, F_GETFL, 0)) >= 0)
    fl = fcntl (s, F_SETFL, fl | O_NONBLOCK);

  return fl;

#endif
}


___SCMOBJ ___device_tcp_client_setup_from_socket
   ___P((___device_tcp_client **dev,
         ___device_group *dgroup,
         SOCKET_TYPE s,
         struct sockaddr *server_addr,
         SOCKET_LEN_TYPE server_addrlen,
         int try_connect_again,
         int direction),
        (dev,
         dgroup,
         s,
         server_addr,
         server_addrlen,
         try_connect_again,
         direction)
___device_tcp_client **dev;
___device_group *dgroup;
SOCKET_TYPE s;
struct sockaddr *server_addr;
SOCKET_LEN_TYPE server_addrlen;
int try_connect_again;
int direction;)
{
  ___SCMOBJ e;
  ___device_tcp_client *d;

  d = ___CAST(___device_tcp_client*,
              ___alloc_mem (sizeof (___device_tcp_client)));

  if (d == NULL)
    {
      CLOSE_SOCKET(s); /* ignore error */
      return ___FIX(___HEAP_OVERFLOW_ERR);
    }

  /*
   * Setup socket to perform nonblocking I/O.
   */

  if (set_socket_non_blocking (s) != 0) /* set nonblocking mode */
    {
      e = ERR_CODE_FROM_SOCKET_CALL;
      CLOSE_SOCKET(s); /* ignore error */
      ___free_mem (d);
      return e;
    }

  d->base.base.vtbl = &___device_tcp_client_table;
  d->s = s;
  d->server_addr = *server_addr;
  d->server_addrlen = server_addrlen;
  d->try_connect_again = try_connect_again;

#ifdef USE_POSIX

  d->try_connect_interval_nsecs = 1000000; /* 0.001 secs */

#endif

#ifdef USE_WIN32

  d->io_event =
    CreateEvent (NULL,  /* can't inherit */
                 TRUE,  /* manual reset */
                 FALSE, /* not signaled */
                 NULL); /* no name */

  if (d->io_event == NULL)
    {
      e = err_code_from_GetLastError ();
      CLOSE_SOCKET(s); /* ignore error */
      ___free_mem (d);
      return e;
    }

#endif

  *dev = d;

  return ___device_stream_setup
           (&d->base,
            dgroup,
            direction,
            0);
}


___SCMOBJ ___device_tcp_client_setup_from_sockaddr
   ___P((___device_tcp_client **dev,
         ___device_group *dgroup,
         struct sockaddr *server_addr,
         SOCKET_LEN_TYPE server_addrlen,
         int options,
         int direction),
        (dev,
         dgroup,
         server_addr,
         server_addrlen,
         options,
         direction)
___device_tcp_client **dev;
___device_group *dgroup;
struct sockaddr *server_addr;
SOCKET_LEN_TYPE server_addrlen;
int options;
int direction;)
{
  ___SCMOBJ e;
  SOCKET_TYPE s;
  ___device_tcp_client *d;

  if ((e = create_tcp_socket (&s, options)) != ___FIX(___NO_ERR))
    return e;

  if ((e = ___device_tcp_client_setup_from_socket
             (&d,
              dgroup,
              s,
              server_addr,
              server_addrlen,
              1,
              direction))
      != ___FIX(___NO_ERR))
    return e;

  *dev = d;

  if (try_connect (d) != 0)
    {
      e = ERR_CODE_FROM_SOCKET_CALL;
      ___device_cleanup (&d->base.base); /* ignore error */
      return e;
    }

  return ___FIX(___NO_ERR);
}

#endif


/*---------------------------------------------------------------------------*/

#ifdef USE_NETWORKING

/* TCP server device. */

typedef struct ___device_tcp_server_struct
  {
    ___device base;
    SOCKET_TYPE s;

#ifdef USE_WIN32

    HANDLE io_event;  /* used by ___device_tcp_server_select_raw_virt */

#endif
  } ___device_tcp_server;

typedef struct ___device_tcp_server_vtbl_struct
  {
    ___device_vtbl base;
  } ___device_tcp_server_vtbl;

___HIDDEN int ___device_tcp_server_kind
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___TCP_SERVER_DEVICE_KIND;
}

___HIDDEN ___SCMOBJ ___device_tcp_server_close_virt
   ___P((___device *self,
         int direction),
        (self,
         direction)
___device *self;
int direction;)
{
  ___device_tcp_server *d = ___CAST(___device_tcp_server*,self);

  if (d->base.read_stage == ___STAGE_CLOSED)
    return ___FIX(___NO_ERR);

  if (direction & ___DIRECTION_RD)
    {
      d->base.read_stage = ___STAGE_CLOSED; /* avoid multiple closes */

#ifdef USE_WIN32

      if (d->io_event != NULL)
        CloseHandle (d->io_event); /* ignore error */

#endif

      if (CLOSE_SOCKET(d->s) != 0)
        return ERR_CODE_FROM_SOCKET_CALL;
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_tcp_server_select_virt
   ___P((___device *self,
         ___BOOL for_writing,
         int i,
         int pass,
         ___device_select_state *state),
        (self,
         for_writing,
         i,
         pass,
         state)
___device *self;
___BOOL for_writing;
int i;
int pass;
___device_select_state *state;)
{
  ___device_tcp_server *d = ___CAST(___device_tcp_server*,self);
  int stage = (for_writing
               ? d->base.write_stage
               : d->base.read_stage);

  if (pass == ___SELECT_PASS_1)
    {
      if (stage != ___STAGE_OPEN)
        state->timeout = ___time_mod.time_neg_infinity;
      else
        {
#ifdef USE_POSIX
          ___device_select_add_fd (state, d->s, for_writing);
#endif

#ifdef USE_WIN32

          HANDLE wait_obj = d->io_event;

          ResetEvent (wait_obj); /* ignore error */

          WSAEventSelect (d->s, wait_obj, FD_ACCEPT);

          ___device_select_add_wait_obj (state, i, wait_obj);

#endif
        }

      return ___FIX(___SELECT_SETUP_DONE);
    }

  /* pass == ___SELECT_PASS_CHECK */

  if (stage != ___STAGE_OPEN)
    state->devs[i] = NULL;
  else
    {
#ifdef USE_POSIX

      if (FD_ISSET(d->s, &state->readfds))
        state->devs[i] = NULL;

#endif

#ifdef USE_WIN32

      if (state->devs_next[i] != -1)
        state->devs[i] = NULL;

#endif
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_tcp_server_release_virt
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_tcp_server_flush_write_virt
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___device_tcp_server_vtbl ___device_tcp_server_table =
{
  {
    ___device_tcp_server_kind,
    ___device_tcp_server_select_virt,
    ___device_tcp_server_release_virt,
    ___device_tcp_server_flush_write_virt,
    ___device_tcp_server_close_virt
  }
};


___SCMOBJ ___device_tcp_server_setup
   ___P((___device_tcp_server **dev,
         ___device_group *dgroup,
         struct sockaddr *server_addr,
         SOCKET_LEN_TYPE server_addrlen,
         int backlog,
         int options),
        (dev,
         dgroup,
         server_addr,
         server_addrlen,
         backlog,
         options)
___device_tcp_server **dev;
___device_group *dgroup;
struct sockaddr *server_addr;
SOCKET_LEN_TYPE server_addrlen;
int backlog;
int options;)
{
  ___SCMOBJ e;
  SOCKET_TYPE s;
  ___device_tcp_server *d;

  if ((e = create_tcp_socket (&s, options)) != ___FIX(___NO_ERR))
    return e;

  if (set_socket_non_blocking (s) != 0 || /* set nonblocking mode */
      bind (s, server_addr, server_addrlen) != 0 ||
      listen (s, backlog) != 0)
    {
      e = ERR_CODE_FROM_SOCKET_CALL;
      CLOSE_SOCKET(s); /* ignore error */
      return e;
    }

  d = ___CAST(___device_tcp_server*,
              ___alloc_mem (sizeof (___device_tcp_server)));

  if (d == NULL)
    {
      CLOSE_SOCKET(s); /* ignore error */
      return ___FIX(___HEAP_OVERFLOW_ERR);
    }

  d->base.vtbl = &___device_tcp_server_table;
  d->base.refcount = 1;
  d->base.direction = ___DIRECTION_RD;
  d->base.read_stage = ___STAGE_OPEN;
  d->base.write_stage = ___STAGE_CLOSED;

#ifdef USE_WIN32

  d->io_event =
    CreateEvent (NULL,  /* can't inherit */
                 TRUE,  /* manual reset */
                 FALSE, /* not signaled */
                 NULL); /* no name */

  if (d->io_event == NULL)
    {
      ___SCMOBJ e = err_code_from_GetLastError ();
      CLOSE_SOCKET(s); /* ignore error */
      ___free_mem (d);
      return e;
    }

#endif

  d->s = s;

  *dev = d;

  device_add_to_group (dgroup, &d->base);

  return ___FIX(___NO_ERR);
}


___SCMOBJ ___device_tcp_server_read
   ___P((___device_tcp_server *dev,
         ___device_group *dgroup,
         ___device_tcp_client **client),
        (dev,
         dgroup,
         client)
___device_tcp_server *dev;
___device_group *dgroup;
___device_tcp_client **client;)
{
  struct sockaddr_in addr;
  SOCKET_LEN_TYPE addrlen;
  SOCKET_TYPE s;

  if (dev->base.read_stage != ___STAGE_OPEN)
    return ___FIX(___CLOSED_DEVICE_ERR);

  addrlen = sizeof (addr);

  if (SOCKET_CALL_ERROR2(s = accept (dev->s,
                                     ___CAST(struct sockaddr*,&addr),
                                     &addrlen)))
    return ERR_CODE_FROM_SOCKET_CALL;

  return ___device_tcp_client_setup_from_socket
           (client,
            dgroup,
            s,
            ___CAST(struct sockaddr*,&addr),
            addrlen,
            0,
            ___DIRECTION_RD|___DIRECTION_WR);
}

#endif


/*---------------------------------------------------------------------------*/

/* Directory device. */

typedef struct ___device_directory_struct
  {
    ___device base;

    int ignore_hidden;

#ifdef USE_opendir
    DIR *dir;
#endif

#ifdef USE_FindFirstFile
    HANDLE h;
    WIN32_FIND_DATA fdata;
    ___BOOL first_file;
#endif
  } ___device_directory;

typedef struct ___device_directory_vtbl_struct
  {
    ___device_vtbl base;
  } ___device_directory_vtbl;

___HIDDEN int ___device_directory_kind
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___DIRECTORY_KIND;
}

___HIDDEN ___SCMOBJ ___device_directory_close_virt
   ___P((___device *self,
         int direction),
        (self,
         direction)
___device *self;
int direction;)
{
  ___device_directory *d = ___CAST(___device_directory*,self);

  if (d->base.read_stage == ___STAGE_CLOSED)
    return ___FIX(___NO_ERR);

  if (direction & ___DIRECTION_RD)
    {
      d->base.read_stage = ___STAGE_CLOSED; /* avoid multiple closes */

#ifdef USE_opendir
      if (closedir (d->dir) < 0)
        return err_code_from_errno ();
#endif

#ifdef USE_FindFirstFile
      if (!FindClose (d->h))
        return err_code_from_GetLastError ();
#endif
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_directory_select_virt
   ___P((___device *self,
         ___BOOL for_writing,
         int i,
         int pass,
         ___device_select_state *state),
        (self,
         for_writing,
         i,
         pass,
         state)
___device *self;
___BOOL for_writing;
int i;
int pass;
___device_select_state *state;)
{
  if (pass == ___SELECT_PASS_1)
    {
      state->timeout = ___time_mod.time_neg_infinity;
      return ___FIX(___SELECT_SETUP_DONE);
    }

  /* pass == ___SELECT_PASS_CHECK */

  state->devs[i] = NULL;

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_directory_release_virt
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_directory_flush_write_virt
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___device_directory_vtbl ___device_directory_table =
{
  {
    ___device_directory_kind,
    ___device_directory_select_virt,
    ___device_directory_release_virt,
    ___device_directory_flush_write_virt,
    ___device_directory_close_virt
  }
};


#ifdef USE_opendir
#define ___DIR_OPEN_PATH_CE_SELECT(latin1,utf8,ucs2,ucs4,wchar,native) native
#endif

#ifdef USE_FindFirstFile
#ifdef _UNICODE
#define ___DIR_OPEN_PATH_CE_SELECT(latin1,utf8,ucs2,ucs4,wchar,native) ucs2
#else
#define ___DIR_OPEN_PATH_CE_SELECT(latin1,utf8,ucs2,ucs4,wchar,native) native
#endif
#endif


#ifdef ___DIR_OPEN_PATH_CE_SELECT

___SCMOBJ ___device_directory_setup
   ___P((___device_directory **dev,
         ___device_group *dgroup,
         ___STRING_TYPE(___DIR_OPEN_PATH_CE_SELECT) path,
         int ignore_hidden),
        (dev,
         dgroup,
         path,
         ignore_hidden)
___device_directory **dev;
___device_group *dgroup;
___STRING_TYPE(___DIR_OPEN_PATH_CE_SELECT) path;
int ignore_hidden;)
{
  ___device_directory *d;

  d = ___CAST(___device_directory*,
              ___alloc_mem (sizeof (___device_directory)));

  if (d == NULL)
    return ___FIX(___HEAP_OVERFLOW_ERR);

  d->base.vtbl = &___device_directory_table;
  d->base.refcount = 1;
  d->base.direction = ___DIRECTION_RD;
  d->base.read_stage = ___STAGE_OPEN;
  d->base.write_stage = ___STAGE_CLOSED;

#ifdef USE_opendir

  d->ignore_hidden = ignore_hidden;

  d->dir = opendir (path);

  if (d->dir == NULL)
    {
      ___SCMOBJ e = fnf_or_err_code_from_errno ();
      ___free_mem (d);
      return e;
    }

#endif

#ifdef USE_FindFirstFile

  {
    ___CHAR_TYPE(___DIR_OPEN_PATH_CE_SELECT) dir[___PATH_MAX_LENGTH+2+1];
    int i = 0;

    while (path[i] != '\0' && i < ___PATH_MAX_LENGTH)
      {
        dir[i] = path[i];
        i++;
      }

    if (i == 0 || (dir[i-1] != '\\' && dir[i-1] != '/'))
      dir[i++] = '\\';

    dir[i++] = '*';
    dir[i++] = '\0';

    d->ignore_hidden = ignore_hidden;
    d->first_file = 1;

    d->h = FindFirstFile (dir, &d->fdata);

    if (d->h == INVALID_HANDLE_VALUE)
      {
        ___SCMOBJ e = fnf_or_err_code_from_GetLastError ();
        ___free_mem (d);
        return e;
      }
  }

#endif

  *dev = d;

  device_add_to_group (dgroup, &d->base);

  return ___FIX(___NO_ERR);
}

___SCMOBJ ___device_directory_read
   ___P((___device_directory *dev,
         char **name),
        (dev,
         name)
___device_directory *dev;
char **name;)
{
  ___SCMOBJ e;

  if (dev->base.read_stage != ___STAGE_OPEN)
    return ___FIX(___CLOSED_DEVICE_ERR);

  for (;;)
    {
      char *temp;

#ifdef USE_opendir

      struct dirent *de = readdir (dev->dir);

      if (de == NULL)
        {
#if 0
          /* this seems to be broken, at least under Linux */
          if (errno != 0)
            return err_code_from_errno ();
#endif
          *name = NULL;
          e = ___FIX(___NO_ERR);
          break;
        }

      temp = de->d_name;

      switch (dev->ignore_hidden)
        {
        default:
        case 2:
          if (temp[0] == '.')
            break;

        case 1:
          if (temp[0] == '.' &&
              (temp[1] == '\0' ||
               (temp[1] == '.' && (temp[2] == '\0'))))
            break;

        case 0:
          *name = temp;
          return ___FIX(___NO_ERR);
        }

#endif

#ifdef USE_FindFirstFile

      if (dev->first_file)
        dev->first_file = 0;
      else if (!FindNextFile (dev->h, &dev->fdata))
        {
          e = err_code_from_GetLastError ();
          *name = NULL;
          if (e == ___FIX(___WIN32_ERR(ERROR_NO_MORE_FILES)))
            e = ___FIX(___NO_ERR);
          break;
        }

      temp = dev->fdata.cFileName;

      if (temp[0] == '\0')
        temp = dev->fdata.cAlternateFileName; /* use 8.3 name */

      switch (dev->ignore_hidden)
        {
        default:
        case 2:
          if (dev->fdata.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
            break;

        case 1:
          if (temp[0] == '.' &&
              (temp[1] == '\0' ||
               (temp[1] == '.' && (temp[2] == '\0'))))
            break;

        case 0:
          *name = temp;
          return ___FIX(___NO_ERR);
        }

#endif
    }

  return e;
}

#endif


/*---------------------------------------------------------------------------*/

/* Event-queue device. */

typedef struct ___device_event_queue_struct
  {
    ___device base;

    int index;

#if 0
#endif
  } ___device_event_queue;

typedef struct ___device_event_queue_vtbl_struct
  {
    ___device_vtbl base;
  } ___device_event_queue_vtbl;

___HIDDEN int ___device_event_queue_kind
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___EVENT_QUEUE_KIND;
}

___HIDDEN ___SCMOBJ ___device_event_queue_close_virt
   ___P((___device *self,
         int direction),
        (self,
         direction)
___device *self;
int direction;)
{
  ___device_event_queue *d = ___CAST(___device_event_queue*,self);

  if (d->base.read_stage == ___STAGE_CLOSED)
    return ___FIX(___NO_ERR);

  if (direction & ___DIRECTION_RD)
    {
      d->base.read_stage = ___STAGE_CLOSED; /* avoid multiple closes */

#if 0
#endif
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_event_queue_select_virt
   ___P((___device *self,
         ___BOOL for_writing,
         int i,
         int pass,
         ___device_select_state *state),
        (self,
         for_writing,
         i,
         pass,
         state)
___device *self;
___BOOL for_writing;
int i;
int pass;
___device_select_state *state;)
{
  if (pass == ___SELECT_PASS_1)
    {
      state->timeout = ___time_mod.time_neg_infinity;
      return ___FIX(___SELECT_SETUP_DONE);
    }

  /* pass == ___SELECT_PASS_CHECK */

  state->devs[i] = NULL;

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_event_queue_release_virt
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_event_queue_flush_write_virt
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___device_event_queue_vtbl ___device_event_queue_table =
{
  {
    ___device_event_queue_kind,
    ___device_event_queue_select_virt,
    ___device_event_queue_release_virt,
    ___device_event_queue_flush_write_virt,
    ___device_event_queue_close_virt
  }
};


___SCMOBJ ___device_event_queue_setup
   ___P((___device_event_queue **dev,
         ___device_group *dgroup,
         int index),
        (dev,
         dgroup,
         index)
___device_event_queue **dev;
___device_group *dgroup;
int index;)
{
  ___device_event_queue *d;

  d = ___CAST(___device_event_queue*,
              ___alloc_mem (sizeof (___device_event_queue)));

  if (d == NULL)
    return ___FIX(___HEAP_OVERFLOW_ERR);

  d->base.vtbl = &___device_event_queue_table;
  d->base.refcount = 1;
  d->base.direction = ___DIRECTION_RD;
  d->base.read_stage = ___STAGE_OPEN;
  d->base.write_stage = ___STAGE_CLOSED;

  d->index = index;

#if 0
#endif

  *dev = d;

  device_add_to_group (dgroup, &d->base);

  return ___FIX(___NO_ERR);
}

___SCMOBJ ___device_event_queue_read
   ___P((___device_event_queue *dev,
         ___SCMOBJ *event),
        (dev,
         event)
___device_event_queue *dev;
___SCMOBJ *event;)
{
  void *ev = NULL;
  ___SCMOBJ result;

  if (dev->base.read_stage != ___STAGE_OPEN)
    return ___FIX(___CLOSED_DEVICE_ERR);

  if (ev == NULL)
    {
      *event = ___EOF;
      return ___FIX(___NO_ERR);
    }

  return ___NONNULLPOINTER_to_SCMOBJ
           (ev,
            ___FAL,
            ___release_pointer,
            event,
            ___RETURN_POS);
}


/*---------------------------------------------------------------------------*/

/* File stream device */

typedef struct ___device_file_struct
  {
    ___device_stream base;

#ifndef USE_POSIX
#ifndef USE_WIN32

    ___FILE *stream;

#endif
#endif

#ifdef USE_POSIX
    int fd;
#endif

#ifdef USE_WIN32
    HANDLE h;
    int flags;
#endif
  } ___device_file;

typedef struct ___device_file_vtbl_struct
  {
    ___device_stream_vtbl base;
  } ___device_file_vtbl;

___HIDDEN int ___device_file_kind
   ___P((___device *self),
        (self)
___device *self;)
{
  return ___FILE_DEVICE_KIND;
}

___HIDDEN ___SCMOBJ ___device_file_close_raw_virt
   ___P((___device_stream *self,
         int direction),
        (self,
         direction)
___device_stream *self;
int direction;)
{
  ___device_file *d = ___CAST(___device_file*,self);
  int is_not_closed = 0;

  if (d->base.base.read_stage != ___STAGE_CLOSED)
    is_not_closed |= ___DIRECTION_RD;

  if (d->base.base.write_stage != ___STAGE_CLOSED)
    is_not_closed |= ___DIRECTION_WR;

  if (is_not_closed == 0)
    return ___FIX(___NO_ERR);

  if ((is_not_closed & ~direction) == 0)
    {
      /* Close file when both sides are closed. */

      d->base.base.read_stage = ___STAGE_CLOSED; /* avoid multiple closes */
      d->base.base.write_stage = ___STAGE_CLOSED;

#ifndef USE_POSIX
#ifndef USE_WIN32

      if (d->stream != 0)
        if (___fclose (d->stream) != 0)
          return err_code_from_errno ();

#endif
#endif

#ifdef USE_POSIX
      if (close (d->fd) < 0)
        return err_code_from_errno ();
#endif

#ifdef USE_WIN32
      if (!CloseHandle (d->h))
        return err_code_from_GetLastError ();
#endif
    }
  else if (is_not_closed & direction & ___DIRECTION_RD)
    d->base.base.read_stage = ___STAGE_CLOSED;
  else if (is_not_closed & direction & ___DIRECTION_WR)
    d->base.base.write_stage = ___STAGE_CLOSED;

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_file_select_raw_virt
   ___P((___device_stream *self,
         ___BOOL for_writing,
         int i,
         int pass,
         ___device_select_state *state),
        (self,
         for_writing,
         i,
         pass,
         state)
___device_stream *self;
___BOOL for_writing;
int i;
int pass;
___device_select_state *state;)
{
  ___device_file *d = ___CAST(___device_file*,self);
  int stage = (for_writing
               ? d->base.base.write_stage
               : d->base.base.read_stage);

  if (pass == ___SELECT_PASS_1)
    {
      if (stage != ___STAGE_OPEN)
        state->timeout = ___time_mod.time_neg_infinity;
      else
        {
#ifndef USE_POSIX
#ifndef USE_WIN32

        state->timeout = ___time_mod.time_neg_infinity;

#endif
#endif

#ifdef USE_POSIX
          ___device_select_add_fd (state, d->fd, for_writing);
#endif
        }
      return ___FIX(___SELECT_SETUP_DONE);
    }

  /* pass == ___SELECT_PASS_CHECK */

  if (stage != ___STAGE_OPEN)
    state->devs[i] = NULL;
  else
    {
#ifndef USE_POSIX
#ifndef USE_WIN32

      state->devs[i] = NULL;

#endif
#endif

#ifdef USE_POSIX

      if (for_writing
           ? FD_ISSET(d->fd, &state->writefds)
           : FD_ISSET(d->fd, &state->readfds))
        state->devs[i] = NULL;

#endif

#ifdef USE_WIN32

      if (state->devs_next[i] != -1)
        state->devs[i] = NULL;

#endif
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_file_release_raw_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_file_flush_write_raw_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  ___device_file *d = ___CAST(___device_file*,self);

  if (d->base.base.write_stage == ___STAGE_OPEN)
    {
#ifndef USE_POSIX
#ifndef USE_WIN32

      ___FILE *stream = d->stream;

      if (stream == 0)
        stream = ___stdout;

      ___fflush (stream); /* ignore error */

#endif
#endif

#ifdef USE_POSIX
#if 0
      if (fsync (d->fd) < 0)/************only works on disk files!!!!!!!*/
        return err_code_from_errno ();
#endif
#endif
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_file_seek_raw_virt
   ___P((___device_stream *self,
         ___stream_index *pos,
         int whence),
        (self,
         pos,
         whence)
___device_stream *self;
___stream_index *pos;
int whence;)
{
  ___device_file *d = ___CAST(___device_file*,self);

  if (d->base.base.read_stage == ___STAGE_OPEN ||
      d->base.base.write_stage == ___STAGE_OPEN)
    {
#ifndef USE_POSIX
#ifndef USE_WIN32

      int new_pos;
      ___FILE *stream = d->stream;

      if (stream == 0)
        stream = ___stdout;

      if (fseek (stream, *pos, whence) < 0 ||
          (new_pos = ftell (stream)) < 0)
        return err_code_from_errno ();

      *pos = new_pos;

#endif
#endif

#ifdef USE_POSIX

      int new_pos;

      if ((new_pos = lseek (d->fd, *pos, whence)) < 0)
        return err_code_from_errno ();

      *pos = new_pos;

#endif

#ifdef USE_WIN32

      LARGE_INTEGER new_pos;

      new_pos.QuadPart = *pos;

      new_pos.LowPart = SetFilePointer (d->h,
                                        new_pos.LowPart,
                                        &new_pos.HighPart,
                                        whence);

      if (new_pos.LowPart == 0xFFFFFFFF
          && GetLastError () != NO_ERROR)
        return err_code_from_GetLastError ();

      *pos = new_pos.LowPart;  /************* incomplete... support large offsets */

#endif
    }

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_file_read_raw_virt
   ___P((___device_stream *self,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done),
        (self,
         buf,
         len,
         len_done)
___device_stream *self;
___U8 *buf;
___stream_index len;
___stream_index *len_done;)
{
  ___device_file *d = ___CAST(___device_file*,self);

  if (d->base.base.read_stage != ___STAGE_OPEN)
    return ___FIX(___CLOSED_DEVICE_ERR);

#ifndef USE_POSIX
#ifndef USE_WIN32

  {
    int n;
    ___FILE *stream = d->stream;

    if (stream == 0)
      stream = ___stdin;

    if (stream == ___stdin)
      len = 1; /* only read 1 byte at a time to prevent blocking on tty */

    if ((n = fread (buf, 1, len, stream)) == 0)
      {
        if (ferror (stream))
          {
            clearerr (stream);
            return ___FIX(___UNKNOWN_ERR);
          }
        clearerr (stream);
      }

    *len_done = n;
  }

#endif
#endif

#ifdef USE_POSIX

  {
    int n;

    if ((n = read (d->fd, buf, len)) < 0)
      return err_code_from_errno ();

    *len_done = n;
  }

#endif

#ifdef USE_WIN32

  {
    DWORD n;

    if (!ReadFile (d->h, buf, len, &n, NULL))
      return err_code_from_GetLastError ();

    *len_done = n;
  }

#endif

  return ___FIX(___NO_ERR);
}

___HIDDEN ___SCMOBJ ___device_file_write_raw_virt
   ___P((___device_stream *self,
         ___U8 *buf,
         ___stream_index len,
         ___stream_index *len_done),
        (self,
         buf,
         len,
         len_done)
___device_stream *self;
___U8 *buf;
___stream_index len;
___stream_index *len_done;)
{
  ___device_file *d = ___CAST(___device_file*,self);

  if (d->base.base.write_stage != ___STAGE_OPEN)
    return ___FIX(___CLOSED_DEVICE_ERR);

#ifndef USE_POSIX
#ifndef USE_WIN32

  {
    int n;
    ___FILE *stream = d->stream;

    if (stream == 0)
      stream = ___stdout;

    if ((n = fwrite (buf, 1, len, stream)) == 0)
      {
        if (ferror (stream))
          {
            clearerr (stream);
            return ___FIX(___UNKNOWN_ERR);
          }
      }

    *len_done = n;
  }

#endif
#endif

#ifdef USE_POSIX

  {
    int n;

    if ((n = write (d->fd, buf, len)) < 0)
      return err_code_from_errno ();

    *len_done = n;
  }

#endif

#ifdef USE_WIN32

  {
    DWORD n;

    if (d->flags & (1 << 3))
      {
        ___stream_index pos = 0; /* end of file */
        ___SCMOBJ e = ___device_file_seek_raw_virt (self, &pos, FILE_END);
        if (e != ___FIX(___NO_ERR))
          return e;
      }

    if (!WriteFile (d->h, buf, len, &n, NULL))
      return err_code_from_GetLastError ();

    *len_done = n;
  }

#endif

  return ___FIX(___NO_ERR);
}


___HIDDEN ___SCMOBJ ___device_file_width_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  return ___FIX(80);
}


___HIDDEN ___SCMOBJ ___device_file_default_options_virt
   ___P((___device_stream *self),
        (self)
___device_stream *self;)
{
  int settings = ___setup_params.file_settings;
  int char_encoding = ___CHAR_ENCODING(settings);
  int eol_encoding = ___EOL_ENCODING(settings);
  int buffering = ___BUFFERING(settings);

  if (char_encoding == 0) char_encoding = ___CHAR_ENCODING_ISO_8859_1;
  if (eol_encoding == 0) eol_encoding = ___EOL_ENCODING_LF;

#ifdef USE_WIN32

  if (buffering == 0)
    {
      ___device_file *d = ___CAST(___device_file*,self);

      if (GetFileType (d->h) == FILE_TYPE_PIPE)
        buffering = ___NO_BUFFERING;
      else
        buffering = ___FULL_BUFFERING;
    }

#else

  if (buffering == 0) buffering = ___FULL_BUFFERING;

#endif

#ifdef ___DEBUG

  ___printf ("file char_encoding=%d   eol_encoding=%d   buffering=%d\n",
             char_encoding,
             eol_encoding,
             buffering);

#endif

  return ___FIX(___STREAM_OPTIONS(char_encoding,
                                  eol_encoding,
                                  buffering,
                                  char_encoding,
                                  eol_encoding,
                                  buffering));
}


___HIDDEN ___SCMOBJ ___device_file_options_set_virt
   ___P((___device_stream *self,
         ___SCMOBJ options),
        (self,
         options)
___device_stream *self;
___SCMOBJ options;)
{
  return ___FIX(___NO_ERR);
}


___HIDDEN ___device_file_vtbl ___device_file_table =
{
  {
    {
      ___device_file_kind,
      ___device_stream_select_virt,
      ___device_stream_release_virt,
      ___device_stream_flush_write_virt,
      ___device_stream_close_virt
    },
    ___device_file_select_raw_virt,
    ___device_file_release_raw_virt,
    ___device_file_flush_write_raw_virt,
    ___device_file_close_raw_virt,
    ___device_file_seek_raw_virt,
    ___device_file_read_raw_virt,
    ___device_file_write_raw_virt,
    ___device_file_width_virt,
    ___device_file_default_options_virt,
    ___device_file_options_set_virt
  }
};


#ifndef USE_POSIX
#ifndef USE_WIN32

___HIDDEN ___SCMOBJ ___device_file_setup_from_stream
   ___P((___device_file **dev,
         ___device_group *dgroup,
         ___FILE *stream,
         int direction),
        (dev,
         dgroup,
         stream,
         direction)
___device_file **dev;
___device_group *dgroup;
___FILE *stream;
int direction;)
{
  ___device_file *d;

  d = ___CAST(___device_file*,
              ___alloc_mem (sizeof (___device_file)));

  if (d == NULL)
    {
      ___fclose (stream); /* ignore error */
      return ___FIX(___HEAP_OVERFLOW_ERR);
    }

  d->base.base.vtbl = &___device_file_table;
  d->stream = stream;

  *dev = d;

  return ___device_stream_setup
           (&d->base,
            dgroup,
            direction,
            0);
}

#endif
#endif


#ifdef USE_POSIX

___HIDDEN ___SCMOBJ ___device_file_setup_from_fd
   ___P((___device_file **dev,
         ___device_group *dgroup,
         int fd,
         int direction),
        (dev,
         dgroup,
         fd,
         direction)
___device_file **dev;
___device_group *dgroup;
int fd;
int direction;)
{
  ___device_file *d;

  d = ___CAST(___device_file*,
              ___alloc_mem (sizeof (___device_file)));

  if (d == NULL)
    {
      close (fd); /* ignore error */
      return ___FIX(___HEAP_OVERFLOW_ERR);
    }

  d->base.base.vtbl = &___device_file_table;
  d->fd = fd;

  *dev = d;

  return ___device_stream_setup
           (&d->base,
            dgroup,
            direction,
            0);
}

#endif


#ifdef USE_WIN32

___HIDDEN ___SCMOBJ ___device_file_setup_from_handle
   ___P((___device_file **dev,
         ___device_group *dgroup,
         HANDLE h,
         int flags,
         int direction,
         int pumps_on),
        (dev,
         dgroup,
         h,
         flags,
         direction,
         pumps_on)
___device_file **dev;
___device_group *dgroup;
HANDLE h;
int flags;
int direction;
int pumps_on;)
{
  ___device_file *d;

  d = ___CAST(___device_file*,
              ___alloc_mem (sizeof (___device_file)));

  if (d == NULL)
    {
      CloseHandle (h); /* ignore error */
      return ___FIX(___HEAP_OVERFLOW_ERR);
    }

  d->base.base.vtbl = &___device_file_table;
  d->h = h;
  d->flags = flags;

  *dev = d;

  return ___device_stream_setup
           (&d->base,
            dgroup,
            direction,
            pumps_on);
}

#endif


/*---------------------------------------------------------------------------*/

#ifndef USE_POSIX
#ifndef USE_WIN32

___SCMOBJ ___device_stream_setup_from_stream
   ___P((___device_stream **dev,
         ___device_group *dgroup,
         ___FILE *stream,
         int kind,
         int direction),
        (dev,
         dgroup,
         ___FILE *stream,
         kind,
         direction)
___device_stream **dev;
___device_group *dgroup;
___FILE *stream;
int kind;
int direction;)
{
  ___SCMOBJ e;
  ___device_file *d;

  if ((e = ___device_file_setup_from_stream
             (&d,
              dgroup,
              stream,
              direction))
      == ___FIX(___NO_ERR))
    *dev = ___CAST(___device_stream*,d);

  return e;
}


___HIDDEN void device_translate_flags
   ___P((int flags,
         char **mode,
         int *direction),
        (flags,
         mode,
         direction)
int flags;
char **mode;
int *direction;)
{
  switch ((flags >> 4) & 3)
    {
    default:
    case 1:
      *mode = "rb";
      *direction = ___DIRECTION_RD;
      break;
    case 2:
      *mode = "wb";
      *direction = ___DIRECTION_WR;
      break;
    case 3:
      *mode = "w+b";
      *direction = ___DIRECTION_RD|___DIRECTION_WR;
      break;
    }
}

#endif
#endif


#ifdef USE_POSIX

___HIDDEN int ___device_stream_kind_from_fd
   ___P((int fd),
        (fd)
int fd;)
{
  /*
   * Determine what kind of device is attached to the file descriptor
   * (tty, socket, or regular file).
   */

  if (isatty (fd))
    return ___TTY_DEVICE_KIND;

#ifdef USE_stat

  {
    struct stat s;

    if (fstat (fd, &s) < 0)
      return ___NONE_KIND;

    if (S_ISREG(s.st_mode))
      return ___FILE_DEVICE_KIND;

#if 0

    if (S_ISDIR(s.st_mode))
      return ???;

    if (S_ISLNK(s.st_mode))
      return ???;

#endif

    if (S_ISCHR(s.st_mode))
      return ___FILE_DEVICE_KIND;

    if (S_ISBLK(s.st_mode))
      return ___FILE_DEVICE_KIND;

    if (S_ISFIFO(s.st_mode))
      return ___FILE_DEVICE_KIND;

#ifdef USE_NETWORKING
    if (S_ISSOCK(s.st_mode))
      return ___TCP_CLIENT_DEVICE_KIND;
#endif
  }

  return ___NONE_KIND;

#else

  return ___FILE_DEVICE_KIND;

#endif
}


___HIDDEN int ___device_stream_direction_from_fd
   ___P((int fd),
        (fd)
int fd;)
{
  int direction = ___DIRECTION_RD|___DIRECTION_WR;
  char buf[1];

  /*
   * A "read" and "write" of 0 bytes is attempted to tell which
   * directions the file descriptor supports.
   */

  if (read (fd, buf, 0) < 0)
    direction &= ~___DIRECTION_RD;

  if (write (fd, buf, 0) < 0)
    direction &= ~___DIRECTION_WR;

  /*
   * It is likely that on some systems a zero length "read" and
   * "write" will return an error, regardless of the possible
   * operations.  In this case assume that both operations are
   * possible.
   */

  if (direction == 0)
    direction = ___DIRECTION_RD|___DIRECTION_WR;

  return direction;
}


___SCMOBJ ___device_stream_setup_from_fd
   ___P((___device_stream **dev,
         ___device_group *dgroup,
         int fd,
         int kind,
         int direction),
        (dev,
         dgroup,
         fd,
         kind,
         direction)
___device_stream **dev;
___device_group *dgroup;
int fd;
int kind;
int direction;)
{
  ___SCMOBJ e = ___FIX(___UNKNOWN_ERR);

  if (kind == ___NONE_KIND)
    kind = ___device_stream_kind_from_fd (fd);

  if (direction == 0)
    direction = ___device_stream_direction_from_fd (fd);

#ifdef ___DEBUG
  ___printf ("fd=%d kind=%d direction=%d\n", fd, kind, direction);
#endif

  switch (kind)
    {
    case ___TTY_DEVICE_KIND:
      {
        ___device_tty *d;
        if ((e = ___device_tty_setup_from_fd
                   (&d,
                    dgroup,
                    fd,
                    direction))
            == ___FIX(___NO_ERR))
          *dev = ___CAST(___device_stream*,d);
        break;
      }

#ifdef USE_NETWORKING

    case ___TCP_CLIENT_DEVICE_KIND:
      {
        ___device_tcp_client *d;
        struct sockaddr server_addr;
        if ((e = ___device_tcp_client_setup_from_socket
                   (&d,
                    dgroup,
                    fd,
                    &server_addr,
                    0,
                    0,
                    direction))
            == ___FIX(___NO_ERR))
          *dev = ___CAST(___device_stream*,d);
        break;
      }

#endif

    case ___FILE_DEVICE_KIND:
      {
        ___device_file *d;
        if ((e = ___device_file_setup_from_fd
                   (&d,
                    dgroup,
                    fd,
                    direction))
            == ___FIX(___NO_ERR))
          *dev = ___CAST(___device_stream*,d);
        break;
      }

    default:
      {
        close (fd); /* ignore error */
        e = ___FIX(___UNKNOWN_ERR);
        break;
      }
    }

  return e;
}


___HIDDEN void device_translate_flags
   ___P((int flags,
         int *fl,
         int *direction),
        (flags,
         fl,
         direction)
int flags;
int *fl;
int *direction;)
{
  int f;

  switch ((flags >> 4) & 3)
    {
    default:
    case 1:
      f = O_RDONLY;
      *direction = ___DIRECTION_RD;
      break;
    case 2:
      f = O_WRONLY;
      *direction = ___DIRECTION_WR;
      break;
    case 3:
      f = O_RDWR;
      *direction = ___DIRECTION_RD|___DIRECTION_WR;
      break;
    }

  if (flags & (1 << 3))
    f |= O_APPEND;

  switch ((flags >> 1) & 3)
    {
    default:
    case 0: break;
    case 1: f |= O_CREAT; break;
    case 2: f |= O_CREAT|O_EXCL; break;
    }

  if (flags & 1)
    f |= O_TRUNC;

  f |= O_NONBLOCK;

  *fl = f;
}

#endif


#ifdef USE_WIN32

___HIDDEN int ___device_stream_kind_from_handle
   ___P((HANDLE h),
        (h)
HANDLE h;)
{
  DWORD n;
  CONSOLE_CURSOR_INFO cinfo;
  DCB dcb;
  BY_HANDLE_FILE_INFORMATION finfo;

#ifdef ___DEBUG
  ___printf ("GetFileType -> %d\n", ___CAST(int,GetFileType (h)));
#endif

  if (GetNumberOfConsoleInputEvents (h, &n))
    return ___TTY_DEVICE_KIND;

  if (GetConsoleCursorInfo (h, &cinfo))
    return ___TTY_DEVICE_KIND;

  if (GetCommState (h, &dcb))
    return ___SERIAL_DEVICE_KIND;

  if (GetFileType (h) == FILE_TYPE_PIPE)
    return ___PIPE_DEVICE_KIND;

  if (GetFileInformationByHandle (h, &finfo))
    return ___FILE_DEVICE_KIND;

  return ___NONE_KIND;
}


___HIDDEN int ___device_stream_direction_from_handle
   ___P((HANDLE h),
        (h)
HANDLE h;)
{
  DWORD n;
  int direction = ___DIRECTION_RD|___DIRECTION_WR;

  /*
   * A "ReadFile" and "WriteFile" of 0 bytes is attempted to tell which
   * directions the file handle supports.
   */

  if (!ReadFile (h, NULL, 0, &n, NULL))
    direction &= ~___DIRECTION_RD;

  if (!WriteFile (h, NULL, 0, &n, NULL))
    direction &= ~___DIRECTION_WR;

  /*
   * It is likely that on some systems a zero length "ReadFile" and
   * "WriteFile" will return an error, regardless of the possible
   * operations.  In this case assume that both operations are
   * possible.
   */

  if (direction == 0)
    direction = ___DIRECTION_RD|___DIRECTION_WR;

  return direction;
}


___SCMOBJ ___device_stream_setup_from_handle
   ___P((___device_stream **dev,
         ___device_group *dgroup,
         HANDLE h,
         int flags,
         int kind,
         int direction),
        (dev,
         dgroup,
         h,
         flags,
         kind,
         direction)
___device_stream **dev;
___device_group *dgroup;
HANDLE h;
int flags;
int kind;
int direction;)
{
  ___SCMOBJ e = ___FIX(___UNKNOWN_ERR);

  if (kind == ___NONE_KIND)
    kind = ___device_stream_kind_from_handle (h);

  if (direction == 0)
    direction = ___device_stream_direction_from_handle (h);

#ifdef ___DEBUG
  ___printf ("kind=%d direction=%d\n", kind, direction);
#endif

  switch (kind)
    {
    case ___TTY_DEVICE_KIND:
      {
        ___device_tty *d;
        if ((e = ___device_tty_setup_from_console
                   (&d,
                    dgroup,
                    direction))
            == ___FIX(___NO_ERR))
          *dev = ___CAST(___device_stream*,d);
        break;
      }

    case ___SERIAL_DEVICE_KIND:
      {
        ___device_serial *d;
        if ((e = ___device_serial_setup_from_handle
                   (&d,
                    dgroup,
                    h,
                    direction))
            == ___FIX(___NO_ERR))
          *dev = ___CAST(___device_stream*,d);
        break;
      }

    case ___FILE_DEVICE_KIND:
    case ___PIPE_DEVICE_KIND:
      {
        ___device_file *d;
        if ((e = ___device_file_setup_from_handle
                   (&d,
                    dgroup,
                    h,
                    flags,
                    direction,
                    (kind == ___FILE_DEVICE_KIND)
                    ? 0
                    : (___DIRECTION_RD|___DIRECTION_WR)))
            == ___FIX(___NO_ERR))
          *dev = ___CAST(___device_stream*,d);
        break;
      }

    default:
      {
        CloseHandle (h); /* ignore error */
        e = ___FIX(___UNKNOWN_ERR);
        break;
      }
    }

  return e;
}


___HIDDEN void device_translate_flags
   ___P((int flags,
         DWORD *access_mode,
         DWORD *share_mode,
         DWORD *creation_mode,
         DWORD *attributes,
         int *direction),
        (flags,
         access_mode,
         share_mode,
         creation_mode,
         attributes,
         direction)
int flags;
DWORD *access_mode;
DWORD *share_mode;
DWORD *creation_mode;
DWORD *attributes;
int *direction;)
{
  DWORD am;
  DWORD cm;

  switch ((flags >> 4) & 3)
    {
    default:
    case 1:
      am = GENERIC_READ;
      *direction = ___DIRECTION_RD;
      break;
    case 2:
      am = GENERIC_WRITE;
      *direction = ___DIRECTION_WR;
      break;
    case 3:
      am = GENERIC_READ|GENERIC_WRITE;
      *direction = ___DIRECTION_RD|___DIRECTION_WR;
      break;
    }

  switch ((flags >> 1) & 3)
    {
    default:
    case 0:
      if (flags & 1)
        cm = TRUNCATE_EXISTING;
      else
        cm = OPEN_EXISTING;
      break;

    case 1:
      if (flags & 1)
        cm = CREATE_ALWAYS;
      else
        cm = OPEN_ALWAYS;
      break;

    case 2:
      cm = CREATE_NEW;
      break;
    }

  *access_mode = am;
  *share_mode = FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;
  *creation_mode = cm;
  *attributes = FILE_ATTRIBUTE_NORMAL;
}

#endif


/*---------------------------------------------------------------------------*/

#ifdef USE_execvp
#define ___STREAM_OPEN_PROCESS_CE_SELECT(latin1,utf8,ucs2,ucs4,wchar,native) native
#endif

#ifdef USE_CreateProcess
#ifdef _UNICODE
#define ___STREAM_OPEN_PROCESS_CE_SELECT(latin1,utf8,ucs2,ucs4,wchar,native) ucs2
#else
#define ___STREAM_OPEN_PROCESS_CE_SELECT(latin1,utf8,ucs2,ucs4,wchar,native) native
#endif
#endif


#ifdef ___STREAM_OPEN_PROCESS_CE_SELECT

#ifdef USE_execvp

/**********************************/
#define USE_pipe

typedef struct half_duplex_pipe
  {
    int reading_fd;
    int writing_fd;
  } half_duplex_pipe;

typedef struct full_duplex_pipe
  {
    half_duplex_pipe input;
    half_duplex_pipe output;
  } full_duplex_pipe;


___HIDDEN int open_half_duplex_pipe
   ___P((half_duplex_pipe *hdp),
        (hdp)
half_duplex_pipe *hdp;)
{
  int fds[2];

#ifdef USE_pipe
  if (pipe (fds) < 0)
    return -1;
#endif

#ifdef USE_socketpair
  if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds) < 0)
    return -1;
#endif

  hdp->reading_fd = fds[0];
  hdp->writing_fd = fds[1];

  return 0;
}

___HIDDEN void close_half_duplex_pipe
   ___P((half_duplex_pipe *hdp,
         int end),
        (hdp,
         end)
half_duplex_pipe *hdp;
int end;)
{
  if (end != 1 && hdp->reading_fd >= 0)
    {
      close (hdp->reading_fd); /* ignore error */
      hdp->reading_fd = -1;
    }

  if (end != 0 && hdp->writing_fd >= 0)
    {
      close (hdp->writing_fd); /* ignore error */
      hdp->writing_fd = -1;
    }
}

___HIDDEN int open_pseudo_terminal_master
   ___P((int *master_fd_ptr,
         int *slave_fd_ptr),
        (master_fd_ptr,
         slave_fd_ptr)
int *master_fd_ptr;
int *slave_fd_ptr;)
{
#ifdef USE_openpty

  return openpty (master_fd_ptr, slave_fd_ptr, NULL, NULL, NULL);

#else
#ifdef USE_getpt

  *slave_fd_ptr = -1;
  return *master_fd_ptr = getpt ();

#else

  *slave_fd_ptr = -1;
  return *master_fd_ptr = open ("/dev/ptmx", O_RDWR);

#endif
#endif
}


#ifndef USE_openpty
/***************************/
#define __USE_XOPEN
#define __USE_GNU
#include <stdlib.h>
#include <stropts.h>
extern char *ptsname (int __fd);
#endif

___HIDDEN int setup_terminal_slave
   ___P((int slave_fd),
        (slave_fd)
int slave_fd;)
{
  struct termios tios;

  if (tcgetattr (slave_fd, &tios) >= 0)
    {
      tios.c_lflag &= ~(ECHO | ECHOCTL | ICANON | IEXTEN | ISIG);
      tios.c_iflag &= ~(BRKINT | INLCR | ICRNL | INPCK | ISTRIP | IXON | IXOFF);
      tios.c_cflag &= ~(CSIZE | PARENB | CLOCAL);
      tios.c_cflag |= (CS8 | HUPCL);
#ifndef OCRNL
#define OCRNL 0
#endif
      tios.c_oflag &= ~(OPOST | ONLCR | OCRNL);

      if (tcsetattr (slave_fd, TCSANOW, &tios) >= 0)
        return 0;
    }

  return -1;
}


___HIDDEN int open_pseudo_terminal_slave
   ___P((int master_fd,
         int *slave_fd),
        (master_fd,
         slave_fd)
int master_fd;
int *slave_fd;)
{
#ifndef USE_openpty
#ifndef USE_ptsname

  errno = xxx;/********************/
  return -1;

#endif
#endif

#ifdef USE_openpty

  return 0;

#endif

#ifdef USE_ptsname

  int fd;
  char *name;

  if (grantpt (master_fd) >= 0 &&
      unlockpt (master_fd) >= 0 &&
      (name = ptsname (master_fd)) != NULL &&
      (fd = open (name, O_RDWR)) >= 0)
    {
      int tmp;

      if (!isastream (fd) ||
          (ioctl (fd, I_PUSH, "ptem") >= 0 &&
           ioctl (fd, I_PUSH, "ldterm") >= 0))
        {
          *slave_fd = fd;
          return 0;
        }

      tmp = errno;
      close (fd); /* ignore error */
      errno = tmp;
    }

  return -1;

#endif
}


___HIDDEN int open_full_duplex_pipe1
   ___P((full_duplex_pipe *fdp,
         ___BOOL use_pty),
        (fdp,
         use_pty)
full_duplex_pipe *fdp;
___BOOL use_pty;)
{
  fdp->input.reading_fd = -1;
  fdp->input.writing_fd = -1;
  fdp->output.reading_fd = -1;
  fdp->output.writing_fd = -1;

  if (use_pty)
    {
      int master_fd;
      int slave_fd;
      if (open_pseudo_terminal_master (&master_fd, &slave_fd) >= 0)
        {
          int master_fd_dup;
          int tmp;
          if ((master_fd_dup = dup (master_fd)) >= 0)
            {
              fdp->input.writing_fd = master_fd;
              fdp->output.reading_fd = master_fd_dup;
              fdp->input.reading_fd = slave_fd;
              return 0;
            }
          tmp = errno;
          close (master_fd); /* ignore error */
          if (slave_fd >= 0)
            close (slave_fd); /* ignore error */
          errno = tmp;
        }
    }
  else
    {
      if (open_half_duplex_pipe (&fdp->input) >= 0)
        {
          if (open_half_duplex_pipe (&fdp->output) >= 0)
            return 0;
          close_half_duplex_pipe (&fdp->input, 2);
        }
    }

  return -1;
}


___HIDDEN int open_full_duplex_pipe2
   ___P((full_duplex_pipe *fdp,
         ___BOOL use_pty),
        (fdp,
         use_pty)
full_duplex_pipe *fdp;
___BOOL use_pty;)
{
  if (use_pty)
    {
      if (setsid () >= 0 &&
#ifdef TIOCSCTTY
          ioctl (fdp->input.reading_fd, TIOCSCTTY, 0) >= 0 &&
#endif
          open_pseudo_terminal_slave (fdp->input.writing_fd,
                                      &fdp->input.reading_fd) >= 0)
        {
          int tmp;
          if (setup_terminal_slave (fdp->input.reading_fd) >= 0 &&
              (fdp->output.writing_fd = dup (fdp->input.reading_fd)) >= 0)
            return 0;
          tmp = errno;
          close (fdp->input.reading_fd); /* ignore error */
          errno = tmp;
        }
    }
  else
    return 0;

  return -1;
}

#endif

#ifdef USE_CreateProcess

___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) argv_to_ccmd
   ___P((___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) *argv),
        (argv)
___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) *argv;)
{
  ___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) ccmd;
  int ccmd_len = 0;
  int i = 0;
  ___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) arg;

  while ((arg = argv[i++]) != NULL)
    {
      int nb_preceding_backslashes = 0;
      int esc = 0;
      int j = 0;
      ___CHAR_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) c;
      while ((c = arg[j++]) != ___UNICODE_NUL)
        {
          if (c == ___UNICODE_BACKSLASH)
            nb_preceding_backslashes++;
          else
            {
              if (c == ___UNICODE_DOUBLEQUOTE)
                esc += nb_preceding_backslashes + 1;
              nb_preceding_backslashes = 0;
            }
        }
      esc += nb_preceding_backslashes + 2; /* add begin/end quotes */
      ccmd_len += j + esc; /* account for escapes */
    }

  ccmd = ___CAST(___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT),
                 ___alloc_mem (ccmd_len * sizeof (*ccmd)));

  if (ccmd != NULL)
    {
      ___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) p = ccmd;
      i = 0;

      while ((arg = argv[i++]) != NULL)
        {
          int nb_preceding_backslashes = 0;
          int j = 0;
          ___CHAR_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) c;
          *p++ = ___UNICODE_DOUBLEQUOTE;
          while ((c = arg[j++]) != ___UNICODE_NUL)
            {
              if (c == ___UNICODE_BACKSLASH)
                nb_preceding_backslashes++;
              else
                {
                  if (c == ___UNICODE_DOUBLEQUOTE)
                    while (nb_preceding_backslashes-- >= 0)
                      *p++ = ___UNICODE_BACKSLASH;
                  nb_preceding_backslashes = 0;
                }
              *p++ = c;
            }
          while (nb_preceding_backslashes-- > 0)
            *p++ = ___UNICODE_BACKSLASH;
          *p++ = ___UNICODE_DOUBLEQUOTE;
          *p++ = ___UNICODE_SPACE;
        }

      p[-1] = ___UNICODE_NUL;
    }

  return ccmd;
}

___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) env_to_cenv
   ___P((___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) *env),
        (env)
___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) *env;)
{
  ___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) cenv;
  int cenv_len = 0;
  int i = 0;
  ___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) varval;

  while ((varval = env[i++]) != NULL)
    {
      int j = 0;
      while (varval[j++] != ___UNICODE_NUL)
        ;
      cenv_len += j;
    }

  cenv_len++;

  cenv = ___CAST(___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT),
                 ___alloc_mem (cenv_len * sizeof (*cenv)));

  if (cenv != NULL)
    {
      ___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) p = cenv;
      i = 0;

      while ((varval = env[i++]) != NULL)
        {
          int j = 0;
          while ((*p++ = varval[j++]) != ___UNICODE_NUL)
            ;
        }

      *p++ = ___UNICODE_NUL;
    }

  return cenv;
}

#endif

___SCMOBJ ___device_stream_setup_from_process
   ___P((___device_stream **dev,
         ___device_group *dgroup,
         ___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) *argv,
         ___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) *env,
         ___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) dir,
         int options),
        (dev,
         dgroup,
         argv,
         env,
         dir,
         options)
___device_stream **dev;
___device_group *dgroup;
___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) *argv;
___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) *env;
___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) dir;
int options;)
{
#ifdef USE_execvp

  ___SCMOBJ e = ___FIX(___NO_ERR);
  int direction;
  ___device_process *d;
  pid_t pid = 0;
  half_duplex_pipe hdp_errno;
  full_duplex_pipe fdp;
  int execvp_errno;
  int n;

  /*
   * Block SIGCHLD so that if the process is created the
   * sigchld_signal_handler will find it in the device group.
   */

#ifdef USE_sigaction

  sigset_t oldmask;
  sigset_t toblock;

  sigemptyset (&toblock);
  sigaddset (&toblock, SIGCHLD);
  sigprocmask (SIG_BLOCK, &toblock, &oldmask);

#endif

#ifdef USE_signal

  int oldmask = sigblock (sigmask (SIGCHLD));

#endif

  if (open_half_duplex_pipe (&hdp_errno) < 0)
    e = err_code_from_errno ();
  else
    {
      if (open_full_duplex_pipe1 (&fdp, options & 2) < 0)
        e = err_code_from_errno ();
      else
        {
          ___disable_os_interrupts ();

          if ((pid = fork ()) < 0)
            {
              e = err_code_from_errno ();
              close_half_duplex_pipe (&fdp.input, 2);
              close_half_duplex_pipe (&fdp.output, 2);
            }

          if (pid > 0)
            ___enable_os_interrupts ();
        }

      if (e != ___FIX(___NO_ERR))
        close_half_duplex_pipe (&hdp_errno, 2);
    }

  if (e == ___FIX(___NO_ERR))
    {
      if (pid == 0)
        {
          /* child process */

          ___set_heartbeat_interval (-1.0);

          if (open_full_duplex_pipe2 (&fdp, options & 2) >= 0 &&
              dup2 (fdp.input.reading_fd, STDIN_FILENO) >= 0 &&
              dup2 (fdp.output.writing_fd, STDOUT_FILENO) >= 0 &&
              ((options & 3) == 0 ||
               dup2 (fdp.output.writing_fd, STDERR_FILENO) >= 0) &&
              fcntl (hdp_errno.writing_fd, F_SETFD, FD_CLOEXEC) >= 0)
            {
              close_half_duplex_pipe (&fdp.input, 1);
              close_half_duplex_pipe (&fdp.output, 0);
              close_half_duplex_pipe (&hdp_errno, 0);

              {
                /* Close all file descriptors that aren't used. */

                int fd = sysconf (_SC_OPEN_MAX) - 1;

                while (fd >= 0)
                  {
                    if (fd != STDIN_FILENO &&
                        fd != STDOUT_FILENO &&
                        fd != STDERR_FILENO &&
                        fd != hdp_errno.writing_fd)
                      close (fd); /* ignore error */
                    fd--;
                  }
              }

              if (dir == NULL || chdir (dir) == 0)
                {
                  if (env != NULL)
                    environ = env;
                  execvp (argv[0], argv);
                }

              /* the exec failed, errno will be returned to parent */
            }

          /* return the errno to the parent process */

          execvp_errno = errno;

          write (hdp_errno.writing_fd, &execvp_errno, sizeof (execvp_errno));

          close_half_duplex_pipe (&fdp.input, 0);
          close_half_duplex_pipe (&fdp.output, 1);
          close_half_duplex_pipe (&hdp_errno, 1);

          _exit (1);
        }

      /* parent process */

      close_half_duplex_pipe (&fdp.input, 0);
      close_half_duplex_pipe (&fdp.output, 1);
      close_half_duplex_pipe (&hdp_errno, 1);

      n = read (hdp_errno.reading_fd, &execvp_errno, sizeof (execvp_errno));

      if (n < 0)
        e = err_code_from_errno ();
      else if (n == sizeof (execvp_errno))
        {
          errno = execvp_errno;
          e = err_code_from_errno ();
        }
      else if (n != 0)
        e = ___FIX(___UNKNOWN_ERR);
      else
        {
          direction = ___DIRECTION_RD|___DIRECTION_WR;

          e = ___device_process_setup_from_pid
                (&d,
                 dgroup,
                 pid,
                 fdp.input.writing_fd,
                 fdp.output.reading_fd,
                 direction);

          *dev = ___CAST(___device_stream*,d);
        }

      if (e != ___FIX(___NO_ERR))
        {
          close_half_duplex_pipe (&fdp.input, 1);
          close_half_duplex_pipe (&fdp.output, 0);
        }

      close_half_duplex_pipe (&hdp_errno, 0);
    }

#ifdef USE_sigaction

  sigprocmask (SIG_SETMASK, &oldmask, 0);

#endif

#ifdef USE_signal

  sigsetmask (oldmask);

#endif

  return e;

#endif

#ifdef USE_CreateProcess

  ___SCMOBJ e;
  int direction;
  ___device_process *d;

  ___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) ccmd;
  ___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT) cenv = NULL;

  HANDLE hstdin_rd = NULL;
  HANDLE hstdin_wr;
  HANDLE hstdout_rd = NULL;
  HANDLE hstdout_wr;

  SECURITY_ATTRIBUTES sa;
  PROCESS_INFORMATION pi;
  STARTUPINFO si;

  sa.nLength = sizeof (SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;

  if ((ccmd = argv_to_ccmd (argv)) == NULL ||
      (env != NULL && (cenv = env_to_cenv (env)) == NULL))
    e = ___FIX(___HEAP_OVERFLOW_ERR);
  else if (!CreatePipe (&hstdin_rd, &hstdin_wr, &sa, 0) ||
           !CreatePipe (&hstdout_rd, &hstdout_wr, &sa, 0))
    e = err_code_from_GetLastError ();
  else
    {
      SetHandleInformation (hstdin_wr,  HANDLE_FLAG_INHERIT, 0);
      SetHandleInformation (hstdout_rd, HANDLE_FLAG_INHERIT, 0);

      ZeroMemory (&pi, sizeof (pi));

      ZeroMemory (&si, sizeof (si));
      si.cb = sizeof (si);
      si.hStdInput = hstdin_rd;
      si.hStdOutput = hstdout_wr;
      if (options & 1)
        si.hStdError = GetStdHandle (STD_ERROR_HANDLE);
      else
        si.hStdError = hstdout_wr;
      si.dwFlags |= STARTF_USESTDHANDLES;

      if (si.hStdError == INVALID_HANDLE_VALUE ||
          !CreateProcess
             (NULL, /* module name                              */
              ccmd, /* command line                             */
              NULL, /* process handle not inheritable           */
              NULL, /* thread handle not inheritable            */
              TRUE, /* set handle inheritance to TRUE           */
              0,    /* no creation flags                        */
              cenv, /* use parent's environment block           */
              dir,  /* use parent's starting directory          */
              &si,  /* pointer to STARTUPINFO structure         */
              &pi)) /* pointer to PROCESS_INFORMATION structure */
        e = err_code_from_GetLastError ();
      else
        {
          direction = ___DIRECTION_RD|___DIRECTION_WR;

          e = ___device_process_setup_from_process
                (&d,
                 dgroup,
                 pi,
                 hstdin_wr,
                 hstdout_rd,
                 direction);

          *dev = ___CAST(___device_stream*,d);
        }
    }

  if (hstdout_rd != NULL)
    CloseHandle (hstdout_wr); /* ignore error */

  if (hstdin_rd != NULL)
    CloseHandle (hstdin_rd); /* ignore error */

  if (e != ___FIX(___NO_ERR))
    {
      if (hstdout_rd != NULL)
        CloseHandle (hstdout_rd); /* ignore error */

      if (hstdin_rd != NULL)
        CloseHandle (hstdin_wr); /* ignore error */
    }

  if (cenv != NULL)
    ___free_mem (cenv);

  if (ccmd != NULL)
    ___free_mem (ccmd);

  return e;

#endif
}

#endif


___SCMOBJ ___os_device_process_pid
   ___P((___SCMOBJ dev),
        (dev)
___SCMOBJ dev;)
{
  ___device_process *d =
    ___CAST(___device_process*,___FIELD(dev,___FOREIGN_PTR));

#ifndef USE_POSIX
#ifndef USE_WIN32

  return ___FIX(0);

#endif
#endif

#ifdef USE_POSIX

  return ___FIX(d->pid);

#endif

#ifdef USE_WIN32

  return ___FIX(d->pi.dwProcessId);

#endif
}


___SCMOBJ ___os_device_process_status
   ___P((___SCMOBJ dev),
        (dev)
___SCMOBJ dev;)
{
  ___device_process *d =
    ___CAST(___device_process*,___FIELD(dev,___FOREIGN_PTR));
  ___SCMOBJ e;

  if ((e = ___device_process_get_status (d)) != ___FIX(___NO_ERR))
    return e;

  if (!d->terminated)
    return ___FAL;

  return ___FIX(d->status);
}


/*---------------------------------------------------------------------------*/

#ifndef USE_POSIX
#ifndef USE_WIN32
#define ___STREAM_OPEN_PATH_CE_SELECT(latin1,utf8,ucs2,ucs4,wchar,native) native
#endif
#endif

#ifdef USE_POSIX
#define ___STREAM_OPEN_PATH_CE_SELECT(latin1,utf8,ucs2,ucs4,wchar,native) native
#endif

#ifdef USE_WIN32
#ifdef _UNICODE
#define ___STREAM_OPEN_PATH_CE_SELECT(latin1,utf8,ucs2,ucs4,wchar,native) ucs2
#else
#define ___STREAM_OPEN_PATH_CE_SELECT(latin1,utf8,ucs2,ucs4,wchar,native) native
#endif
#endif


#ifdef ___STREAM_OPEN_PATH_CE_SELECT

___SCMOBJ ___device_stream_setup_from_path
   ___P((___device_stream **dev,
         ___device_group *dgroup,
         ___STRING_TYPE(___STREAM_OPEN_PATH_CE_SELECT) path,
         int flags,
         int mode),
        (dev,
         dgroup,
         path,
         flags,
         mode)
___device_stream **dev;
___device_group *dgroup;
___STRING_TYPE(___STREAM_OPEN_PATH_CE_SELECT) path;
int flags;
int mode;)
{
#ifndef USE_POSIX
#ifndef USE_WIN32

  char *mod;
  int direction;
  ___FILE *stream;

  device_translate_flags (flags,
                          &mod,
                          &direction);

#ifdef ___DEBUG
  ___printf ("path=\"%s\" mode=%s\n", path, mod);
#endif

  if ((stream = ___fopen (path, mod)) == 0)
    return fnf_or_err_code_from_errno ();

  return ___device_stream_setup_from_stream
           (dev,
            dgroup,
            stream,
            ___NONE_KIND,
            direction);

#endif
#endif

#ifdef USE_POSIX

  int fl;
  int direction;
  int fd;

  device_translate_flags (flags,
                          &fl,
                          &direction);

#ifdef ___DEBUG
  ___printf ("path=\"%s\" fl=%d\n", path, fl);
#endif

  if ((fd = open (path,
                  fl,
#ifdef O_BINARY
                  O_BINARY|
#endif
                  mode))
      < 0)
    return fnf_or_err_code_from_errno ();

  return ___device_stream_setup_from_fd
           (dev,
            dgroup,
            fd,
            ___NONE_KIND,
            direction);

#endif

#ifdef USE_WIN32

  DWORD access_mode;
  DWORD share_mode;
  DWORD creation_mode;
  DWORD attributes;
  int direction;
  HANDLE h;

  device_translate_flags (flags,
                          &access_mode,
                          &share_mode,
                          &creation_mode,
                          &attributes,
                          &direction);

  h = CreateFile (path,
                  access_mode,
                  share_mode,
                  NULL,
                  creation_mode,
                  attributes,
                  NULL);

  if (h == INVALID_HANDLE_VALUE)
    return fnf_or_err_code_from_GetLastError ();

  return ___device_stream_setup_from_handle
           (dev,
            dgroup,
            h,
            flags,
            ___NONE_KIND,
            direction);

#endif
}

#endif


/*---------------------------------------------------------------------------*/

/* I/O module. */

typedef struct io_module_struct
  {
    ___device_group *dgroup;
  } io_module;

___HIDDEN io_module io_mod;


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Device operations. */

___SCMOBJ ___os_device_kind
   ___P((___SCMOBJ dev),
        (dev)
___SCMOBJ dev;)
{
  ___device *d = ___CAST(___device*,___FIELD(dev,___FOREIGN_PTR));

  return ___FIX(___device_kind (d));
}


___SCMOBJ ___os_device_force_output
   ___P((___SCMOBJ dev),
        (dev)
___SCMOBJ dev;)
{
  ___device *d = ___CAST(___device*,___FIELD(dev,___FOREIGN_PTR));

  return ___device_flush_write (d);
}


___SCMOBJ ___os_device_close
   ___P((___SCMOBJ dev,
         ___SCMOBJ direction),
        (dev,
         direction)
___SCMOBJ dev;
___SCMOBJ direction;)
{
  ___device *d = ___CAST(___device*,___FIELD(dev,___FOREIGN_PTR));

  return ___device_close (d, ___INT(direction));
}


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Stream device operations. */

___SCMOBJ ___os_device_stream_seek
   ___P((___SCMOBJ dev,
         ___SCMOBJ pos,
         ___SCMOBJ whence),
        (dev,
         pos,
         whence)
___SCMOBJ dev;
___SCMOBJ pos;
___SCMOBJ whence;)
{
  ___device_stream *d =
    ___CAST(___device_stream*,___FIELD(dev,___FOREIGN_PTR));
  ___S32 p;
  ___SCMOBJ e;
  ___SCMOBJ result;

  if ((e = ___SCMOBJ_to_S32 (pos, &p, 2)) == ___FIX(___NO_ERR))
    e = ___device_stream_seek (d, &p, ___INT(whence));

  if (e != ___FIX(___NO_ERR) ||
      (e = ___S32_to_SCMOBJ (p, &result, ___RETURN_POS)) != ___FIX(___NO_ERR))
    result = e;

  return result;
}


___SCMOBJ ___os_device_stream_read
   ___P((___SCMOBJ dev,
         ___SCMOBJ buffer,
         ___SCMOBJ lo,
         ___SCMOBJ hi),
        (dev,
         buffer,
         lo,
         hi)
___SCMOBJ dev;
___SCMOBJ buffer;
___SCMOBJ lo;
___SCMOBJ hi;)
{
  ___device_stream *d =
    ___CAST(___device_stream*,___FIELD(dev,___FOREIGN_PTR));
  ___stream_index len_done;
  ___SCMOBJ e;

  if ((e = ___device_stream_read
             (d,
              ___CAST(___U8*,___BODY_AS(buffer,___tSUBTYPED)) + ___INT(lo),
              ___INT(hi) - ___INT(lo),
              &len_done))
      == ___FIX(___NO_ERR))
    return ___FIX(len_done);

  return e;
}


___SCMOBJ ___os_device_stream_write
   ___P((___SCMOBJ dev,
         ___SCMOBJ buffer,
         ___SCMOBJ lo,
         ___SCMOBJ hi),
        (dev,
         buffer,
         lo,
         hi)
___SCMOBJ dev;
___SCMOBJ buffer;
___SCMOBJ lo;
___SCMOBJ hi;)
{
  ___device_stream *d =
    ___CAST(___device_stream*,___FIELD(dev,___FOREIGN_PTR));
  ___stream_index len_done;
  ___SCMOBJ e;

  if ((e = ___device_stream_write
             (d,
              ___CAST(___U8*,___BODY_AS(buffer,___tSUBTYPED)) + ___INT(lo),
              ___INT(hi) - ___INT(lo),
              &len_done))
      == ___FIX(___NO_ERR))
    return ___FIX(len_done);

  return e;
}


___SCMOBJ ___os_device_stream_width
   ___P((___SCMOBJ dev),
        (dev)
___SCMOBJ dev;)
{
  ___device_stream *d =
    ___CAST(___device_stream*,___FIELD(dev,___FOREIGN_PTR));

  return ___device_stream_width (d);
}


___SCMOBJ ___os_device_stream_default_options
   ___P((___SCMOBJ dev),
        (dev)
___SCMOBJ dev;)
{
  ___device_stream *d =
    ___CAST(___device_stream*,___FIELD(dev,___FOREIGN_PTR));

  return ___device_stream_default_options (d);
}


___SCMOBJ ___os_device_stream_options_set
   ___P((___SCMOBJ dev,
         ___SCMOBJ options),
        (dev,
         options)
___SCMOBJ dev;
___SCMOBJ options;)
{
  ___device_stream *d =
    ___CAST(___device_stream*,___FIELD(dev,___FOREIGN_PTR));

  return ___device_stream_options_set (d, options);
}


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/*
 * Procedure called by the Scheme runtime when a device is no longer
 * reachable.
 */

___HIDDEN ___SCMOBJ device_cleanup_from_ptr
   ___P((void *ptr),
        (ptr)
void *ptr;)
{
  return ___device_cleanup (___CAST(___device*,ptr));
}


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Opening a predefined device (stdin, stdout, stderr, console, etc). */

___SCMOBJ ___os_device_stream_open_predefined
   ___P((___SCMOBJ index,
         ___SCMOBJ flags),
        (index,
         flags)
___SCMOBJ index;
___SCMOBJ flags;)
{
  ___SCMOBJ e;
  ___device_stream *dev;
  ___SCMOBJ result;

#ifndef USE_POSIX
#ifndef USE_WIN32

  char *mode;
  int direction;
  ___FILE *stream;

  device_translate_flags (___INT(flags),
                          &mode,
                          &direction);

  switch (___INT(index))
    {
#if 0
    case -4:
      {
        ___device_tty *d;

        if ((e = ___device_tty_setup_console
                   (&d,
                    io_mod.dgroup,
                    direction))
            != ___FIX(___NO_ERR))
          return e;

        dev = ___CAST(___device_stream*,d);

        break;
      }
#endif

    default:
      {
        switch (___INT(index))
          {
          case -1:
            stream = ___stdin;
            break;
          case -2:
            stream = ___stdout;
            break;
          case -3:
            stream = ___stderr;
            break;
          case -4:
            stream = 0;
            break;
          default:
            stream = fdopen (___INT(index), mode);
            break;
          }

        if ((e = ___device_stream_setup_from_stream
                   (&dev,
                    io_mod.dgroup,
                    stream,
                    ___NONE_KIND,
                    direction))
            != ___FIX(___NO_ERR))
          return e;

        break;
      }
    }

#endif
#endif

#ifdef USE_POSIX

  int fl;
  int direction;
  int fd;

  device_translate_flags (___INT(flags),
                          &fl,
                          &direction);

  switch (___INT(index))
    {
    case -4:
      {
        ___device_tty *d;

        if ((e = ___device_tty_setup_console
                   (&d,
                    io_mod.dgroup,
                    direction))
            != ___FIX(___NO_ERR))
          return e;

        dev = ___CAST(___device_stream*,d);

        break;
      }

    default:
      {
        switch (___INT(index))
          {
          case -1:
            fd = STDIN_FILENO;
            break;
          case -2:
            fd = STDOUT_FILENO;
            break;
          case -3:
            fd = STDERR_FILENO;
            break;
          default:
            fd = ___INT(index);
            break;
          }

#if 0
        /*
         * The file descriptor must be dup'ed so that the standard
         * stdin/stdout/stderr are not closed.
         */

        ___printf ("tty fd=%d\n", fd);/*****************************/
        if ((fd = dup (fd)) < 0)
          return err_code_from_errno ();
        ___printf ("  new tty fd=%d\n", fd);/***********************/
#endif

        if ((e = ___device_stream_setup_from_fd
                   (&dev,
                    io_mod.dgroup,
                    fd,
                    ___NONE_KIND,
                    direction))
            != ___FIX(___NO_ERR))
          return e;

        break;
      }
    }

#endif

#ifdef USE_WIN32

  DWORD access_mode;
  DWORD share_mode;
  DWORD creation_mode;
  DWORD attributes;
  int direction;
  HANDLE h;

  device_translate_flags (___INT(flags),
                          &access_mode,
                          &share_mode,
                          &creation_mode,
                          &attributes,
                          &direction);

  switch (___INT(index))
    {
    case -4:
    open_console:
      {
        ___device_tty *d;

        if ((e = ___device_tty_setup_console
                   (&d,
                    io_mod.dgroup,
                    direction))
            != ___FIX(___NO_ERR))
          return e;

        dev = ___CAST(___device_stream*,d);

        break;
      }

    default:
      {
        switch (___INT(index))
          {
          default:
          case -1:
            h = GetStdHandle (STD_INPUT_HANDLE);
            break;
          case -2:
            h = GetStdHandle (STD_OUTPUT_HANDLE);
            break;
          case -3:
            h = GetStdHandle (STD_ERROR_HANDLE);
            break;
          }

        if (h == INVALID_HANDLE_VALUE)
          return err_code_from_GetLastError ();

        if (GetFileType (h) == FILE_TYPE_UNKNOWN)
          goto open_console;

        /*********** duplicate the handle? */

        /******** ___printf ("index=%d\n", ___INT(index)); */

        if ((e = ___device_stream_setup_from_handle
                   (&dev,
                    io_mod.dgroup,
                    h,
                    0,
#if 1
                    /* we have to force the kind to "file" so that
                       when stdin is the console, we don't get
                       the "line editor" machinery. */
                    ___FILE_DEVICE_KIND,
#else
                    ___NONE_KIND,
#endif
                    direction))
            != ___FIX(___NO_ERR))
          return e;

        break;
      }
    }

#endif

  e = ___NONNULLPOINTER_to_SCMOBJ
        (dev,
         ___FAL,
         device_cleanup_from_ptr,
         &result,
         ___RETURN_POS);

  if (e != ___FIX(___NO_ERR))
    {
      ___device_cleanup (___CAST(___device*,dev)); /* ignore error */
      return e;
    }

  ___release_scmobj (result);

  return result;
}


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Opening a path. */

___SCMOBJ ___os_device_stream_open_path
   ___P((___SCMOBJ path,
         ___SCMOBJ flags,
         ___SCMOBJ mode),
        (path,
         flags,
         mode)
___SCMOBJ path;
___SCMOBJ flags;
___SCMOBJ mode;)
{
#ifndef ___STREAM_OPEN_PATH_CE_SELECT

  return ___FIX(___UNIMPL_ERR);

#else

  ___SCMOBJ e;
  ___SCMOBJ result;
  ___device_stream *dev;
  void *cpath;

  if ((e = ___SCMOBJ_to_NONNULLSTRING
             (path,
              &cpath,
              1,
              ___CE(___STREAM_OPEN_PATH_CE_SELECT),
              0))
      != ___FIX(___NO_ERR))
    result = e;
  else
    {
      ___STRING_TYPE(___STREAM_OPEN_PATH_CE_SELECT) p =
        ___CAST(___STRING_TYPE(___STREAM_OPEN_PATH_CE_SELECT),cpath);

      if ((e = ___device_stream_setup_from_path
                 (&dev,
                  io_mod.dgroup,
                  p,
                  ___INT(flags),
                  ___INT(mode)))
          != ___FIX(___NO_ERR))
        result = e;
      else
        {
          if ((e = ___NONNULLPOINTER_to_SCMOBJ
                     (dev,
                      ___FAL,
                      device_cleanup_from_ptr,
                      &result,
                      ___RETURN_POS))
              != ___FIX(___NO_ERR))
            {
              ___device_cleanup (___CAST(___device*,dev)); /* ignore error */
              result = e;
            }
        }

      ___release_string (cpath);
    }

  ___release_scmobj (result);

  return result;

#endif
}


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Opening a process. */

___SCMOBJ ___os_device_stream_open_process
   ___P((___SCMOBJ path_and_args,
         ___SCMOBJ environment,
         ___SCMOBJ directory,
         ___SCMOBJ options),
        (path_and_args,
         environment,
         directory,
         options)
___SCMOBJ path_and_args;
___SCMOBJ environment;
___SCMOBJ directory;
___SCMOBJ options;)
{
#ifndef ___STREAM_OPEN_PROCESS_CE_SELECT

  return ___FIX(___UNIMPL_ERR);

#else

  ___SCMOBJ e;
  ___device_stream *dev;
  ___SCMOBJ result;
  void *argv = NULL;
  void *env = NULL;
  void *dir = NULL;

  if ((e = ___SCMOBJ_to_NONNULLSTRINGLIST
             (path_and_args,
              &argv,
              1,
              ___CE(___STREAM_OPEN_PROCESS_CE_SELECT)))
      != ___FIX(___NO_ERR) ||
      (environment != ___FAL &&
       (e = ___SCMOBJ_to_NONNULLSTRINGLIST
              (environment,
               &env,
               2,
               ___CE(___STREAM_OPEN_PROCESS_CE_SELECT)))
       != ___FIX(___NO_ERR)) ||
      (directory != ___FAL &&
       (e = ___SCMOBJ_to_NONNULLSTRING
              (directory,
               &dir,
               3,
               ___CE(___STREAM_OPEN_PROCESS_CE_SELECT),
               0))
       != ___FIX(___NO_ERR)) ||
      (e = ___device_stream_setup_from_process
             (&dev,
              io_mod.dgroup,
              ___CAST(___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT)*,argv),
              ___CAST(___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT)*,env),
              ___CAST(___STRING_TYPE(___STREAM_OPEN_PROCESS_CE_SELECT),dir),
              ___INT(options)))
      != ___FIX(___NO_ERR))
    result = e;
  else
    {
      if ((e = ___NONNULLPOINTER_to_SCMOBJ
                 (dev,
                  ___FAL,
                  device_cleanup_from_ptr,
                  &result,
                  ___RETURN_POS))
          == ___FIX(___NO_ERR))
        ___release_scmobj (result);
    }

  if (argv != NULL)
    ___release_string_list (argv);

  if (env != NULL)
    ___release_string_list (env);

  if (dir != NULL)
    ___release_string (dir);

  return result;

#endif
}


#ifdef USE_POSIX

___HIDDEN void sigchld_signal_handler (int sig)
{
#ifdef USE_signal
  ___set_signal_handler (SIGCHLD, sigchld_signal_handler);
#endif

  {
    int status;
    pid_t pid = waitpid (-1, &status, WNOHANG);

    if (pid > 0)
      {
        /*
         * Find the process device structure for the process which
         * terminated, and save the exit status and change the state
         * to "terminated".
         */

        ___device *head = io_mod.dgroup->list;

        if (head != NULL)
          {
            ___device *d = head;

            do
              {
                if (___device_kind (d) == ___PROCESS_DEVICE_KIND)
                  {
                    ___device_process *dev = ___CAST(___device_process*,d);
                    if (dev->pid == pid)
                      {
                        dev->status = status;

                        if (WIFEXITED(status) || WIFSIGNALED(status))
                          ___device_process_cleanup (dev); /* ignore error */
                        break;
                      }
                  }
                d = d->next;
              } while  (d != head);
          }
      }
  }
}

#endif


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Opening a TCP client. */

___SCMOBJ ___os_device_tcp_client_open
   ___P((___SCMOBJ server_addr,
         ___SCMOBJ port_num,
         ___SCMOBJ options),
        (server_addr,
         port_num,
         options)
___SCMOBJ server_addr;
___SCMOBJ port_num;
___SCMOBJ options;)
{
#ifndef USE_NETWORKING

  return ___FIX(___UNIMPL_ERR);

#else

  ___SCMOBJ e;
  ___device_tcp_client *dev;
  ___SCMOBJ result;
  struct sockaddr sa;
  int salen;

  if ((e = ___SCMOBJ_to_sockaddr (server_addr, port_num, &sa, &salen, 1))
      != ___FIX(___NO_ERR))
    return e;

  e = ___device_tcp_client_setup_from_sockaddr
        (&dev,
         io_mod.dgroup,
         &sa,
         salen,
         ___INT(options),
         ___DIRECTION_RD|___DIRECTION_WR);

  if (e != ___FIX(___NO_ERR))
    return e;

  e = ___NONNULLPOINTER_to_SCMOBJ
        (dev,
         ___FAL,
         device_cleanup_from_ptr,
         &result,
         ___RETURN_POS);

  if (e != ___FIX(___NO_ERR))
    {
      ___device_cleanup (___CAST(___device*,dev)); /* ignore error */
      return e;
    }

  ___release_scmobj (result);

  return result;

#endif
}


___SCMOBJ ___os_device_tcp_client_socket_info
   ___P((___SCMOBJ dev,
         ___SCMOBJ peer),
        (dev,
         peer)
___SCMOBJ dev;
___SCMOBJ peer;)
{
#ifndef USE_NETWORKING

  return ___FIX(___UNIMPL_ERR);

#else

  ___device_tcp_client *d =
    ___CAST(___device_tcp_client*,___FIELD(dev,___FOREIGN_PTR));
  struct sockaddr sa;
  SOCKET_LEN_TYPE salen;

  if (d->try_connect_again != 0)
    {
      if (try_connect (d) == 0)
        {
          if (d->try_connect_again != 0)
            return ___ERR_CODE_EAGAIN;
        }
      else
        return ERR_CODE_FROM_SOCKET_CALL;
    }

  salen = sizeof (sa);

  if (((peer == ___FAL)
       ? getsockname (d->s, &sa, &salen)
       : getpeername (d->s, &sa, &salen)) < 0)
    {
      ___SCMOBJ e = ERR_CODE_FROM_SOCKET_CALL;
      if (NOT_CONNECTED(e))
        e = ___ERR_CODE_EAGAIN;
      return e;
    }

  return ___sockaddr_to_SCMOBJ (&sa, salen, ___RETURN_POS);

#endif
}


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Opening and reading a TCP server. */

___SCMOBJ ___os_device_tcp_server_open
   ___P((___SCMOBJ server_addr,
         ___SCMOBJ port_num,
         ___SCMOBJ backlog,
         ___SCMOBJ options),
        (server_addr,
         port_num,
         backlog,
         options)
___SCMOBJ server_addr;
___SCMOBJ port_num;
___SCMOBJ backlog;
___SCMOBJ options;)
{
#ifndef USE_NETWORKING

  return ___FIX(___UNIMPL_ERR);

#else

  ___SCMOBJ e;
  ___device_tcp_server *dev;
  ___SCMOBJ result;
  struct sockaddr sa;
  int salen;

  if ((e = ___SCMOBJ_to_sockaddr (server_addr, port_num, &sa, &salen, 1))
      != ___FIX(___NO_ERR))
    return e;

  e = ___device_tcp_server_setup
        (&dev,
         io_mod.dgroup,
         &sa,
         salen,
         ___INT(backlog),
         ___INT(options));

  if (e != ___FIX(___NO_ERR))
    return e;

  e = ___NONNULLPOINTER_to_SCMOBJ
        (dev,
         ___FAL,
         device_cleanup_from_ptr,
         &result,
         ___RETURN_POS);

  if (e != ___FIX(___NO_ERR))
    {
      ___device_cleanup (___CAST(___device*,dev)); /* ignore error */
      return e;
    }

  ___release_scmobj (result);

  return result;

#endif
}


___SCMOBJ ___os_device_tcp_server_read
   ___P((___SCMOBJ dev),
        (dev)
___SCMOBJ dev;)
{
#ifndef USE_NETWORKING

  return ___FIX(___UNIMPL_ERR);

#else

  ___device_tcp_server *d =
    ___CAST(___device_tcp_server*,___FIELD(dev,___FOREIGN_PTR));
  ___SCMOBJ e;
  ___device_tcp_client *client;
  ___SCMOBJ result;

  if ((e = ___device_tcp_server_read (d, io_mod.dgroup, &client))
      != ___FIX(___NO_ERR))
    return e;

  e = ___NONNULLPOINTER_to_SCMOBJ
        (client,
         ___FAL,
         device_cleanup_from_ptr,
         &result,
         ___RETURN_POS);

  if (e != ___FIX(___NO_ERR))
    {
      ___device_cleanup (___CAST(___device*,d)); /* ignore error */
      return e;
    }

  ___release_scmobj (result);

  return result;

#endif
}


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Opening and reading a directory. */

___SCMOBJ ___os_device_directory_open_path
   ___P((___SCMOBJ path,
         ___SCMOBJ ignore_hidden),
        (path,
         ignore_hidden)
___SCMOBJ path;
___SCMOBJ ignore_hidden;)
{
#ifndef ___DIR_OPEN_PATH_CE_SELECT

  return ___FIX(___UNIMPL_ERR);

#else

  ___SCMOBJ e;
  ___SCMOBJ result;
  ___device_directory *dev;
  void *cpath;

  if ((e = ___SCMOBJ_to_NONNULLSTRING
             (path,
              &cpath,
              1,
              ___CE(___DIR_OPEN_PATH_CE_SELECT),
              0))
      != ___FIX(___NO_ERR))
    result = e;
  else
    {
      ___STRING_TYPE(___DIR_OPEN_PATH_CE_SELECT) p =
        ___CAST(___STRING_TYPE(___DIR_OPEN_PATH_CE_SELECT),cpath);

      if ((e = ___device_directory_setup
                 (&dev,
                  io_mod.dgroup,
                  p,
                  ___INT(ignore_hidden)))
          != ___FIX(___NO_ERR))
        result = e;
      else
        {
          if ((e = ___NONNULLPOINTER_to_SCMOBJ
                     (dev,
                      ___FAL,
                      device_cleanup_from_ptr,
                      &result,
                      ___RETURN_POS))
              != ___FIX(___NO_ERR))
            {
              ___device_cleanup (___CAST(___device*,dev)); /* ignore error */
              result = e;
            }
        }

      ___release_string (cpath);
    }

  ___release_scmobj (result);

  return result;

#endif
}


___SCMOBJ ___os_device_directory_read
   ___P((___SCMOBJ dev),
        (dev)
___SCMOBJ dev;)
{
#ifndef ___DIR_OPEN_PATH_CE_SELECT

  return ___FIX(___UNIMPL_ERR);

#else

  ___device_directory *d =
    ___CAST(___device_directory*,___FIELD(dev,___FOREIGN_PTR));
  ___SCMOBJ e;
  char *name;/******************/
  ___SCMOBJ result;

  if ((e = ___device_directory_read (d, &name)) != ___FIX(___NO_ERR))
    return e;

  if (name == NULL)
    return ___EOF;

  if ((e = ___CHARSTRING_to_SCMOBJ (name, &result, ___RETURN_POS))
      != ___FIX(___NO_ERR))
    return e;

  ___release_scmobj (result);

  return result;

#endif
}


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Opening an event-queue. */

___SCMOBJ ___os_device_event_queue_open
   ___P((___SCMOBJ index),
        (index)
___SCMOBJ index;)
{
  ___SCMOBJ e;
  ___SCMOBJ result;
  ___device_event_queue *dev;

  if ((e = ___device_event_queue_setup
             (&dev,
              io_mod.dgroup,
              ___FIX(index)))
      != ___FIX(___NO_ERR))
    result = e;
  else
    {
      if ((e = ___NONNULLPOINTER_to_SCMOBJ
                 (dev,
                  ___FAL,
                  device_cleanup_from_ptr,
                  &result,
                  ___RETURN_POS))
          != ___FIX(___NO_ERR))
        {
          ___device_cleanup (___CAST(___device*,dev)); /* ignore error */
          result = e;
        }
    }

  ___release_scmobj (result);

  return result;
}


___SCMOBJ ___os_device_event_queue_read
   ___P((___SCMOBJ dev),
        (dev)
___SCMOBJ dev;)
{
  ___device_event_queue *d =
    ___CAST(___device_event_queue*,___FIELD(dev,___FOREIGN_PTR));
  ___SCMOBJ e;
  ___SCMOBJ result;

  if ((e = ___device_event_queue_read (d, &result)) != ___FIX(___NO_ERR))
    return e;

  ___release_scmobj (result);

  return result;
}


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/* Waiting for I/O to become possible on a set of devices. */

___SCMOBJ ___os_condvar_select
   ___P((___SCMOBJ run_queue,
         ___SCMOBJ timeout),
        (run_queue,
         timeout)
___SCMOBJ run_queue;
___SCMOBJ timeout;)
{
/******************/
#define ___BTQ_DEQ_NEXT 1
#define ___BTQ_DEQ_PREV 2
#define ___BTQ_COLOR    3
#define ___BTQ_PARENT   4
#define ___BTQ_LEFT     5
#define ___BTQ_RIGHT    6
#define ___BTQ_LEFTMOST 6
#define ___BTQ_OWNER    7
#define ___CONDVAR_NAME 8

  ___SCMOBJ e;
  ___time to;
  ___device *devs[MAX_CONDVARS];
  ___SCMOBJ condvars[MAX_CONDVARS];
  int read_pos;
  int write_pos;
  ___SCMOBJ condvar;
  int i;
  int j;

  if (timeout == ___FAL)
    to = ___time_mod.time_neg_infinity;
  else if (timeout == ___TRU)
    to = ___time_mod.time_pos_infinity;
  else
    ___time_from_seconds (&to, ___F64VECTORREF(timeout,___FIX(0)));

  read_pos = 0;
  write_pos = MAX_CONDVARS;
  condvar = ___FIELD(run_queue,___BTQ_DEQ_NEXT);

  while (condvar != run_queue)
    {
      ___SCMOBJ owner = ___FIELD(condvar,___BTQ_OWNER);
      if (read_pos < write_pos)
        {
          if (owner & ___FIX(2))
            condvars[--write_pos] = condvar;
          else
            condvars[read_pos++] = condvar;
          ___FIELD(condvar,___BTQ_OWNER) = owner & ~___FIX(1);
        }
      else
        {
          to = ___time_mod.time_neg_infinity;
          ___FIELD(condvar,___BTQ_OWNER) = owner | ___FIX(1);
        }
      condvar = ___FIELD(condvar,___BTQ_DEQ_NEXT);
    }

  i = 0;

  while (i < read_pos)
    {
      devs[i] = ___CAST(___device*,
                        ___FIELD(___FIELD(condvars[i],___CONDVAR_NAME),
                                 ___FOREIGN_PTR));
      i++;
    }

  j = MAX_CONDVARS;

  while (j > write_pos)
    {
      j--;
      devs[i] = ___CAST(___device*,
                        ___FIELD(___FIELD(condvars[j],___CONDVAR_NAME),
                                 ___FOREIGN_PTR));
      i++;
    }

  e = ___device_select (devs, read_pos, MAX_CONDVARS-write_pos, to);

  i = 0;

  while (i < read_pos)
    {
      if (devs[i] == NULL)
        {
          condvar = condvars[i];
          ___FIELD(condvar,___BTQ_OWNER) |= ___FIX(1);
        }
      i++;
    }

  j = MAX_CONDVARS;

  while (j > write_pos)
    {
      j--;
      if (devs[i] == NULL)
        {
          condvar = condvars[j];
          ___FIELD(condvar,___BTQ_OWNER) |= ___FIX(1);
        }
      i++;
    }

  return e;
}


/*   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   */

/*
 * Decoding and encoding of a buffer of Scheme characters to a buffer
 * of bytes.
 */

/**************** replace with code in c_intf.c */

#define unicode_LF 10
#define unicode_CR 13
#define unicode_BOM 0xfeff

#define bytes_per_ISO_8859_1 1
#define max_ISO_8859_1       0xff

#define bytes_per_UTF_8 1 /* optimization for single byte case */
#define max_UTF_8       0x7f

#define bytes_per_UCS_2 2
#define max_UCS_2       0xffff

#define bytes_per_UCS_4 4
#define max_UCS_4       0x7fffffff

#define DECODE_EOL(loop_label) \
if (c != unicode_LF) \
  { \
    if (c != unicode_CR) \
      { \
        options += ___DECODE_STATE_NONE-___DECODE_STATE(options); \
        *chi++ = c; \
        if (chi < end) \
          goto loop_label; \
      } \
    else \
      { \
        int eol = ___EOL_ENCODING(options); \
        if (eol != ___EOL_ENCODING_LF) \
          { \
            if (eol != ___EOL_ENCODING_CR) \
              { \
                int rs = ___DECODE_STATE(options); \
                if (rs == ___DECODE_STATE_LF) \
                  { \
                    options += \
                      ___DECODE_STATE_NONE-___DECODE_STATE_LF; \
                    goto loop_label; \
                  } \
                options += ___DECODE_STATE_CR-rs; \
              } \
            c = char_EOL; \
          } \
        *chi++ = c; \
        if (chi < end) \
          goto loop_label; \
      } \
  } \
else \
  { \
    int eol = ___EOL_ENCODING(options); \
    if (eol != ___EOL_ENCODING_CR) \
      { \
        if (eol != ___EOL_ENCODING_LF) \
          { \
            int rs = ___DECODE_STATE(options); \
            if (rs == ___DECODE_STATE_CR) \
              { \
                options += \
                  ___DECODE_STATE_NONE-___DECODE_STATE_CR; \
                goto loop_label; \
              } \
            options += ___DECODE_STATE_LF-rs; \
          } \
        c = char_EOL; \
      } \
    *chi++ = c; \
    if (chi < end) \
      goto loop_label; \
  }

#define DECODE_CHARS_LOOP(loop_label,bytes_per_char,max_char,err_code,get_char) \
\
loop_label: \
blo += bytes_per_char; \
if (blo <= bhi) \
  { \
    c = get_char(-1); \
    if (max_char <= ___MAX_CHR || \
        c <= ___MAX_CHR) \
      { \
        DECODE_EOL(loop_label) \
      } \
    else \
      { \
        blo -= bytes_per_char; \
        e = err_code; \
      } \
  } \
else \
  { \
    blo -= bytes_per_char; \
    if (bytes_per_char > 1 && \
        blo < bhi &&   /* at least one byte left in buffer? */ \
        eof != ___FAL) /* end-of-file was reached? */ \
      e = err_code; /* trailing partial! */ \
  } \
break;

#define ENCODE_EOL(loop_label,bytes_per_char,put_char) \
switch (___EOL_ENCODING(options)) \
  { \
  case ___EOL_ENCODING_CR: \
    put_char(-1,unicode_CR); \
    break; \
  case ___EOL_ENCODING_CRLF: \
    bhi += bytes_per_char; \
    if (bhi > end) \
      { \
        bhi -= 2*bytes_per_char; \
        clo--; \
        goto encode_chars_end; \
      } \
    put_char(-2,unicode_CR); \
  default: \
    put_char(-1,unicode_LF); \
    break; \
  } \
if (___LINE_BUFFERED(options)) \
  goto encode_chars_end; /* must empty byte buffer */ \
else if (clo < chi) \
  goto loop_label;

#define ENCODE_CHARS_LOOP(loop_label,bytes_per_char,max_char,err_code,put_char) \
\
loop_label: \
c = *clo++; \
if (___MAX_CHR <= max_char || \
    c <= max_char) \
  { \
    bhi += bytes_per_char; \
    if (bhi <= end) \
      { \
        if (c != char_EOL) \
          { \
            put_char(-1,c); \
            if (clo < chi) \
              goto loop_label; \
          } \
        else \
          { \
            ENCODE_EOL(loop_label,bytes_per_char,put_char); \
          } \
      } \
    else \
      { \
        bhi -= bytes_per_char; \
        clo--; \
        goto encode_chars_end; \
      } \
  } \
else \
  { \
    e = err_code; \
    goto encode_chars_end; \
  } \
break;

#define get_ISO_8859_1(i) \
blo[(i)*bytes_per_ISO_8859_1]

#define get_UTF_8(i) \
blo[(i)*bytes_per_UTF_8]

#define get_UCS_2BE(i) \
(___CAST(___UCS_2,blo[(i)*bytes_per_UCS_2+0]) << 8) + \
___CAST(___UCS_2,blo[(i)*bytes_per_UCS_2+1])

#define get_UCS_2LE(i) \
(___CAST(___UCS_2,blo[(i)*bytes_per_UCS_2+1]) << 8) + \
___CAST(___UCS_2,blo[(i)*bytes_per_UCS_2+0])

#define get_UCS_4BE(i) \
(((((___CAST(___UCS_4,blo[(i)*bytes_per_UCS_4+0]) << 8) + \
    ___CAST(___UCS_4,blo[(i)*bytes_per_UCS_4+1])) << 8) + \
  ___CAST(___UCS_4,blo[(i)*bytes_per_UCS_4+2])) << 8) + \
___CAST(___UCS_4,blo[(i)*bytes_per_UCS_4+3])

#define get_UCS_4LE(i) \
(((((___CAST(___UCS_4,blo[(i)*bytes_per_UCS_4+3]) << 8) + \
    ___CAST(___UCS_4,blo[(i)*bytes_per_UCS_4+2])) << 8) + \
  ___CAST(___UCS_4,blo[(i)*bytes_per_UCS_4+1])) << 8) + \
___CAST(___UCS_4,blo[(i)*bytes_per_UCS_4+0])

#define put_ISO_8859_1(i,c) \
bhi[(i)*bytes_per_ISO_8859_1] = (c);

#define put_UTF_8(i,c) \
bhi[(i)*bytes_per_UTF_8] = (c);

#define put_UCS_2BE(i,c) \
bhi[(i)*bytes_per_UCS_2+1] = (c) & 0xff; \
bhi[(i)*bytes_per_UCS_2+0] = ((c)>>8) & 0xff;

#define put_UCS_2LE(i,c) \
bhi[(i)*bytes_per_UCS_2+0] = (c) & 0xff; \
bhi[(i)*bytes_per_UCS_2+1] = ((c)>>8) & 0xff;

#define put_UCS_4BE(i,c) \
bhi[(i)*bytes_per_UCS_4+3] = (c) & 0xff; \
bhi[(i)*bytes_per_UCS_4+2] = ((c)>>8) & 0xff; \
bhi[(i)*bytes_per_UCS_4+1] = ((c)>>16) & 0xff; \
bhi[(i)*bytes_per_UCS_4+0] = ((c)>>24) & 0xff;

#define put_UCS_4LE(i,c) \
bhi[(i)*bytes_per_UCS_4+0] = (c) & 0xff; \
bhi[(i)*bytes_per_UCS_4+1] = ((c)>>8) & 0xff; \
bhi[(i)*bytes_per_UCS_4+2] = ((c)>>16) & 0xff; \
bhi[(i)*bytes_per_UCS_4+3] = ((c)>>24) & 0xff;

/****************/
#define ___PORT_MUTEX                1
#define ___PORT_RKIND                2
#define ___PORT_WKIND                3
#define ___PORT_NAME                 4
#define ___PORT_READ_DATUM           5
#define ___PORT_WRITE_DATUM          6
#define ___PORT_NEWLINE              7
#define ___PORT_FORCE_OUTPUT         8
#define ___PORT_CLOSE                9
#define ___PORT_ROPTIONS             10
#define ___PORT_RTIMEOUT             11
#define ___PORT_RTIMEOUT_THUNK       12
#define ___PORT_SET_RTIMEOUT         13
#define ___PORT_WOPTIONS             14
#define ___PORT_WTIMEOUT             15
#define ___PORT_WTIMEOUT_THUNK       16
#define ___PORT_SET_WTIMEOUT         17

#define ___PORT_OBJECT_OTHER1        18
#define ___PORT_OBJECT_OTHER2        19
#define ___PORT_OBJECT_OTHER3        20

#define ___PORT_CHAR_RBUF            18
#define ___PORT_CHAR_RLO             19
#define ___PORT_CHAR_RHI             20
#define ___PORT_CHAR_RCHARS          21
#define ___PORT_CHAR_RLINES          22
#define ___PORT_CHAR_RCURLINE        23
#define ___PORT_CHAR_RBUF_FILL       24
#define ___PORT_CHAR_PEEK_EOFP       25

#define ___PORT_CHAR_WBUF            26
#define ___PORT_CHAR_WLO             27
#define ___PORT_CHAR_WHI             28
#define ___PORT_CHAR_WCHARS          29
#define ___PORT_CHAR_WLINES          30
#define ___PORT_CHAR_WCURLINE        31
#define ___PORT_CHAR_WBUF_DRAIN      32
#define ___PORT_INPUT_READTABLE      33
#define ___PORT_OUTPUT_READTABLE     34
#define ___PORT_OUTPUT_WIDTH         35

#define ___PORT_CHAR_OTHER1          36
#define ___PORT_CHAR_OTHER2          37
#define ___PORT_CHAR_OTHER3          38
#define ___PORT_CHAR_OTHER4          39
#define ___PORT_CHAR_OTHER5          40

#define ___PORT_BYTE_RBUF            36
#define ___PORT_BYTE_RLO             37
#define ___PORT_BYTE_RHI             38
#define ___PORT_BYTE_RBUF_FILL       39

#define ___PORT_BYTE_WBUF            40
#define ___PORT_BYTE_WLO             41
#define ___PORT_BYTE_WHI             42
#define ___PORT_BYTE_WBUF_DRAIN      43

#define ___PORT_BYTE_OTHER1          44
#define ___PORT_BYTE_OTHER2          45

#define ___PORT_RDEVICE_CONDVAR      44
#define ___PORT_WDEVICE_CONDVAR      45

#define ___PORT_DEVICE_OTHER1        46
#define ___PORT_DEVICE_OTHER2        47

#define ___C ___CS_SELECT(___U8,___U16,___U32)

___SCMOBJ ___os_port_decode_chars
   ___P((___SCMOBJ port,
         ___SCMOBJ eof),
        (port,
         eof)
___SCMOBJ port;
___SCMOBJ eof;)
{
  ___SCMOBJ e;
  ___SCMOBJ result;
  ___C *char_rbuf =
     ___CAST(___C*,___BODY_AS(___FIELD(port,___PORT_CHAR_RBUF),___tSUBTYPED));
  ___C *chi =
     char_rbuf + ___INT(___FIELD(port,___PORT_CHAR_RHI));
  ___C *end =
     char_rbuf + ___INT(___STRINGLENGTH(___FIELD(port,___PORT_CHAR_RBUF)));
  ___U8 *byte_rbuf =
     ___CAST(___U8*,___BODY_AS(___FIELD(port,___PORT_BYTE_RBUF),___tSUBTYPED));
  ___U8 *blo =
     byte_rbuf + ___INT(___FIELD(port,___PORT_BYTE_RLO));
  ___U8 *bhi =
     byte_rbuf + ___INT(___FIELD(port,___PORT_BYTE_RHI));
  ___UCS_4 c;
  int options = ___INT(___FIELD(port,___PORT_ROPTIONS));

#if 0
  if ((___INT(___FIELD(port,___PORT_RKIND)) & ___TTY_DEVICE_KIND) ==
      ___TTY_DEVICE_KIND)
    {
      /**************** this special handling should be removed */
      /*
       * Characters read from tty stream devices are represented as 8,
       * 16 or 32 bit unsigned integers of the machine's endianness.
       * It is the tty stream device driver that decodes the bytes
       * received from the device using the encoding specified in the
       * ___PORT_ROPTIONS field.
       */

      options = (options - ___CHAR_ENCODING(options)) +
                ___CS_SELECT(___CHAR_ENCODING_U8,
                             ___CHAR_ENCODING_U16,
                             ___CHAR_ENCODING_U32);
    }
#endif

  e = ___FIX(___NO_ERR);

  ___FIELD(port,___PORT_CHAR_RCHARS) =
    ___FIELD(port,___PORT_CHAR_RCHARS) + ___FIX(chi-char_rbuf);

  chi = char_rbuf;

  /* fill character buffer as much as possible */

  if (chi < end)
    {
      switch (___CHAR_ENCODING(options))
        {
        default:
        case ___CHAR_ENCODING_ASCII:
        case ___CHAR_ENCODING_ISO_8859_1:
          DECODE_CHARS_LOOP(decode_next_ISO_8859_1,
                            bytes_per_ISO_8859_1,
                            0xff,
                            ___FIX(___CTOS_ISO_8859_1STRING_ERR),
                            get_ISO_8859_1);

        case ___CHAR_ENCODING_UTF_8:
          {
            decode_next_UTF_8:
            blo += bytes_per_UTF_8;
            if (blo <= bhi)
              {
                c = get_UTF_8(-1);
                if (c <= 0x7f)
                  {
                    DECODE_EOL(decode_next_UTF_8);
                  }
                else if (c <= 0xbf || c > 0xfd)
                  {
                    blo -= bytes_per_UTF_8;
                    e = ___FIX(___CTOS_UTF_8STRING_ERR);
                  }
                else
                  {
                    ___U8* orig_blo = blo;
                    ___U8 b0 = c;
                    int bits = 6;
                    while (b0 & 0x40)
                      {
                        ___U8 next = *blo++;
                        if (blo > bhi)
                          {
                            blo = orig_blo-bytes_per_UTF_8;
                            if (eof != ___FAL) /* end-of-file reached? */
                              e = ___FIX(___CTOS_UTF_8STRING_ERR);
                            goto end_UTF_8;
                          }
                        if (next <= 0x7f || next > 0xbf)
                          {
                            blo = orig_blo-bytes_per_UTF_8;
                            e = ___FIX(___CTOS_UTF_8STRING_ERR);
                            goto end_UTF_8;
                          }
                        c = (c << 6) + (next & 0x3f);
                        b0 <<= 1;
                        bits += 5;
                      }
                    c &= (___CAST(___UCS_4,1)<<bits)-1;
                    if (c >= 0x80 &&
                        c >= (___CAST(___UCS_4,1)<<(bits-5)) &&
                        c <= ___MAX_CHR)
                      {
                        options +=
                          ___DECODE_STATE_NONE-___DECODE_STATE(options);
                        *chi++ = c;
                        if (chi < end)
                          goto decode_next_UTF_8;
                      }
                    else
                      {
                        blo = orig_blo-bytes_per_UTF_8;
                        e = ___FIX(___CTOS_UTF_8STRING_ERR);
                      }
                    end_UTF_8:;
                  }
              }
            else
              {
                blo -= bytes_per_UTF_8;
                if (blo < bhi &&   /* at least one byte left in buffer? */
                    eof != ___FAL) /* end-of-file was reached? */
                  e = ___FIX(___CTOS_UTF_8STRING_ERR);
              }
            break;
          }

        case ___CHAR_ENCODING_UCS_2:
          {
            blo += bytes_per_UCS_2;
            if (blo <= bhi)
              {
                ___UCS_4 cle;
                c = get_UCS_2BE(-1);
                if (c == unicode_BOM)
                  {
                    options += ___CHAR_ENCODING_UCS_2BE-___CHAR_ENCODING_UCS_2;
                    goto decode_next_UCS_2BE;
                  }
                cle = ((c&0xff) << 8) +
                      ((c>>8)&0xff);
                if (cle == unicode_BOM)
                  {
                    options += ___CHAR_ENCODING_UCS_2LE-___CHAR_ENCODING_UCS_2;
                    goto decode_next_UCS_2LE;
                  }
                blo -= bytes_per_UCS_2;
#ifdef ___DEFAULT_CHAR_ENCODING_TO_BIG_ENDIAN
                options += ___CHAR_ENCODING_UCS_2BE-___CHAR_ENCODING_UCS_2;
                goto decode_next_UCS_2BE;
#else
                options += ___CHAR_ENCODING_UCS_2LE-___CHAR_ENCODING_UCS_2;
                goto decode_next_UCS_2LE;
#endif
              }
            else
              {
                blo -= bytes_per_UCS_2;
                if (bytes_per_UCS_2 > 1 &&
                    blo < bhi &&   /* at least one byte left in buffer? */
                    eof != ___FAL) /* end-of-file was reached? */
                  e = ___FIX(___CTOS_UCS_2STRING_ERR); /* trailing partial! */
              }
            break;
          }

        case ___CHAR_ENCODING_UCS_2BE:
          DECODE_CHARS_LOOP(decode_next_UCS_2BE,
                            bytes_per_UCS_2,
                            0xffff,
                            ___FIX(___CTOS_UCS_2STRING_ERR),
                            get_UCS_2BE);

        case ___CHAR_ENCODING_UCS_2LE:
          DECODE_CHARS_LOOP(decode_next_UCS_2LE,
                            bytes_per_UCS_2,
                            0xffff,
                            ___FIX(___CTOS_UCS_2STRING_ERR),
                            get_UCS_2LE);

        case ___CHAR_ENCODING_UCS_4:
          {
            blo += bytes_per_UCS_4;
            if (blo <= bhi)
              {
                ___UCS_4 cle;
                c = get_UCS_4BE(-1);
                if (c == unicode_BOM)
                  {
                    options += ___CHAR_ENCODING_UCS_4BE-___CHAR_ENCODING_UCS_4;
                    goto decode_next_UCS_4BE;
                  }
                cle = ((((((c&0xff) << 8) +
                          ((c>>8)&0xff)) << 8) +
                        ((c>>16)&0xff)) << 8) +
                      ((c>>24)&0xff);
                if (cle == unicode_BOM)
                  {
                    options += ___CHAR_ENCODING_UCS_4LE-___CHAR_ENCODING_UCS_4;
                    goto decode_next_UCS_4LE;
                  }
                blo -= bytes_per_UCS_4;
#ifdef ___DEFAULT_CHAR_ENCODING_TO_BIG_ENDIAN
                options += ___CHAR_ENCODING_UCS_4BE-___CHAR_ENCODING_UCS_4;
                goto decode_next_UCS_4BE;
#else
                options += ___CHAR_ENCODING_UCS_4LE-___CHAR_ENCODING_UCS_4;
                goto decode_next_UCS_4LE;
#endif
              }
            else
              {
                blo -= bytes_per_UCS_4;
                if (bytes_per_UCS_4 > 1 &&
                    blo < bhi &&   /* at least one byte left in buffer? */
                    eof != ___FAL) /* end-of-file was reached? */
                  e = ___FIX(___CTOS_UCS_4STRING_ERR); /* trailing partial! */
              }
            break;
          }

        case ___CHAR_ENCODING_UCS_4BE:
          DECODE_CHARS_LOOP(decode_next_UCS_4BE,
                            bytes_per_UCS_4,
                            0xffffffff,
                            ___FIX(___CTOS_UCS_4STRING_ERR),
                            get_UCS_4BE);

        case ___CHAR_ENCODING_UCS_4LE:
          DECODE_CHARS_LOOP(decode_next_UCS_4LE,
                            bytes_per_UCS_4,
                            0xffffffff,
                            ___FIX(___CTOS_UCS_4STRING_ERR),
                            get_UCS_4LE);
        }
    }

  /*
   * either the character buffer is full (chi == end) or no more
   * characters can be extracted from the byte buffer either because
   * the remaining bytes are not long enough to form a character (e ==
   * ___FIX(___NO_ERR)) or don't form a valid character (e !=
   * ___FIX(___NO_ERR)).
   */

  if (chi == char_rbuf)
    {
      /* could not extract a single character from the byte buffer */

      if (e == ___FIX(___NO_ERR))
        {
          /*
           * the remaining bytes in the buffer (possibly zero bytes)
           * are not long enough to form a character so make some room
           * at the end of the byte buffer to be filled with new bytes.
           */

          ___U8 *p = byte_rbuf;

          if (p != blo)
            {
              while (blo < bhi)
                *p++ = *blo++;
              blo = byte_rbuf;
              bhi = p;
            }
        }

      result = ___FAL; /* need to fill byte buffer */
    }
  else
    {
      e = ___FIX(___NO_ERR); /* only report errors at head of char buffer */
      result = ___TRU; /* no need to fill byte buffer */
    }

  ___FIELD(port,___PORT_CHAR_RLO) = ___FIX(0);
  ___FIELD(port,___PORT_CHAR_RHI) = ___FIX(chi-char_rbuf);
  ___FIELD(port,___PORT_BYTE_RLO) = ___FIX(blo-byte_rbuf);
  ___FIELD(port,___PORT_BYTE_RHI) = ___FIX(bhi-byte_rbuf);
  ___FIELD(port,___PORT_ROPTIONS) = ___FIX(options);

  if (e != ___FIX(___NO_ERR))
    return e;

  return result;
}


___SCMOBJ ___os_port_encode_chars
   ___P((___SCMOBJ port),
        (port)
___SCMOBJ port;)
{
  ___SCMOBJ e;
  ___SCMOBJ result;
  ___C *char_wbuf =
     ___CAST(___C*,___BODY_AS(___FIELD(port,___PORT_CHAR_WBUF),___tSUBTYPED));
  ___C *clo =
     char_wbuf + ___INT(___FIELD(port,___PORT_CHAR_WLO));
  ___C *chi =
     char_wbuf + ___INT(___FIELD(port,___PORT_CHAR_WHI));
  ___U8 *byte_wbuf =
     ___CAST(___U8*,___BODY_AS(___FIELD(port,___PORT_BYTE_WBUF),___tSUBTYPED));
  ___U8 *bhi =
     byte_wbuf + ___INT(___FIELD(port,___PORT_BYTE_WHI));
  ___U8 *end =
     byte_wbuf + ___HD_BYTES(___HEADER(___FIELD(port,___PORT_BYTE_WBUF)));
  ___UCS_4 c;
  int options = ___INT(___FIELD(port,___PORT_WOPTIONS));

#if 0
  if ((___INT(___FIELD(port,___PORT_WKIND)) & ___TTY_DEVICE_KIND) ==
      ___TTY_DEVICE_KIND)
    {
      /**************** this special handling should be removed */
      /*
       * Characters written to tty stream devices are represented as
       * 8, 16 or 32 bit unsigned integers of the machine's
       * endianness.  When the tty stream device driver transfers the
       * characters to the device, the characters will be encoded
       * using the encoding specified in the ___PORT_WOPTIONS field.
       */

      options = (options - ___CHAR_ENCODING(options)) +
                ___CS_SELECT(___CHAR_ENCODING_U8,
                             ___CHAR_ENCODING_U16,
                             ___CHAR_ENCODING_U32);
    }
#endif

  e = ___FIX(___NO_ERR);

  result = ___TRU; /* default is to empty byte buffer */

  /* empty character buffer as much as possible */

  if (clo < chi)
    {
      switch (___CHAR_ENCODING(options))
        {
        default:
        case ___CHAR_ENCODING_ASCII:
        case ___CHAR_ENCODING_ISO_8859_1:
          ENCODE_CHARS_LOOP(encode_next_ISO_8859_1,
                            bytes_per_ISO_8859_1,
                            max_ISO_8859_1,
                            ___FIX(___CTOS_ISO_8859_1STRING_ERR),
                            put_ISO_8859_1);

        case ___CHAR_ENCODING_UTF_8:
          {
            encode_next_UTF_8:
            c = *clo++;
            if (___MAX_CHR <= max_UTF_8 ||
                c <= max_UTF_8)
              {
                bhi += bytes_per_UTF_8;
                if (bhi <= end)
                  {
                    if (c != char_EOL)
                      {
                        put_UTF_8(-1,c);
                        if (clo < chi)
                          goto encode_next_UTF_8;
                      }
                    else
                      {
                        ENCODE_EOL(encode_next_UTF_8,bytes_per_UTF_8,put_UTF_8);
                      }
                  }
                else
                  {
                    bhi -= bytes_per_UTF_8;
                    clo--;
                    goto encode_chars_end;
                  }
              }
            else
              {
                ___U8 *p;
                int bytes;
                if      (c <= 0x7ff)      bytes = 2;
                else if (c <= 0xffff)     bytes = 3;
                else if (c <= 0x1fffff)   bytes = 4;
                else if (c <= 0x3ffffff)  bytes = 5;
                else if (c <= 0x7fffffff) bytes = 6;
                else
                  {
                    e = ___FIX(___STOC_UTF_8STRING_ERR);
                    goto encode_chars_end;
                  }
                p = bhi + bytes;
                if (p <= end)
                  {
                    bhi = p;
                    switch (bytes)
                      {
                      case 6:  *--p = 0x80+(c&0x3f); c >>= 6;
                      case 5:  *--p = 0x80+(c&0x3f); c >>= 6;
                      case 4:  *--p = 0x80+(c&0x3f); c >>= 6;
                      case 3:  *--p = 0x80+(c&0x3f); c >>= 6;
                      default: *--p = 0x80+(c&0x3f); c >>= 6;
                      }
                    *--p = 0xff - (0xff>>bytes) + c;
                    if (clo < chi)
                      goto encode_next_UTF_8;
                  }
                else
                  {
                    clo--;
                    goto encode_chars_end;
                  }
              }
            break;
          }

        case ___CHAR_ENCODING_UCS_2:
          bhi += bytes_per_UCS_2;
          if (bhi > end)
            {
              bhi -= bytes_per_UCS_2;
              goto encode_chars_end;
            }
#ifdef ___DEFAULT_CHAR_ENCODING_TO_BIG_ENDIAN
          put_UCS_2BE(-1,unicode_BOM);
          options += ___CHAR_ENCODING_UCS_2BE-___CHAR_ENCODING_UCS_2;
          ___FIELD(port,___PORT_WOPTIONS) = ___FIX(options);
          goto encode_next_UCS_2BE;
#else
          put_UCS_2LE(-1,unicode_BOM);
          options += ___CHAR_ENCODING_UCS_2LE-___CHAR_ENCODING_UCS_2;
          ___FIELD(port,___PORT_WOPTIONS) = ___FIX(options);
          goto encode_next_UCS_2LE;
#endif

        case ___CHAR_ENCODING_UCS_2BE:
          ENCODE_CHARS_LOOP(encode_next_UCS_2BE,
                            bytes_per_UCS_2,
                            max_UCS_2,
                            ___FIX(___CTOS_UCS_2STRING_ERR),
                            put_UCS_2BE);

        case ___CHAR_ENCODING_UCS_2LE:
          ENCODE_CHARS_LOOP(encode_next_UCS_2LE,
                            bytes_per_UCS_2,
                            max_UCS_2,
                            ___FIX(___CTOS_UCS_2STRING_ERR),
                            put_UCS_2LE);

        case ___CHAR_ENCODING_UCS_4:
          bhi += bytes_per_UCS_4;
          if (bhi > end)
            {
              bhi -= bytes_per_UCS_4;
              goto encode_chars_end;
            }
#ifdef ___DEFAULT_CHAR_ENCODING_TO_BIG_ENDIAN
          put_UCS_4BE(-1,unicode_BOM);
          options += ___CHAR_ENCODING_UCS_4BE-___CHAR_ENCODING_UCS_4;
          ___FIELD(port,___PORT_WOPTIONS) = ___FIX(options);
          goto encode_next_UCS_4BE;
#else
          put_UCS_4LE(-1,unicode_BOM);
          options += ___CHAR_ENCODING_UCS_4LE-___CHAR_ENCODING_UCS_4;
          ___FIELD(port,___PORT_WOPTIONS) = ___FIX(options);
          goto encode_next_UCS_4LE;
#endif

        case ___CHAR_ENCODING_UCS_4BE:
          ENCODE_CHARS_LOOP(encode_next_UCS_4BE,
                            bytes_per_UCS_4,
                            max_UCS_4,
                            ___FIX(___CTOS_UCS_4STRING_ERR),
                            put_UCS_4BE);

        case ___CHAR_ENCODING_UCS_4LE:
          ENCODE_CHARS_LOOP(encode_next_UCS_4LE,
                            bytes_per_UCS_4,
                            max_UCS_4,
                            ___FIX(___CTOS_UCS_4STRING_ERR),
                            put_UCS_4LE);
        }
    }

  /* character buffer has been fully emptied */

  ___FIELD(port,___PORT_CHAR_WCHARS) =
    ___FIELD(port,___PORT_CHAR_WCHARS) + ___FIX(chi-char_wbuf);

  clo = char_wbuf;
  chi = char_wbuf;

  result = ___FAL; /* do not empty byte buffer */

  encode_chars_end:

  ___FIELD(port,___PORT_CHAR_WLO) = ___FIX(clo-char_wbuf);
  ___FIELD(port,___PORT_CHAR_WHI) = ___FIX(chi-char_wbuf);
  ___FIELD(port,___PORT_BYTE_WHI) = ___FIX(bhi-byte_wbuf);

  /*****************  ___printf ("clo=%d chi=%d blo=%d bhi=%d\n", clo-char_wbuf, chi-char_wbuf, ___INT(___FIELD(port,___PORT_BYTE_WLO)), bhi-byte_wbuf); */

  if (e != ___FIX(___NO_ERR))
    return e;

  return result;
}


/*---------------------------------------------------------------------------*/

/* I/O module initialization/finalization. */


___HIDDEN ___SCMOBJ io_module_setup ___PVOID
{
  ___SCMOBJ e;

  /**************** ___printf ("io_module_setup\n"); */

  if ((e = ___device_group_setup (&io_mod.dgroup)) == ___FIX(___NO_ERR))
    {
#ifdef USE_POSIX

      ___set_signal_handler (SIGCHLD, sigchld_signal_handler);

#endif

#ifdef USE_WIN32

#define WINSOCK_MAJOR 1
#define WINSOCK_MINOR 1

      WSADATA winsock_data;

      if (!WSAStartup (MAKEWORD(WINSOCK_MAJOR, WINSOCK_MINOR), &winsock_data))
        {
          if (LOBYTE(winsock_data.wVersion) == WINSOCK_MINOR &&
              HIBYTE(winsock_data.wVersion) == WINSOCK_MAJOR)
            return ___FIX(___NO_ERR);
          WSACleanup (); /* ignore error */
        }

      e = ___FIX(___UNKNOWN_ERR);

      ___device_group_cleanup (io_mod.dgroup);

#endif
    }

  return e;
}


___HIDDEN void io_module_cleanup ___PVOID
{
  /********** ___printf ("io_module_cleanup\n"); */

#ifdef USE_POSIX

  ___set_signal_handler (SIGCHLD, SIG_DFL);

#endif

#ifdef USE_WIN32

  WSACleanup (); /* ignore error */

#endif

  /******************* tty_module_cleanup (); */
  ___device_group_cleanup (io_mod.dgroup);
}



___SCMOBJ ___setup_io_module ___PVOID
{
  if (!___io_mod.setup)
    {
#ifdef USE_WIN32

      ___SCMOBJ e = ___FIX(___NO_ERR);

      ___io_mod.always_signaled = NULL;
      ___io_mod.abort_select = NULL;

      ___io_mod.always_signaled =
        CreateEvent (NULL,  /* can't inherit */
                     TRUE,  /* manual reset */
                     TRUE,  /* signaled */
                     NULL); /* no name */

      if (___io_mod.always_signaled == NULL)
        e = err_code_from_GetLastError ();
      else
        {
          ___io_mod.abort_select =
            CreateEvent (NULL,  /* can't inherit */
                         TRUE,  /* manual reset */
                         FALSE, /* not signaled */
                         NULL); /* no name */

          if (___io_mod.abort_select == NULL)
            {
              CloseHandle (___io_mod.always_signaled); /* ignore error */
              e = err_code_from_GetLastError ();
            }
        }

#endif

      io_module_setup ();/*****************************/
      ___io_mod.setup = 1;
      return ___FIX(___NO_ERR);
    }

  return ___FIX(___UNKNOWN_ERR);
}


void ___cleanup_io_module ___PVOID
{
  if (___io_mod.setup)
    {
      io_module_cleanup ();/*****************************/
#ifdef USE_WIN32
      CloseHandle (___io_mod.abort_select); /* ignore error */
      CloseHandle (___io_mod.always_signaled); /* ignore error */
#endif
      ___io_mod.setup = 0;
    }
}


/*---------------------------------------------------------------------------*/