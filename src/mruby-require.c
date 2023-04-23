#if 0
cmd = %W(#{ARGV[0] || ENV["CC"] || "cc"} -std=gnu99 -Iinclude -DMRB_NO_PRESYM -O0 -Wall -Wextra -c #{__FILE__})
puts %(>>> #{cmd.inspect}\n)
system *cmd
p $?.exitstatus unless $?.success?
__END__
#endif

#include <mruby.h>
#ifdef MRB_NO_STDIO
# error mruby-require conflicts MRB_NO_STDIO
#endif
#if defined(MRUBY_REQUIRE_NO_IREP_LOADER) && !defined(MRUBY_REQUIRE_USE_COMPILER)
# error "enable_rb_loading がない disable_mrb_loading の使用は出来ません"
#endif
#ifdef MRUBY_REQUIRE_USE_COMPILER
# include <mruby/compile.h>
#endif
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/debug.h>
#include <mruby/dump.h>
#include <mruby/error.h>
#include <mruby/irep.h>
#include <mruby/opcode.h>
#include <mruby/proc.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/presym.h>
#include <mruby/require.h>

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#if defined(_WIN32)
# include <locale.h>
# include <wchar.h>
# include <windows.h>
# include <winbase.h>

static char *
realpath(const char *path, char *resolved_path)
{
  size_t bufsize = MAX_PATH + 1;
  size_t needs = GetFullPathNameA(path, bufsize, resolved_path, NULL);
  if (needs <= bufsize) {
    char *p = resolved_path;
    const char *term = p + needs;
    mbstate_t mbs = 0;
    while (p < term) {
      int clen = (int)mbrlen(p, term - p, &mbs);
      if (clen > 1) {
        p += clen;
      } else {
        if (*p == '\\') {
          *p = '/';
        }
        p++;
      }
    }
    return resolved_path;
  } else {
    errno = ENAMETOOLONG;
    return NULL;
  }
}
#endif // _WIN32

#define E_LOAD_ERROR mrb_exc_get_id(mrb, MRB_SYM(LoadError))
#define CINFO_NONE    0 /* from src/vm.c */
#define CINFO_DIRECT  2 /* from src/vm.c */
#define CI_CCI_DIRECT_P(ci) ((ci)->cci == CINFO_DIRECT)

#define RARRAY_FOREACH(ary, var) \
  for (const mrb_value *var = RARRAY_PTR(ary), \
                       *const _end_of_ ## var = var + RARRAY_LEN(ary); \
       var < _end_of_ ## var; \
       var++)

typedef mrb_value loader_func(mrb_state *mrb, FILE *fp, mrbc_context *cxt);

#if defined(_WIN32) || defined(_WIN64)
# define PATH_SEP_P(ch) ((ch) == '/' || (ch) == '\\')
#else
# define PATH_SEP_P(ch) ((ch) == '/')
#endif

static mrb_bool
absolute_path_p(const char *str)
{
  mrb_assert(str != NULL);

  if (PATH_SEP_P(str[0])) {
    return TRUE;
  }
#if defined(_WIN32) || defined(_WIN64)
  if (ISALPHA(str[0]) && str[1] == ':') {
    return TRUE;
  }
#endif
  return FALSE;
}

static mrb_bool
own_or_parent_path_p(const char *path)
{
  if (path[0] == '.' &&
      (PATH_SEP_P(path[1]) || (path[1] == '.' && PATH_SEP_P(path[2])))) {
    return TRUE;
  } else {
    return FALSE;
  }
}

static const char *
extname(const char *path)
{
  const char *p = strrchr(path, '.');
  if (!p) {
    return NULL;
  }

  if (strchr(p, '/')) {
    return NULL;
  }
#if defined(_WIN32) || defined(_WIN64)
  if (strchr(p, '\\')) {
    return NULL;
  }
#endif

  return p;
}

static mrb_bool
exist_file(const char *path)
{
  struct stat st;
  if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
    return TRUE;
  } else {
    return FALSE;
  }
}

static mrb_bool
str_equal(mrb_value a, mrb_value b)
{
  if (mrb_string_p(a) && mrb_string_p(b) &&
      RSTRING_LEN(a) == RSTRING_LEN(b) &&
      memcmp(RSTRING_PTR(a), RSTRING_PTR(b), RSTRING_LEN(a)) == 0) {
    return TRUE;
  } else {
    return FALSE;
  }
}

static mrb_noreturn void
raise_load_error(mrb_state *mrb, const char *feature)
{
  mrb_raisef(mrb, E_LOAD_ERROR, "cannot load such file -- %s", feature);
}

static mrb_bool
ary_include_p(mrb_value ary, mrb_value element)
{
  RARRAY_FOREACH(ary, p) {
    if (str_equal(*p, element)) {
      return TRUE;
    }
  }

  return FALSE;
}

static mrb_value
str_recycle(mrb_state *mrb, mrb_value *maybe_str, const char *str)
{
  if (mrb_string_p(*maybe_str)) {
    struct RString *strp = mrb_str_ptr(*maybe_str);
    mrb_str_modify(mrb, strp);
    RSTR_SET_LEN(strp, 0);

    if (str) {
      mrb_str_cat_cstr(mrb, *maybe_str, str);
    }
  } else {
    if (str) {
      *maybe_str = mrb_str_new_cstr(mrb, str);
    } else {
      *maybe_str = mrb_str_new_capa(mrb, 128);
    }
  }

  return *maybe_str;
}

#if 0
struct strscrubber {
  const char *str;
  mbstate_t mbs;
};

static const char *
strscrubber_next(struct strscrubber *scr)
{
  if (scr->str[0] == '\0') {
    return NULL;
  } else {
    const char *p = scr->str;
    int clen = (int)mbrlen(p, MB_CUR_MAX, &scr->mbs);
    if (clen > 1) {
      scr->str += clen;
    } else {
      scr->str++;
    }
    return p;
  }
}

static const char *
dirname_mod(const char *path)
{
  struct strscrubber scr = { path, 0 };

}
#endif

static const char *
get_irep_basedir(mrb_state *mrb, const mrb_irep *irep)
{
  const char *basepath = mrb_debug_get_filename(mrb, irep, (uint32_t)(mrb->c->ci[-1].pc - irep->iseq));
  if (!basepath || strcmp(basepath, "-e") == 0) {
    return NULL;
  }

  const char *p = strrchr(basepath, '/');
  if (p && p != basepath) {
    mrb_value str = mrb_str_new(mrb, basepath, p - basepath);
    mrb_obj_ptr(str)->c = NULL;
    return mrb_string_cstr(mrb, str);
  }

  return "/";
}

static const char *
make_feature_path(mrb_state *mrb, mrb_value *tmpstr, const char *basedir, const char *path)
{
  if (basedir == NULL) {
    return path;
  }

  str_recycle(mrb, tmpstr, NULL);

  const char *p = strrchr(basedir, '/');
  if (p && p[1] == '\0') {
    mrb_str_cat_cstr(mrb, *tmpstr, basedir);
  } else {
    mrb_str_cat_cstr(mrb, *tmpstr, basedir);
    mrb_str_cat_cstr(mrb, *tmpstr, "/");
  }
  mrb_str_cat_cstr(mrb, *tmpstr, path);

  return mrb_string_cstr(mrb, *tmpstr);
}

#define ID_LOAD_PATH mrb_intern_lit(mrb, "$:")
#define ID_LOADED_FEATURES mrb_intern_lit(mrb, "$\"")

static mrb_value
get_loadpath(mrb_state *mrb)
{
  mrb_value loadpath = mrb_gv_get(mrb, ID_LOAD_PATH);
  mrb_ensure_array_type(mrb, loadpath);
  return loadpath;
}

static mrb_value
get_loadedfeatures(mrb_state *mrb)
{
  mrb_value features = mrb_gv_get(mrb, ID_LOADED_FEATURES);
  mrb_ensure_array_type(mrb, features);
  return features;
}

static mrb_value
prepare_runner(mrb_state *mrb, mrb_value proc)
{
  mrb_check_type(mrb, proc, MRB_TT_PROC);
  mrb->c->ci->mid = 0;
  mrb->c->ci->cci = CINFO_NONE;
  mrb->c->ci->u.target_class = mrb->object_class; // 強制的な書き換え (もしかしたら不要かも？)
  return mrb_yield_cont(mrb, proc, mrb_top_self(mrb), 0, NULL);
}

static mrb_value
epilogue_require(mrb_state *mrb, mrb_value self)
{
  (void)self;

  mrb_value signature = mrb_proc_cfunc_env_get(mrb, 0);
  if (!mrb_nil_p(signature)) {
    mrb_value features = get_loadedfeatures(mrb);
    mrb_ary_push(mrb, features, signature);
  }
  return mrb_true_value();
}

/*
 * IMPORTANT: 末尾呼び出しの形をしなければならない
 */
static mrb_value
ignition_irep(mrb_state *mrb, mrb_value proc, mrb_value features, mrb_value signature, mrb_value wrap, mrb_bool tailcall)
{
  if (!tailcall || (mrb->c->ci <= mrb->c->cibase && CI_CCI_DIRECT_P(mrb->c->ci))) {
    mrb_top_run(mrb, mrb_proc_ptr(proc), mrb_top_self(mrb), 0);
    if (!mrb_nil_p(features)) {
      mrb_ary_push(mrb, features, signature);
    }
    return mrb_true_value();
  } else {
    /*
     *  このブロックは mruby VM 内から feature を実行するための準備を行う。
     *
     *  ブロックから関数の呼び出し元に C の制御が移った時、mruby VM のコールフレームは次のようになっている。
     *
     *      cibase ... [epilogue of Kernel#require] [code runner] [dummy frame]
     *
     *  "epilogue of Kernel#require" は Kernel#require が成功したとき $" に登録して true を返す関数を呼び出す。
     *
     *  "code runner" は Kernel#require によって読み込まれたコードを実行する。
     *
     *  "dummy frame" は mrb_vm_exec() へ制御が戻ったときに、Kernel#require の代わりに削除されるフレーム。
     */

    static const mrb_code require_iseq[] = {
      OP_CALL,
    };

    static const mrb_irep require_irep = {
      1, 4, 0, MRB_IREP_STATIC,
      require_iseq, NULL, NULL, NULL, NULL, NULL,
      sizeof(require_iseq), 0, 0, 0, 0,
    };

    static const struct RProc require_proc = {
      NULL, NULL, MRB_TT_PROC, MRB_GC_RED, MRB_FL_OBJ_IS_FROZEN | MRB_PROC_SCOPE | MRB_PROC_STRICT,
      { &require_irep }, NULL, { NULL }
    };

    static const struct RProc runner_proc = {
      NULL, NULL, MRB_TT_PROC, MRB_GC_RED, MRB_FL_OBJ_IS_FROZEN | MRB_PROC_CFUNC_FL,
      { (const mrb_irep *)prepare_runner }, NULL, { NULL }
    };

    if (mrb_nil_p(features)) {
      signature = mrb_nil_value();
    }

    struct RProc *epilogue_require_proc = mrb_proc_new_cfunc_with_env(mrb, epilogue_require, 1, &signature);

    mrb_callinfo *ci = mrb->c->ci;
    ci->proc = &require_proc;
    ci->cci = CINFO_NONE;
    ci->pc = require_iseq;
    ci->stack[0] = mrb_obj_value(epilogue_require_proc);
    ci->u.target_class = mrb->object_class;

    mrb_yield_with_class(mrb, mrb_obj_value((void *)(uintptr_t)&runner_proc), 0, NULL, proc, mrb->object_class);
    mrb->c->ci++; // mrb_yield_cont() で積んだダミー CI を再利用

    return mrb_top_self(mrb); // mrb_yield_cond() で実行されるライブラリの self として使われる
  }
}

/*
 * IMPORTANT: 末尾呼び出しの形をしなければならない
 */
static mrb_value
load_common(mrb_state *mrb, const char *path, loader_func *loader, mrb_value wrap, mrb_bool add_features, mrb_bool tailcall)
{
  mrb_value signature = mrb_str_new_capa(mrb, PATH_MAX);
  if (!realpath(path, RSTRING_PTR(signature))) {
    mrb_sys_fail(mrb, path);
  }
  RSTR_SET_LEN(mrb_str_ptr(signature), strlen(RSTRING_PTR(signature)));
  mrb_str_resize(mrb, signature, RSTRING_LEN(signature));

  mrb_value features;
  if (add_features) {
    features = get_loadedfeatures(mrb);
    if (ary_include_p(features, signature)) {
      return mrb_false_value();
    }
    mrb_check_frozen(mrb, mrb_obj_ptr(features));
  } else {
    features = mrb_nil_value();
  }

  if (!exist_file(mrb_string_cstr(mrb, signature))) {
    return mrb_undef_value();
  }

  mrbc_context *cxt = mrbc_context_new(mrb);
  cxt->no_exec = TRUE;
  mrbc_filename(mrb, cxt, mrb_string_cstr(mrb, signature));

  FILE *fp = fopen(mrb_string_cstr(mrb, signature), "rb");
  if (fp == NULL) {
    mrbc_context_free(mrb, cxt);
    mrb_raisef(mrb, E_LOAD_ERROR, "Cannot open library file: %s", path);
  }

  mrb_value proc = loader(mrb, fp, cxt);
  fclose(fp);
  mrbc_context_free(mrb, cxt);
  if (!mrb_proc_p(proc)) {
    if (mrb->exc) {
      mrb_exc_raise(mrb, mrb_obj_value(mrb->exc));
    } else {
      mrb_raisef(mrb, E_SCRIPT_ERROR, "maybe syntax error - %s", path);
    }
  }

  if (mrb_test(wrap)) {
    if (mrb_module_p(wrap)) {
      mrb_proc_ptr(proc)->e.target_class = mrb_class_ptr(wrap);
    } else {
      struct RClass *mod = mrb_module_new(mrb);
      mrb_proc_ptr(proc)->e.target_class = mod;
      wrap = mrb_obj_value(mod);
    }
  } else {
    mrb_proc_ptr(proc)->e.target_class = mrb->object_class;
    wrap = mrb_nil_value();
  }

  return ignition_irep(mrb, proc, features, signature, wrap, tailcall);
}

static mrb_value
search_and_require(mrb_state *mrb, mrb_value *tmpstr, const char *basedir, const char *path, mrb_bool tailcall)
{
  path = make_feature_path(mrb, tmpstr, basedir, path);
  if (exist_file(path)) {
    const char *ext = extname(path);
#ifdef MRUBY_REQUIRE_USE_COMPILER
    if (strcasecmp(ext, ".rb") == 0) {
      return load_common(mrb, path, mrb_load_file_cxt, mrb_nil_value(), TRUE, tailcall);
    }
#endif
#ifndef MRUBY_REQUIRE_NO_IREP_LOADER
    if (strcasecmp(ext, ".mrb") == 0) {
      return load_common(mrb, path, mrb_load_irep_file_cxt, mrb_nil_value(), TRUE, tailcall);
    }
#endif
  }

  if (!basedir) {
    str_recycle(mrb, tmpstr, path);
  }

#ifdef MRUBY_REQUIRE_USE_COMPILER
  mrb_ssize savedlen = RSTRING_LEN(*tmpstr);
  mrb_str_cat_cstr(mrb, *tmpstr, ".rb");
  if (exist_file(mrb_string_cstr(mrb, *tmpstr))) {
    return load_common(mrb, mrb_string_cstr(mrb, *tmpstr), mrb_load_file_cxt, mrb_nil_value(), TRUE, tailcall);
  }
  RSTR_SET_LEN(mrb_str_ptr(*tmpstr), savedlen);
#endif

#ifndef MRUBY_REQUIRE_NO_IREP_LOADER
  mrb_str_cat_cstr(mrb, *tmpstr, ".mrb");
  if (exist_file(mrb_string_cstr(mrb, *tmpstr))) {
    return load_common(mrb, mrb_string_cstr(mrb, *tmpstr), mrb_load_irep_file_cxt, mrb_nil_value(), TRUE, tailcall);
  }
#endif

  return mrb_undef_value();
}

#if defined(MRUBY_REQUIRE_NO_IREP_LOADER)
# define load_loader mrb_load_file_cxt
#elif !defined(MRUBY_REQUIRE_USE_COMPILER)
# define load_loader mrb_load_irep_file_cxt
#else
# define load_loader mrb_load_detect_file_cxt
#endif

static mrb_value
load_internal(mrb_state *mrb, const char *path, mrb_value wrap, mrb_bool tailcall)
{
  if (exist_file(path)) {
    mrb_value result = load_common(mrb, path, load_loader, wrap, FALSE, tailcall);
    if (!mrb_undef_p(result)) {
      return result;
    }
  }

  if (!absolute_path_p(path) && !own_or_parent_path_p(path)) {
    mrb_value tmpstr = mrb_nil_value();
    mrb_value loadpath = get_loadpath(mrb);
    RARRAY_FOREACH(loadpath, dir) {
      if (mrb_string_p(*dir)) {
        const char *path1 = make_feature_path(mrb, &tmpstr, mrb_string_cstr(mrb, *dir), path);
        if (exist_file(path1)) {
          mrb_value result = load_common(mrb, path1, load_loader, wrap, FALSE, tailcall);
          if (!mrb_undef_p(result)) {
            return result;
          }
        }
      }
    }
  }

  raise_load_error(mrb, path);
  return mrb_nil_value(); /* not reached */
}

MRB_API void
mrb_load(mrb_state *mrb, const char *path, mrb_value wrap)
{
  load_internal(mrb, path, wrap, FALSE);
}

static mrb_value
require_internal(mrb_state *mrb, const char *feature, mrb_bool tailcall)
{
  mrb_value tmpstr = mrb_nil_value();
  if (absolute_path_p(feature) || own_or_parent_path_p(feature)) {
    mrb_value result = search_and_require(mrb, &tmpstr, NULL, feature, tailcall);
    if (!mrb_undef_p(result)) {
      return result;
    }
  } else {
    mrb_value loadpath = get_loadpath(mrb);
    RARRAY_FOREACH(loadpath, dir) {
      if (mrb_string_p(*dir)) {
        mrb_value result = search_and_require(mrb, &tmpstr, mrb_string_cstr(mrb, *dir), feature, tailcall);
        if (!mrb_undef_p(result)) {
          return result;
        }
      }
    }
  }

  raise_load_error(mrb, feature);
  return mrb_nil_value(); /* not reached */
}

MRB_API mrb_bool
mrb_require(mrb_state *mrb, const char *feature)
{
  mrb_value result = require_internal(mrb, feature, FALSE);
  return mrb_test(result);
}

static mrb_value
mrb_f_load(mrb_state *mrb, mrb_value self)
{
  (void)self;

  const char *path;
  mrb_value wrap = mrb_nil_value();
  mrb_get_args(mrb, "z|o", &path, &wrap);
  return load_internal(mrb, path, wrap, TRUE);
}

static mrb_value
mrb_f_require(mrb_state *mrb, mrb_value self)
{
  (void)self;

  const char *feature;
  mrb_get_args(mrb, "z", &feature);
  return require_internal(mrb, feature, TRUE);
}

static mrb_value
mrb_f_require_relative(mrb_state *mrb, mrb_value self)
{
  (void)self;

  const char *feature;
  mrb_get_args(mrb, "z", &feature);

  if (mrb->c->ci <= mrb->c->cibase) {
inferbasepath:
    mrb_raise(mrb, E_LOAD_ERROR, "cannot infer basepath");
  }

  const struct RProc *proc = mrb->c->ci[-1].proc;
  if (!proc || MRB_PROC_CFUNC_P(proc)) {
    goto inferbasepath;
  }

  const char *basedir = get_irep_basedir(mrb, proc->body.irep);
  if (!basedir) {
    goto inferbasepath;
  }

  mrb_value tmpstr = mrb_nil_value();
  mrb_value result = search_and_require(mrb, &tmpstr, basedir, feature, TRUE);
  if (mrb_undef_p(result)) {
    raise_load_error(mrb, feature);
  }

  return result;
}

void
mrb_mruby_require_gem_init(mrb_state *mrb)
{
  mrb_define_class(mrb, "LoadError", E_SCRIPT_ERROR);

  mrb_define_method(mrb, mrb->kernel_module, "load", mrb_f_load, MRB_ARGS_ANY());
  mrb_define_method(mrb, mrb->kernel_module, "require", mrb_f_require, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, mrb->kernel_module, "require_relative", mrb_f_require_relative, MRB_ARGS_REQ(1));

  mrb_gv_set(mrb, ID_LOAD_PATH, mrb_ary_new(mrb));
  mrb_gv_set(mrb, ID_LOADED_FEATURES, mrb_ary_new(mrb));
}

void
mrb_mruby_require_gem_final(mrb_state *mrb)
{
  (void)mrb;
}
