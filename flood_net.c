/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2001 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 *
 * Originally developed by Aaron Bannert and Justin Erenkrantz, eBuilt.
 */

#include "config.h"
#include "flood_profile.h"
#include "flood_net.h"

struct flood_socket_t {
    apr_socket_t *socket;
    apr_pollfd_t *poll;
};

/* Open the TCP connection to the server */
flood_socket_t* open_socket(apr_pool_t *pool, request_t *r)
{
    apr_status_t rv = 0;
    apr_sockaddr_t *destsa;
    flood_socket_t* fs;
    
    fs = apr_palloc(pool, sizeof(flood_socket_t));

    if ((rv = apr_sockaddr_info_get(&destsa, r->parsed_uri->hostname, APR_INET, 
                                    r->parsed_uri->port, 0, pool)) 
                                    != APR_SUCCESS) {
        return NULL;
    }

    if ((rv = apr_socket_create(&fs->socket, APR_INET, SOCK_STREAM,
                                pool)) != APR_SUCCESS) {
        return NULL;
    }

    if ((rv = apr_connect(fs->socket, destsa)) != APR_SUCCESS) {
        if (APR_STATUS_IS_EINPROGRESS(rv)) {
            /* FIXME: Handle better */
            close_socket(fs);
            return NULL;
        }
        else {
            /* FIXME: Handle */
            close_socket(fs);
            return NULL;
        }
    }

    apr_setsocketopt(fs->socket, APR_SO_TIMEOUT, LOCAL_SOCKET_TIMEOUT);

    apr_poll_setup(&fs->poll, 1, pool);
    apr_poll_socket_add(fs->poll, fs->socket, APR_POLLIN);

    return fs;
}

/* close down TCP socket */
void close_socket(flood_socket_t *s)
{
    /* FIXME: recording and other stuff here? */
    apr_socket_close(s->socket);
}

apr_status_t read_socket(flood_socket_t *s, char *buf, int *buflen)
{
    apr_status_t e;
    int socketsRead = 1;

    e = apr_poll(s->poll, &socketsRead, LOCAL_SOCKET_TIMEOUT);
    if (e != APR_SUCCESS)
        return e;
    return apr_recv(s->socket, buf, buflen);
}

apr_status_t write_socket(flood_socket_t *s, request_t *r)
{
    apr_size_t l;
    apr_status_t e;

    l = r->rbufsize;

    e = apr_send(s->socket, r->rbuf, &l);

    /* FIXME: Better error and allow restarts? */
    if (l != r->rbufsize)
        return APR_EGENERAL;

    return APR_SUCCESS;     
}