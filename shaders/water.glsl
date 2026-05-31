#version 430

// ===========================================================================
// water.fs - liquid surface effect pass.
//
// Adds animated caustics and a gentle refraction wobble to water-like pixels.
// Water is detected purely from colour (blue dominant), so it costs nothing
// extra from the simulation and only affects the blue liquids.
// ===========================================================================

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2  resolution;
uniform float time;        // seconds, animates the caustics

// Cheap layered caustic field: a couple of rotated sine lattices summed so the
// pattern doesn't look like a single regular grid.
float caustics(vec2 p) {
    float c = 0.0;
    c += sin(p.x * 1.3 + time * 2.0) * sin(p.y * 1.1 - time * 1.5);
    c += sin((p.x + p.y) * 0.9 - time * 1.7) * sin((p.x - p.y) * 0.7 + time * 1.1);
    return c * 0.5; // back to ~[-1,1]
}

void main() {
    vec2 uv = fragTexCoord;
    vec3 c  = texture(texture0, uv).rgb;

    // Water mask: how much more blue than red this pixel is (0..1), smoothed.
    float water = smoothstep(0.04, 0.35, (c.b - c.r));

    vec2 p = uv * resolution * 0.06;
    float ca = caustics(p);

    // Refraction: nudge the sample position by the caustic gradient.
    vec2 offset = vec2(ca, caustics(p + 1.7)) * water * 1.6 / resolution;
    vec3 refracted = texture(texture0, uv + offset).rgb;

    c = mix(c, refracted, water);                 // bend the surface
    c += water * max(ca, 0.0) * vec3(0.05, 0.07, 0.09); // bluish sparkle

    finalColor = vec4(c, 1.0);
}
