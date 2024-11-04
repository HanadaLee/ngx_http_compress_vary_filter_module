# ngx_http_compress_vary_filter_module

# Name
ngx_http_compress_vary_filter_module is a header filter module used instead of the 'gzip_vary' directive.

# Table of Content

* [Name](#name)
* [Status](#status)
* [Synopsis](#synopsis)
* [Installation](#installation)
* [Directives](#directives)
  * [compress_vary](#compress_vary)
* [Author](#author)
* [License](#license)

# Status

This Nginx module is currently considered experimental. Issues and PRs are welcome if you encounter any problems.

# Synopsis

```nginx
server {
    listen 127.0.0.1:8080;
    server_name localhost;

    location / {
        gzip on;
        compress_vary on;

        proxy_pass http://foo.com;
    }
}
```

# Installation

To use theses modules, configure your nginx branch with `--add-module=/path/to/ngx_http_compress_vary_filter_module`.

# Directives

## compress_vary

**Syntax:** *compress_vary on | off;*

**Default:** *compress_vary off;*

**Context:** *http, server, location*

Enables or disables inserting the “Vary: Accept-Encoding” response header field if the directives gzip, gzip_static, or gunzip are active.

Unlike gzip_vary, if a Vary header exists for the original response, it will append the Accept-Encoding to the original Vary header. In addition, multiple Vary headers will be merged into one and separated by commas. Duplicate header values ​​in Vary will be removed.

# Author

Hanada im@hanada.info

This module is based on [ngx_http_gunzip_module](https://nginx.org/en/docs/http/ngx_http_gunzip_module.html), one of nginx core modules and [ngx_unbrotli](https://github.com/clyfish/ngx_unbrotli), a nginx module for brotli decompression.

# License

This Nginx module is licensed under [BSD 2-Clause License](LICENSE).
