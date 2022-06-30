#include "pb.hpp"

#include <algorithm>

void print_warn(const std::string& msg)
{
  std::cerr << "\033[1;33m" << msg << "\033[0m" << '\n';
}



Printer::Printer(std::ostream& out, std::string prefix) : out(out), prefix(prefix) {}

Printer Printer::operator+(std::string str) {
  return Printer(out, prefix+str);
}

void Printer::line() {
  out << std::endl;
}

void Printer::line(std::string str) {
  out << prefix << str << std::endl;
}



Name::Name(std::string name, std::string scope, bool auto_scope) : name(name), scope(scope), auto_scope(auto_scope) {}

std::string Name::cpp_simple_name() const {
  return name;
}

std::string Name::cpp_name() const {
  return scope + name;
}

std::string Name::self_scope() const {
  return scope;
}

std::string Name::as_scope() const {
  if (auto_scope) return self_scope();
  return scope + name + "::";
}

std::string Name::bind_name() const {
  return "PB__" + name;
}

std::string Name::py_name() const {
  return name;
}

Name Name::operator+(std::string son) const {
  return Name(son, as_scope(), false);
}



PB_Def::PB_Def(std::string name, Name parent) : name(parent + name), parent(parent), def("def") {}

PB_Def::PB_Def(cppast::cpp_function const& func, Name parent) : PB_Def(func.name(), parent) {}

PB_Def::PB_Def(cppast::cpp_member_variable const& var, Name parent) : PB_Def(var.name(), parent) {
  if (var.type().kind() == cppast::cpp_type_kind::cv_qualified_t
      && is_const(dynamic_cast<cppast::cpp_cv_qualified_type const&>(var.type()).cv_qualifier())) {
    def = "def_readonly";
  } else {
    def = "def_readwrite";
  }
}

PB_Def::PB_Def(cppast::cpp_variable const& var, Name parent) : PB_Def(var.name(), parent) {
  bool is_static = false;
  bool is_writable = true;

  if (var.type().kind() == cppast::cpp_type_kind::cv_qualified_t
      && is_const(dynamic_cast<cppast::cpp_cv_qualified_type const&>(var.type()).cv_qualifier())) {
    is_writable = false;
  }

  if (cppast::is_static(var.storage_class())) is_static = true;

  if (is_writable) {
    def = is_static ? "def_readwrite_static" : "def_readwrite";
  } else {
    def = is_static ? "def_readonly_static" : "def_readonly";
  }
}

void PB_Def::print(Printer pr) const {
  pr.line(parent.bind_name() + "." + def + "(\"" + name.py_name() + "\", &" + name.cpp_name() + ");");
}



PB_Meth::PB_Meth(cppast::cpp_member_function const& func, Name parent) : PB_Def(func.name(), parent) {
  auto& vi = func.virtual_info();

  is_virtual = cppast::is_virtual(vi);
  is_pure = cppast::is_pure(vi);
  is_override = cppast::is_overriding(vi);
  is_final = cppast::is_final(vi);

  ret_type = cppast::to_string(func.return_type());

  for (cppast::cpp_function_parameter const& param : func.parameters()) {
    params.push_back(cppast::to_string(param.type()));
  }
}

bool PB_Meth::needs_trampoline() const {
  return (is_virtual || is_override) && !is_final;
}

void PB_Meth::print_trampoline(Printer pr) const {
  if (!needs_trampoline()) return;

  std::string decl = ret_type + " " + name.cpp_simple_name() + "(";
  for (unsigned k = 0; k < params.size(); k++) {
    decl += (k ? ", " : "") + params[k] + " arg_" + std::to_string(k);
  }
  decl += ") override";

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

PB_Cons::PB_Cons(cppast::cpp_constructor const& cons, Name parent) : parent(parent) {
  for (cppast::cpp_function_parameter const& param : cons.parameters()) {
    params.push_back(cppast::to_string(param.type()));
  }
}

std::string PB_Cons::str_params() const {
  std::string ret = "";
  for (unsigned k = 0; k < params.size(); k++) {
    ret += params[k];
    if (k + 1 < params.size()) ret += ", ";
  }
  return ret;
}

void PB_Cons::print(Printer pr) const {
  pr.line(parent.bind_name() + ".def(py::init<" + str_params() + ">());");
}



PB_Class::PB_Class(cppast::cpp_class const& cl, Name parent, cppast::cpp_entity_index const& idx) : name(parent + cl.name()), parent(parent) {
  for (cppast::cpp_base_class const& base : cl.bases()) {
    bases.push_back(base.name());

    inherit(base, idx);
  }

  for (cppast::cpp_entity const& entity : cl) {
    process(entity, idx);
  }
}

void PB_Class::inherit(cppast::cpp_base_class const& base, cppast::cpp_entity_index const& idx) {
  PB_Class base_class = PB_Class(get_class(idx, base).value(), Name(), idx); // Todo Name() is bad
  for (auto const& k : base_class.mems) add(k);
  for (auto const& k : base_class.meths) add(k);
  for (auto const& k : base_class.cls) add(k);
}

void PB_Class::add(PB_Def def) { mems.push_back(def); }

void PB_Class::add(PB_Meth meth) {
  std::erase_if(meths, [&](auto const& k) { return meth.same_sig(k); });
  meths.push_back(meth);
}

void PB_Class::add(PB_Cons cons) { conss.push_back(cons); }
void PB_Class::add(PB_Class cl) { cls.push_back(cl); }

void PB_Class::process(cppast::cpp_entity const& entity, cppast::cpp_entity_index const& idx) {
  if (entity.kind() == cppast::cpp_entity_kind::member_function_t) {
    add(PB_Meth(dynamic_cast<cppast::cpp_member_function const&>(entity), name));
  } else if (entity.kind() == cppast::cpp_entity_kind::constructor_t) {
    add(PB_Cons(dynamic_cast<cppast::cpp_constructor const&>(entity), name));
  } else if (entity.kind() == cppast::cpp_entity_kind::member_variable_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_member_variable const&>(entity), name));
  } else if (entity.kind() == cppast::cpp_entity_kind::variable_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_variable const&>(entity), name));
  } else if (entity.kind() == cppast::cpp_entity_kind::class_t) {
    add(PB_Class(dynamic_cast<cppast::cpp_class const&>(entity), name, idx));
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
  for (auto const& cl : cls) {
    cl.print(pr);
  }
}

