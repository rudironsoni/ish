SHELL := /bin/bash

LINUX_VERSION ?= 6.12
LINUX_ARCH ?= arm64
LINUX_KERNEL_SERIES ?=
LINUX_TARBALL_URL ?=
LINUX_VENDOR_ROOT ?= third_party/linux
KEEP_LINUX_TMP ?= 0
LINUX_MAKE ?=
LINUX_LLVM_BIN ?=
LINUX_SED ?=

.PHONY: vendor-linux-headers
vendor-linux-headers:
	@set -euo pipefail; \
	repo_root="$$(pwd)"; \
	linux_version="$(LINUX_VERSION)"; \
	linux_arch="$(LINUX_ARCH)"; \
	kernel_series="$(LINUX_KERNEL_SERIES)"; \
	vendor_root="$(LINUX_VENDOR_ROOT)"; \
	keep_tmp="$(KEEP_LINUX_TMP)"; \
	linux_make="$(LINUX_MAKE)"; \
	llvm_bin="$(LINUX_LLVM_BIN)"; \
	linux_sed="$(LINUX_SED)"; \
	case "$$linux_arch" in \
		arm64) ;; \
		*) echo "unsupported Linux arch: $$linux_arch" >&2; exit 1 ;; \
	esac; \
	if [ -z "$$kernel_series" ]; then \
		kernel_series="v$${linux_version%%.*}.x"; \
	fi; \
	if [ -z "$$llvm_bin" ] && [ -d /opt/homebrew/opt/llvm/bin ]; then \
		llvm_bin=/opt/homebrew/opt/llvm/bin; \
	fi; \
	if [ -n "$$llvm_bin" ]; then \
		export PATH="$$llvm_bin:$$PATH"; \
	fi; \
	if [ -z "$$linux_sed" ] && command -v gsed >/dev/null 2>&1; then \
		linux_sed="$$(command -v gsed)"; \
	fi; \
	if [ -z "$$linux_make" ]; then \
		if command -v gmake >/dev/null 2>&1; then \
			linux_make="$$(command -v gmake)"; \
		else \
			linux_make="$$(command -v make)"; \
		fi; \
	fi; \
	if ! "$$linux_make" --version 2>/dev/null | head -n1 | grep -Eq 'GNU Make ([4-9]|[1-9][0-9])'; then \
		echo "Linux vendoring requires GNU Make >= 4.0; found: $$("$$linux_make" --version | head -n1)" >&2; \
		echo "Set LINUX_MAKE=/path/to/gmake and rerun." >&2; \
		exit 1; \
	fi; \
	if ! command -v clang >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then \
		echo "Linux vendoring requires clang and ld.lld in PATH." >&2; \
		echo "Set LINUX_LLVM_BIN=/path/to/llvm/bin and rerun." >&2; \
		exit 1; \
	fi; \
	if ! command -v rsync >/dev/null 2>&1; then \
		echo "Linux vendoring requires rsync in PATH." >&2; \
		exit 1; \
	fi; \
	if [ -n "$(LINUX_TARBALL_URL)" ]; then \
		tarball_url="$(LINUX_TARBALL_URL)"; \
	else \
		tarball_url="https://cdn.kernel.org/pub/linux/kernel/$$kernel_series/linux-$$linux_version.tar.xz"; \
	fi; \
	if [[ "$$vendor_root" = /* ]]; then \
		final_vendor_root="$$vendor_root"; \
	else \
		final_vendor_root="$$repo_root/$$vendor_root"; \
	fi; \
	tmp="$$(mktemp -d "$${TMPDIR:-/tmp}/vendor-linux-headers.XXXXXX")"; \
	cleanup() { \
		if [ "$$keep_tmp" = "1" ]; then \
			echo "kept temporary directory: $$tmp"; \
			return; \
		fi; \
		rm -rf "$$tmp"; \
	}; \
	trap cleanup EXIT; \
	require_file() { \
		local path="$$1"; \
		if [ ! -f "$$path" ]; then \
			echo "missing required file: $$path" >&2; \
			exit 1; \
		fi; \
	}; \
	require_dir() { \
		local path="$$1"; \
		if [ ! -d "$$path" ]; then \
			echo "missing required directory: $$path" >&2; \
			exit 1; \
		fi; \
	}; \
	copy_tree() { \
		local src="$$1"; \
		local dest="$$2"; \
		require_dir "$$src"; \
		mkdir -p "$$dest"; \
		cp -R "$$src/." "$$dest"; \
	}; \
	write_source_json() { \
		local tuple_root="$$1"; \
		local tarball_sha="$$2"; \
		printf '%s\n' \
			"{" \
			"  \"linux_version\": \"$$linux_version\"," \
			"  \"linux_arch\": \"$$linux_arch\"," \
			"  \"kernel_series\": \"$$kernel_series\"," \
			"  \"tarball_url\": \"$$tarball_url\"," \
			"  \"tarball_sha256\": \"$$tarball_sha\"," \
			"  \"generated_by\": \"Makefile:vendor-linux-headers\"," \
			"  \"make_targets\": [" \
			"    \"defconfig\"," \
			"    \"olddefconfig\"," \
			"    \"prepare\"," \
			"    \"modules_prepare\"," \
			"    \"headers_install\"" \
			"  ]" \
			"}" \
			> "$$tuple_root/source.json"; \
	}; \
	write_readme() { \
		local out_file="$$1"; \
		printf '%s\n' \
			"# Vendored Linux Headers" \
			"" \
			"This tuple was generated from upstream Linux sources." \
			"Do not edit vendored files manually." \
			"" \
			"Regenerate with:" \
			"" \
			"\`\`\`sh" \
			"make vendor-linux-headers LINUX_VERSION=<version> LINUX_ARCH=<arch>" \
			"\`\`\`" \
			"" \
			"Surfaces:" \
			"" \
			"- \`uapi/include\`: output from \`make headers_install\`." \
			"- \`kheaders/source\`: copied non-generated Linux kernel source headers." \
			"- \`kheaders/generated\`: copied generated Linux kernel build headers from O=." \
			> "$$out_file"; \
	}; \
	write_manifest() { \
		local tuple_root="$$1"; \
		local manifest_file="$$2"; \
		local rel; \
		( \
			cd "$$tuple_root"; \
			find . -type f ! -name manifest.sha256 -print \
				| LC_ALL=C sort \
				| while IFS= read -r rel; do \
					rel="$${rel#./}"; \
					printf '%s  %s\n' "$$(shasum -a 256 "$$tuple_root/$$rel" | awk '{print $$1}')" "$$rel"; \
				done \
		) > "$$manifest_file"; \
	}; \
	validate_vendor_tree() { \
		local tuple_root="$$1"; \
		local uapi_root="$$tuple_root/uapi/include"; \
		local kheaders_source_root="$$tuple_root/kheaders/source"; \
		local kheaders_generated_root="$$tuple_root/kheaders/generated"; \
		if [ -z "$$(find "$$tuple_root" -type f -print -quit)" ]; then \
			echo "empty tuple root: $$tuple_root" >&2; \
			exit 1; \
		fi; \
		require_file "$$uapi_root/linux/wait.h"; \
		require_file "$$uapi_root/asm/signal.h"; \
		require_file "$$uapi_root/asm-generic/errno-base.h"; \
		require_file "$$uapi_root/linux/futex.h"; \
		require_file "$$uapi_root/linux/seccomp.h"; \
		require_file "$$kheaders_source_root/include/linux/fs.h"; \
		require_file "$$kheaders_source_root/include/linux/sched.h"; \
		require_dir "$$kheaders_source_root/arch/$$linux_arch/include"; \
		require_file "$$kheaders_generated_root/include/generated/autoconf.h"; \
		require_file "$$kheaders_generated_root/include/generated/utsrelease.h"; \
		require_dir "$$kheaders_generated_root/include/config"; \
		require_dir "$$kheaders_generated_root/arch/$$linux_arch/include/generated"; \
	}; \
	tarball="$$tmp/linux-$$linux_version.tar.xz"; \
	src="$$tmp/linux-$$linux_version"; \
	obj="$$tmp/obj-$$linux_version-$$linux_arch"; \
	uapi_out="$$tmp/uapi-out"; \
	stage_vendor_root="$$tmp/vendor-root"; \
	host_compat_include="$$tmp/host-compat/include"; \
	host_tool_bin="$$tmp/host-tools/bin"; \
	linux_make_env=(LLVM=1 LLVM_IAS=1 HOSTCC=clang CC=clang LD=ld.lld); \
	mkdir -p "$$host_compat_include" "$$host_tool_bin" "$$uapi_out" "$$stage_vendor_root"; \
	curl -fL --retry 5 --retry-delay 1 --retry-all-errors "$$tarball_url" -o "$$tarball"; \
	tarball_sha="$$(shasum -a 256 "$$tarball" | awk '{print $$1}')"; \
	tar -xf "$$tarball" -C "$$tmp"; \
	require_dir "$$src"; \
	cp "$$repo_root/build_support/linux_host_compat/include/elf.h" "$$host_compat_include/elf.h"; \
	cp "$$repo_root/build_support/linux_host_compat/include/endian.h" "$$host_compat_include/endian.h"; \
	cp "$$repo_root/build_support/linux_host_compat/include/byteswap.h" "$$host_compat_include/byteswap.h"; \
	cp "$$repo_root/build_support/linux_host_compat/include/linux_arm_elf_compat.h" "$$host_compat_include/linux_arm_elf_compat.h"; \
	perl -0pi -e 's/#include \"modpost.h\"/#define _UUID_T\n#define uuid_t int\n#include \"modpost.h\"\n#undef uuid_t/' "$$src/scripts/mod/file2alias.c"; \
	linux_make_env+=("HOSTCFLAGS=-I$$host_compat_include -include $$host_compat_include/linux_arm_elf_compat.h"); \
	if [ -n "$$linux_sed" ]; then \
		ln -sf "$$linux_sed" "$$host_tool_bin/sed"; \
		export PATH="$$host_tool_bin:$$PATH"; \
	fi; \
	"$$linux_make" -C "$$src" O="$$obj" ARCH="$$linux_arch" "$${linux_make_env[@]}" defconfig; \
	if [ -x "$$src/scripts/config" ]; then \
		"$$src/scripts/config" --file "$$obj/.config" -d BUILDTIME_TABLE_SORT || true; \
	fi; \
	"$$linux_make" -C "$$src" O="$$obj" ARCH="$$linux_arch" "$${linux_make_env[@]}" olddefconfig; \
	"$$linux_make" -C "$$src" O="$$obj" ARCH="$$linux_arch" "$${linux_make_env[@]}" prepare; \
	"$$linux_make" -C "$$src" O="$$obj" ARCH="$$linux_arch" "$${linux_make_env[@]}" modules_prepare; \
	"$$linux_make" -C "$$src" O="$$obj" ARCH="$$linux_arch" "$${linux_make_env[@]}" headers_install INSTALL_HDR_PATH="$$uapi_out"; \
	require_dir "$$uapi_out/include"; \
	require_dir "$$src/include"; \
	require_dir "$$src/arch/$$linux_arch/include"; \
	require_dir "$$obj/include/generated"; \
	require_dir "$$obj/include/config"; \
	require_dir "$$obj/arch/$$linux_arch/include/generated"; \
	tuple_stage="$$stage_vendor_root/$$linux_version/$$linux_arch"; \
	uapi_dest="$$tuple_stage/uapi/include"; \
	kheaders_root="$$tuple_stage/kheaders"; \
	kheaders_source_root="$$kheaders_root/source"; \
	kheaders_generated_root="$$kheaders_root/generated"; \
	copy_tree "$$uapi_out/include" "$$uapi_dest"; \
	if [ -d "$$uapi_dest/include" ] && [ ! -d "$$uapi_dest/linux" ]; then \
		normalized_uapi="$$tmp/uapi-normalized"; \
		mkdir -p "$$normalized_uapi"; \
		cp -R "$$uapi_dest/include/." "$$normalized_uapi"; \
		rm -rf "$$uapi_dest"; \
		mkdir -p "$$uapi_dest"; \
		cp -R "$$normalized_uapi/." "$$uapi_dest"; \
	fi; \
	copy_tree "$$src/include" "$$kheaders_source_root/include"; \
	copy_tree "$$src/arch/$$linux_arch/include" "$$kheaders_source_root/arch/$$linux_arch/include"; \
	copy_tree "$$obj/include/generated" "$$kheaders_generated_root/include/generated"; \
	copy_tree "$$obj/include/config" "$$kheaders_generated_root/include/config"; \
	copy_tree "$$obj/arch/$$linux_arch/include/generated" "$$kheaders_generated_root/arch/$$linux_arch/include/generated"; \
	write_readme "$$tuple_stage/README.md"; \
	write_source_json "$$tuple_stage" "$$tarball_sha"; \
	write_manifest "$$tuple_stage" "$$tuple_stage/manifest.sha256"; \
	validate_vendor_tree "$$tuple_stage"; \
	final_tuple_root="$$final_vendor_root/$$linux_version/$$linux_arch"; \
	rm -rf "$$final_tuple_root"; \
	mkdir -p "$$(dirname "$$final_tuple_root")"; \
	rsync -a --delete "$$tuple_stage/" "$$final_tuple_root/"; \
	echo "vendored Linux tuple:"; \
	echo "  $$vendor_root/$$linux_version/$$linux_arch"; \
	echo "surfaces:"; \
	echo "  uapi/include"; \
	echo "  kheaders/source"; \
	echo "  kheaders/generated"
