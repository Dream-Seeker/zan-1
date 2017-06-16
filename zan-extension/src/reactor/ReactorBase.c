/*
  +----------------------------------------------------------------------+
  | Zan                                                                  |
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
  +----------------------------------------------------------------------+
*/

#include "swoole.h"
#include "swLog.h"
#include "swReactor.h"
#include "swSignal.h"
#include "swConnection.h"
#include "swAsyncIO.h"
#include "list.h"

static void swReactor_onTimeout_and_Finish(swReactor *reactor);
static void swReactor_onTimeout(swReactor *reactor);
static void swReactor_onFinish(swReactor *reactor);
static int swReactor_defer(swReactor *reactor, swCallback callback, void *data);
static int swReactor_setHandle(swReactor *reactor, int _fdtype, swReactor_handle handle);
static int swReactor_write(swReactor *reactor, int fd, void *buf, int n);
static void handle_defer_call(swReactor* reactor);

int swReactor_init(swReactor *reactor, int max_event)
{
	if (!reactor)
	{
		return SW_ERR;
	}

	bzero(reactor, sizeof(swReactor));

	int ret = 0;
#ifdef HAVE_EPOLL
	ret = swReactorEpoll_create(reactor, max_event);
#elif defined(HAVE_KQUEUE)
	ret = swReactorKqueue_create(reactor, max_event);
#elif defined(SW_MAINREACTOR_USE_POLL)
	ret = swReactorPoll_create(reactor, max_event);
#else
	ret = swReactorSelect_create(reactor);
#endif

	if (ret < 0)
	{
		return SW_ERR;
	}

    reactor->running = 1;
    reactor->setHandle = swReactor_setHandle;
    reactor->onFinish = swReactor_onFinish;
    reactor->onTimeout = swReactor_onTimeout;
    reactor->write = swReactor_write;
    reactor->defer = swReactor_defer;
    reactor->close = swReactor_close;

    reactor->socket_array = swArray_create(1024, sizeof(swConnection));
    if (!reactor->socket_array)
    {
        swWarn("create socket array failed.");
        reactor->free(reactor);
        return SW_ERR;
    }

    return SW_OK;
}

swReactor_handle swReactor_getHandle(swReactor *reactor, int event_type, int fdtype)
{
    if (event_type == SW_EVENT_WRITE)
    {
        return (reactor->write_handle[fdtype] != NULL) ?
        		reactor->write_handle[fdtype] : reactor->handle[SW_FD_WRITE];
    }
    if (event_type == SW_EVENT_ERROR)
    {
        return (reactor->error_handle[fdtype] != NULL) ?
        		reactor->error_handle[fdtype] : reactor->handle[SW_FD_CLOSE];
    }

    return reactor->handle[fdtype];
}

int swReactor_add_event(swReactor *reactor, int fd, enum swEvent_type event_type)
{
    swConnection *conn = swReactor_get(reactor, fd);
    if (!(conn->events & event_type))
    {
        return reactor->set(reactor, fd, conn->fdtype | conn->events | event_type);
    }

    return SW_OK;
}

int swReactor_del_event(swReactor *reactor, int fd, enum swEvent_type event_type)
{
    swConnection *conn = swReactor_get(reactor, fd);
    if (conn->events & event_type)
    {
        return reactor->set(reactor, fd, conn->fdtype | (conn->events & (~event_type)));
    }

    return SW_OK;
}

static int swReactor_setHandle(swReactor *reactor, int _fdtype, swReactor_handle handle)
{
    int fdtype = swReactor_fdtype(_fdtype);

    if (fdtype >= SW_MAX_FDTYPE)
    {
        swWarn("fdtype > SW_MAX_FDTYPE[%d]", SW_MAX_FDTYPE);
        return SW_ERR;
    }

    if (swReactor_event_read(_fdtype))
    {
        reactor->handle[fdtype] = handle;
    }
    else if (swReactor_event_write(_fdtype))
    {
        reactor->write_handle[fdtype] = handle;
    }
    else if (swReactor_event_error(_fdtype))
    {
        reactor->error_handle[fdtype] = handle;
    }
    else
    {
        swWarn("unknow fdtype");
        return SW_ERR;
    }

    return SW_OK;
}

static int swReactor_defer(swReactor *reactor, swCallback callback, void *data)
{
    swDefer_callback *cb = sw_malloc(sizeof(swDefer_callback));
    if (!cb)
    {
        swWarn("malloc(%ld) failed.", sizeof(swDefer_callback));
        return SW_ERR;
    }
    cb->callback = callback;
    cb->data = data;
    LL_APPEND(reactor->defer_callback_list, cb);
    return SW_OK;
}

