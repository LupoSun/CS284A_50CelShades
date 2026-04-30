#include <algorithm>
#include <cmath>
#include <glad/glad.h>

#include <fstream>
#include <sstream>

#include <CGL/vector3D.h>
#include <nanogui/nanogui.h>
#include <nanogui/colorpicker.h>
#include <nanogui/vscrollpanel.h>

#include "json.hpp"

#include "clothSimulator.h"

#include "camera.h"
#include "cloth.h"
#include "collision/mesh.h"
#include "collision/plane.h"
#include "collision/sphere.h"
#include "misc/camera_info.h"
#include "misc/file_utils.h"
// Needed to generate stb_image binaries. Should only define in exactly one source file importing stb_image.h.
#define STB_IMAGE_IMPLEMENTATION
#include "misc/stb_image.h"

using namespace nanogui;
using namespace std;

namespace {

Vector3f colorToVec3(const nanogui::Color &color) {
  return Vector3f(color[0], color[1], color[2]);
}

} // namespace

Vector3D load_texture(int frame_idx, GLuint handle, const char* where) {
  Vector3D size_retval;
  
  if (strlen(where) == 0) return size_retval;
  
  glActiveTexture(GL_TEXTURE0 + frame_idx);
  glBindTexture(GL_TEXTURE_2D, handle);
  
  
  int img_x, img_y, img_n;
  unsigned char* img_data = stbi_load(where, &img_x, &img_y, &img_n, 3);
  if (!img_data) return size_retval;
  size_retval.x = img_x;
  size_retval.y = img_y;
  size_retval.z = img_n;

  // Upload base level and generate mipmaps on the CPU by downsampling successive levels.
  int width = img_x;
  int height = img_y;
  const int channels = 3;

  // Copy image data into a vector for CPU-side mip generation and free stb image buffer.
  std::vector<unsigned char> curr(img_data, img_data + (width * height * channels));
  stbi_image_free(img_data);

  int level = 0;
  while (true) {
    // Upload current mip level
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, curr.data());

    // Stop if we reached 1x1
    if (width == 1 && height == 1) break;

    // Compute next level size
    int next_w = std::max(1, width / 2);
    int next_h = std::max(1, height / 2);
    std::vector<unsigned char> next(next_w * next_h * channels);

    // Simple box filter 2x2 -> 1 averaging
    for (int y = 0; y < next_h; ++y) {
      for (int x = 0; x < next_w; ++x) {
        int dst_idx = (y * next_w + x) * channels;
        // average up to 4 texels from curr
        int src_x = x * 2;
        int src_y = y * 2;
        int accum[3] = {0, 0, 0};
        int count = 0;
        for (int oy = 0; oy < 2; ++oy) {
          for (int ox = 0; ox < 2; ++ox) {
            int sx = src_x + ox;
            int sy = src_y + oy;
            if (sx < width && sy < height) {
              int src_idx = (sy * width + sx) * channels;
              accum[0] += curr[src_idx + 0];
              accum[1] += curr[src_idx + 1];
              accum[2] += curr[src_idx + 2];
              ++count;
            }
          }
        }
        next[dst_idx + 0] = (unsigned char)(accum[0] / count);
        next[dst_idx + 1] = (unsigned char)(accum[1] / count);
        next[dst_idx + 2] = (unsigned char)(accum[2] / count);
      }
    }

    // Move to next level
    curr.swap(next);
    width = next_w;
    height = next_h;
    ++level;
  }

  // Use trilinear sampling by default
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  
  return size_retval;
}

void load_cubemap(int frame_idx, GLuint handle, const std::vector<std::string>& file_locs) {
  glActiveTexture(GL_TEXTURE0 + frame_idx);
  glBindTexture(GL_TEXTURE_CUBE_MAP, handle);
  for (int side_idx = 0; side_idx < 6; ++side_idx) {
    
    int img_x, img_y, img_n;
    unsigned char* img_data = stbi_load(file_locs[side_idx].c_str(), &img_x, &img_y, &img_n, 3);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + side_idx, 0, GL_RGB, img_x, img_y, 0, GL_RGB, GL_UNSIGNED_BYTE, img_data);
    stbi_image_free(img_data);
    std::cout << "Side " << side_idx << " has dimensions " << img_x << ", " << img_y << std::endl;

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  }
}

void ClothSimulator::load_textures() {
  glGenTextures(1, &m_gl_texture_1);
  glGenTextures(1, &m_gl_texture_2);
  glGenTextures(1, &m_gl_texture_3);
  glGenTextures(1, &m_gl_texture_4);
  glGenTextures(1, &m_gl_cubemap_tex);
  
  m_gl_texture_1_size = load_texture(1, m_gl_texture_1, (m_project_root + "/textures/texture_1.png").c_str());
  m_gl_texture_2_size = load_texture(2, m_gl_texture_2, (m_project_root + "/textures/texture_2.png").c_str());
  m_gl_texture_3_size = load_texture(3, m_gl_texture_3, (m_project_root + "/textures/texture_3.png").c_str());
  m_gl_texture_4_size = load_texture(4, m_gl_texture_4, (m_project_root + "/textures/texture_4.png").c_str());
  
  std::cout << "Texture 1 loaded with size: " << m_gl_texture_1_size << std::endl;
  std::cout << "Texture 2 loaded with size: " << m_gl_texture_2_size << std::endl;
  std::cout << "Texture 3 loaded with size: " << m_gl_texture_3_size << std::endl;
  std::cout << "Texture 4 loaded with size: " << m_gl_texture_4_size << std::endl;
  
  std::vector<std::string> cubemap_fnames = {
    m_project_root + "/textures/cube/posx.jpg",
    m_project_root + "/textures/cube/negx.jpg",
    m_project_root + "/textures/cube/posy.jpg",
    m_project_root + "/textures/cube/negy.jpg",
    m_project_root + "/textures/cube/posz.jpg",
    m_project_root + "/textures/cube/negz.jpg"
  };
  
  load_cubemap(5, m_gl_cubemap_tex, cubemap_fnames);
  std::cout << "Loaded cubemap texture" << std::endl;

  // Keep unit 0 populated with valid fallback textures. Apple's OpenGL
  // driver can warn if a shader or UI pass touches sampler unit 0 before
  // an explicit binding has been established there.
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_gl_texture_1);
  glBindTexture(GL_TEXTURE_CUBE_MAP, m_gl_cubemap_tex);
}

void ClothSimulator::load_shaders() {
  std::set<std::string> shader_folder_contents;
  bool success = FileUtils::list_files_in_directory(m_project_root + "/shaders", shader_folder_contents);
  if (!success) {
    std::cout << "Error: Could not find the shaders folder!" << std::endl;
  }
  
  std::string std_vert_shader = m_project_root + "/shaders/Default.vert";
  
  for (const std::string& shader_fname : shader_folder_contents) {
    std::string file_extension;
    std::string shader_name;
    
    FileUtils::split_filename(shader_fname, shader_name, file_extension);
    
    if (file_extension != "frag") {
      std::cout << "Skipping non-shader file: " << shader_fname << std::endl;
      continue;
    }
    if (shader_name.find("fullscreen_") == 0 || shader_name == "shadow_depth") {
      std::cout << "Skipping internal shader file: " << shader_fname << std::endl;
      continue;
    }
    
    std::cout << "Found shader file: " << shader_fname << std::endl;
    
    // Check if there is a proper .vert shader or not for it
    std::string vert_shader = std_vert_shader;
    std::string associated_vert_shader_path = m_project_root + "/shaders/" + shader_name + ".vert";
    if (FileUtils::file_exists(associated_vert_shader_path)) {
      vert_shader = associated_vert_shader_path;
    }
    
    std::shared_ptr<GLShader> nanogui_shader = make_shared<GLShader>();
    try {
      nanogui_shader->initFromFiles(shader_name, vert_shader,
                                    m_project_root + "/shaders/" + shader_fname);
    } catch (const std::exception &e) {
      std::cout << "Skipping shader \"" << shader_name
                << "\" due to compile/link failure: " << e.what() << std::endl;
      continue;
    }
    
    // Special filenames are treated a bit differently
    ShaderTypeHint hint;
    if (shader_name == "Wireframe") {
      hint = ShaderTypeHint::WIREFRAME;
      std::cout << "Type: Wireframe" << std::endl;
    } else if (shader_name == "Normal") {
      hint = ShaderTypeHint::NORMALS;
      std::cout << "Type: Normal" << std::endl;
    } else {
      hint = ShaderTypeHint::PHONG;
      std::cout << "Type: Custom" << std::endl;
    }
    
    UserShader user_shader(shader_name, nanogui_shader, hint);
    
    shaders.push_back(user_shader);
    shaders_combobox_names.push_back(shader_name);
  }
  
  // Prefer "Cel" as the default, fall back to "Wireframe".
  int cel_idx = -1;
  int wireframe_idx = -1;
  for (size_t i = 0; i < shaders_combobox_names.size(); ++i) {
    if (shaders_combobox_names[i] == "Cel") cel_idx = (int)i;
    else if (shaders_combobox_names[i] == "Wireframe") wireframe_idx = (int)i;
  }
  if (cel_idx >= 0) active_shader_idx = cel_idx;
  else if (wireframe_idx >= 0) active_shader_idx = wireframe_idx;
}

ClothSimulator::ClothSimulator(std::string project_root, Screen *screen)
: m_project_root(project_root) {
  this->screen = screen;

  this->load_shaders();
  this->load_textures();
  m_cel_texture_path = m_project_root + "/textures/texture_1.png";
  this->loadCelPreset(defaultCelPresetPath());

  glEnable(GL_PROGRAM_POINT_SIZE);
  glEnable(GL_DEPTH_TEST);
}

std::string ClothSimulator::defaultCelPresetPath() const {
  return m_project_root + "/scene/cel_preset.json";
}

