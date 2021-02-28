#define _USE_MATH_DEFINES
#define _CRT_SECURE_NO_WARNINGS
#include "WinAudio.h"
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>
#include <iostream>
#include <complex>
#include <math.h>
#include <fstream>

//Constants
static const int target_fps = 60;
static const int sample_rate = 48000;
static const int max_freq = 4000;
static const int window_w_init = 1280;
static const int window_h_init = 720;
static const int starting_fractal = 0;
static const int max_iters = 1200;
static const double escape_radius_sq = 1000.0;
static const char window_name[] = "Fractal Sound Explorer";

//Settings
static int window_w = window_w_init;
static int window_h = window_h_init;
static double cam_x = 0.0;
static double cam_y = 0.0;
static double cam_zoom = 100.0;
static int cam_x_fp = 0;
static int cam_y_fp = 0;
static double cam_x_dest = cam_x;
static double cam_y_dest = cam_y;
static double cam_zoom_dest = cam_zoom;
static bool sustain = true;
static bool normalized = true;
static bool use_color = false;
static bool hide_orbit = true;
static double jx = 1e8;
static double jy = 1e8;
static int frame = 0;

//Fractal abstraction definition
typedef void (*Fractal)(double&, double&, double, double);
static Fractal fractal = nullptr;

//Blend modes
const sf::BlendMode BlendAlpha(sf::BlendMode::SrcAlpha, sf::BlendMode::OneMinusSrcAlpha, sf::BlendMode::Add,
                               sf::BlendMode::Zero, sf::BlendMode::One, sf::BlendMode::Add);
const sf::BlendMode BlendIgnoreAlpha(sf::BlendMode::One, sf::BlendMode::Zero, sf::BlendMode::Add,
                                     sf::BlendMode::Zero, sf::BlendMode::One, sf::BlendMode::Add);

//Screen utilities
void ScreenToPt(int x, int y, double& px, double& py) {
  px = double(x - window_w / 2) / cam_zoom - cam_x;
  py = double(y - window_h / 2) / cam_zoom - cam_y;
}
void PtToScreen(double px, double py, int& x, int& y) {
  x = int(cam_zoom * (px + cam_x)) + window_w / 2;
  y = int(cam_zoom * (py + cam_y)) + window_h / 2;
}

//All fractal equations
void mandelbrot(double& x, double& y, double cx, double cy) {
  double nx = x*x - y*y + cx;
  double ny = 2.0*x*y + cy;
  x = nx;
  y = ny;
}
void burning_ship(double& x, double& y, double cx, double cy) {
  double nx = x*x - y*y + cx;
  double ny = 2.0*std::abs(x*y) + cy;
  x = nx;
  y = ny;
}
void feather(double& x, double& y, double cx, double cy) {
  std::complex<double> z(x, y);
  std::complex<double> z2(x*x, y*y);
  std::complex<double> c(cx, cy);
  std::complex<double> one(1.0, 0.0);
  z = z*z*z/(one + z2) + c;
  x = z.real();
  y = z.imag();
}
void sfx(double& x, double& y, double cx, double cy) {
  std::complex<double> z(x, y);
  std::complex<double> c2(cx*cx, cy*cy);
  z = z * (x*x + y*y) - (z * c2);
  x = z.real();
  y = z.imag();
}
void henon(double& x, double& y, double cx, double cy) {
  double nx = 1.0 - cx*x*x + y;
  double ny = cy*x;
  x = nx;
  y = ny;
}
void duffing(double& x, double& y, double cx, double cy) {
  double nx = y;
  double ny = -cy*x + cx*y - y*y*y;
  x = nx;
  y = ny;
}
void ikeda(double& x, double& y, double cx, double cy) {
  double t = 0.4 - 6.0 / (1.0 + x*x + y*y);
  double st = std::sin(t);
  double ct = std::cos(t);
  double nx = 1.0 + cx*(x*ct - y*st);
  double ny = cy*(x*st + y*ct);
  x = nx;
  y = ny;
}
void chirikov(double& x, double& y, double cx, double cy) {
  y += cy*std::sin(x);
  x += cx*y;
}

//List of fractal equations
static const Fractal all_fractals[] = {
  mandelbrot,
  burning_ship,
  feather,
  sfx,
  henon,
  duffing,
  ikeda,
  chirikov,
};

