# Genesis Engine Node Graph Native Nodes

This file is the canonical node contract for the current Genesis Engine node editor.

Implementation order is fixed:
1. document the node,
2. register it in search / spawn UI,
3. wire backend metadata.

No node should exist in the graph unless all three are done in the same pass.

## Core model

Each visible graph node is a **derived ancilla fanout anchor**. In practical editor language, that means each node represents a Fourier-carrier anchor that groups a bounded set of resonant anchors sharing one operator path.

The node graph is derived-only. It is not:
- the substrate microprocessor,
- the AI brain,
- the chat-memory cortex.

The graph may read from separate substrates:
- Chat Memory Cortex for conversation context,
- Project Work Substrate for file/repo context,
- Coherence for file/reference lookup.

## Search and placement workflow

Use either of these entry paths:
- right-click a selected carrier node in the graph,
- type in the Node Graph search box and press **Search/Spawn** or **Enter**.

Search matches all documented node metadata:
- visible label,
- lookup name,
- operator path,
- placement guidance,
- interconnect text,
- logic-stream effect,
- language hint,
- export scope,
- language rule,
- input pins,
- output pins.

Spawned nodes are attached as child carrier nodes under the selected source carrier.
Placement previews must show the selected parent/source node, the child attach relationship, and the minimal pin grammar before a node is placed.

## Search tool contract

The search tool is not optional decoration. Every node added to the graph contract must be:
- documented first,
- searchable by its documented fields,
- visible in the graph search results list,
- spawnable from either the right-click palette or the direct search/spawn path.

Search results are graph-interactive, not read-only notes:
- selecting a result previews its placement/interconnect/effect/language/export contract in the Node summary,
- double-clicking a result places that node into the graph from the current source carrier,
- the Search/Spawn button places the currently selected result when one is selected,
- export-capable results expose a **Preview Export** action in the graph so export scope and language rules are inspectable before backend materialization grows later.

The Node Graph search tool is part of the canonical authoring workflow.

Behavior rules:
- every spawnable node must be discoverable by search before it is considered implemented,
- the search index must include label, lookup name, operator path, placement, interconnect text, and logic-stream effect,
- if a search query resolves to one exact/usable result, the graph may place it directly from the search action,
- if a search query resolves to multiple results, the spawn palette must show those results and keep their metadata visible,
- the summary/inspection surface should expose the current match count and representative matches so node placement is understandable before spawning.

Implementation order still applies: documentation first, then search/UI registration, then backend metadata.

## Divergence and sequence rules

Each node carries:
- **sequence integer**: ordered execution position inside one logic stream,
- **divergence integer**: branch family identifier,
- **divergence color/intensity**: graph visualization of parallel logic.

Rules:
- parallel nodes that later reconverge may share sequence order and divergence family,
- truly divergent logic gets a new divergence id,
- deeper downstream nodes may visually diffuse in intensity toward the end node,
- these labels are advisory editor metadata and do not replace canonical substrate execution.

## Language and export rules

In the node graph, use **export** terminology instead of hydration terminology.

Export lane requirements:
- support file artifact export,
- support repo patch export,
- support whole-repo export,
- support AI language auto-suggestion,
- support per-file language override,
- lock the language where a file type or integration requires a specific language.

Examples of locked-language cases:
- HTML/web surface files,
- extensions/plugins tied to an external platform language,
- any file whose role is constrained by an integration contract.

The AI may auto-suggest language from node context and temporal coherence, but locked-language rules override the suggestion.

## Export lane metadata contract

Export-lane nodes must carry enough metadata for both search placement and later backend export handling. At minimum this includes:
- lookup name,
- placement guidance,
- interconnects,
- logic-stream effect,
- language hint,
- locked-language state.

Search and node summaries must surface those fields for export nodes so whole-repo export, patch export, and per-file language override behavior are inspectable in the graph before execution/backend wiring expands later.
The graph must also provide an in-graph export behavior preview action for export-capable nodes/results. That preview must surface scope, placement, language hint, locked-language status, and language rule, and it must remain preview-only until later backend export execution is fully wired.


## Export and language metadata

Every export-capable or language-shaping node must document four graph-visible fields:
- **language hint**: what language the AI currently expects or suggests,
- **export scope**: whether the node acts on one file, a patch, a whole repository, or only planning state,
- **language rule**: whether the language is AI-suggested, user-overridable, or locked by integration role,
- **placement guidance**: where the node sits relative to its source carrier.

