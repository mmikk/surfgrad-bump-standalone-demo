# surfgrad-bump-standalone-demo
This demo serves as a [reference implementation](https://drive.google.com/file/d/1yIXVnMXcJg90lxKXnD93gOe9bJ0AjBA-/view?usp=sharing) for how to use the [Surface Gradient-Based Bump Mapping Framework](https://unity.com/labs/publications#surface-gradient-based-bump-mapping-framework-2019) in both simple and very complex scenarios.
The purpose of this framework is to provide a uniform approach to compositing bump maps correctly and works for just about every imaginable type of bump mapping:
Tangent space normal maps (incl. when using multiple sets of texture coordinates), Height Maps, object space normal maps, 3D bump maps such as triplanar projection, projective decals **(box/cone/sphere)**, procedural 3D noise such as perlin, worley, ...

An additional feature of this framework is post-resolve bump mapping which allows us to bump map a surface which has already been bump mapped once. An example of this is shown here.
<img src="https://github.com/mmikk/mmikk.github.io/blob/master/pictures/surfgrad_demo/pom_details_combined.png" alt="Detail Map on POM surface" />

This example shows two parallax occlusion mapped **(POM)** surfaces. The new surface normal is evaluated directly from the height map itself, used with POM, which removes the need for the artist to have to guess the right scale factor between the height map and a corresponding normal map. The example on the left shows a detail bump map applied to the new virtual surface as post-resolve bump mapping where the correct mip level is determined analytically to account for displacement and 2x2 thread divergence.
In the example on the right the post-resolve details are created with a procedural 3D bump map [dented](http://web.engr.oregonstate.edu/~mjb/prman/dented.sl). In our implementation frequency clamping is used to aleviate aliasing since mip mapping isn't possible for a procedural texture.

<img src="https://github.com/mmikk/mmikk.github.io/blob/master/pictures/surfgrad_demo/pirate_combined.png" alt="multiple sets of UV" />
This example shows a low polygonal mesh with a baked normal map assigned to the primary set of texture coordinates. A separate normal map of generic tileable detail is added on top. In the image on the left the detail normal map is assigned to the primary set of texture coordinates which appears wrong. For the image on the right the details are assigned to the secondary set of texture coordinates which allows the tiling direction to align with the shoulder strap. Note that there is no precomputed vertex tangent space for the secondary set of texture coordinates. Furthermore, we are able to composite bump contributions correctly when using multiple sets of texture coordinates including the ability to composite with object space normal maps.

<img src="https://github.com/mmikk/mmikk.github.io/blob/master/pictures/surfgrad_demo/decal_projectors.png" alt="Decal Projectors" width="800"/>

As shown in this picture three different decal volume projectors are implemented in our demo (box/cone/sphere). In the case of a sphere the bump map is a cube normal map.
Mip mapping is achieved by calculating the screen-space derivatives of the projected texture coordinate for each case analytically which works with POM too.
Correct compositing is achieved by generating the surface gradient which allows us to scale, accumulate and blend the same as for any other form of bump map.

 
