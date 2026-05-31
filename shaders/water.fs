#version 330

// ===========================================================================
// water.fs - liquid surface effect pass.
//
// Runs over the rendered scene and adds animated caustics + a gentle
// refraction wobble to water-like pixels. It detects "water" purely from
// colour (blue channel dominant), so it costs nothing extra from the
// simulation and naturally affects only the blue liquids.
// ===========================================================================

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 resolution;
uniform float time;        // seconds, animates the caustics

void main() {
    vec2 uv = fragTexCoord;
    vec3 c = texture(texture0, uv).rgb;

    // Water mask: how much more blue than red this pixel is (0..1).
    float water = clamp((c.b - c.r) * 2.0, 0.0, 1.0);

    // Two crossing sine waves give a shifting caustic pattern.
    vec2 p = uv * resolution * 0.06;
    float caustic = sin(p.x + time * 2.0) * sin(p.y - time * 1.5);

    // Refraction: nudge the sample position by the caustic, scaled by the mask.
    vec2 offset = vec2(caustic) * water * 1.5 / resolution;
    vec3 refracted = texture(texture0, uv + offset).rgb;

    c = mix(c, refracted, water);          // bend the water surface
    c += water * caustic * 0.05;           // sparkle highlights

    finalColor = vec4(c, 1.0);
}
