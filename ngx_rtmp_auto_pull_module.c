/*
 * Copyright (C) AlexWoo(Wu Jie) wj19840501@gmail.com
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_cmd_module.h"
#include "ngx_rtmp_relay_module.h"
#include "ngx_stream_zone_module.h"
#include "ngx_event_multiport_module.h"


static ngx_rtmp_publish_pt              next_publish;
static ngx_rtmp_play_pt                 next_play;


static void *ngx_rtmp_auto_pull_create_app_conf(ngx_conf_t *cf);
static char *ngx_rtmp_auto_pull_merge_app_conf(ngx_conf_t *cf,
       void *parent, void *child);
static ngx_int_t ngx_rtmp_auto_pull_postconfiguration(ngx_conf_t *cf);


typedef struct {
    ngx_flag_t                          auto_pull;
    ngx_str_t                           auto_pull_port;
} ngx_rtmp_auto_pull_app_conf_t;


static ngx_command_t  ngx_rtmp_auto_pull_commands[] = {

    { ngx_string("rtmp_auto_pull"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_auto_pull_app_conf_t, auto_pull),
      NULL },

    { ngx_string("rtmp_auto_pull_port"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_auto_pull_app_conf_t, auto_pull_port),
      NULL },

      ngx_null_command
};


static ngx_rtmp_module_t  ngx_rtmp_auto_pull_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_rtmp_auto_pull_postconfiguration,   /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_rtmp_auto_pull_create_app_conf,     /* create app configuration */
    ngx_rtmp_auto_pull_merge_app_conf       /* merge app configuration */
};


ngx_module_t  ngx_rtmp_auto_pull_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_auto_pull_module_ctx,         /* module context */
    ngx_rtmp_auto_pull_commands,            /* module directives */
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
ngx_rtmp_auto_pull_create_app_conf(ngx_conf_t *cf)
{
    ngx_rtmp_auto_pull_app_conf_t      *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_auto_pull_app_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->auto_pull = NGX_CONF_UNSET;

    return conf;
}

static char *
ngx_rtmp_auto_pull_merge_app_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtmp_auto_pull_app_conf_t      *prev = parent;
    ngx_rtmp_auto_pull_app_conf_t      *conf = child;

    ngx_conf_merge_value(conf->auto_pull, prev->auto_pull, 1);
    ngx_conf_merge_str_value(conf->auto_pull_port, prev->auto_pull_port,
                             "unix:/tmp/rtmp_auto_pull.sock");

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_rtmp_auto_pull_target(ngx_rtmp_session_t *s,
        ngx_rtmp_relay_target_t *target, ngx_int_t pslot)
{
    ngx_rtmp_auto_pull_app_conf_t      *apcf;
    ngx_url_t                          *u;
    socklen_t                           len;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    ngx_memzero(target, sizeof(ngx_rtmp_relay_target_t));

    u = &target->url;
    u->url = s->name;
    target->name = s->name;
    target->app = s->app;
    target->tc_url = s->tc_url;
    target->page_url = s->page_url;
    target->swf_url = s->swf_url;
    target->flash_ver = s->flashver;

    u->naddrs = 1;
    u->addrs = ngx_pcalloc(s->connection->pool, sizeof(ngx_addr_t));
    if (u->addrs == NULL) {
        return NGX_ERROR;
    }

    if (apcf->auto_pull_port.len >= 5 &&
        ngx_strncasecmp(apcf->auto_pull_port.data, (u_char *) "unix:", 5) == 0)
    {
        len = sizeof(struct sockaddr_un);
    } else if (apcf->auto_pull_port.data[0] == '[') {
        len = sizeof(struct sockaddr_in);
    } else {
        len = sizeof(struct sockaddr_in6);
    }

    u->addrs[0].sockaddr = ngx_pcalloc(s->connection->pool, len);
    if (u->addrs[0].sockaddr == NULL) {
        return NGX_ERROR;
    }

    u->addrs[0].socklen = ngx_event_multiport_get_multiport(
            u->addrs[0].sockaddr, &apcf->auto_pull_port, pslot);
    if (u->addrs[0].socklen == 0) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static ngx_int_t
ngx_rtmp_auto_pull_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v)
{
    ngx_rtmp_auto_pull_app_conf_t      *apcf;
    ngx_int_t                           pslot;
    ngx_rtmp_relay_target_t             target;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    if (!apcf->auto_pull || s->relay) {
        goto next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "auto push, session %p publish %s", s, v->name);

    /* TODO for multiprocess stream should be serverid+app+name */
    pslot = ngx_stream_zone_insert_stream(&s->stream);
    if (pslot == NGX_ERROR) {
        return NGX_ERROR;
    }
    s->live_stream->pslot = pslot;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "auto push, session %p stream %s not in current process, "
            "pslot:%i ngx_process_slot:%i",
            s, v->name, pslot, ngx_process_slot);

    if (pslot == ngx_process_slot) {
        goto next;
    }

    if (ngx_rtmp_auto_pull_target(s, &target, pslot) == NGX_ERROR) {
        return NGX_ERROR;
    }

    ngx_rtmp_relay_push(s, &s->name, &target);

next:
    return next_publish(s, v);
}


static ngx_int_t
ngx_rtmp_auto_pull_play(ngx_rtmp_session_t *s, ngx_rtmp_play_t *v)
{
    ngx_rtmp_auto_pull_app_conf_t      *apcf;
    ngx_int_t                           pslot;
    ngx_rtmp_relay_target_t             target;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    if (!apcf->auto_pull || s->relay) {
        goto next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "auto pull, session %p play %s", s, v->name);

    if (s->live_stream->pslot != -1) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                "auto pull, session %p stream %s already in current process",
                s, v->name);
        goto next;
    }

    if (s->live_stream->pslot == -1) { /* first access for stream */
        /* TODO for multiprocess stream should be serverid+app+name */
        pslot = ngx_stream_zone_insert_stream(&s->stream);
        if (pslot == NGX_ERROR) {
            return NGX_ERROR;
        }
        s->live_stream->pslot = pslot;
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                "auto pull, session %p stream %s not in current process, "
                "pslot:%i ngx_process_slot:%i",
                s, v->name, pslot, ngx_process_slot);
    }

    if (pslot == ngx_process_slot) {
        goto next;
    }

    if (ngx_rtmp_auto_pull_target(s, &target, pslot) == NGX_ERROR) {
        return NGX_ERROR;
    }

    s->auto_pulled = 1;
    ngx_rtmp_relay_pull(s, &s->name, &target);

next:
    return next_play(s, v);
}


static ngx_int_t
ngx_rtmp_auto_pull_postconfiguration(ngx_conf_t *cf)
{
    /* chain handlers */

    next_publish = ngx_rtmp_publish;
    ngx_rtmp_publish = ngx_rtmp_auto_pull_publish;

    next_play = ngx_rtmp_play;
    ngx_rtmp_play = ngx_rtmp_auto_pull_play;

    return NGX_OK;
}

