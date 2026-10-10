# The 4.0 scene language grammar reference

Generated reference for the `.pov4` scene language. Regenerate with `node tools/language/grammar/render.mjs`;
the sources of truth are the tree-sitter grammar in `libraries/tree-sitter-pov4` and the classic
parser's expect-loops (mined into `tools/language/grammar/blocks.json`). This file is for reading and
for LLMs; tools should consume `pov4.json` and `blocks.json` directly.

## Lexical words

`identifier`, `number`, `string` and `comment` are defined in the EBNF below. Reserved words are
classified by the contextual scanner as follows (a word followed by `(` that is callable parses as a
built-in call; bare value words parse as identifiers).

| class | words |
|---|---|
| keyword (block and item words) | aa_level aa_threshold absorption accuracy adaptive adc_bailout agate agate_turb albedo all all_intersections altitude always_sample ambient ambient_light angle anisotropy any aoi aperture arc_angle area_illumination area_light ascii assumed_gamma autostop average b_spline back back_filter_tags background bezier_spline bicubic_patch black_hole blend_gamma blend_mode blob blur_samples bmp bokeh bounded_by box boxed bozo brick brick_size brightness brilliance bt2020 bt709 bump_map bump_size bumps camera caustics cells charset checker circular clipped_by cmap collect color_map colour_map component composite cone confidence conic_sweep conserve_energy contained_by control0 control1 coords count crackle crand cube cubic cubic_spline cubic_wave cutaway_textures cylinder cylindrical default density density_file density_map dents df3 dictionary difference diffuse direction disc dispersion dispersion_samples dist_exp distance distance_maximum double_illuminate dtag eccentricity emission error_bound evaluate exit expand_thresholds exponent exr exterior extinction face_indices facets fade_color fade_colour fade_distance fade_power fallback falloff falloff_angle far filter_tags finish fisheye flatness flip focal_point fog fog_alt fog_offset fog_type form frequency fresnel front front_filter_tags gamma gather gif global_lights global_settings gradient granite gray_threshold grey_threshold hdr height_field hexagon hf_gray_16 hf_grey_16 hierarchy hollow hypercomplex iff image_map image_pattern importance inside_vector interior interior_texture internal interpolate intersection intervals inverse ior irid irid_wavelength isosurface isosurface_mesh jitter jpeg julia julia_fractal lambda lathe lemon leopard light_group light_source linear_spline linear_sweep load_file location look_at looks_like low_error_factor magnet major_radius mandel map_type marble material material_map matrix max_gradient max_intersections max_iteration max_sample max_trace max_trace_level maximum_reuse media media_attenuation media_interaction merge mesh mesh2 mesh_camera metallic method metric minimum_reuse mix mixed mm_per_unit mortar natural_spline near nearest_count no_bump_scale no_image no_lights no_radiosity no_reflection no_shadow noise_generator none normal normal_indices normal_map normal_vectors number_of_sides number_of_tiles number_of_waves obj object octaves offset omega omnimax once onion open optional orient orientation orthographic ovus panoramic parallel parametric pass_through pattern pavement perspective perturb pgm phase phong phong_size photon_only photons pigment pigment_map pigment_pattern planar plane png point_at polarity poly poly_wave polygon polynomial portal pot potential povm ppm precision precompute prefix premultiplied pretrace_end pretrace_start priority prism projected_through pwr quadratic_spline quadric quartic quaternion quick_color quick_colour quilted radial radiosity radiosity_size radius rainbow ramp_wave ratio reciprocal recursion_limit reflection reflection_exponent refraction refraction_angle render repeat resolution right ripples rotate roughness samples save_file sblue scale scallop_wave scattering screen sgreen shadowless sine_wave sint16be sint16le sint32be sint32le sint8 size skein skein_mesh sky sky_sphere slice slope slope_map smooth smooth_triangle solid sor spacing specular sphere sphere_sweep spherical spiral1 spiral2 spline split_union spotlight spotted square sred statistics strength sturm subsurface suffix superellipsoid sys tags target text texture texture_list texture_map tga thickness threshold tiff tightness tile2 tiles tiling tolerance toroidal torus transform translate translucency triangle triangle_wave triangular ttf turb_depth turbulence type u_steps uint16be uint16le uint8 ultra_wide_angle union unofficial up use_alpha use_color use_colour use_index user_defined utf8 uv_indices uv_mapping uv_vectors v_steps variance vertex_vectors volume_sampling warp water_level waves width wood wrinkles xyz |
| value / built-in | abs acos acosh array asc asin asinh atan atan2 atanh bitwise_and bitwise_or bitwise_xor ceil chr clock clock_on concat cos cosh datetime debug defined degrees dimension_size dimensions div error exp file_exists filter floor inside int ln log max max_extent min min_extent mod no now off on pi pow prod radians rand range seed select sin sinh sqr sqrt str strcmp strlen strlwr strupr substr sum t tan tanh tau trace u v val vaxis_rotate vcross vdot version vlength vnormalize vrotate vstr vturbulence warning x y yes z |
| colour operator | rgb rgbf rgbt rgbft srgb srgbf srgbt srgbft color colour |
| colour channel | alpha blue filter gray green grey red transmit |

