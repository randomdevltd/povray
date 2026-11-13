// A torus, a sheared torus, a surface of revolution and an ovus must report where a ray from inside them leaves, and
// the answer must not depend on the units they are modelled in: U is the object-space unit, rescaled to the same world
// size. Half the rays lie almost square to the axis. The parse stops where trace() from a point inside misses the
// solid, lands beyond the first point outside it, or lands before it with material still past the hit.
#version 4.0;
#ifndef (U) #declare U=1; #end
#declare Size=100;
#declare Shapes=array[4] {
 torus { 1.2*U,0.5*U scale Size/U },
 torus { 1.2*U,0.5*U matrix <1,0,0.5, 0.3,-1,0, 0,0,0.6, 0.2*U,0,0> scale Size/U },
 sor { 5, <0.4,-1.6>*U,<0.8,-1.2>*U,<0.4,0>*U,<0.9,1>*U,<0.2,1.4>*U scale Size/U },
 ovus { 1*U,0.6*U translate -0.4*U*y scale Size/U } }
#declare Rays=1500; #declare Steps=400; #declare R=seed(11); #declare Missed=0; #declare Late=0; #declare Early=0;
#for (Kind,0,3)
 #declare Shape=Shapes[Kind];
 #for (I,1,Rays)
  #declare From=(<rand(R),rand(R),rand(R)>-0.5)*3.4*Size;
  #declare Dir=vnormalize((<rand(R),rand(R),rand(R)>-0.5)*<1,(mod(I,2)?0.004:1),1>);
  #if (inside(Shape,From))
   #declare Step=6*Size/Steps; #declare Out=-1;
   #for (K,1,Steps) #if (Out<0) #if (!inside(Shape,From+Dir*K*Step)) #declare Out=K; #end #end #end
   #declare Normal=<0,0,0>; #declare Hit=trace(Shape,From,Dir,Normal);
   #if (vlength(Normal)=0) #declare Missed=Missed+1;
   #elseif (vlength(Hit-From)>Out*Step*(1+1e-9)) #declare Late=Late+1;
   #elseif (vlength(Hit-From)<(Out-1)*Step*(1-1e-9) & inside(Shape,Hit+Dir*Step*1e-3)) #declare Early=Early+1; #end
  #end
 #end
#end
#if (Missed+Late+Early>0) #error concat("solids modelled in units of ",str(U,0,-1),": ",str(Missed,0,0)," rays from inside found no surface, ",
 str(Late,0,0)," found it beyond the first point outside, ",str(Early,0,0)," found it inside the material") #end
global_settings { assumed_gamma 1 }
sphere { 0,1 }
