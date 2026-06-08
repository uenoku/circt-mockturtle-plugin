# RUN: rm -rf %t.dir %t.logs
# RUN: mkdir -p %t.dir/benches
# RUN: touch %t.dir/benches/cex.btor2 %t.dir/benches/proven.btor2 %t.dir/benches/timeout.btor2 %t.dir/benches/unsupported.btor2
# RUN: printf '#!/usr/bin/env bash\nfile="${@: -1}"\ncase "$file" in\n*timeout*) exit 124 ;;\nesac\nfor arg in "$@"; do\n  if [[ "$arg" == "--fake-extra" ]]; then\n    echo "saw fake extra"\n  fi\n  if [[ "$arg" == "--btor2-pdr-blocked-cube-limit=7" ]]; then\n    echo "saw block limit"\n  fi\n  if [[ "$arg" == "--conflict-limit=100" ]]; then\n    echo "saw conflict limit"\n  fi\ndone\ncase "$file" in\n*cex*) echo "pdr: counterexample found"; exit 1 ;;\n*proven*) echo "pdr: proven safe"; exit 0 ;;\n*unsupported*) echo "large arrays are no""t supported"; exit 1 ;;\nesac\necho "pdr: unknown within depth"; exit 0\n' > %t.tool
# RUN: chmod +x %t.tool
# RUN: %fraig_scripts/bench-word-level-hwmc.sh --tool %t.tool --root %t.dir --set %t.dir/benches --depth 1 --timeout 1 --pdr-blocked-cube-limit 7 --conflict-limit 100 --tool-arg --fake-extra --log-dir %t.logs --summary-table --detail-table | FileCheck %s
# RUN: FileCheck %s --check-prefix=LOG < %t.logs/benches__proven.btor2.log

# CHECK: summary:
# CHECK:   total: 4
# CHECK:   proven: 1
# CHECK:   counterexample: 1
# CHECK:   timeout: 1
# CHECK:   unsupported-large-array: 1
# CHECK: | set | order | total | success | timeout | fail | proven | counterexample | unknown | unsupported | error |
# CHECK: | benches | name | 4 | 2 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |
# CHECK: | file | result | status | detail | log |
# CHECK: | {{.*}}cex.btor2 | success | counterexample | pdr: counterexample found | {{.*}}benches__cex.btor2.log |
# CHECK: | {{.*}}proven.btor2 | success | proven | pdr: proven safe | {{.*}}benches__proven.btor2.log |
# CHECK: | {{.*}}timeout.btor2 | timeout | timeout |  | {{.*}}benches__timeout.btor2.log |
# CHECK: | {{.*}}unsupported.btor2 | fail | unsupported-large-array | large arrays are not supported | {{.*}}benches__unsupported.btor2.log |

# LOG: saw block limit
# LOG: saw conflict limit
# LOG: saw fake extra
# LOG: pdr: proven safe
