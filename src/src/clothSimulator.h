#ifndef CGL_CLOTH_SIMULATOR_H
#define CGL_CLOTH_SIMULATOR_H

#include <nanogui/nanogui.h>
#include <functional>
#include <memory>
#include <string>

#include "camera.h"
#include "cloth.h"
#include "collision/collisionObject.h"

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
  
  void load_shaders();
  void load_textures();
  
  // File management
  
  std::string m_project_root;

  // Camera methods

  virtual void resetCamera();
  virtual Matrix4f getProjectionMatrix();
  virtual Matrix4f getViewMatrix();

  // Default simulation values

  int frames_per_sec = 90;
  int simulation_steps = 30;

  CGL::Vector3D gravity = CGL::Vector3D(0, -9.8, 0);
  nanogui::Color color = nanogui::Color(0.85f, 0.35f, 0.40f, 1.0f);

  Cloth *cloth;
  ClothParameters *cp;
  vector<CollisionObject *> *collision_objects;

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

  // Cel shader parameters (live-tweakable via Appearance GUI)

  nanogui::Color m_cel_dark_color = nanogui::Color(0.38f, 0.16f, 0.18f, 1.0f);
  nanogui::Color m_cel_bright_color = nanogui::Color(1.0f, 0.85f, 0.78f, 1.0f);
  nanogui::Vector3f m_cel_light_dir = nanogui::Vector3f(0.5f, 2.0f, 2.0f);
  float m_cel_dark_threshold = 0.42f;
  float m_cel_bright_threshold = 0.95f;
  float m_cel_shadow_strength = 0.9f;
  float m_cel_highlight_strength = 0.18f;
  float m_cel_pattern_scale = 1.0f;
  float m_cel_pattern_radius = 0.18f;
  float m_cel_bands = 4.0f;
  std::string m_cel_texture_path;

  std::function<void()> m_refresh_cel_widgets;

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

  Vector2i default_window_size = Vector2i(1024, 800);
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