## Syntax

### Statements

```ebnf
let_statement := "let" name: identifier ("=" _value | ε) ";";

global_statement := "global" name: identifier "=" _value ";";

assignment := target: (identifier | index_expression | member_expression) "=" _value ";";

function_definition := "fn" name: identifier parameters: parameters body: body;

if_statement := "if" condition: parenthesized_expression consequence: body ("else" alternative: (body | if_statement) | ε);

while_statement := "while" condition: parenthesized_expression body: body;

for_statement := "for" "(" variable: identifier ("=" start: _expression "to" end: _expression ("step" step: _expression | ε) | "in" iterable: _expression) ")" body: body;

break_statement := "break" ";";

continue_statement := "continue" ";";

return_statement := "return" (value: _expression | ε) ";";

include_statement := "include" file: _expression ";";
```

### Expressions

```ebnf
conditional_expression := condition: _expression "?" consequence: _expression ":" alternative: _expression;

binary_expression :=
  | left: _expression operator: "+" right: _expression
  | left: _expression operator: "-" right: _expression
  | left: _expression operator: "*" right: _expression
  | left: _expression operator: "/" right: _expression;

unary_expression := operator: ("-" | "+" | "!") operand: _expression;

lambda := parameters: (parameters | identifier) "=>" body: (body | _expression);

colour_expression := (operator: colour_operator value: _expression | operator: color_operator (value: _expression | ε) | channel) channel*;

call_expression := function: _postfix arguments: arguments;

index_expression := object: _postfix token("[", immediate) index: _expression "]";

member_expression := object: _postfix token(".", immediate) member: identifier;

number := /(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?/;

string := /"([^"\\\n]|\\.)*"/;

identifier := /[A-Za-z_][A-Za-z0-9_]*/;

true := "true";

false := "false";

null := "null";

vector := "<" _expression "," _expression ("," _expression)* ">";

dictionary := "{" ((pair | spread) ("," (pair | spread))* ("," | ε) | ε) "}";

parenthesized_expression := "(" _comparable ")";

block := name: (keyword | "to" as keyword) "{" _item* "}";

function_block := "function" (parameters: function_parameters | width: _size "," height: _size | ε) "{" (keyword | _comparable | ",")* "}";

array := "[" _item* "]";
```

### Other named rules

