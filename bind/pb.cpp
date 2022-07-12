// PyBind11 bindings generator on top of cppast
// Written by Victor Miquel <victor.miquel@ens.psl.eu>

#include "pb.hpp"

#include <algorithm>
#include <cppast/cpp_type_alias.hpp>

void print_warn(const std::string& msg)
{
  std::cerr << "\033[1;33m" << msg << "\033[0m" << '\n';
}

#define DEBUG

inline void print_debug(std::string const& msg) {
#ifdef DEBUG
  std::cerr << "\033[1;35m" << msg << "\033[0m" << '\n';
#endif
}



Printer::Printer(std::ostream& out, std::string prefix) : out(out), prefix(prefix) {}

Printer Printer::operator+(std::string str) {
  return Printer(out, prefix+str);
}

Printer& Printer::operator+=(std::string str) {
  prefix += str;
  return *this;
}

void Printer::line() {
  out << std::endl;
}

void Printer::line(std::string str) {
  out << prefix << str << std::endl;
}



std::string str_params(std::vector<std::string> const& params) {
  std::string ret = "";
  for (unsigned k = 0; k < params.size(); k++) {
    ret += params[k];
    if (k + 1 < params.size()) ret += ", ";
  }
  return ret;
}

// TODO: handle nested templates (ie: ",<,>,")
std::vector<std::string> split_params(std::string const& params) {
  std::vector<std::string> ret = {""};
  for (char const& c : params) {
    if (c==',') ret.push_back("");
    else ret.back() += c;
  }
  return ret;
}



Context::Context(cppast::cpp_entity_index const& idx) : idx(idx), access(cppast::cpp_access_specifier_kind::cpp_public) {}

Context::Context(Context const& ctx, cppast::cpp_class_template_specialization const& cts) : idx(ctx.idx) {
  //tpl_args["T"] = "Float";
  
  auto const& ct = dynamic_cast<cppast::cpp_class_template const&>(cts.primary_template().get(ctx.idx)[0].get());

  auto args = split_params(cts.unexposed_arguments().as_string());
  unsigned k = 0;

  for (auto const& x : ct.parameters()) {
    if (k == args.size()) break;
    tpl_args[std::string(dynamic_cast<cppast::cpp_template_type_parameter const&>(x).name())] = args[k];
  }
}

Context::Context(Context const& ctx, cppast::cpp_access_specifier_kind access) : idx(ctx.idx), tpl_args(ctx.tpl_args), access(access) {
}

std::string Context::to_string(cppast::cpp_type const& type) {
  class to_string_generator : public cppast::code_generator {
   public:
    to_string_generator(std::map<std::string, std::string> const& tpl_args) : tpl_args(tpl_args) {}

    std::string get()
    {
        return std::move(result_);
    }

   private:
    void do_indent() override {}

    void do_unindent() override {}

    void do_write_token_seq(cppast::string_view tokens) override
    {
      std::string str = tokens.c_str();
      if (tpl_args.contains(str)) result_ += tpl_args.at(str);
      else result_ += str;
    }

    std::string result_;
    std::map<std::string, std::string> const& tpl_args;
  } generator(tpl_args);

  // just a dummy type for the output
  static auto dummy_entity = cppast::cpp_type_alias::build("foo", cppast::cpp_builtin_type::build(cppast::cpp_int));
  to_string_generator::output output(type_safe::ref(generator), type_safe::ref(*dummy_entity),
                                     cppast::cpp_public);
  cppast::detail::write_type(output, type, "");
  return generator.get();
}

bool Context::is_public() const {
  return access == cppast::cpp_access_specifier_kind::cpp_public;
}

bool Context::is_protected() const {
  return access == cppast::cpp_access_specifier_kind::cpp_protected;
}



Name::Name(std::string name, std::string scope, bool auto_scope) : name(name), scope(scope), auto_scope(auto_scope) {}

std::string Name::cpp_simple_name() const {
  return name;
}

std::string Name::cpp_name() const {
  return scope + name;
}

std::string Name::sane_name() const {
  std::string ret = name;
  for (char & c : ret) {
    if ((c>='a') && (c<='z')) continue;
    if ((c>='A') && (c<='Z')) continue;
    if ((c>='0') && (c<='9')) continue;
    c = '_';
  }
  return ret;
}

