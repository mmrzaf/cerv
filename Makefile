SHELL := /bin/bash
.DEFAULT_GOAL := all

CC ?= cc
AR ?= ar
GCC ?= gcc
RELEASE_CC ?= $(GCC)
CLANG ?= clang
LLVM_PROFDATA ?= llvm-profdata
LLVM_COV ?= llvm-cov
CBMC ?= cbmc
VERSION := $(shell cat VERSION)

CPPFLAGS := -Isrc -DCERV_VERSION=\"$(VERSION)\"
WARNFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion \
             -Wshadow -Wformat=2 -Wundef -Wstrict-prototypes -Wmissing-prototypes \
             -Wcast-qual -Wwrite-strings -Wvla -Wswitch-enum -fno-common
DEBUGFLAGS := -O0 -g3
SANFLAGS := -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined -DCERV_INSTRUMENTED_BUILD=1
COVERAGEFLAGS := -O0 -g3 -fprofile-instr-generate -fcoverage-mapping -DCERV_INSTRUMENTED_BUILD=1
RELEASE_CFLAGS := -O2 -g1 -DNDEBUG -fPIE -fstack-protector-strong -fstack-clash-protection -D_FORTIFY_SOURCE=3 -ffile-prefix-map=$(CURDIR)=. -fdebug-prefix-map=$(CURDIR)=.
RELEASE_LDFLAGS := -static-pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
FUZZFLAGS := -O1 -g3 -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined
FUZZ_CORE_FLAGS := -O1 -g3 -fno-omit-frame-pointer -fsanitize=fuzzer-no-link,address,undefined
FUZZ_RUNS ?= 5000
FUZZ_MAX_LEN ?= 17000

CORE_SRCS := \
    src/base/invariant.c \
    src/base/slice.c \
    src/base/checked.c \
    src/base/buffer.c \
    src/base/time.c \
    src/http/http_date.c \
    src/http/http_fields.c \
    src/http/http_target.c \
    src/http/http_range.c \
    src/http/http_request.c \
    src/linux/clock.c \
    src/linux/fs.c \
    src/linux/seccomp_filters.c \
    src/linux/sandbox.c \
    src/serve/media_type.c \
    src/serve/representation.c \
    src/serve/response.c \
    src/runtime/timer.c \
    src/runtime/capacity.c \
    src/runtime/arena.c \
    src/runtime/conn_state.c \
    src/runtime/conn.c \
    src/runtime/worker.c \
    src/process/config_workers.c \
    src/process/config.c \
    src/process/diag.c \
    src/process/supervisor.c

DEBUG_OBJS := $(patsubst src/%.c,build/debug/%.o,$(CORE_SRCS))
RELEASE_OBJS := $(patsubst src/%.c,build/release/%.o,$(CORE_SRCS))
TEST_GCC_OBJS := $(patsubst src/%.c,build/test-gcc/obj/%.o,$(CORE_SRCS))
TEST_CLANG_OBJS := $(patsubst src/%.c,build/test-clang/obj/%.o,$(CORE_SRCS))
FUZZ_CORE_OBJS := $(patsubst src/%.c,build/fuzz/obj/%.o,$(CORE_SRCS))
SAN_OBJS := $(patsubst src/%.c,build/sanitize/obj/%.o,$(CORE_SRCS))
COVERAGE_OBJS := $(patsubst src/%.c,build/coverage/obj/%.o,$(CORE_SRCS))
FUZZ_TARGETS := request request_line target authority content_length accept_encoding etag if_range date range qvalue buffer response timer runtime config arena

.PHONY: all debug release server test test-gcc test-clang headers architecture-check sanitize analyze coverage \
        fuzz fuzz-build proof proof-native proof-cbmc proof-required bench hardening-check \
        differential differential-required syscall-audit cross-aarch64 cross-aarch64-required \
        verification-check release-check bounds-report reproducible release-bundle container-check clean help

all: debug

debug: build/debug/libcerv_core.a build/debug/cerv

build/debug/cerv: $(CORE_SRCS) src/main.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(CORE_SRCS) src/main.c -o $@

server: build/debug/cerv

build/debug/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) -MMD -MP -c $< -o $@

build/debug/libcerv_core.a: $(DEBUG_OBJS)
	$(AR) rcs $@ $^

release: build/release/libcerv_core.a build/release/unit build/release/cerv

