shader_type spatial;
render_mode cull_back,blend_mix,depth_draw_opaque,diffuse_burley,specular_schlick_ggx;

#include "res://addons/terrabrush/Resources/Shaders/heightmap_clipmap_shader_include.gdshaderinc"

varying vec3 _worldVertex;
varying vec3 _triplanarPos;
varying vec3 _triplanarPowerNormal;

void vertex() {
	_worldVertex = vec3(0);
	_triplanarPos = vec3(0);
	_triplanarPowerNormal = vec3(0);
	calculateHeightmapVertex(
		MODEL_MATRIX,
		COLOR,
		VERTEX,
		NORMAL,
		_worldVertex,
		_triplanarPos,
		_triplanarPowerNormal
	);
}

void fragment() {
	calculateHeightmapFragment(
		_worldVertex,
		_triplanarPos,
		_triplanarPowerNormal,
		ALBEDO,
		NORMAL_MAP,
		METALLIC,
		SPECULAR,
		ROUGHNESS
	);

	if (UseFlatShading) {
		// Compute per-face normal from view-space vertex position derivatives.
		// VERTEX is in view space in the fragment shader, matching NORMAL's expected space.
		NORMAL = normalize(cross(dFdy(VERTEX), dFdx(VERTEX)));
		// Reset normal map to neutral so texture normals don't override the flat normal.
		NORMAL_MAP = vec3(0.5, 0.5, 1.0);
	}
}