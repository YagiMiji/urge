// Copyright 2018-2025 Admenri.
// Use of this source code is governed by a BSD - style license that can be
// found in the LICENSE file.

#include "binding/mri/binding_patch.h"

#include <utility>
#include <vector>

#include "content/input/mouse_controller.h"
#include "content/public/engine_graphics.h"
#include "content/public/engine_input.h"

namespace binding {

namespace {

struct BindingSet {
  std::string name;
  int key_id;
};

const BindingSet kKeyboardBindings[] = {
    {"DOWN", 2},   {"LEFT", 4},  {"RIGHT", 6}, {"UP", 8},

    {"A", 11},     {"B", 12},    {"C", 13},    {"X", 14},  {"Y", 15},
    {"Z", 16},     {"L", 17},    {"R", 18},

    {"SHIFT", 21}, {"CTRL", 22}, {"ALT", 23},

    {"F5", 25},    {"F6", 26},   {"F7", 27},   {"F8", 28}, {"F9", 29},
};

std::string GetButtonSymbol(int argc, VALUE* argv) {
  MriCheckArgc(argc, 1);

  std::string sym;
  if (FIXNUM_P(*argv)) {
    int key_id = FIX2INT(*argv);
    for (size_t i = 0; i < std::size(kKeyboardBindings); ++i)
      if (kKeyboardBindings[i].key_id == key_id)
        return kKeyboardBindings[i].name;
  } else if (SYMBOL_P(*argv)) {
    MriParseArgsTo(argc, argv, "n", &sym);
  }

  return sym;
}

std::string GetRenderFilterParamName(VALUE value) {
  if (SYMBOL_P(value))
    return rb_id2name(SYM2ID(value));

  if (RB_TYPE_P(value, RUBY_T_STRING))
    return std::string(RSTRING_PTR(value), RSTRING_LEN(value));

  rb_raise(rb_eTypeError,
           "Render filter parameter name must be a Symbol or String.");
  return {};
}

float GetRenderFilterParamNumericValue(VALUE value) {
  if (!rb_obj_is_kind_of(value, rb_cNumeric))
    rb_raise(rb_eTypeError, "Render filter parameter value must be Numeric.");

  return static_cast<float>(NUM2DBL(value));
}

std::vector<std::string> GetRenderFilterParamNamesOrRaise(
    scoped_refptr<content::Graphics> graphics,
    content::Graphics::RenderFilter filter) {
  content::ExceptionState exception_state;
  std::vector<std::string> names =
      graphics->GetRenderFilterParamNames(filter, exception_state);
  MriProcessException(exception_state);

  if (names.empty()) {
    rb_raise(MriGetException(content::ExceptionCode::CONTENT_ERROR),
             "Render filter %d has no parameters.", static_cast<int>(filter));
  }

  return names;
}

}  // namespace

MRI_METHOD(graphics_set_render_filter_params) {
  int32_t filter_id;
  VALUE params_hash;
  MriParseArgsTo(argc, argv, "io", &filter_id, &params_hash);

  if (!RB_TYPE_P(params_hash, RUBY_T_HASH))
    rb_raise(rb_eTypeError, "Expected render filter params Hash.");

  scoped_refptr<content::Graphics> graphics = MriGetGlobalModules()->Graphics;
  auto filter = static_cast<content::Graphics::RenderFilter>(filter_id);
  GetRenderFilterParamNamesOrRaise(graphics, filter);

  using ParamUpdate = std::pair<std::string, float>;
  std::vector<ParamUpdate> updates;

  VALUE keys = rb_funcall(params_hash, rb_intern("keys"), 0);
  updates.reserve(RARRAY_LEN(keys));
  for (long i = 0; i < RARRAY_LEN(keys); ++i) {
    VALUE key = rb_ary_entry(keys, i);
    std::string name = GetRenderFilterParamName(key);
    VALUE param_value = rb_hash_aref(params_hash, key);
    float value = GetRenderFilterParamNumericValue(param_value);

    content::ExceptionState exception_state;
    graphics->GetRenderFilterParam(filter, name, exception_state);
    MriProcessException(exception_state);

    updates.emplace_back(name, value);
  }

  for (const auto& update : updates) {
    content::ExceptionState exception_state;
    graphics->SetRenderFilterParam(filter, update.first, update.second,
                                   exception_state);
    MriProcessException(exception_state);
  }

  return Qnil;
}

MRI_METHOD(graphics_render_filter_params) {
  int32_t filter_id;
  MriParseArgsTo(argc, argv, "i", &filter_id);

  scoped_refptr<content::Graphics> graphics = MriGetGlobalModules()->Graphics;
  auto filter = static_cast<content::Graphics::RenderFilter>(filter_id);
  std::vector<std::string> names =
      GetRenderFilterParamNamesOrRaise(graphics, filter);

  VALUE result = rb_hash_new();
  for (const auto& name : names) {
    content::ExceptionState exception_state;
    float value = graphics->GetRenderFilterParam(filter, name, exception_state);
    MriProcessException(exception_state);

    rb_hash_aset(result, ID2SYM(rb_intern(name.c_str())), DBL2NUM(value));
  }

  return result;
}

MRI_METHOD(input_is_pressed) {
  scoped_refptr<content::Input> input = MriGetGlobalModules()->Input;
  std::string key = GetButtonSymbol(argc, argv);
  content::ExceptionState exception_state;
  bool v = input->IsPressed(key, exception_state);
  MriProcessException(exception_state);
  return v ? Qtrue : Qfalse;
}

MRI_METHOD(input_is_triggered) {
  scoped_refptr<content::Input> input = MriGetGlobalModules()->Input;
  std::string key = GetButtonSymbol(argc, argv);
  content::ExceptionState exception_state;
  bool v = input->IsTriggered(key, exception_state);
  MriProcessException(exception_state);
  return v ? Qtrue : Qfalse;
}

MRI_METHOD(input_is_repeated) {
  scoped_refptr<content::Input> input = MriGetGlobalModules()->Input;
  std::string key = GetButtonSymbol(argc, argv);
  content::ExceptionState exception_state;
  bool v = input->IsRepeated(key, exception_state);
  MriProcessException(exception_state);
  return v ? Qtrue : Qfalse;
}

void ApplyInputPatch() {
  VALUE klass = rb_const_get(rb_cObject, rb_intern("Input"));

  MriDefineModuleFunction(klass, "press?", input_is_pressed);
  MriDefineModuleFunction(klass, "trigger?", input_is_triggered);
  MriDefineModuleFunction(klass, "repeat?", input_is_repeated);

  for (size_t i = 0; i < std::size(kKeyboardBindings); ++i) {
    auto& binding_set = kKeyboardBindings[i];
    ID key = rb_intern(binding_set.name.c_str());
    rb_const_set(klass, key, INT2FIX(binding_set.key_id));
  }
}

void ApplyGraphicsPatch() {
  VALUE klass = rb_const_get(rb_cObject, rb_intern("Graphics"));

  MriDefineModuleFunction(klass, "set_render_filter_params",
                          graphics_set_render_filter_params);
  MriDefineModuleFunction(klass, "render_filter_params",
                          graphics_render_filter_params);
}

struct MouseButtonSet {
  std::string name;
  int button_id;
};

const MouseButtonSet kMouseButtonSets[] = {
    {"LEFT", content::MouseImpl::Button::Left},
    {"MIDDLE", content::MouseImpl::Button::Middle},
    {"RIGHT", content::MouseImpl::Button::Right},
    {"X1", content::MouseImpl::Button::X1},
    {"X2", content::MouseImpl::Button::X2},
};

void ApplyMousePatch() {
  VALUE klass = rb_const_get(rb_cObject, rb_intern("Mouse"));

  for (size_t i = 0; i < std::size(kMouseButtonSets); ++i)
    rb_const_set(klass, rb_intern(kMouseButtonSets[i].name.c_str()),
                 INT2FIX(kMouseButtonSets[i].button_id));
}

void MriApplyBindingPatch() {
  ApplyGraphicsPatch();
  ApplyInputPatch();
  ApplyMousePatch();
}

}  // namespace binding