build/release/cerv: $(RELEASE_OBJS) src/main.c
	@mkdir -p $(dir $@)
	$(RELEASE_CC) $(CPPFLAGS) $(WARNFLAGS) $(RELEASE_CFLAGS) $(RELEASE_OBJS) src/main.c $(RELEASE_LDFLAGS) -o $@

build/release/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(RELEASE_CC) $(CPPFLAGS) $(WARNFLAGS) $(RELEASE_CFLAGS) -MMD -MP -c $< -o $@

build/release/libcerv_core.a: $(RELEASE_OBJS)
	$(AR) rcs $@ $^

build/release/unit: $(RELEASE_OBJS) test/unit/test_main.c
	@mkdir -p $(dir $@)
	$(RELEASE_CC) $(CPPFLAGS) $(WARNFLAGS) $(RELEASE_CFLAGS) $(RELEASE_OBJS) test/unit/test_main.c $(RELEASE_LDFLAGS) -o $@

headers:
	@set -e; for compiler in "$(GCC)" "$(CLANG)"; do \
	  while IFS= read -r header; do \
	    printf '#include "%s"\n' "$${header#src/}" | $$compiler $(CPPFLAGS) $(WARNFLAGS) -x c -fsyntax-only -; \
	  done < <(find src -type f -name '*.h' | sort); \
	done
	@echo "ok: self-contained headers under GCC + Clang"

architecture-check:
	tools/check_source_layers.sh

build/test-gcc/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) -MMD -MP -c $< -o $@

build/test-clang/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) -MMD -MP -c $< -o $@

build/test-gcc/unit: $(TEST_GCC_OBJS) test/unit/test_main.c
	@mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_GCC_OBJS) test/unit/test_main.c -o $@

build/test-gcc/runtime-unit: $(TEST_GCC_OBJS) test/unit/test_runtime.c
	@mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_GCC_OBJS) test/unit/test_runtime.c -o $@

build/test-gcc/supervisor-unit: $(TEST_GCC_OBJS) test/unit/test_supervisor.c
	@mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_GCC_OBJS) test/unit/test_supervisor.c -o $@

build/test-gcc/adversarial: $(TEST_GCC_OBJS) test/adversarial/corpus_runner.c
	@mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_GCC_OBJS) test/adversarial/corpus_runner.c -o $@

build/test-gcc/filesystem-integration: $(TEST_GCC_OBJS) test/integration/test_filesystem.c
	@mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_GCC_OBJS) test/integration/test_filesystem.c -o $@

build/test-gcc/worker-integration: $(TEST_GCC_OBJS) test/integration/test_worker.c
	@mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_GCC_OBJS) test/integration/test_worker.c -o $@

build/test-gcc/supervisor-integration: $(TEST_GCC_OBJS) test/integration/test_supervisor.c
	@mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_GCC_OBJS) test/integration/test_supervisor.c -o $@

build/test-gcc/sandbox-integration: $(TEST_GCC_OBJS) test/integration/test_sandbox.c
	@mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_GCC_OBJS) test/integration/test_sandbox.c -o $@

build/test-gcc/service-integration: $(TEST_GCC_OBJS) test/integration/test_service.c
	@mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_GCC_OBJS) test/integration/test_service.c -o $@

build/test-gcc/cerv: $(TEST_GCC_OBJS) src/main.c
	@mkdir -p $(dir $@)
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_GCC_OBJS) src/main.c -o $@

build/test-clang/unit: $(TEST_CLANG_OBJS) test/unit/test_main.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_CLANG_OBJS) test/unit/test_main.c -o $@

build/test-clang/runtime-unit: $(TEST_CLANG_OBJS) test/unit/test_runtime.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_CLANG_OBJS) test/unit/test_runtime.c -o $@

build/test-clang/supervisor-unit: $(TEST_CLANG_OBJS) test/unit/test_supervisor.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_CLANG_OBJS) test/unit/test_supervisor.c -o $@

build/test-clang/adversarial: $(TEST_CLANG_OBJS) test/adversarial/corpus_runner.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_CLANG_OBJS) test/adversarial/corpus_runner.c -o $@

build/test-clang/filesystem-integration: $(TEST_CLANG_OBJS) test/integration/test_filesystem.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_CLANG_OBJS) test/integration/test_filesystem.c -o $@

build/test-clang/worker-integration: $(TEST_CLANG_OBJS) test/integration/test_worker.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_CLANG_OBJS) test/integration/test_worker.c -o $@

build/test-clang/supervisor-integration: $(TEST_CLANG_OBJS) test/integration/test_supervisor.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_CLANG_OBJS) test/integration/test_supervisor.c -o $@

