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
static ngx_rtmp_close_stream_pt         next_close_stream;


static void *ngx_rtmp_auto_pull_create_app_conf(ngx_conf_t *cf);
static char *ngx_rtmp_auto_pull_merge_app_conf(ngx_conf_t *cf,
       void *parent, void *child);
static ngx_int_t ngx_rtmp_auto_pull_postconfiguration(ngx_conf_t *cf);


typedef struct ngx_rtmp_auto_pull_hash_node_s ngx_rtmp_auto_pull_hash_node_t;

typedef struct {
    ngx_flag_t                          closed;
} ngx_rtmp_auto_pull_ctx_t;

struct ngx_rtmp_auto_pull_hash_node_s {
    u_char                              name[NGX_RTMP_MAX_NAME];
    ngx_rtmp_auto_pull_hash_node_t     *next;
    ngx_uint_t                          count;
    ngx_int_t                           pslot;
};

typedef struct {
    ngx_flag_t                          auto_pull;
    ngx_str_t                           auto_pull_port;

    ngx_uint_t                          nbuckets;
    ngx_rtmp_auto_pull_hash_node_t    **hash;

    ngx_pool_t                         *pool;
    ngx_rtmp_auto_pull_hash_node_t     *free;

#if (NGX_DEBUG)
    ngx_uint_t                          free_count;
#endif
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
    conf->nbuckets = 10007;

    return conf;
}

static char *
ngx_rtmp_auto_pull_merge_app_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtmp_auto_pull_app_conf_t      *prev = parent;
    ngx_rtmp_auto_pull_app_conf_t      *conf = child;

    ngx_conf_merge_value(conf->auto_pull, prev->auto_pull, 0);
    ngx_conf_merge_str_value(conf->auto_pull_port, prev->auto_pull_port,
                             "unix:/tmp/rtmp_auto_pull.sock");

    conf->pool = ngx_create_pool(4096, &cf->cycle->new_log);
    if (conf->pool == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->hash = ngx_pcalloc(cf->pool,
            sizeof(ngx_rtmp_auto_pull_hash_node_t *) * conf->nbuckets);

    return NGX_CONF_OK;
}


static void
ngx_rtmp_auto_pull_print(ngx_rtmp_session_t *s)
{
#if (NGX_DEBUG)
    ngx_rtmp_auto_pull_app_conf_t      *apcf;
    ngx_rtmp_auto_pull_hash_node_t     *node;
    ngx_uint_t                          i;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "auto pull debug, "
            "auto_pull_port: %V, nbuckets: %ui, free_count: %ui",
            &apcf->auto_pull_port, apcf->nbuckets, apcf->free_count);
    for (i = 0; i < apcf->nbuckets; ++i) {
        node = apcf->hash[i];
        if (node == NULL) {
            continue;
        }

        for (; node; node = node->next) {
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                    "    %p   name: %s, count: %ui, pslot: %i",
                    node, node->name, node->count, node->pslot);
        }
    }
#endif
}

static ngx_rtmp_auto_pull_hash_node_t *
ngx_rtmp_auto_pull_get_hash_node(ngx_rtmp_session_t *s, ngx_str_t *name)
{
    ngx_rtmp_auto_pull_app_conf_t      *apcf;
    ngx_rtmp_auto_pull_hash_node_t     *node;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    node = apcf->free;
    if (node == NULL) {
        node = ngx_pcalloc(apcf->pool, sizeof(ngx_rtmp_auto_pull_hash_node_t));
    } else {
        apcf->free = node->next;
#if (NGX_DEBUG)
        --apcf->free_count;
#endif
    }

    (void) ngx_cpystrn(node->name, name->data, name->len + 1);
    node->count = 0;
    node->next = NULL;

    return node;
}

static void
ngx_rtmp_auto_pull_put_hash_node(ngx_rtmp_session_t *s,
        ngx_rtmp_auto_pull_hash_node_t *node)
{
    ngx_rtmp_auto_pull_app_conf_t      *apcf;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    node->next = apcf->free;
    apcf->free = node;
#if (NGX_DEBUG)
    ++apcf->free_count;
#endif
}

static ngx_rtmp_auto_pull_hash_node_t **
ngx_rtmp_auto_pull_find_node(ngx_rtmp_session_t *s, ngx_str_t *name)
{
    ngx_rtmp_auto_pull_app_conf_t      *apcf;
    ngx_rtmp_auto_pull_hash_node_t    **pnode;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    pnode = &apcf->hash[ngx_hash_key(name->data, name->len) % apcf->nbuckets];
    for (; *pnode; pnode = &(*pnode)->next) {
        if (ngx_strlen((*pnode)->name) == name->len &&
                ngx_memcmp((*pnode)->name, name->data, name->len) == 0) {
            break;
        }
    }

    return pnode;
}


