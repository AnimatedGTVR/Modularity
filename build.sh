#!/usr/bin/env bash
set -euo pipefail

start_time="$(date +%s)"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_platform="native"
build_platform_label="Linux"
build_dir="${script_dir}/build"
player_cache_dir="${build_dir}/player-cache"
windows_toolchain_file=""
cmake_platform_args=()
last_step="bootstrap"
current_step=0
total_steps=0
build_started=0
status_line_active=0
status_current_label=""
status_current_spinner="-"
status_current_elapsed=0

declare -a build_warning_messages=()
declare -a build_error_messages=()

if [[ -t 1 ]]; then
    C_RESET=$'\033[0m'
    C_BOLD=$'\033[1m'
    C_DIM=$'\033[2m'
    C_GREEN=$'\033[32m'
    C_YELLOW=$'\033[33m'
    C_RED=$'\033[31m'
    C_CYAN=$'\033[36m'
    ICON_INFO="ℹ"
    ICON_WARN="⚠"
    ICON_ERROR="✖"
    ICON_OK="✔"
else
    C_RESET=''
    C_BOLD=''
    C_DIM=''
    C_GREEN=''
    C_YELLOW=''
    C_RED=''
    C_CYAN=''
    ICON_INFO="[i]"
    ICON_WARN="[!]"
    ICON_ERROR="[x]"
    ICON_OK="[ok]"
fi

log_info() { printf "%s%s%s %s\n" "${C_CYAN}" "${ICON_INFO}" "${C_RESET}" "$*"; }
log_warn() { printf "%s%s%s %s\n" "${C_YELLOW}" "${ICON_WARN}" "${C_RESET}" "$*"; record_warning "$*"; }
log_error() { printf "%s%s%s %s\n" "${C_RED}" "${ICON_ERROR}" "${C_RESET}" "$*"; record_error "$*"; }
log_ok() { printf "%s%s%s %s\n" "${C_GREEN}" "${ICON_OK}" "${C_RESET}" "$*"; }

record_warning() {
    build_warning_messages+=("$1")
}

record_error() {
    build_error_messages+=("$1")
}

repeat_char() {
    local char="$1"
    local count="$2"
    if (( count <= 0 )); then
        return
    fi
    local result=""
    local i
    for ((i = 0; i < count; i++)); do
        result+="${char}"
    done
    printf "%s" "${result}"
}

if [[ -t 1 ]]; then
    BAR_FILL_CHAR="█"
    BAR_EMPTY_CHAR="░"
    SEP="│"
else
    BAR_FILL_CHAR="#"
    BAR_EMPTY_CHAR="-"
    SEP="|"
fi

progress_prefix() {
    local icon="${1:-${ICON_INFO}}"
    local icon_color="${2:-${C_CYAN}}"
    local width=24
    local filled=0
    local empty="${width}"
    local percent=0
    if (( total_steps > 0 )); then
        filled=$((current_step * width / total_steps))
        empty=$((width - filled))
        percent=$((current_step * 100 / total_steps))
    fi
    local bar_fill bar_empty
    bar_fill="$(repeat_char "${BAR_FILL_CHAR}" "${filled}")"
    bar_empty="$(repeat_char "${BAR_EMPTY_CHAR}" "${empty}")"

    printf "%s[%s]%s %s%s%s %s%2d/%2d%s %s%s%s [%s%s%s%s%s] %s%3d%%%s %s%s%s" \
        "${icon_color}" "${icon}" "${C_RESET}" \
        "${C_DIM}" "${SEP}" "${C_RESET}" \
        "${C_BOLD}" "${current_step}" "${total_steps}" "${C_RESET}" \
        "${C_DIM}" "${SEP}" "${C_RESET}" \
        "${C_GREEN}" "${bar_fill}" "${C_DIM}" "${bar_empty}" "${C_RESET}" \
        "${C_BOLD}" "${percent}" "${C_RESET}" \
        "${C_DIM}" "${SEP}" "${C_RESET}"
}

clear_status_line() {
    if [[ -t 1 && "${status_line_active}" -eq 1 ]]; then
        printf "\r\033[2K"
        status_line_active=0
    fi
}

