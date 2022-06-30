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

  std::string cpp_simple_name() const;
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
  PB_Def(cppast::cpp_member_variable const& var, Name parent);
  PB_Def(cppast::cpp_variable const& var, Name parent);

  void print(Printer pr) const;
};

struct PB_Meth : PB_Def {
  std::string ret_type;
  std::vector<std::string> params;
  bool is_virtual, is_pure, is_override, is_final;

  PB_Meth(cppast::cpp_member_function const& func, Name parent);

  bool needs_trampoline() const;
  void print_trampoline(Printer pr) const;

  bool same_sig(PB_Meth const& other) const;
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

  std::vector<std::string> bases;

  std::vector<PB_Def> mems;
  std::vector<PB_Meth> meths;
  std::vector<PB_Cons> conss;
  std::vector<PB_Class> cls;

  PB_Class(cppast::cpp_class const& cl, Name parent, cppast::cpp_entity_index const& idx);

  void inherit(cppast::cpp_base_class const& base, cppast::cpp_entity_index const& idx);

  void add(PB_Def def);
  void add(PB_Meth meth);
  void add(PB_Cons cons);
  void add(PB_Class cl);

  void process(cppast::cpp_entity const& entity, cppast::cpp_entity_index const& idx);

  void print_content(Printer pr) const;
  void print(Printer pr) const;

  bool needs_trampoline() const;
  std::string trampoline_name() const;
  void print_trampoline(Printer pr) const;
};

struct PB_SubModule;

struct PB_Module {
  Name module_name;
  std::vector<PB_SubModule> mods;
  std::vector<PB_Def> defs;
  std::vector<PB_Class> cls;

  PB_Module(std::string module_name, cppast::cpp_entity_index const& idx);

  void print_prelude_content(Printer pr) const;
  void print_content(Printer pr) const;

  void add(PB_SubModule mod);
  void add(PB_Def def);
  void add(PB_Class cl);

  void process(cppast::cpp_entity const& entity, cppast::cpp_entity_index const& idx);
};

struct PB_SubModule : PB_Module {
  Name parent;

  PB_SubModule(cppast::cpp_namespace const& ns, Name parent, cppast::cpp_entity_index const& idx);

  void print(Printer pr) const;
  void print_prelude(Printer pr) const;
};

struct PB_RootModule : PB_Module {
  std::string lib_name;
  std::vector<std::string> includes;

  PB_RootModule(cppast::cpp_file const& file, std::string lib_name, cppast::cpp_entity_index const& idx);

  void print_prelude(Printer pr) const;
  void print_module(Printer pr) const;

  void print_file(Printer pr) const;
};

void process_file(std::ostream& out, cppast::cpp_file const& file, cppast::cpp_entity_index const& idx);