```ebnf
source_file := _item*;

parameters := "(" (parameter ("," parameter)* ("," | ε) | ε) ")";

parameter := name: identifier ("=" default: _expression | ε);

body := "{" _item* "}";

comparison_expression :=
  | left: _comparable operator: "||" right: _comparable
  | left: _comparable operator: "&&" right: _comparable
  | left: _comparable operator: "==" right: _comparable
  | left: _comparable operator: "!=" right: _comparable
  | left: _comparable operator: "<" right: _comparable
  | left: _comparable operator: "<=" right: _comparable
  | left: _comparable operator: ">" right: _comparable
  | left: _comparable operator: ">=" right: _comparable;

channel :=
  | name: colour_channel value: _expression
  | name: colour_channel;

arguments := "(" ((_expression | spread) ("," (_expression | spread))* ("," | ε) | ε) ")";

pair := (key: (identifier | string) | "[" computed: _expression "]") ":" value: _expression;

spread := "..." _expression;

function_parameters := "(" ((identifier | builtin) ("," (identifier | builtin))* | ε) ")";
```

### Lexical rules

Hidden rules (leading underscore) are folded into their parents.

```ebnf
break_statement := "break" ";";

continue_statement := "continue" ";";

true := "true";

false := "false";

null := "null";

identifier := /[A-Za-z_][A-Za-z0-9_]*/;

number := /(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?/;

string := /"([^"\\\n]|\\.)*"/;

comment := token(/\/\/[^\n]*/ | /\/\*[^*]*\*+([^/*][^*]*\*+)*\//);
```

## Block keywords

Legal reserved-word items inside each block, mined from the classic parser. Sub-blocks such as
`reflection { ... }` inside `finish` are listed per block. Blocks not listed here are not statically checked.

