# -*- Python -*-

import os

import lit.formats

from lit.llvm import llvm_config

config.name = 'CIRCT-FRAIG-LEC'
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)
config.suffixes = ['.mlir', '.btor2', '.sh']
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.circt_fraig_lec_obj_root, 'test')
config.excludes = ['CMakeLists.txt', 'lit.cfg.py', 'lit.site.cfg.py.in']

llvm_config.with_system_environment(['HOME', 'TMP', 'TEMP'])
llvm_config.use_default_substitutions()

tool_dirs = [
    config.circt_fraig_lec_tools_dir,
    config.circt_tools_dir,
    config.llvm_tools_dir,
]
llvm_config.with_environment('PATH', config.circt_fraig_lec_tools_dir,
                             append_path=True)
llvm_config.with_environment('PATH', config.circt_tools_dir, append_path=True)
llvm_config.with_environment('PATH', config.llvm_tools_dir, append_path=True)

llvm_config.add_tool_substitutions([
    'circt-fraig-lec',
    'circt-opt',
    'FileCheck',
    'not',
], tool_dirs)
