/*
 * Copyright (C) AlexWoo(Wu Jie) wj19840501@gmail.com
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_cmd_module.h"
#include "ngx_rtmp_relay_module.h"
#include "ngx_stream_zone_module.h"
#include "ngx_multiport.h"


static ngx_rtmp_publish_pt              next_publish;
static ngx_rtmp_play_pt                 next_play;
static ngx_rtmp_close_stream_pt         next_close_stream;


static void *ngx_rtmp_auto_pull_create_app_conf(ngx_conf_t *cf);
static char *ngx_rtmp_auto_pull_merge_app_conf(ngx_conf_t *cf,
       void *parent, void *child);
static ngx_int_t ngx_rtmp_auto_pull_postconfiguration(ngx_conf_t *cf);


typedef struct {
    ngx_flag_t                          auto_pull;
    ngx_str_t                           auto_pull_port;
    ngx_msec_t                          push_reconnect;
    ngx_msec_t                          pull_reconnect;
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

    { ngx_string("rtmp_auto_push_reconnect"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_auto_pull_app_conf_t, push_reconnect),
      NULL },

    { ngx_string("rtmp_auto_pull_reconnect"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_auto_pull_app_conf_t, pull_reconnect),
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
    conf->push_reconnect = NGX_CONF_UNSET_MSEC;
    conf->pull_reconnect = NGX_CONF_UNSET_MSEC;

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
    ngx_conf_merge_msec_value(conf->push_reconnect, prev->push_reconnect, 1000);
    ngx_conf_merge_msec_value(conf->pull_reconnect, prev->pull_reconnect, 1000);

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_rtmp_auto_pull_target(ngx_rtmp_session_t *s,
        ngx_rtmp_relay_target_t *target, ngx_int_t pslot)
{
    ngx_rtmp_auto_pull_app_conf_t      *apcf;
    ngx_url_t                          *u;
    ngx_str_t                           port;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    ngx_memzero(target, sizeof(ngx_rtmp_relay_target_t));

    u = &target->url;
    target->name = s->name;
    target->app = s->app;
    target->tc_url = s->tc_url;
    target->page_url = s->page_url;
    target->swf_url = s->swf_url;
    target->flash_ver = s->flashver;
    target->tag = &ngx_rtmp_auto_pull_module;
    target->data = s->live_stream;

    ngx_memzero(u, sizeof(ngx_url_t));
    ngx_memzero(&port, sizeof(ngx_str_t));

    if (ngx_multiport_get_port(s->connection->pool, &port,
            &apcf->auto_pull_port, pslot) == NGX_ERROR)
    {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                "auto pull, get mulitport error: %V", &apcf->auto_pull_port);
        return NGX_ERROR;
    }

    u->url = port;
    u->no_resolve = 1;

    if (ngx_parse_url(s->connection->pool, u) != NGX_OK) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                "auto pull, parse url failed '%V'", &u->url);
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_rtmp_auto_pull_pull_reconnect(ngx_event_t *ev)
{
    ngx_rtmp_session_t                 *s;
    ngx_relay_reconnect_t              *rc, **prc;
    ngx_live_stream_t                  *live_stream;
    ngx_rtmp_relay_target_t             target;
    ngx_rtmp_play_t                     v;
    ngx_int_t                           pslot;

    rc = ev->data;
    live_stream = rc->live_stream;

    if (live_stream->play_ctx == NULL) { /* all pull closed */
        ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                "auto pull, pull reconnect, all players closed");
        goto done;
    }

    s = live_stream->play_ctx->session;
    pslot = ngx_stream_zone_insert_stream(&s->stream);
    if (pslot == NGX_ERROR) { /* process next_play */
        goto next;
    }
    s->live_stream->pslot = pslot;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "auto pull reconnect , stream %V not in current process, "
            "pslot:%i ngx_process_slot:%i",
            &s->name, pslot, ngx_process_slot);

    if (pslot == ngx_process_slot) { /* curr process occupied stream */
        goto next;
    }

    if (ngx_rtmp_auto_pull_target(s, &target, pslot) == NGX_ERROR) {
        goto done;
    }

    live_stream->relay_pull_tag = NULL;
    ngx_rtmp_relay_pull(s, &s->name, &target);

    goto done;

next:
    ngx_memzero(&v, sizeof(ngx_rtmp_play_t));
    ngx_memcpy(v.name, s->name.data, s->name.len);
    ngx_memcpy(v.args, s->pargs.data, s->pargs.len);

    s->auto_pulled = 0;
    s->live_stream->relay_pull_tag = NULL;
    s->live_stream->relay_pull_data = NULL;
    next_play(s, &v);

done:
    for (prc = &live_stream->play_reconnect; *prc; prc = &(*prc)->next) {
        if (*prc == rc) {
            *prc = rc->next;
            ngx_live_put_relay_reconnect(rc);
            break;
        }
    }
}


