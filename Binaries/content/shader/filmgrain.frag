// Film grain — per-frame hash noise, simulating CRT/analog film texture.

uniform sampler2D tScene;
uniform float uGrainStrength; // 0.02-0.05 for subtle grain
uniform float uTime;          // seconds (used to vary grain each frame)

varying vec2 vTexCoord;

float hash(vec2 p, float t)
{
    return fract(sin(dot(p * 127.1 + t * 311.7, vec2(311.7, 127.1))) * 43758.5453);
}

void main()
{
    vec3 c     = texture2D(tScene, vTexCoord).rgb;
    float grain = hash(vTexCoord, floor(uTime * 24.0));
    c += (grain - 0.5) * uGrainStrength;
    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
