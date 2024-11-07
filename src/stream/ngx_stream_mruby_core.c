/*
// ngx_stream_mruby_core.c - ngx_mruby mruby module
//
// See Copyright Notice in ngx_http_mruby_module.c
*/

#include "ngx_stream_mruby_core.h"

#include "ngx_stream_mruby_module.h"

#include <mruby/hash.h>
#include <mruby/string.h>

static mrb_value ngx_stream_mrb_errlogger(mrb_state *mrb, mrb_value self)
{
  mrb_value msg;
  mrb_int log_level;
  ngx_stream_mruby_internal_ctx_t *ictx = mrb->ud;
  ngx_stream_session_t *s = ictx->s;

  if (s == NULL) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "can't use logger at this phase. only use at session stream phase");
  }

  mrb_get_args(mrb, "io", &log_level, &msg);
  if (log_level < 0) {
    ngx_log_error(NGX_LOG_ERR, s->connection->log, 0, "%s ERROR %s: log level is not positive number", MODULE_NAME,
                  __func__);
    return self;
  }
  msg = mrb_obj_as_string(mrb, msg);
  ngx_log_error((ngx_uint_t)log_level, s->connection->log, 0, "%*s", RSTRING_LEN(msg), RSTRING_PTR(msg));

  return self;
}

static mrb_value ngx_stream_mrb_get_ngx_mruby_name(mrb_state *mrb, mrb_value self)
{
  return mrb_str_new_lit(mrb, MODULE_NAME);
}

static mrb_value ngx_stream_mrb_add_listener(mrb_state *mrb, mrb_value self)
{
  ngx_stream_mruby_srv_conf_t *mscf = mrb->ud;
  ngx_stream_core_srv_conf_t *cscf = mscf->ctx->cscf;
  ngx_conf_t *cf = mscf->ctx->cf;
  mrb_value listener, address;
  ngx_str_t addr;
  ngx_url_t u;
#if (nginx_version > 1015009)
  ngx_uint_t i, n;
#else
  ngx_uint_t i;
#endif
  ngx_stream_listen_opt_t ls;

  mrb_get_args(mrb, "H", &listener);
  address = mrb_hash_get(mrb, listener, mrb_check_intern_cstr(mrb, "address"));
  addr.data = (u_char *)RSTRING_PTR(address);
  addr.len = RSTRING_LEN(address);

  ngx_memzero(&u, sizeof(ngx_url_t));

  u.url = addr;
  u.listen = 1;
  cscf->listen = 1;

  if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
    if (u.err) {
      ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%s in \"%V\" of the \"listen\" directive via mruby", u.err, &u.url);
    }

    mrb_raise(mrb, E_RUNTIME_ERROR, "ngx_stream_mrb_add_listener ngx_parse_url failed");
  }

  ngx_memzero(&ls, sizeof(ngx_stream_listen_opt_t));
  ls.rcvbuf = -1;
  ls.sndbuf = -1;
  ls.type = SOCK_STREAM;
  ls.wildcard = u.wildcard;

#if (NGX_HAVE_INET6)
  ls.ipv6only = 1;
#endif

#if !(NGX_WIN32)
  if (mrb_bool(mrb_hash_get(mrb, listener, mrb_check_intern_cstr(mrb, "udp")))) {
    ls.type = SOCK_DGRAM;
  }
#endif

  if (ls.type == SOCK_DGRAM) {
#if (NGX_STREAM_SSL)
    if (ls.ssl) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "\"ssl\" parameter is incompatible with \"udp\"");
    }
