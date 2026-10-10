#declare ReadSize = Height + dimension_size(Base, 1) * 0.1 + Mixed[0] * 0.1;
#macro Stack() box { <-0.2, -1, -0.2>, <0.2, Height - 1, 0.2> } #end
#macro Count() #declare Ticks = Ticks + 1; sphere { <Ticks * 0.4 - 3, -1, -2>, 0.15 } #end
