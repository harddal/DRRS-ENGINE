// Color grading — saturation boost, brightness lift, and multiplicative tint.
// Inserted after tonemapping so it operates on LDR values [0,1].

uniform sampler2D tScene;
uniform float uSaturation; // 1.0 = unchanged; >1 = boost; <1 = desaturate
uniform float uBrightness; // additive mid-tone lift (0.0 = no change)
uniform vec3  uColorTint;  // multiplicative RGB tint (1,1,1 = no tint)

varying vec2 vTexCoord;

void main()
{
    vec3 c    = texture2D(tScene, vTexCoord).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(lum), c, uSaturation) * uColorTint + vec3(uBrightness);
    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
