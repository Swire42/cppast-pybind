#pragma once

#include <iostream>

#include <cppast/cpp_entity_kind.hpp>
#include <cppast/cpp_forward_declarable.hpp>
#include <cppast/cpp_namespace.hpp>
#include <cppast/cpp_function.hpp>
#include <cppast/cpp_member_function.hpp>
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

struct PB_Def {
  std::string name;
  
  PB_Def(std::string name);

  PB_Def(cppast::cpp_function const& func);

  virtual void print(Printer pr, std::string const& super_name) const;
};

struct PB_Meth : PB_Def {
  PB_Meth(cppast::cpp_member_function const& func);

  void print(Printer pr, std::string const& super_name) const override;
};

struct PB_Cons {
  std::vector<std::string> params;

  PB_Cons(cppast::cpp_constructor const& cons);

  std::string str_params() const;

  void print(Printer pr, std::string const& super_name) const;
};

struct PB_Class {
  std::string name;

  std::vector<PB_Meth> meths;
  std::vector<PB_Cons> conss;

  PB_Class(std::string name);

  void add(PB_Meth meth);
  void add(PB_Cons cons);

  void process(cppast::cpp_entity const& entity);

  PB_Class(cppast::cpp_class const& cl);

  void print_content(Printer pr) const;

  void print(Printer pr, std::string const& module_name) const;
};

struct PB_SubModule;

struct PB_Module {
  std::string module_name;
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
  PB_SubModule(std::string module_name);

  PB_SubModule(cppast::cpp_namespace const& ns);

  void print(Printer pr, std::string super) const;
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
