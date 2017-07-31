/*
 * Copyright (C) AlexWoo(Wu Jie) wj19840501@gmail.com
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_eval.h"
#include "ngx_rtmp_cmd_module.h"
#include "ngx_rtmp_netcall_module.h"


static ngx_rtmp_publish_pt              next_publish;
static ngx_rtmp_play_pt                 next_play;


static void *ngx_rtmp_auth_request_create_app_conf(ngx_conf_t *cf);
static char *ngx_rtmp_auth_request_merge_app_conf(ngx_conf_t *cf,
       void *parent, void *child);
static ngx_int_t ngx_rtmp_auth_request_postconfiguration(ngx_conf_t *cf);


ngx_str_t   ngx_rtmp_urlencoded =
            ngx_string("application/x-www-form-urlencoded");


typedef struct {
    ngx_str_t                           auth_uri;
} ngx_rtmp_auth_request_app_conf_t;

typedef struct {
    ngx_url_t                          *url;
    ngx_rtmp_play_t                    *play_v;
    ngx_rtmp_publish_t                 *publish_v;

    unsigned                            publishing;
} ngx_rtmp_auth_request_ctx_t;


static ngx_rtmp_eval_t *ngx_rtmp_auth_request_eval[] = {
    ngx_rtmp_eval_session,
    NULL
};


static ngx_command_t  ngx_rtmp_auth_request_commands[] = {

    { ngx_string("auth_uri"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_auth_request_app_conf_t, auth_uri),
      NULL },

      ngx_null_command
};


static ngx_rtmp_module_t  ngx_rtmp_auth_request_module_ctx = {
    NULL,                                       /* preconfiguration */
    ngx_rtmp_auth_request_postconfiguration,    /* postconfiguration */
    NULL,                                       /* create main configuration */
    NULL,                                       /* init main configuration */
    NULL,                                       /* create server configuration */
    NULL,                                       /* merge server configuration */
    ngx_rtmp_auth_request_create_app_conf,      /* create app configuration */
    ngx_rtmp_auth_request_merge_app_conf        /* merge app configuration */
};


ngx_module_t  ngx_rtmp_auth_request_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_auth_request_module_ctx,          /* module context */
    ngx_rtmp_auth_request_commands,             /* module directives */
    NGX_RTMP_MODULE,                            /* module type */
    NULL,                                       /* init master */
    NULL,                                       /* init module */
    NULL,                                       /* init process */
    NULL,                                       /* init thread */
    NULL,                                       /* exit thread */
    NULL,                                       /* exit process */
    NULL,                                       /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_rtmp_auth_request_create_app_conf(ngx_conf_t *cf)
{
    ngx_rtmp_auth_request_app_conf_t   *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_auth_request_app_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    return conf;
}

static char *
ngx_rtmp_auth_request_merge_app_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtmp_auth_request_app_conf_t   *prev = parent;
    ngx_rtmp_auth_request_app_conf_t   *conf = child;

    ngx_conf_merge_str_value(conf->auth_uri, prev->auth_uri, "");

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_rtmp_auth_request_http_parse_retcode(ngx_rtmp_session_t *s, ngx_chain_t *in)
{
    ngx_buf_t                          *b;
    ngx_int_t                           n;
    u_char                             *p;
    ngx_uint_t                          retcode = 0;

    /* find 10th character */

    n = sizeof("HTTP/1.1 ") - 1;
    while (in) {
        b = in->buf;
        if (b->last - b->pos > n) {
            p = b->pos + n; /* skip 'HTTP/1.1 ' */
            /* start parse retcode */
            while (*p >= (u_char)'0' && *p <= (u_char)'9' && p < b->last) {
                retcode = retcode * 10 + (int)(*p - '0');
                ++p;
            }

            if (retcode >= 100 && retcode < 600) {
                return retcode;
            }

            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                    "netcall: invalid HTTP retcode: %d", retcode);

            return NGX_ERROR;
        }
        n -= (b->last - b->pos);
        in = in->next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "netcall: empty or broken HTTP response");

    /*
     * not enough data;
     * it can happen in case of empty or broken reply
     */

    return NGX_ERROR;
}

static ngx_chain_t *
ngx_rtmp_auth_request_create(ngx_rtmp_session_t *s, void *arg,
        ngx_pool_t *pool)
{
    ngx_rtmp_auth_request_ctx_t        *ctx;
    ngx_url_t                          *u;
    ngx_buf_t                          *b;
    ngx_str_t                           uri;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_auth_request_module);

    u = ctx->url;

    b = ngx_create_temp_buf(pool, u->uri.len * 3);
    if (b == NULL) {
        return NULL;
    }

    b->last = (u_char *) ngx_escape_uri(b->last, u->uri.data, u->uri.len,
            NGX_ESCAPE_URI);
    uri.data = b->pos;
    uri.len = b->last - b->pos;

    return ngx_rtmp_netcall_http_format_request(NGX_RTMP_NETCALL_HTTP_GET,
            &u->host, &uri, NULL, NULL, pool, &ngx_rtmp_urlencoded);
}

