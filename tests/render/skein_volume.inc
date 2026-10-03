// skein_volume.inc: SolidVolume(Object, N) integrates a closed object's volume from the crossings of N x N rays along z; SolidVolumeIn over a given extent.
#macro SolidVolume(Object, N)
  #local Lo = min_extent(Object);
  #local Hi = max_extent(Object);
  #local Cell = (Hi - Lo) / N;
  #local Sum = 0;
  #for (I, 0, N - 1)
    #for (J, 0, N - 1)
      #local O = <Lo.x + (I + 0.5)*Cell.x, Lo.y + (J + 0.5)*Cell.y, Lo.z - 1>;
      #local Normal = z;
      #local Entry = 0;
      #local Inside = 0;
      #while (vlength(Normal) > 0)
        #local P = trace(Object, O, z, Normal);
        #if (vlength(Normal) > 0)
          #if (Inside)
            #local Sum = Sum + P.z - Entry;
          #else
            #local Entry = P.z;
          #end
          #local Inside = 1 - Inside;
          #local O = P + 1e-7*z;
        #end
      #end
    #end
  #end
  (Sum * Cell.x * Cell.y)
#end

// SolidVolumeIn(Object, Lo, Hi, N): the same over the given x, y extent instead of the object's bounding box.
#macro SolidVolumeIn(Object, Lo, Hi, N)
  #local Cell = (Hi - Lo) / N;
  #local Sum = 0;
  #for (I, 0, N - 1)
    #for (J, 0, N - 1)
      #local O = <Lo.x + (I + 0.5)*Cell.x, Lo.y + (J + 0.5)*Cell.y, min_extent(Object).z - 1>;
      #local Normal = z;
      #local Entry = 0;
      #local Inside = 0;
      #while (vlength(Normal) > 0)
        #local P = trace(Object, O, z, Normal);
        #if (vlength(Normal) > 0)
          #if (Inside)
            #local Sum = Sum + P.z - Entry;
          #else
            #local Entry = P.z;
          #end
          #local Inside = 1 - Inside;
          #local O = P + 1e-7*z;
        #end
      #end
    #end
  #end
  (Sum * Cell.x * Cell.y)
#end