static ngx_int_t
ngx_rtmp_auto_pull_join(ngx_rtmp_session_t *s, ngx_str_t *name)
{
    ngx_rtmp_auto_pull_hash_node_t    **pnode;
    ngx_rtmp_auto_pull_ctx_t           *ctx;

    ctx = ngx_pcalloc(s->connection->pool, sizeof(ngx_rtmp_auto_pull_ctx_t));
    if (ctx == NULL) {
        return NGX_ERROR;
    }
    ngx_rtmp_set_ctx(s, ctx, ngx_rtmp_auto_pull_module);

    pnode = ngx_rtmp_auto_pull_find_node(s, name);

    if (*pnode) { /* stream in current worker, link in current session */
        ++(*pnode)->count;
        return NGX_AGAIN;
    }

    *pnode = ngx_rtmp_auto_pull_get_hash_node(s, name);
    ++(*pnode)->count;

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_auto_pull_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v)
{
    ngx_rtmp_auto_pull_app_conf_t      *apcf;
    ngx_rtmp_auto_pull_hash_node_t    **pnode;
    ngx_int_t                           rc;
    ngx_int_t                           pslot;
    ngx_str_t                           name;
    u_char                             *p;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    if (!apcf->auto_pull || s->relay) {
        goto next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "auto pull, session %p publish %s", s, v->name);

    rc = ngx_rtmp_auto_pull_join(s, &s->name);
    switch (rc) {
    case NGX_AGAIN:
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0, "auto pull, "
                "stream %V has already publish in current process", &s->name);
        return NGX_ERROR;
    case NGX_OK:
        break;
    default:
        return NGX_ERROR;
    }

    name.len = s->app.len + 1 + s->name.len; /* app/name */
    name.data = ngx_pcalloc(s->connection->pool, name.len);
    if (name.data == NULL) {
        return NGX_ERROR;
    }
    p = name.data;
    p = ngx_copy(p, s->app.data, s->app.len);
    *p++ = '/';
    p = ngx_copy(p, s->name.data, s->name.len);

    pslot = ngx_stream_zone_insert_stream(&name);
    if (pslot == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (pslot != ngx_process_slot) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0, "auto pull, "
                "stream %V has already publish in sibling process", &s->name);
        return NGX_ERROR;
    }

    pnode = ngx_rtmp_auto_pull_find_node(s, &s->name);
    (*pnode)->pslot = pslot;

next:
    ngx_rtmp_auto_pull_print(s);
    return next_publish(s, v);
}


static ngx_int_t
ngx_rtmp_auto_pull_play(ngx_rtmp_session_t *s, ngx_rtmp_play_t *v)
{
    ngx_rtmp_auto_pull_app_conf_t      *apcf;
    ngx_rtmp_auto_pull_hash_node_t    **pnode;
    ngx_int_t                           rc;
    ngx_int_t                           pslot;
    ngx_str_t                           name;
    u_char                             *p;
    ngx_rtmp_relay_target_t             target;
    ngx_url_t                          *u;
    socklen_t                           len;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    if (!apcf->auto_pull || s->relay) {
        goto next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "auto pull, session %p play %s", s, v->name);

    rc = ngx_rtmp_auto_pull_join(s, &s->name);
    switch (rc) {
    case NGX_AGAIN:
        goto next;
    case NGX_OK:
        break;
    default:
        return NGX_ERROR;
    }

    name.len = s->app.len + 1 + s->name.len; /* app/name */
    name.data = ngx_pcalloc(s->connection->pool, name.len);
    if (name.data == NULL) {
        return NGX_ERROR;
    }
    p = name.data;
    p = ngx_copy(p, s->app.data, s->app.len);
    *p++ = '/';
    p = ngx_copy(p, s->name.data, s->name.len);

    pslot = ngx_stream_zone_insert_stream(&name);
    if (pslot == NGX_ERROR) {
        return NGX_ERROR;
    }

    pnode = ngx_rtmp_auto_pull_find_node(s, &s->name);
    (*pnode)->pslot = pslot;

    if (pslot == ngx_process_slot) {
        goto next;
    }

    ngx_memzero(&target, sizeof(target));

    u = &target.url;
    u->url = s->name;
    target.name = s->name;
    target.app = s->app;
    target.tc_url = s->tc_url;
    target.page_url = s->page_url;
    target.swf_url = s->swf_url;
    target.flash_ver = s->flashver;

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

    s->auto_pulled = 1;
    ngx_rtmp_relay_pull(s, &s->name, &target);

next:
    ngx_rtmp_auto_pull_print(s);
    return next_play(s, v);
}


static ngx_int_t
ngx_rtmp_auto_pull_close_stream(ngx_rtmp_session_t *s,
        ngx_rtmp_close_stream_t *v)
{
    ngx_rtmp_auto_pull_app_conf_t      *apcf;
    ngx_rtmp_auto_pull_hash_node_t     *node, **pnode;
    ngx_rtmp_auto_pull_ctx_t           *ctx;
    ngx_str_t                           name;
    ngx_int_t                           pslot;
    u_char                             *p;

    apcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_auto_pull_module);

    if (!apcf->auto_pull || s->relay) {
        goto next;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_auto_pull_module);
    if (ctx == NULL || ctx->closed) {
        goto next;
    }

    ctx->closed = 1;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "auto pull, session %p close stream %V", s, &s->name);

    pnode = ngx_rtmp_auto_pull_find_node(s, &s->name);
    if (*pnode == NULL) {
        goto next;
    }

    if (--(*pnode)->count != 0) {
        goto next;
    }

    node = *pnode;
    *pnode = node->next;
    pslot = node->pslot;
    ngx_rtmp_auto_pull_put_hash_node(s, node);

    /* all publisher and players close */
    name.len = s->app.len + 1 + s->name.len; /* app/name */
    name.data = ngx_pcalloc(s->connection->pool, name.len);
    if (name.data == NULL) {
        ngx_log_error(NGX_LOG_EMERG, s->connection->log, 0, "auto pull, "
                "alloc for name \"%V\" failed", &s->name);
        return NGX_ERROR;
    }
    p = name.data;
    p = ngx_copy(p, s->app.data, s->app.len);
    *p++ = '/';
    p = ngx_copy(p, s->name.data, s->name.len);

    if (pslot == ngx_process_slot) {
        ngx_stream_zone_delete_stream(&name);
    }

next:
    ngx_rtmp_auto_pull_print(s);
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

