#local Width = 0.1;
#declare Post_Width = Width * 2;
#declare Post_Count = 0;
#macro Post(H)
  #declare Post_Count = Post_Count + 1;
  cylinder { 0, y * H, Post_Width }
#end
