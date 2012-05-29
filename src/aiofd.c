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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "debug.h"
#include "macros.h"
#include "array.h"
#include "events.h"
#include "aiofd.h"

typedef struct aiofd_write_s
{
	void * data;
	size_t size;
	int iov;
	size_t nleft;

} aiofd_write_t;


static evt_ret_t aiofd_write_fn( evt_loop_t * const el,
								 evt_t * const evt,
								 evt_params_t * const params,
								 void * user_data )
{
	int keep_evt_on = TRUE;
	int errval = 0;
	ssize_t written = 0;
	aiofd_t * aiofd = (aiofd_t*)user_data;

	DEBUG( "write event\n" );

	while ( array_size( &(aiofd->wbuf) ) > 0 )
	{
		/* we must have data to write */
		aiofd_write_t * wb = array_get_head( &(aiofd->wbuf) );

		if ( wb->iov )
		{
			written = writev( aiofd->wfd, (struct iovec *)wb->data, (int)wb->size );
		}
		else
		{
			written = write( aiofd->wfd, wb->data, wb->size );
		}

		/* try to write the data to the socket */
		if ( written < 0 )
		{
			if ( (errno == EAGAIN) || (errno == EWOULDBLOCK) )
			{
				DEBUG( "write would block...waiting for next write event\n" );
				break;
			}
			else
			{
				WARN( "write error: %d\n", errno );
				if ( aiofd->ops.error_fn != NULL )
				{
					DEBUG( "calling error callback\n" );
					(*(aiofd->ops.error_fn))( aiofd, errno, aiofd->user_data );
				}
				return EVT_OK;
			}
		}
		else
		{
			/* decrement how many bytes are left to write */
			wb->nleft -= written;

			/* check to see if everything has been written */
			if ( wb->nleft <= 0 )
			{
				/* remove the write buffer from the queue */
				array_pop_head( &(aiofd->wbuf) );

				/* call the write complete callback to let client know that a particular
				 * buffer has been written to the fd. */
				if ( aiofd->ops.write_fn != NULL )
				{
					DEBUG( "calling write complete callback\n" );
					keep_evt_on = (*(aiofd->ops.write_fn))( aiofd, wb->data, aiofd->user_data );
				}

				/* free it */
				FREE( wb );
			}
		}
	}

	/* call the write complete callback with NULL buffer to signal completion */
	if ( aiofd->ops.write_fn != NULL )
	{
		DEBUG( "calling write complete callback with null buffer\n" );
		keep_evt_on = (*(aiofd->ops.write_fn))( aiofd, NULL, aiofd->user_data );
	}
	
	if ( ! keep_evt_on )
	{
		/* stop the write event processing */
		evt_stop_event_handler( aiofd->el, &(aiofd->wevt) );
	}
	
	return EVT_OK;
}


static evt_ret_t aiofd_read_fn( evt_loop_t * const el,
								evt_t * const evt,
								evt_params_t * const params,
								void * user_data )
{
	int keep_going = TRUE;
	size_t nread = 0;
	aiofd_t * aiofd = (aiofd_t*)user_data;

	DEBUG( "read event\n" );

	/* get how much data is available to read */
	if ( ioctl( aiofd->rfd, FIONREAD, &nread ) < 0 )
	{
		if ( aiofd->ops.error_fn != NULL )
		{
			DEBUG( "calling error callback\n" );
			(*(aiofd->ops.error_fn))( aiofd, errno, aiofd->user_data );
		}
		return EVT_OK;
	}

	/* callback to tell client that there is data to read */
	if ( aiofd->ops.read_fn != NULL )
	{
		DEBUG( "calling read callback\n" );
		keep_going  = (*(aiofd->ops.read_fn))( aiofd, nread, aiofd->user_data );
	}

	/* we were told to stop the read event */
	if ( keep_going == FALSE )
	{
		/* stop the read event processing */
		evt_stop_event_handler( aiofd->el, &(aiofd->revt) );
	}

	return EVT_OK;
}


aiofd_t * aiofd_new( int const write_fd,
					 int const read_fd,
					 aiofd_ops_t * const ops,
					 evt_loop_t * const el,
					 void * user_data )
{
	aiofd_t * aiofd = NULL;
	
	/* allocate the the aiofd struct */
	aiofd = (aiofd_t*)CALLOC( 1, sizeof(aiofd_t) );
	CHECK_PTR_RET( aiofd, NULL );

	/* initlialize the aiofd */
	aiofd_initialize( aiofd, write_fd, read_fd, ops, el, user_data );

	return aiofd;
}

void aiofd_delete( void * aio )
{
	aiofd_t * aiofd = (aiofd_t*)aio;
	CHECK_PTR( aiofd );

	aiofd_deinitialize( aiofd );

	FREE( (void*)aiofd );
}

