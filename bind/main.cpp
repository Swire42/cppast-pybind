// Copyright (C) 2017-2022 Jonathan Müller and cppast contributors
// SPDX-License-Identifier: MIT

#include <iostream>

#include <cxxopts.hpp>

#include <cppast/code_generator.hpp>     // for generate_code()
#include <cppast/cpp_entity_kind.hpp>    // for the cpp_entity_kind definition
#include <cppast/cpp_forward_declarable.hpp> // for is_definition()
#include <cppast/cpp_namespace.hpp>      // for cpp_namespace
#include <cppast/cpp_function.hpp>      // for cpp_function
#include <cppast/cpp_member_function.hpp>      // for cpp_function
#include <cppast/libclang_parser.hpp> // for libclang_parser, libclang_compile_config, cpp_entity,...
#include <cppast/visitor.hpp>     // for visit()

// print help options
void print_help(const cxxopts::Options& options)
{
  std::cout << options.help({"", "compilation"}) << '\n';
}

// print error message
void print_error(const std::string& msg)
{
  std::cerr << "\033[1;31m" << msg << "\033[0m" << '\n';
}

struct Printer {
  std::ostream& out;
  std::string prefix;

  Printer(std::ostream& out, std::string prefix) : out(out), prefix(prefix) {}

  Printer operator+(std::string str) {
    return Printer(out, prefix+str);
  }

  void line() {
    out << std::endl;
  }

  void line(std::string str) {
    out << prefix << str << std::endl;
  }
};

struct PB_Def {
  std::string name;
  
  PB_Def(std::string name) : name(name) {}

  PB_Def(cppast::cpp_function const& func) : PB_Def(func.name()) {}

  virtual void print(Printer pr, std::string const& super_name) const {
    pr.line(super_name + ".def(\"" + name + "\", &" + name + ");");
  }
};

struct PB_Meth : PB_Def {
  PB_Meth(cppast::cpp_member_function const& func) : PB_Def(func.name()) {}

  void print(Printer pr, std::string const& super_name) const override {
    pr.line(super_name + ".def(\"" + name + "\", &" + super_name + "::" + name + ");");
  }
};

struct PB_Cons {
  std::vector<std::string> params;

  PB_Cons(cppast::cpp_constructor const& cons) {
    for (cppast::cpp_function_parameter const& param : cons.parameters()) {
      params.push_back(cppast::to_string(param.type()));
    }
  }

  std::string str_params() const {
    std::string ret = "";
    for (unsigned k = 0; k < params.size(); k++) {
      ret += params[k];
      if (k + 1 < params.size()) ret += ", ";
    }
    return ret;
  }

  void print(Printer pr, std::string const& super_name) const {
    pr.line(super_name + ".def(py::init<" + str_params() + ">());");
  }
};

struct PB_Class {
  std::string name;

  std::vector<PB_Meth> meths;
  std::vector<PB_Cons> conss;

  PB_Class(std::string name) : name(name) {}

  void add(PB_Meth meth) { meths.push_back(meth); }
  void add(PB_Cons cons) { conss.push_back(cons); }

  void process(cppast::cpp_entity const& entity) {
    if (entity.kind() == cppast::cpp_entity_kind::member_function_t) {
      add(PB_Meth(dynamic_cast<cppast::cpp_member_function const&>(entity)));
    } else if (entity.kind() == cppast::cpp_entity_kind::constructor_t) {
      add(PB_Cons(dynamic_cast<cppast::cpp_constructor const&>(entity)));
    } else {
      print_error("ignored: " + entity.name() + " (" + cppast::to_string(entity.kind()) + ")");
    }
  }

  PB_Class(cppast::cpp_class const& cl) : PB_Class(cl.name()) {
    for (cppast::cpp_entity const& entity : cl) {
      process(entity);
    }
  }

  void print_content(Printer pr) const {
    for (auto const& cons : conss) {
      cons.print(pr, name);
    }
    for (auto const& meth : meths) {
      meth.print(pr, name);
    }
  }

  void print(Printer pr, std::string const& module_name) const {
    pr.line("py::class_<" + name + "> " + name + "(" + module_name + ", \"" + name + "\"); {");
    print_content(pr+"  ");
    pr.line("}");
    pr.line();
  }
};

struct PB_SubModule;

struct PB_Module {
  std::string module_name;
  std::vector<PB_SubModule> mods;
  std::vector<PB_Def> defs;
  std::vector<PB_Class> cls;

  PB_Module(std::string module_name) : module_name(module_name) {}

  void print_content(Printer pr) const;

  void add(PB_SubModule mod);
  void add(PB_Def def) { defs.push_back(def); }
  void add(PB_Class cl) { cls.push_back(cl); }

