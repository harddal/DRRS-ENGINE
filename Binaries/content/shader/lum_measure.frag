uniform sampler2D tScene;

void main()
{
    // Log-average luminance over an 8x8 grid of the scene
    float logLumSum = 0.0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
        {
            vec3  c   = texture2D(tScene, vec2((float(x) + 0.5) / 8.0, (float(y) + 0.5) / 8.0)).rgb;
            float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
            logLumSum += log2(max(lum, 0.00001));
        }

    float avgLogLum = logLumSum / 64.0;

    // Encode log2 luminance [-10, 10] into [0, 1] for A8R8G8B8 storage.
    // Covers the luminance range [2^-10, 2^10] ~ [0.001, 1024].
    float encoded = clamp((avgLogLum + 10.0) / 20.0, 0.0, 1.0);

    gl_FragColor = vec4(encoded, encoded, encoded, 1.0);
}
