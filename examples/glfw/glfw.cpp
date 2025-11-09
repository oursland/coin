/* Simple example that demonstrates how to render with Coin3D and GLFW.
 * 
 * Note: This example uses GLFW, so you do not need to have any of the 
 * SoGUI libraries installed.
 */

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoSceneManager.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoRotationXYZ.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/system/renderer.h>

#include <cstdlib>
#include <functional>

static SoRenderer::Enum renderer =
#if defined(__EMSCRIPTEN__)
  SoRenderer::GLES
#else
  SoRenderer::GL
#endif
  ;

static bool useEGL = false;

static bool useGLCompatibilityProfile = false;

#if defined(COIN_USE_GL_RENDERER)
#define GL_GLEXT_PROTOTYPES
#endif
#include <GLFW/glfw3.h>

#if !defined(__EMSCRIPTEN__)
  #include <GLFW/glfw3native.h>
#endif

// ----------------------------------------------------------------------

GLFWwindow* window;
SoSceneManager* sceneManager;
SoCamera* camera;
static bool cameraFromScene = false;

static void setupSceneManager(SoSeparator* root);
static SoSeparator* loadSceneFromFile(const char* path);
static void redrawCallback(void * user, SoSceneManager * manager);

static void setupSceneManager(SoSeparator* root)
{
    sceneManager = new SoSceneManager;
    sceneManager->setRenderCallback(redrawCallback, (void *)1);
    sceneManager->setBackgroundColor(SbColor(1.0f, 1.0f, 1.0f));
    sceneManager->activate();
    sceneManager->setSceneGraph(root);
    if (!cameraFromScene && camera) {
        camera->viewAll(root, sceneManager->getViewportRegion());
    }
}

static SoSeparator* loadSceneFromFile(const char* path)
{
    SoInput input;
    if (!input.openFile(path)) {
        fprintf(stderr, "Unable to open scene file: %s\n", path);
        return nullptr;
    }
    SoSeparator* loaded = SoDB::readAll(&input);
    if (!loaded) {
        fprintf(stderr, "Failed to parse scene file: %s\n", path);
        return nullptr;
    }
    SoSearchAction search;
    search.setType(SoCamera::getClassTypeId());
    search.setSearchingAll(TRUE);
    search.setInterest(SoSearchAction::ALL);
    search.apply(loaded);
    const SoPathList& paths = search.getPaths();
    if (paths.getLength() > 0) {
        SoPath* camPath = paths[0];
        camera = static_cast<SoCamera*>(camPath->getTail());
        cameraFromScene = true;
    } else {
        camera = new SoPerspectiveCamera;
        loaded->insertChild(camera, 0);
        cameraFromScene = false;
    }
    setupSceneManager(loaded);
    return loaded;
}

// ----------------------------------------------------------------------

typedef struct cc_glglue cc_glglue;
const cc_glglue * sogl_glue_instance(const SoState * state);
SbBool sogl_compatibility_profile(const SoState * state);

// Redraw on scenegraph changes.
void redrawCallback(void * user, SoSceneManager * manager)
{
#if defined(COIN_USE_GL_RENDERER)
  if (SoRenderer::isOpenGL()) {
    const SoState* state = manager->getGLRenderAction()->getState();
    if (sogl_compatibility_profile(state)) {
      glEnable(GL_DEPTH_TEST);
      glEnable(GL_LIGHTING);
    }
  }
#endif

  sceneManager->render();

#if defined(COIN_USE_GL_RENDERER)
  if (SoRenderer::isOpenGL()) {
    glfwSwapBuffers(window);
  }
#endif
}

// Redraw on expose events.
void exposeCallback(void)
{
#if defined(COIN_USE_GL_RENDERER)
  if (SoRenderer::isOpenGL()) {
    const SoState* state = sceneManager->getGLRenderAction()->getState();
    if (sogl_compatibility_profile(state)) {
      glEnable(GL_DEPTH_TEST);
      glEnable(GL_LIGHTING);
    }
  }
#endif

  sceneManager->render();

#if defined(COIN_USE_GL_RENDERER)
  if (SoRenderer::isOpenGL()) {
    glfwSwapBuffers(window);
  }
#endif
}

// Reconfigure on changes to window dimensions.
void framebufferSizeCallback(GLFWwindow* window,int w, int h)
{
  sceneManager->setWindowSize(SbVec2s(w, h));
  //sceneManager->setSize(SbVec2s(w, h));
  //sceneManager->setViewportRegion(SbViewportRegion(w, h));
  sceneManager->scheduleRedraw();
}

// Process the internal Coin queues when idle. Necessary to get the
// animation to work.
void idleCallback(void)
{
  SoDB::getSensorManager()->processTimerQueue();
  SoDB::getSensorManager()->processDelayQueue(TRUE);
}