std::string Name::self_scope() const {
  return scope;
}

std::string Name::as_scope() const {
  if (auto_scope) return self_scope();
  return scope + name + "::";
}

std::string Name::bind_name() const {
  return "PB__" + sane_name();
}

std::string Name::py_name() const {
  return sane_name();
}

Name Name::operator+(std::string son) const {
  return Name(son, as_scope(), false);
}

void Name::change_parent(Name new_parent) {
  scope = new_parent.as_scope();
}



PB_Def::PB_Def(std::string name, Name parent, Context ctx) : name(parent + name), parent(parent), def("def"), is_protected(ctx.is_protected()) {}

PB_Def::PB_Def(cppast::cpp_function const& func, Name parent, Context ctx) : PB_Def(func.name(), parent, ctx) {}

bool is_const_deep(cppast::cpp_type const& t) {
  switch (t.kind()) {
   case cppast::cpp_type_kind::cv_qualified_t:
    return cppast::is_const(dynamic_cast<cppast::cpp_cv_qualified_type const&>(t).cv_qualifier());
   case cppast::cpp_type_kind::array_t:
    return is_const_deep(dynamic_cast<cppast::cpp_array_type const&>(t).value_type());
   default:
    return false;
  }
}

PB_Def::PB_Def(cppast::cpp_member_variable const& var, Name parent, Context ctx) : PB_Def(var.name(), parent, ctx) {
  if (is_const_deep(var.type())) {
    def = "def_readonly";
  } else {
    def = "def_readwrite";
  }
}

PB_Def::PB_Def(cppast::cpp_variable const& var, Name parent, Context ctx) : PB_Def(var.name(), parent, ctx) {
  is_static = cppast::is_static(var.storage_class());

  bool is_writable = !is_const_deep(var.type());

  if (is_writable) {
    def = "def_readwrite";
  } else {
    def = "def_readonly";
  }

  if (is_static) def += "_static";
}

void PB_Def::change_parent(Name new_parent) {
  parent = new_parent;
  name.change_parent(new_parent);
}

void PB_Def::print(Printer pr) const {
  if (is_protected) return;
  pr.line(parent.bind_name() + "." + def + "(\"" + name.py_name() + "\", &" + name.cpp_name() + ");");
}



PB_Meth::PB_Meth(cppast::cpp_member_function const& func, Name parent, Context ctx) : PB_Def(func.name(), parent, ctx) {
  auto& vi = func.virtual_info();

  is_virtual = cppast::is_virtual(vi);
  is_pure = cppast::is_pure(vi);
  is_override = cppast::is_overriding(vi);
  is_final = cppast::is_final(vi);
  is_const = cppast::is_const(func.cv_qualifier());
  is_deleted = func.body_kind() == cppast::cpp_function_body_kind::cpp_function_deleted;

  is_overload = false;
  is_static = false;

  ret_type = cppast::to_string(func.return_type());

  for (cppast::cpp_function_parameter const& param : func.parameters()) {
    params.push_back(ctx.to_string(param.type()));
  }

  if (is_static) def += "_static";
}

PB_Meth::PB_Meth(cppast::cpp_function const& func, Name parent, Context ctx) : PB_Def(func.name(), parent, ctx) {
  is_virtual = false;
  is_pure = false;
  is_override = false;
  is_final = false;
  is_const = false;
  is_deleted = func.body_kind() == cppast::cpp_function_body_kind::cpp_function_deleted;

  is_overload = false;
  is_static = true;

  ret_type = cppast::to_string(func.return_type());

  for (cppast::cpp_function_parameter const& param : func.parameters()) {
    params.push_back(ctx.to_string(param.type()));
  }

  if (is_static) def += "_static";
}

bool PB_Meth::panic() const {
  return std::any_of(params.cbegin(), params.cend(), [](auto const& k){ return k.find("&&") != std::string::npos; });
}

void PB_Meth::print(Printer pr) const {
  if (is_deleted) return;
  if (is_protected) return;
  if (panic()) pr += "//";

  std::string pyname = name.py_name();
  if (is_overload && is_static) pyname += "_static"; // overloading a method with both static and instance methods is not supported
  std::string start = parent.bind_name() + "." + def + "(\"" + pyname + "\", ";
  if (is_overload) {
    std::string cast = "py::overload_cast<" + str_params(params) + ">";
    if (is_const) pr.line(start + cast + "(&" + name.cpp_name() + ", py::const_));");
    else pr.line(start + cast + "(&" + name.cpp_name() + "));");
  } else {
    pr.line(start + "&" + name.cpp_name() + ");");
  }
}