build/test-clang/sandbox-integration: $(TEST_CLANG_OBJS) test/integration/test_sandbox.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_CLANG_OBJS) test/integration/test_sandbox.c -o $@

build/test-clang/service-integration: $(TEST_CLANG_OBJS) test/integration/test_service.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_CLANG_OBJS) test/integration/test_service.c -o $@

build/test-clang/cerv: $(TEST_CLANG_OBJS) src/main.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(DEBUGFLAGS) $(TEST_CLANG_OBJS) src/main.c -o $@

test-gcc: build/test-gcc/unit build/test-gcc/runtime-unit build/test-gcc/supervisor-unit build/test-gcc/adversarial build/test-gcc/filesystem-integration build/test-gcc/worker-integration build/test-gcc/supervisor-integration build/test-gcc/sandbox-integration build/test-gcc/service-integration build/test-gcc/cerv
	build/test-gcc/unit
	build/test-gcc/runtime-unit
	build/test-gcc/supervisor-unit
	build/test-gcc/adversarial test/corpus/request
	build/test-gcc/filesystem-integration
	build/test-gcc/worker-integration
	build/test-gcc/supervisor-integration
	build/test-gcc/sandbox-integration
	build/test-gcc/service-integration
	@build/test-gcc/cerv --help >/dev/null
	@build/test-gcc/cerv --version | grep -q '^cerv $(VERSION)$$'
	@set +e; build/test-gcc/cerv --unknown >/dev/null 2>&1; code=$$?; set -e; test $$code -eq 2
	python3 test/integration/test_env_runtime.py build/test-gcc/cerv

test-clang: build/test-clang/unit build/test-clang/runtime-unit build/test-clang/supervisor-unit build/test-clang/adversarial build/test-clang/filesystem-integration build/test-clang/worker-integration build/test-clang/supervisor-integration build/test-clang/sandbox-integration build/test-clang/service-integration build/test-clang/cerv
	build/test-clang/unit
	build/test-clang/runtime-unit
	build/test-clang/supervisor-unit
	build/test-clang/adversarial test/corpus/request
	build/test-clang/filesystem-integration
	build/test-clang/worker-integration
	build/test-clang/supervisor-integration
	build/test-clang/sandbox-integration
	build/test-clang/service-integration
	@build/test-clang/cerv --help >/dev/null
	@build/test-clang/cerv --version | grep -q '^cerv $(VERSION)$$'
	@set +e; build/test-clang/cerv --unknown >/dev/null 2>&1; code=$$?; set -e; test $$code -eq 2
	python3 test/integration/test_env_runtime.py build/test-clang/cerv

test: headers architecture-check test-gcc test-clang

build/sanitize/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(SANFLAGS) -MMD -MP -c $< -o $@

sanitize: $(SAN_OBJS)
	@mkdir -p build/sanitize
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(SANFLAGS) $(SAN_OBJS) test/unit/test_main.c -o build/sanitize/unit
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(SANFLAGS) $(SAN_OBJS) test/unit/test_runtime.c -o build/sanitize/runtime-unit
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(SANFLAGS) $(SAN_OBJS) test/unit/test_supervisor.c -o build/sanitize/supervisor-unit
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(SANFLAGS) $(SAN_OBJS) test/adversarial/corpus_runner.c -o build/sanitize/adversarial
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(SANFLAGS) $(SAN_OBJS) test/integration/test_filesystem.c -o build/sanitize/filesystem-integration
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(SANFLAGS) $(SAN_OBJS) test/integration/test_worker.c -o build/sanitize/worker-integration
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(SANFLAGS) $(SAN_OBJS) test/integration/test_supervisor.c -o build/sanitize/supervisor-integration
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(SANFLAGS) $(SAN_OBJS) test/integration/test_sandbox.c -o build/sanitize/sandbox-integration
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(SANFLAGS) $(SAN_OBJS) test/integration/test_service.c -o build/sanitize/service-integration
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/sanitize/unit
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/sanitize/runtime-unit
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/sanitize/supervisor-unit
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/sanitize/adversarial test/corpus/request
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/sanitize/filesystem-integration
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/sanitize/worker-integration
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/sanitize/supervisor-integration
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/sanitize/sandbox-integration
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/sanitize/service-integration