render_status_line() {
    local label="$1"
    local spinner="$2"
    local elapsed="$3"

    if [[ ! -t 1 ]]; then
        return
    fi

    status_current_label="${label}"
    status_current_spinner="${spinner}"
    status_current_elapsed="${elapsed}"

    printf "\r\033[2K%s %s %s%s%s %s(%ss)%s" \
        "$(progress_prefix "${spinner}" "${C_CYAN}")" \
        "${label}" \
        "${C_DIM}" "${SEP}" "${C_RESET}" \
        "${C_DIM}" "${elapsed}" "${C_RESET}"

    status_line_active=1
}

emit_scrolling_event() {
    local level="$1"
    local message="$2"
    local had_status="${status_line_active}"

    clear_status_line
    case "${level}" in
        info)
            printf "%s%s%s %s\n" "${C_CYAN}" "${ICON_INFO}" "${C_RESET}" "${message}"
            ;;
        warn)
            printf "%s%s%s %s\n" "${C_YELLOW}" "${ICON_WARN}" "${C_RESET}" "${message}"
            ;;
        error)
            printf "%s%s%s %s\n" "${C_RED}" "${ICON_ERROR}" "${C_RESET}" "${message}"
            ;;
        ok)
            printf "%s%s%s %s\n" "${C_GREEN}" "${ICON_OK}" "${C_RESET}" "${message}"
            ;;
        *)
            printf "%s\n" "${message}"
            ;;
    esac

    if [[ -t 1 && "${had_status}" -eq 1 ]]; then
        render_status_line "${status_current_label}" "${status_current_spinner}" "${status_current_elapsed}"
    fi
}

emit_step_completion() {
    local message="$1"
    local had_status="${status_line_active}"

    clear_status_line
    printf "%s\n" "${message}"

    if [[ -t 1 && "${had_status}" -eq 1 ]]; then
        render_status_line "${status_current_label}" "${status_current_spinner}" "${status_current_elapsed}"
    fi
}

is_interesting_info_line() {
    local lower="$1"
    [[ "${lower}" == *"built target"* ]] ||
    [[ "${lower}" == *"configuring done"* ]] ||
    [[ "${lower}" == *"generating done"* ]] ||
    [[ "${lower}" == *"build files have been written"* ]] ||
    [[ "${lower}" == *"installing:"* ]] ||
    [[ "${lower}" == *"up-to-date:"* ]] ||
    [[ "${lower}" == *"linking cxx executable"* ]] ||
    [[ "${lower}" == *"linking cxx static library"* ]] ||
    [[ "${lower}" == *"copying"* ]]
}

process_build_output_line() {
    local step="$1"
    local line="$2"

    line="${line%$'\r'}"
    if [[ -z "${line//[[:space:]]/}" ]]; then
        return
    fi

    local lower="${line,,}"
    local issue_text="[${step}] ${line}"

    if [[ "${lower}" == *"0 errors generated"* ]]; then
        return
    fi

    if [[ "${lower}" == *"warning:"* ]] || [[ "${lower}" == *" cmake warning"* ]] || [[ "${lower}" == *"warning "* ]]; then
        record_warning "${issue_text}"
        emit_scrolling_event "warn" "${issue_text}"
        return
    fi

    if [[ "${lower}" == *"error:"* ]] || [[ "${lower}" == *"fatal"* ]] || [[ "${lower}" == *"undefined reference"* ]] || [[ "${lower}" == *"collect2: error"* ]] || [[ "${lower}" == *"ld: cannot"* ]]; then
        record_error "${issue_text}"
        emit_scrolling_event "error" "${issue_text}"
        return
    fi

    if is_interesting_info_line "${lower}"; then
        emit_scrolling_event "info" "[${step}] ${line}"
    fi
}

