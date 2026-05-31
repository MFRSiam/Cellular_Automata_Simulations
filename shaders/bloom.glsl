#version 430

// ===========================================================================
// bloom.fs - final post-processing pass.
//
// Takes the composited scene and makes bright pixels (lava, fire, gold, water
// foam) bleed light into their neighbours, then darkens the corners with a
// vignette for depth. This is the last shader in the chain.
// ===========================================================================

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 resolution;

// Two-pass-style separable-ish blur done in a single 9-tap diamond per axis to
// keep it cheap while spreading the glow a little wider than a tight box.
vec3 sampleBloom(vec2 uv) {
    vec3 bloom = vec3(0.0);
    float total = 0.0;
    for (int x = -3; x <= 3; x++) {
        for (int y = -3; y <= 3; y++) {
            float w = 1.0 - length(vec2(x, y)) / 5.0; // soft falloff
            if (w <= 0.0) continue;
            vec2 off = vec2(float(x), float(y)) * 1.6 / resolution;
            vec3 s = texture(texture0, uv + off).rgb;
            float bright = max(s.r, max(s.g, s.b));
            // Threshold just above the cap used for non-emissive cells
            // (195/255), so only fire / lava / gold actually bloom.
            bloom += s * smoothstep(0.78, 1.0, bright) * w;
            total += w;
        }
    }
    return bloom / max(total, 1.0);
}

void main() {
    vec3 base  = texture(texture0, fragTexCoord).rgb;
    vec3 bloom = sampleBloom(fragTexCoord);

    vec3 col = base + bloom * 1.5; // add the glow on top of the base image

    // Vignette: gently fade toward the screen edges.
    vec2 uv = fragTexCoord - 0.5;
    col *= mix(0.80, 1.0, smoothstep(0.95, 0.25, length(uv)));

    finalColor = vec4(min(col, vec3(1.0)), 1.0);
}