void aiofd_initialize( aiofd_t * const aiofd, 
					   int const write_fd,
					   int const read_fd,
					   aiofd_ops_t * const ops,
					   evt_loop_t * const el,
					   void * user_data )
{
	evt_params_t params;

	CHECK_PTR( aiofd );
	CHECK_PTR( ops );
	CHECK_PTR( el );
	CHECK( write_fd >= 0 );
	CHECK( read_fd >= 0 );

	MEMSET( (void*)aiofd, 0, sizeof(aiofd_t) );

	/* store the file descriptors */
	aiofd->wfd = write_fd;
	aiofd->rfd = read_fd;

	/* initialize the write buffer */
	array_initialize( &(aiofd->wbuf), 8, FREE );

	/* set up params for fd write event */
	params.io_params.fd = aiofd->wfd;
	params.io_params.types = EVT_IO_WRITE;

	/* hook up the fd write event */
	evt_initialize_event_handler( &(aiofd->wevt), 
								  aiofd->el, 
								  EVT_IO, 
								  &params, 
								  aiofd_write_fn, 
								  (void *)aiofd );

	/* set up params for socket read event */
	params.io_params.fd = aiofd->rfd;
	params.io_params.types = EVT_IO_READ;

	/* hook up the fd read event */
	evt_initialize_event_handler( &(aiofd->revt), 
								  aiofd->el, 
								  EVT_IO, 
								  &params, 
								  aiofd_read_fn, 
								  (void *)aiofd );

	/* store the event loop pointer */
	aiofd->el = el;
	
	/* store the user_data pointer */
	aiofd->user_data = user_data;

	/* copy the ops into place */
	MEMCPY( (void*)&(aiofd->ops), ops, sizeof(aiofd_ops_t) );
}

void aiofd_deinitialize( aiofd_t * const aiofd )
{
	/* stop write event */
	evt_stop_event_handler( aiofd->el, &(aiofd->wevt) );
	
	/* stop read event */
	evt_stop_event_handler( aiofd->el, &(aiofd->revt) );

	/* clean up the array of write buffers */
	array_deinitialize( &(aiofd->wbuf) );
}

int aiofd_enable_write_evt( aiofd_t * const aiofd,
							int enable )
{
	CHECK_RET( aiofd, FALSE );

	if ( enable )
	{
		evt_start_event_handler( aiofd->el, &(aiofd->wevt) );
	}
	else
	{
		evt_stop_event_handler( aiofd->el, &(aiofd->wevt) );
	}
	return TRUE;
}

int aiofd_enable_read_evt( aiofd_t * const aiofd,
						   int enable )
{
	CHECK_RET( aiofd, FALSE );

	if ( enable )
	{
		evt_start_event_handler( aiofd->el, &(aiofd->revt) );
	}
	else
	{
		evt_stop_event_handler( aiofd->el, &(aiofd->revt) );
	}
	return TRUE;
}

int32_t aiofd_read( aiofd_t * const aiofd,
					uint8_t * const buffer,
					int32_t const n )
{
	ssize_t res = 0;

	CHECK_PTR_RET(aiofd, 0);
	
	/* if they pass a NULL buffer pointer return -1 */
	CHECK_PTR_RET(buffer, 0);

	CHECK_RET(n > 0, 0);

	res = read( aiofd->rfd, buffer, n );
	switch ( (int)res )
	{
		case 0:
			errno = EPIPE;
		case -1:
			if ( aiofd->ops.error_fn != NULL )
			{
				(*(aiofd->ops.error_fn))( aiofd, errno, aiofd->user_data );
			}
			return 0;
		default:
			return (int32_t)res;
	}
}


/* queue up data to write to the fd */
static int aiofd_write_common( aiofd_t* const aiofd, 
							   void * buffer,
							   size_t cnt,
							   size_t total,
							   int iov )
{
	int_t asize = 0;
	ssize_t res = 0;
	aiofd_write_t * wb = NULL;
	
	CHECK_PTR_RET(aiofd, 0);
	CHECK_PTR_RET(buffer, 0);
	CHECK_RET(cnt > 0, 0);

	wb = CALLOC( 1, sizeof(aiofd_write_t) );
	if ( wb == NULL )
	{
		WARN( "failed to allocate write buffer struct\n" );
		return FALSE;
	}

	/* store the values */
	wb->data = buffer;
	wb->size = cnt;
	wb->iov = iov;
	wb->nleft = total;

	/* queue the write */
	array_push_tail( &(aiofd->wbuf), wb );

	/* just in case it isn't started, start the write event processing so
	 * the queued data will get written */
	evt_start_event_handler( aiofd->el, &(aiofd->wevt) );

	return TRUE;
}

int aiofd_write( aiofd_t * const aiofd, 
				 uint8_t const * const buffer, 
				 size_t const n )
{
	return aiofd_write_common( aiofd, (void*)buffer, n, n, FALSE );
}

int aiofd_writev( aiofd_t * const aiofd,
				  struct iovec * iov,
				  size_t iovcnt )
{
	int i;
	size_t total = 0;

	/* calculate how many bytes are in the iovec */
	for ( i = 0; i < iovcnt; i++ )
	{
		total += iov[i].iov_len;
	}

	return aiofd_write_common( aiofd, (void*)iov, iovcnt, total, TRUE );
}

int aiofd_flush( aiofd_t * const aiofd )
{
	CHECK_PTR_RET(aiofd, FALSE);
	
	fsync( aiofd->wfd );
	fsync( aiofd->rfd );
	
	return TRUE;
}
