$input v_normal

#include <bgfx_shader.sh>

void main()
{
	vec3 normal = normalize(v_normal);
	vec3 light = normalize(vec3(0.35, 0.85, 0.4));
	float diffuse = max(dot(normal, light), 0.0);
	vec3 base = vec3(0.62, 0.66, 0.74);
	gl_FragColor = vec4(base * (0.25 + 0.75 * diffuse), 1.0);
}

