// FXAA 3.11 -- Quality preset. GLSL 1.10 (no #version): fxaa.vert is shared by
// every post-process pass and is 1.10, and mixing #version across a single
// program fails to link on some drivers.
//
// Replaces the old "console"/FXAA-II variant, which had two structural problems:
//   * no edge threshold, so the directional blur ran on EVERY pixel and softened
//     flat texture detail across the whole frame;
//   * no edge-search loop, so long near-horizontal edges only ever got a 3-tap
//     smear along a direction clamped to +-8 texels.
//
// This pass runs AFTER tonemap, so its input is gamma-encoded LDR -- which is
// what the FXAA luma model assumes. Do not move it ahead of the tonemapper.

uniform sampler2D tScene;
uniform vec2      uRcpFrame;   // 1.0 / render-target size, in texels

varying vec2 vTexCoord;

// Local contrast below this leaves the pixel untouched. MIN is an absolute floor
// for dark regions, where the relative test alone is meaningless.
#define EDGE_THRESHOLD_MIN  0.0312
#define EDGE_THRESHOLD_MAX  0.125

// Edge-search steps in each direction. 12 reaches ~28 texels at the step sizes
// in qualityStep(), enough for everything short of a near-horizon line.
#define ITERATIONS          12

// How much sub-pixel (thin-feature) blur is allowed through. 0 = off, 1 = full.
// Higher trades fine detail for less crawling on wires, railings and foliage.
#define SUBPIXEL_QUALITY    0.75

