#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
set -eu
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -f "$build_dir/protocol" "$build_dir/driver" "$build_dir/component"; rmdir "$build_dir"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined tests/protocol_test.cpp -o "$build_dir/protocol"
"$build_dir/protocol"
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -I tests/stubs tests/driver_test.cpp components/zenner_nfc/pn5180.cpp -o "$build_dir/driver"
"$build_dir/driver"

c++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -I tests/stubs tests/component_test.cpp components/zenner_nfc/zenner_nfc.cpp components/zenner_nfc/pn5180.cpp -o "$build_dir/component"
"$build_dir/component"
