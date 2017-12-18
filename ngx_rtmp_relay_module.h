
/*
 * Copyright (C) Roman Arutyunyan
 */


#ifndef _NGX_RTMP_RELAY_H_INCLUDED_
#define _NGX_RTMP_RELAY_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp.h"


typedef struct {
    ngx_url_t                       url;
    ngx_str_t                       app;
    ngx_str_t                       name;
    ngx_str_t                       tc_url;
    ngx_str_t                       page_url;
    ngx_str_t                       swf_url;
    ngx_str_t                       flash_ver;
    ngx_str_t                       play_path;
    ngx_int_t                       live;
    ngx_int_t                       start;
    ngx_int_t                       stop;

    void                           *tag;     /* usually module reference */
    void                           *data;    /* module-specific data */
    ngx_uint_t                      counter; /* mutable connection counter */
} ngx_rtmp_relay_target_t;


typedef struct {
    ngx_str_t                   name;
    ngx_str_t                   url;
    ngx_rtmp_session_t         *session;

    ngx_str_t                   pargs; /* play or publish ctx */

    ngx_str_t                   app;
    ngx_str_t                   args;
    ngx_str_t                   tc_url;
    ngx_str_t                   page_url;
    ngx_str_t                   swf_url;
    ngx_str_t                   flash_ver;
    uint32_t                    acodecs;
    uint32_t                    vcodecs;

    ngx_str_t                   play_path;
    ngx_int_t                   live;
    ngx_int_t                   start;
    ngx_int_t                   stop;

    void                       *tag;
    void                       *data;

    unsigned                    publishing:1;
} ngx_rtmp_relay_ctx_t;


extern ngx_module_t  ngx_rtmp_relay_module;


ngx_int_t ngx_rtmp_relay_pull(ngx_rtmp_session_t *s, ngx_str_t *name,
                              ngx_rtmp_relay_target_t *target);
ngx_int_t ngx_rtmp_relay_push(ngx_rtmp_session_t *s, ngx_str_t *name,
                              ngx_rtmp_relay_target_t *target);


#endif /* _NGX_RTMP_RELAY_H_INCLUDED_ */