bool ClothSimulator::saveCelPreset(const std::string &path) {
  nlohmann::json j;
  j["base_color"] = {color[0], color[1], color[2], color[3]};
  j["dark_color"] = {m_cel_dark_color[0], m_cel_dark_color[1],
                     m_cel_dark_color[2], m_cel_dark_color[3]};
  j["bright_color"] = {m_cel_bright_color[0], m_cel_bright_color[1],
                       m_cel_bright_color[2], m_cel_bright_color[3]};
  j["light_dir"] = {m_cel_light_dir.x(), m_cel_light_dir.y(),
                    m_cel_light_dir.z()};
  j["light_pos"] = {m_cel_light_pos.x(), m_cel_light_pos.y(),
                    m_cel_light_pos.z()};
  j["light_type"] = m_cel_light_type;
  j["dark_threshold"] = m_cel_dark_threshold;
  j["bright_threshold"] = m_cel_bright_threshold;
  j["shadow_strength"] = m_cel_shadow_strength;
  j["highlight_strength"] = m_cel_highlight_strength;
  j["pattern_scale"] = m_cel_pattern_scale;
  j["pattern_radius"] = m_cel_pattern_radius;
  j["bands"] = m_cel_bands;
  // Background and edge settings
  j["background_color"] = {m_background_color[0], m_background_color[1], m_background_color[2], m_background_color[3]};
  j["edge_enabled"] = m_edge_enabled;
  j["edge_color"] = {m_edge_line_color[0], m_edge_line_color[1], m_edge_line_color[2], m_edge_line_color[3]};
  j["edge_depth_threshold"] = m_edge_depth_threshold;
  j["edge_normal_threshold"] = m_edge_normal_threshold;
  j["edge_strength"] = m_edge_strength;
  j["depth_edge_thickness"] = m_depth_edge_thickness;
  j["normal_edge_thickness"] = m_normal_edge_thickness;
  if (!m_cel_texture_path.empty()) j["texture_path"] = m_cel_texture_path;

  std::ofstream out(path);
  if (!out.is_open()) {
    std::cout << "Cel preset: could not open for write: " << path << std::endl;
    return false;
  }
  out << j.dump(2);
  std::cout << "Cel preset saved: " << path << std::endl;
  return true;
}

bool ClothSimulator::loadCelPreset(const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open()) return false;

  nlohmann::json j;
  try { in >> j; }
  catch (const std::exception &e) {
    std::cout << "Cel preset: parse error in " << path << ": " << e.what() << std::endl;
    return false;
  }

  auto readColor = [](const nlohmann::json &arr, nanogui::Color &out) {
    if (arr.is_array() && arr.size() >= 4) {
      out = nanogui::Color((float)arr[0], (float)arr[1], (float)arr[2], (float)arr[3]);
    }
  };
  auto readVec3 = [](const nlohmann::json &arr, nanogui::Vector3f &out) {
    if (arr.is_array() && arr.size() >= 3) {
      out = nanogui::Vector3f((float)arr[0], (float)arr[1], (float)arr[2]);
    }
  };

  auto has = [&j](const char *key) { return j.find(key) != j.end(); };

  if (has("base_color")) readColor(j["base_color"], color);
  if (has("dark_color")) readColor(j["dark_color"], m_cel_dark_color);
  if (has("bright_color")) readColor(j["bright_color"], m_cel_bright_color);
  if (has("light_dir")) readVec3(j["light_dir"], m_cel_light_dir);
  if (has("light_pos")) readVec3(j["light_pos"], m_cel_light_pos);
  if (has("light_type")) m_cel_light_type = j["light_type"];
  if (has("dark_threshold")) m_cel_dark_threshold = j["dark_threshold"];
  if (has("bright_threshold")) m_cel_bright_threshold = j["bright_threshold"];
  if (has("shadow_strength")) m_cel_shadow_strength = j["shadow_strength"];
  if (has("highlight_strength")) m_cel_highlight_strength = j["highlight_strength"];
  if (has("pattern_scale")) m_cel_pattern_scale = j["pattern_scale"];
  if (has("pattern_radius")) m_cel_pattern_radius = j["pattern_radius"];
  if (has("bands")) m_cel_bands = j["bands"];
  if (has("texture_path")) {
    std::string tex = j["texture_path"];
    if (!tex.empty()) reloadCelTexture(tex);
  }

  // Optional background and edge settings
  if (has("background_color") && j["background_color"].is_array() && j["background_color"].size() >= 4) {
    auto &arr = j["background_color"];
    m_background_color = nanogui::Color((float)arr[0], (float)arr[1], (float)arr[2], (float)arr[3]);
  }
  if (has("edge_enabled")) m_edge_enabled = j["edge_enabled"];
  if (has("edge_color") && j["edge_color"].is_array() && j["edge_color"].size() >= 4) {
    auto &arr = j["edge_color"];
    m_edge_line_color = nanogui::Color((float)arr[0], (float)arr[1], (float)arr[2], (float)arr[3]);
  }
  if (has("edge_depth_threshold")) m_edge_depth_threshold = j["edge_depth_threshold"];
  if (has("edge_normal_threshold")) m_edge_normal_threshold = j["edge_normal_threshold"];
  if (has("edge_strength")) m_edge_strength = j["edge_strength"];
  if (has("depth_edge_thickness")) {
      m_depth_edge_thickness = j["depth_edge_thickness"];
  }

  if (has("normal_edge_thickness")) {
      m_normal_edge_thickness = j["normal_edge_thickness"];
  }

  // Backward compatibility for old preset files
  if (has("edge_thickness")) {
      m_depth_edge_thickness = j["edge_thickness"];
      m_normal_edge_thickness = j["edge_thickness"];
  }

  if (m_refresh_widgets) m_refresh_widgets();
  std::cout << "Cel preset loaded: " << path << std::endl;
  return true;
}

void ClothSimulator::reloadCelTexture(const std::string &path) {
  m_cel_texture_path = path;
  load_texture(1, m_gl_texture_1, path.c_str());
  std::cout << "Cel texture reloaded: " << path << std::endl;
}

bool ClothSimulator::exportCelHLSL(const std::string &path) {
  std::ofstream out(path);
  if (!out.is_open()) {
    std::cout << "HLSL export: could not open for write: " << path << std::endl;
    return false;
  }

  std::ostringstream s;
  s.precision(4);
  s << std::fixed;

  s << "// Auto-exported from CS284A_50CelShades cloth simulator.\n";
  s << "// Current GUI values are baked in as Property defaults;\n";
  s << "// tweak in the Unity inspector after import.\n";
  s << "Shader \"Custom/Cel_Exported\" {\n";
  s << "    Properties {\n";
  s << "        _BaseColor (\"Base Color\", Color) = ("
    << color[0] << ", " << color[1] << ", " << color[2] << ", " << color[3] << ")\n";
  s << "        _DarkColor (\"Dark Color\", Color) = ("
    << m_cel_dark_color[0] << ", " << m_cel_dark_color[1] << ", "
    << m_cel_dark_color[2] << ", " << m_cel_dark_color[3] << ")\n";
  s << "        _BrightColor (\"Bright Color\", Color) = ("
    << m_cel_bright_color[0] << ", " << m_cel_bright_color[1] << ", "
    << m_cel_bright_color[2] << ", " << m_cel_bright_color[3] << ")\n";
  s << "        _LightDir (\"Light Direction\", Vector) = ("
    << m_cel_light_dir.x() << ", " << m_cel_light_dir.y() << ", "
    << m_cel_light_dir.z() << ", 0)\n";
  s << "        _LightPos (\"Light Position\", Vector) = ("
    << m_cel_light_pos.x() << ", " << m_cel_light_pos.y() << ", "
    << m_cel_light_pos.z() << ", 0)\n";
  s << "        _LightType (\"Light Type (0=Dir 1=Point)\", Float) = "
    << m_cel_light_type << "\n";
  s << "        _DarkThreshold (\"Dark Threshold\", Range(0,1)) = " << m_cel_dark_threshold << "\n";
  s << "        _BrightThreshold (\"Bright Threshold\", Range(0,1)) = " << m_cel_bright_threshold << "\n";
  s << "        _ShadowStrength (\"Shadow Strength\", Range(0,1)) = " << m_cel_shadow_strength << "\n";
  s << "        _HighlightStrength (\"Highlight Strength\", Range(0,2)) = " << m_cel_highlight_strength << "\n";
  s << "        _PatternScale (\"Pattern Scale\", Range(0.05,30)) = " << m_cel_pattern_scale << "\n";
  s << "        _Bands (\"Bands\", Range(1,16)) = " << m_cel_bands << "\n";
  s << "        _MainTex (\"Pattern Texture\", 2D) = \"white\" {}\n";
  s << "    }\n";
  s << "    SubShader {\n";
  s << "        Tags { \"RenderType\"=\"Opaque\" }\n";
  s << "        Pass {\n";
  s << "            CGPROGRAM\n";
  s << "            #pragma vertex vert\n";
  s << "            #pragma fragment frag\n";
  s << "            #include \"UnityCG.cginc\"\n\n";
  s << "            struct appdata {\n";
  s << "                float4 vertex : POSITION;\n";
  s << "                float3 normal : NORMAL;\n";
  s << "            };\n";
  s << "            struct v2f {\n";
  s << "                float4 pos : SV_POSITION;\n";
  s << "                float3 worldPos : TEXCOORD0;\n";
  s << "                float3 worldNormal : TEXCOORD1;\n";
  s << "            };\n\n";
  s << "            sampler2D _MainTex;\n";
  s << "            float4 _BaseColor, _DarkColor, _BrightColor, _LightDir;\n";
  s << "            float _DarkThreshold, _BrightThreshold;\n";
  s << "            float _ShadowStrength, _HighlightStrength;\n";
  s << "            float _PatternScale, _Bands;\n\n";
  s << "            v2f vert(appdata v) {\n";
  s << "                v2f o;\n";
  s << "                o.pos = UnityObjectToClipPos(v.vertex);\n";
  s << "                o.worldPos = mul(unity_ObjectToWorld, v.vertex).xyz;\n";
  s << "                o.worldNormal = UnityObjectToWorldNormal(v.normal);\n";
  s << "                return o;\n";
  s << "            }\n\n";
  s << "            float4 triplanarSample(sampler2D tex, float3 pos, float3 n, float tiling) {\n";
  s << "                float3 w = max(abs(n), 1e-5);\n";
  s << "                w /= (w.x + w.y + w.z);\n";
  s << "                float4 sx = tex2D(tex, pos.yz * tiling);\n";
  s << "                float4 sy = tex2D(tex, pos.xz * tiling);\n";
  s << "                float4 sz = tex2D(tex, pos.xy * tiling);\n";
  s << "                return sx * w.x + sy * w.y + sz * w.z;\n";
  s << "            }\n\n";
  s << "            float4 frag(v2f i) : SV_Target {\n";
  s << "                float3 N = normalize(i.worldNormal);\n";
  s << "                float3 L = normalize(_LightDir.xyz);\n";
  s << "                float4 triPat = triplanarSample(_MainTex, i.worldPos, N, _PatternScale);\n";
  s << "                float ndotl = dot(N, L);\n";
  s << "                float bands = max(1.0, _Bands);\n";
  s << "                float ramp = saturate(ndotl / max(_DarkThreshold, 1e-3));\n";
  s << "                float litQ = floor(ramp * bands) / bands;\n";
  s << "                float darkMask = 1.0 - litQ;\n";
  s << "                float darkBlend = darkMask * _ShadowStrength;\n";
  s << "                float darkT = saturate(triPat.r * darkBlend);\n";
  s << "                float3 shaded = lerp(_BaseColor.rgb, _DarkColor.rgb, darkT);\n";
  s << "                float brightMask = step(_BrightThreshold, ndotl);\n";
  s << "                float3 highlight = brightMask * _BrightColor.rgb * _HighlightStrength;\n";
  s << "                return float4(min(shaded + highlight, 1.0), _BaseColor.a);\n";
  s << "            }\n";
  s << "            ENDCG\n";
  s << "        }\n";
  s << "    }\n";
  s << "    FallBack \"Diffuse\"\n";
  s << "}\n";

  out << s.str();
  std::cout << "HLSL exported: " << path << std::endl;
  return true;
}

