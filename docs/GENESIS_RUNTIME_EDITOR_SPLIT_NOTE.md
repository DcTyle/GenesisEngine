# Genesis Runtime / Editor Split Note

On the active reset continuation line, runtime and editor packaging are now made explicit in the build/install surface.

Implemented here:
- a generated runtime/editor split manifest in the Vulkan app build directory
- explicit install components for runtime and editor app artifacts
- continued default behavior of installing the runtime target by default and the editor target only when `GENESIS_INSTALL_EDITOR_APP=ON`

This keeps runtime packaging readable as editor systems mature, without introducing a second build graph or a compatibility shim circus.
