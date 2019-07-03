# NGINX-based Media Streaming Server
## nginx-rtmp-module


### Project blog

  http://nginx-rtmp.blogspot.com

  http://pingos.me

### Wiki manual

  [auto pull module](./doc/ngx_rtmp_oclp_module.chs.md)

### Google group

  https://groups.google.com/group/nginx-rtmp

  https://groups.google.com/group/nginx-rtmp-ru (Russian)

### Donation page (Paypal etc)

  http://arut.github.com/nginx-rtmp-module/

### Build

```shell
$ git clone https://github.com/im-pingo/nginx-client-module.git
$ git clone https://github.com/im-pingo/nginx-multiport-module.git
$ git clone https://github.com/im-pingo/nginx-toolkit-module.git
$ git clone https://github.com/im-pingo/nginx-rtmp-module.git
$ git clone https://github.com/nginx/nginx.git
$
$ cd nginx
$
$ ./auto/configure --add-module=../nginx-client-module \
    --add-module=../nginx-multiport-module             \
    --add-module=../nginx-toolkit-module               \
    --add-module=../nginx-rtmp-module
$
$ sudo make && sudo make install
```
### Publish & Play

#### rtmp publish

rtmp://localhost/live/xx

#### play

* rtmp => rtmp://localhost/live/xx

* http-flv => http://localhost/flv/xx

* http-ts => http://localhost/ts/xx

* hls => http://localhost/hls/xx.m3u8


### Features

* RTMP/HLS/MPEG-DASH/HTTP-FLV/HTTP-TS live streaming

* RTMP Video on demand FLV/MP4,
  playing from local filesystem or HTTP

* Stream relay support for distributed
  streaming: push & pull models

* Recording streams in multiple FLVs

* H264/H265/AAC/MP3 support

* Online transcoding with FFmpeg

* HTTP callbacks (publish/play/record/update etc)

* Running external programs on certain events (exec)

* HTTP control module for recording audio/video and dropping clients

* Advanced buffering techniques
  to keep memory allocations at a minimum
  level for faster streaming and low
  memory footprint

* Proved to work with Wirecast, FMS, Wowza,
  JWPlayer, FlowPlayer, StrobeMediaPlayback,
  ffmpeg, avconv, rtmpdump, flvstreamer
  and many more

* Statistics in XML/XSL in machine- & human-
  readable form

* Linux/FreeBSD/MacOS/Windows

### Windows limitations

Windows support is limited. These features are not supported

* execs
* static pulls
* auto_push

### RTMP URL format

    rtmp://rtmp.example.com/app[/name]

app -  should match one of application {}
         blocks in config

name - interpreted by each application
         can be empty


### Multi-worker live streaming

Module supports multi-worker live
streaming through automatic stream pushing
to nginx workers. This option is toggled with
rtmp_auto_push directive.


### Example nginx.conf

```nginx
    user  root;
    daemon on;
    master_process on;
    worker_processes  1;
    #worker_rlimit 4g;
    #working_directory /usr/local/openresty/nginx/logs;

    #error_log  logs/error.log;
    #error_log  logs/error.log  notice;
    error_log  logs/error.log  info;

    worker_rlimit_nofile 102400;
    worker_rlimit_core   2G;
    working_directory    /tmp;

    #pid        logs/nginx.pid;

    events {
        worker_connections  1024;
    }
    stream_zone buckets=1024 streams=4096;

    rtmp {
        server {
            listen 1935;

            # 允许缓存的最大音视频帧数
            # 该值将直接影响到roll_back和cache_time，
            # 如果希望roll_back或cache_time可以设置非常大，那么该值也应该设置成较大的值才行
            out_queue 204800;

            application live {
#               pull rtmp://127.0.0.1:1936/live app=live;
#               oclp_pull http://127.0.0.1/oclp;

                # 允许最长回看时间:
                # 如 rtmp://xxx/xxx/xxx?roll_back=30000 和 http://xxx/xxx/xxx?roll_back=30000
                # 表示从30000ms前开始播放
                roll_back 3m;

                send_all off;

                # 下发数据时，时间戳是否从零开始
                zero_start off;

                live on;
                hls on;
                hls_path /tmp/hls;
                wait_key on;
                wait_video on;

                # 缓存时长，和roll_back搭配使用时，取最大值
                cache_time 1s;

                # 低延时模式，发现最新关键帧则跳至帧开始下发，roll_back > 0 时该配置失效
                low_latency on;

                # 首次下发数据时长，如果希望追求低延时，牺牲秒开效果，可以将该值设置成 0s
                # 默认与cache_time相等
                one_off_send 2s;

                # 能忍受的最大时间戳差值，超过这个值则自动矫正(只有在 cache_time > 0 时生效)
                fix_timestamp 0ms;
               # h265 codecid, default 12
                hevc_codecid  12;
            }
        }
}

    # HTTP can be used for accessing RTMP stats
    http {

        server {

            listen      8080;

            # This URL provides RTMP statistics in XML
            location /stat {
                rtmp_stat all;

                # Use this stylesheet to view XML as web page
                # in browser
                rtmp_stat_stylesheet stat.xsl;
            }

            location /oclp {
                return 302 http://127.0.0.1:8080/live/1;
            }

            location /flv {
                flv_live 1935 app=live;
            }

            location /ts {
                ts_live 1935 app=live;
            }

            location /stat.xsl {
                # XML stylesheet to view RTMP stats.
                # Copy stat.xsl wherever you want
                # and put the full directory path here
                root /path/to/stat.xsl/;
            }

            location /hls {
                # Serve HLS fragments
                types {
                    application/vnd.apple.mpegurl m3u8;
                    video/mp2t ts;
                }
                root /tmp;
                add_header Cache-Control no-cache;
            }

            location /dash {
                # Serve DASH fragments
                root /tmp;
                add_header Cache-Control no-cache;
            }
        }
    }

```
