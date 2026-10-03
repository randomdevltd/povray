// Shrinks a shape by the fraction K about the centre of its bounding box, for a portal a hair inside its glass.
#macro Inset(Shape, K)
    #local C = (min_extent(Shape) + max_extent(Shape)) / 2;
    object { Shape translate -C scale 1 - K translate C }
#end