UI/search requirements:
- these fields must appear in search matching,
- these fields must appear in spawn-result summaries,
- these fields must appear in selected-node inspection,
- direct single-match spawn must preserve them into backend node metadata.

Backend requirements:
- spawned nodes carry the same export/language metadata as the documented palette entry,
- ancilla-anchor semantics still apply; export nodes are fanout anchors over project/work substrate lanes,
- language locks override AI auto-suggestion whenever file type or integration role requires a fixed language.


## Minimal interconnect / pin grammar

The current graph uses a **minimal pin grammar** so node placement is authorable before full Blueprint-style wires exist.

Each node must document and carry these pin fields:
- **input pins**: bounded entry sockets the node accepts,
- **output pins**: bounded exit sockets the node emits,
- **placement parent**: which carrier or node family the node is expected to attach under,
- **placement feedback**: what sequence/divergence assignment the graph will give it when spawned.

Current pin vocabulary:
- `exec_in` / `exec_out`: ordered execution flow,
- `scalar_in` / `scalar_out`: scalar anchor values,
- `carrier_in` / `carrier_out`: Fourier/carrier tuples,
- `event_in` / `event_out`: trigger/event flow,
- `route_in` / `route_out`: dispatcher/routing flow,
- `file_in` / `file_out`: file artifact/export flow,
- `repo_in` / `repo_out`: repository/export flow,
- `lang_in` / `lang_out`: language-selection and language-policy flow.

Compatibility rules:
- pins are advisory graph grammar for now, not final execution,
- the graph should only advertise spawn/attach combinations that make sense for the current source carrier,
- node summaries must show input/output pins before placement,
- spawned nodes must preserve the documented pin contract in backend metadata.

Placement feedback rules:
- every spawn preview should identify the current parent/source node,
- every spawn preview should show the child attach relationship,
- every spawn preview should show the sequence/divergence integers the node would inherit or branch into,
- divergence coloring and sequence labels remain derived editor metadata, not substrate execution truth.


## Minimal connect / disconnect behavior

Before full Blueprint-style wires exist, the graph supports one bounded connection grammar:
- a **source carrier/node** may connect to a documented search result or spawned child only when the documented pin compatibility rules succeed,
- **Connect Selected** uses the current graph selection as the parent/source and records one derived edge from that source into the placed child,
- **Disconnect Selected** removes the derived parent/edge record from the selected spawned child,
- connect/disconnect remains editor-only advisory graph state and must not replace substrate execution.

UI/search requirements:
- search results must report whether they are connectable from the current source,
- the graph must expose a direct connect action in addition to search/spawn,
- the selected-node summary must show incoming/outgoing derived edge counts and edge labels.

Backend requirements:
- a bounded parent/child edge record must be preserved when a node is connected,
- disconnected nodes must clear that derived edge record cleanly,
- the graph may keep parent/child edge labels derived from the first compatible pin pair (for example `exec_out -> exec_in`).

## Minimal edge / wire display rules

The current graph uses a **lightweight derived wire display** rather than full Blueprint execution wires.

Required behavior:
- every placed node must expose its derived **incoming** and **outgoing** edge counts,
- the graph must render a minimal incoming/outgoing pin-stub visualization in each node row,
- a selected node must surface the first derived edge label in the summary/inspection surface,
- edge display is **derived from bounded anchor-id edge records** and must never become a second execution graph,
- connect/disconnect only updates the bounded derived edge records and the display reads back from those records.

## Native node registry

### Carrier Anchor Root :: AI write manifold
- **Lookup name**: `carrier_root`
- **Placement**: root of graph; drag-off or right-click here to search all native nodes
- **Interconnects**: scheduler_carrier, variable_carrier, function_carrier, event_carrier, dispatcher_carrier, export carriers
- **Logic-stream effect**: roots the visible ancilla fanout carriers for AI/editor writes
- **Anchor encoding**: ancilla fanout root anchor

### Scheduler Carrier f0 :: bounded schedule anchors
- **Lookup name**: `scheduler_carrier`
- **Placement**: right-click or drag-off from root/scheduler to place bounded schedule nodes
- **Interconnects**: delay_window, tick_fanout, flow_sequence
- **Logic-stream effect**: groups scheduler ancilla anchors for bounded tick fanout
- **Anchor encoding**: ancilla schedule fanout anchor

