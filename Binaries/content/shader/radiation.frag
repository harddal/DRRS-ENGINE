// Radiation exposure — yellow vignette, chromatic aberration, scanline flicker.
// All effects scale with uIntensity (0 = off, 1 = full).

uniform sampler2D tScene;
uniform float uIntensity;
uniform float uTime;

varying vec2 vTexCoord;

float hash(vec2 p, float t)
{
    return fract(sin(dot(p * 127.1 + t * 311.7, vec2(311.7, 127.1))) * 43758.5453);
}

void main()
{
    vec2 uv     = vTexCoord;
    vec2 center = uv - 0.5;

    // Chromatic aberration — split RGB outward from screen center.
    float aberration = uIntensity * 0.009;
    float r = texture2D(tScene, uv + center * aberration).r;
    float g = texture2D(tScene, uv).g;
    float b = texture2D(tScene, uv - center * aberration).b;
    vec3 color = vec3(r, g, b);

    // Yellow tint overlay.
    vec3 radTint = vec3(0.9, 0.75, 0.0);
    color = mix(color, color * radTint * 2.8, uIntensity * 0.45);

    // Vignette — darkens edges, grows inward with intensity.
    float vignetteDist    = length(center);
    float vignetteEdge    = mix(0.85, 0.45, uIntensity);
    float vignette        = 1.0 - smoothstep(vignetteEdge - 0.25, vignetteEdge + 0.25, vignetteDist);
    color *= mix(1.0, vignette, uIntensity * 1.4);

    // Scanline flicker — hash noise tinted yellow.
    float flicker = hash(uv, floor(uTime * 24.0)) * 0.06 * uIntensity;
    color += flicker * vec3(0.9, 0.75, 0.0);

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