print_issue_summary() {
    local max_items=8
    local i

    if [[ "${#build_warning_messages[@]}" -gt 0 ]]; then
        printf "\n%sWarnings (%d):%s\n" "${C_YELLOW}" "${#build_warning_messages[@]}" "${C_RESET}"
        for ((i = 0; i < ${#build_warning_messages[@]} && i < max_items; i++)); do
            printf "  %s %s\n" "${ICON_WARN}" "${build_warning_messages[$i]}"
        done
        if [[ "${#build_warning_messages[@]}" -gt "${max_items}" ]]; then
            printf "  %s ... and %d more warning(s)\n" "${ICON_WARN}" "$(( ${#build_warning_messages[@]} - max_items ))"
        fi
    fi

    if [[ "${#build_error_messages[@]}" -gt 0 ]]; then
        printf "\n%sErrors (%d):%s\n" "${C_RED}" "${#build_error_messages[@]}" "${C_RESET}"
        for ((i = 0; i < ${#build_error_messages[@]} && i < max_items; i++)); do
            printf "  %s %s\n" "${ICON_ERROR}" "${build_error_messages[$i]}"
        done
        if [[ "${#build_error_messages[@]}" -gt "${max_items}" ]]; then
            printf "  %s ... and %d more error(s)\n" "${ICON_ERROR}" "$(( ${#build_error_messages[@]} - max_items ))"
        fi
    fi
}

advance_step() {
    local label="$1"
    current_step=$((current_step + 1))
    last_step="${label}"
}

run_step() {
    local label="$1"
    shift

    advance_step "${label}"
    clear_status_line
    printf "\n%s %s\n" "$(progress_prefix "${ICON_INFO}" "${C_CYAN}")" "${label}"

    "$@"
}

run_long_step() {
    local label="$1"
    shift

    advance_step "${label}"

    if [[ ! -t 1 ]]; then
        printf "\n%s %s\n" "$(progress_prefix "${ICON_INFO}" "${C_CYAN}")" "${label}"
        "$@"
        return
    fi

    local log_file
    log_file="$(mktemp "/tmp/modularity-build-step-${current_step}.XXXX.log")"
    local start_step=${SECONDS}
    local spinner_frames='-\|/'
    local spinner_index=0

    set +e
    "$@" >"${log_file}" 2>&1 &
    local cmd_pid=$!
    render_status_line "${label}" "-" 0

    while kill -0 "${cmd_pid}" >/dev/null 2>&1; do
        local elapsed=$((SECONDS - start_step))
        local frame="${spinner_frames:spinner_index:1}"
        render_status_line "${label}" "${frame}" "${elapsed}"
        spinner_index=$(((spinner_index + 1) % 4))
        sleep 0.2
    done

    wait "${cmd_pid}"
    local exit_code=$?
    set -e

    while IFS= read -r line; do
        process_build_output_line "${label}" "${line}"
    done < "${log_file}"

    local elapsed=$((SECONDS - start_step))

    if [[ "${exit_code}" -ne 0 ]]; then
        clear_status_line
        record_error "[${label}] Step failed with exit code ${exit_code}. Log: ${log_file}"
        emit_step_completion "$(progress_prefix "${ICON_ERROR}" "${C_RED}") ${label} ${C_DIM}${SEP}${C_RESET} ${C_RED}failed${C_RESET} ${C_DIM}(${elapsed}s)${C_RESET}"
        log_error "Detailed log kept at: ${log_file}"
        return "${exit_code}"
    fi

    clear_status_line
    emit_step_completion "$(progress_prefix "${ICON_OK}" "${C_GREEN}") ${label} ${C_DIM}${SEP}${C_RESET} ${C_DIM}(${elapsed}s)${C_RESET}"
    rm -f "${log_file}"
}

finish() {
    local exit_code="$1"
    set +e

    if [[ "${build_started}" -eq 0 ]]; then
        return
    fi
    clear_status_line

    local end_time
    end_time="$(date +%s)"
    local duration=$((end_time - start_time))
    echo

    if [[ "${exit_code}" -eq 0 ]]; then
        printf "%s%s================================%s\n" "${C_RESET}" "${C_BOLD}" "${C_RESET}"
        printf "%s   Modularity - Native Linux Build Complete%s\n" "${C_BOLD}" "${C_RESET}"
        printf "%s================================%s\n" "${C_BOLD}" "${C_RESET}"
        log_ok "Build completed in ${duration}s."
        log_info "Artifacts: ${build_dir}"
        print_issue_summary
    else
        printf "%s%s================================%s\n" "${C_RESET}" "${C_BOLD}" "${C_RESET}"
        printf "%s   Modularity - Native Linux Build Failed%s\n" "${C_BOLD}" "${C_RESET}"
        printf "%s================================%s\n" "${C_BOLD}" "${C_RESET}"
        log_error "Build failed after ${duration}s at step: ${last_step} (exit code ${exit_code})."
        print_issue_summary
    fi
}
trap 'finish $?' EXIT

usage() {
    cat <<'EOF'
Usage: ./build.sh [options]
Options:
  --clean                 Remove existing build directories first
  --Windows               Cross-build a Windows target with MinGW-w64
  --build-type=<type>     CMake build type (default: Release)
  --generator=<name>      Force CMake generator (e.g. Ninja, "Unix Makefiles")
  --jobs=<N>              Parallel compile jobs (default: nproc - 2, min 1)
  --skip-deps             Skip automatic dependency checks/install
  --zip                   Package as .zip instead of the default .7z
  --help                  Show this help message
EOF
}

clean_build=0
build_type="Release"
skip_deps=0
package_format="7Z"
ncpus="$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 4)"
jobs=$(( ncpus - 2 ))
if (( jobs < 1 )); then
    jobs=1
fi
jobs_overridden=0
preferred_generator=""
cmake_generator=""

for arg in "$@"; do
    case "$arg" in
        --clean)
            clean_build=1
            ;;
        --Windows|--windows|--platform=Windows|--platform=windows)
            build_platform="windows"
            ;;
        --platform=native|--platform=Linux|--platform=linux)
            build_platform="native"
            ;;
        --build-type=*)
            build_type="${arg#*=}"
            ;;
        --generator=*)
            preferred_generator="${arg#*=}"
            ;;
        --jobs=*)
            jobs_value="${arg#*=}"
            if ! [[ "${jobs_value}" =~ ^[0-9]+$ ]] || (( jobs_value < 1 )); then
                log_error "--jobs requires a positive integer (got: ${jobs_value})"
                exit 1
            fi
            jobs="${jobs_value}"
            jobs_overridden=1
            ;;
        --skip-deps)
            skip_deps=1
            ;;
        --zip|--ZIP)
            package_format="ZIP"
            ;;
        --7z|--7Z)
            package_format="7Z"
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            log_warn "Unknown argument: ${arg}"
            usage
            exit 1
        ;;
    esac
