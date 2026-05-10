// Plasma Ball Fragment Shader
// Quake 3 style with manual depth testing

varying vec3 vNormal;
varying vec3 vPosition;
varying vec2 vTexCoord;
varying vec3 vViewDir;
varying float vDepth;  // Linear depth

uniform float uTime;

// Noise functions
float hash(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 x)
{
    vec3 p = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    
    return mix(
        mix(mix(hash(p + vec3(0,0,0)), hash(p + vec3(1,0,0)), f.x),
            mix(hash(p + vec3(0,1,0)), hash(p + vec3(1,1,0)), f.x), f.y),
        mix(mix(hash(p + vec3(0,0,1)), hash(p + vec3(1,0,1)), f.x),
            mix(hash(p + vec3(0,1,1)), hash(p + vec3(1,1,1)), f.x), f.y),
        f.z);
}

void main()
{
    // Plasma effect
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(vViewDir);
    float fresnel = pow(1.0 - abs(dot(normal, viewDir)), 3.0);
    
    float time = uTime * 2.0;
    float n1 = noise(vPosition * 3.0 + vec3(time, 0.0, 0.0));
    float n2 = noise(vPosition * 5.0 - vec3(0.0, time * 1.3, 0.0));
    float energyNoise = n1 * 0.6 + n2 * 0.4;
    
    float pulse = 0.8 + 0.2 * sin(time * 3.0);
    
    vec3 coreColor = vec3(0.3, 0.5, 1.0);
    vec3 edgeColor = vec3(0.6, 0.2, 1.0);
    vec3 glowColor = vec3(0.4, 0.7, 1.2);
    
    vec3 plasmaColor = mix(coreColor, edgeColor, energyNoise);
    plasmaColor = mix(plasmaColor, glowColor, fresnel * 0.7);
    plasmaColor *= pulse;
    
    float edgeGlow = fresnel * (1.0 + energyNoise * 0.5);
    plasmaColor += edgeGlow * glowColor * 0.8;
    
    float glowIntensity = fresnel * 3.0;
    float alpha = glowIntensity + 0.9;
    alpha = clamp(alpha, 0.0, 1.0);
    
    plasmaColor += glowColor * glowIntensity * 1.5;
    
    gl_FragColor = vec4(plasmaColor, alpha);
}
