# Neuralis Corpus Domains — Expanded Allowlist

This is a curated, high-signal domain list for Neuralis encoder ingestion, organized by category and context lane.

Safety + compliance defaults:
- Prefer open-licensed / public-domain / open-access sources.
- Respect robots.txt, Terms of Service, rate limits, and attribution requirements.
- Do not test any “AI ↔ Internet” integration without explicit human consent (per governance rules). This list is only “where to ingest from,” not permission to connect live.

---

## Lane 0 — Training-grade meta corpora (widely used building blocks)

- Common Crawl — `commoncrawl.org` (web crawl corpus; huge, heterogeneous; you still need allowlists/filters)  
  Source: https://commoncrawl.org/
- Wikipedia / Wikidata / Wiktionary dumps — `dumps.wikimedia.org` (bulk exports for offline ingestion)  
  Source: https://dumps.wikimedia.org/
- OpenAlex (CC0) — `openalex.org` (open scholarly index; CC0 per docs)  
  Sources: https://openalex.org/ | https://docs.openalex.org/
- Crossref metadata — `crossref.org` (DOI metadata; Crossref says metadata is open for reuse)  
  Source: https://www.crossref.org/services/metadata-retrieval/
- Zenodo — `zenodo.org` (open research artifacts; mixed licensing per record; filter by license)  
  Source: https://zenodo.org/
- arXiv — `arxiv.org` (papers, preprints; usage allowed for reading; consider bulk access policies)  
  Source: https://arxiv.org/
- PubMed Central (Open Access subset) — `ncbi.nlm.nih.gov/pmc/`  
  Source: https://www.ncbi.nlm.nih.gov/pmc/
- Project Gutenberg — `gutenberg.org` (public domain books; lots of fiction, but useful for language coverage)  
  Source: https://www.gutenberg.org/

---

## Lane 1 — Determinism, programming languages, compilers, systems

- LLVM — `llvm.org`
- ISO C++ (WG21 papers index references) — `open-std.org` (careful: PDFs and meeting papers, but authoritative)
- CMake — `cmake.org`
- Vulkan Registry — `khronos.org` (Vulkan specs)
- NVIDIA CUDA docs — `docs.nvidia.com`
- Microsoft Learn (Win64, toolchain specifics) — `learn.microsoft.com`
- cppreference — `en.cppreference.com` (non-official but high signal)
- Git documentation — `git-scm.com`
- POSIX / The Open Group — `opengroup.org`
- Python docs — `docs.python.org` (for scripting interfaces)

---

## Lane 2 — Physics (foundational → applied)

- NIST — `nist.gov` (constants, data, references)
- CODATA references — `physics.nist.gov` (constants)
- MIT OpenCourseWare — `ocw.mit.edu`
- Stanford Encyclopedia of Philosophy (physics/philosophy context) — `plato.stanford.edu`
- CERN — `home.cern`
- Perimeter Institute (public lectures/articles) — `perimeterinstitute.ca`
- NASA — `nasa.gov`
- ESA — `esa.int`
- Caltech library/open resources — `caltech.edu`
- Wolfram Demonstrations / MathWorld — `mathworld.wolfram.com` (use carefully; not always primary-source)

---

## Lane 3 — Chemistry, materials, crystallography

- NIST Chemistry WebBook — `webbook.nist.gov`
- Materials Project — `materialsproject.org` (API + datasets; check licensing)
- Open Crystallography Database — `crystallography.net`
- IUPAC — `iupac.org` (definitions/standards)
- Royal Society of Chemistry (open content subsets) — `rsc.org` (license filtering)
- PubChem — `pubchem.ncbi.nlm.nih.gov`

---

## Lane 4 — Biology / Anatomy / Physiology (reference-first)

- OpenStax Biology / Anatomy (open license) — `openstax.org`
- NIH (general) — `nih.gov`
- NCBI Bookshelf — `ncbi.nlm.nih.gov/books/`
- Allen Brain Atlas — `brain-map.org`
- Human Protein Atlas — `proteinatlas.org`
- Khan Academy (bio/anatomy content) — `khanacademy.org` (check usage)
- Wikipedia anatomy portals (via dumps) — `dumps.wikimedia.org`

---

## Lane 5 — Neurology / Cognitive science / Motor control

- Scholarpedia — `scholarpedia.org` (expert-written)
- PubMed (metadata + abstracts; filter) — `pubmed.ncbi.nlm.nih.gov`
- OpenNeuro — `openneuro.org` (datasets; license-filter)
- Society for Neuroscience (open resources) — `sfn.org` (filter)
- MIT OCW neural courses — `ocw.mit.edu`

---

## Lane 6 — Robotics, control, animation, simulation (intent → motion)

- ROS docs — `ros.org` / `docs.ros.org`
- IEEE Robotics (open subsets) — `ieee.org` (filter)
- Mujoco docs — `mujoco.org`
- Bullet Physics — `pybullet.org` / `bulletphysics.org`
- OpenSim — `opensim.stanford.edu`
- Blender manual (rigging/animation concepts) — `docs.blender.org`
- Godot docs (engine patterns) — `docs.godotengine.org` (for comparison patterns)

---

## Lane 7 — Governance, safety, licensing, compliance

- Creative Commons — `creativecommons.org`
- SPDX license list — `spdx.org/licenses/`
- robots.txt standard discussions — (general; implement policy, don’t treat as law)
- W3C — `w3.org` (web standards)

---

## Lane 8 — High-signal reference encyclopedias / dictionaries

- Wiktionary (dumps) — `dumps.wikimedia.org`
- DictionaryAPI-like open sources (if any are added later; keep strict license checks)
- Etymology references (filter) — (keep minimal; prefer Wiktionary)

---

## Lane 9 — Optional: curated fiction / narrative datasets (language richness)

- Project Gutenberg — `gutenberg.org`
- Standard Ebooks — `standardebooks.org` (public domain; clean formatting)
- LibriVox (audio; if doing audio lanes) — `librivox.org`

---

## Notes
- Treat “Lane” as a training context lane; don’t mix lane embeddings unless explicitly bridged via coherence graph rules.
- Prefer offline dumps for determinism and repeatability during development.
- Implement strong license metadata tagging so training artifacts can be filtered or removed deterministically later.