done

if [[ "${build_platform}" == "windows" ]]; then
    build_platform_label="Windows cross-build"
    build_dir="${script_dir}/build/windows"
    player_cache_dir="${build_dir}/player-cache"
    cmake_platform_args=(-DMODULARITY_USE_MONO=OFF -DMODULARITY_ENABLE_PHYSX=OFF -DMODULARITY_ENABLE_VULKAN=OFF -DMODULARITY_ENABLE_SNDFILE=OFF -DMODULARITY_ENABLE_OPUSFILE=OFF)

    if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 && \
       command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 && \
       command -v x86_64-w64-mingw32-windres >/dev/null 2>&1; then
        windows_toolchain_file="${script_dir}/src/ThirdParty/glfw/CMake/x86_64-w64-mingw32.cmake"
    elif command -v x86_64-w64-mingw32-clang++ >/dev/null 2>&1 && \
         command -v x86_64-w64-mingw32-clang >/dev/null 2>&1 && \
         command -v x86_64-w64-mingw32-windres >/dev/null 2>&1; then
        windows_toolchain_file="${script_dir}/src/ThirdParty/glfw/CMake/x86_64-w64-mingw32-clang.cmake"
    else
        log_error "Windows cross-build requested, but no x86_64-w64-mingw32 toolchain was found."
        log_error "Install MinGW-w64 (gcc or clang variant) before using --Windows."
        exit 1
    fi

    cmake_platform_args+=(-DCMAKE_TOOLCHAIN_FILE="${windows_toolchain_file}")
fi

pkg_manager=""
pkg_prefix=()
pkg_index_updated=0

