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

#include <strings.h>    /* strncasecmp */
#include <apr_errno.h>
#include <apr_thread_proc.h>
#include <threadproc.h>
#include <apr_strings.h>
#include "config.h"
#include "flood_farmer.h"

#include "flood_farm.h"

extern apr_file_t *local_stdout;
extern apr_file_t *local_stderr;

struct farm_t {
    const char *name;
    int n_farmers;
#if APR_HAS_THREADS
    apr_thread_t **farmers; /* pointer to array of threads */
#else
    apr_proc_t **farmers;   /* pointer to array of processes */
#endif
};
typedef struct farm_t farm_t;

struct farmer_worker_info_t {
    const char *farmer_name;
    config_t *config;
};
typedef struct farmer_worker_info_t farmer_worker_info_t;

#if APR_HAS_THREADS
/**
 * Worker function that is assigned to a thread. Each worker is
 * called a farmer in our system.
 */
void * APR_THREAD_FUNC farmer_worker(apr_thread_t *thd, void *data)
{
    apr_status_t stat;
    apr_pool_t *pool;
    farmer_worker_info_t *info;

    info = (farmer_worker_info_t *)data;
    pool = apr_thread_pool_get(thd);

    /* should we create a subpool here? */
#ifdef FARM_DEBUG
    apr_file_printf(local_stdout, "Starting farmer_worker thread '%s'.\n",
                    info->farmer_name);
#endif

    if ((stat = run_farmer(info->config, info->farmer_name,
                           pool)) != APR_SUCCESS) {
        char buf[256];
        apr_strerror(stat, (char*) &buf, 256);
        apr_file_printf(local_stderr, "Error running farmer '%s': %s.\n",
                        info->farmer_name, (char*) &buf);
        /* just die for now, later try to return status */
    }

#if 0 /* this gets uncommented after apr_thread_exit() fixes are commited */
    apr_thread_exit(thd, APR_SUCCESS);
#endif
    return NULL;
}
#else
void *farmer_worker(void *data)
{
    apr_status_t stat;
    apr_pool_t *pool;
    farmer_worker_info_t *info;

    info = (farmer_worker_info_t *)data;
    apr_pool_create(&pool, NULL);

    /* should we create a subpool here? */
#ifdef FARM_DEBUG
    apr_file_printf(local_stdout, "Starting farmer_worker child '%s'.\n",
                    info->farmer_name);
#endif

    if ((stat = run_farmer(info->config, info->farmer_name,
                           pool)) != APR_SUCCESS) {
        char buf[256];
        apr_strerror(stat, (char*) &buf, 256);
        apr_file_printf(local_stderr, "Error running farmer '%s': %s.\n",
                        info->farmer_name, (char*) &buf);
        /* just die for now, later try to return status */
    }

    return NULL;
}
#endif