swConnection* swReactor_get(swReactor *reactor, int fd)
{
    assert(fd < SwooleG.max_sockets);

    if (reactor->thread)
    {
        return &reactor->socket_list[fd];
    }

    swConnection *socket = swArray_alloc(reactor->socket_array, fd);
    if (socket == NULL)
    {
        return NULL;
    }

    if (!socket->active)
    {
        socket->fd = fd;
    }

    return socket;
}

int swReactor_add(swReactor *reactor, int fd, int fdtype)
{
    assert (fd <= SwooleG.max_sockets);

    swConnection *socket = swReactor_get(reactor, fd);

    socket->fdtype = swReactor_fdtype(fdtype);
    socket->events |= swReactor_events(fdtype);
    socket->removed = 0;

    swTraceLog(SW_TRACE_REACTOR, "fd=%d, type=%d, events=%d", fd, socket->socket_type, socket->events);

    return SW_OK;
}

int swReactor_del(swReactor *reactor, int fd)
{
    swConnection *socket = swReactor_get(reactor, fd);
    socket->events = 0;
    socket->removed = 1;
    return SW_OK;
}

void swReactor_set(swReactor *reactor, int fd, int fdtype)
{
    swConnection *socket = swReactor_get(reactor, fd);
    socket->events = swReactor_events(fdtype);
}

/**
 * execute when reactor timeout and reactor finish
 */
static void swReactor_onTimeout_and_Finish(swReactor *reactor)
{
    //check timer
    if (reactor->check_timer)
    {
        swTimer_select(&SwooleG.timer);
    }
    //server master
    if (SwooleG.serv && SwooleTG.update_time)
    {
        swoole_update_time();
    }
    //server worker
    swWorker *worker = SwooleWG.worker;
    if (worker != NULL && SwooleWG.reload)
    {
		SwooleWG.reload_count++;

		if (reactor->event_num <= 2 || SwooleWG.reload_count >= SW_MAX_RELOAD_WAIT)
		{
			reactor->running = 0;
		}
    }

    //client
    if (SwooleG.serv == NULL && SwooleG.timer.num <= 0)
    {
        if (SwooleAIO.init && reactor->event_num == 1 && SwooleAIO.task_num == 0)
        {
            reactor->running = 0;
        }
        else if (reactor->event_num == 0)
        {
            reactor->running = 0;
        }
    }
#ifdef SW_USE_MALLOC_TRIM
    malloc_trim();
#endif
}

static void swReactor_onTimeout(swReactor *reactor)
{
//    swReactor_onTimeout_and_Finish(reactor);

    if (reactor->disable_accept)
    {
        reactor->enable_accept(reactor);
        reactor->disable_accept = 0;
    }
}

static void swReactor_onFinish(swReactor *reactor)
{
    //check signal
    if (reactor->singal_no)
    {
        swSignal_callback(reactor->singal_no);
        reactor->singal_no = 0;
    }

    swReactor_onTimeout_and_Finish(reactor);

    //defer callback
    handle_defer_call(reactor);
}

static void handle_defer_call(swReactor* reactor)
{
	reactor->defer_list_backup = reactor->defer_callback_list;
	reactor->defer_callback_list = NULL;

	swDefer_callback *cb = NULL;
	swDefer_callback *tmp = NULL;
//	LL_FOREACH(reactor->defer_list_backup, cb)
//	{
//		cb->callback(cb->data);
//	}

	LL_FOREACH_SAFE(reactor->defer_list_backup, cb, tmp)
	{
		cb->callback(cb->data);
		sw_free(cb);
	}

	reactor->defer_list_backup = NULL;
}

int swReactor_close(swReactor *reactor, int fd)
{
    swConnection *socket = swReactor_get(reactor, fd);
    if (socket && socket->out_buffer)
    {
        swBuffer_free(socket->out_buffer);
        socket->out_buffer = NULL;
    }
    if (socket && socket->in_buffer)
    {
        swBuffer_free(socket->in_buffer);
        socket->in_buffer = NULL;
    }

#ifdef SW_USE_OPENSSL
    if (socket && socket->ssl)
    {
        swSSL_close(socket);
    }
#endif

    if (!reactor->thread && socket && !socket->removed)
    {
    	reactor->del(reactor,fd);
    }

    if (socket)
    {
    	bzero(socket, sizeof(swConnection));
    	socket->removed = 1;
    	socket->closed = 1;
    }

    return close(fd);
}

