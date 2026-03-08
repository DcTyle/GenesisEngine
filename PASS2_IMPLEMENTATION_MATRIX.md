# Genesis Engine Surgical Patch Contract — Pass 2 Implementation Matrix

Pass 2 scope, per contract:
- A4 anchor-first reporting
- A5 planner/task-graph layer
- B1 ambiguous canonical bind resolution

## File-by-file matrix

| File | Pass 2 item(s) | Pre-pass status | Surgical edit implemented | Result |
|---|---|---:|---|---:|
| `include/GE_runtime.hpp` | A4, A5 | partial / missing | Extended the canonical workflow state with one bounded plan array, anchor-binding report fields, ambiguity flags, and public mutation APIs for binding reports and plan updates. No second planner or second workflow model was added. | implemented |
| `src/GE_runtime.cpp` | A4, A5, B1 | partial / missing / partial | Added canonical plan/binding-report mutation functions; added one deterministic plan builder for single-target and emit-artifact flows; wired semantic binding decisions into `ew_synthcode_execute(...)`; surfaced rejected candidates, ambiguity level, review prudence, and ordered plan steps through the existing workflow path. | implemented |
| `include/coherence_graph.hpp` | B1 | partial | Added one canonical `SemanticPatchDecision` structure and resolver API so ambiguous semantic candidates can be ranked and explained in a single binder path. | implemented |
| `src/coherence_graph.cpp` | B1 | partial | Implemented deterministic semantic target resolution with tie-break rationale, ambiguity classification, human-review prudence flagging, and compact rejected-candidate summaries. | implemented |
| `src/code_artifact_ops.cpp` | A4, A5 | partial / missing | Wired direct `PATCH:` operations into the same canonical binding-report and bounded plan path so explicit anchor/exact/file edits now expose inspectable targeting and step ordering instead of bypassing workflow explanation. | implemented |

## Pass 2 contract notes

- The old `code_synthesizer.cpp` path was still not revived.
- Planner state lives inside the existing canonical workflow state rather than a second queue/orchestrator.
- Ambiguous bind handling now resolves through one coherence-graph decision path.
- Direct patch commands and `SYNTHCODE:` requests now both emit binding/plan signals through the same workflow state.

## Validation performed for this pass

Syntax validation run against touched C++ sources:

- `g++ -std=c++17 -fsyntax-only -Iinclude src/coherence_graph.cpp` -> passed
- `g++ -std=c++17 -fsyntax-only -Iinclude src/code_artifact_ops.cpp` -> passed
- `g++ -std=c++17 -fsyntax-only -Iinclude src/GE_runtime.cpp` -> passed
