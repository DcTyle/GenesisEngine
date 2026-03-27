// photon_output_formats_README.md
# Photon Simulation Output Formats

## Trajectory Log
- `photon_packet_trajectory_sample.csv` / `.json`: Per-packet, per-timestep log of position, phase, amplitude, frequency, coupling, inertia, and 6DoF tensor.

## Lattice 6DoF Tensor
- `photon_lattice_tensor6d_sample.csv` / `.json`: Per-lattice-site 6DoF tensor gradients and emergent features.

## Path Classification
- `photon_packet_path_classification_sample.csv` / `.json`: Shared vs. individual packet path classification, phase-lock scores, etc.

## Vector Excitations
- `photon_vector_excitation_sample.csv` / `.json`: 3D vector field and spin/OAM data for visualization.

## Tensor Gradient Glyphs
- `photon_tensor_gradient_glyph_sample.csv` / `.json`: 3x3 tensor and color for glyph rendering.

## Audio Waveform
- `photon_audio_waveform_sample.csv` / `.json`: Multi-channel audio waveform buffer per tick.

## Shader Texture
- `photon_shader_texture_sample.csv` / `.json`: RGB values per lattice cell for Vulkan shader input.