analyze:
	@rm -rf build/analyze && mkdir -p build/analyze/gcc build/analyze/clang
	@printf '%s\n' $(CORE_SRCS) | xargs -P4 -n1 sh -c 'src="$$1"; rel=$${src#src/}; obj="build/analyze/gcc/$${rel%.c}.o"; mkdir -p "$$(dirname "$$obj")"; $(GCC) $(CPPFLAGS) $(WARNFLAGS) -O0 -fanalyzer -c "$$src" -o "$$obj"' sh
	@printf '%s\n' $(CORE_SRCS) | xargs -P4 -n1 sh -c 'src="$$1"; rel=$${src#src/}; mark="build/analyze/clang/$${rel%.c}.ok"; mkdir -p "$$(dirname "$$mark")"; $(CLANG) $(CPPFLAGS) $(WARNFLAGS) -O0 --analyze -Xanalyzer -analyzer-output=text "$$src"; touch "$$mark"' sh
	@if command -v clang-tidy >/dev/null 2>&1; then \
	  clang-tidy $(CORE_SRCS) -- -std=c17 $(CPPFLAGS); \
	else \
	  echo "SKIP: clang-tidy not installed (GCC -fanalyzer and Clang Static Analyzer completed)"; \
	fi
	@echo "ok: available static analyzers completed"

build/coverage/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(COVERAGEFLAGS) -MMD -MP -c $< -o $@