ClothSimulator::~ClothSimulator() {
  for (auto shader : shaders) {
    shader.nanogui_shader->free();
  }
  glDeleteTextures(1, &m_gl_texture_1);
  glDeleteTextures(1, &m_gl_texture_2);
  glDeleteTextures(1, &m_gl_texture_3);
  glDeleteTextures(1, &m_gl_texture_4);
  glDeleteTextures(1, &m_gl_cubemap_tex);

  destroyFramebuffer();

  if (m_fullscreen_vbo != 0) {
      glDeleteBuffers(1, &m_fullscreen_vbo);
      m_fullscreen_vbo = 0;
  }
  if (m_fullscreen_vao != 0) {
      glDeleteVertexArrays(1, &m_fullscreen_vao);
      m_fullscreen_vao = 0;
  }
  if (m_fullscreen_edge_shader) {
      m_fullscreen_edge_shader->free();
      m_fullscreen_edge_shader.reset();
  }
  if (m_fullscreen_compare_shader) {
      m_fullscreen_compare_shader->free();
      m_fullscreen_compare_shader.reset();
  }

  if (collision_objects) {
      for (CollisionObject *co : m_runtime_meshes) {
          delete co;
      }
      m_runtime_meshes.clear();
      if (m_owns_collision_objects) {
          delete collision_objects;
      }
  }
  cloth = nullptr;
  cp = nullptr;
  collision_objects = nullptr;

  destroyShadowMap();

  if (m_shadow_depth_shader) {
      m_shadow_depth_shader->free();
      m_shadow_depth_shader.reset();
  }
}

void ClothSimulator::loadCloth(Cloth *cloth) { this->cloth = cloth; }

void ClothSimulator::loadClothParameters(ClothParameters *cp) { this->cp = cp; }

void ClothSimulator::loadCollisionObjects(vector<CollisionObject *> *objects) {
  this->collision_objects = objects;
  m_owns_collision_objects = false;

  m_mesh_collision_enabled = false;
  if (collision_objects) {
    for (CollisionObject *co : *collision_objects) {
      if (co->isMesh() && co->collisionEnabled()) {
        m_mesh_collision_enabled = true;
        break;
      }
    }
  }
  setMeshCollisionEnabled(m_mesh_collision_enabled);
}

/**
 * Initializes the cloth simulation and spawns a new thread to separate
 * rendering from simulation.
 */
void ClothSimulator::init() {

  // Initialize GUI
  screen->setSize(default_window_size);
  initGUI(screen);

  // Reload default preset after GUI is built so widgets are refreshed
  loadCelPreset(defaultCelPresetPath());

  // Initialize camera

  CGL::Collada::CameraInfo camera_info;
  camera_info.hFov = 50;
  camera_info.vFov = 35;
  camera_info.nClip = 0.01;
  camera_info.fClip = 10000;

  // Try to intelligently figure out the camera target

  Vector3D avg_pm_position(0, 0, 0);

  for (auto &pm : cloth->point_masses) {
    avg_pm_position += pm.position / cloth->point_masses.size();
  }

  CGL::Vector3D target(avg_pm_position.x, avg_pm_position.y / 2,
                       avg_pm_position.z);
  CGL::Vector3D c_dir(0., 0., 0.);
  canonical_view_distance = max(cloth->width, cloth->height) * 0.9;
  scroll_rate = canonical_view_distance / 10;

  view_distance = canonical_view_distance * 2;
  min_view_distance = canonical_view_distance / 10.0;
  max_view_distance = canonical_view_distance * 20.0;

  // canonicalCamera is a copy used for view resets

  camera.place(target, acos(c_dir.y), atan2(c_dir.x, c_dir.z), view_distance,
               min_view_distance, max_view_distance);
  canonicalCamera.place(target, acos(c_dir.y), atan2(c_dir.x, c_dir.z),
                        view_distance, min_view_distance, max_view_distance);

  // Query the actual framebuffer size (accounts for Retina/HiDPI scaling)
  glfwGetFramebufferSize(screen->glfwWindow(), &screen_w, &screen_h);

  camera.configure(camera_info, screen_w, screen_h);
  canonicalCamera.configure(camera_info, screen_w, screen_h);

  // Initialize offscreen/post-process resources now that screen size is known.
  try {
    m_fullscreen_edge_shader = std::make_shared<GLShader>();
    m_fullscreen_edge_shader->initFromFiles(
        "FullscreenEdge",
        m_project_root + "/shaders/fullscreen_edge.vert",
        m_project_root + "/shaders/fullscreen_edge.frag");
  } catch (const std::exception& e) {
    std::cout << "Warning: could not load fullscreen edge shader: "
        << e.what() << std::endl;
    m_fullscreen_edge_shader.reset();
  }

  try {
    m_fullscreen_compare_shader = std::make_shared<GLShader>();
    m_fullscreen_compare_shader->initFromFiles(
        "FullscreenCompare",
        m_project_root + "/shaders/fullscreen_edge.vert",
        m_project_root + "/shaders/fullscreen_compare.frag");
  } catch (const std::exception& e) {
    std::cout << "Warning: could not load fullscreen compare shader: "
        << e.what() << std::endl;
    m_fullscreen_compare_shader.reset();
  }

  try {
    m_shadow_depth_shader = std::make_shared<GLShader>();
    m_shadow_depth_shader->initFromFiles(
        "ShadowDepth",
        m_project_root + "/shaders/shadow_depth.vert",
        m_project_root + "/shaders/shadow_depth.frag"
    );
  } catch (const std::exception& e) {
    std::cout << "Warning: could not load shadow depth shader: "
        << e.what() << std::endl;
    m_shadow_depth_shader.reset();
  }

initShadowMap();

initFramebuffer();
initFullscreenQuad();
}

bool ClothSimulator::isAlive() { return is_alive; }

int ClothSimulator::shaderIndexByName(const std::string &name) const {
  for (size_t i = 0; i < shaders.size(); ++i) {
    if (shaders[i].display_name == name) return (int)i;
  }
  return -1;
}

void ClothSimulator::setMeshCollisionEnabled(bool enabled) {
  m_mesh_collision_enabled = enabled;
  if (!collision_objects) return;

  for (CollisionObject *co : *collision_objects) {
    if (co->isMesh()) {
      co->setCollisionEnabled(enabled);
    }
  }
}

bool ClothSimulator::loadRuntimeMesh(const std::string &path, double friction,
                                     const Vector3D &scale,
                                     const Vector3D &translate) {
  if (path.empty()) {
    return false;
  }

  Mesh *mesh = new Mesh(path, friction, scale, translate,
                        m_mesh_collision_enabled);
  if (!mesh->loaded()) {
    delete mesh;
    return false;
  }

  if (!collision_objects) {
    collision_objects = new vector<CollisionObject *>();
    m_owns_collision_objects = true;
  }

  collision_objects->push_back(mesh);
  m_runtime_meshes.push_back(mesh);
  setMeshCollisionEnabled(m_mesh_collision_enabled);
  return true;
}

void ClothSimulator::renderShadowMap(const Matrix4f &lightViewProjection) {
  if (m_shadow_fbo == 0 || !m_shadow_depth_shader || m_cel_light_type != 0) {
    return;
  }

  Matrix4f model;
  model.setIdentity();

  glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_fbo);
  glViewport(0, 0, m_shadow_map_size, m_shadow_map_size);
  glEnable(GL_DEPTH_TEST);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_FRONT);

  GLShader &shader = *m_shadow_depth_shader;
  shader.bind();
  shader.setUniform("u_model", model);
  shader.setUniform("u_light_view_projection", lightViewProjection);

  drawPhong(shader);

  if (collision_objects) {
    for (CollisionObject *co : *collision_objects) {
      shader.setUniform("u_model", model);
      co->render(shader);
    }
  }

  shader.setUniform("u_model", model);
  glCullFace(GL_BACK);
  glDisable(GL_CULL_FACE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ClothSimulator::renderSceneGeometry(
    GLShader &shader, const UserShader &active_shader, bool cel_materials,
    const Matrix4f &viewProjection, const Matrix4f &lightViewProjection) {

  Matrix4f model;
  model.setIdentity();
  shader.bind();
  shader.setUniform("u_model", model);
  shader.setUniform("u_view_projection", viewProjection);

  switch (active_shader.type_hint) {
  case WIREFRAME:
    shader.setUniform("u_color", color, false);
    drawWireframe(shader);
    break;
  case NORMALS:
    drawNormals(shader);
    break;
  case PHONG: {
    Vector3D cam_pos = camera.position();
    Vector3f cel_light_dir = m_cel_light_dir.normalized();

    shader.setUniform("u_color", color, false);
    shader.setUniform("u_cam_pos", Vector3f(cam_pos.x, cam_pos.y, cam_pos.z), false);
    shader.setUniform("u_light_pos", Vector3f(0.5f, 2.0f, 2.0f), false);
    shader.setUniform("u_light_intensity", Vector3f(3.0f, 3.0f, 3.0f), false);
    shader.setUniform("u_texture_1_size", Vector2f(m_gl_texture_1_size.x, m_gl_texture_1_size.y), false);
    shader.setUniform("u_texture_2_size", Vector2f(m_gl_texture_2_size.x, m_gl_texture_2_size.y), false);
    shader.setUniform("u_texture_3_size", Vector2f(m_gl_texture_3_size.x, m_gl_texture_3_size.y), false);
    shader.setUniform("u_texture_4_size", Vector2f(m_gl_texture_4_size.x, m_gl_texture_4_size.y), false);
    shader.setUniform("u_texture_1", 1, false);
    shader.setUniform("u_texture_2", 2, false);
    shader.setUniform("u_texture_3", 3, false);
    shader.setUniform("u_texture_4", 4, false);
    shader.setUniform("u_normal_scaling", m_normal_scaling, false);
    shader.setUniform("u_height_scaling", m_height_scaling, false);
    shader.setUniform("u_texture_cubemap", 5, false);

    if (cel_materials) {
      shader.setUniform("u_cel_dark_color", colorToVec3(m_cel_dark_color), false);
      shader.setUniform("u_cel_bright_color", colorToVec3(m_cel_bright_color), false);
      shader.setUniform("u_cel_light_dir", cel_light_dir, false);
      shader.setUniform("u_cel_light_pos", m_cel_light_pos, false);
      shader.setUniform("u_cel_light_type", (float)m_cel_light_type, false);
      shader.setUniform("u_cel_dark_threshold", m_cel_dark_threshold, false);
      shader.setUniform("u_cel_bright_threshold", m_cel_bright_threshold, false);
      shader.setUniform("u_cel_shadow_strength", m_cel_shadow_strength, false);
      shader.setUniform("u_cel_highlight_strength", m_cel_highlight_strength, false);
      shader.setUniform("u_cel_pattern_scale", m_cel_pattern_scale, false);
      shader.setUniform("u_cel_pattern_radius", m_cel_pattern_radius, false);
      shader.setUniform("u_cel_bands", m_cel_bands, false);
      shader.setUniform("u_cel_flat", 0.0f, false);
      shader.setUniform("u_use_mtl_style", 0.0f, false);
      shader.setUniform("u_light_view_projection", lightViewProjection, false);
      shader.setUniform("u_shadow_map", 6, false);
      shader.setUniform("u_shadow_bias", 0.0025f, false);
      shader.setUniform("u_shadow_strength", 0.65f, false);
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gl_texture_1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_gl_texture_2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_gl_texture_3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_gl_texture_4);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_gl_cubemap_tex);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, m_shadow_depth_tex);

    drawPhong(shader);
  } break;
  }

  if (collision_objects) {
    for (CollisionObject *co : *collision_objects) {
      shader.setUniform("u_model", model);
      if (active_shader.type_hint == PHONG) {
        bool use_mtl_style = cel_materials && co->isMesh() && co->hasMtl();
        shader.setUniform("u_use_mtl_style", use_mtl_style ? 1.0f : 0.0f, false);
        if (!use_mtl_style) {
          shader.setUniform("u_color", color, false);
        }
      }
      co->render(shader);
    }
  }

  shader.setUniform("u_model", model);

  if (cel_materials && m_cel_light_type == 1) {
    shader.setUniform("u_color", nanogui::Color(1.0f, 0.9f, 0.2f, 1.0f), false);
    shader.setUniform("u_cel_flat", 1.0f, false);
    Vector3D lp(m_cel_light_pos.x(), m_cel_light_pos.y(), m_cel_light_pos.z());
    m_light_sphere.draw_sphere(shader, lp, 0.06);
    shader.setUniform("u_model", model);
    shader.setUniform("u_cel_flat", 0.0f, false);
    shader.setUniform("u_color", color, false);
  }
}

