/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */

#ifndef __EVENTS_H__
#define __EVENTS_H__

#include <ev.h>

typedef enum evt_ret_e
{
	/* non-errors */
	EVT_OK = 1,

	/* errors */
	EVT_BADPTR = -1,
	EVT_ERROR = -2,
	EVT_BADPARAM = -3

} evt_ret_t;

typedef enum evt_type_e
{
	EVT_SIGNAL,
	EVT_CHILD,
	EVT_IO,

	EVT_TYPE_LAST,
	EVT_TYPE_FIRST = EVT_SIGNAL,
	EVT_TYPE_COUNT = EVT_TYPE_LAST - EVT_TYPE_FIRST

} evt_type_t;

#define VALID_EVENT_TYPE( t ) ((t >= EVT_TYPE_FIRST) && (t < EVT_TYPE_LAST))

typedef enum evt_io_type_e
{
	EVT_IO_READ =  0x01,
	EVT_IO_WRITE = 0x02
} evt_io_type_t;

typedef union evt_params_u
{
	struct
	{
		int signum;
	} signal_params;

	struct
	{
		int pid;	/* pid to watch */
		int trace;	/* 0 == only signal upon termination, 1 == also signal when stopped/continued */
		int rpid;	/* pid of process causing change */
		int rstatus;/* status word of process, use macros from sys/wait.h, waitpid */
	} child_params;

	struct
	{
		int fd;
		evt_io_type_t types;
	} io_params;

} evt_params_t;

typedef struct ev_loop evt_loop_t;
typedef struct evt_s evt_t;
typedef evt_ret_t (*evt_fn)( evt_loop_t * const el,
							 evt_t * const evt,
							 evt_params_t * const params,
							 void * user_data );

typedef union ev_data_u
{
	struct ev_signal	sig;
	struct ev_child		child;
	struct ev_io		io;
} ev_data_t;

struct evt_s
{
	ev_data_t		ev;			/* MUST BE FIRST */

	evt_type_t		evt_type;
	evt_params_t	evt_params;
	evt_fn			callback;
	void *			user_data;

	evt_loop_t *	el;			/* the event loop associated wtih */
};

/* allocate and initialize the events system */
evt_loop_t* evt_new( void );

/* deinitialize and free the events system */
void evt_delete( void * e );

int evt_initialize_event_handler( evt_t * const evt,
								  evt_type_t const t,
								  evt_params_t * const params,
								  evt_fn callback,
								  void * user_data );
void evt_deinitialize_event_handler( evt_t * const evt );

/* add a new event handler */
evt_t * evt_new_event_handler( evt_type_t const t,
							   evt_params_t * const params,
							   evt_fn callback,
							   void * user_data );

/* delete function for event handlers */
void evt_delete_event_handler( void * e );

/* start the event handler */
evt_ret_t evt_start_event_handler( evt_loop_t * const el,
								   evt_t * const evt );

/* stop the event handler */
evt_ret_t evt_stop_event_handler( evt_loop_t * const el,
								  evt_t * const evt );

/* runs the event loop */
evt_ret_t evt_run( evt_loop_t * const el );

/* stops the event loop */
evt_ret_t evt_stop( evt_loop_t * const el, int once );

#endif/*__EVENTS_H__*/