//Synthesizer class to inherit Windows Audio.
class Synth : public WinAudio {
public:
  bool audio_reset;
  bool audio_pause;
  double volume;
  double play_x, play_y;
  double play_cx, play_cy;
  double play_nx, play_ny;
  double play_px, play_py;

  Synth(HWND hwnd) : WinAudio(hwnd, sample_rate) {
    audio_reset = true;
    audio_pause = false;
    volume = 8000.0;
    play_x = 0.0;
    play_y = 0.0;
    play_cx = 0.0;
    play_cy = 0.0;
    play_nx = 0.0;
    play_ny = 0.0;
    play_px = 0.0;
    play_py = 0.0;
  }

  void SetPoint(double x, double y) {
    play_nx = x;
    play_ny = y;
    audio_reset = true;
    audio_pause = false;
  }

  virtual bool onGetData(Chunk& data) override {
    //Setup the chunk info
    data.samples = m_samples;
    data.sampleCount = AUDIO_BUFF_SIZE;
    memset(m_samples, 0, sizeof(m_samples));

    //Check if audio needs to reset
    if (audio_reset) {
      m_audio_time = 0;
      play_cx = (jx < 1e8 ? jx : play_nx);
      play_cy = (jy < 1e8 ? jy : play_ny);
      play_x = play_nx;
      play_y = play_ny;
      play_px = play_nx;
      play_py = play_ny;
      mean_x = play_nx;
      mean_y = play_ny;
      volume = 8000.0;
      audio_reset = false;
    }

    //Check if paused
    if (audio_pause) {
      return true;
    }

    //Generate the tones
    const int steps = sample_rate / max_freq;
    for (int i = 0; i < AUDIO_BUFF_SIZE; i+=2) {
      const int j = m_audio_time % steps;
      if (j == 0) {
        play_px = play_x;
        play_py = play_y;
        fractal(play_x, play_y, play_cx, play_cy);
        if (play_x*play_x + play_y*play_y > escape_radius_sq) {
          audio_pause = true;
          return true;
        }

        if (normalized) {
          dpx = play_px - play_cx;
          dpy = play_py - play_cy;
          dx = play_x - play_cx;
          dy = play_y - play_cy;
          if (dx != 0.0 || dy != 0.0) {
            double dpmag = 1.0 / std::sqrt(1e-12 + dpx*dpx + dpy*dpy);
            double dmag = 1.0 / std::sqrt(1e-12 + dx*dx + dy*dy);
            dpx *= dpmag;
            dpy *= dpmag;
            dx *= dmag;
            dy *= dmag;
          }
        } else {
          //Point is relative to mean
          dx = play_x - mean_x;
          dy = play_y - mean_y;
          dpx = play_px - mean_x;
          dpy = play_py - mean_y;
        }

        //Update mean
        mean_x = mean_x*0.99 + play_x*0.01;
        mean_y = mean_y*0.99 + play_y*0.01;

        //Don't let the volume go to infinity, clamp.
        double m = dx*dx + dy*dy;
        if (m > 2.0) {
          dx *= 2.0 / m;
          dy *= 2.0 / m;
        }
        m = dpx*dpx + dpy*dpy;
        if (m > 2.0) {
          dpx *= 2.0 / m;
          dpy *= 2.0 / m;
        }

        //Lose volume over time unless in sustain mode
        if (!sustain) {
          volume *= 0.9992;
        }
      }

      //Cosine interpolation
      double t = double(j) / double(steps);
      t = 0.5 - 0.5*std::cos(t * 3.14159);
      double wx = t*dx + (1.0 - t)*dpx;
      double wy = t*dy + (1.0 - t)*dpy;

      //Save the audio to the 2 channels
      m_samples[i]   = (int16_t)std::min(std::max(wx * volume, -32000.0), 32000.0);
      m_samples[i+1] = (int16_t)std::min(std::max(wy * volume, -32000.0), 32000.0);
      m_audio_time += 1;
    }

    //Return the sound clip
    return !audio_reset;
  }

  int16_t m_samples[AUDIO_BUFF_SIZE];
  int32_t m_audio_time;
  double mean_x;
  double mean_y;
  double dx;
  double dy;
  double dpx;
  double dpy;
};

//Change the fractal
void SetFractal(sf::Shader& shader, int type, Synth& synth) {
  shader.setUniform("iType", type);
  jx = jy = 1e8;
  fractal = all_fractals[type];
  normalized = (type == 0);
  synth.audio_pause = true;
  hide_orbit = true;
  frame = 0;
}