static void
ngx_rtmp_auto_pull_push_reconnect(ngx_event_t *ev)
{
    ngx_rtmp_session_t                 *s;
    ngx_relay_reconnect_t              *rc, **prc;
    ngx_live_stream_t                  *live_stream;
    ngx_rtmp_relay_target_t             target;
    ngx_int_t                           pslot;

    rc = ev->data;
    live_stream = rc->live_stream;

    if (live_stream->publish_ctx == NULL) { /* all push closed */
        ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                "auto pull, push reconnect, all publishers closed");
        goto done;
    }

    s = live_stream->publish_ctx->session;
    pslot = ngx_stream_zone_insert_stream(&s->stream);
    if (pslot == NGX_ERROR) {
        goto done;
    }
    s->live_stream->pslot = pslot;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "auto push reconnect , stream %V not in current process, "
            "pslot:%i ngx_process_slot:%i",
            &s->name, pslot, ngx_process_slot);

    if (pslot == ngx_process_slot) { /* curr process occupied stream */
        goto done;
    }

    if (ngx_rtmp_auto_pull_target(s, &target, pslot) == NGX_ERROR) {
        goto done;
    }

    ngx_rtmp_relay_push(s, &s->name, &target);

done:
    for (prc = &live_stream->publish_reconnect; *prc; prc = &(*prc)->next) {
        if (*prc == rc) {
            *prc = rc->next;
            ngx_live_put_relay_reconnect(rc);
            break;
        }
    }
}


static void
ngx_rtmp_auto_pull_create_reconnect(ngx_rtmp_session_t *s,
        ngx_live_stream_t *st, unsigned publishing)
{
    ngx_relay_reconnect_t              *rc;
    ngx_rtmp_auto_pull_app_conf_t      *apcf;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    rc = ngx_live_get_relay_reconnect();
    rc->tag = &ngx_rtmp_auto_pull_module;
    rc->data = NULL;
    rc->live_stream = st;

    ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
            "auto pull, relay session closed, reconnect %p", rc);

    rc->reconnect.data = rc;
    rc->reconnect.log = ngx_cycle->log;
    if (publishing) { /* for pull, relay session is publishing */
        rc->reconnect.handler = ngx_rtmp_auto_pull_pull_reconnect;

        rc->next = st->play_reconnect;
        st->play_reconnect = rc;
        ngx_add_timer(&rc->reconnect, apcf->pull_reconnect);
    } else { /* for push, relay session is playing */
        rc->reconnect.handler = ngx_rtmp_auto_pull_push_reconnect;

        rc->next = st->publish_reconnect;
        st->publish_reconnect = rc;
        ngx_add_timer(&rc->reconnect, apcf->push_reconnect);
    }

    return;
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

    pslot = ngx_stream_zone_insert_stream(&s->stream);
    if (pslot == NGX_ERROR) {
        goto next;
    }
    s->live_stream->pslot = pslot;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "auto push, stream %s not in current process, "
            "pslot:%i ngx_process_slot:%i",
            v->name, pslot, ngx_process_slot);

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

    if (s->live_stream->pslot != -1) {
        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                "auto pull, stream %s already in current process", v->name);
        goto next;
    } else { /* first access for stream */
        pslot = ngx_stream_zone_insert_stream(&s->stream);
        if (pslot == NGX_ERROR) {
            goto next;
        }
        s->live_stream->pslot = pslot;
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                "auto pull, stream %s not in current process, "
                "pslot:%i ngx_process_slot:%i",
                v->name, pslot, ngx_process_slot);
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
ngx_rtmp_auto_pull_close_stream(ngx_rtmp_session_t *s,
        ngx_rtmp_close_stream_t *v)
{
    ngx_rtmp_relay_ctx_t               *ctx;
    ngx_live_stream_t                  *st;

    if (s->relay == 0 || s->closed) {
        goto next;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_relay_module);
    if (ctx == NULL) {
        goto next;
    }

    if (ctx->tag != &ngx_rtmp_auto_pull_module) {
        /* relay not create by rtmp auto pull module */
        goto next;
    }

    st = ctx->data;

    if (ctx->publishing) { /* relay pull session close */
        if (st->play_ctx != NULL) {
            st->relay_pull_tag = &ngx_rtmp_auto_pull_module;
            ngx_rtmp_auto_pull_create_reconnect(s, st, 1);
        }
    } else { /* relay push session close */
        if (st->publish_ctx != NULL) {
            ngx_rtmp_auto_pull_create_reconnect(s, st, 0);
        }
    }

next:
    return next_close_stream(s, v);
}


static ngx_int_t
ngx_rtmp_auto_pull_postconfiguration(ngx_conf_t *cf)
{
    /* chain handlers */

    next_publish = ngx_rtmp_publish;
    ngx_rtmp_publish = ngx_rtmp_auto_pull_publish;

    next_play = ngx_rtmp_play;
    ngx_rtmp_play = ngx_rtmp_auto_pull_play;

    next_close_stream = ngx_rtmp_close_stream;
    ngx_rtmp_close_stream = ngx_rtmp_auto_pull_close_stream;

    return NGX_OK;
}

