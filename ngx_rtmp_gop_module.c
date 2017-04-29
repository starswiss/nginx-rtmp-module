/*
 * Copyright (C) AlexWoo(Wu Jie) wj19840501@gmail.com
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp.h"


static void *ngx_rtmp_cache_create_app_conf(ngx_conf_t *cf);
static char *ngx_rtmp_cache_merge_app_conf(ngx_conf_t *cf, void *parent,
       void *child);


typedef struct {
    ngx_msec_t                          cache_time;
    ngx_msec_t                          latency_time;
} ngx_rtmp_cache_app_conf_t;


static ngx_command_t  ngx_rtmp_cache_commands[] = {

    { ngx_string("cache_time"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_cache_app_conf_t, cache_time),
      NULL },

    { ngx_string("latency_time"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_cache_app_conf_t, latency_time),
      NULL },

      ngx_null_command
};


static ngx_rtmp_module_t  ngx_rtmp_cache_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_rtmp_cache_create_app_conf,         /* create app configuration */
    ngx_rtmp_cache_merge_app_conf           /* merge app configuration */
};


ngx_module_t  ngx_rtmp_cache_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_cache_module_ctx,             /* module context */
    ngx_rtmp_cache_commands,                /* module directives */
    NGX_RTMP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_rtmp_live_create_app_conf(ngx_conf_t *cf)
{
    ngx_rtmp_cache_app_conf_t      *cacf;

    cacf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_cache_app_conf_t));
    if (cacf == NULL) {
        return NULL;
    }

    cacf->cache_time = NGX_CONF_UNSET_MSEC;
    cacf->latency_time = NGX_CONF_UNSET_MSEC;

    return cacf;
}


static char *
ngx_rtmp_live_merge_app_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtmp_cache_app_conf_t *prev = parent;
    ngx_rtmp_cache_app_conf_t *conf = child;

    ngx_conf_merge_msec_value(conf->cache_time, prev->cache_time, 0);

    return NGX_CONF_OK;
}


ngx_int_t
ngx_rtmp_cache_gop(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *pkt)
{
    return NGX_OK;
}

ngx_int_t
ngx_rtmp_send_gop(ngx_rtmp_session_t *s)
{
    return NGX_OK;
}

ngx_int_t
ngx_rtmp_send_frame(ngx_rtmp_session_t *s)
{
    return NGX_OK;
}
