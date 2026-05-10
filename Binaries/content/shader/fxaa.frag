uniform sampler2D tScene;
uniform vec2 uRcpFrame;

varying vec2 vTexCoord;

#define FXAA_REDUCE_MIN   (1.0 / 128.0)
#define FXAA_REDUCE_MUL   (1.0 / 8.0)
#define FXAA_SPAN_MAX     8.0

void main()
{
    vec2 uv = vTexCoord;

    vec3 luma = vec3(0.299, 0.587, 0.114);

    float lumaNW = dot(texture2D(tScene, uv + vec2(-1.0, -1.0) * uRcpFrame).rgb, luma);
    float lumaNE = dot(texture2D(tScene, uv + vec2( 1.0, -1.0) * uRcpFrame).rgb, luma);
    float lumaSW = dot(texture2D(tScene, uv + vec2(-1.0,  1.0) * uRcpFrame).rgb, luma);
    float lumaSE = dot(texture2D(tScene, uv + vec2( 1.0,  1.0) * uRcpFrame).rgb, luma);
    float lumaM  = dot(texture2D(tScene, uv).rgb, luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * uRcpFrame;

    vec4 rgbA = 0.5 * (
        texture2D(tScene, uv + dir * (1.0 / 3.0 - 0.5)) +
        texture2D(tScene, uv + dir * (2.0 / 3.0 - 0.5)));

    vec4 rgbB = rgbA * 0.5 + 0.25 * (
        texture2D(tScene, uv + dir * -0.5) +
        texture2D(tScene, uv + dir *  0.5));

    float lumaB = dot(rgbB.rgb, luma);

    if (lumaB < lumaMin || lumaB > lumaMax)
        gl_FragColor = rgbA;
    else
        gl_FragColor = rgbB;
}