void ClothSimulator::renderSceneToOffscreen(
    const UserShader &active_shader, bool cel_materials,
    const Matrix4f &lightViewProjection, double projection_aspect) {

  glBindFramebuffer(GL_FRAMEBUFFER, m_offscreen_fbo);
  glViewport(0, 0, screen_w, screen_h);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  GLenum bufs[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
  glDrawBuffers(2, bufs);
  glClearColor(m_background_color[0], m_background_color[1],
               m_background_color[2], m_background_color[3]);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  Matrix4f projection = projection_aspect > 0.0
                            ? getProjectionMatrixForAspect(projection_aspect)
                            : getProjectionMatrix();
  Matrix4f viewProjection = projection * getViewMatrix();
  GLShader &shader = *active_shader.nanogui_shader;
  renderSceneGeometry(shader, active_shader, cel_materials,
                      viewProjection, lightViewProjection);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ClothSimulator::renderEdgeComposite(GLuint target_fbo) {
  if (!m_edge_enabled || !m_fullscreen_edge_shader) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_offscreen_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_fbo);
    if (target_fbo != 0) {
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }
    glBlitFramebuffer(0, 0, screen_w, screen_h, 0, 0, screen_w, screen_h,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
  if (target_fbo != 0) {
    GLenum buf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &buf);
  }
  glViewport(0, 0, screen_w, screen_h);
  glDisable(GL_DEPTH_TEST);
  glClearColor(m_background_color[0], m_background_color[1],
               m_background_color[2], m_background_color[3]);
  glClear(GL_COLOR_BUFFER_BIT);

  GLShader &edge = *m_fullscreen_edge_shader;
  edge.bind();

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_scene_color_tex);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, m_scene_normal_tex);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, m_scene_depth_tex);

  edge.setUniform("u_colorTex", 0, false);
  edge.setUniform("u_normalTex", 1, false);
  edge.setUniform("u_depthTex", 2, false);
  edge.setUniform("u_depth_edge_thickness", m_depth_edge_thickness, false);
  edge.setUniform("u_normal_edge_thickness", m_normal_edge_thickness, false);
  edge.setUniform("u_depth_threshold", m_edge_depth_threshold, false);
  edge.setUniform("u_normal_threshold", m_edge_normal_threshold, false);
  edge.setUniform("u_edge_strength", m_edge_strength, false);
  edge.setUniform("u_edge_color", colorToVec3(m_edge_line_color), false);

  drawFullscreenQuad();

  glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ClothSimulator::renderBaselineToFramebuffer(double projection_aspect) {
  int phong_idx = shaderIndexByName("Phong");
  if (phong_idx < 0) {
    phong_idx = active_shader_idx;
  }

  const UserShader &baseline_shader = shaders[phong_idx];

  glBindFramebuffer(GL_FRAMEBUFFER, m_baseline_fbo);
  GLenum buf = GL_COLOR_ATTACHMENT0;
  glDrawBuffers(1, &buf);
  glViewport(0, 0, screen_w, screen_h);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glClearColor(m_background_color[0], m_background_color[1],
               m_background_color[2], m_background_color[3]);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  Matrix4f lightViewProjection = getLightViewProjectionMatrix();
  Matrix4f projection = projection_aspect > 0.0
                            ? getProjectionMatrixForAspect(projection_aspect)
                            : getProjectionMatrix();
  Matrix4f viewProjection = projection * getViewMatrix();
  GLShader &shader = *baseline_shader.nanogui_shader;
  renderSceneGeometry(shader, baseline_shader, false,
                      viewProjection, lightViewProjection);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ClothSimulator::renderCompareComposite() {
  if (!m_fullscreen_compare_shader) {
    renderEdgeComposite(0);
    return;
  }

  if (m_compare_animate) {
    float t = (float)glfwGetTime() * m_compare_anim_speed;
    m_compare_split = 0.5f + 0.45f * std::sin(t);
  }
  m_compare_split = std::max(0.0f, std::min(1.0f, m_compare_split));

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, screen_w, screen_h);
  glDisable(GL_DEPTH_TEST);
  glClearColor(m_background_color[0], m_background_color[1],
               m_background_color[2], m_background_color[3]);
  glClear(GL_COLOR_BUFFER_BIT);

  GLShader &compare = *m_fullscreen_compare_shader;
  compare.bind();

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_cel_composite_tex);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, m_baseline_color_tex);

  compare.setUniform("u_leftTex", 0, false);
  compare.setUniform("u_rightTex", 1, false);
  compare.setUniform("u_split", m_compare_split, false);
  compare.setUniform("u_compare_mode", (float)m_compare_layout, false);
  compare.setUniform("u_divider_width", 2.0f / std::max(1, screen_w), false);
  compare.setUniform("u_divider_color", Vector3f(1.0f, 1.0f, 1.0f), false);

  drawFullscreenQuad();

  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
}

void ClothSimulator::drawContents() {
  glEnable(GL_DEPTH_TEST);

  if (!is_paused) {
    vector<Vector3D> external_accelerations = {gravity};

    for (int i = 0; i < simulation_steps; i++) {
      cloth->simulate(frames_per_sec, simulation_steps, cp,
                      external_accelerations, collision_objects);
    }
  }

  if (m_light_rotate) {
    float t = (float)glfwGetTime() * m_light_rotate_speed;
    float cos_t = std::cos(t);
    float sin_t = std::sin(t);

    float r_dir = std::sqrt(m_cel_light_dir.x() * m_cel_light_dir.x() +
                            m_cel_light_dir.z() * m_cel_light_dir.z());
    if (r_dir < 1e-4f) r_dir = 1.0f;
    m_cel_light_dir.x() = r_dir * cos_t;
    m_cel_light_dir.z() = r_dir * sin_t;

    float r_pos = std::sqrt(m_cel_light_pos.x() * m_cel_light_pos.x() +
                            m_cel_light_pos.z() * m_cel_light_pos.z());
    if (r_pos < 1e-4f) r_pos = 1.0f;
    m_cel_light_pos.x() = r_pos * cos_t;
    m_cel_light_pos.z() = r_pos * sin_t;
  }

  Matrix4f lightViewProjection = getLightViewProjectionMatrix();
  renderShadowMap(lightViewProjection);

  const UserShader &active_shader = shaders[active_shader_idx];
  int cel_idx = shaderIndexByName("Cel");
  const UserShader &cel_shader = (cel_idx >= 0) ? shaders[cel_idx] : active_shader;

  if (m_offscreen_fbo == 0) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screen_w, screen_h);
    glEnable(GL_DEPTH_TEST);
    glClearColor(m_background_color[0], m_background_color[1],
                 m_background_color[2], m_background_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Matrix4f viewProjection = getProjectionMatrix() * getViewMatrix();
    GLShader &shader = *active_shader.nanogui_shader;
    renderSceneGeometry(shader, active_shader,
                        active_shader.display_name == "Cel",
                        viewProjection, lightViewProjection);
    return;
  }

  if (m_compare_enabled) {
    double compare_aspect = -1.0;
    if (m_compare_layout == 1) {
      compare_aspect = std::max(1, screen_w / 2) / (double)std::max(1, screen_h);
    }
    renderSceneToOffscreen(cel_shader, cel_shader.display_name == "Cel",
                           lightViewProjection, compare_aspect);
    renderEdgeComposite(m_cel_composite_fbo);
    renderBaselineToFramebuffer(compare_aspect);
    renderCompareComposite();
  } else {
    renderSceneToOffscreen(active_shader, active_shader.display_name == "Cel",
                           lightViewProjection);
    renderEdgeComposite(0);
  }
}

void ClothSimulator::drawWireframe(GLShader &shader) {
  int num_structural_springs =
      2 * cloth->num_width_points * cloth->num_height_points -
      cloth->num_width_points - cloth->num_height_points;
  int num_shear_springs =
      2 * (cloth->num_width_points - 1) * (cloth->num_height_points - 1);
  int num_bending_springs = num_structural_springs - cloth->num_width_points -
                            cloth->num_height_points;

  int num_springs = cp->enable_structural_constraints * num_structural_springs +
                    cp->enable_shearing_constraints * num_shear_springs +
                    cp->enable_bending_constraints * num_bending_springs;

  MatrixXf positions(4, num_springs * 2);
  MatrixXf normals(4, num_springs * 2);

  // Draw springs as lines

  int si = 0;

  for (int i = 0; i < cloth->springs.size(); i++) {
    Spring s = cloth->springs[i];

    if ((s.spring_type == STRUCTURAL && !cp->enable_structural_constraints) ||
        (s.spring_type == SHEARING && !cp->enable_shearing_constraints) ||
        (s.spring_type == BENDING && !cp->enable_bending_constraints)) {
      continue;
    }

    Vector3D pa = s.pm_a->position;
    Vector3D pb = s.pm_b->position;

    Vector3D na = s.pm_a->normal();
    Vector3D nb = s.pm_b->normal();

    positions.col(si) << pa.x, pa.y, pa.z, 1.0;
    positions.col(si + 1) << pb.x, pb.y, pb.z, 1.0;

    normals.col(si) << na.x, na.y, na.z, 0.0;
    normals.col(si + 1) << nb.x, nb.y, nb.z, 0.0;

    si += 2;
  }

  //shader.setUniform("u_color", nanogui::Color(1.0f, 1.0f, 1.0f, 1.0f), false);
  shader.uploadAttrib("in_position", positions, false);
  // Commented out: the wireframe shader does not have this attribute
  //shader.uploadAttrib("in_normal", normals);

  shader.drawArray(GL_LINES, 0, num_springs * 2);
}

