#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    ngx_flag_t  compress_vary;
} ngx_http_compress_vary_filter_conf_t;

static ngx_int_t ngx_http_compress_vary_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_compress_vary_filter_init(ngx_conf_t *cf);

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
    ngx_http_compress_vary_filter_conf_t  *conf;

    conf = ngx_http_get_module_loc_conf(r,
        ngx_http_compress_vary_filter_module);

    if (!conf->compress_vary) {
        return ngx_http_next_header_filter(r);
    }

    ngx_list_part_t            *part;
    ngx_table_elt_t            *header;
    ngx_uint_t                  i, j;
    ngx_array_t                 values;
    ngx_array_t                 unique_values;
    ngx_str_t                  *value;
    ngx_uint_t                  n;
    ngx_str_t                   token;
    ngx_uint_t                  found;

    if (ngx_array_init(&values,
        r->pool, 4, sizeof(ngx_str_t)) != NGX_OK) {
        return NGX_ERROR;
    }

    /* 收集所有的Vary头部值并移除现有的Vary头 */
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

        if (header[i].key.len == 4
            && ngx_strcasecmp(header[i].key.data, (u_char *)"Vary") == 0)
        {

            /* 处理header[i].value */
            u_char *p = header[i].value.data;
            u_char *last = p + header[i].value.len;

            while (p < last) {
                /* 跳过前导空白和逗号 */
                while (p < last && (*p == ' ' || *p == '\t' || *p == ',')) {
                    p++;
                }

                u_char *start = p;

                /* 找到token的结束位置 */
                while (p < last && *p != ',' && *p != ' ' && *p != '\t') {
                    p++;
                }

                if (start != p) {
                    /* 得到一个token */
                    token.data = start;
                    token.len = p - start;
                    found = 0;
                    value = unique_values.elts;
                    ngx_str_t *v = ngx_array_push(&values);
                    if (v == NULL) {
                        return NGX_ERROR;
                    }

                    v->data = start;
                    v->len = p - start;
                }
            }

            /* 移除此头部 */
            header[i].hash = 0;
            ngx_str_null(&header[i].key);
            ngx_str_null(&header[i].value);
        }
    }

    /* 移除重复值（不区分大小写） */
    if (ngx_array_init(&unique_values, r->pool, values.nelts, sizeof(ngx_str_t)) != NGX_OK) {
        return NGX_ERROR;
    }

    value = values.elts;

    for (i = 0; i < values.nelts; i++) {
        ngx_str_t *v = &value[i];
        ngx_str_t *uv = unique_values.elts;
        ngx_uint_t found = 0;

        for (j = 0; j < unique_values.nelts; j++) {
            if (v->len == uv[j].len &&
                ngx_strncasecmp(v->data, uv[j].data, v->len) == 0)
            {
                found = 1;
                break;
            }
        }

        if (!found) {
            ngx_str_t *u = ngx_array_push(&unique_values);
            if (u == NULL) {
                return NGX_ERROR;
            }

            *u = *v;
        }
    }

    ngx_str_t accept_encoding = ngx_string("Accept-Encoding");

    /* 如果r->gzip_vary为真，确保'Accept-Encoding'在列表中 */
    if (r->gzip_vary) {
        value = unique_values.elts;
        n = unique_values.nelts;
        ngx_uint_t found = 0;

        for (i = 0; i < n; i++) {
            if (value[i].len == accept_encoding.len &&
                ngx_strncasecmp(value[i].data,
                    accept_encoding.data, accept_encoding.len) == 0) {
                found = 1;
                break;
            }
        }

        if (!found) {
            /* 添加'Accept-Encoding' */
            ngx_str_t *v = ngx_array_push(&unique_values);
            if (v == NULL) {
                return NGX_ERROR;
            }

            v->data = accept_encoding.data;
            v->len = accept_encoding.len;
        }
    }

    /* 构建新的Vary头部值 */
    n = unique_values.nelts;
    value = unique_values.elts;
    size_t total_len = 0;

    for (i = 0; i < n; i++) {
        total_len += value[i].len;
        if (i != n - 1) {
            total_len += 2;  /* ', ' */
        }
    }

    u_char *data = ngx_pnalloc(r->pool, total_len);
    if (data == NULL) {
        return NGX_ERROR;
    }

    u_char *p = data;

    for (i = 0; i < n; i++) {
        p = ngx_copy(p, value[i].data, value[i].len);
        if (i != n - 1) {
            *p++ = ',';
            *p++ = ' ';
        }
    }

    /* 添加新的Vary头部 */
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

static ngx_int_t
ngx_http_compress_vary_filter_init(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_compress_vary_header_filter;

    return NGX_OK;
}