  void process(cppast::cpp_entity const& entity);
};

struct PB_SubModule : PB_Module {
  PB_SubModule(std::string module_name) : PB_Module(module_name) {}

  PB_SubModule(cppast::cpp_namespace const& ns) : PB_SubModule(ns.name()) {
    for (cppast::cpp_entity const& entity : ns) {
      process(entity);
    }
  }

  void print(Printer pr, std::string super) const {
    pr.line("py::module " + module_name + " = " + super + ".def_submodule(\"" + module_name + "\"); {");
    pr.line("  using namespace " + module_name + ";");
    print_content(pr+"  ");
    pr.line("}");
    pr.line();
  }
};

struct PB_RootModule : PB_Module {
  std::string lib_name;

  PB_RootModule(std::string lib_name) : PB_Module("m"), lib_name(lib_name) {}

  void print(Printer pr) const {
    pr.line("PYBIND11_MODULE(" + lib_name + ", " + module_name + ") {");
    print_content(pr+"  ");
    pr.line("}");
    pr.line();
  }
};

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

struct PB_File {
  std::vector<std::string> includes;

  PB_RootModule mod;

  PB_File(std::string lib_name) : mod(lib_name) {}

  PB_File(cppast::cpp_file const& file, std::string lib_name) : PB_File(lib_name) {
    includes.push_back(file.name());

    for (cppast::cpp_entity const& entity : file) {
      mod.process(entity);
    }
  }

  void print(std::ostream& out) const {
    out << "#include <pybind11/pybind11.h>\n";
    out << "namespace py = pybind11;\n";
    out << std::endl;

    for (std::string const& path : includes) {
      out << "#include \"" << path << "\"\n";
    }
    out << std::endl;

    mod.print(Printer(out, ""));
  }
};

void PB_Module::process(cppast::cpp_entity const& entity) {
  if (entity.kind() == cppast::cpp_entity_kind::function_t) {
    add(PB_Def(dynamic_cast<cppast::cpp_function const&>(entity)));
  } else if (entity.kind() == cppast::cpp_entity_kind::namespace_t) {
    add(PB_SubModule(dynamic_cast<cppast::cpp_namespace const&>(entity)));
  } else if (entity.kind() == cppast::cpp_entity_kind::class_t) {
    add(PB_Class(dynamic_cast<cppast::cpp_class const&>(entity)));
  } else {
    print_error("ignored: " + entity.name() + " (" + cppast::to_string(entity.kind()) + ")");
  }
}

void process_file(std::ostream& out, cppast::cpp_file const& file) {
  PB_File(file, "example").print(out);
}

// parse a file
std::unique_ptr<cppast::cpp_file> parse_file(const cppast::libclang_compile_config& config,
                       const cppast::diagnostic_logger&     logger,
                       const std::string& filename, bool fatal_error)
{
  // the entity index is used to resolve cross references in the AST
  // we don't need that, so it will not be needed afterwards
  cppast::cpp_entity_index idx;
  // the parser is used to parse the entity
  // there can be multiple parser implementations
  cppast::libclang_parser parser(type_safe::ref(logger));
  // parse the file
  auto file = parser.parse(idx, filename, config);
  if (fatal_error && parser.error())
    return nullptr;
  return file;
}