### Variable Carrier f1 :: state/variable anchors
- **Lookup name**: `variable_carrier`
- **Placement**: place under variable lane or tuple-producing nodes
- **Interconnects**: get_anchor_scalar, set_anchor_scalar, compose_carrier_tuple
- **Logic-stream effect**: groups variable/state anchors written through the same operator path
- **Anchor encoding**: ancilla variable fanout anchor

### Function Carrier f2 :: operator anchors
- **Lookup name**: `function_carrier`
- **Placement**: place under function lane or transform-capable tuple nodes
- **Interconnects**: apply_substrate_op, fourier_transform, flow_branch
- **Logic-stream effect**: groups operator/function anchors over one carrier lane
- **Anchor encoding**: ancilla operator fanout anchor

### Event Carrier f3 :: trigger anchors
- **Lookup name**: `event_carrier`
- **Placement**: place under event lane or coherence-hit carriers
- **Interconnects**: on_trigger, on_coherence_match, emit_control_packet
- **Logic-stream effect**: groups bounded trigger anchors for event flow
- **Anchor encoding**: ancilla trigger fanout anchor

### Dispatcher Carrier f4 :: routing anchors
- **Lookup name**: `dispatcher_carrier`
- **Placement**: place under dispatcher lane or packet-emitting nodes
- **Interconnects**: emit_control_packet, route_to_carrier, export_repo_patch
- **Logic-stream effect**: groups routing anchors that move results across carriers
- **Anchor encoding**: ancilla routing fanout anchor

### Project Work Substrate Carrier
- **Lookup name**: `project_work_substrate`
- **Placement**: place export nodes here; whole-repo and per-file export nodes branch from this carrier
- **Interconnects**: export_write_file, export_repo_patch, export_whole_repo, auto_language, set_file_language
- **Logic-stream effect**: surfaces project/work substrate state as an export lane
- **Anchor encoding**: ancilla project fanout anchor

### Chat Memory Cortex Partition
- **Lookup name**: `chat_memory_cortex`
- **Placement**: read-only derived carrier; does not place export/build nodes directly
- **Interconnects**: conversation context only
- **Logic-stream effect**: surfaces talk/code/sim context as a separate AI partition
- **Anchor encoding**: ancilla context partition anchor

## Spawnable native nodes

### Sequence -> Begin Tick
- **Lookup name**: `begin_tick`
- **Placement**: place by dragging off Scheduler Carrier or root
- **Interconnects**: delay_window, tick_fanout, flow_sequence
- **Logic-stream effect**: establishes a bounded per-tick entry for a schedule path
- **Anchor encoding**: ancilla schedule fanout anchor

### Gate -> Delay Window
- **Lookup name**: `delay_window`
- **Placement**: place after Begin Tick or other scheduler nodes
- **Interconnects**: begin_tick, tick_fanout, emit_control_packet
- **Logic-stream effect**: inserts a bounded delay/timing gate before later schedule work
- **Anchor encoding**: ancilla timing gate fanout anchor

### Dispatch -> Tick Fanout
- **Lookup name**: `tick_fanout`
- **Placement**: place after scheduler gates when routing to multiple bounded lanes
- **Interconnects**: begin_tick, delay_window, emit_control_packet
- **Logic-stream effect**: fans one schedule carrier into bounded downstream anchor work
- **Anchor encoding**: ancilla scheduler fanout anchor

### Variable -> Get Anchor Scalar
- **Lookup name**: `get_anchor_scalar`
- **Placement**: place under Variable Carrier or after flow sequence nodes
- **Interconnects**: set_anchor_scalar, compose_carrier_tuple, apply_substrate_op
- **Logic-stream effect**: reads one scalar-like value from the current anchor lane
- **Anchor encoding**: ancilla variable read anchor

### Variable -> Set Anchor Scalar
- **Lookup name**: `set_anchor_scalar`
- **Placement**: place under Variable Carrier when actuating state or export payload fields
- **Interconnects**: get_anchor_scalar, compose_carrier_tuple, emit_control_packet
- **Logic-stream effect**: writes one scalar-like value into the current anchor lane
- **Anchor encoding**: ancilla variable write anchor

