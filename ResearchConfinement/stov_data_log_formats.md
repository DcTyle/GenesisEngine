# STOV Data Log Format Example (CSV)
x,y,z,phase,oam_density,winding_number
0,0,0,0.12,0.002,1
0,0,1,0.15,-0.001,-1
...

# STOV Data Log Format Example (JSON)
[
  {"x":0, "y":0, "z":0, "phase":0.12, "oam_density":0.002, "winding_number":1},
  {"x":0, "y":0, "z":1, "phase":0.15, "oam_density":-0.001, "winding_number":-1}
]

# Trajectory Log (CSV)
packet_id,timestep,x,y,z,theta,amplitude,freq_x,freq_y,freq_z,phase_coupling,temporal_inertia,curvature,coherence,flux
1,0,0.0,0.0,0.0,0.0,1.0,0.5,0.5,0.5,0.0,0.0,0.0,1.0,0.0
...

# Vector Excitation Log (CSV)
x,y,z,vec_x,vec_y,vec_z,spin_x,spin_y,spin_z,oam_twist
0,0,0,0.1,0.0,0.0,0.0,0.0,1.0,0.002
...

# Audio Buffer (CSV)
timestep,channel_0,channel_1,channel_2,channel_3
0,0.01,0.02,0.00,0.01
...

# Shader Texture (CSV)
x,y,z,r,g,b
0,0,0,0.8,0.2,0.5
...