void ClothSimulator::drawNormals(GLShader &shader) {
  int num_tris = cloth->clothMesh->triangles.size();

  MatrixXf positions(4, num_tris * 3);
  MatrixXf normals(4, num_tris * 3);

  for (int i = 0; i < num_tris; i++) {
    Triangle *tri = cloth->clothMesh->triangles[i];

    Vector3D p1 = tri->pm1->position;
    Vector3D p2 = tri->pm2->position;
    Vector3D p3 = tri->pm3->position;

    Vector3D n1 = tri->pm1->normal();
    Vector3D n2 = tri->pm2->normal();
    Vector3D n3 = tri->pm3->normal();

    positions.col(i * 3) << p1.x, p1.y, p1.z, 1.0;
    positions.col(i * 3 + 1) << p2.x, p2.y, p2.z, 1.0;
    positions.col(i * 3 + 2) << p3.x, p3.y, p3.z, 1.0;

    normals.col(i * 3) << n1.x, n1.y, n1.z, 0.0;
    normals.col(i * 3 + 1) << n2.x, n2.y, n2.z, 0.0;
    normals.col(i * 3 + 2) << n3.x, n3.y, n3.z, 0.0;
  }

  shader.uploadAttrib("in_position", positions, false);
  shader.uploadAttrib("in_normal", normals, false);

  shader.drawArray(GL_TRIANGLES, 0, num_tris * 3);
}

void ClothSimulator::drawPhong(GLShader &shader) {
  int num_tris = cloth->clothMesh->triangles.size();

  MatrixXf positions(4, num_tris * 3);
  MatrixXf normals(4, num_tris * 3);
  MatrixXf uvs(2, num_tris * 3);
  MatrixXf tangents(4, num_tris * 3);

  for (int i = 0; i < num_tris; i++) {
    Triangle *tri = cloth->clothMesh->triangles[i];

    Vector3D p1 = tri->pm1->position;
    Vector3D p2 = tri->pm2->position;
    Vector3D p3 = tri->pm3->position;

    Vector3D n1 = tri->pm1->normal();
    Vector3D n2 = tri->pm2->normal();
    Vector3D n3 = tri->pm3->normal();

    positions.col(i * 3    ) << p1.x, p1.y, p1.z, 1.0;
    positions.col(i * 3 + 1) << p2.x, p2.y, p2.z, 1.0;
    positions.col(i * 3 + 2) << p3.x, p3.y, p3.z, 1.0;

    normals.col(i * 3    ) << n1.x, n1.y, n1.z, 0.0;
    normals.col(i * 3 + 1) << n2.x, n2.y, n2.z, 0.0;
    normals.col(i * 3 + 2) << n3.x, n3.y, n3.z, 0.0;
    
    uvs.col(i * 3    ) << tri->uv1.x, tri->uv1.y;
    uvs.col(i * 3 + 1) << tri->uv2.x, tri->uv2.y;
    uvs.col(i * 3 + 2) << tri->uv3.x, tri->uv3.y;
    
    tangents.col(i * 3    ) << 1.0, 0.0, 0.0, 1.0;
    tangents.col(i * 3 + 1) << 1.0, 0.0, 0.0, 1.0;
    tangents.col(i * 3 + 2) << 1.0, 0.0, 0.0, 1.0;
  }


  shader.uploadAttrib("in_position", positions, false);
  shader.uploadAttrib("in_normal", normals, false);
  shader.uploadAttrib("in_uv", uvs, false);
  shader.uploadAttrib("in_tangent", tangents, false);

  shader.drawArray(GL_TRIANGLES, 0, num_tris * 3);
}


void ClothSimulator::initFramebuffer() {
    destroyFramebuffer();

    glGenFramebuffers(1, &m_offscreen_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_offscreen_fbo);

    // Scene color texture
    glGenTextures(1, &m_scene_color_tex);
    glBindTexture(GL_TEXTURE_2D, m_scene_color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screen_w, screen_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_scene_color_tex, 0);

    // Scene normal texture
    glGenTextures(1, &m_scene_normal_tex);
    glBindTexture(GL_TEXTURE_2D, m_scene_normal_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, screen_w, screen_h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_scene_normal_tex, 0);

    // Depth texture
    glGenTextures(1, &m_scene_depth_tex);
    glBindTexture(GL_TEXTURE_2D, m_scene_depth_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, screen_w, screen_h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_scene_depth_tex, 0);

    GLenum bufs[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, bufs);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "Offscreen framebuffer is not complete!" << std::endl;
    }

    glGenFramebuffers(1, &m_cel_composite_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_cel_composite_fbo);
    glGenTextures(1, &m_cel_composite_tex);
    glBindTexture(GL_TEXTURE_2D, m_cel_composite_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screen_w, screen_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_cel_composite_tex, 0);
    GLenum celBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &celBuf);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "Cel composite framebuffer is not complete!" << std::endl;
    }

    glGenFramebuffers(1, &m_baseline_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_baseline_fbo);
    glGenTextures(1, &m_baseline_color_tex);
    glBindTexture(GL_TEXTURE_2D, m_baseline_color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screen_w, screen_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_baseline_color_tex, 0);

    glGenTextures(1, &m_baseline_depth_tex);
    glBindTexture(GL_TEXTURE_2D, m_baseline_depth_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, screen_w, screen_h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_baseline_depth_tex, 0);
    GLenum baselineBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &baselineBuf);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "Baseline framebuffer is not complete!" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ClothSimulator::destroyFramebuffer() {
    if (m_baseline_depth_tex != 0) {
        glDeleteTextures(1, &m_baseline_depth_tex);
        m_baseline_depth_tex = 0;
    }
    if (m_baseline_color_tex != 0) {
        glDeleteTextures(1, &m_baseline_color_tex);
        m_baseline_color_tex = 0;
    }
    if (m_baseline_fbo != 0) {
        glDeleteFramebuffers(1, &m_baseline_fbo);
        m_baseline_fbo = 0;
    }
    if (m_cel_composite_tex != 0) {
        glDeleteTextures(1, &m_cel_composite_tex);
        m_cel_composite_tex = 0;
    }
    if (m_cel_composite_fbo != 0) {
        glDeleteFramebuffers(1, &m_cel_composite_fbo);
        m_cel_composite_fbo = 0;
    }
    if (m_scene_color_tex != 0) {
        glDeleteTextures(1, &m_scene_color_tex);
        m_scene_color_tex = 0;
    }
    if (m_scene_normal_tex != 0) {
        glDeleteTextures(1, &m_scene_normal_tex);
        m_scene_normal_tex = 0;
    }
    if (m_scene_depth_tex != 0) {
        glDeleteTextures(1, &m_scene_depth_tex);
        m_scene_depth_tex = 0;
    }
    if (m_offscreen_fbo != 0) {
        glDeleteFramebuffers(1, &m_offscreen_fbo);
        m_offscreen_fbo = 0;
    }
}

void ClothSimulator::resizeFramebuffer(int width, int height) {
    screen_w = width;
    screen_h = height;
    initFramebuffer();
}

