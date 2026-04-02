#version 330

in vec3 fragPos;
in vec3 fragNormal;

uniform vec4 cor_base;
uniform vec4 cor_secundaria;
uniform float escala;
uniform float contraste;

out vec4 finalColor;

vec2 AxisProject(vec3 pos, vec3 normal)
{
    vec3 n = normal;
    float nLen = dot(n, n);
    if (nLen < 0.0001)
        n = vec3(0.0, 1.0, 0.0);
    vec3 an = abs(n);

    if (an.x >= an.y && an.x >= an.z)
        return pos.yz;
    if (an.y >= an.x && an.y >= an.z)
        return pos.xz;
    return pos.xy;
}

void main()
{
    vec2 uv = AxisProject(fragPos, fragNormal) * max(escala, 0.0001);

    float checker = mod(floor(uv.x) + floor(uv.y), 2.0);
    vec3 base = cor_base.rgb;
    vec3 secondary = cor_secundaria.rgb;
    vec3 checkColor = mix(base, secondary, checker);

    vec3 color = checkColor;
    color = clamp((color - 0.5) * contraste + 0.5, 0.0, 1.0);

    finalColor = vec4(color, 1.0);
}
