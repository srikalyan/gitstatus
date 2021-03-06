// Copyright 2019 Roman Perepelitsa.
//
// This file is part of GitStatus.
//
// GitStatus is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// GitStatus is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with GitStatus. If not, see <https://www.gnu.org/licenses/>.

#include "options.h"

#include <getopt.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iostream>

namespace gitstatus {

namespace {

long ParseLong(const char* s) {
  errno = 0;
  char* end = nullptr;
  long res = std::strtol(s, &end, 10);
  if (*end || errno) {
    std::cerr << "gitstatusd: not an integer: " << s << std::endl;
    std::exit(1);
  }
  return res;
}

long ParseInt(const char* s) {
  long res = ParseLong(s);
  if (res < INT_MIN || res > INT_MAX) {
    std::cerr << "gitstatusd: integer out of bounds: " << s << std::endl;
    std::exit(1);
  }
  return res;
}

void PrintUsage() {
  std::cout << "Usage: gitstatusd [OPTION]...\n"
            << "Print machine-readable status of the git repos for directores in stdin.\n"
            << "\n"
            << "OPTIONS\n"
            << "  -l, --lock-fd=NUM [default=-1]\n"
            << "   If non-negative, check whether the specified file descriptor is locked when\n"
            << "   not receiving any requests for one second; exit if it isn't locked.\n"
            << "\n"
            << "  -p, --sigwinch-pid=NUM [default=-1]\n"
            << "   If non-negative, send SIGWINCH to the specified PID when not receiving any\n"
            << "   requests for one second; exit if signal sending fails.\n"
            << "\n"
            << "  -t, --num-threads=NUM [default=-1]\n"
            << "   Use this many threads to scan git workdir for unstaged and untracked files.\n"
            << "   Non-positive value means as many threads as there are CPUs. E.g., on a\n"
            << "   quad-core machine with hyperthreading enabled gitstatusd will use 8 threads.\n"
            << "\n"
            << "  -m, --dirty-max-index-size=NUM [default=-1]\n"
            << "   Report -1 unstaged and untracked if there are more than this many files in\n"
            << "   the index; negative value means infinity.\n"
            << "\n"
            << "  -h, --help\n"
            << "  Display this help and exit.\n"
            << "\n"
            << "INPUT\n"
            << "\n"
            << "  Requests are read from stdin, separated by ascii 30 (record separator). Each\n"
            << "  request is made of the following fields, in the specified order, separated by\n"
            << "  ascii 31 (unit separator):\n"
            << "\n"
            << "    1. Request ID. Any string. Can be empty.\n"
            << "    2. Path to the directory for which git stats are being requested.\n"
            << "\n"
            << "OUTPUT\n"
            << "\n"
            << "  For every request read from stdin there is response written to stdout.\n"
            << "  Responses are separated by ascii 30 (record separator). Each response is made\n"
            << "  of the following fields, in the specified order, separated by ascii 31\n"
            << "  (unit separator):\n"
            << "\n"
            << "     1. Request id. The same as the first field in the request.\n"
            << "     2. 0 if the directory isn't a git repo, 1 otherwise. If 0, all the\n"
            << "        following fields are missing.\n"
            << "     3. Absolute path to the git repository workdir.\n"
            << "     4. Commit hash that HEAD is pointing to. 40 hex digits.\n"
            << "     5. Local branch name or empty if not on a branch.\n"
            << "     6. Upstream branch name. Can be empty.\n"
            << "     7. Remote URL. Can be empty.\n"
            << "     8. Repository state, A.K.A. action. Can be empty.\n"
            << "     9. 1 if there are staged changes, 0 otherwise.\n"
            << "    10. 1 if there are unstaged changes, 0 if there aren't, -1 if unknown.\n"
            << "    11. 1 if there are untracked files, 0 if there aren't, -1 if unknown.\n"
            << "    12. Number of commits the current branch is ahead of upstream.\n"
            << "    13. Number of commits the current branch is behind upstream.\n"
            << "    14. The first tag (in lexicographical order) that points to the same\n"
            << "        commit as HEAD.\n"
            << "    15. Absolute path to the git repository workdir.\n"
            << "\n"
            << "EXAMPLE\n"
            << "\n"
            << "  Send a single request and print response (zsh syntax):\n"
            << "\n"
            << "    local req_id=id\n"
            << "    local dir=$PWD\n"
            << "    echo -nE $req_id$'\\x1f'$dir$'\\x1e' | ./gitstatusd | {\n"
            << "      local resp\n"
            << "      IFS=$'\\x1f' read -rd $'\\x1e' -A resp && print -lr -- \"${(@qq)resp}\"\n"
            << "    }\n"
            << "\n"
            << "  Output:"
            << "\n"
            << "    'id'\n"
            << "    '1'\n"
            << "    'master'\n"
            << "    'master'\n"
            << "    'git@github.com:romkatv/gitstatus.git'\n"
            << "    ''\n"
            << "    '0'\n"
            << "    '1'\n"
            << "    '0'\n"
            << "    '0'\n"
            << "    '0'\n"
            << "    '0'\n"
            << "    '/home/romka/.oh-my-zsh/custom/plugins/gitstatus'\n"
            << "\n"
            << "EXIT STATUS\n"
            << "\n"
            << "  The command returns zero on success (when printing help or on EOF),\n"
            << "  non-zero on failure. In the latter case the output is unspecified.\n"
            << "\n"
            << "COPYRIGHT\n"
            << "\n"
            << "  Copyright 2019 Roman Perepelitsa\n"
            << "  This is free software; see https://github.com/romkatv/gitstatus for copying\n"
            << "  conditions. There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR\n"
            << "  A PARTICULAR PURPOSE." << std::endl;
}

}  // namespace

Options ParseOptions(int argc, char** argv) {
  const struct option opts[] = {{"help", no_argument, nullptr, 'h'},
                                {"lock-fd", required_argument, nullptr, 'l'},
                                {"sigwinch-pid", required_argument, nullptr, 'p'},
                                {"num-threads", required_argument, nullptr, 't'},
                                {"dirty-max-index-size", required_argument, nullptr, 'm'},
                                {}};
  Options res;
  while (true) {
    switch (getopt_long(argc, argv, "hl:t:m:", opts, nullptr)) {
      case -1:
        return res;
      case 'h':
        PrintUsage();
        std::exit(0);
      case 'l':
        res.lock_fd = ParseInt(optarg);
        break;
      case 'p':
        res.sigwinch_pid = ParseInt(optarg);
        break;
      case 't': {
        long n = ParseLong(optarg);
        if (n <= 0) {
          std::cerr << "invalid number of threads: " << n << std::endl;
          std::exit(1);
        }
        res.num_threads = n;
        break;
      }
      case 'm':
        res.dirty_max_index_size = ParseLong(optarg);
        break;
      default:
        std::exit(1);
    }
  }
}

}  // namespace gitstatus
