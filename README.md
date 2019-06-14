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

### Features

* RTMP/HLS/MPEG-DASH/HTTP-FLV live streaming

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
            application live {
#                pull rtmp://127.0.0.1:1936/live app=live;
#               oclp_pull http://127.0.0.1/oclp;
                live on;
                cache_time 3s;
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

            location /live-flv {
                flv_live 1935 app=live;
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