bool PB_Meth::needs_trampoline() const {
  return (is_virtual || is_override) && !is_final && !is_deleted;
}

void PB_Meth::print_trampoline(Printer pr) const {
  if (!needs_trampoline()) return;
  if (is_protected) return;
  if (panic()) pr += "//";

  std::string decl = ret_type + " " + name.cpp_simple_name() + "(";
  for (unsigned k = 0; k < params.size(); k++) {
    decl += (k ? ", " : "") + params[k] + " arg_" + std::to_string(k);
  }
  decl += ")";
  if (is_const) decl += " const";
  decl += " override";

  pr.line(decl + " {");
  if (is_pure) pr.line("  PYBIND11_OVERRIDE_PURE(");
  else pr.line("  PYBIND11_OVERRIDE(");
  pr.line("    /* return type:   */ " + ret_type);
  pr.line("  , /* parent class:  */ " + parent.cpp_name());
  pr.line("  , /* function name: */ " + name.cpp_simple_name());
  if (params.empty()) {
    pr.line("    ,");
  } else {
    pr.line("    /* arguments: */");
    for (unsigned k = 0; k < params.size(); k++) {
      pr.line("    , arg_" + std::to_string(k));
    }
  }
  pr.line("  );");
  pr.line("}");
  pr.line();
}

bool PB_Meth::same_sig(PB_Meth const& other) const {
  return ret_type == other.ret_type && params == other.params && name.cpp_simple_name() == other.name.cpp_simple_name();
}



PB_Cons::PB_Cons(Name parent) : parent(parent) {}

PB_Cons::PB_Cons(cppast::cpp_constructor const& cons, Name parent, Context ctx) : parent(parent), is_protected(ctx.is_protected()) {
  is_deleted = cons.body_kind() == cppast::cpp_function_body_kind::cpp_function_deleted;

  for (cppast::cpp_function_parameter const& param : cons.parameters()) {
    params.push_back(ctx.to_string(param.type()));
  }
}

bool PB_Cons::panic() const {
  return std::any_of(params.cbegin(), params.cend(), [](auto const& k){ return k.find("&&") != std::string::npos; });
}

void PB_Cons::print(Printer pr) const {
  if (is_deleted) return;
  if (is_protected) return;
  if (panic()) pr += "//";
  pr.line(parent.bind_name() + ".def(py::init<" + str_params(params) + ">());");
}

std::string location(cppast::cpp_entity const& entity) {
  if (entity.parent().has_value()) {
    auto const& parent = entity.parent().value();
    if (parent.kind() == cppast::cpp_entity_kind::class_t
        || parent.kind() == cppast::cpp_entity_kind::namespace_t)
      return location(parent) + parent.name() + "::";
    else return location(parent);
  }
  return "";
}

PB_Class::PB_Class(cppast::cpp_class const& cl, Name name, Name parent, Context ctx) : name(name), parent(parent) {
  print_debug(cl.name());

  is_final = cl.is_final();

  for (cppast::cpp_base_class const& base : cl.bases()) {
    bases.push_back(base.name());

    inherit(base, ctx);
  }
  print_debug(cl.name() + " inherit OK");

  cppast::cpp_access_specifier_kind access = cppast::cpp_access_specifier_kind::cpp_private;
  if (cl.class_kind() == cppast::cpp_class_kind::struct_t) access = cppast::cpp_access_specifier_kind::cpp_public;
  for (cppast::cpp_entity const& entity : cl) {
    if (entity.kind() == cppast::cpp_entity_kind::access_specifier_t) {
      access = dynamic_cast<cppast::cpp_access_specifier const&>(entity).access_specifier();
    } else if (access != cppast::cpp_access_specifier_kind::cpp_private) {
      process(entity, Context(ctx, access));
    }
  }
  print_debug(cl.name() + " OK");
}

PB_Class::PB_Class(cppast::cpp_class const& cl, Name parent, Context ctx) : PB_Class(cl, parent + cl.name(), parent, ctx) {
}

