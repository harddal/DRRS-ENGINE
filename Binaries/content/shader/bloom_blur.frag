uniform sampler2D tBloom;
uniform vec2      uRcpFrame;
uniform vec2      uDirection; // (1,0) horizontal  (0,1) vertical

varying vec2 vTexCoord;

void main()
{
    // 9-tap separable Gaussian — step is 2 pixels for a wider bloom spread
    vec2 step = uDirection * uRcpFrame * 2.0;

    vec3 result =
        texture2D(tBloom, vTexCoord             ).rgb * 0.2270270270 +
        texture2D(tBloom, vTexCoord + step      ).rgb * 0.1945945946 +
        texture2D(tBloom, vTexCoord - step      ).rgb * 0.1945945946 +
        texture2D(tBloom, vTexCoord + step * 2.0).rgb * 0.1216216216 +
        texture2D(tBloom, vTexCoord - step * 2.0).rgb * 0.1216216216 +
        texture2D(tBloom, vTexCoord + step * 3.0).rgb * 0.0540540541 +
        texture2D(tBloom, vTexCoord - step * 3.0).rgb * 0.0540540541 +
        texture2D(tBloom, vTexCoord + step * 4.0).rgb * 0.0162162162 +
        texture2D(tBloom, vTexCoord - step * 4.0).rgb * 0.0162162162;

    gl_FragColor = vec4(result, 1.0);
}