void glfwInitGL()
{
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);

  if (useEGL) {
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
  }

  // Needs at least OpenGL 4.3 for KHR_debug to be available (on Linux)
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

  if (SoRenderer::get() == SoRenderer::GLES) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  } else {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

    if (useGLCompatibilityProfile) {
      glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    }
  }
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
  SbVec3f position = camera->position.getValue();
  if (action == GLFW_PRESS) {
    switch(key) {
      case GLFW_KEY_W:
        camera->position.setValue(position + SbVec3f(0, 0, -1));
        break;
      case GLFW_KEY_A:
      camera->position.setValue(position + SbVec3f(-1, 0, 0));
        break;
      case GLFW_KEY_S:
        camera->position.setValue(position + SbVec3f(0, 0, 1));
        break;
      case GLFW_KEY_D:
        camera->position.setValue(position + SbVec3f(1, 0, 0));
        break;
    }
  }
  camera->position.touch();
}

// ----------------------------------------------------------------------

SoSeparator* createScene();
unsigned char * img;
const int IMGWIDTH = 256;
const int IMGHEIGHT = 256;
static void mandel(double sr, double si, double width, double height,
        int bwidth, int bheight, int mult, unsigned char * bmp, int n);

std::function<void()> loop;
void main_loop() { loop(); }

int main(int argc, char** argv)
{
    glfwSetErrorCallback([](int error, const char* description) {
      fprintf(stderr, "Error: %s\n", description);
    });

    SoRenderer::set(renderer);
    SoDB::init();

    if (!glfwInit())
        return EXIT_FAILURE;

    if (SoRenderer::isOpenGL()) {
      glfwInitGL();
    }

    window = glfwCreateWindow(640, 480, "Coin3D", NULL, NULL);
    if (!window) {
      glfwTerminate();
      return EXIT_FAILURE;
    }

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
  
#if defined(COIN_USE_GL_RENDERER)
    if (SoRenderer::isOpenGL()) {
      glfwMakeContextCurrent(window);
    }
#endif

    img = new unsigned char[IMGWIDTH * IMGHEIGHT];
    mandel(-0.5, 0.6, 0.025, 0.025, IMGWIDTH, IMGHEIGHT, 1, img, 256);

    const char* iv_path = argc > 1 ? argv[1] : nullptr;
    SoSeparator* root = nullptr;
    if (iv_path) {
        root = loadSceneFromFile(iv_path);
    }
    if (!root) {
        root = createScene();
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    framebufferSizeCallback(window, width, height);

    glfwSetKeyCallback(window, keyCallback);

    loop = [&] {
        glfwPollEvents();

        idleCallback();
        exposeCallback();
    };

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(main_loop, 0, true);
#else
    while (!glfwWindowShouldClose(window))
        main_loop();
#endif

    root->unref();
    delete sceneManager;
    delete[] img;
  
    glfwTerminate();
    return 0;
}

// ----------------------------------------------------------------------

static char scene_iv[] = {
  "#Inventor V2.1 ascii\n\n"
  "Separator {\n"
  "  ShaderProgram {\n"
  "    shaderObject [\n"
  "      VertexShader {\n"
  "        sourceProgram \"perpixel_vertex.glsl\"\n"
  "      }\n"
  "      FragmentShader {\n"
  "        sourceProgram \"perpixel_fragment.glsl\"\n"
  "      }\n"
  "    ]\n"
  "  }\n"
  "  Cube { }\n"
  "}\n"
};

SoSeparator* createScene()
{
    auto root = new SoSeparator;
    root->ref();

    SoPerspectiveCamera * perspectiveCamera = new SoPerspectiveCamera;
    perspectiveCamera->nearDistance = 0.01f;
    perspectiveCamera->farDistance = 100.0f;
    camera = perspectiveCamera;
    root->addChild(perspectiveCamera);

    //root->addChild(new SoDirectionalLight);

#if 0
    SoInput in;
    in.setBuffer(scene_iv, strlen(scene_iv));
    
    SoSeparator* result = SoDB::readAll(&in);
    if (result == nullptr) {
      fprintf(stderr, "Could not load scene graph from text");
      exit(EXIT_FAILURE); 
    }
    root->addChild(result);
#endif

#if 0
    SoImage * nimage = new SoImage;
    nimage->vertAlignment = SoImage::HALF;
    nimage->horAlignment = SoImage::CENTER;
    nimage->image.setValue(SbVec2s(IMGWIDTH, IMGHEIGHT), 1, img);
    root->addChild(nimage);
#endif

    SoLightModel * lightModel = new SoLightModel;
    lightModel->model = SoLightModel::BASE_COLOR;
    root->addChild(lightModel);

    SoBaseColor * col = new SoBaseColor;
    col->rgb = SbColor(1, 1, 0);
    root->addChild(col);

    //root->addChild(new SoCone);
    root->addChild(new SoCube);

    cameraFromScene = false;
    setupSceneManager(root);

    return root;
}

static void mandel(double sr, double si, double width, double height,
        int bwidth, int bheight, int mult, unsigned char * bmp, int n)
{
  double zr, zr_old, zi, cr, ci;
  int w;

  for (int y=0; y<bheight; y++)
    for (int x=0; x<bwidth; x++) {
      cr = ((double)(x)/(double)(bwidth))*width+sr;
      ci = ((double)(y)/(double)(bheight))*height+si;
      zr = zi = 0.0;
      for (w = 0; (w < n) && (zr*zr + zi*zi)<n; w++) {
        zr_old = zr;
        zr = zr*zr - zi*zi + cr;
        zi = 2*zr_old*zi + ci;
      }
      bmp[y*bwidth+x] = w*mult;
    }
}
