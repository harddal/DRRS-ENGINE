uniform sampler2D tScene;
uniform float     uExposure;
uniform float     uWhitePoint;

varying vec2 vTexCoord;

vec3 Uncharted2Tonemap(vec3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

void main()
{
    vec3 color = texture2D(tScene, vTexCoord).rgb;

    color = Uncharted2Tonemap(color * uExposure) / Uncharted2Tonemap(vec3(uWhitePoint));

    // Linear -> sRGB gamma
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));

    gl_FragColor = vec4(color, 1.0);
}