#endif

    if (ls.so_keepalive) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "\"so_keepalive\" parameter is incompatible with \"udp\"");
    }

    if (ls.proxy_protocol) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "\"proxy_protocol\" parameter is incompatible with \"udp\"");
    }
  }

  for (n = 0; n < u.naddrs; n++) {
    for (i = 0; i < n; i++) {
      if (ngx_cmp_sockaddr(u.addrs[n].sockaddr, u.addrs[n].socklen, u.addrs[i].sockaddr, u.addrs[i].socklen, 1) ==
          NGX_OK) {
        goto next;
      }
    next:
      continue;
    }

    ls.sockaddr = u.addrs[n].sockaddr;
    ls.socklen = u.addrs[n].socklen;
    ls.addr_text = u.addrs[n].name;
    ls.wildcard = ngx_inet_wildcard(ls.sockaddr);

    if (ngx_stream_add_listen(cf, cscf, &ls) != NGX_OK) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "can't add strem listener");
    }
  }

  return mrb_true_value();
}

ngx_stream_mruby_ctx_t *ngx_stream_mrb_get_module_ctx(mrb_state *mrb, ngx_stream_session_t *s)
{
  ngx_stream_mruby_ctx_t *ctx;
  ctx = ngx_stream_get_module_ctx(s, ngx_stream_mruby_module);
  if (ctx == NULL) {
    if ((ctx = ngx_pcalloc(s->connection->pool, sizeof(*ctx))) == NULL) {
      if (mrb != NULL) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "failed to allocate context");
      } else {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "failed to allocate memory from r->pool(mrb_state is a nonexistent directive) %s:%d",
                      __FUNCTION__, __LINE__);
        return NULL;
      }
      ctx->cleanup = NULL;
    }
    ngx_stream_set_ctx(s, ctx, ngx_stream_mruby_module);
  }
  return ctx;
}
void ngx_stream_mrb_core_class_init(mrb_state *mrb, struct RClass *class)
{
  mrb_define_const(mrb, class, "OK", mrb_fixnum_value(NGX_OK));
  mrb_define_const(mrb, class, "ERROR", mrb_fixnum_value(NGX_ERROR));
  mrb_define_const(mrb, class, "AGAIN", mrb_fixnum_value(NGX_AGAIN));
  mrb_define_const(mrb, class, "BUSY", mrb_fixnum_value(NGX_BUSY));
  mrb_define_const(mrb, class, "DONE", mrb_fixnum_value(NGX_DONE));
  mrb_define_const(mrb, class, "DECLINED", mrb_fixnum_value(NGX_DECLINED));
  mrb_define_const(mrb, class, "ABORT", mrb_fixnum_value(NGX_ABORT));
  // error log priority
  mrb_define_const(mrb, class, "LOG_STDERR", mrb_fixnum_value(NGX_LOG_STDERR));
  mrb_define_const(mrb, class, "LOG_EMERG", mrb_fixnum_value(NGX_LOG_EMERG));
  mrb_define_const(mrb, class, "LOG_ALERT", mrb_fixnum_value(NGX_LOG_ALERT));
  mrb_define_const(mrb, class, "LOG_CRIT", mrb_fixnum_value(NGX_LOG_CRIT));
  mrb_define_const(mrb, class, "LOG_ERR", mrb_fixnum_value(NGX_LOG_ERR));
  mrb_define_const(mrb, class, "LOG_WARN", mrb_fixnum_value(NGX_LOG_WARN));
  mrb_define_const(mrb, class, "LOG_NOTICE", mrb_fixnum_value(NGX_LOG_NOTICE));
  mrb_define_const(mrb, class, "LOG_INFO", mrb_fixnum_value(NGX_LOG_INFO));
  mrb_define_const(mrb, class, "LOG_DEBUG", mrb_fixnum_value(NGX_LOG_DEBUG));

  mrb_define_class_method(mrb, class, "errlogger", ngx_stream_mrb_errlogger, MRB_ARGS_REQ(2));
  mrb_define_class_method(mrb, class, "log", ngx_stream_mrb_errlogger, MRB_ARGS_REQ(2));
  mrb_define_class_method(mrb, class, "module_name", ngx_stream_mrb_get_ngx_mruby_name, MRB_ARGS_NONE());
  mrb_define_class_method(mrb, class, "add_listener", ngx_stream_mrb_add_listener, MRB_ARGS_REQ(1));
}
