#include "pb.hpp"

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
  return name;
}

std::string Name::py_name() const {
  return name;
}

Name Name::operator+(std::string son) const {
  return Name(son, as_scope(), false);
}



PB_Def::PB_Def(std::string name, Name parent) : name(parent + name), parent(parent), def("def") {}

PB_Def::PB_Def(cppast::cpp_function const& func, Name parent) : PB_Def(func.name(), parent) {}

PB_Def::PB_Def(cppast::cpp_member_function const& func, Name parent) : PB_Def(func.name(), parent) {}

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



PB_Class::PB_Class(cppast::cpp_class const& cl, Name parent) : name(parent + cl.name()), parent(parent) {
  for (cppast::cpp_entity const& entity : cl) {
    process(entity);
  }
}

void PB_Class::add(PB_Def def) { meths.push_back(def); }
void PB_Class::add(PB_Cons cons) { conss.push_back(cons); }
void PB_Class::add(PB_Class cl) { cls.push_back(cl); }

void PB_Class::process(cppast::cpp_entity const& entity) {
  if (entity.kind() == cppast::cpp_entity_kind::member_function_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_member_function const&>(entity), name));
  } else if (entity.kind() == cppast::cpp_entity_kind::constructor_t) {
    add(PB_Cons(dynamic_cast<cppast::cpp_constructor const&>(entity), name));
  } else if (entity.kind() == cppast::cpp_entity_kind::member_variable_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_member_variable const&>(entity), name));
  } else if (entity.kind() == cppast::cpp_entity_kind::variable_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_variable const&>(entity), name));
  } else if (entity.kind() == cppast::cpp_entity_kind::class_t) {
    add(PB_Class(dynamic_cast<cppast::cpp_class const&>(entity), name));
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
  for (auto const& meth : meths) {
    meth.print(pr);
  }
  for (auto const& cl : cls) {
    cl.print(pr);
  }
}

void PB_Class::print(Printer pr) const {
  pr.line("py::class_<" + name.cpp_name() + "> " + name.bind_name() + "(" + parent.bind_name() + ", \"" + name.py_name() + "\"); {");
  print_content(pr+"  ");
  pr.line("}");
  pr.line();
}



PB_Module::PB_Module(std::string module_name) : module_name(module_name) {}

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

void PB_Module::add(PB_SubModule mod) { mods.push_back(mod); }
void PB_Module::add(PB_Def def) { defs.push_back(def); }
void PB_Module::add(PB_Class cl) { cls.push_back(cl); }

void PB_Module::process(cppast::cpp_entity const& entity) {
  if (entity.kind() == cppast::cpp_entity_kind::function_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_function const&>(entity), module_name));
  } else if (entity.kind() == cppast::cpp_entity_kind::namespace_t) {
    add(PB_SubModule(dynamic_cast<cppast::cpp_namespace const&>(entity), module_name));
  } else if (entity.kind() == cppast::cpp_entity_kind::class_t) {
    add(PB_Class(dynamic_cast<cppast::cpp_class const&>(entity), module_name));
  //} else if (entity.kind() == cppast::cpp_entity_kind::variable_t) {
    // TODO: does not work
    //add(PB_Def(dynamic_cast<cppast::cpp_variable const&>(entity), module_name));
  } else {
    print_warn("ignored: " + entity.name() + " (" + cppast::to_string(entity.kind()) + ")");
  }
}



PB_SubModule::PB_SubModule(cppast::cpp_namespace const& ns, Name parent) : PB_Module(ns.name()), parent(parent) {
  for (cppast::cpp_entity const& entity : ns) {
    process(entity);
  }
}

void PB_SubModule::print(Printer pr) const {
  pr.line("py::module " + module_name.bind_name() + " = " + parent.bind_name() + ".def_submodule(\"" + module_name.py_name() + "\"); {");
  pr.line("  using namespace " + module_name.bind_name() + ";");
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



PB_RootModule::PB_RootModule(cppast::cpp_file const& file, std::string lib_name) : PB_Module("m"), lib_name(lib_name) {
  includes.push_back(file.name());

  for (cppast::cpp_entity const& entity : file) {
    process(entity);
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
}

void PB_RootModule::print_file(Printer pr) const {
  print_prelude(pr);
  print_module(pr);
}



void process_file(std::ostream& out, cppast::cpp_file const& file) {
  PB_RootModule(file, "example").print_file(Printer(out, ""));
}
