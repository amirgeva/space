#version 430 core

in vec2 vNoiseUv;
in vec2 vQuadUv;

uniform sampler2D uNoise; // sRGB texture, sampled as linear

out vec4 fragColor;

void main()
{
    vec3 color = texture(uNoise, vNoiseUv).rgb;

    // Fade out towards the edges, a little brighter right at the rim where the field meets its emitter
    vec2 edge = min(vQuadUv, 1.0 - vQuadUv);
    float d = min(edge.x, edge.y);
    float fade = smoothstep(0.0, 0.12, d);
    float rim = 1.0 - smoothstep(0.0, 0.02, d);
    color = color * (0.25 + 0.75 * fade) + vec3(0.15, 0.35, 0.8) * rim;

    // Added on top of the scene (additive blending), so output in display (gamma) space
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