detect_package_manager() {
    if command -v apt-get >/dev/null 2>&1; then
        pkg_manager="apt"
    elif command -v dnf >/dev/null 2>&1; then
        pkg_manager="dnf"
    elif command -v pacman >/dev/null 2>&1; then
        pkg_manager="pacman"
    elif command -v zypper >/dev/null 2>&1; then
        pkg_manager="zypper"
    else
        pkg_manager=""
    fi

    if [[ "$(id -u)" -eq 0 ]]; then
        pkg_prefix=()
    elif command -v sudo >/dev/null 2>&1; then
        pkg_prefix=(sudo)
    elif command -v doas >/dev/null 2>&1; then
        pkg_prefix=(doas)

    else
        pkg_prefix=()
    fi
}

detect_cmake_generator() {
    if [[ -n "${preferred_generator}" ]]; then
        cmake_generator="${preferred_generator}"
        return
    fi

    if [[ -n "${CMAKE_GENERATOR:-}" ]]; then
        cmake_generator="${CMAKE_GENERATOR}"
        return
    fi

    if [[ "${clean_build}" -eq 0 && -f "${build_dir}/CMakeCache.txt" ]]; then
        cmake_generator="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "${build_dir}/CMakeCache.txt" | head -n1)"
        if [[ -n "${cmake_generator}" ]]; then
            return
        fi
    fi

    if command -v ninja >/dev/null 2>&1; then
        cmake_generator="Ninja"
        return
    fi

    cmake_generator=""
}

admin_cmd() {
    if [[ "${#pkg_prefix[@]}" -gt 0 ]]; then
        "${pkg_prefix[@]}" "$@"
    else
        "$@"
    fi
}

update_pkg_index_once() {
    if [[ "${pkg_index_updated}" -eq 1 ]]; then
        return
    fi
    case "${pkg_manager}" in
        apt)
            admin_cmd apt-get update
            ;;
        pacman)
            admin_cmd pacman -Sy --noconfirm
            ;;
        dnf|zypper)
            ;;
    esac
    pkg_index_updated=1
}

install_packages() {
    local -a packages=("$@")
    if [[ "${#packages[@]}" -eq 0 ]]; then
        return
    fi

    update_pkg_index_once
    case "${pkg_manager}" in
        apt)
            admin_cmd apt-get install -y "${packages[@]}"
            ;;
        dnf)
            admin_cmd dnf install -y "${packages[@]}"
            ;;
        pacman)
            admin_cmd pacman -S --noconfirm --needed "${packages[@]}"
            ;;
        zypper)
            admin_cmd zypper --non-interactive install --no-recommends "${packages[@]}"
            ;;
        *)
            return 1
            ;;
    esac
}

install_optional_first_hit() {
    local -a candidates=("$@")
    local candidate
    for candidate in "${candidates[@]}"; do
        if install_packages "${candidate}" >/dev/null 2>&1; then
            log_ok "Installed optional package: ${candidate}"
            return 0
        fi
    done
    return 1
}