PB_Class::PB_Class(cppast::cpp_class_template_specialization const& cts, Name parent, Context ctx)
  : PB_Class(
      dynamic_cast<cppast::cpp_class_template const&>(cts.primary_template().get(ctx.idx)[0].get()).class_(),
      parent + (cts.name() + "<" + cts.unexposed_arguments().as_string() + ">"),
      parent,
      Context(ctx, cts))
{}

void PB_Class::inherit(cppast::cpp_base_class const& base, Context ctx) {
  if (!get_class_or_typedef(ctx.idx, base).has_value()) print_warn("yo wtf #2");

  // Name() is bad
  // Context should be updated with templates arguments
  PB_Class base_class = PB_Class(get_class(ctx.idx, base).value(), Name(), ctx);
  for (auto k : base_class.mems) {
    k.change_parent(name);
    add(k);
  }
  for (auto k : base_class.meths) {
    k.change_parent(name);
    add(k);
  }
  //cls.merge(base_class.cls);
}

void PB_Class::merge(PB_Class const& other) {
  if (other.bases.size() > bases.size()) bases = other.bases;
  for (auto const& k : other.mems) mems.push_back(k);
  for (auto const& k : other.meths) meths.push_back(k);
  for (auto const& k : other.conss) conss.push_back(k);
  cls.merge(other.cls);
}

void PB_Class::add(PB_Def def) { mems.push_back(def); }

void PB_Class::add(PB_Meth meth) {
  std::erase_if(meths, [&](auto const& k) { return meth.same_sig(k); });
  for (auto& k : meths) {
    if (k.name.cpp_simple_name() == meth.name.cpp_simple_name()) {
      k.is_overload = true;
      meth.is_overload = true;
    }
  }
  meths.push_back(meth);
}

void PB_Class::add(PB_Cons cons) { conss.push_back(cons); }
void PB_Class::add(PB_Class cl) { cls.add(cl); }

void PB_Class::process(cppast::cpp_entity const& entity, Context ctx) {
  if (entity.kind() == cppast::cpp_entity_kind::member_function_t) {
    add(PB_Meth(dynamic_cast<cppast::cpp_member_function const&>(entity), name, ctx));
  } else if (entity.kind() == cppast::cpp_entity_kind::function_t) {
    add(PB_Meth(dynamic_cast<cppast::cpp_function const&>(entity), name, ctx));
  } else if (entity.kind() == cppast::cpp_entity_kind::constructor_t) {
    add(PB_Cons(dynamic_cast<cppast::cpp_constructor const&>(entity), name, ctx));
  } else if (entity.kind() == cppast::cpp_entity_kind::member_variable_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_member_variable const&>(entity), name, ctx));
  } else if (entity.kind() == cppast::cpp_entity_kind::variable_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_variable const&>(entity), name, ctx));
  } else if (entity.kind() == cppast::cpp_entity_kind::class_t) {
    add(PB_Class(dynamic_cast<cppast::cpp_class const&>(entity), name, ctx));
  } else {
    print_warn("ignored: " + entity.name() + " (" + cppast::to_string(entity.kind()) + ")");
  }
}

void PB_Class::print_content(Printer pr) const {
  if (conss.empty()) {
    PB_Cons(name).print(pr);
  }
  for (auto const& cons : conss) {
    cons.print(pr);
  }
  for (auto const& mem : mems) {
    mem.print(pr);
  }
  for (auto const& meth : meths) {
    meth.print(pr);
  }
  cls.print(pr);
}

void PB_Class::print(Printer pr) const {
  if (panic()) pr += "//";
  std::string decl = "py::class_<" + name.cpp_name();
  for (std::string const& base : bases) decl += ", " + base;
  if (needs_trampoline()) decl += ", " + trampoline_name();
  decl += "> " + name.bind_name() + "(" + parent.bind_name() + ", \"" + name.py_name() + "\");";
  pr.line(decl + " {");
  print_content(pr+"  ");
  pr.line("}");
  pr.line();
}

bool PB_Class::panic() const {
  // abstract class that cannot be overrided for now
  return std::any_of(meths.cbegin(), meths.cend(), [](auto const& k){ return k.is_protected && k.is_pure; });
}

