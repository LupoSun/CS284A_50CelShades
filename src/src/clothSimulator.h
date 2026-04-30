#ifndef CGL_CLOTH_SIMULATOR_H
#define CGL_CLOTH_SIMULATOR_H

#include <nanogui/nanogui.h>
#include <functional>
#include <memory>
#include <string>

#include "camera.h"
#include "cloth.h"
#include "collision/collisionObject.h"
#include "misc/sphere_drawing.h"

using namespace nanogui;

struct UserShader;
enum ShaderTypeHint { WIREFRAME = 0, NORMALS = 1, PHONG = 2 };

class ClothSimulator {
public:
  ClothSimulator(std::string project_root, Screen *screen);
  ~ClothSimulator();

  void init();

  void loadCloth(Cloth *cloth);
  void loadClothParameters(ClothParameters *cp);
  void loadCollisionObjects(vector<CollisionObject *> *objects);
  virtual bool isAlive();
  virtual void drawContents();

  // Screen events

  virtual bool cursorPosCallbackEvent(double x, double y);
  virtual bool mouseButtonCallbackEvent(int button, int action, int modifiers);
  virtual bool keyCallbackEvent(int key, int scancode, int action, int mods);
  virtual bool dropCallbackEvent(int count, const char **filenames);
  virtual bool scrollCallbackEvent(double x, double y);
  virtual bool resizeCallbackEvent(int width, int height);



private:
  virtual void initGUI(Screen *screen);
  void drawWireframe(GLShader &shader);
  void drawNormals(GLShader &shader);
  void drawPhong(GLShader &shader);
  int shaderIndexByName(const std::string &name) const;
  void renderShadowMap(const Matrix4f &lightViewProjection);
  void renderSceneToOffscreen(const UserShader &active_shader,
                              bool cel_materials,
                              const Matrix4f &lightViewProjection,
                              double projection_aspect = -1.0);
  void renderSceneGeometry(GLShader &shader, const UserShader &active_shader,
                           bool cel_materials,
                           const Matrix4f &viewProjection,
                           const Matrix4f &lightViewProjection);
  void renderEdgeComposite(GLuint target_fbo);
  void renderBaselineToFramebuffer(double projection_aspect = -1.0);
  void renderCompareComposite();
  void setMeshCollisionEnabled(bool enabled);
  bool loadRuntimeMesh(const std::string &path, double friction,
                       const Vector3D &scale, const Vector3D &translate);
  
  void load_shaders();
  void load_textures();
  
  // Two-pass post-processing (offscreen) resources
  void initFramebuffer();
  void destroyFramebuffer();
  void resizeFramebuffer(int width, int height);

  void initFullscreenQuad();
  void drawFullscreenQuad();

  GLuint m_offscreen_fbo = 0;            // offscreen framebuffer
  GLuint m_scene_color_tex = 0;          // color (RGBA) texture
  GLuint m_scene_normal_tex = 0;         // normal (RGB) texture
  GLuint m_scene_depth_tex = 0;          // depth texture
  GLuint m_cel_composite_fbo = 0;        // final cel+edge color for compare mode
  GLuint m_cel_composite_tex = 0;
  GLuint m_baseline_fbo = 0;             // Phong baseline color for compare mode
  GLuint m_baseline_color_tex = 0;
  GLuint m_baseline_depth_tex = 0;

  GLuint m_fullscreen_vao = 0;           // fullscreen quad VAO
  GLuint m_fullscreen_vbo = 0;           // fullscreen quad VBO

  std::shared_ptr<GLShader> m_fullscreen_edge_shader; // separate fullscreen edge shader
  std::shared_ptr<GLShader> m_fullscreen_compare_shader;

  // Edge/outline parameters (tweakable)
  nanogui::Color m_edge_line_color = nanogui::Color(0.0f, 0.0f, 0.0f, 1.0f);
  float m_edge_depth_threshold = 0.01f;
  float m_edge_normal_threshold = 0.2f;
  float m_edge_strength = 1.0f;
  float m_depth_edge_thickness = 1.8f;
  float m_normal_edge_thickness = 1.0f;
  bool m_edge_enabled = true;

  bool m_compare_enabled = false;
  int m_compare_layout = 0; // 0 = wipe boundary, 1 = side-by-side panes
  float m_compare_split = 0.5f;
  bool m_compare_animate = false;
  float m_compare_anim_speed = 0.75f;
  bool m_mesh_collision_enabled = false;
  nanogui::Vector3f m_gui_mesh_scale = nanogui::Vector3f(1.0f, 1.0f, 1.0f);
  nanogui::Vector3f m_gui_mesh_translate = nanogui::Vector3f(0.0f, 0.0f, 0.0f);
  float m_gui_mesh_friction = 0.0f;
  