apr_status_t run_farm(config_t *config, const char *farm_name, apr_pool_t *pool)
{
#if APR_HAS_THREADS
    apr_status_t child_stat;
#endif
    apr_status_t stat;
    int usefarmer_count, i, j;
    long farmer_start_count;
    apr_time_t farmer_start_delay;
    char *xml_farm, **usefarmer_names;
    struct apr_xml_elem *e, *root_elem, *farm_elem;
    struct apr_xml_attr *use_elem;
    farm_t *farm;
    farmer_worker_info_t *infovec;

    farmer_start_delay = 0;

    xml_farm = apr_pstrdup(pool, XML_FARM);

    /* get the root config node */
    if ((stat = retrieve_root_xml_elem(&root_elem, config)) != APR_SUCCESS) {
        return stat;
    }

    /* get farmer node from config */
    if ((stat = retrieve_xml_elem_with_childmatch(
             &farm_elem, root_elem,
             xml_farm, XML_FARM_NAME, farm_name)) != APR_SUCCESS)
        return stat;

    /* count the number of "usefarmer" children */
    usefarmer_count = 0;
    for (e = farm_elem->first_child; e; e = e->next) {
        if (strncasecmp(e->name, XML_FARM_USEFARMER, FLOOD_STRLEN_MAX) == 0) {
            for (use_elem = e->attr; use_elem; use_elem = use_elem->next) {
                if (use_elem->name &&
                    strncasecmp(use_elem->name, XML_FARM_USEFARMER_COUNT, 
                                FLOOD_STRLEN_MAX) == 0) {
                    usefarmer_count += strtol(use_elem->value, NULL, 10);
                } 
            }
        }
    }

    if (!usefarmer_count)
        usefarmer_count = 1;

    /* create each of the children and put their names in an array */
    usefarmer_names = apr_palloc(pool, sizeof(char*) * (usefarmer_count + 1));
    /* set the sentinel (no need for pcalloc()) */
    usefarmer_names[usefarmer_count] = NULL; 
    i = 0;
    for (e = farm_elem->first_child; e; e = e->next) {
        int handled_usefarmers = 0;

        if (strncasecmp(e->name, XML_FARM_USEFARMER, FLOOD_STRLEN_MAX) == 0) {
            for (use_elem = e->attr; use_elem; use_elem = use_elem->next) {
                if (use_elem->name && strncasecmp(use_elem->name, 
                            XML_FARM_USEFARMER_COUNT, FLOOD_STRLEN_MAX) == 0) {
                    for (j = strtol(use_elem->value, NULL, 10); j > 0; j--) {
                        usefarmer_names[i++] = apr_pstrdup(pool, 
                                                   e->first_cdata.first->text);
                    }
                    handled_usefarmers = 1;
                }
                else if (use_elem->name &&
                         strncasecmp(use_elem->name, XML_FARM_USEFARMER_DELAY, 
                                     FLOOD_STRLEN_MAX) == 0) {
                    char *endptr;
                    farmer_start_delay = strtoll(use_elem->value, &endptr, 10);
                    if (*endptr != '\0')
                    {
                        apr_file_printf(local_stderr,
                                        "Attribute %s has invalid value %s.\n",
                                        XML_FARM_USEFARMER_DELAY, 
                                        use_elem->value);
                        return APR_EGENERAL;
                    }
                    farmer_start_delay *= APR_USEC_PER_SEC;
                }
                else if (use_elem->name && 
                         strncasecmp(use_elem->name, XML_FARM_USEFARMER_START, 
                                     FLOOD_STRLEN_MAX) == 0) {
                    char *endptr;
                    farmer_start_count = strtol(use_elem->value, &endptr, 10);
                    if (*endptr != '\0')
                    {
                        apr_file_printf(local_stderr,
                                        "Attribute %s has invalid value %s.\n",
                                        XML_FARM_USEFARMER_START, 
                                        use_elem->value);
                        return APR_EGENERAL;
                    }
                }
            }
            if (!handled_usefarmers)
                usefarmer_names[i++] = apr_pstrdup(pool, 
                                                   e->first_cdata.first->text);
        }
    }

    /* create the farm object */
    farm = apr_pcalloc(pool, sizeof(farm_t));
    farm->name = apr_pstrdup(pool, farm_name);
    farm->n_farmers = usefarmer_count;
#if APR_HAS_THREADS
    farm->farmers = apr_pcalloc(pool, 
                                sizeof(apr_thread_t*) * (usefarmer_count + 1));
#else
    farm->farmers = apr_pcalloc(pool, 
                                sizeof(apr_proc_t*) * (usefarmer_count + 1));

    for (i = 0; i < usefarmer_count + 1; i++)
        farm->farmers[i] = apr_pcalloc(pool, sizeof(apr_proc_t*));

#endif

    infovec = apr_pcalloc(pool, sizeof(farmer_worker_info_t) * usefarmer_count);

    /* for each of my farmers, start them */
    for (i = 0; i < usefarmer_count; i++) {
        infovec[i].farmer_name = usefarmer_names[i];
        infovec[i].config = config;
#if APR_HAS_THREADS
        if ((stat = apr_thread_create(&farm->farmers[i],
                                      NULL,
                                      farmer_worker,
                                      (void*) &infovec[i],
                                      pool)) != APR_SUCCESS) {
            /* error, perhaps shutdown other threads then exit? */
            return stat;
        }
#else
        if (apr_proc_fork(farm->farmers[i], pool) == APR_INCHILD)
        {
            farmer_worker(&infovec[i]);
            exit(0);
        }
#endif
        if (farmer_start_delay && (i+1) % farmer_start_count == 0)
            apr_sleep(farmer_start_delay);
    }

    for (i = 0; i < usefarmer_count; i++) {
#if APR_HAS_THREADS
        if ((stat = apr_thread_join(&child_stat, farm->farmers[i])) != APR_SUCCESS) {
#else
        if ((stat = apr_proc_wait(farm->farmers[i],NULL,
                                  APR_WAIT)) != APR_CHILD_DONE) {
#endif

            apr_file_printf(local_stderr, "Error joining farmer thread '%d' ('%s').\n",
                            i, usefarmer_names[i]);
            return stat;
        } else {
#ifdef FARM_DEBUG
            apr_file_printf(local_stdout, "Farmer '%d' ('%s') completed successfully.\n",
                            i, usefarmer_names[i]);
#endif
        }
    }

    return APR_SUCCESS;
}