void ClothSimulator::initFullscreenQuad() {
    if (m_fullscreen_vao != 0) {
        glDeleteVertexArrays(1, &m_fullscreen_vao);
        m_fullscreen_vao = 0;
    }
    if (m_fullscreen_vbo != 0) {
        glDeleteBuffers(1, &m_fullscreen_vbo);
        m_fullscreen_vbo = 0;
    }

    float quadVertices[] = {
        // position   // uv
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,

        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f
    };

    glGenVertexArrays(1, &m_fullscreen_vao);
    glGenBuffers(1, &m_fullscreen_vbo);

    glBindVertexArray(m_fullscreen_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_fullscreen_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void ClothSimulator::drawFullscreenQuad() {
    glBindVertexArray(m_fullscreen_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
// ----------------------------------------------------------------------------
// CAMERA CALCULATIONS
//
// OpenGL 3.1 deprecated the fixed pipeline, so we lose a lot of useful OpenGL
// functions that have to be recreated here.
// ----------------------------------------------------------------------------

void ClothSimulator::resetCamera() { camera.copy_placement(canonicalCamera); }

Matrix4f ClothSimulator::getProjectionMatrix() {
  return getProjectionMatrixForAspect(camera.aspect_ratio());
}

Matrix4f ClothSimulator::getProjectionMatrixForAspect(double aspect) {
  Matrix4f perspective;
  perspective.setZero();

  double cam_near = camera.near_clip();
  double cam_far = camera.far_clip();
  aspect = std::max(1e-6, aspect);

  double theta = camera.v_fov() * PI / 360;
  double range = cam_far - cam_near;
  double invtan = 1. / tanf(theta);

  perspective(0, 0) = invtan / aspect;
  perspective(1, 1) = invtan;
  perspective(2, 2) = -(cam_near + cam_far) / range;
  perspective(3, 2) = -1;
  perspective(2, 3) = -2 * cam_near * cam_far / range;
  perspective(3, 3) = 0;

  return perspective;
}

Matrix4f ClothSimulator::getViewMatrix() {
  Matrix4f lookAt;
  Matrix3f R;

  lookAt.setZero();

  // Convert CGL vectors to Eigen vectors
  // TODO: Find a better way to do this!

  CGL::Vector3D c_pos = camera.position();
  CGL::Vector3D c_udir = camera.up_dir();
  CGL::Vector3D c_target = camera.view_point();

  Vector3f eye(c_pos.x, c_pos.y, c_pos.z);
  Vector3f up(c_udir.x, c_udir.y, c_udir.z);
  Vector3f target(c_target.x, c_target.y, c_target.z);

  R.col(2) = (eye - target).normalized();
  R.col(0) = up.cross(R.col(2)).normalized();
  R.col(1) = R.col(2).cross(R.col(0));

  lookAt.topLeftCorner<3, 3>() = R.transpose();
  lookAt.topRightCorner<3, 1>() = -R.transpose() * eye;
  lookAt(3, 3) = 1.0f;

  return lookAt;
}

Matrix4f ClothSimulator::getLightViewProjectionMatrix() {
    Vector3f lightDir = m_cel_light_dir.normalized();

    Vector3D target3 = camera.view_point();
    Vector3f target(target3.x, target3.y, target3.z);

    float dist = canonical_view_distance * 2.0f;
    Vector3f eye = target - lightDir * dist;

    Vector3f up(0.0f, 1.0f, 0.0f);
    if (fabs(lightDir.dot(up)) > 0.95f) {
        up = Vector3f(1.0f, 0.0f, 0.0f);
    }

    // lookAt
    Vector3f z = (eye - target).normalized();
    Vector3f x = up.cross(z).normalized();
    Vector3f y = z.cross(x);

    Matrix4f view;
    view.setIdentity();
    view(0, 0) = x.x(); view(0, 1) = x.y(); view(0, 2) = x.z(); view(0, 3) = -x.dot(eye);
    view(1, 0) = y.x(); view(1, 1) = y.y(); view(1, 2) = y.z(); view(1, 3) = -y.dot(eye);
    view(2, 0) = z.x(); view(2, 1) = z.y(); view(2, 2) = z.z(); view(2, 3) = -z.dot(eye);

    float r = canonical_view_distance * 1.5f;
    float n = 0.1f;
    float f = canonical_view_distance * 6.0f;

    Matrix4f proj;
    proj.setZero();
    proj(0, 0) = 1.0f / r;
    proj(1, 1) = 1.0f / r;
    proj(2, 2) = -2.0f / (f - n);
    proj(2, 3) = -(f + n) / (f - n);
    proj(3, 3) = 1.0f;

    return proj * view;
}

// ----------------------------------------------------------------------------
// EVENT HANDLING
// ----------------------------------------------------------------------------

bool ClothSimulator::cursorPosCallbackEvent(double x, double y) {
  if (left_down && !middle_down && !right_down) {
    if (ctrl_down) {
      mouseRightDragged(x, y);
    } else {
      mouseLeftDragged(x, y);
    }
  } else if (!left_down && !middle_down && right_down) {
    mouseRightDragged(x, y);
  } else if (!left_down && !middle_down && !right_down) {
    mouseMoved(x, y);
  }

  mouse_x = x;
  mouse_y = y;

  return true;
}

bool ClothSimulator::mouseButtonCallbackEvent(int button, int action,
                                              int modifiers) {
  switch (action) {
  case GLFW_PRESS:
    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
      left_down = true;
      break;
    case GLFW_MOUSE_BUTTON_MIDDLE:
      middle_down = true;
      break;
    case GLFW_MOUSE_BUTTON_RIGHT:
      right_down = true;
      break;
    }
    return true;

  case GLFW_RELEASE:
    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
      left_down = false;
      break;
    case GLFW_MOUSE_BUTTON_MIDDLE:
      middle_down = false;
      break;
    case GLFW_MOUSE_BUTTON_RIGHT:
      right_down = false;
      break;
    }
    return true;
  }

  return false;
}

void ClothSimulator::initShadowMap() {
    destroyShadowMap();

    glGenFramebuffers(1, &m_shadow_fbo);

    glGenTextures(1, &m_shadow_depth_tex);
    glBindTexture(GL_TEXTURE_2D, m_shadow_depth_tex);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_DEPTH_COMPONENT24,
        m_shadow_map_size,
        m_shadow_map_size,
        0,
        GL_DEPTH_COMPONENT,
        GL_FLOAT,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_fbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D,
        m_shadow_depth_tex,
        0
    );

    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "Shadow framebuffer is not complete!" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ClothSimulator::destroyShadowMap() {
    if (m_shadow_depth_tex != 0) {
        glDeleteTextures(1, &m_shadow_depth_tex);
        m_shadow_depth_tex = 0;
    }
    if (m_shadow_fbo != 0) {
        glDeleteFramebuffers(1, &m_shadow_fbo);
        m_shadow_fbo = 0;
    }
}

void ClothSimulator::mouseMoved(double x, double y) { y = screen_h - y; }

void ClothSimulator::mouseLeftDragged(double x, double y) {
  float dx = x - mouse_x;
  float dy = y - mouse_y;

  camera.rotate_by(-dy * (PI / screen_h), -dx * (PI / screen_w));
}

void ClothSimulator::mouseRightDragged(double x, double y) {
  camera.move_by(mouse_x - x, y - mouse_y, canonical_view_distance);
}

bool ClothSimulator::keyCallbackEvent(int key, int scancode, int action,
                                      int mods) {
  ctrl_down = (bool)(mods & GLFW_MOD_CONTROL);

  if (action == GLFW_PRESS) {
    switch (key) {
    case GLFW_KEY_ESCAPE:
      is_alive = false;
      break;
    case 'r':
    case 'R':
      cloth->reset();
      break;
    case ' ':
      resetCamera();
      break;
    case 'p':
    case 'P':
      is_paused = !is_paused;
      break;
    case 'n':
    case 'N':
      if (is_paused) {
        is_paused = false;
        drawContents();
        is_paused = true;
      }
      break;
    }
  }

  double pan_speed = canonical_view_distance * 50.0;

  if (action == GLFW_PRESS || action == GLFW_REPEAT) {
    switch (key) {
    case GLFW_KEY_W:
    case GLFW_KEY_UP:
      camera.move_by(0, pan_speed, canonical_view_distance);
      break;
    case GLFW_KEY_S:
    case GLFW_KEY_DOWN:
      camera.move_by(0, -pan_speed, canonical_view_distance);
      break;
    case GLFW_KEY_A:
    case GLFW_KEY_LEFT:
      camera.move_by(-pan_speed, 0, canonical_view_distance);
      break;
    case GLFW_KEY_D:
    case GLFW_KEY_RIGHT:
      camera.move_by(pan_speed, 0, canonical_view_distance);
      break;
    }
  }

  return true;
}

bool ClothSimulator::dropCallbackEvent(int count, const char **filenames) {
  return true;
}

bool ClothSimulator::scrollCallbackEvent(double x, double y) {
  camera.move_forward(y * scroll_rate);
  return true;
}

bool ClothSimulator::resizeCallbackEvent(int width, int height) {
    screen_w = width;
    screen_h = height;

    camera.set_screen_size(screen_w, screen_h);
    resizeFramebuffer(width, height);
    return true;
}

void ClothSimulator::initGUI(Screen *screen) {
  Window *window;
  
  window = new Window(screen, "Simulation");
  window->setPosition(Vector2i(default_window_size(0) - 245, 15));
  window->setLayout(new GroupLayout(15, 6, 14, 5));

  // Spring types

  new Label(window, "Spring types", "sans-bold");

  {
    Button *b = new Button(window, "structural");
    b->setFlags(Button::ToggleButton);
    b->setPushed(cp->enable_structural_constraints);
    b->setFontSize(14);
    b->setChangeCallback(
        [this](bool state) { cp->enable_structural_constraints = state; });

    b = new Button(window, "shearing");
    b->setFlags(Button::ToggleButton);
    b->setPushed(cp->enable_shearing_constraints);
    b->setFontSize(14);
    b->setChangeCallback(
        [this](bool state) { cp->enable_shearing_constraints = state; });

    b = new Button(window, "bending");
    b->setFlags(Button::ToggleButton);
    b->setPushed(cp->enable_bending_constraints);
    b->setFontSize(14);
    b->setChangeCallback(
        [this](bool state) { cp->enable_bending_constraints = state; });
  }

  // Mass-spring parameters

  new Label(window, "Parameters", "sans-bold");

  {
    Widget *panel = new Widget(window);
    GridLayout *layout =
        new GridLayout(Orientation::Horizontal, 2, Alignment::Middle, 5, 5);
    layout->setColAlignment({Alignment::Maximum, Alignment::Fill});
    layout->setSpacing(0, 10);
    panel->setLayout(layout);

    new Label(panel, "density :", "sans-bold");

    FloatBox<double> *fb = new FloatBox<double>(panel);
    fb->setEditable(true);
    fb->setFixedSize(Vector2i(100, 20));
    fb->setFontSize(14);
    fb->setValue(cp->density / 10);
    fb->setUnits("g/cm^2");
    fb->setSpinnable(true);
    fb->setCallback([this](float value) { cp->density = (double)(value * 10); });

    new Label(panel, "ks :", "sans-bold");

    fb = new FloatBox<double>(panel);
    fb->setEditable(true);
    fb->setFixedSize(Vector2i(100, 20));
    fb->setFontSize(14);
    fb->setValue(cp->ks);
    fb->setUnits("N/m");
    fb->setSpinnable(true);
    fb->setMinValue(0);
    fb->setCallback([this](float value) { cp->ks = value; });
  }

  // Simulation constants

  new Label(window, "Simulation", "sans-bold");

  {
    Widget *panel = new Widget(window);
    GridLayout *layout =
        new GridLayout(Orientation::Horizontal, 2, Alignment::Middle, 5, 5);
    layout->setColAlignment({Alignment::Maximum, Alignment::Fill});
    layout->setSpacing(0, 10);
    panel->setLayout(layout);

    new Label(panel, "frames/s :", "sans-bold");

    IntBox<int> *fsec = new IntBox<int>(panel);
    fsec->setEditable(true);
    fsec->setFixedSize(Vector2i(100, 20));
    fsec->setFontSize(14);
    fsec->setValue(frames_per_sec);
    fsec->setSpinnable(true);
    fsec->setCallback([this](int value) { frames_per_sec = value; });

    new Label(panel, "steps/frame :", "sans-bold");

    IntBox<int> *num_steps = new IntBox<int>(panel);
    num_steps->setEditable(true);
    num_steps->setFixedSize(Vector2i(100, 20));
    num_steps->setFontSize(14);
    num_steps->setValue(simulation_steps);
    num_steps->setSpinnable(true);
    num_steps->setMinValue(0);
    num_steps->setCallback([this](int value) { simulation_steps = value; });
  }

  // Damping slider and textbox

  new Label(window, "Damping", "sans-bold");

  {
    Widget *panel = new Widget(window);
    panel->setLayout(
        new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 5));

    Slider *slider = new Slider(panel);
    slider->setValue(cp->damping);
    slider->setFixedWidth(105);

    TextBox *percentage = new TextBox(panel);
    percentage->setFixedWidth(75);
    percentage->setValue(to_string(cp->damping));
    percentage->setUnits("%");
    percentage->setFontSize(14);

    slider->setCallback([percentage](float value) {
      percentage->setValue(std::to_string(value));
    });
    slider->setFinalCallback([&](float value) {
      cp->damping = (double)value;
      // cout << "Final slider value: " << (int)(value * 100) << endl;
    });
  }

  // Gravity

  new Label(window, "Gravity", "sans-bold");

  {
    Widget *panel = new Widget(window);
    GridLayout *layout =
        new GridLayout(Orientation::Horizontal, 2, Alignment::Middle, 5, 5);
    layout->setColAlignment({Alignment::Maximum, Alignment::Fill});
    layout->setSpacing(0, 10);
    panel->setLayout(layout);

    new Label(panel, "x :", "sans-bold");

    FloatBox<double> *fb = new FloatBox<double>(panel);
    fb->setEditable(true);
    fb->setFixedSize(Vector2i(100, 20));
    fb->setFontSize(14);
    fb->setValue(gravity.x);
    fb->setUnits("m/s^2");
    fb->setSpinnable(true);
    fb->setCallback([this](float value) { gravity.x = value; });

    new Label(panel, "y :", "sans-bold");

    fb = new FloatBox<double>(panel);
    fb->setEditable(true);
    fb->setFixedSize(Vector2i(100, 20));
    fb->setFontSize(14);
    fb->setValue(gravity.y);
    fb->setUnits("m/s^2");
    fb->setSpinnable(true);
    fb->setCallback([this](float value) { gravity.y = value; });

    new Label(panel, "z :", "sans-bold");

    fb = new FloatBox<double>(panel);
    fb->setEditable(true);
    fb->setFixedSize(Vector2i(100, 20));
    fb->setFontSize(14);
    fb->setValue(gravity.z);
    fb->setUnits("m/s^2");
    fb->setSpinnable(true);
    fb->setCallback([this](float value) { gravity.z = value; });
  }
  
  window = new Window(screen, "Appearance");
  window->setPosition(Vector2i(15, 15));
  window->setFixedSize(Vector2i(260, std::max(360, default_window_size(1) - 30)));
  window->setLayout(new GroupLayout(10, 6, 10, 5));

  VScrollPanel *appearance_scroll = new VScrollPanel(window);
  appearance_scroll->setFixedSize(
      Vector2i(240, std::max(300, default_window_size(1) - 75)));

  Widget *appearance = new Widget(appearance_scroll);
  appearance->setLayout(new GroupLayout(15, 6, 14, 5));

  // Appearance

  {
    ComboBox *cb = new ComboBox(appearance, shaders_combobox_names);
    cb->setFontSize(14);
    cb->setCallback(
        [this, screen](int idx) { active_shader_idx = idx; });
    cb->setSelectedIndex(active_shader_idx);
  }

  // Shared refresh callbacks for Appearance widgets (used to update UI after loading preset)
  auto refreshes = std::make_shared<std::vector<std::function<void()>>>();

  // Shader Parameters

  new Label(appearance, "Color", "sans-bold");

  {
    ColorWheel *cw = new ColorWheel(appearance, color);
    cw->setColor(this->color);
    cw->setCallback(
        [this](const nanogui::Color &color) { this->color = color; });
    refreshes->push_back([cw, this] { cw->setColor(this->color); });
  }

  new Label(appearance, "Parameters", "sans-bold");

  {
    Widget *panel = new Widget(appearance);
    GridLayout *layout =
        new GridLayout(Orientation::Horizontal, 2, Alignment::Middle, 5, 5);
    layout->setColAlignment({Alignment::Maximum, Alignment::Fill});
    layout->setSpacing(0, 10);
    panel->setLayout(layout);

    new Label(panel, "Normal :", "sans-bold");

    FloatBox<double> *fb = new FloatBox<double>(panel);
    fb->setEditable(true);
    fb->setFixedSize(Vector2i(100, 20));
    fb->setFontSize(14);
    fb->setValue(this->m_normal_scaling);
    fb->setSpinnable(true);
    fb->setCallback([this](float value) { this->m_normal_scaling = value; });
    refreshes->push_back([fb, this] { fb->setValue((double)this->m_normal_scaling); });

    new Label(panel, "Height :", "sans-bold");

    fb = new FloatBox<double>(panel);
    fb->setEditable(true);
    fb->setFixedSize(Vector2i(100, 20));
    fb->setFontSize(14);
    fb->setValue(this->m_height_scaling);
    fb->setSpinnable(true);
    fb->setCallback([this](float value) { this->m_height_scaling = value; });
    refreshes->push_back([fb, this] { fb->setValue((double)this->m_height_scaling); });
  }

  // Cel shader live controls

  new Label(appearance, "Cel Shader", "sans-bold");

  {
    Widget *panel = new Widget(appearance);
    GridLayout *layout =
        new GridLayout(Orientation::Horizontal, 2, Alignment::Middle, 5, 5);
    layout->setColAlignment({Alignment::Maximum, Alignment::Fill});
    layout->setSpacing(0, 8);
    panel->setLayout(layout);

    auto add_float = [panel, refreshes](const std::string &label, float *target,
                                        float minv, float maxv, float step) {
      new Label(panel, label, "sans-bold");
      FloatBox<float> *fb = new FloatBox<float>(panel);
      fb->setEditable(true);
      fb->setFixedSize(Vector2i(100, 20));
      fb->setFontSize(14);
      fb->setValue(*target);
      fb->setMinValue(minv);
      fb->setMaxValue(maxv);
      fb->setValueIncrement(step);
      fb->setSpinnable(true);
      fb->setCallback([target](float v) { *target = v; });
      refreshes->push_back([fb, target] { fb->setValue(*target); });
    };

    new Label(panel, "Dark color :", "sans-bold");
    ColorPicker *dark_picker = new ColorPicker(panel, m_cel_dark_color);
    dark_picker->setFixedSize(Vector2i(100, 20));
    dark_picker->setFontSize(14);
    dark_picker->setCallback(
        [this](const nanogui::Color &c) { m_cel_dark_color = c; });
    refreshes->push_back([dark_picker, this] { dark_picker->setColor(this->m_cel_dark_color); });

    new Label(panel, "Bright color :", "sans-bold");
    ColorPicker *bright_picker = new ColorPicker(panel, m_cel_bright_color);
    bright_picker->setFixedSize(Vector2i(100, 20));
    bright_picker->setFontSize(14);
    bright_picker->setCallback(
        [this](const nanogui::Color &c) { m_cel_bright_color = c; });
    refreshes->push_back([bright_picker, this] { bright_picker->setColor(this->m_cel_bright_color); });

    // Texture picker
    new Label(panel, "Pattern tex :", "sans-bold");
    {
      Widget *row = new Widget(panel);
      row->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 4));

      auto tex_label = std::make_shared<std::string>(
          m_cel_texture_path.empty() ? "texture_1.png"
          : m_cel_texture_path.substr(m_cel_texture_path.find_last_of("/\\") + 1));

      Label *name_lbl = new Label(row, *tex_label, "sans");
      name_lbl->setFixedWidth(60);
      name_lbl->setFontSize(12);

      // update filename label when preset loads
      refreshes->push_back([name_lbl, this] {
        std::string fname = this->m_cel_texture_path.empty() ? "texture_1.png" : this->m_cel_texture_path.substr(this->m_cel_texture_path.find_last_of("/\\") + 1);
        name_lbl->setCaption(fname);
      });

      Button *browse_btn = new Button(row, "Browse");
      browse_btn->setFontSize(12);
      browse_btn->setCallback([this, name_lbl] {
        std::string path = nanogui::file_dialog(
            {{"png","PNG"}, {"jpg","JPEG"}, {"bmp","BMP"}, {"tga","TGA"}}, false);
        if (path.empty()) return;
        reloadCelTexture(path);
        std::string fname = path.substr(path.find_last_of("/\\") + 1);
        name_lbl->setCaption(fname);
      });
    }

    add_float("Dark thresh :", &m_cel_dark_threshold, 0.0f, 1.0f, 0.02f);
    add_float("Bright thresh :", &m_cel_bright_threshold, 0.0f, 1.0f, 0.02f);
    add_float("Shadow str :", &m_cel_shadow_strength, 0.0f, 1.0f, 0.05f);
    add_float("Highlight str :", &m_cel_highlight_strength, 0.0f, 2.0f, 0.05f);
    add_float("Pattern scale :", &m_cel_pattern_scale, 0.05f, 10.0f, 0.5f);
    add_float("Bands :", &m_cel_bands, 1.0f, 16.0f, 1.0f);

    auto make_axis = [panel, refreshes](const std::string &label, float *target) {
      new Label(panel, label, "sans-bold");
      FloatBox<float> *fb = new FloatBox<float>(panel);
      fb->setEditable(true);
      fb->setFixedSize(Vector2i(100, 20));
      fb->setFontSize(14);
      fb->setSpinnable(true);
      fb->setValueIncrement(0.1f);
      fb->setValue(*target);
      fb->setCallback([target](float v) { *target = v; });
      refreshes->push_back([fb, target] { fb->setValue(*target); });
    };
    new Label(panel, "Light type :", "sans-bold");
    ComboBox *light_type_cb = new ComboBox(panel, {"Directional", "Point"});
    light_type_cb->setSelectedIndex(m_cel_light_type);
    light_type_cb->setFontSize(14);
    light_type_cb->setFixedSize(Vector2i(100, 20));
    light_type_cb->setCallback([this](int idx) { m_cel_light_type = idx; });
    refreshes->push_back([light_type_cb, this] { light_type_cb->setSelectedIndex(m_cel_light_type); });

    make_axis("Light dir x :", &m_cel_light_dir.x());
    make_axis("Light dir y :", &m_cel_light_dir.y());
    make_axis("Light dir z :", &m_cel_light_dir.z());

    make_axis("Light pos x :", &m_cel_light_pos.x());
    make_axis("Light pos y :", &m_cel_light_pos.y());
    make_axis("Light pos z :", &m_cel_light_pos.z());

    new Label(panel, "Rotate light :", "sans-bold");
    {
      Button *rot_btn = new Button(panel, "Off");
      rot_btn->setFlags(Button::ToggleButton);
      rot_btn->setPushed(m_light_rotate);
      rot_btn->setFixedSize(Vector2i(100, 20));
      rot_btn->setFontSize(14);
      rot_btn->setChangeCallback([this, rot_btn](bool state) {
        m_light_rotate = state;
        rot_btn->setCaption(state ? "On" : "Off");
      });
      refreshes->push_back([rot_btn, this] {
        rot_btn->setPushed(m_light_rotate);
        rot_btn->setCaption(m_light_rotate ? "On" : "Off");
      });
    }

    make_axis("Rotate speed :", &m_light_rotate_speed);
  }

  // Cel preset save/load buttons

  // Edge controls
  new Label(appearance, "Edge", "sans-bold");
  {
    Widget *panel = new Widget(appearance);
    GridLayout *layout =
        new GridLayout(Orientation::Horizontal, 2, Alignment::Middle, 5, 5);
    layout->setColAlignment({Alignment::Maximum, Alignment::Fill});
    layout->setSpacing(0, 10);
    panel->setLayout(layout);

    new Label(panel, "Depth thick :", "sans-bold");
    FloatBox<float>* fb = new FloatBox<float>(panel);
    fb->setEditable(true);
    fb->setFixedSize(Vector2i(100, 20));
    fb->setFontSize(14);
    fb->setValue(m_depth_edge_thickness);
    fb->setSpinnable(true);
    fb->setCallback([this](float value) {
        m_depth_edge_thickness = value;
        });

    new Label(panel, "Normal thick :", "sans-bold");
    fb = new FloatBox<float>(panel);
    fb->setEditable(true);
    fb->setFixedSize(Vector2i(100, 20));
    fb->setFontSize(14);
    fb->setValue(m_normal_edge_thickness);
    fb->setSpinnable(true);
    fb->setCallback([this](float value) {
        m_normal_edge_thickness = value;
        });

    // expose background color in same panel? No, add separate control below.

    new Label(panel, "Strength :", "sans-bold");
    fb = new FloatBox<float>(panel);
    fb->setEditable(true);
    fb->setFixedSize(Vector2i(100, 20));
    fb->setFontSize(14);
    fb->setValue(m_edge_strength);
    fb->setSpinnable(true);
    fb->setCallback([this](float value) { m_edge_strength = value; });

    new Label(panel, "Color :", "sans-bold");
    ColorPicker *edge_picker = new ColorPicker(panel, m_edge_line_color);
    edge_picker->setFixedSize(Vector2i(100, 20));
    edge_picker->setFontSize(14);
    edge_picker->setCallback([this](const nanogui::Color &c) { m_edge_line_color = c; });
  }

  new Label(appearance, "Mesh", "sans-bold");
  {
    Widget *panel = new Widget(appearance);
    GridLayout *layout =
        new GridLayout(Orientation::Horizontal, 2, Alignment::Middle, 5, 5);
    layout->setColAlignment({Alignment::Maximum, Alignment::Fill});
    layout->setSpacing(0, 10);
    panel->setLayout(layout);

    auto add_mesh_float = [panel](const std::string &label, float *target,
                                  float step) {
      new Label(panel, label, "sans-bold");
      FloatBox<float> *fb = new FloatBox<float>(panel);
      fb->setEditable(true);
      fb->setFixedSize(Vector2i(100, 20));
      fb->setFontSize(14);
      fb->setValue(*target);
      fb->setValueIncrement(step);
      fb->setSpinnable(true);
      fb->setCallback([target](float value) { *target = value; });
    };

    add_mesh_float("Scale x :", &m_gui_mesh_scale.x(), 0.1f);
    add_mesh_float("Scale y :", &m_gui_mesh_scale.y(), 0.1f);
    add_mesh_float("Scale z :", &m_gui_mesh_scale.z(), 0.1f);
    add_mesh_float("Trans x :", &m_gui_mesh_translate.x(), 0.1f);
    add_mesh_float("Trans y :", &m_gui_mesh_translate.y(), 0.1f);
    add_mesh_float("Trans z :", &m_gui_mesh_translate.z(), 0.1f);

    new Label(panel, "Friction :", "sans-bold");
    FloatBox<float> *friction_box = new FloatBox<float>(panel);
    friction_box->setEditable(true);
    friction_box->setFixedSize(Vector2i(100, 20));
    friction_box->setFontSize(14);
    friction_box->setValue(m_gui_mesh_friction);
    friction_box->setMinValue(0.0f);
    friction_box->setMaxValue(1.0f);
    friction_box->setValueIncrement(0.05f);
    friction_box->setSpinnable(true);
    friction_box->setCallback([this](float value) {
      m_gui_mesh_friction = std::max(0.0f, std::min(1.0f, value));
    });

    new Label(panel, "Collide :", "sans-bold");
    Button *mesh_collide_btn = new Button(panel, m_mesh_collision_enabled ? "On" : "Off");
    mesh_collide_btn->setFlags(Button::ToggleButton);
    mesh_collide_btn->setPushed(m_mesh_collision_enabled);
    mesh_collide_btn->setFixedSize(Vector2i(100, 20));
    mesh_collide_btn->setFontSize(14);
    mesh_collide_btn->setChangeCallback([this, mesh_collide_btn](bool state) {
      setMeshCollisionEnabled(state);
      mesh_collide_btn->setCaption(state ? "On" : "Off");
    });

    new Label(panel, "Load OBJ :", "sans-bold");
    Button *load_mesh_btn = new Button(panel, "Browse");
    load_mesh_btn->setFixedSize(Vector2i(100, 20));
    load_mesh_btn->setFontSize(14);

    new Label(panel, "Status :", "sans-bold");
    Label *mesh_status = new Label(panel, "Ready", "sans");
    mesh_status->setFixedWidth(100);
    mesh_status->setFontSize(12);

    load_mesh_btn->setCallback([this, mesh_status] {
      std::string path = nanogui::file_dialog({{"obj", "Wavefront OBJ"}}, false);
      if (path.empty()) {
        return;
      }

      Vector3D scale(m_gui_mesh_scale.x(), m_gui_mesh_scale.y(), m_gui_mesh_scale.z());
      Vector3D translate(m_gui_mesh_translate.x(), m_gui_mesh_translate.y(),
                         m_gui_mesh_translate.z());
      bool ok = loadRuntimeMesh(path, m_gui_mesh_friction, scale, translate);

      size_t slash = path.find_last_of("/\\");
      std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
      if (name.size() > 18) {
        name = name.substr(0, 15) + "...";
      }
      mesh_status->setCaption(ok ? name : "Load failed");
    });
  }

  new Label(appearance, "Compare", "sans-bold");
  {
    Widget *panel = new Widget(appearance);
    GridLayout *layout =
        new GridLayout(Orientation::Horizontal, 2, Alignment::Middle, 5, 5);
    layout->setColAlignment({Alignment::Maximum, Alignment::Fill});
    layout->setSpacing(0, 10);
    panel->setLayout(layout);

    new Label(panel, "Split view :", "sans-bold");
    Button *compare_btn = new Button(panel, m_compare_enabled ? "On" : "Off");
    compare_btn->setFlags(Button::ToggleButton);
    compare_btn->setPushed(m_compare_enabled);
    compare_btn->setFixedSize(Vector2i(100, 20));
    compare_btn->setFontSize(14);
    compare_btn->setChangeCallback([this, compare_btn](bool state) {
      m_compare_enabled = state;
      compare_btn->setCaption(state ? "On" : "Off");
    });

    new Label(panel, "Layout :", "sans-bold");
    ComboBox *layout_cb = new ComboBox(panel, {"Wipe", "Side-by-side"});
    layout_cb->setSelectedIndex(m_compare_layout);
    layout_cb->setFixedSize(Vector2i(100, 20));
    layout_cb->setFontSize(14);
    layout_cb->setCallback([this](int idx) { m_compare_layout = idx; });

    new Label(panel, "Boundary :", "sans-bold");
    {
      Widget *row = new Widget(panel);
      row->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 4));

      Slider *slider = new Slider(row);
      slider->setValue(m_compare_split);
      slider->setFixedWidth(70);

      TextBox *value = new TextBox(row);
      value->setFixedWidth(26);
      value->setFontSize(12);
      value->setValue(std::to_string((int)(m_compare_split * 100.0f)));

      slider->setCallback([this, value](float v) {
        m_compare_split = v;
        value->setValue(std::to_string((int)(v * 100.0f)));
      });
    }

    new Label(panel, "Animate :", "sans-bold");
    Button *anim_btn = new Button(panel, m_compare_animate ? "On" : "Off");
    anim_btn->setFlags(Button::ToggleButton);
    anim_btn->setPushed(m_compare_animate);
    anim_btn->setFixedSize(Vector2i(100, 20));
    anim_btn->setFontSize(14);
    anim_btn->setChangeCallback([this, anim_btn](bool state) {
      m_compare_animate = state;
      anim_btn->setCaption(state ? "On" : "Off");
    });

    new Label(panel, "Anim speed :", "sans-bold");
    FloatBox<float> *speed_box = new FloatBox<float>(panel);
    speed_box->setEditable(true);
    speed_box->setFixedSize(Vector2i(100, 20));
    speed_box->setFontSize(14);
    speed_box->setValue(m_compare_anim_speed);
    speed_box->setMinValue(0.0f);
    speed_box->setValueIncrement(0.1f);
    speed_box->setSpinnable(true);
    speed_box->setCallback([this](float value) {
      m_compare_anim_speed = std::max(0.0f, value);
    });

  }

  // Background color picker
  new Label(appearance, "Background", "sans-bold");
  {
    Widget *panel = new Widget(appearance);
    GridLayout *layout = new GridLayout(Orientation::Horizontal, 2, Alignment::Middle, 5, 5);
    layout->setColAlignment({Alignment::Maximum, Alignment::Fill});
    layout->setSpacing(0, 10);
    panel->setLayout(layout);

    new Label(panel, "Color :", "sans-bold");
    ColorPicker *bg_picker = new ColorPicker(panel, m_background_color);
    bg_picker->setFixedSize(Vector2i(100, 20));
    bg_picker->setFontSize(14);
    bg_picker->setCallback([this](const nanogui::Color &c) { m_background_color = c; });
    refreshes->push_back([bg_picker, this] { bg_picker->setColor(this->m_background_color); });
  }

  {
    Widget *panel = new Widget(appearance);
    panel->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 4));

    Button *save_btn = new Button(panel, "Save");
    save_btn->setFontSize(14);
    save_btn->setCallback([this] { saveCelPreset(defaultCelPresetPath()); });

    Button *save_as_btn = new Button(panel, "Save As");
    save_as_btn->setFontSize(14);
    save_as_btn->setCallback([this] {
      std::string path = nanogui::file_dialog(
          {{"json", "Cel preset"}}, true);
      if (!path.empty()) saveCelPreset(path);
    });

    Button *load_btn = new Button(panel, "Load");
    load_btn->setFontSize(14);
    load_btn->setCallback([this] {
      std::string path = nanogui::file_dialog(
          {{"json", "Cel preset"}}, false);
      if (!path.empty()) loadCelPreset(path);
    });

    // Update UI when default preset is reloaded
    refreshes->push_back([this] {
      if (m_refresh_widgets) m_refresh_widgets();
    });

    Button *hlsl_btn = new Button(panel, "Export HLSL");
    hlsl_btn->setFontSize(14);
    hlsl_btn->setCallback([this] {
      std::string path = nanogui::file_dialog(
          {{"shader", "Unity HLSL shader"}}, true);
      if (!path.empty()) exportCelHLSL(path);
    });
    
    // Edge enable toggle
    Button *edge_btn = new Button(panel, "Edge: On");
    edge_btn->setFlags(Button::ToggleButton);
    edge_btn->setPushed(m_edge_enabled);
    edge_btn->setFontSize(14);
    edge_btn->setChangeCallback([this, edge_btn](bool state) {
      m_edge_enabled = state;
      edge_btn->setCaption(state ? "Edge: On" : "Edge: Off");
    });
  }
}