  // File management
  
  std::string m_project_root;

  // Camera methods

  virtual void resetCamera();
  virtual Matrix4f getProjectionMatrix();
  Matrix4f getProjectionMatrixForAspect(double aspect);
  virtual Matrix4f getViewMatrix();

  Matrix4f getLightViewProjectionMatrix();

  // Default simulation values

  int frames_per_sec = 90;
  int simulation_steps = 30;

  CGL::Vector3D gravity = CGL::Vector3D(0, -9.8, 0);
  nanogui::Color color = nanogui::Color(0.85f, 0.35f, 0.40f, 1.0f);

  Cloth *cloth;
  ClothParameters *cp;
  vector<CollisionObject *> *collision_objects;
  vector<CollisionObject *> m_runtime_meshes;
  bool m_owns_collision_objects = false;

  // OpenGL attributes

  int active_shader_idx = 0;

  vector<UserShader> shaders;
  vector<std::string> shaders_combobox_names;
  
  // OpenGL textures
  
  Vector3D m_gl_texture_1_size;
  Vector3D m_gl_texture_2_size;
  Vector3D m_gl_texture_3_size;
  Vector3D m_gl_texture_4_size;
  GLuint m_gl_texture_1;
  GLuint m_gl_texture_2;
  GLuint m_gl_texture_3;
  GLuint m_gl_texture_4;
  GLuint m_gl_cubemap_tex;
  
  // OpenGL customizable inputs

  double m_normal_scaling = 2.0;
  double m_height_scaling = 0.1;
  nanogui::Color m_background_color = nanogui::Color(0.85f, 0.85f, 0.85f, 1.0f);

  // Cel shader parameters (live-tweakable via Appearance GUI)

  nanogui::Color m_cel_dark_color = nanogui::Color(0.38f, 0.16f, 0.18f, 1.0f);
  nanogui::Color m_cel_bright_color = nanogui::Color(1.0f, 0.85f, 0.78f, 1.0f);
  nanogui::Vector3f m_cel_light_dir = nanogui::Vector3f(0.5f, 2.0f, 2.0f);
  nanogui::Vector3f m_cel_light_pos = nanogui::Vector3f(0.5f, 2.0f, 2.0f);
  int m_cel_light_type = 0; // 0 = directional, 1 = point
  CGL::Misc::SphereMesh m_light_sphere{10, 10};
  float m_cel_dark_threshold = 0.42f;
  float m_cel_bright_threshold = 0.95f;
  float m_cel_shadow_strength = 0.9f;
  float m_cel_highlight_strength = 0.18f;
  float m_cel_pattern_scale = 2.0f;
  float m_cel_pattern_radius = 0.18f;
  float m_cel_bands = 1.0f;
  std::string m_cel_texture_path;
  bool m_light_rotate = false;
  float m_light_rotate_speed = 1.0f;

  std::function<void()> m_refresh_widgets;

  std::string defaultCelPresetPath() const;
  bool saveCelPreset(const std::string &path);
  bool loadCelPreset(const std::string &path);
  bool exportCelHLSL(const std::string &path);
  void reloadCelTexture(const std::string &path);

  // Camera attributes

  CGL::Camera camera;
  CGL::Camera canonicalCamera;

  double view_distance;
  double canonical_view_distance;
  double min_view_distance;
  double max_view_distance;

  double scroll_rate;

  // Screen methods

  Screen *screen;
  void mouseLeftDragged(double x, double y);
  void mouseRightDragged(double x, double y);
  void mouseMoved(double x, double y);

  // Mouse flags

  bool left_down = false;
  bool right_down = false;
  bool middle_down = false;

  // Keyboard flags

  bool ctrl_down = false;

  // Simulation flags

  bool is_paused = true;

  // Screen attributes

  int mouse_x;
  int mouse_y;

  int screen_w;
  int screen_h;

  bool is_alive = true;

  Vector2i default_window_size = Vector2i(1280, 1000);


  // Shadow mapping
  GLuint m_shadow_fbo = 0;
  GLuint m_shadow_depth_tex = 0;
  int m_shadow_map_size = 2048;

  std::shared_ptr<GLShader> m_shadow_depth_shader;

  void initShadowMap();
  void destroyShadowMap();
};

struct UserShader {
  UserShader(std::string display_name, std::shared_ptr<GLShader> nanogui_shader, ShaderTypeHint type_hint)
  : display_name(display_name)
  , nanogui_shader(nanogui_shader)
  , type_hint(type_hint) {
  }
  
  std::shared_ptr<GLShader> nanogui_shader;
  std::string display_name;

  ShaderTypeHint type_hint;
  
};

#endif // CGL_CLOTH_SIM_H