void PB_Class::print(Printer pr) const {
  std::string decl = "py::class_<" + name.cpp_name();
  for (std::string const& base : bases) decl += ", " + base;
  if (needs_trampoline()) decl += ", " + trampoline_name();
  decl += "> " + name.bind_name() + "(" + parent.bind_name() + ", \"" + name.py_name() + "\");";
  pr.line(decl + " {");
  print_content(pr+"  ");
  pr.line("}");
  pr.line();
}

bool PB_Class::needs_trampoline() const {
  return std::any_of(meths.cbegin(), meths.cend(), [](auto const& k){ return k.needs_trampoline(); });
}

std::string PB_Class::trampoline_name() const {
  return "Tr"+name.bind_name();
}

void PB_Class::print_trampoline(Printer pr) const {
  if (!needs_trampoline()) return;

  pr.line("struct " + trampoline_name() + " : public " + name.cpp_name() + " {");
  Printer pr2 = pr+"  ";
  pr2.line("using " + name.cpp_name() + "::" + name.cpp_simple_name() + ";");
  pr2.line();
  for (auto const& meth : meths) {
    // TODO: add inherited methods
    meth.print_trampoline(pr2);
  }
  pr.line("};");
  pr.line();
}



PB_Module::PB_Module(std::string module_name, cppast::cpp_entity_index const& idx) : module_name(module_name) {}

void PB_Module::print_content(Printer pr) const {
  for (auto const& mod : mods) {
    mod.print(pr);
  }

  for (auto const& cl : cls) {
    cl.print(pr);
  }

  for (auto const& def : defs) {
    def.print(pr);
  }
}

void PB_Module::print_prelude_content(Printer pr) const {
  for (auto const& mod : mods) {
    mod.print_prelude(pr);
  }

  for (auto const& cl : cls) {
    cl.print_trampoline(pr);
  }
}

void PB_Module::add(PB_SubModule mod) { mods.push_back(mod); }
void PB_Module::add(PB_Def def) { defs.push_back(def); }
void PB_Module::add(PB_Class cl) { cls.push_back(cl); }

void PB_Module::process(cppast::cpp_entity const& entity, cppast::cpp_entity_index const& idx) {
  if (entity.kind() == cppast::cpp_entity_kind::function_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_function const&>(entity), module_name));
  } else if (entity.kind() == cppast::cpp_entity_kind::namespace_t) {
    add(PB_SubModule(dynamic_cast<cppast::cpp_namespace const&>(entity), module_name, idx));
  } else if (entity.kind() == cppast::cpp_entity_kind::class_t) {
    add(PB_Class(dynamic_cast<cppast::cpp_class const&>(entity), module_name, idx));
  //} else if (entity.kind() == cppast::cpp_entity_kind::variable_t) {
    // TODO: does not work, needs alternative solution. (setters and getters?)
    //add(PB_Def(dynamic_cast<cppast::cpp_variable const&>(entity), module_name));
  } else {
    print_warn("ignored: " + entity.name() + " (" + cppast::to_string(entity.kind()) + ")");
  }
}



PB_SubModule::PB_SubModule(cppast::cpp_namespace const& ns, Name parent, cppast::cpp_entity_index const& idx) : PB_Module(ns.name(), idx), parent(parent) {
  for (cppast::cpp_entity const& entity : ns) {
    process(entity, idx);
  }
}

void PB_SubModule::print_prelude(Printer pr) const {
  pr.line("namespace " + module_name.bind_name() + " {");
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



void PB_RootModule::print_module(Printer pr) const {
  pr.line("PYBIND11_MODULE(" + lib_name + ", " + module_name.bind_name() + ") {");
  print_content(pr+"  ");
  pr.line("}");
  pr.line();
}



PB_RootModule::PB_RootModule(cppast::cpp_file const& file, std::string lib_name, cppast::cpp_entity_index const& idx) : PB_Module("m", idx), lib_name(lib_name) {
  includes.push_back(file.name());

  for (cppast::cpp_entity const& entity : file) {
    process(entity, idx);
  }
}

void PB_RootModule::print_prelude(Printer pr) const {
  pr.line("#include <pybind11/pybind11.h>");
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



void process_file(std::ostream& out, cppast::cpp_file const& file, cppast::cpp_entity_index const& idx) {
  PB_RootModule(file, "example", idx).print_file(Printer(out, ""));
}
