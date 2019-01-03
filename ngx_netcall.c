/*
 * Copyright (C) AlexWoo(Wu Jie) wj19840501@gmail.com
 */


#include "ngx_netcall.h"


static void
ngx_netcall_timeout(ngx_event_t *ev)
{
    ngx_netcall_ctx_t          *nctx;
    ngx_http_request_t         *hcr;

    nctx = ev->data;
    hcr = nctx->hcr;

    if (nctx->handler) {
        nctx->handler(nctx, NGX_ERROR);
        nctx->hcr = NULL;
    }

    // connection error, close http client request and close connection
    ngx_http_client_finalize_request(hcr, 1);
}

static void
ngx_netcall_handler(void *data, ngx_http_request_t *hcr)
{
    ngx_netcall_ctx_t          *nctx;
    ngx_int_t                   code;

    nctx = data;
    if (nctx->ev.timer_set) {
        ngx_del_timer(&nctx->ev);
    }

    code = ngx_http_client_status_code(hcr);

    if (nctx->handler) {
        nctx->handler(nctx, code);
        nctx->hcr = NULL;
    }

    // only close http client request, keep connection alive
    ngx_http_client_finalize_request(hcr, 0);
}

static void
ngx_netcall_destroy_handler(ngx_event_t *ev)
{
    ngx_netcall_ctx_t          *nctx;

    nctx = ev->data;

    ngx_destroy_pool(nctx->pool);
}

ngx_netcall_ctx_t *
ngx_netcall_create_ctx(ngx_uint_t type, ngx_str_t *groupid, ngx_uint_t stage,
    ngx_msec_t timeout, ngx_int_t retries, ngx_msec_t update, ngx_uint_t idx)
{
    ngx_netcall_ctx_t          *ctx;
    ngx_pool_t                 *pool;

    pool = ngx_create_pool(4096, ngx_cycle->log);
    if (pool == NULL) {
        return NULL;
    }

    ctx = ngx_pcalloc(pool, sizeof(ngx_netcall_ctx_t));
    if (ctx == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    ctx->pool = pool;

    ctx->idx = idx;
    ctx->type = type;

    ctx->groupid.len = groupid->len;
    ctx->groupid.data = ngx_pcalloc(pool, ctx->groupid.len);
    if (ctx->groupid.data == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    ngx_memcpy(ctx->groupid.data, groupid->data, groupid->len);

    ctx->ev.log = ngx_cycle->log;
    ctx->ev.data = ctx;

    ctx->stage = stage;
    ctx->timeout = timeout;
    ctx->update = update;

    return ctx;
}

void
ngx_netcall_create(ngx_netcall_ctx_t *nctx, ngx_log_t *log)
{
    ngx_http_request_t         *hcr;

    hcr = ngx_http_client_get(log, &nctx->url, NULL, nctx);
    if (hcr == NULL) {
        return;
    }

    ngx_http_client_set_read_handler(hcr, ngx_netcall_handler);

    // detach old http client request
    if (nctx->hcr) {
        ngx_http_client_detach(nctx->hcr);
    }

    nctx->hcr = hcr;

    nctx->ev.log = log;
    nctx->ev.handler = ngx_netcall_timeout;
    ngx_add_timer(&nctx->ev, nctx->timeout);
}

void
ngx_netcall_destroy(ngx_netcall_ctx_t *nctx)
{
    ngx_http_request_t         *hcr;

    if (nctx->ev.timer_set) {
        ngx_del_timer(&nctx->ev);
    }

    hcr = nctx->hcr;
    if (hcr) { // use detach will keep client connection alive
        ngx_http_client_detach(hcr);
    }

    // destroy may called in nctx->handler
    // destroy pool may cause memory error
    // so we destroy nctx pool asynchronous
    nctx->ev.handler = ngx_netcall_destroy_handler;
    ngx_post_event(&nctx->ev, &ngx_posted_events);
}

ngx_str_t *
ngx_netcall_header(ngx_netcall_ctx_t *nctx, ngx_str_t *key)
{
    ngx_http_request_t         *hcr;

    hcr = nctx->hcr;

    return ngx_http_client_header_in(hcr, key);
}