ensure_linux_dependencies() {
    detect_package_manager

    if [[ -z "${pkg_manager}" ]]; then
        log_warn "No supported package manager detected (apt/dnf/pacman/zypper). Skipping auto-install."
        return
    fi

    if [[ "$(id -u)" -ne 0 && "${#pkg_prefix[@]}" -eq 0 ]]; then
        log_warn "Auto-install requires root or sudo. Skipping dependency installation."
        return
    fi

    local need_install=0
    local -a missing=()
    local cmd
    for cmd in git cmake pkg-config c++; do
        if ! command -v "${cmd}" >/dev/null 2>&1; then
            need_install=1
            missing+=("${cmd}")
        fi
    done

    if command -v pkg-config >/dev/null 2>&1; then
        local module
        for module in x11 xrandr xi xinerama xcursor gl; do
            if ! pkg-config --exists "${module}"; then
                need_install=1
                missing+=("${module}-dev")
            fi
        done
        if ! pkg-config --exists vulkan; then
            need_install=1
            missing+=("vulkan-dev")
        fi
        for module in sndfile opusfile; do
            if ! pkg-config --exists "${module}"; then
                need_install=1
                missing+=("${module}")
            fi
        done
    fi

    if ! command -v glslc >/dev/null 2>&1; then
        need_install=1
        missing+=("glslc")
    fi
    if ! command -v vulkaninfo >/dev/null 2>&1; then
        need_install=1
        missing+=("vulkan-tools")
    fi

    local -a required_pkgs=()
    local -a core_pkgs=()
    local -a x11_pkgs=()
    local -a graphics_pkgs=()
    local -a vulkan_pkgs=()
    local -a optional_pkgs=()
    local -a glslc_candidates=()
    case "${pkg_manager}" in
        apt)
            core_pkgs=(build-essential cmake pkg-config git zlib1g-dev)
            x11_pkgs=(libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev)
            graphics_pkgs=(libgl1-mesa-dev libegl1-mesa-dev libwayland-dev)
            vulkan_pkgs=(libvulkan-dev vulkan-tools glslang-tools)
            audio_pkgs=(libsndfile1-dev libopusfile-dev)
            optional_pkgs=(ccache)
            glslc_candidates=(glslc shaderc)
            ;;
        dnf)
            core_pkgs=(gcc gcc-c++ make cmake pkgconf-pkg-config git zlib-devel)
            x11_pkgs=(libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel)
            graphics_pkgs=(mesa-libGL-devel mesa-libEGL-devel wayland-devel)
            vulkan_pkgs=(vulkan-loader-devel vulkan-tools glslang)
            audio_pkgs=(libsndfile-devel opusfile-devel)
            optional_pkgs=(ccache)
            glslc_candidates=(shaderc shaderc-devel)
            ;;
        pacman)
            core_pkgs=(base-devel cmake pkgconf git zlib)
            x11_pkgs=(libx11 libxrandr libxinerama libxcursor libxi)
            graphics_pkgs=(mesa wayland)
            vulkan_pkgs=(vulkan-headers vulkan-tools glslang)
            audio_pkgs=(libsndfile opusfile)
            optional_pkgs=(ccache)
            glslc_candidates=(shaderc)
            ;;
        zypper)
            core_pkgs=(gcc gcc-c++ make cmake pkg-config git zlib-devel)
            x11_pkgs=(libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel)
            graphics_pkgs=(Mesa-libGL-devel Mesa-libEGL-devel wayland-devel)
            vulkan_pkgs=(vulkan-devel vulkan-tools glslang)
            audio_pkgs=(libsndfile-devel libopusfile-devel)
            optional_pkgs=(ccache)
            glslc_candidates=(shaderc shaderc-devel)
            ;;
    esac
    required_pkgs=(
        "${core_pkgs[@]}"
        "${x11_pkgs[@]}"
        "${graphics_pkgs[@]}"
        "${vulkan_pkgs[@]}"
        "${audio_pkgs[@]}"
        "${optional_pkgs[@]}"
    )

    log_info "Dependency hierarchy (${pkg_manager}):"
    echo "  +-- Core toolchain"
    for pkg in "${core_pkgs[@]}"; do echo "  |   +-- ${pkg}"; done
    echo "  +-- X11/Windowing"
    for pkg in "${x11_pkgs[@]}"; do echo "  |   +-- ${pkg}"; done
    echo "  +-- OpenGL/EGL/Wayland"
    for pkg in "${graphics_pkgs[@]}"; do echo "  |   +-- ${pkg}"; done
    echo "  +-- Vulkan stack"
    for pkg in "${vulkan_pkgs[@]}"; do echo "  |   +-- ${pkg}"; done
    echo "  +-- Audio import (sndfile, opusfile)"
    for pkg in "${audio_pkgs[@]}"; do echo "  |   +-- ${pkg}"; done
    echo "  +-- Optional acceleration"
    for pkg in "${optional_pkgs[@]}"; do echo "      +-- ${pkg}"; done

    if [[ "${need_install}" -eq 0 ]]; then
        log_ok "Build dependencies already present."
        return
    fi

    log_info "Missing prerequisites detected: ${missing[*]}"
    log_info "Installing packages using ${pkg_manager}..."

    install_packages "${required_pkgs[@]}"

    if ! command -v glslc >/dev/null 2>&1; then
        install_optional_first_hit "${glslc_candidates[@]}" || log_warn "glslc package candidate not found; Vulkan runtime shader compile may fail."
    fi

    if ! command -v glslc >/dev/null 2>&1; then
        log_warn "glslc is still missing."
    fi
    if ! command -v vulkaninfo >/dev/null 2>&1; then
        log_warn "vulkaninfo is still missing."
    fi
}