//Used whenever the window is created or resized
void resize_window(sf::RenderWindow& window, sf::RenderTexture& rt, const sf::ContextSettings& settings, int w, int h) {
  window_w = w;
  window_h = h;
  rt.create(w, h);
  window.setView(sf::View(sf::FloatRect(0, 0, (float)w, (float)h)));
  frame = 0;
}
void make_window(sf::RenderWindow& window, sf::RenderTexture& rt, const sf::ContextSettings& settings, bool is_fullscreen) {
  window.close();
  sf::VideoMode screenSize;
  if (is_fullscreen) {
    screenSize = sf::VideoMode::getDesktopMode();
    window.create(screenSize, window_name, sf::Style::Fullscreen, settings);
  } else {
    screenSize = sf::VideoMode(window_w_init, window_h_init, 24);
    window.create(screenSize, window_name, sf::Style::Resize | sf::Style::Close, settings);
  }
  resize_window(window, rt, settings, screenSize.width, screenSize.height);
  window.setFramerateLimit(target_fps);
  //window.setVerticalSyncEnabled(true);
  window.setKeyRepeatEnabled(false);
  window.requestFocus();
}

//Main entry-point
#if _WIN32
INT WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow) {
#else
int main(int argc, char *argv[]) {
#endif
  //Make sure shader is supported
  if (!sf::Shader::isAvailable()) {
    std::cerr << "Graphics card does not support shaders" << std::endl;
    return 1;
  }

  //Load the vertex shader
  sf::Shader shader;
  if (!shader.loadFromFile("vert.glsl", sf::Shader::Vertex)) {
    std::cerr << "Failed to compile vertex shader" << std::endl;
    system("pause");
    return 1;
  }

  //Load the fragment shader
  if (!shader.loadFromFile("frag.glsl", sf::Shader::Fragment)) {
    std::cerr << "Failed to compile fragment shader" << std::endl;
    system("pause");
    return 1;
  }

  //Load the font
  sf::Font font;
  if (!font.loadFromFile("RobotoMono-Medium.ttf")) {
    std::cerr << "Failed to load font" << std::endl;
    system("pause");
    return 1;
  }

  //Create the full-screen rectangle to draw the shader
  sf::RectangleShape rect;
  rect.setPosition(0, 0);

  //GL settings
  sf::ContextSettings settings;
  settings.depthBits = 24;
  settings.stencilBits = 8;
  settings.antialiasingLevel = 4;
  settings.majorVersion = 3;
  settings.minorVersion = 0;

  //Create the window
  sf::RenderWindow window;
  sf::RenderTexture renderTexture;
  bool is_fullscreen = false;
  bool toggle_fullscreen = false;
  make_window(window, renderTexture, settings, is_fullscreen);

  //Create audio synth
  Synth synth(window.getSystemHandle());

  //Setup the shader
  shader.setUniform("iCam", sf::Vector2f((float)cam_x, (float)cam_y));
  shader.setUniform("iZoom", (float)cam_zoom);
  SetFractal(shader, starting_fractal, synth);

  //Start the synth
  synth.play();

  //Main Loop
  double px, py, orbit_x, orbit_y;
  bool leftPressed = false;
  bool dragging = false;
  bool juliaDrag = false;
  bool takeScreenshot = false;
  bool showHelpMenu = false;
  sf::Vector2i prevDrag;
  while (window.isOpen()) {
    sf::Event event;
    while (window.pollEvent(event)) {
      if (event.type == sf::Event::Closed) {
        window.close();
        break;
      } else if (event.type == sf::Event::Resized) {
        resize_window(window, renderTexture, settings, event.size.width, event.size.height);
      } else if (event.type == sf::Event::KeyPressed) {
        const sf::Keyboard::Key keycode = event.key.code;
        if (keycode == sf::Keyboard::Escape) {
          window.close();
          break;
        } else if (keycode >= sf::Keyboard::Num1 && keycode <= sf::Keyboard::Num8) {
          SetFractal(shader, keycode - sf::Keyboard::Num1, synth);
        } else if (keycode == sf::Keyboard::F11) {
          toggle_fullscreen = true;
        } else if (keycode == sf::Keyboard::D) {
          sustain = !sustain;
        } else if (keycode == sf::Keyboard::C) {
          use_color = !use_color;
          frame = 0;
        } else if (keycode == sf::Keyboard::R) {
          cam_x = cam_x_dest = 0.0;
          cam_y = cam_y_dest = 0.0;
          cam_zoom = cam_zoom_dest = 100.0;
          frame = 0;
        } else if (keycode == sf::Keyboard::J) {
          if (jx < 1e8) {
            jx = jy = 1e8;
          } else {
            juliaDrag = true;
            const sf::Vector2i mousePos = sf::Mouse::getPosition(window);
            ScreenToPt(mousePos.x, mousePos.y, jx, jy);
          }
          synth.audio_pause = true;
          hide_orbit = true;
          frame = 0;
        } else if (keycode == sf::Keyboard::S) {
          takeScreenshot = true;
        } else if (keycode == sf::Keyboard::H) {
          showHelpMenu = !showHelpMenu;
        }
      } else if (event.type == sf::Event::KeyReleased) {
        if (event.key.code == sf::Keyboard::J) {
          juliaDrag = false;
          frame = 0;
        }
      } else if (event.type == sf::Event::MouseWheelMoved) {
        cam_zoom_dest *= std::pow(1.1f, event.mouseWheel.delta);
        const sf::Vector2i mouse_pos = sf::Mouse::getPosition(window);
        cam_x_fp = mouse_pos.x;
        cam_y_fp = mouse_pos.y;
      } else if (event.type == sf::Event::MouseButtonPressed) {
        if (event.mouseButton.button == sf::Mouse::Left) {
          leftPressed = true;
          hide_orbit = false;
          ScreenToPt(event.mouseButton.x, event.mouseButton.y, px, py);
          synth.SetPoint(px, py);
          orbit_x = px;
          orbit_y = py;
        } else if (event.mouseButton.button == sf::Mouse::Middle) {
          prevDrag = sf::Vector2i(event.mouseButton.x, event.mouseButton.y);
          dragging = true;
        } else if (event.mouseButton.button == sf::Mouse::Right) {
          synth.audio_pause = true;
          hide_orbit = true;
        }
      } else if (event.type == sf::Event::MouseButtonReleased) {
        if (event.mouseButton.button == sf::Mouse::Left) {
          leftPressed = false;
        } else if (event.mouseButton.button == sf::Mouse::Middle) {
          dragging = false;
        }
      } else if (event.type == sf::Event::MouseMoved) {
        if (leftPressed) {
          ScreenToPt(event.mouseMove.x, event.mouseMove.y, px, py);
          synth.SetPoint(px, py);
          orbit_x = px;
          orbit_y = py;
        }
        if (dragging) {
          sf::Vector2i curDrag = sf::Vector2i(event.mouseMove.x, event.mouseMove.y);
          cam_x_dest += (curDrag.x - prevDrag.x) / cam_zoom;
          cam_y_dest += (curDrag.y - prevDrag.y) / cam_zoom;
          prevDrag = curDrag;
          frame = 0;
        }
        if (juliaDrag) {
          ScreenToPt(event.mouseMove.x, event.mouseMove.y, jx, jy);
          frame = 0;
        }
      }
    }

    //Apply zoom
    double fpx, fpy, delta_cam_x, delta_cam_y;
    ScreenToPt(cam_x_fp, cam_y_fp, fpx, fpy);
    cam_zoom = cam_zoom*0.8 + cam_zoom_dest*0.2;
    ScreenToPt(cam_x_fp, cam_y_fp, delta_cam_x, delta_cam_y);
    cam_x_dest += delta_cam_x - fpx;
    cam_y_dest += delta_cam_y - fpy;
    cam_x += delta_cam_x - fpx;
    cam_y += delta_cam_y - fpy;
    cam_x = cam_x*0.8 + cam_x_dest*0.2;
    cam_y = cam_y*0.8 + cam_y_dest*0.2;

    //Create drawing flags for the shader
    const bool hasJulia = (jx < 1e8);
    const bool drawMset = (juliaDrag || !hasJulia);
    const bool drawJset = (juliaDrag || hasJulia);
    const int flags = (drawMset ? 0x01 : 0) | (drawJset ? 0x02 : 0) | (use_color ? 0x04 : 0);

    //Set the shader parameters
    const sf::Glsl::Vec2 window_res((float)window_w, (float)window_h);
    shader.setUniform("iResolution", window_res);
    shader.setUniform("iCam", sf::Vector2f((float)cam_x, (float)cam_y));
    shader.setUniform("iZoom", (float)cam_zoom);
    shader.setUniform("iFlags", flags);
    shader.setUniform("iJulia", sf::Vector2f((float)jx, (float)jy));
    shader.setUniform("iIters", max_iters);
    shader.setUniform("iTime", frame);

    //Draw the full-screen shader to the render texture
    sf::RenderStates states = sf::RenderStates::Default;
    states.blendMode = (frame > 0 ? BlendAlpha : BlendIgnoreAlpha);
    states.shader = &shader;
    rect.setSize(window_res);
    renderTexture.draw(rect, states);
    renderTexture.display();

    //Draw the render texture to the window
    sf::Sprite sprite(renderTexture.getTexture());
    window.clear();
    window.draw(sprite, sf::RenderStates(BlendIgnoreAlpha));

    //Save screen shot if needed
    if (takeScreenshot) {
      window.display();
      const time_t t = std::time(0);
      const tm* now = std::localtime(&t);
      char buffer[128];
      std::strftime(buffer, sizeof(buffer), "pic_%m-%d-%y_%H-%M-%S.png", now);
      const sf::Vector2u windowSize = window.getSize();
      sf::Texture texture;
      texture.create(windowSize.x, windowSize.y);
      texture.update(window);
      texture.copyToImage().saveToFile(buffer);
      takeScreenshot = false;
    }

    //Draw the orbit
    if (!hide_orbit) {
      glLineWidth(1.0f);
      glColor3f(1.0f, 0.0f, 0.0f);
      glBegin(GL_LINE_STRIP);
      int sx, sy;
      double x = orbit_x;
      double y = orbit_y;
      PtToScreen(x, y, sx, sy);
      glVertex2i(sx, sy);
      double cx = (hasJulia ? jx : px);
      double cy = (hasJulia ? jy : py);
      for (int i = 0; i < 200; ++i) {
        fractal(x, y, cx, cy);
        PtToScreen(x, y, sx, sy);
        glVertex2i(sx, sy);
        if (x*x + y*y > escape_radius_sq) {
          break;
        } else if (i < max_freq / target_fps) {
          orbit_x = x;
          orbit_y = y;
        }
      }
      glEnd();
    }

    //Draw help menu
    if (showHelpMenu) {
      sf::RectangleShape dimRect(sf::Vector2f((float)window_w, (float)window_h));
      dimRect.setFillColor(sf::Color(0,0,0,128));
      window.draw(dimRect, sf::RenderStates(BlendAlpha));
      sf::Text helpMenu;
      helpMenu.setFont(font);
      helpMenu.setCharacterSize(24);
      helpMenu.setFillColor(sf::Color::White);
      helpMenu.setString(
        "  H - Toggle Help Menu                Left Mouse - Click or drag to hear orbits\n"
        "  D - Toggle Audio Dampening        Middle Mouse - Drag to pan view\n"
        "  C - Toggle Color                   Right Mouse - Stop orbit and sound\n"
        "F11 - Toggle Fullscreen             Scroll Wheel - Zoom in and out\n"
        "  S - Save Snapshot\n"
        "  R - Reset View\n"
        "  J - Hold down, move mouse, and\n"
        "      release to make Julia sets.\n"
        "      Press again to switch back.\n"
        "  1 - Mandelbrot Set\n"
        "  2 - Burning Ship\n"
        "  3 - Feather Fractal\n"
        "  4 - SFX Fractal\n"
        "  5 - Hénon Map\n"
        "  6 - Duffing Map\n"
        "  7 - Ikeda Map\n"
        "  8 - Chirikov Map\n"
      );
      helpMenu.setPosition(20.0f, 20.0f);
      window.draw(helpMenu);
    }

    //Flip the screen buffer
    window.display();

    //Update shader time if frame blending is needed
    const double xSpeed = std::abs(cam_x - cam_x_dest) * cam_zoom_dest;
    const double ySpeed = std::abs(cam_x - cam_x_dest) * cam_zoom_dest;
    const double zoomSpeed = std::abs(cam_zoom / cam_zoom_dest - 1.0);
    if (xSpeed < 0.2 && ySpeed < 0.2 && zoomSpeed < 0.002) {
      frame += 1;
    } else {
      frame = 1;
    }

    //Toggle full-screen if needed
    if (toggle_fullscreen) {
      toggle_fullscreen = false;
      is_fullscreen = !is_fullscreen;
      make_window(window, renderTexture, settings, is_fullscreen);
    }
  }

  //Stop the synth before quitting
  synth.stop();
  return 0;
}
