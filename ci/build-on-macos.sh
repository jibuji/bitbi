#!/usr/bin/env bash
#
# Copyright (c) 2019-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

export HOST=x86_64-apple-darwin
# Homebrew's python@3.12 is marked as externally managed (PEP 668).
# Therefore, `--break-system-packages` is needed.
export PIP_PACKAGES="--break-system-packages zmq"
export GOAL="install"
export BITCOIN_CONFIG="--with-gui --with-miniupnpc --with-natpmp --enable-reduce-exports"
export CI_OS_NAME="macos"
export NO_DEPENDS=1
export OSX_SDK=""
export CCACHE_MAXSIZE=400M
export RUN_FUZZ_TESTS=true
export FUZZ_TESTS_CONFIG="--exclude banman"  # https://github.com/bitbi/bitbi/issues/27924


#!/usr/bin/env bash
#
# Copyright (c) 2019-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


# The root dir.
# The ci system copies this folder.
BASE_ROOT_DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )"/../../ >/dev/null 2>&1 && pwd )
export BASE_ROOT_DIR
# The depends dir.
# This folder exists only on the ci guest, and on the ci host as a volume.
export DEPENDS_DIR=${DEPENDS_DIR:-$BASE_ROOT_DIR/depends}
# A folder for the ci system to put temporary files (ccache, datadirs for tests, ...)
# This folder only exists on the ci host.
export BASE_SCRATCH_DIR=${BASE_SCRATCH_DIR:-$BASE_ROOT_DIR/ci/scratch}


echo "Fallback to default values in env (if not yet set)"
# The number of parallel jobs to pass down to make and test_runner.py
export MAKEJOBS=${MAKEJOBS:--j4}
# What host to compile for. See also ./depends/README.md
# Tests that need cross-compilation export the appropriate HOST.
# Tests that run natively guess the host
export HOST=${HOST:-$("$BASE_ROOT_DIR/depends/config.guess")}
# Whether to prefer BusyBox over GNU utilities
export USE_BUSY_BOX=${USE_BUSY_BOX:-false}

export RUN_UNIT_TESTS=${RUN_UNIT_TESTS:-false}
export RUN_FUNCTIONAL_TESTS=${RUN_FUNCTIONAL_TESTS:-false}
export RUN_TIDY=${RUN_TIDY:-false}
export RUN_SECURITY_TESTS=${RUN_SECURITY_TESTS:-false}
# By how much to scale the test_runner timeouts (option --timeout-factor).
# This is needed because some ci machines have slow CPU or disk, so sanitizers
# might be slow or a reindex might be waiting on disk IO.
export TEST_RUNNER_TIMEOUT_FACTOR=${TEST_RUNNER_TIMEOUT_FACTOR:-40}
export TEST_RUNNER_ENV=${TEST_RUNNER_ENV:-}
export RUN_FUZZ_TESTS=${RUN_FUZZ_TESTS:-false}

