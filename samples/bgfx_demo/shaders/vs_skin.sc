$input a_position, a_normal, a_indices, a_weight
$output v_normal

#include <bgfx_shader.sh>

uniform sampler2D u_bonesTex;
uniform vec4 u_boneCount;

mat4 boneMatrix(float index)
{
	float v = (index + 0.5) / u_boneCount.x;
	vec4 row0 = texture2DLod(u_bonesTex, vec2(0.125, v), 0.0);
	vec4 row1 = texture2DLod(u_bonesTex, vec2(0.375, v), 0.0);
	vec4 row2 = texture2DLod(u_bonesTex, vec2(0.625, v), 0.0);
	vec4 row3 = texture2DLod(u_bonesTex, vec2(0.875, v), 0.0);
	return mat4(row0, row1, row2, row3);
}

void main()
{
	mat4 skin = boneMatrix(float(a_indices.x)) * a_weight.x
	          + boneMatrix(float(a_indices.y)) * a_weight.y
	          + boneMatrix(float(a_indices.z)) * a_weight.z
	          + boneMatrix(float(a_indices.w)) * a_weight.w;

	vec4 worldPos = mul(skin, vec4(a_position, 1.0));
	gl_Position = mul(u_modelViewProj, worldPos);
	v_normal = normalize(mul(skin, vec4(a_normal, 0.0)).xyz);
}
