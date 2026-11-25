// Only `memory` heading an item of the global_settings photons block is the photon cap.
#version 4.0;
#declare memory = 3;
#declare Cap = 1;
global_settings { photons { method 2 quality memory } }
global_settings { photons { method 2 count memory } }
global_settings { photons { method 2 memory 4 } subsurface { memory 30 } }
global_settings { photons { method 2 #if (Cap) memory 4 #end } }
global_settings { photons { method 2 #if (Cap) memory 4 #else memory 2 #end } }
sphere { 0, 1 photons { target memory } }
sphere { 0, 1 photons { memory 4 } }
