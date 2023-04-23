#ifndef MRUBY_REQUIRE_H
#define MRUBY_REQUIRE_H 1

#include <mruby.h>

MRB_BEGIN_DECL

/**
 * 例外を捕捉するために、mrb_protect_error() を使って下さい。
 */
MRB_API void mrb_load(mrb_state *mrb, const char *path, mrb_value wrap);

/**
 * 例外を捕捉するために、mrb_protect_error() を使って下さい。
 */
MRB_API mrb_bool mrb_require(mrb_state *mrb, const char *feature);

MRB_END_DECL

#endif /* MRUBY_REQUIRE_H */