coverage: $(COVERAGE_OBJS)
	@rm -f build/coverage/*.profraw build/coverage/merged.profdata build/coverage/report.txt
	@rm -rf build/coverage/html && mkdir -p build/coverage/html
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(COVERAGEFLAGS) $(COVERAGE_OBJS) test/unit/test_main.c -o build/coverage/unit
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(COVERAGEFLAGS) $(COVERAGE_OBJS) test/unit/test_runtime.c -o build/coverage/runtime-unit
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(COVERAGEFLAGS) $(COVERAGE_OBJS) test/unit/test_supervisor.c -o build/coverage/supervisor-unit
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(COVERAGEFLAGS) $(COVERAGE_OBJS) test/adversarial/corpus_runner.c -o build/coverage/adversarial
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(COVERAGEFLAGS) $(COVERAGE_OBJS) test/integration/test_filesystem.c -o build/coverage/filesystem-integration
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(COVERAGEFLAGS) $(COVERAGE_OBJS) test/integration/test_worker.c -o build/coverage/worker-integration
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(COVERAGEFLAGS) $(COVERAGE_OBJS) test/integration/test_supervisor.c -o build/coverage/supervisor-integration
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(COVERAGEFLAGS) $(COVERAGE_OBJS) test/integration/test_sandbox.c -o build/coverage/sandbox-integration
	$(CLANG) $(CPPFLAGS) $(WARNFLAGS) $(COVERAGEFLAGS) $(COVERAGE_OBJS) test/integration/test_service.c -o build/coverage/service-integration
	LLVM_PROFILE_FILE=build/coverage/unit.profraw build/coverage/unit
	LLVM_PROFILE_FILE=build/coverage/runtime-unit.profraw build/coverage/runtime-unit
	LLVM_PROFILE_FILE=build/coverage/supervisor-unit.profraw build/coverage/supervisor-unit
	LLVM_PROFILE_FILE=build/coverage/adversarial.profraw build/coverage/adversarial test/corpus/request
	LLVM_PROFILE_FILE=build/coverage/filesystem-integration.profraw build/coverage/filesystem-integration
	LLVM_PROFILE_FILE=build/coverage/worker-integration.profraw build/coverage/worker-integration
	LLVM_PROFILE_FILE=build/coverage/supervisor-integration-%p.profraw build/coverage/supervisor-integration
	LLVM_PROFILE_FILE=build/coverage/sandbox-integration-%p.profraw build/coverage/sandbox-integration
	LLVM_PROFILE_FILE=build/coverage/service-integration.profraw build/coverage/service-integration
	$(LLVM_PROFDATA) merge -sparse build/coverage/*.profraw -o build/coverage/merged.profdata
	$(LLVM_COV) report build/coverage/unit -object build/coverage/runtime-unit -object build/coverage/adversarial -object build/coverage/filesystem-integration -object build/coverage/worker-integration -object build/coverage/supervisor-unit -object build/coverage/supervisor-integration -object build/coverage/sandbox-integration -object build/coverage/service-integration \
	  -instr-profile=build/coverage/merged.profdata $(CORE_SRCS) | tee build/coverage/report.txt
	$(LLVM_COV) show build/coverage/unit -object build/coverage/runtime-unit -object build/coverage/adversarial -object build/coverage/filesystem-integration -object build/coverage/worker-integration -object build/coverage/supervisor-unit -object build/coverage/supervisor-integration -object build/coverage/sandbox-integration -object build/coverage/service-integration \
	  -instr-profile=build/coverage/merged.profdata -format=html -output-dir=build/coverage/html $(CORE_SRCS) >/dev/null

build/fuzz/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CLANG) $(CPPFLAGS) -Ifuzz $(WARNFLAGS) $(FUZZ_CORE_FLAGS) -MMD -MP -c $< -o $@

fuzz-build: $(FUZZ_CORE_OBJS)
	@mkdir -p build/fuzz
	@set -e; for target in $(FUZZ_TARGETS); do \
	  echo "[fuzz-build] $$target"; \
	  $(CLANG) $(CPPFLAGS) -Ifuzz $(WARNFLAGS) $(FUZZFLAGS) $(FUZZ_CORE_OBJS) "fuzz/fuzz_$$target.c" -o "build/fuzz/$$target"; \
	done

fuzz: fuzz-build
	@rm -rf build/fuzz-corpus build/fuzz-artifacts build/fuzz-logs && mkdir -p build/fuzz-corpus build/fuzz-artifacts build/fuzz-logs
	@set -e; for target in $(FUZZ_TARGETS); do \
	  echo "[fuzz] $$target ($(FUZZ_RUNS) runs)"; \
	  mkdir -p "build/fuzz-corpus/$$target"; \
	  cp -a "fuzz/corpus/$$target/." "build/fuzz-corpus/$$target/"; \
	  if ! ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    "build/fuzz/$$target" "build/fuzz-corpus/$$target" -runs=$(FUZZ_RUNS) -max_len=$(FUZZ_MAX_LEN) \
	    -artifact_prefix="build/fuzz-artifacts/$$target-" >"build/fuzz-logs/$$target.log" 2>&1; then \
	      cat "build/fuzz-logs/$$target.log" >&2; exit 1; \
	  fi; \
	done
	@echo "ok: bounded libFuzzer campaign completed for $(words $(FUZZ_TARGETS)) targets"

proof-native:
	@mkdir -p build/proof
	$(GCC) $(CPPFLAGS) $(WARNFLAGS) -O2 $(CORE_SRCS) proof/model_checks.c -o build/proof/model_checks
	build/proof/model_checks

proof-cbmc:
	@if command -v $(CBMC) >/dev/null 2>&1; then \
	  set -e; \
	  echo '[cbmc] decimal'; \
	  $(CBMC) proof/cbmc/decimal.c src/base/checked.c $(CPPFLAGS) --function main --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check --unwind 8 --unwinding-assertions; \
	  echo '[cbmc] qvalue'; \
	  $(CBMC) proof/cbmc/qvalue.c src/http/http_fields.c src/http/http_date.c src/base/checked.c src/base/slice.c -Isrc --function main --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check --unwind 12 --unwinding-assertions; \
	  echo '[cbmc] range'; \
	  $(CBMC) proof/cbmc/range.c src/http/http_range.c src/base/checked.c src/http/http_fields.c src/http/http_date.c src/base/slice.c -Isrc --function main --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check --unwind 20 --unwinding-assertions; \
	  echo '[cbmc] buffer'; \
	  $(CBMC) proof/cbmc/buffer.c src/base/buffer.c src/base/checked.c $(CPPFLAGS) --function main --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check --unwind 12 --unwinding-assertions; \
	  echo '[cbmc] path'; \
	  $(CBMC) proof/cbmc/path.c src/http/http_target.c -Isrc --function main --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check --unwind 24 --unwinding-assertions; \
	  echo '[cbmc] timer'; \
	  $(CBMC) proof/cbmc/timer.c src/runtime/timer.c -Isrc --function main --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check --unwind 16 --unwinding-assertions; \
	  echo '[cbmc] slot'; \
	  $(CBMC) proof/cbmc/slot.c src/runtime/arena.c src/runtime/timer.c -Isrc --function main --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check --unwind 16 --unwinding-assertions; \
	  echo '[cbmc] state'; \
	  $(CBMC) proof/cbmc/state.c src/runtime/conn_state.c -Isrc --function main --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check --unwind 8 --unwinding-assertions; \
	  echo '[cbmc] config'; \
	  $(CBMC) proof/cbmc/config.c src/process/config.c src/process/config_workers.c src/runtime/capacity.c src/base/checked.c $(CPPFLAGS) --function main --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check --unwind 16 --unwinding-assertions; \
	else \
	  echo 'SKIP: cbmc not installed; harnesses are present under proof/cbmc/'; \
	fi

proof: proof-native proof-cbmc

proof-required: proof-native
	@command -v $(CBMC) >/dev/null 2>&1 || { echo 'ERROR: cbmc is required for proof-required' >&2; exit 1; }
	@$(MAKE) --no-print-directory proof-cbmc

bench:
	@mkdir -p build/bench
	$(CC) $(CPPFLAGS) $(WARNFLAGS) -O2 $(CORE_SRCS) bench/bench_parsers.c -o build/bench/parsers
	build/bench/parsers | tee build/bench/latest.txt

build/tools/trace_syscalls: tools/trace_syscalls.c
	@mkdir -p $(dir $@)
	$(GCC) $(WARNFLAGS) -O2 $< -o $@

differential: build/release/cerv
	@if command -v nginx >/dev/null 2>&1; then \
	  python3 test/differential/test_nginx_proxy.py build/release/cerv "$$(command -v nginx)"; \
	else \
	  echo 'SKIP: nginx not installed; differential harness is present'; \
	fi

differential-required: build/release/cerv
	@command -v nginx >/dev/null 2>&1 || { echo 'ERROR: nginx is required for differential-required' >&2; exit 1; }
	python3 test/differential/test_nginx_proxy.py build/release/cerv "$$(command -v nginx)"

syscall-audit: build/release/cerv build/tools/trace_syscalls
	tools/syscall_audit.sh build/release/cerv build/tools/trace_syscalls

cross-aarch64:
	tools/check_aarch64.sh

cross-aarch64-required:
	CERV_REQUIRE_FULL_AARCH64=1 tools/check_aarch64.sh

hardening-check: build/release/cerv
	tools/check_elf.sh build/release/cerv

verification-check: release test sanitize analyze coverage fuzz proof differential syscall-audit cross-aarch64 bench hardening-check bounds-report reproducible
	@echo "ok: complete local Cerv verification pipeline completed (see explicit optional-tool SKIP lines above)"

release-check: verification-check container-check
	@echo "ok: complete local Cerv release check completed"

build/release/bounds-report: $(RELEASE_OBJS) tools/bounds_report.c
	@mkdir -p $(dir $@)
	$(RELEASE_CC) $(CPPFLAGS) $(WARNFLAGS) $(RELEASE_CFLAGS) $(RELEASE_OBJS) tools/bounds_report.c $(RELEASE_LDFLAGS) -o $@

bounds-report: build/release/bounds-report
	@build/release/bounds-report

reproducible:
	@tools/reproducible_build.sh

release-bundle:
	@tools/release_bundle.sh

container-check:
	@tools/container_smoke.sh

clean:
	rm -rf build
	@find . -type d -name '__pycache__' -prune -exec rm -rf {} +

help:
	@printf '%s\n' \
	  'make              Build the debug Cerv server and core archive' \
	  'make test         Architecture check + GCC/Clang protocol/runtime/adversarial/integration tests' \
	  'make sanitize     ASan + UBSan + leak-mode test suites' \
	  'make analyze      GCC -fanalyzer + Clang Static Analyzer (+ clang-tidy if installed)' \
	  'make coverage     LLVM line/branch-region coverage plumbing/report' \
	  'make fuzz         Bounded libFuzzer + ASan/UBSan campaign for protocol, timers, and runtime state' \
	  'make proof        Native exhaustive models + CBMC when installed' \
	  'make proof-required  Same, but fail if CBMC is unavailable' \
	  'make bench        Parser microbenchmark harness' \
	  'make differential Nginx reverse-proxy framing differential (skip if unavailable)' \
	  'make syscall-audit Measure post-seccomp master/worker syscall vocabulary with ptrace' \
	  'make cross-aarch64 Exact AArch64 seccomp/UAPI check + full cross-link when toolchain exists' \
	  'make release      Hardened linked verification artifact + core archive' \
	  'make bounds-report     Print fixed per-connection/timer and capacity bounds' \
	  'make reproducible      Compare two controlled clean release builds' \
	  'make release-bundle    Produce deterministic source/binary bundles + SBOM/provenance' \
	  'make container-check   Build/smoke-test the Docker runtime image (requires Docker or Podman)' \
	  'make verification-check Complete local verification pipeline' \
	  'make release-check     Verification pipeline plus container smoke check'

-include $(DEBUG_OBJS:.o=.d) $(RELEASE_OBJS:.o=.d)