static ngx_int_t
ngx_rtmp_auth_request_handle(ngx_rtmp_session_t *s, void *arg,
        ngx_chain_t *in)
{
    ngx_rtmp_auth_request_ctx_t        *ctx;
    ngx_int_t                           rc;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_auth_request_module);

    rc = ngx_rtmp_auth_request_http_parse_retcode(s, in);
    if (rc != NGX_HTTP_OK) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                "rtmp auth request, auth retcode %d, forbiden", rc);
        ngx_rtmp_finalize_session(s);
        return NGX_OK;
    }

    if (ctx->publishing) {
        return next_publish(s, ctx->publish_v);
    } else {
        return next_play(s, ctx->play_v);
    }
}

static ngx_int_t
ngx_rtmp_auth_request_send(ngx_rtmp_session_t *s)
{
    ngx_rtmp_auth_request_app_conf_t   *aacf;
    ngx_rtmp_auth_request_ctx_t        *ctx;
    ngx_rtmp_eval_t                   **eval;
    ngx_str_t                           url;
    ngx_int_t                           rc;
    ngx_url_t                          *u;
    ngx_rtmp_netcall_init_t             ci;

    aacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auth_request_module);
    if (ngx_strncasecmp(aacf->auth_uri.data, (u_char *) "http://",
            sizeof("http://") - 1))
    {
        return NGX_ERROR;
    }

    /* get URL */
    eval = ngx_rtmp_auth_request_eval;
    rc = ngx_rtmp_eval(s, &aacf->auth_uri, eval, &url, s->connection->log);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                "rtmp auth request, eval for uri failed");
        return NGX_ERROR;
    }

    /* parse URL */
    u = ngx_pcalloc(s->connection->pool, sizeof(ngx_url_t));
    u->url.len = url.len - 7; /* 7: sizeof("http://") - 1 */
    u->url.data = url.data + 7;
    u->default_port = 80;
    u->uri_part = 1;
    u->no_resolve = 1;

    if (ngx_parse_url(s->connection->pool, u) != NGX_OK) {
        if (u->err) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                    "rtmp auth request, %s in url \"%V\"", u->err, &u->url);
        }
        return NGX_ERROR;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_auth_request_module);
    ctx->url = u;

    /* create ci */
    ngx_memzero(&ci, sizeof(ci));
    ci.url = u;
    ci.create = ngx_rtmp_auth_request_create;
    ci.handle = ngx_rtmp_auth_request_handle;
    ci.arg = NULL;
    ci.argsize = 0;

    return ngx_rtmp_netcall_create(s, &ci);
}

static ngx_int_t
ngx_rtmp_auth_request_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v)
{
    ngx_rtmp_auth_request_app_conf_t   *aacf;
    ngx_rtmp_auth_request_ctx_t        *ctx;

    aacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auth_request_module);
    if (aacf->auth_uri.len == 0) {
        goto next;
    }

    if (s->relay || s->live_type != NGX_RTMP_LIVE) {
        goto next;
    }

    if (s->connection->sockaddr->sa_family == AF_UNIX) { /* inter processes */
        goto next;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_auth_request_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(s->connection->pool,
                sizeof(ngx_rtmp_auth_request_ctx_t));
        if (ctx == NULL) {
            goto next;
        }
        ngx_rtmp_set_ctx(s, ctx, ngx_rtmp_auth_request_module);
        ctx->publishing = 1;
    }

    ctx->publish_v = v;

    if (ngx_rtmp_auth_request_send(s) == NGX_ERROR) {
        goto next;
    }

    return NGX_OK;

next:
    return next_publish(s, v);
}

static ngx_int_t
ngx_rtmp_auth_request_play(ngx_rtmp_session_t *s, ngx_rtmp_play_t *v)
{
    ngx_rtmp_auth_request_app_conf_t   *aacf;
    ngx_rtmp_auth_request_ctx_t        *ctx;

    aacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auth_request_module);
    if (aacf->auth_uri.len == 0) {
        goto next;
    }

    if (s->relay || s->live_type != NGX_RTMP_LIVE) {
        goto next;
    }

    if (s->connection->sockaddr->sa_family == AF_UNIX) { /* inter processes */
        goto next;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_auth_request_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(s->connection->pool,
                sizeof(ngx_rtmp_auth_request_ctx_t));
        if (ctx == NULL) {
            goto next;
        }
        ngx_rtmp_set_ctx(s, ctx, ngx_rtmp_auth_request_module);
    }

    ctx->play_v = v;

    if (ngx_rtmp_auth_request_send(s) == NGX_ERROR) {
        goto next;
    }

    return NGX_OK;

next:
    return next_play(s, v);
}

static ngx_int_t
ngx_rtmp_auth_request_postconfiguration(ngx_conf_t *cf)
{
    /* chain handlers */

    next_publish = ngx_rtmp_publish;
    ngx_rtmp_publish = ngx_rtmp_auth_request_publish;

    next_play = ngx_rtmp_play;
    ngx_rtmp_play = ngx_rtmp_auth_request_play;

    return NGX_OK;
}
