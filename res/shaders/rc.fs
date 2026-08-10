// agent: composer-2.5 | 2026-08-10 | brighter unity ambient rc.fs | 1fd03f
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform float ng_time;
uniform vec2 ng_resolution;
uniform vec3 ng_tint;
uniform vec3 ng_glow;
uniform float ng_roughness;
uniform float ng_metalness;

out vec4 finalColor;

const float AMBIENT = 1.0;
const float DIRECTIONAL = 1.0;
const float AMBIENT_ALBEDO = 0.22;

void main() {
  vec3 n = normalize(fragNormal);
  vec3 albedo = ng_tint;
  vec3 view_dir = normalize(vec3(0.4, 0.55, 1.0));

  /* Two hardcoded lights until a light describe API exists. */
  vec3 l0 = normalize(vec3(0.45, 0.85, 0.2));
  vec3 l1 = normalize(vec3(-0.55, 0.35, 0.65));
  vec3 c0 = vec3(1.15, 1.08, 0.95);
  vec3 c1 = vec3(0.55, 0.65, 1.15);

  float ndl0 = max(dot(n, l0), 0.0);
  float ndl1 = max(dot(n, l1), 0.0);
  vec3 diffuse = albedo * (c0 * ndl0 + c1 * ndl1 * 0.55);

  float rough = clamp(ng_roughness, 0.04, 1.0);
  float metal = clamp(ng_metalness, 0.0, 1.0);
  float shininess = mix(8.0, 96.0, 1.0 - rough);
  vec3 h0 = normalize(l0 + view_dir);
  vec3 h1 = normalize(l1 + view_dir);
  float spec0 = pow(max(dot(n, h0), 0.0), shininess);
  float spec1 = pow(max(dot(n, h1), 0.0), shininess);
  vec3 spec_col = mix(vec3(0.04), albedo, metal);
  vec3 specular = spec_col * (c0 * spec0 + c1 * spec1 * 0.4) * (1.0 - rough * 0.65);

  vec3 ambient = albedo * AMBIENT_ALBEDO * AMBIENT;
  vec3 direct = (mix(diffuse, diffuse * (1.0 - metal * 0.85), metal) + specular) * DIRECTIONAL;
  finalColor = vec4(ambient + direct + ng_glow, 1.0);
}
// agent: composer-2.5 | 2026-08-10 | brighter unity ambient rc.fs | 1fd03f
