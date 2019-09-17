
/*
 * Copyright (C) Pingo (cczjp89@gmail.com)
 */

#ifndef _NGX_HLS_LIVE_MODULE_H_INCLUDE_
#define _NGX_HLS_LIVE_MODULE_H_INCLUDE_

typedef struct ngx_hls_live_frag_s ngx_hls_live_frag_t;
typedef struct ngx_hls_live_play_s ngx_hls_live_play_t;
typedef struct ngx_hls_live_ctx_s  ngx_hls_live_ctx_t;

struct ngx_hls_live_play_s {
    ngx_str_t               name;
    /* connection parameters */
    ngx_rtmp_addr_conf_t   *addr_conf;
    ngx_str_t               app;
    ngx_str_t               stream;
    ngx_str_t               args;
    ngx_str_t               flashver;
    ngx_str_t               swf_url;
    ngx_str_t               tc_url;
    uint32_t                acodecs;
    uint32_t                vcodecs;
    ngx_str_t               page_url;
    ngx_str_t               domain;
    ngx_str_t               serverid;
    ngx_log_t              *log;
};

struct ngx_hls_live_frag_s {
    ngx_uint_t              ref;
    ngx_hls_live_frag_t    *next;
    uint64_t                id;
    uint64_t                key_id;
    double                  duration;
    unsigned                active:1;
    unsigned                discont:1; /* before */
    ngx_uint_t              length;
    ngx_chain_t            *out;
    ngx_uint_t              content_last;
    ngx_uint_t              content_pos;
    ngx_mpegts_frame_t     *content[0];
};

struct ngx_hls_live_ctx_s {
    unsigned                opened:1;
    unsigned                playing:1;

    ngx_buf_t              *patpmt;

    ngx_str_t               sid;
    ngx_str_t               stream;
    ngx_str_t               name;

    uint64_t                nfrag;
    uint64_t                frag_ts;
    uint64_t                key_id;
    ngx_uint_t              nfrags;
    ngx_hls_live_frag_t   **frags; /* circular 2 * winfrags + 1 */
    ngx_hls_live_frag_t    *frag;

    ngx_uint_t              audio_cc;
    ngx_uint_t              video_cc;
    ngx_uint_t              key_frags;

    uint64_t                aframe_base;
    uint64_t                aframe_num;

    ngx_buf_t              *aframe;
    uint64_t                aframe_pts;
    ngx_event_t             ev;
    ngx_msec_t              timeout;
    ngx_msec_t              last_time;
};

ngx_int_t ngx_hls_live_write_playlist(ngx_rtmp_session_t *s, ngx_buf_t *out);
ngx_hls_live_frag_t* ngx_hls_live_find_frag(ngx_rtmp_session_t *s,
    ngx_str_t *name);
ngx_chain_t* ngx_hls_live_prepare_frag(ngx_rtmp_session_t *s,
    ngx_hls_live_frag_t *frag);
void ngx_hls_live_free_frag(ngx_rtmp_session_t *s, ngx_hls_live_frag_t *frag);
ngx_rtmp_session_t* ngx_hls_live_fetch_session(ngx_str_t *server,
    ngx_str_t *stream, ngx_str_t *session);

#endif