### Vector -> Compose Carrier Tuple
- **Lookup name**: `compose_carrier_tuple`
- **Placement**: place after Get/Set Anchor Scalar before transforms or export
- **Interconnects**: get_anchor_scalar, fourier_transform, emit_control_packet
- **Logic-stream effect**: packs bounded values into one carrier/vector tuple
- **Anchor encoding**: ancilla tuple-pack fanout anchor

### Operator -> Apply Substrate Op
- **Lookup name**: `apply_substrate_op`
- **Placement**: place under Function Carrier or downstream of tuple/trigger nodes
- **Interconnects**: get_anchor_scalar, fourier_transform, emit_control_packet
- **Logic-stream effect**: applies one bounded substrate-side operator over the current lane
- **Anchor encoding**: ancilla operator fanout anchor

### Carrier -> Fourier Transform
- **Lookup name**: `fourier_transform`
- **Placement**: place after tuple/vector nodes or before export nodes
- **Interconnects**: compose_carrier_tuple, export_write_file, export_whole_repo
- **Logic-stream effect**: derives a Fourier/carrier representation for downstream routing or export
- **Anchor encoding**: ancilla transform fanout anchor over a carrier-frequency path

### Event -> On Trigger
- **Lookup name**: `on_trigger`
- **Placement**: place under Event Carrier or event-derived child carriers
- **Interconnects**: apply_substrate_op, emit_control_packet, flow_branch
- **Logic-stream effect**: introduces a bounded trigger source into the lane
- **Anchor encoding**: ancilla trigger fanout anchor

### Event -> On Coherence Match
- **Lookup name**: `on_coherence_match`
- **Placement**: place under Event Carrier or coherence-hit carriers
- **Interconnects**: route_to_carrier, export_repo_patch, flow_branch
- **Logic-stream effect**: fires when a coherence match is relevant to the current source
- **Anchor encoding**: ancilla trigger fanout anchor driven by the coherence substrate

### Dispatch -> Emit Control Packet
- **Lookup name**: `emit_control_packet`
- **Placement**: place under Dispatcher Carrier or after trigger/tick nodes
- **Interconnects**: tick_fanout, on_trigger, route_to_carrier
- **Logic-stream effect**: emits one bounded runtime/editor control packet into the canonical packet path
- **Anchor encoding**: ancilla dispatch fanout anchor

### Dispatch -> Route To Carrier
- **Lookup name**: `route_to_carrier`
- **Placement**: place under Dispatcher Carrier when reconverging or retargeting lanes
- **Interconnects**: emit_control_packet, export_repo_patch, export_whole_repo
- **Logic-stream effect**: routes output into another carrier lane without becoming a second authority
- **Anchor encoding**: ancilla routing fanout anchor

### Export -> Write File Artifact
- **Lookup name**: `export_write_file`
- **Placement**: place under Project Work Substrate Carrier or export child lanes
- **Interconnects**: fourier_transform, export_repo_patch, set_file_language
- **Logic-stream effect**: exports one file artifact from the linked project substrate
- **Language hint**: auto from node context unless locked by file type
- **Export scope**: single file artifact
- **Language rule**: AI auto-suggest by default; locked integrations override
- **Anchor encoding**: ancilla export fanout anchor

### Export -> Materialize Repo Patch
- **Lookup name**: `export_repo_patch`
- **Placement**: place under Project Work Substrate Carrier when bundling multiple file changes
- **Interconnects**: export_write_file, coherence hints, route_to_carrier
- **Logic-stream effect**: assembles an exportable repo diff from the project work substrate
- **Language hint**: patch bundle; language derives per file
- **Export scope**: repo patch
- **Language rule**: per-file language with lock-aware override rules
- **Anchor encoding**: ancilla export fanout anchor

### Export -> Whole Repo Bundle
- **Lookup name**: `export_whole_repo`
- **Placement**: place under Project Work Substrate Carrier when materializing a full repository export
- **Interconnects**: fourier_transform, auto_language, set_file_language
- **Logic-stream effect**: exports the whole repo from the linked project substrate
- **Language hint**: mixed per-file language; locked file roles stay fixed
- **Export scope**: whole repository
- **Language rule**: AI auto-suggest per file; user override only when unlocked
- **Anchor encoding**: ancilla whole-repo export fanout anchor

