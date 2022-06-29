#pragma once

#include <iostream>

#include <cppast/cpp_entity_kind.hpp>
#include <cppast/cpp_forward_declarable.hpp>
#include <cppast/cpp_namespace.hpp>
#include <cppast/cpp_function.hpp>
#include <cppast/cpp_member_function.hpp>
#include <cppast/cpp_member_variable.hpp>
#include <cppast/cpp_variable.hpp>
#include <cppast/cpp_class.hpp>
#include <cppast/cpp_file.hpp>

void print_warn(const std::string& msg);



struct Printer {
  std::ostream& out;
  std::string prefix;

  Printer(std::ostream& out, std::string prefix);

  Printer operator+(std::string str);

  void line();

  void line(std::string str);
};

class Name {
  std::string name;
  bool auto_scope;
  std::string scope;

 public:
  explicit Name(std::string name = "", std::string scope = "", bool auto_scope = true);

  std::string cpp_name() const;
  std::string self_scope() const;
  std::string as_scope() const;
  std::string bind_name() const; // TODO prevent name collisions
  std::string py_name() const;

  Name operator+(std::string son) const;
};

struct PB_Def {
  Name name;
  Name parent;
  std::string def;
  
  PB_Def(std::string name, Name parent);

  PB_Def(cppast::cpp_function const& func, Name parent);
  PB_Def(cppast::cpp_member_function const& func, Name parent);
  PB_Def(cppast::cpp_member_variable const& var, Name parent);
  PB_Def(cppast::cpp_variable const& var, Name parent);

  void print(Printer pr) const;
};

struct PB_Cons {
  std::vector<std::string> params;
  Name parent;

  PB_Cons(Name parent);
  PB_Cons(cppast::cpp_constructor const& cons, Name parent);

  std::string str_params() const;

  void print(Printer pr) const;
};

struct PB_Class {
  Name name;
  Name parent;

  std::vector<PB_Def> meths;
  std::vector<PB_Cons> conss;
  std::vector<PB_Class> cls;

  PB_Class(cppast::cpp_class const& cl, Name parent);

  void add(PB_Def meth);
  void add(PB_Cons cons);
  void add(PB_Class cl);

  void process(cppast::cpp_entity const& entity);

  void print_content(Printer pr) const;

  void print(Printer pr) const;
};

struct PB_SubModule;

struct PB_Module {
  Name module_name;
  std::vector<PB_SubModule> mods;
  std::vector<PB_Def> defs;
  std::vector<PB_Class> cls;

  PB_Module(std::string module_name);

  void print_content(Printer pr) const;

  void add(PB_SubModule mod);
  void add(PB_Def def);
  void add(PB_Class cl);

  void process(cppast::cpp_entity const& entity);
};

struct PB_SubModule : PB_Module {
  Name parent;

  PB_SubModule(cppast::cpp_namespace const& ns, Name parent);

  void print(Printer pr) const;
};

struct PB_RootModule : PB_Module {
  std::string lib_name;

  PB_RootModule(std::string lib_name);

  void print(Printer pr) const;
};

struct PB_File {
  std::vector<std::string> includes;

  PB_RootModule mod;

  PB_File(std::string lib_name);

  PB_File(cppast::cpp_file const& file, std::string lib_name);

  void print(std::ostream& out) const;
};

void process_file(std::ostream& out, cppast::cpp_file const& file);