bool PB_Class::needs_trampoline() const {
  if (is_final) return false;
  return std::any_of(meths.cbegin(), meths.cend(), [](auto const& k){ return k.needs_trampoline(); });
}

std::string PB_Class::trampoline_name() const {
  return "Tr"+name.bind_name();
}

void PB_Class::print_trampoline(Printer pr) const {
  if (panic()) pr += "//";
  if (!needs_trampoline()) return;

  pr.line("struct " + trampoline_name() + " : public " + name.cpp_name() + " {");
  Printer pr2 = pr+"  ";
  pr2.line("using " + name.cpp_name() + "::" + name.cpp_simple_name() + ";");
  pr2.line();
  for (auto const& meth : meths) {
    meth.print_trampoline(pr2);
  }
  pr.line("};");
  pr.line();
}



void ClassCollection::add(PB_Class const& x) {
  std::string name = x.name.cpp_simple_name();
  if (M_data.contains(name)) {
    M_data.at(name).merge(x);
  } else {
    M_data.insert({name, x});
  }
}

void ClassCollection::merge(ClassCollection const& other) {
  for (auto const& k : other.M_data) {
    add(k.second);
  }
}

std::vector<PB_Class> ClassCollection::order() const {
  std::vector<unsigned> rem, ret;
  std::set<std::string> waiting;
  std::vector<PB_Class> vec_data;

  for (auto const& x : M_data) {
    vec_data.push_back(x.second);
  }

  for (unsigned k = 0; k < vec_data.size(); k++) {
    rem.push_back(k);
    waiting.insert(vec_data[k].name.cpp_simple_name());
  }

  while (!rem.empty()) {
    std::vector<unsigned> next;
    bool none = true;

    for (unsigned k : rem) {
      bool is_ok = true;
      for (std::string const& base : vec_data[k].bases) {
        if (waiting.contains(base)) {
          is_ok = false;
          break;
        }
      }
      if (is_ok) {
        ret.push_back(k);
        waiting.erase(vec_data[k].name.cpp_simple_name());
        none = false;
      } else {
        next.push_back(k);
      }
    }

    rem = next;

    if (none) {
      for (unsigned k : rem) {
        ret.push_back(k);
        print_warn("missing parent(s) for "+vec_data[k].name.cpp_name());
      }
      break;
    }
  }

  std::vector<PB_Class> ret_data;

  for (unsigned k : ret) {
    ret_data.push_back(vec_data[k]);
  }

  return ret_data;
}

void ClassCollection::print(Printer pr) const {
  for (auto const& k : order()) {
    k.print(pr);
  }
}

void ClassCollection::print_trampolines(Printer pr) const {
  for (auto const& k : order()) {
    k.print_trampoline(pr);
  }
}



void SubModCollection::add(PB_SubModule const& m) {
  std::string name = m.module_name.cpp_simple_name();
  if (M_data.contains(name)) {
    M_data.at(name).merge(m);
  } else {
    M_data.insert({name, m});
  }
}

void SubModCollection::merge(SubModCollection const& other) {
  for (auto const& k : other.M_data) {
    add(k.second);
  }
}

void SubModCollection::print(Printer pr) const {
  for (auto const& k : M_data) {
    k.second.print(pr);
  }
}

void SubModCollection::print_prelude(Printer pr) const {
  for (auto const& k : M_data) {
    k.second.print_prelude(pr);
  }
}



PB_Module::PB_Module(std::string module_name) : module_name(module_name) {}
PB_Module::PB_Module(std::string module_name, Context ctx) : module_name(module_name) {}

void PB_Module::print_content(Printer pr) const {
  mods.print(pr);

  cls.print(pr);

  for (auto const& def : defs) {
    def.print(pr);
  }
}

void PB_Module::print_prelude_content(Printer pr) const {
  mods.print_prelude(pr);

  cls.print_trampolines(pr);
}

void PB_Module::add(PB_SubModule mod) { mods.add(mod); }
void PB_Module::add(PB_Def def) { defs.push_back(def); }
void PB_Module::add(PB_Class cl) { cls.add(cl); }

