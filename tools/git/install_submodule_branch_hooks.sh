#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
gitmodules_path="${repo_root}/.gitmodules"

git config --local core.hooksPath "${repo_root}/.githooks"
git config --local push.recurseSubmodules on-demand
git config --local submodule.recurse true

if [[ -f "${gitmodules_path}" ]]; then
    while IFS= read -r submodule_rel; do
        [[ -n "${submodule_rel}" ]] || continue
        submodule_path="${repo_root}/${submodule_rel}"
        if git -C "${submodule_path}" rev-parse --git-dir >/dev/null 2>&1; then
            git -C "${submodule_path}" config --local core.hooksPath "${repo_root}/.githooks/submodule"
        fi
    done < <(git config -f "${gitmodules_path}" --name-only --get-regexp '^submodule\..*\.branch$' 2>/dev/null \
        | sed -E 's/^submodule\.(.*)\.branch$/\1/')
fi

printf 'Installed repo hooks at %s/.githooks\n' "${repo_root}"
