import os

import lit.formats
from lit.llvm import llvm_config
from lit.llvm.subst import ToolSubst

config.name = "CIRCT_EXPERIMENT"
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)
config.suffixes = [".mlir"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.circt_experiment_obj_root, "test")
config.excludes = ["CMakeLists.txt", "lit.cfg.py", "lit.site.cfg.py"]

config.substitutions.append(("%shlibext", config.llvm_shlib_ext))
llvm_config.with_system_environment(["PATH"])
llvm_config.use_default_substitutions()

tool_dirs = [
    config.circt_experiment_tools_dir,
    config.circt_tools_dir,
    config.llvm_tools_dir,
]
tools = ["circt-mockturtle-opt", "circt-mockturtle-translate", "FileCheck"]
if config.circt_experiment_has_plugin:
  tools.append("circt-opt")
llvm_config.add_tool_substitutions(tools, tool_dirs)

config.substitutions.append(("%plugin", config.circt_experiment_plugin))

if not config.circt_experiment_has_plugin:
  config.available_features.add("no-circt-experiment-plugin")