void PB_Module::process(cppast::cpp_entity const& entity, Context ctx) {
  if (entity.kind() == cppast::cpp_entity_kind::function_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_function const&>(entity), module_name, ctx));
  } else if (entity.kind() == cppast::cpp_entity_kind::namespace_t) {
    add(PB_SubModule(dynamic_cast<cppast::cpp_namespace const&>(entity), module_name, ctx));
  } else if (entity.kind() == cppast::cpp_entity_kind::class_t) {
    add(PB_Class(dynamic_cast<cppast::cpp_class const&>(entity), module_name, ctx));
  } else if (entity.kind() == cppast::cpp_entity_kind::class_template_specialization_t) {
    auto const& tcl = dynamic_cast<cppast::cpp_class_template_specialization const&>(entity);
    print_warn(std::string("#")+std::to_string(tcl.is_full_specialization()));
    print_warn(std::string("#")+tcl.unexposed_arguments().as_string());
    auto const& cl2 = dynamic_cast<cppast::cpp_class_template const&>(tcl.primary_template().get(ctx.idx)[0].get());
    print_warn(std::string("#")+cl2.name());
    print_warn(std::string("#")+to_string(cl2.kind()));
    //print_warn(std::string("/")+to_string(cl2.name()));
    for (auto const& k : cl2.parameters()) {
      print_warn(std::string(">"+k.name()));
    }
    /*add(PB_Class(cl2.class_(), module_name, ctx));
    */
    add(PB_Class(tcl, module_name, ctx));
  //} else if (entity.kind() == cppast::cpp_entity_kind::variable_t) {
    // TODO: does not work, needs alternative solution. (setters and getters?)
    //add(PB_Def(dynamic_cast<cppast::cpp_variable const&>(entity), module_name));
  } else {
    print_warn("ignored: " + entity.name() + " (" + cppast::to_string(entity.kind()) + ")");
  }
}

void PB_Module::merge(PB_Module const& other) {
  mods.merge(other.mods);
  for (auto const& k : other.defs) defs.push_back(k);
  cls.merge(other.cls);
}



PB_SubModule::PB_SubModule(cppast::cpp_namespace const& ns, Name parent, Context ctx) : PB_Module(ns.name(), ctx), parent(parent) {
  for (cppast::cpp_entity const& entity : ns) {
    process(entity, ctx);
  }
}

void PB_SubModule::print_prelude(Printer pr) const {
  pr.line("namespace " + module_name.cpp_name() + " {");
  print_prelude_content(pr+"  ");
  pr.line("}");
}

void PB_SubModule::print(Printer pr) const {
  pr.line("py::module " + module_name.bind_name() + " = " + parent.bind_name() + ".def_submodule(\"" + module_name.py_name() + "\"); {");
  pr.line("  using namespace " + module_name.cpp_name() + ";");
  print_content(pr+"  ");
  pr.line("}");
  pr.line();
}

void PB_SubModule::merge(PB_SubModule const& other) {
  PB_Module::merge(other);
}



void PB_RootModule::print_module(Printer pr) const {
  pr.line("PYBIND11_MODULE(" + lib_name + ", " + module_name.bind_name() + ") {");
  print_content(pr+"  ");
  pr.line("}");
  pr.line();
}



PB_RootModule::PB_RootModule(std::string lib_name) : PB_Module("m"), lib_name(lib_name) {}

PB_RootModule::PB_RootModule(cppast::cpp_file const& file, std::string lib_name, Context ctx) : PB_Module("m", ctx), lib_name(lib_name) {
  includes.push_back(file.name());

  for (cppast::cpp_entity const& entity : file) {
    process(entity, ctx);
  }
}

void PB_RootModule::print_prelude(Printer pr) const {
  pr.line("#include <pybind11/pybind11.h>");
  pr.line("#include <pybind11/stl.h>");
  pr.line("namespace py = pybind11;");
  pr.line();

  for (std::string const& path : includes) {
    pr.line("#include \"" + path + "\"");
  }
  pr.line();

  print_prelude_content(pr);
  pr.line();
}

void PB_RootModule::print_file(Printer pr) const {
  print_prelude(pr);
  print_module(pr);
}

void PB_RootModule::merge(PB_RootModule const& other) {
  for (auto const& k : other.includes) includes.push_back(k);
  PB_Module::merge(other);
}



void process_file(std::ostream& out, cppast::cpp_file const& file, cppast::cpp_entity_index const& idx) {
  PB_RootModule(file, "example", Context(idx)).print_file(Printer(out, ""));
}
