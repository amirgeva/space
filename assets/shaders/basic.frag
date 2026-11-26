#version 430 core

struct Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    vec3 emissive;
    float shininess;
    sampler2D diffuseMap;  // sRGB texture, sampled as linear
    sampler2D emissiveMap; // sRGB texture, multiplied by emissive
};

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

uniform Material uMaterial;
uniform vec3 uLightDir;   // direction the sunlight travels
uniform vec3 uCameraPos;

out vec4 fragColor;

vec3 acesFilm(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec3 albedo = texture(uMaterial.diffuseMap, vTexCoord).rgb * uMaterial.diffuse;

    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing)
        n = -n;
    vec3 v = normalize(uCameraPos - vWorldPos);

    // Key light: the sun
    vec3 l = normalize(-uLightDir);
    vec3 h = normalize(l + v);
    vec3 sun = vec3(1.0, 0.97, 0.92) * 2.2;
    vec3 color = albedo * max(dot(n, l), 0.0) * sun;
    color += pow(max(dot(n, h), 0.0), uMaterial.shininess) * uMaterial.specular * sun * step(0.0, dot(n, l));

    // Cool fill light from below and opposite the sun (planet bounce), plus hemispheric ambient
    vec3 fillDir = normalize(vec3(0.45, -0.6, 0.65));
    color += albedo * max(dot(n, fillDir), 0.0) * vec3(0.22, 0.27, 0.36);
    color += albedo * uMaterial.ambient * mix(vec3(0.05, 0.055, 0.07), vec3(0.11, 0.115, 0.13), n.y * 0.5 + 0.5);

    color += uMaterial.emissive * texture(uMaterial.emissiveMap, vTexCoord).rgb;

    color = acesFilm(color);
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
