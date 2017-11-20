/*
 * Copyright (C) AlexWoo(Wu Jie) wj19840501@gmail.com
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_http.h>
#include "ngx_rtmp.h"
#include "ngx_rbuf.h"
#include "ngx_stream_zone_module.h"
#include "ngx_event_timer_module.h"
#include "ngx_event_resolver.h"
#include "ngx_dynamic_resolver.h"


static char *ngx_http_sys_stat(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


static ngx_command_t  ngx_http_sys_stat_commands[] = {

    { ngx_string("sys_stat"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_sys_stat,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_sys_stat_module_ctx = {
    NULL,                               /* preconfiguration */
    NULL,                               /* postconfiguration */

    NULL,                               /* create main configuration */
    NULL,                               /* init main configuration */

    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */

    NULL,                               /* create location configuration */
    NULL                                /* merge location configuration */
};


ngx_module_t  ngx_http_sys_stat_module = {
    NGX_MODULE_V1,
    &ngx_http_sys_stat_module_ctx,      /* module context */
    ngx_http_sys_stat_commands,         /* module directives */
    NGX_HTTP_MODULE,                    /* module type */
    NULL,                               /* init master */
    NULL,                               /* init module */
    NULL,                               /* init process */
    NULL,                               /* init thread */
    NULL,                               /* exit thread */
    NULL,                               /* exit process */
    NULL,                               /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_sys_stat_handler(ngx_http_request_t *r)
{
    ngx_chain_t                       **ll, *out;

    r->headers_out.status = NGX_HTTP_OK;
    ngx_http_send_header(r);

    ll = &out;

    *ll = ngx_rtmp_shared_state(r);

    if (*ll) {
        ll = &(*ll)->next;
    }
    *ll = ngx_rbuf_state(r);

    if (*ll) {
        ll = &(*ll)->next;
    }
    *ll = ngx_stream_zone_state(r, 0);

    if (*ll) {
        ll = &(*ll)->next;
    }
    *ll = ngx_event_timer_state(r);

    if (*ll) {
        ll = &(*ll)->next;
    }
    *ll = ngx_event_resolver_state(r);

    if (*ll) {
        ll = &(*ll)->next;
    }
    *ll = ngx_dynamic_resolver_state(r);

    (*ll)->buf->last_buf = 1;

    return ngx_http_output_filter(r, out);
}

static char *
ngx_http_sys_stat(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t           *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_sys_stat_handler;

    return NGX_CONF_OK;
}
