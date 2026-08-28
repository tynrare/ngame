// agent: grok-4.6 | 2026-08-28 | MSDF median AA shader | 42ee89
in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform vec3 ng_tint;
uniform float ng_outline;

out vec4 finalColor;

float median(float r, float g, float b) {
  return max(min(r, g), min(max(r, g), b));
}

void main() {
  vec3 s = texture(texture0, fragTexCoord).rgb;
  float d = median(s.r, s.g, s.b);
  float w = fwidth(d);
  float fill = smoothstep(0.5 - w, 0.5 + w, d);
  float alpha = fill;
  vec3 rgb = ng_tint;
  if (ng_outline > 0.0) {
    float out_a = smoothstep(0.5 - ng_outline - w, 0.5 - ng_outline + w, d);
    rgb = mix(vec3(0.0), ng_tint, fill);
    alpha = max(out_a, fill);
  }
  finalColor = vec4(rgb, alpha);
}
// agent: grok-4.6 | 2026-08-28 | MSDF median AA shader | 42ee89
