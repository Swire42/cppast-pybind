// Copyright (C) 2017-2022 Jonathan Müller and cppast contributors
// SPDX-License-Identifier: MIT

#include <iostream>

#include <cxxopts.hpp>

#include <cppast/libclang_parser.hpp> // for libclang_parser, libclang_compile_config, cpp_entity,...

#include "pb.hpp" // process_file

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



// parse a file
std::unique_ptr<cppast::cpp_file> parse_file(const cppast::libclang_compile_config& config,
                       const cppast::diagnostic_logger&     logger,
                       const std::string& filename, bool fatal_error,
                       cppast::cpp_entity_index& idx)
{
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
     cxxopts::value<std::vector<std::string>>());
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
  else if (!options.count("file") || options["file"].as<std::vector<std::string>>().empty())
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
          = cppast::libclang_compile_config(database, options["file"].as<std::vector<std::string>>()[0]);
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

    PB_RootModule mod("example");

    for (std::string const& filename : options["file"].as<std::vector<std::string>>()) {
      // used to resolve cross references
      cppast::cpp_entity_index idx;

      auto file = parse_file(config, logger, filename,
                   options.count("fatal_errors") == 1, idx);
      if (!file)
        return 2;
      //process_file(std::cout, *file, idx);
      mod.merge(PB_RootModule(*file, "example", Context(idx)));
    }

    mod.print_file(Printer(std::cout, ""));
  }
}
catch (const cppast::libclang_error& ex)
{
  print_error(std::string("[fatal parsing error] ") + ex.what());
  return 2;
}
