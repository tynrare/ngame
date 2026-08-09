// agent: composer-2.5 | 2026-08-09 | RC compose Direct plus GI | 6e0c88
// agent: composer-2.5 | 2026-08-09 | compose GI vs direct | 30745a
// agent: composer-2.5 | 2026-08-09 | compose restore exposure | 3e79b6
in vec2 fragTexCoord;

uniform sampler2D tex_albedo;
uniform sampler2D tex_normal;
uniform sampler2D tex_glow;
uniform sampler2D tex_depth;
uniform sampler2D tex_irradiance;
uniform float ng_gi_strength;
uniform vec3 ng_sky;

out vec4 finalColor;

void main() {
  vec2 uv = fragTexCoord;
  float d = texture(tex_depth, uv).r;
  if (d < 0.02) {
    finalColor = vec4(ng_sky * 0.25, 1.0);
    return;
  }

  vec3 albedo = texture(tex_albedo, uv).rgb;
  vec3 n = texture(tex_normal, uv).rgb * 2.0 - 1.0;
  n = normalize(n);
  vec3 glow = texture(tex_glow, uv).rgb;
  vec3 irradiance = texture(tex_irradiance, uv).rgb;

  vec3 l0 = normalize(vec3(0.45, 0.85, 0.2));
  vec3 l1 = normalize(vec3(-0.55, 0.35, 0.65));
  vec3 c0 = vec3(1.0, 0.95, 0.85);
  vec3 c1 = vec3(0.45, 0.55, 1.0);
  float ndl0 = max(dot(n, l0), 0.0);
  float ndl1 = max(dot(n, l1), 0.0);
  /* Direct primary; GI is a readable add, not a wash. */
  vec3 direct = albedo * (c0 * ndl0 + c1 * ndl1 * 0.55);
  vec3 ambient = albedo * 0.06;
  vec3 gi = albedo * irradiance * ng_gi_strength;

  finalColor = vec4(ambient + direct + gi + glow, 1.0);
}
// agent: composer-2.5 | 2026-08-09 | RC compose Direct plus GI | 6e0c88
// agent: composer-2.5 | 2026-08-09 | compose GI vs direct | 30745a
// agent: composer-2.5 | 2026-08-09 | compose restore exposure | 3e79b6