| block | items |
|---|---|
| `bicubic_patch` | `accuracy` `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `flatness` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `type` `u_steps` `uv_mapping` `uv_vectors` `v_steps` |
| `blob` | `bounded_by` `clipped_by` `component` `cutaway_textures` `cylinder` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `sphere` `split_union` `strength` `sturm` `tags` `texture` `threshold` `transform` `translate` `uv_mapping` |
| `box` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `bump_map` | `bmp` `bump_size` `chr` `concat` `datetime` `exr` `function` `gamma` `gif` `hdr` `iff` `interpolate` `jpeg` `map_type` `offset` `once` `pgm` `png` `pot` `ppm` `premultiplied` `repeat` `str` `strlwr` `strupr` `substr` `sys` `tga` `tiff` `use_color` `use_colour` `use_index` `vstr` |
| `camera` | `angle` `aperture` `blur_samples` `bokeh` `confidence` `cylinder` `direction` `filter_tags` `fisheye` `focal_point` `location` `look_at` `matrix` `mesh_camera` `no_radiosity` `normal` `omnimax` `orthographic` `panoramic` `perspective` `radiosity_size` `right` `rotate` `scale` `sky` `spherical` `tags` `transform` `translate` `ultra_wide_angle` `up` `user_defined` `variance` |
| `composite` | `bicubic_patch` `blob` `bounded_by` `box` `clipped_by` `composite` `cone` `cubic` `cutaway_textures` `cylinder` `difference` `disc` `double_illuminate` `dtag` `finish` `height_field` `hierarchy` `hollow` `interior` `interior_texture` `intersection` `inverse` `isosurface` `isosurface_mesh` `julia_fractal` `lathe` `lemon` `light_group` `light_source` `material` `matrix` `merge` `mesh` `mesh2` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `object` `ovus` `parametric` `photons` `pigment` `plane` `poly` `polygon` `polynomial` `portal` `prism` `quadric` `quartic` `radiosity` `rotate` `scale` `skein` `skein_mesh` `smooth_triangle` `sor` `sphere` `sphere_sweep` `split_union` `sturm` `superellipsoid` `tags` `text` `texture` `torus` `transform` `translate` `triangle` `union` `uv_mapping` |
| `cone` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `open` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `cubic` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `cylinder` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `open` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `density` | `accuracy` `agate` `agate_turb` `aoi` `average` `boxed` `bozo` `brick` `brick_size` `bump_map` `bump_size` `bumps` `cells` `checker` `color_map` `colour_map` `control0` `control1` `coords` `crackle` `cubic` `cubic_wave` `cylindrical` `density_file` `density_map` `dents` `exponent` `exterior` `facets` `form` `frequency` `function` `gradient` `granite` `hexagon` `image_map` `image_pattern` `interior` `interpolate` `julia` `lambda` `leopard` `magnet` `mandel` `marble` `matrix` `metric` `mortar` `no_bump_scale` `noise_generator` `normal_map` `number_of_sides` `number_of_tiles` `object` `octaves` `offset` `omega` `onion` `pattern` `pavement` `phase` `pigment_map` `pigment_pattern` `planar` `poly_wave` `potential` `quick_color` `quick_colour` `quilted` `radial` `ramp_wave` `repeat` `ripples` `rotate` `scale` `scallop_wave` `screen` `sine_wave` `size` `slope` `slope_map` `solid` `spherical` `spiral1` `spiral2` `spotted` `square` `texture_map` `tiling` `transform` `translate` `triangle_wave` `triangular` `turbulence` `user_defined` `uv_mapping` `warp` `waves` `wood` `wrinkles` |
| `difference` | `bicubic_patch` `blob` `bounded_by` `box` `clipped_by` `composite` `cone` `cubic` `cutaway_textures` `cylinder` `difference` `disc` `double_illuminate` `dtag` `finish` `height_field` `hierarchy` `hollow` `interior` `interior_texture` `intersection` `inverse` `isosurface` `isosurface_mesh` `julia_fractal` `lathe` `lemon` `light_group` `light_source` `material` `matrix` `merge` `mesh` `mesh2` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `object` `ovus` `parametric` `photons` `pigment` `plane` `poly` `polygon` `polynomial` `portal` `prism` `quadric` `quartic` `radiosity` `rotate` `scale` `skein` `skein_mesh` `smooth_triangle` `sor` `sphere` `sphere_sweep` `split_union` `sturm` `superellipsoid` `tags` `text` `texture` `torus` `transform` `translate` `triangle` `union` `uv_mapping` |
| `disc` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `finish` | `albedo` `ambient` `brilliance` `caustics` `conserve_energy` `crand` `diffuse` `emission` `fresnel` `ior` `irid` `metallic` `phong` `phong_size` `reflection` `reflection_exponent` `refraction` `roughness` `specular` `subsurface` `use_alpha` |
| `global_settings` | `adc_bailout` `ambient_light` `assumed_gamma` `charset` `filter_tags` `hf_gray_16` `hf_grey_16` `irid_wavelength` `max_intersections` `max_trace_level` `mm_per_unit` `noise_generator` `number_of_waves` `photons` `radiosity` `refraction_angle` `subsurface` |
| `height_field` | `bmp` `bounded_by` `chr` `clipped_by` `concat` `cutaway_textures` `datetime` `double_illuminate` `dtag` `exr` `finish` `function` `gamma` `gif` `hdr` `hierarchy` `hollow` `iff` `interior` `interior_texture` `inverse` `jpeg` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `pgm` `photons` `pigment` `png` `pot` `ppm` `premultiplied` `radiosity` `rotate` `scale` `smooth` `split_union` `str` `strlwr` `strupr` `sturm` `substr` `sys` `tags` `texture` `tga` `tiff` `transform` `translate` `uv_mapping` `vstr` `water_level` |
| `image_map` | `alpha` `bmp` `chr` `concat` `datetime` `exr` `filter` `function` `gamma` `gif` `hdr` `iff` `interpolate` `jpeg` `map_type` `offset` `once` `pgm` `png` `pot` `ppm` `premultiplied` `repeat` `str` `strlwr` `strupr` `substr` `sys` `tga` `tiff` `transmit` `use_color` `use_colour` `use_index` `vstr` |
| `interior` | `caustics` `dispersion` `dispersion_samples` `fade_color` `fade_colour` `fade_distance` `fade_power` `ior` `media` `priority` `refraction` |
| `intersection` | `bicubic_patch` `blob` `bounded_by` `box` `clipped_by` `composite` `cone` `cubic` `cutaway_textures` `cylinder` `difference` `disc` `double_illuminate` `dtag` `finish` `height_field` `hierarchy` `hollow` `interior` `interior_texture` `intersection` `inverse` `isosurface` `isosurface_mesh` `julia_fractal` `lathe` `lemon` `light_group` `light_source` `material` `matrix` `merge` `mesh` `mesh2` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `object` `ovus` `parametric` `photons` `pigment` `plane` `poly` `polygon` `polynomial` `portal` `prism` `quadric` `quartic` `radiosity` `rotate` `scale` `skein` `skein_mesh` `smooth_triangle` `sor` `sphere` `sphere_sweep` `split_union` `sturm` `superellipsoid` `tags` `text` `texture` `torus` `transform` `translate` `triangle` `union` `uv_mapping` |
| `isosurface` | `accuracy` `all_intersections` `bounded_by` `clipped_by` `contained_by` `cutaway_textures` `double_illuminate` `dtag` `evaluate` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `max_gradient` `max_trace` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `open` `photons` `pigment` `polarity` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `threshold` `transform` `translate` `uv_mapping` |
| `isosurface_mesh` | `accuracy` `all_intersections` `bounded_by` `clipped_by` `contained_by` `cutaway_textures` `double_illuminate` `dtag` `evaluate` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `max_gradient` `max_trace` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `open` `photons` `pigment` `polarity` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `threshold` `transform` `translate` `uv_mapping` |
| `julia_fractal` | `acos` `acosh` `asin` `asinh` `atan` `atanh` `bounded_by` `clipped_by` `cos` `cosh` `cube` `cutaway_textures` `double_illuminate` `dtag` `exp` `finish` `hierarchy` `hollow` `hypercomplex` `interior` `interior_texture` `inverse` `light_source` `ln` `material` `matrix` `max_iteration` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `precision` `pwr` `quaternion` `radiosity` `reciprocal` `rotate` `scale` `sin` `sinh` `slice` `split_union` `sqr` `sturm` `tags` `tan` `tanh` `texture` `transform` `translate` `uv_mapping` |
| `lathe` | `bezier_spline` `bounded_by` `clipped_by` `cubic_spline` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `linear_spline` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `quadratic_spline` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `lemon` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `open` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `light_group` | `bicubic_patch` `blob` `bounded_by` `box` `clipped_by` `composite` `cone` `cubic` `cutaway_textures` `cylinder` `difference` `disc` `double_illuminate` `dtag` `finish` `height_field` `hierarchy` `hollow` `interior` `interior_texture` `intersection` `inverse` `isosurface` `isosurface_mesh` `julia_fractal` `lathe` `lemon` `light_group` `light_source` `material` `matrix` `merge` `mesh` `mesh2` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `object` `ovus` `parametric` `photons` `pigment` `plane` `poly` `polygon` `polynomial` `portal` `prism` `quadric` `quartic` `radiosity` `rotate` `scale` `skein` `skein_mesh` `smooth_triangle` `sor` `sphere` `sphere_sweep` `split_union` `sturm` `superellipsoid` `tags` `text` `texture` `torus` `transform` `translate` `triangle` `union` `uv_mapping` |
| `light_source` | `adaptive` `area_illumination` `area_light` `bounded_by` `brightness` `circular` `clipped_by` `color_map` `colour_map` `cutaway_textures` `cylinder` `double_illuminate` `dtag` `fade_distance` `fade_power` `falloff` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `jitter` `light_source` `looks_like` `material` `matrix` `media_attenuation` `media_interaction` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `orient` `parallel` `photon_only` `photons` `pigment` `point_at` `projected_through` `radiosity` `radius` `rotate` `samples` `scale` `shadowless` `split_union` `spotlight` `sturm` `tags` `texture` `tightness` `transform` `translate` `uv_mapping` |
| `material` | `interior` `interior_texture` `matrix` `rotate` `scale` `texture` `transform` `translate` |
| `media` | `aa_level` `aa_threshold` `absorption` `collect` `confidence` `density` `emission` `intervals` `jitter` `light_source` `matrix` `method` `mix` `priority` `ratio` `refraction` `resolution` `rotate` `samples` `scale` `scattering` `transform` `translate` `variance` |
| `merge` | `bicubic_patch` `blob` `bounded_by` `box` `clipped_by` `composite` `cone` `cubic` `cutaway_textures` `cylinder` `difference` `disc` `double_illuminate` `dtag` `finish` `height_field` `hierarchy` `hollow` `interior` `interior_texture` `intersection` `inverse` `isosurface` `isosurface_mesh` `julia_fractal` `lathe` `lemon` `light_group` `light_source` `material` `matrix` `merge` `mesh` `mesh2` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `object` `ovus` `parametric` `photons` `pigment` `plane` `poly` `polygon` `polynomial` `portal` `prism` `quadric` `quartic` `radiosity` `rotate` `scale` `skein` `skein_mesh` `smooth_triangle` `sor` `sphere` `sphere_sweep` `split_union` `sturm` `superellipsoid` `tags` `text` `texture` `torus` `transform` `translate` `triangle` `union` `uv_mapping` |
| `mesh` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `inside_vector` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `smooth_triangle` `split_union` `sturm` `tags` `texture` `transform` `translate` `triangle` `uv_mapping` |
| `mesh2` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `inside_vector` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `normal_indices` `normal_vectors` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `texture_list` `transform` `translate` `uv_indices` `uv_mapping` `uv_vectors` `vertex_vectors` |
| `normal` | `accuracy` `agate` `agate_turb` `aoi` `average` `boxed` `bozo` `brick` `brick_size` `bump_map` `bump_size` `bumps` `cells` `checker` `color_map` `colour_map` `control0` `control1` `coords` `crackle` `cubic` `cubic_wave` `cylindrical` `density_file` `density_map` `dents` `exponent` `exterior` `facets` `form` `frequency` `function` `gradient` `granite` `hexagon` `image_map` `image_pattern` `interior` `interpolate` `julia` `lambda` `leopard` `magnet` `mandel` `marble` `matrix` `metric` `mortar` `no_bump_scale` `noise_generator` `normal_map` `number_of_sides` `number_of_tiles` `object` `octaves` `offset` `omega` `onion` `pattern` `pavement` `phase` `pigment_map` `pigment_pattern` `planar` `poly_wave` `potential` `quick_color` `quick_colour` `quilted` `radial` `ramp_wave` `repeat` `ripples` `rotate` `scale` `scallop_wave` `screen` `sine_wave` `size` `slope` `slope_map` `solid` `spherical` `spiral1` `spiral2` `spotted` `square` `texture_map` `tiling` `transform` `translate` `triangle_wave` `triangular` `turbulence` `user_defined` `uv_mapping` `warp` `waves` `wood` `wrinkles` |
| `object` | `bicubic_patch` `blob` `bounded_by` `box` `clipped_by` `composite` `cone` `cubic` `cutaway_textures` `cylinder` `difference` `disc` `double_illuminate` `dtag` `finish` `height_field` `hierarchy` `hollow` `interior` `interior_texture` `intersection` `inverse` `isosurface` `isosurface_mesh` `julia_fractal` `lathe` `lemon` `light_group` `light_source` `material` `matrix` `merge` `mesh` `mesh2` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `object` `ovus` `parametric` `photons` `pigment` `plane` `poly` `polygon` `polynomial` `portal` `prism` `quadric` `quartic` `radiosity` `rotate` `scale` `skein` `skein_mesh` `smooth_triangle` `sor` `sphere` `sphere_sweep` `split_union` `sturm` `superellipsoid` `tags` `text` `texture` `torus` `transform` `translate` `triangle` `union` `uv_mapping` |
| `ovus` | `bounded_by` `clipped_by` `cutaway_textures` `distance` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `precision` `radiosity` `radius` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `parametric` | `accuracy` `bounded_by` `clipped_by` `contained_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `max_gradient` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `precompute` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` `x` `y` `z` |
| `pigment` | `accuracy` `agate` `agate_turb` `aoi` `average` `boxed` `bozo` `brick` `brick_size` `bump_map` `bump_size` `bumps` `cells` `checker` `color_map` `colour_map` `control0` `control1` `coords` `crackle` `cubic` `cubic_wave` `cylindrical` `density_file` `density_map` `dents` `exponent` `exterior` `facets` `form` `frequency` `function` `gradient` `granite` `hexagon` `image_map` `image_pattern` `interior` `interpolate` `julia` `lambda` `leopard` `magnet` `mandel` `marble` `matrix` `metric` `mortar` `no_bump_scale` `noise_generator` `normal_map` `number_of_sides` `number_of_tiles` `object` `octaves` `offset` `omega` `onion` `pattern` `pavement` `phase` `pigment_map` `pigment_pattern` `planar` `poly_wave` `potential` `quick_color` `quick_colour` `quilted` `radial` `ramp_wave` `repeat` `ripples` `rotate` `scale` `scallop_wave` `screen` `sine_wave` `size` `slope` `slope_map` `solid` `spherical` `spiral1` `spiral2` `spotted` `square` `texture_map` `tiling` `transform` `translate` `triangle_wave` `triangular` `turbulence` `user_defined` `uv_mapping` `warp` `waves` `wood` `wrinkles` |
| `plane` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `poly` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `polygon` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `polynomial` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `portal` | `back` `back_filter_tags` `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `exit` `fallback` `far` `filter_tags` `finish` `front` `front_filter_tags` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `max_trace_level` `near` `no_image` `no_lights` `no_radiosity` `no_reflection` `no_shadow` `normal` `perturb` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `to` `transform` `translate` `uv_mapping` |
| `prism` | `bezier_spline` `bounded_by` `clipped_by` `conic_sweep` `cubic_spline` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `linear_spline` `linear_sweep` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `open` `photons` `pigment` `quadratic_spline` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `quadric` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `quartic` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `smooth_triangle` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `sor` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `open` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `sphere` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `sphere_sweep` | `b_spline` `bounded_by` `clipped_by` `cubic_spline` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `linear_spline` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `tolerance` `transform` `translate` `uv_mapping` |
| `superellipsoid` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `text` | `bounded_by` `charset` `clipped_by` `cmap` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `internal` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `ttf` `uv_mapping` |
| `texture` | `finish` `material_map` `matrix` `no_bump_scale` `normal` `pigment` `rotate` `scale` `tiles` `transform` `translate` |
| `torus` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `triangle` | `bounded_by` `clipped_by` `cutaway_textures` `double_illuminate` `dtag` `finish` `hierarchy` `hollow` `interior` `interior_texture` `inverse` `light_source` `material` `matrix` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `photons` `pigment` `radiosity` `rotate` `scale` `split_union` `sturm` `tags` `texture` `transform` `translate` `uv_mapping` |
| `union` | `bicubic_patch` `blob` `bounded_by` `box` `clipped_by` `composite` `cone` `cubic` `cutaway_textures` `cylinder` `difference` `disc` `double_illuminate` `dtag` `finish` `height_field` `hierarchy` `hollow` `interior` `interior_texture` `intersection` `inverse` `isosurface` `isosurface_mesh` `julia_fractal` `lathe` `lemon` `light_group` `light_source` `material` `matrix` `merge` `mesh` `mesh2` `no_image` `no_radiosity` `no_reflection` `no_shadow` `normal` `object` `ovus` `parametric` `photons` `pigment` `plane` `poly` `polygon` `polynomial` `portal` `prism` `quadric` `quartic` `radiosity` `rotate` `scale` `skein` `skein_mesh` `smooth_triangle` `sor` `sphere` `sphere_sweep` `split_union` `sturm` `superellipsoid` `tags` `text` `texture` `torus` `transform` `translate` `triangle` `union` `uv_mapping` |