sync_submodules() {
    git -C "${script_dir}" submodule sync --recursive
    git -C "${script_dir}" submodule update --init --recursive
}

configure_ccache() {
    if command -v ccache >/dev/null 2>&1; then
        export CCACHE_BASEDIR="${script_dir}"
        export CCACHE_NOHASHDIR=1
        log_info "ccache detected. Normalizing paths for cross-build cache hits."
    fi
}

clean_editor_build() {
    if [[ -d "${build_dir}" ]]; then
        rm -rf "${build_dir}"
        log_ok "Removed ${build_dir}"
    fi
}

clean_player_cache() {
    if [[ -d "${player_cache_dir}" ]]; then
        rm -rf "${player_cache_dir}"
        log_ok "Removed ${player_cache_dir}"
    fi
}

configure_editor_build() {
    local -a generator_args=()
    if [[ ! -f "${build_dir}/CMakeCache.txt" && -n "${cmake_generator}" ]]; then
        generator_args=(-G "${cmake_generator}")
    fi
    cmake "${generator_args[@]}" -S "${script_dir}" -B "${build_dir}" \
        "${cmake_platform_args[@]}" \
        -DMONO_ROOT=/usr \
        -DCMAKE_BUILD_TYPE="${build_type}"
}

build_editor_targets() {
    cmake --build "${build_dir}" --parallel "${jobs}"
}

install_editor_targets() {
    cmake --install "${build_dir}" --prefix "${build_dir}/install"
}

copy_third_party_libraries() {
    local target_dir="$1"

    mkdir -p "${target_dir}/Packages/ThirdParty"
    if [[ "${build_platform}" == "windows" ]]; then
        find "${target_dir}" -type f \( -name "*.a" -o -name "*.so" -o -name "*.dylib" -o -name "*.lib" -o -name "*.dll" -o -name "*.dll.a" \) \
            -not -path "${target_dir}/Packages/*" -exec cp -f {} "${target_dir}/Packages/ThirdParty/" \;
    else
        find "${target_dir}" -type f \( -name "*.a" -o -name "*.so" -o -name "*.dylib" -o -name "*.lib" \) \
            -not -path "${target_dir}/Packages/*" -exec cp -f {} "${target_dir}/Packages/ThirdParty/" \;
    fi
}

copy_engine_libraries() {
    local target_dir="$1"

    mkdir -p "${target_dir}/Packages/Engine"
    find "${target_dir}" -type f \( -name "libcore*" -o -name "core*.lib" -o -name "core*.dll" \) \
        -not -path "${target_dir}/Packages/*" -exec cp -f {} "${target_dir}/Packages/Engine/" \;
}

configure_player_build() {
    local -a generator_args=()
    if [[ ! -f "${player_cache_dir}/CMakeCache.txt" && -n "${cmake_generator}" ]]; then
        generator_args=(-G "${cmake_generator}")
    fi
    cmake "${generator_args[@]}" -S "${script_dir}" -B "${player_cache_dir}" \
        "${cmake_platform_args[@]}" \
        -DMONO_ROOT=/usr \
        -DCMAKE_BUILD_TYPE="${build_type}" \
        -DMODULARITY_BUILD_EDITOR=OFF
}

build_player_target() {
    cmake --build "${player_cache_dir}" --target ModularityPlayer --parallel "${jobs}"
}

finalize_packaging() {
    rm -rf "${build_dir}/Template-Projects"
    cp -r "${script_dir}/Resources" "${build_dir}/"
    if [[ -d "${script_dir}/Template-Projects" ]]; then
        cp -r "${script_dir}/Template-Projects" "${build_dir}/"
    else
        mkdir -p "${build_dir}/Template-Projects"
    fi
    if [[ -f "${build_dir}/Resources/imgui.ini" ]]; then
        cp "${build_dir}/Resources/imgui.ini" "${build_dir}/"
    fi
    ln -sfn "${build_dir}/compile_commands.json" "${script_dir}/compile_commands.json"
    (cd "${build_dir}" && cpack -G "${package_format}")
}