int main(int argc, char* argv[])
try
{
  cxxopts::Options option_list("cppast",
                 "cppast - The commandline interface to the cppast library.\n");
  // clang-format off
  option_list.add_options()
    ("h,help", "display this help and exit")
    ("version", "display version information and exit")
    ("v,verbose", "be verbose when parsing")
    ("fatal_errors", "abort program when a parser error occurs, instead of doing error correction")
    ("file", "the file that is being parsed (last positional argument)",
     cxxopts::value<std::string>());
  option_list.add_options("compilation")
    ("database_dir", "set the directory where a 'compile_commands.json' file is located containing build information",
    cxxopts::value<std::string>())
    ("database_file", "set the file name whose configuration will be used regardless of the current file name",
    cxxopts::value<std::string>())
    ("std", "set the C++ standard (c++98, c++03, c++11, c++14, c++1z (experimental), c++17, c++2a, c++20)",
     cxxopts::value<std::string>()->default_value(cppast::to_string(cppast::cpp_standard::cpp_latest)))
    ("I,include_directory", "add directory to include search path",
     cxxopts::value<std::vector<std::string>>())
    ("D,macro_definition", "define a macro on the command line",
     cxxopts::value<std::vector<std::string>>())
    ("U,macro_undefinition", "undefine a macro on the command line",
     cxxopts::value<std::vector<std::string>>())
    ("f,feature", "enable a custom feature (-fXX flag)",
     cxxopts::value<std::vector<std::string>>())
    ("gnu_extensions", "enable GNU extensions (equivalent to -std=gnu++XX)")
    ("msvc_extensions", "enable MSVC extensions (equivalent to -fms-extensions)")
    ("msvc_compatibility", "enable MSVC compatibility (equivalent to -fms-compatibility)")
    ("fast_preprocessing", "enable fast preprocessing, be careful, this breaks if you e.g. redefine macros in the same file!")
    ("remove_comments_in_macro", "whether or not comments generated by macro are kept, enable if you run into errors");
  // clang-format on
  option_list.parse_positional("file");

  auto options = option_list.parse(argc, argv);
  if (options.count("help"))
    print_help(option_list);
  else if (options.count("version"))
  {
    std::cout << "cppast version " << CPPAST_VERSION_STRING << "\n";
    std::cout << "Copyright (C) Jonathan Müller 2017-2019 <jonathanmueller.dev@gmail.com>\n";
    std::cout << '\n';
    std::cout << "Using libclang version " << CPPAST_CLANG_VERSION_STRING << '\n';
  }
  else if (!options.count("file") || options["file"].as<std::string>().empty())
  {
    print_error("missing file argument");
    return 1;
  }
  else
  {
    // the compile config stores compilation flags
    cppast::libclang_compile_config config;
    if (options.count("database_dir"))
    {
      cppast::libclang_compilation_database database(
        options["database_dir"].as<std::string>());
      if (options.count("database_file"))
        config
          = cppast::libclang_compile_config(database,
                            options["database_file"].as<std::string>());
      else
        config
          = cppast::libclang_compile_config(database, options["file"].as<std::string>());
    }

    if (options.count("verbose"))
      config.write_preprocessed(true);

    if (options.count("fast_preprocessing"))
      config.fast_preprocessing(true);

    if (options.count("remove_comments_in_macro"))
      config.remove_comments_in_macro(true);

    if (options.count("include_directory"))
      for (auto& include : options["include_directory"].as<std::vector<std::string>>())
        config.add_include_dir(include);
    if (options.count("macro_definition"))
      for (auto& macro : options["macro_definition"].as<std::vector<std::string>>())
      {
        auto equal = macro.find('=');
        auto name  = macro.substr(0, equal);
        if (equal == std::string::npos)
          config.define_macro(std::move(name), "");
        else
        {
          auto def = macro.substr(equal + 1u);
          config.define_macro(std::move(name), std::move(def));
        }
      }
    if (options.count("macro_undefinition"))
      for (auto& name : options["macro_undefinition"].as<std::vector<std::string>>())
        config.undefine_macro(name);
    if (options.count("feature"))
      for (auto& name : options["feature"].as<std::vector<std::string>>())
        config.enable_feature(name);

    // the compile_flags are generic flags
    cppast::compile_flags flags;
    if (options.count("gnu_extensions"))
      flags |= cppast::compile_flag::gnu_extensions;
    if (options.count("msvc_extensions"))
      flags |= cppast::compile_flag::ms_extensions;
    if (options.count("msvc_compatibility"))
      flags |= cppast::compile_flag::ms_compatibility;

    if (options["std"].as<std::string>() == "c++98")
      config.set_flags(cppast::cpp_standard::cpp_98, flags);
    else if (options["std"].as<std::string>() == "c++03")
      config.set_flags(cppast::cpp_standard::cpp_03, flags);
    else if (options["std"].as<std::string>() == "c++11")
      config.set_flags(cppast::cpp_standard::cpp_11, flags);
    else if (options["std"].as<std::string>() == "c++14")
      config.set_flags(cppast::cpp_standard::cpp_14, flags);
    else if (options["std"].as<std::string>() == "c++1z")
      config.set_flags(cppast::cpp_standard::cpp_1z, flags);
    else if (options["std"].as<std::string>() == "c++17")
      config.set_flags(cppast::cpp_standard::cpp_17, flags);
    else if (options["std"].as<std::string>() == "c++2a")
      config.set_flags(cppast::cpp_standard::cpp_2a, flags);
    else if (options["std"].as<std::string>() == "c++20")
      config.set_flags(cppast::cpp_standard::cpp_20, flags);
    else
    {
      print_error("invalid value '" + options["std"].as<std::string>() + "' for std flag");
      return 1;
    }

    // the logger is used to print diagnostics
    cppast::stderr_diagnostic_logger logger;
    if (options.count("verbose"))
      logger.set_verbose(true);

    auto file = parse_file(config, logger, options["file"].as<std::string>(),
                 options.count("fatal_errors") == 1);
    if (!file)
      return 2;
    process_file(std::cout, *file);
  }
}
catch (const cppast::libclang_error& ex)
{
  print_error(std::string("[fatal parsing error] ") + ex.what());
  return 2;
}
