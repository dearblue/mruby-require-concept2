MRuby::Gem::Specification.new("mruby-require") do |spec|
  spec.license = "CC0"
  spec.author  = "dearblue"
  spec.summary = "Kernel#require features"

  def disable_mrb_loading
    cc.defines << %w(MRUBY_REQUIRE_NO_IREP_LOADER)
    cxx.defines << %w(MRUBY_REQUIRE_NO_IREP_LOADER)
  end

  def enable_rb_loading
    add_dependency "mruby-compiler", core: "mruby-compiler"
    cc.defines << %w(MRUBY_REQUIRE_USE_COMPILER)
    cxx.defines << %w(MRUBY_REQUIRE_USE_COMPILER)
  end

  enable_rb_loading
end