### Language -> Auto Suggest
- **Lookup name**: `auto_language`
- **Placement**: place before export nodes when you want AI-selected language routing
- **Interconnects**: export_write_file, export_whole_repo, set_file_language
- **Logic-stream effect**: uses node context and temporal coherence to auto-suggest per-file language where not locked by file type
- **Language hint**: AI-suggested per-file language
- **Export scope**: language planning
- **Language rule**: temporal coherence picks language unless file role is locked
- **Anchor encoding**: ancilla language-selection fanout anchor

### Language -> Set File Language
- **Lookup name**: `set_file_language`
- **Placement**: place immediately upstream of export nodes for user-selected file language
- **Interconnects**: export_write_file, export_whole_repo, auto_language
- **Logic-stream effect**: lets the user override per-file language unless the file type is constrained by platform/integration language rules
- **Language hint**: user-selected file language unless constrained by integration contract
- **Export scope**: language override
- **Language rule**: user may override only unlocked file roles
- **Anchor encoding**: ancilla language-selection fanout anchor

### Flow -> Branch
- **Lookup name**: `flow_branch`
- **Placement**: place after decision/trigger nodes when logic will not reconverge cleanly
- **Interconnects**: all lane nodes
- **Logic-stream effect**: creates a bounded divergent branch
- **Anchor encoding**: ancilla branch fanout anchor

### Flow -> Sequence
- **Lookup name**: `flow_sequence`
- **Placement**: place between nodes that share one ordered logic stream
- **Interconnects**: all lane nodes
- **Logic-stream effect**: keeps multiple nodes in one ordered non-divergent stream
- **Anchor encoding**: ancilla sequence fanout anchor

## Coherence-derived hints

Spawn results may also include `Coherence -> <path>` entries.

- **Lookup name**: exact repo-relative path
- **Placement**: spawn from the current selected carrier when you want file/repo-aware context
- **Interconnects**: export_repo_patch, route_to_carrier, review references
- **Logic-stream effect**: injects a coherence-backed target into the current logic stream
- **Anchor encoding**: ancilla coherence target anchor

## Current limits

The current graph supports:
- search-aware spawn palette,
- source-lane-aware native node list,
- coherence-driven suggestions,
- persistent spawned child nodes,
- sequence/divergence metadata,
- export/language node grammar.

It does not yet provide full pin/wire execution or graph compilation. Those must arrive through the canonical substrate/runtime path, not via a second execution system.

## Pass-5 contract extension: explicit pin choice and lockstep metadata

The current native-node contract now requires one additional compact metadata field on every spawnable node family:
- **doc key**: a stable lookup/documentation key, normally equal to the node lookup name.

Lockstep rules:
- the doc key must be searchable from the node search surface,
- the selected-node summary must surface the doc key,
- backend node metadata must preserve the same doc key after spawn/connect,
- the node summary must report whether docs/search/backend lockstep is ready for the selected node or search result.

Explicit pin-choice behavior now extends the minimal pin grammar:
- the graph keeps one explicit **selected source output pin** for the current carrier/node,
- the graph keeps one explicit **selected target input pin** for the active search result,
- pre-commit connection previews must prefer the chosen pin pair when it is valid,
- derived edge labels must use the actual chosen pin pair when a valid pair exists,
- node/viewport summaries should surface the chosen pair.

Current editor shortcuts for explicit pin choice:
- **Alt+[ / Alt+]** cycles the selected source output pin,
- **Ctrl+[ / Ctrl+]** cycles the selected target input pin.

These shortcuts are editor-only derived authoring aids. They do not create a second execution graph and do not override substrate authority.


## Pass 8 continuation and runtime/editor audit addendum

Whole-repo export is not just a vague preview label. When the `export_whole_repo` lane is previewed, the backend must stage a bounded whole-repo continuation object tied to the currently linked inherited repository line. That staged object must surface:

- an operation label,
- a continuation summary stating that the action remains preview-first and bounded,
- a runtime/editor audit summary showing whether the staged target set includes editor-facing paths,
- and the same export dialect in graph/UI text (do not regress to hydration wording where export/apply is the correct operator-facing language).

This audit remains advisory/derived. It does not grant editor panels authority over runtime packaging, and it does not collapse editor builds and runtime builds into one mushy blob.