show_stage_hierarchy() {
    local -a stages=()

    if [[ "${skip_deps}" -eq 0 && "${build_platform}" != "windows" && "$(uname -s)" == "Linux" ]]; then
        stages+=("Check/install system dependencies")
    fi
    if [[ "${clean_build}" -eq 1 ]]; then
        stages+=("Clean editor build directory")
        stages+=("Clean player cache directory")
    fi
    stages+=(
        "Sync git submodules"
        "Configure editor build"
        "Build editor + engine targets"
        "Install editor artifacts"
        "Collect editor third-party libraries"
        "Collect editor engine libraries"
        "Configure player-only cache build"
        "Build ModularityPlayer target"
        "Collect player third-party libraries"
        "Collect player engine libraries"
        "Package artifacts and resources"
    )

    log_info "Build stage hierarchy:"
    local i
    local stage_count="${#stages[@]}"
    for ((i = 0; i < stage_count; i++)); do
        local branch="|--"
        if [[ $((i + 1)) -eq "${stage_count}" ]]; then
            branch='`--'
        fi
        printf "  %s [%02d/%02d] %s\n" "${branch}" "$((i + 1))" "${stage_count}" "${stages[$i]}"
    done
}

base_steps=11
total_steps="${base_steps}"
if [[ "${clean_build}" -eq 1 ]]; then
    total_steps=$((total_steps + 2))
fi
if [[ "${skip_deps}" -eq 0 && "${build_platform}" != "windows" && "$(uname -s)" == "Linux" ]]; then
    total_steps=$((total_steps + 1))
fi

build_started=1
detect_cmake_generator
printf "%s================================%s\n" "${C_BOLD}" "${C_RESET}"
printf "%s   Modularity - %s%s\n" "${C_BOLD}" "${build_platform_label}" "${C_RESET}"
printf "%s================================%s\n" "${C_BOLD}" "${C_RESET}"
if [[ "${jobs_overridden}" -eq 0 && "${jobs}" -lt "${ncpus}" ]]; then
    log_info "Build type: ${build_type} | Jobs: ${jobs} (of ${ncpus} cores; reserved 2 to keep desktop responsive — override with --jobs=N)"
else
    log_info "Build type: ${build_type} | Jobs: ${jobs}"
fi
if [[ -n "${cmake_generator}" ]]; then
    log_info "CMake generator: ${cmake_generator}"
fi
log_info "Package format: ${package_format}"
if [[ "${build_platform}" == "windows" && -n "${windows_toolchain_file}" ]]; then
    log_info "Windows toolchain: ${windows_toolchain_file}"
fi
show_stage_hierarchy

if [[ "${skip_deps}" -eq 0 && "${build_platform}" != "windows" && "$(uname -s)" == "Linux" ]]; then
    run_step "Installing Dependencies" ensure_linux_dependencies
elif [[ "${skip_deps}" -eq 0 ]]; then
    log_warn "Auto dependency install is only implemented for native Linux builds."
fi

configure_ccache

if [[ "${clean_build}" -eq 1 ]]; then
    run_long_step "Cleaning Editor" clean_editor_build
    run_long_step "Cleaning Player" clean_player_cache
fi

run_long_step "Syncing Submodules" sync_submodules
run_long_step "Configuring Editor" configure_editor_build
run_long_step "Building Editor" build_editor_targets
run_long_step "Installing Editor" install_editor_targets
run_long_step "Collecting Editor Libs" copy_third_party_libraries "${build_dir}"
run_long_step "Collecting Engine Libs" copy_engine_libraries "${build_dir}"
run_long_step "Configuring Player" configure_player_build
run_long_step "Building Player" build_player_target
run_long_step "Collecting Player Libs" copy_third_party_libraries "${player_cache_dir}"
run_long_step "Collecting Player Engine Libs" copy_engine_libraries "${player_cache_dir}"
run_long_step "Packaging Engine" finalize_packaging