export CONTAINER_NAME=${CONTAINER_NAME:-ci_unnamed}
export CI_IMAGE_NAME_TAG=${CI_IMAGE_NAME_TAG:-ubuntu:20.04}
# Randomize test order.
# See https://www.boost.org/doc/libs/1_71_0/libs/test/doc/html/boost_test/utf_reference/rt_param_reference/random.html
export BOOST_TEST_RANDOM=${BOOST_TEST_RANDOM:-1}
# See man 7 debconf
export DEBIAN_FRONTEND=noninteractive
export CCACHE_SIZE=${CCACHE_SIZE:-100M}
export CCACHE_TEMPDIR=${CCACHE_TEMPDIR:-/tmp/.ccache-temp}
export CCACHE_COMPRESS=${CCACHE_COMPRESS:-1}
# The cache dir.
# This folder exists only on the ci guest, and on the ci host as a volume.
export CCACHE_DIR=${CCACHE_DIR:-$BASE_SCRATCH_DIR/.ccache}
# Folder where the build result is put (bin and lib).
export BASE_OUTDIR=${BASE_OUTDIR:-$BASE_SCRATCH_DIR/out/$HOST}
# Folder where the build is done (dist and out-of-tree build).
export BASE_BUILD_DIR=${BASE_BUILD_DIR:-$BASE_SCRATCH_DIR/build}
# The folder for previous release binaries.
# This folder exists only on the ci guest, and on the ci host as a volume.
export PREVIOUS_RELEASES_DIR=${PREVIOUS_RELEASES_DIR:-$BASE_ROOT_DIR/releases/$HOST}
export DIR_IWYU="${BASE_SCRATCH_DIR}/iwyu"
export SDK_URL=${SDK_URL:-https://bitcoincore.org/depends-sources/sdks}
export CI_BASE_PACKAGES=${CI_BASE_PACKAGES:-build-essential libtool autotools-dev automake pkg-config bsdmainutils curl ca-certificates ccache python3 rsync cmake git procps bison}
export GOAL=${GOAL:-install}
export DIR_QA_ASSETS=${DIR_QA_ASSETS:-${BASE_SCRATCH_DIR}/qa-assets}
export PATH=${BASE_ROOT_DIR}/ci/retry:$PATH
export CI_RETRY_EXE=${CI_RETRY_EXE:-"retry --"}



# Create folders that are mounted into the docker
mkdir -p "${CCACHE_DIR}"
mkdir -p "${PREVIOUS_RELEASES_DIR}"

export ASAN_OPTIONS="detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1"
export LSAN_OPTIONS="suppressions=${BASE_ROOT_DIR}/test/sanitizer_suppressions/lsan"
export TSAN_OPTIONS="suppressions=${BASE_ROOT_DIR}/test/sanitizer_suppressions/tsan:halt_on_error=1:log_path=${BASE_SCRATCH_DIR}/sanitizer-output/tsan"
export UBSAN_OPTIONS="suppressions=${BASE_ROOT_DIR}/test/sanitizer_suppressions/ubsan:print_stacktrace=1:halt_on_error=1:report_error_type=1"
env | grep -E '^(BITCOIN_CONFIG|BASE_|QEMU_|CCACHE_|LC_ALL|BOOST_TEST_RANDOM|DEBIAN_FRONTEND|CONFIG_SHELL|(ASAN|LSAN|TSAN|UBSAN)_OPTIONS|PREVIOUS_RELEASES_DIR)' | tee /tmp/env
if [[ $BITCOIN_CONFIG = *--with-sanitizers=*address* ]]; then # If ran with (ASan + LSan), Docker needs access to ptrace (https://github.com/google/sanitizers/issues/764)
  CI_CONTAINER_CAP="--cap-add SYS_PTRACE"
fi

export P_CI_DIR="$PWD"
export BINS_SCRATCH_DIR="${BASE_SCRATCH_DIR}/bins/"

CI_EXEC () {
  echo "export PATH=${BINS_SCRATCH_DIR}:\$PATH && cd \"$P_CI_DIR\" && $*" ;
  echo "export PATH=${BINS_SCRATCH_DIR}:\$PATH && cd \"$P_CI_DIR\" && $*" >> ~/command.log
  $CI_EXEC_CMD_PREFIX bash -c "export PATH=${BINS_SCRATCH_DIR}:\$PATH && cd \"$P_CI_DIR\" && $*"
}
export -f CI_EXEC

CI_EXEC mkdir -p "${BINS_SCRATCH_DIR}"

if [ "$CI_OS_NAME" == "macos" ]; then
  top -l 1 -s 0 | awk ' /PhysMem/ {print}'
  echo "Number of CPUs: $(sysctl -n hw.logicalcpu)"
else
  CI_EXEC free -m -h
  CI_EXEC echo "Number of CPUs \(nproc\):" \$\(nproc\)
  CI_EXEC echo "$(lscpu | grep Endian)"
fi
CI_EXEC echo "Free disk space:"
CI_EXEC df -h



CI_EXEC mkdir -p "${BASE_SCRATCH_DIR}/sanitizer-output/"


# Make sure default datadir does not exist and is never read by creating a dummy file
if [ "$CI_OS_NAME" == "macos" ]; then
  echo > "${HOME}/Library/Application Support/Bitcoin"
else
  CI_EXEC echo \> \$HOME/.bitcoin
fi


BITCOIN_CONFIG_ALL="--enable-suppress-external-warnings --disable-dependency-tracking"
if [ -z "$NO_DEPENDS" ]; then
  BITCOIN_CONFIG_ALL="${BITCOIN_CONFIG_ALL} CONFIG_SITE=$DEPENDS_DIR/$HOST/share/config.site"
fi
if [ -z "$NO_WERROR" ]; then
  BITCOIN_CONFIG_ALL="${BITCOIN_CONFIG_ALL} --enable-werror=no"
fi

CI_EXEC "ccache --zero-stats --max-size=$CCACHE_SIZE"
PRINT_CCACHE_STATISTICS="ccache --version | head -n 1 && ccache --show-stats"

BITCOIN_CONFIG_ALL="${BITCOIN_CONFIG_ALL} --enable-external-signer --prefix=$BASE_OUTDIR"

if [ -n "$CONFIG_SHELL" ]; then
  CI_EXEC "$CONFIG_SHELL" -c "./autogen.sh"
else
  CI_EXEC ./autogen.sh
fi

CI_EXEC mkdir -p "${BASE_BUILD_DIR}"
export P_CI_DIR="${BASE_BUILD_DIR}"

CI_EXEC "${BASE_ROOT_DIR}/configure" --cache-file=config.cache "$BITCOIN_CONFIG_ALL" "$BITCOIN_CONFIG" || ( (CI_EXEC cat config.log) && false)

CI_EXEC make distdir VERSION="$HOST"

export P_CI_DIR="${BASE_BUILD_DIR}/bitcoin-$HOST"

CI_EXEC ./configure --cache-file=../config.cache "$BITCOIN_CONFIG_ALL" "$BITCOIN_CONFIG" || ( (CI_EXEC cat config.log) && false)

set -o errtrace
trap 'CI_EXEC "cat ${BASE_SCRATCH_DIR}/sanitizer-output/* 2> /dev/null"' ERR


CI_EXEC "${MAYBE_BEAR}" "${MAYBE_TOKEN}" make "$MAKEJOBS" "$GOAL" || ( echo "Build failure. Verbose build follows." && CI_EXEC make "$GOAL" V=1 ; false )

CI_EXEC "${PRINT_CCACHE_STATISTICS}"
CI_EXEC du -sh "${DEPENDS_DIR}"/*/
CI_EXEC du -sh "${PREVIOUS_RELEASES_DIR}"
cp src/bitbid /root/bitcoin-dist/bitbid