int swReactor_error(swReactor *reactor)
{
    switch (errno)
    {
    case EINTR:
        if (reactor->singal_no)
        {
            swSignal_callback(reactor->singal_no);
            reactor->singal_no = 0;
        }

        return SW_OK;
    }
    return SW_ERR;
}

static int swReactor_write(swReactor *reactor, int fd, void *buf, int n)
{
    int ret;
    swConnection *socket = swReactor_get(reactor, fd);
    swBuffer *buffer = socket->out_buffer;

    socket->fd = (socket->fd == 0)? fd:socket->fd;

    socket->buffer_size = (socket->buffer_size == 0)? SwooleG.socket_buffer_size:socket->buffer_size;

    if (swBuffer_empty(buffer))
    {
        if (socket->ssl_send)
        {
            goto do_buffer;
        }

        do_send:
        ret = swConnection_send(socket, buf, n, 0);
        if (ret > 0)
        {
            if (n == ret)
            {
                return ret;
            }
            else
            {
                buf += ret;
                n -= ret;
                goto do_buffer;
            }
        }
        
#ifdef HAVE_KQUEUE
        else if (errno == EAGAIN || errno == ENOBUFS)
#else
        else if (errno == EAGAIN)
#endif
        {
            do_buffer:
            if (!socket->out_buffer)
            {
                buffer = swBuffer_new(sizeof(swEventData));
                if (!buffer)
                {
                    swWarn("create worker buffer failed.");
                    return SW_ERR;
                }
                socket->out_buffer = buffer;
            }

            socket->events |= SW_EVENT_WRITE;

            if (socket->events & SW_EVENT_READ)
            {
                if (reactor->set(reactor, fd, socket->fdtype | socket->events) < 0)
                {
                    swSysError("reactor->set(%d, SW_EVENT_WRITE) failed.", fd);
                }
            }
            else
            {
                if (reactor->add(reactor, fd, socket->fdtype | SW_EVENT_WRITE) < 0)
                {
                    swSysError("reactor->add(%d, SW_EVENT_WRITE) failed.", fd);
                }
            }

            goto append_buffer;
        }
        else if (errno == EINTR)
        {
            goto do_send;
        }
        else
        {
            return SW_ERR;
        }
    }
    else
    {
        append_buffer:

        if (buffer->length > socket->buffer_size)
        {
            if (SwooleG.socket_dontwait)
            {
                return SW_ERR;
            }
            else
            {
                swWarn("socket[fd=%d, type=%d] output buffer overflow, reactor will block.", fd, socket->fdtype);
                swYield();
                swSocket_wait(fd, SW_SOCKET_OVERFLOW_WAIT, SW_EVENT_WRITE);
            }
        }

        if (swBuffer_append(buffer, buf, n) < 0)
        {
            return SW_ERR;
        }
    }

    return ret;
}

int swReactor_onWrite(swReactor *reactor, swEvent *ev)
{
    int ret;
    int fd = ev->fd;

    swConnection *socket = swReactor_get(reactor, fd);
    swBuffer_trunk *chunk = NULL;
    swBuffer *buffer = socket->out_buffer;

    //send to socket
    while (!swBuffer_empty(buffer))
    {
        chunk = swBuffer_get_trunk(buffer);
        if (chunk->type == SW_CHUNK_CLOSE)
        {
            close_fd:
            reactor->close(reactor, ev->fd);
            return SW_ASYNCERR;
        }
        else if (chunk->type == SW_CHUNK_SENDFILE)
        {
            ret = swConnection_onSendfile(socket, chunk);
        }
        else
        {
            ret = swConnection_buffer_send(socket);
        }

        if (ret < 0)
        {
            if (socket->close_wait)
            {
                goto close_fd;
            }
            else if (socket->send_wait)
            {
                return SW_OK;
            }
        }
    }

    //remove EPOLLOUT event
    if (swBuffer_empty(buffer))
    {
        if (socket->events & SW_EVENT_READ)
        {
            socket->events &= (~SW_EVENT_WRITE);
            if (reactor->set(reactor, fd, socket->fdtype | socket->events) < 0)
            {
                swSysError("reactor->set(%d, SW_EVENT_READ) failed.", fd);
            }
        }
        else
        {
            if (reactor->del(reactor, fd) < 0)
            {
                swSysError("reactor->del(%d) failed.", fd);
            }
        }
    }

    return SW_OK;
}

int swReactor_wait_write_buffer(swReactor *reactor, int fd)
{
    swConnection *conn = swReactor_get(reactor, fd);
    swEvent event;

    if (conn->out_buffer)
    {
    	swSetNonBlock(fd,0);
        event.fd = fd;
        return swReactor_onWrite(reactor, &event);
    }
    return SW_OK;
}
