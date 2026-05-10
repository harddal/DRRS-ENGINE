uniform sampler2D tScene;
uniform float uThreshold;

varying vec2 vTexCoord;

void main()
{
    vec3 color = texture2D(tScene, vTexCoord).rgb;

    // Soft-knee threshold: smooth ramp so bright edges don't flicker
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float knee       = uThreshold * 0.5;
    float ramp       = clamp((brightness - uThreshold + knee) / (2.0 * knee), 0.0, 1.0);
    gl_FragColor     = vec4(color * ramp, 1.0);
}
