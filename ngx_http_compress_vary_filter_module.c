
/*
 * Copyright (c) Hanada
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_flag_t  compress_vary;
} ngx_http_compress_vary_filter_conf_t;


static ngx_int_t ngx_http_compress_vary_header_filter(ngx_http_request_t *r);
ngx_http_compress_vary_filter_create_conf(ngx_conf_t *cf);
ngx_http_compress_vary_filter_merge_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_compress_vary_filter_init(ngx_conf_t *cf);


static ngx_command_t  ngx_http_compress_vary_filter_commands[] = {

    { ngx_string("compress_vary"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_compress_vary_filter_conf_t, compress_vary),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_compress_vary_filter_module_ctx = {
    NULL,                                       /* preconfiguration */
    ngx_http_compress_vary_filter_init,         /* postconfiguration */

    NULL,                                       /* create main configuration */
    NULL,                                       /* init main configuration */

    NULL,                                       /* create server configuration */
    NULL,                                       /* merge server configuration */

    ngx_http_compress_vary_filter_create_conf,  /* create location configuration */
    ngx_http_compress_vary_filter_merge_conf    /* merge location configuration */
};


ngx_module_t  ngx_http_compress_vary_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_compress_vary_filter_module_ctx,  /* module context */
    ngx_http_compress_vary_filter_commands,     /* module directives */
    NGX_HTTP_MODULE,                            /* module type */
    NULL,                                       /* init master */
    NULL,                                       /* init module */
    NULL,                                       /* init process */
    NULL,                                       /* init thread */
    NULL,                                       /* exit thread */
    NULL,                                       /* exit process */
    NULL,                                       /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;


static ngx_int_t
ngx_http_compress_vary_header_filter(ngx_http_request_t *r)
{
    ngx_list_part_t    *part;
    ngx_table_elt_t    *header;
    ngx_uint_t          i, j;
    ngx_array_t         unique_values;
    ngx_str_t          *value;
    ngx_str_t           accept_encoding = ngx_string("Accept-Encoding");
    ngx_uint_t          has_accept_encoding = 0;

    ngx_http_compress_vary_filter_conf_t  *conf;

    conf = ngx_http_get_module_loc_conf(r,
        ngx_http_compress_vary_filter_module);

    if (!conf->compress_vary) {
        return ngx_http_next_header_filter(r);
    }

    if (ngx_array_init(&unique_values, r->pool, 4,
        sizeof(ngx_str_t)) != NGX_OK)
    {
        return NGX_ERROR;
    }

    part = &r->headers_out.headers.part;
    header = part->elts;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            header = part->elts;
            i = 0;
        }

        if (header[i].hash == 0) {
            continue;
        }

        if (header[i].key.len == 4 &&
            ngx_strncasecmp(header[i].key.data, (u_char *)"Vary", 4) == 0) {

            u_char *p = header[i].value.data;
            u_char *last = p + header[i].value.len;

            while (p < last) {
                while (p < last && (*p == ' ' || *p == '\t' || *p == ',')) {
                    p++;
                }

                u_char *start = p;

                while (p < last && *p != ',' && *p != ' ' && *p != '\t') {
                    p++;
                }

                if (start != p) {
                    ngx_str_t token;
                    token.data = start;
                    token.len = p - start;

                    ngx_uint_t found = 0;
                    value = unique_values.elts;

                    for (j = 0; j < unique_values.nelts; j++) {
                        if (token.len == value[j].len &&
                            ngx_strncasecmp(token.data, value[j].data, token.len) == 0)
                        {
                            found = 1;
                            break;
                        }
                    }

                    if (!found) {
                        ngx_str_t *v = ngx_array_push(&unique_values);
                        if (v == NULL) {
                            return NGX_ERROR;
                        }

                        v->data = token.data;
                        v->len = token.len;

                        if (token.len == accept_encoding.len &&
                            ngx_strncasecmp(token.data, accept_encoding.data, token.len) == 0)
                        {
                            has_accept_encoding = 1;
                        }
                    }
                }
            }

            header[i].hash = 0;
            ngx_str_null(&header[i].key);
            ngx_str_null(&header[i].value);
        }
    }

    if (r->gzip_vary && !has_accept_encoding) {
        ngx_str_t *v = ngx_array_push(&unique_values);
        if (v == NULL) {
            return NGX_ERROR;
        }

        v->data = accept_encoding.data;
        v->len = accept_encoding.len;
    }

    ngx_uint_t n = unique_values.nelts;
    value = unique_values.elts;
    size_t total_len = 0;

    for (i = 0; i < n; i++) {
        total_len += value[i].len;
        if (i != n - 1) {
            total_len += 2;  /* ', ' */
        }
    }

    if (total_len == 0) {
        return ngx_http_next_header_filter(r);
    }

    u_char *data = ngx_pnalloc(r->pool, total_len);
    if (data == NULL) {
        return NGX_ERROR;
    }

    u_char *p = data;

    for (i = 0; i < n; i++) {
        p = ngx_cpymem(p, value[i].data, value[i].len);
        if (i != n - 1) {
            *p++ = ',';
            *p++ = ' ';
        }
    }

    ngx_table_elt_t *h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    h->hash = 1;
    ngx_str_set(&h->key, "Vary");
    h->value.len = total_len;
    h->value.data = data;

    return ngx_http_next_header_filter(r);
}


static void *
ngx_http_compress_vary_filter_create_conf(ngx_conf_t *cf)
{
    ngx_http_compress_vary_filter_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool,
        sizeof(ngx_http_compress_vary_filter_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->compress_vary = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_compress_vary_filter_merge_conf(ngx_conf_t *cf,
    void *parent, void *child)
{
    ngx_http_compress_vary_filter_conf_t *prev = parent;
    ngx_http_compress_vary_filter_conf_t *conf = child;

    ngx_conf_merge_value(conf->compress_vary, prev->compress_vary, 0);

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_compress_vary_filter_init(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_compress_vary_header_filter;

    return NGX_OK;
}
