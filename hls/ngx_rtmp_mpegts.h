
/*
 * Copyright (C) Roman Arutyunyan
 */


#ifndef _NGX_RTMP_MPEGTS_H_INCLUDED_
#define _NGX_RTMP_MPEGTS_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <openssl/aes.h>


extern u_char ngx_rtmp_mpegts_pat[];

typedef struct ngx_rtmp_mpegts_file_s  ngx_rtmp_mpegts_file_t;

typedef ssize_t (*ngx_rtmp_mpegts_write_pt) (ngx_rtmp_mpegts_file_t *file,
        u_char *in, size_t in_size);


struct ngx_rtmp_mpegts_file_s {
    ngx_fd_t    fd;
    ngx_log_t  *log;
    off_t       file_size;
    unsigned    encrypt:1;
    unsigned    size:4;
    u_char      buf[16];
    u_char      iv[16];
    AES_KEY     key;
    ngx_int_t   acodec;
    ngx_int_t   vcodec;
    ngx_buf_t   wbuf;
    ngx_rtmp_mpegts_write_pt whandle;
};

ngx_int_t ngx_rtmp_mpegts_init_encryption(ngx_rtmp_mpegts_file_t *file,
    u_char *key, size_t key_len, uint64_t iv);
ngx_int_t ngx_rtmp_mpegts_open_file(ngx_rtmp_mpegts_file_t *file, u_char *path,
    ngx_log_t *log);
ngx_int_t ngx_rtmp_mpegts_close_file(ngx_rtmp_mpegts_file_t *file);
ngx_int_t ngx_rtmp_mpegts_write_header(ngx_rtmp_mpegts_file_t *file);
ngx_int_t ngx_rtmp_mpegts_write_frame(ngx_rtmp_mpegts_file_t *file,
    ngx_mpegts_frame_t *f, ngx_buf_t *b, ngx_buf_t **out);

ngx_int_t ngx_rtmp_mpegts_gen_pmt(ngx_int_t vcodec,
    ngx_int_t acodec, ngx_log_t *log, u_char *pmt);



#endif /* _NGX_RTMP_MPEGTS_H_INCLUDED_ */