// Gamma-space luma. The sqrt compression is what makes the threshold constants
// above behave consistently between the darks and the highlights.
float rgb2luma(vec3 rgb)
{
    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

// Search step size per iteration -- accelerates away from the pixel so a long
// edge is bracketed in few taps. Written as branches because GLSL 1.10 cannot
// index a const array with a non-constant index.
float qualityStep(int i)
{
    if (i < 5)   return 1.0;
    if (i == 5)  return 1.5;
    if (i < 10)  return 2.0;
    if (i == 10) return 4.0;
    return 8.0;
}

void main()
{
    vec2 uv = vTexCoord;
    vec3 colorCenter = texture2D(tScene, uv).rgb;

    float lumaCenter = rgb2luma(colorCenter);
    float lumaDown   = rgb2luma(texture2D(tScene, uv + vec2( 0.0,         -uRcpFrame.y)).rgb);
    float lumaUp     = rgb2luma(texture2D(tScene, uv + vec2( 0.0,          uRcpFrame.y)).rgb);
    float lumaLeft   = rgb2luma(texture2D(tScene, uv + vec2(-uRcpFrame.x,  0.0        )).rgb);
    float lumaRight  = rgb2luma(texture2D(tScene, uv + vec2( uRcpFrame.x,  0.0        )).rgb);

    float lumaMin   = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    float lumaMax   = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;

    // Flat enough to leave alone. This early-out is why the new version is both
    // sharper and cheaper than the old one -- most of the frame stops here.
    if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX))
    {
        gl_FragColor = vec4(colorCenter, 1.0);
        return;
    }

    float lumaDownLeft  = rgb2luma(texture2D(tScene, uv + vec2(-uRcpFrame.x, -uRcpFrame.y)).rgb);
    float lumaUpRight   = rgb2luma(texture2D(tScene, uv + vec2( uRcpFrame.x,  uRcpFrame.y)).rgb);
    float lumaUpLeft    = rgb2luma(texture2D(tScene, uv + vec2(-uRcpFrame.x,  uRcpFrame.y)).rgb);
    float lumaDownRight = rgb2luma(texture2D(tScene, uv + vec2( uRcpFrame.x, -uRcpFrame.y)).rgb);

    float lumaDownUp    = lumaDown + lumaUp;
    float lumaLeftRight = lumaLeft + lumaRight;

    float lumaLeftCorners  = lumaDownLeft  + lumaUpLeft;
    float lumaDownCorners  = lumaDownLeft  + lumaDownRight;
    float lumaRightCorners = lumaDownRight + lumaUpRight;
    float lumaUpCorners    = lumaUpRight   + lumaUpLeft;

    // Is the edge through this pixel horizontal or vertical? Second-derivative
    // estimate along each axis; the larger response is the edge direction.
    float edgeHorizontal = abs(-2.0 * lumaLeft   + lumaLeftCorners)
                         + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0
                         + abs(-2.0 * lumaRight  + lumaRightCorners);
    float edgeVertical   = abs(-2.0 * lumaUp     + lumaUpCorners)
                         + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0
                         + abs(-2.0 * lumaDown   + lumaDownCorners);

    bool isHorizontal = (edgeHorizontal >= edgeVertical);

    // The two neighbours across the edge, and the gradient to each.
    float luma1 = isHorizontal ? lumaDown : lumaLeft;
    float luma2 = isHorizontal ? lumaUp   : lumaRight;
    float gradient1 = luma1 - lumaCenter;
    float gradient2 = luma2 - lumaCenter;

    bool  is1Steepest    = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

    // Step half a texel toward the steeper side so the search walks along the
    // edge itself rather than along the row of pixels beside it.
    float stepLength       = isHorizontal ? uRcpFrame.y : uRcpFrame.x;
    float lumaLocalAverage = 0.0;
    if (is1Steepest)
    {
        stepLength       = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
    }
    else
    {
        lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
    }

    vec2 currentUv = uv;
    if (isHorizontal) currentUv.y += stepLength * 0.5;
    else              currentUv.x += stepLength * 0.5;

    // --- Walk the edge both ways until the luma stops matching the local average ---
    vec2 offset = isHorizontal ? vec2(uRcpFrame.x, 0.0) : vec2(0.0, uRcpFrame.y);

    vec2 uv1 = currentUv - offset;
    vec2 uv2 = currentUv + offset;

    float lumaEnd1 = rgb2luma(texture2D(tScene, uv1).rgb) - lumaLocalAverage;
    float lumaEnd2 = rgb2luma(texture2D(tScene, uv2).rgb) - lumaLocalAverage;

    bool reached1    = abs(lumaEnd1) >= gradientScaled;
    bool reached2    = abs(lumaEnd2) >= gradientScaled;
    bool reachedBoth = reached1 && reached2;

    if (!reached1) uv1 -= offset;
    if (!reached2) uv2 += offset;

    if (!reachedBoth)
    {
        for (int i = 2; i < ITERATIONS; i++)
        {
            if (!reached1) lumaEnd1 = rgb2luma(texture2D(tScene, uv1).rgb) - lumaLocalAverage;
            if (!reached2) lumaEnd2 = rgb2luma(texture2D(tScene, uv2).rgb) - lumaLocalAverage;

            reached1    = abs(lumaEnd1) >= gradientScaled;
            reached2    = abs(lumaEnd2) >= gradientScaled;
            reachedBoth = reached1 && reached2;

            if (!reached1) uv1 -= offset * qualityStep(i);
            if (!reached2) uv2 += offset * qualityStep(i);

            if (reachedBoth) break;
        }
    }

    // Position along the edge determines how far to shift the final sample.
    float distance1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float distance2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);

    bool  isDirection1  = distance1 < distance2;
    float distanceFinal = min(distance1, distance2);
    float edgeThickness = distance1 + distance2;
    float pixelOffset   = -distanceFinal / edgeThickness + 0.5;

    // Reject the offset when the luma at the nearer end sits on the same side of
    // the local average as the centre: that means the search walked ACROSS the
    // edge rather than along it, and shifting would smear unrelated pixels.
    bool  isLumaCenterSmaller = lumaCenter < lumaLocalAverage;
    bool  correctVariation    = ((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
    float finalOffset         = correctVariation ? pixelOffset : 0.0;

    // --- Sub-pixel antialiasing: features thinner than one texel, which the edge
    // search cannot bracket because they have no run to walk along ---
    float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight)
                                        + lumaLeftCorners + lumaRightCorners);
    float subPixelOffset1 = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);
    float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;
    float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * SUBPIXEL_QUALITY;

    finalOffset = max(finalOffset, subPixelOffsetFinal);

    vec2 finalUv = uv;
    if (isHorizontal) finalUv.y += finalOffset * stepLength;
    else              finalUv.x += finalOffset * stepLength;

    gl_FragColor = vec4(texture2D(tScene, finalUv).rgb, 1.0);
}
