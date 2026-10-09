#version 3.8;

#declare Cylinder = 1;
#declare Spacing = 0.02;
#declare Autostop = 0.4;
#include "photon_ring_autostop.pov"

#declare Idle = 0;
#while (Idle < 12)
  light_source { <Idle, 10, 3> color rgb 0.1 photons { refraction off reflection off } }
  #declare Idle = Idle + 1;
#end
