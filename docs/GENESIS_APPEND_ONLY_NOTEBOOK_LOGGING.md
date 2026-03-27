# Genesis Append-Only Notebook Logging

This repository now has an append-only notebook logging pipeline for build logs, install/package logs, and runtime/editor logs.

Entry points:
- `scripts/build_game_win64.bat`
- `scripts/build_editor_win64.bat`
- `scripts/install_vulkan_app_win64.bat`
- `scripts/package_vulkan_app_win64.bat`
- editor/runtime shutdown hook in `vulkan_app/src/GE_app.cpp`

Core files:
- `scripts/invoke_logged_step.ps1` captures each major step into deterministic log files under `GenesisEngineState/Logs`
- `scripts/update_research_notebook.py` appends changed logs into the notebook
- `ProjectSettings/notebook_logging.json` selects the provider and output paths
- `ResearchConfinement/docs_v4/ENGINE_BUILD_RUNTIME_LAB_NOTEBOOK.md` is the append-only notebook

Provider selection:
- `aethen`: native deterministic summarizer for log-note generation
- `copilot`: external bridge mode intended for VS Code GitHub Copilot API integration
- `hybrid`: try the Copilot bridge first and merge it with Aethen notes

Important implementation boundary:
- Repo scripts cannot directly call private VS Code extension internals on their own.
- The `copilot` mode therefore uses a bridge command configured in `ProjectSettings/notebook_logging.json` or `GENESIS_NOTEBOOK_COPILOT_BRIDGE`.
- The bridge command receives a JSON payload on stdin and must emit concise notebook bullets on stdout.

Useful environment overrides:
- `GENESIS_NOTEBOOK_PROVIDER=aethen|copilot|hybrid`
- `GENESIS_NOTEBOOK_COPILOT_BRIDGE=<command line>`

The notebook generator is append-only by design. It tracks log fingerprints in `GenesisEngineState/Logs/notebook_logger_state.json` and only appends new entries when log content changes.