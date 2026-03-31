# Merged Repository Wiring

This bridge layer is the low-risk seam between the canonical runtime tree and the merged repository content that came in through `master`, `main`, `codex/model-silicon-photon-confinement`, and `unifiedpatches`.

The wiring is intentionally additive:

- the runtime path stays unchanged unless the bridge is explicitly enabled
- merged repository files are read through the existing crawler observation path instead of being hot-swapped into live systems
- the OpenAI chat bridge is command-driven and does not run unless asked

## Runtime Bridges

The canonical runtime now exposes three bridge surfaces:

- `GE_RepoReader` in [GE_repo_reader.hpp](/C:/Users/Myke/.codex/worktrees/47d0-canonical/include/GE_repo_reader.hpp)
- merged repository status commands in [GE_runtime.cpp](/C:/Users/Myke/.codex/worktrees/47d0-canonical/src/GE_runtime.cpp)
- optional OpenAI chat bridge in [ew_openai_chat.hpp](/C:/Users/Myke/.codex/worktrees/47d0-canonical/vulkan_app/include/ew_openai_chat.hpp)

`GE_RepoReader` scans bounded text assets from:

- `include/`
- `src/`
- `vulkan_app/include/`
- `vulkan_app/src/`
- `docs/`
- `ResearchConfinement/`
- `scripts/`
- `ge_canon/...` mirrors when present

## Commands

Use these commands from the runtime/editor UI:

- `/repo_reader on files=1 bytes=4096 rescan=1`
- `/repo_reader off`
- `/repo_reader_status`
- `/merged_repo_status`
- `/openai_status`
- `/openai_chat <prompt>`

`/research_status` now includes the merged repository and repo-reader status lines so the bridge state is visible during normal research audits.

## Safety Contract

The bridge is designed to preserve current runtime behavior:

- `repo_reader.enabled` defaults to `false`
- file reads are capped per tick and per file
- only source/spec-style text files are admitted
- build outputs, `.git`, and third-party trees are excluded
- OpenAI traffic requires a local `chatgptAPI.txt` key file and is only used on explicit command
