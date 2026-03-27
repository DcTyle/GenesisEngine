#include <cuda_runtime.h>
#include "photon_confinement.hpp"

// CUDA kernel for photon confinement step
__global__ void photon_confinement_kernel(PhotonConfinement::PhotonState* states, int n, float dt, float field_strength) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    // Simple placeholder: update position by momentum, apply field_strength as a damping factor
    states[i].x += states[i].px * dt;
    states[i].y += states[i].py * dt;
    states[i].z += states[i].pz * dt;
    // Dampen momentum to simulate confinement
    states[i].px *= (1.0f - field_strength * dt);
    states[i].py *= (1.0f - field_strength * dt);
    states[i].pz *= (1.0f - field_strength * dt);
    states[i].time += dt;
}

// Host launcher for the kernel
void photon_confinement_kernel_launcher(PhotonConfinement::PhotonState* states, int n, float dt, float field_strength) {
    PhotonConfinement::PhotonState* d_states = nullptr;
    cudaMalloc(&d_states, n * sizeof(PhotonConfinement::PhotonState));
    cudaMemcpy(d_states, states, n * sizeof(PhotonConfinement::PhotonState), cudaMemcpyHostToDevice);
    int threads = 128;
    int blocks = (n + threads - 1) / threads;
    photon_confinement_kernel<<<blocks, threads>>>(d_states, n, dt, field_strength);
    cudaMemcpy(states, d_states, n * sizeof(PhotonConfinement::PhotonState), cudaMemcpyDeviceToHost);
    cudaFree(d_states);
}
