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



PB_Def::PB_Def(std::string name) : name(name) {}

PB_Def::PB_Def(cppast::cpp_function const& func) : PB_Def(func.name()) {}

void PB_Def::print(Printer pr, std::string const& super_name) const {
  pr.line(super_name + ".def(\"" + name + "\", &" + name + ");");
}



PB_Meth::PB_Meth(cppast::cpp_member_function const& func) : PB_Def(func.name()) {}

void PB_Meth::print(Printer pr, std::string const& super_name) const {
  pr.line(super_name + ".def(\"" + name + "\", &" + super_name + "::" + name + ");");
}



PB_Cons::PB_Cons(cppast::cpp_constructor const& cons) {
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

void PB_Cons::print(Printer pr, std::string const& super_name) const {
  pr.line(super_name + ".def(py::init<" + str_params() + ">());");
}



PB_Class::PB_Class(std::string name) : name(name) {}

void PB_Class::add(PB_Meth meth) { meths.push_back(meth); }
void PB_Class::add(PB_Cons cons) { conss.push_back(cons); }

void PB_Class::process(cppast::cpp_entity const& entity) {
  if (entity.kind() == cppast::cpp_entity_kind::member_function_t) {
    add(PB_Meth(dynamic_cast<cppast::cpp_member_function const&>(entity)));
  } else if (entity.kind() == cppast::cpp_entity_kind::constructor_t) {
    add(PB_Cons(dynamic_cast<cppast::cpp_constructor const&>(entity)));
  } else {
    print_warn("ignored: " + entity.name() + " (" + cppast::to_string(entity.kind()) + ")");
  }
}

PB_Class::PB_Class(cppast::cpp_class const& cl) : PB_Class(cl.name()) {
  for (cppast::cpp_entity const& entity : cl) {
    process(entity);
  }
}

void PB_Class::print_content(Printer pr) const {
  for (auto const& cons : conss) {
    cons.print(pr, name);
  }
  for (auto const& meth : meths) {
    meth.print(pr, name);
  }
}

void PB_Class::print(Printer pr, std::string const& module_name) const {
  pr.line("py::class_<" + name + "> " + name + "(" + module_name + ", \"" + name + "\"); {");
  print_content(pr+"  ");
  pr.line("}");
  pr.line();
}



PB_Module::PB_Module(std::string module_name) : module_name(module_name) {}

void PB_Module::print_content(Printer pr) const {
  for (auto const& mod : mods) {
    mod.print(pr, module_name);
  }

  for (auto const& cl : cls) {
    cl.print(pr, module_name);
  }

  for (auto const& def : defs) {
    def.print(pr, module_name);
  }
}

void PB_Module::add(PB_SubModule mod) { mods.push_back(mod); }
void PB_Module::add(PB_Def def) { defs.push_back(def); }
void PB_Module::add(PB_Class cl) { cls.push_back(cl); }

void PB_Module::process(cppast::cpp_entity const& entity) {
  if (entity.kind() == cppast::cpp_entity_kind::function_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_function const&>(entity)));
  } else if (entity.kind() == cppast::cpp_entity_kind::namespace_t) {
    add(PB_SubModule(dynamic_cast<cppast::cpp_namespace const&>(entity)));
  } else if (entity.kind() == cppast::cpp_entity_kind::class_t) {
    add(PB_Class(dynamic_cast<cppast::cpp_class const&>(entity)));
  } else {
    print_warn("ignored: " + entity.name() + " (" + cppast::to_string(entity.kind()) + ")");
  }
}



PB_SubModule::PB_SubModule(std::string module_name) : PB_Module(module_name) {}

PB_SubModule::PB_SubModule(cppast::cpp_namespace const& ns) : PB_SubModule(ns.name()) {
  for (cppast::cpp_entity const& entity : ns) {
    process(entity);
  }
}

void PB_SubModule::print(Printer pr, std::string super) const {
  pr.line("py::module " + module_name + " = " + super + ".def_submodule(\"" + module_name + "\"); {");
  pr.line("  using namespace " + module_name + ";");
  print_content(pr+"  ");
  pr.line("}");
  pr.line();
}



PB_RootModule::PB_RootModule(std::string lib_name) : PB_Module("m"), lib_name(lib_name) {}

void PB_RootModule::print(Printer pr) const {
  pr.line("PYBIND11_MODULE(" + lib_name + ", " + module_name + ") {");
  print_content(pr+"  ");
  pr.line("}");
  pr.line();
}



PB_File::PB_File(std::string lib_name) : mod(lib_name) {}

PB_File::PB_File(cppast::cpp_file const& file, std::string lib_name) : PB_File(lib_name) {
  includes.push_back(file.name());

  for (cppast::cpp_entity const& entity : file) {
    mod.process(entity);
  }
}

void PB_File::print(std::ostream& out) const {
  out << "#include <pybind11/pybind11.h>\n";
  out << "namespace py = pybind11;\n";
  out << std::endl;

  for (std::string const& path : includes) {
    out << "#include \"" << path << "\"\n";
  }
  out << std::endl;

  mod.print(Printer(out, ""));
}



void process_file(std::ostream& out, cppast::cpp_file const& file) {
  PB_File(file, "example").print(out);
}
