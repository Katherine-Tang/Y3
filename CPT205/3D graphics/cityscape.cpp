// CPT205 Assessment 2 - Future Smart City (Hi, Future)
// Zhiying Tang 2360010

// Scenes(press key 1-4 to switch):
//  1 - Bird-view city (default); Press drone to switch drone view
//  2 - Robot view
//  3 - Street view: orbit camera with right-mouse drag
//  4 - Train view; 'V' transfer train forward-facing camera

// All textures used were created by Jimeng AI (an AI tool specifically designed for generating images).


#define FREEGLUT_STATIC
#include <GL/freeglut.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

using std::vector;


//Define global window/ scene state
int gWindowWidth = 1080;
int gWindowHeight = 620;

int currentScene = 1;          // initial scene index: 1 ~ 4
bool isNightMode = false;    // false = day, true = night
bool showHelp = false;    // big help window
bool showHelpHint = true;     // small hint “Press H for Help”


//Define camera mode
enum CameraMode {
    CAM_NORMAL = 0,
    CAM_TRAIN_VIEW,
    CAM_DRONE_VIEW
};

CameraMode cameraMode = CAM_NORMAL;


//State robot joint
float robotHeadYaw = 0.0f;

float leftUpperArmRotX = 0.0f;
float leftLowerArmRotX = 0.0f;
float rightUpperArmRotX = 0.0f;
float rightLowerArmRotX = 0.0f;

float leftThighRotX = 0.0f;
float leftShinRotX = 0.0f;
float rightThighRotX = 0.0f;
float rightShinRotX = 0.0f;



//State train 
float trainPos = -34.0f;
float trainSpeed = 0.1f;
bool  trainRunning = true;
bool  trainVisible = true;
int   trainHideCounter = 0;

const int   TRAIN_HIDE_FRAMES = 40;
const float TRAIN_VISIBLE_MIN_X = -34.0f;
const float TRAIN_VISIBLE_MAX_X = 34.0f;

const float TRACK_Z = -15.0f;
const float TRACK_TOP_Y = 4.0f;
const float TRAIN_Y = 4.5f;
const float LEFT_HOUSE_X = -40.0f;
const float RIGHT_HOUSE_X = 40.0f;


//Define robot surrounding house
float robotSceneHouseHeights[4];
bool  robotSceneHouseInit = false;


//Define the drone
float droneAngleDeg = 0.0f;
float dronePropeller = 0.0f;

float droneX = 0.0f, droneY = 15.0f, droneZ = 0.0f;
float droneRadiusPixels = 25.0f; // picking radius in screen space


//Define orbit camara
bool  scene3OrbitActive = false;
int   lastMouseX = 0;
int   lastMouseY = 0;
float scene3YawDeg = 50.0f;
float scene3PitchDeg = 20.0f;
float scene3Distance = 45.0f;


//Define direction light
float cyberLightYawDeg = -45.0f;   // left-right
float cyberLightPitchDeg = 20.0f;   // up-down


//Define small house
struct SmallHouse {
    float x, z;     // position on ground plane
    float h;        // height
};

vector<SmallHouse> citySmallHouses;
bool citySmallHousesInit = false;

void generateSmallHouses() {
    if (citySmallHousesInit) return;

    const int   NUM_HOUSES = 14;   //set small house number
    const float INNER_RADIUS = 15.0f;
    const float OUTER_MIN = 25.0f;
    const float OUTER_MAX = 70.0f;

    for (int i = 0; i < NUM_HOUSES; i++) {
        SmallHouse sh;
        int  tries = 0;
        bool ok = false;

        while (!ok && tries < 50) {
            tries++;

            float r = OUTER_MIN + (rand() % 1000) / 1000.0f * (OUTER_MAX - OUTER_MIN);
            float angle = (rand() % 360) * 3.1415926f / 180.0f;

            sh.x = std::cos(angle) * r;
            sh.z = std::sin(angle) * r;

            // random height (tech building)
            sh.h = 25.0f + (rand() % 25) * 0.1f;  // 4.0 ~ 6.5

            // avoid overlapping
            ok = true;
            for (auto& other : citySmallHouses) {
                float dx = sh.x - other.x;
                float dz = sh.z - other.z;
                if (dx * dx + dz * dz < 100.0f) {
                    ok = false;
                    break;
                }
            }
        }

        citySmallHouses.push_back(sh);
    }

    citySmallHousesInit = true;
}


//Define texture mapping
GLint  imageWidth, imageHeight, pixelLength;
vector<GLubyte*> gPixels;
GLuint gTextures[16];

enum TextureID {
    TEX_FLOOR = 0,
    TEX_ROAD,
    TEX_BUILDING1,
    TEX_BUILDING2,
    TEX_BUILDING_LIGHT,
    TEX_TRAIN_SIDE,
    TEX_RAIL,
    TEX_ROBOT_METAL,
    TEX_ROBOT_FACE,
    TEX_AD_SCREEN1,
    TEX_AD_SCREEN2,
    TEX_COUNT
};

const char* textureFilename[TEX_COUNT] = {
    "textures/floor.bmp",           // 0 (optional)
    "textures/road.bmp",            // 1 (optional)
    "textures/building_wall1.bmp",  // 2
    "textures/building_glass.bmp",  // 3
    "textures/building_sci_fi.bmp", // 4 (holographic panel)
    "textures/train_side.bmp",      // 5
    "textures/rail.bmp",            // 6
    "textures/robot_metal.bmp",     // 7
    "textures/robot_face.bmp",      // 8
    "textures/ad_screen1.bmp",      // 9
    "textures/ad_screen2.bmp"       // 10
};


GLubyte* ReadBMP(const char path[256],
    GLint& width, GLint& height, GLint& pixlen) {
    FILE* pfile = nullptr;
#ifdef _MSC_VER
    fopen_s(&pfile, path, "rb");
#else
    pfile = fopen(path, "rb");
#endif
    if (!pfile) {
        printf("Failed to open BMP file: %s\n", path);
        return nullptr;
    }

    fseek(pfile, 0x0012, SEEK_SET);
    fread(&width, sizeof(width), 1, pfile);
    fread(&height, sizeof(height), 1, pfile);

    pixlen = width * 3;
    while (pixlen % 4 != 0) pixlen++;
    pixlen *= height;

    GLubyte* pixeldata = (GLubyte*)malloc(pixlen);
    if (!pixeldata) {
        fclose(pfile);
        return nullptr;
    }

    fseek(pfile, 54, SEEK_SET);
    fread(pixeldata, pixlen, 1, pfile);
    fclose(pfile);

    return pixeldata;
}


//Init texture
void initTextures() {
    glEnable(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(TEX_COUNT, gTextures);

    for (int i = 0; i < TEX_COUNT; ++i) {
        GLubyte* data = ReadBMP(textureFilename[i],
            imageWidth, imageHeight, pixelLength);
        if (!data) {
            printf("Texture %s NOT loaded, fallback to solid colors.\n",
                textureFilename[i]);
            gPixels.push_back(nullptr);
            continue;
        }
        gPixels.push_back(data);

        glBindTexture(GL_TEXTURE_2D, gTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, 3,
            imageWidth, imageHeight,
            0, GL_BGR_EXT, GL_UNSIGNED_BYTE, data);

        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}


//Set day and night mode
void applyDayNightMode() {
    if (isNightMode) {
        // Night mode
        glClearColor(0.02f, 0.04f, 0.10f, 1.0f); // deep galaxy blue

        GLfloat lightAmb[] = { 0.05f, 0.08f, 0.15f, 1.0f };
        GLfloat lightDif[] = { 0.25f, 0.55f, 1.0f,  1.0f };
        GLfloat lightSpec[] = { 0.9f,  0.95f, 1.0f,  1.0f };

        glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmb);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDif);
        glLightfv(GL_LIGHT0, GL_SPECULAR, lightSpec);
    }
    else {
        // Day mode
        glClearColor(0.70f, 0.82f, 0.97f, 1.0f); // sunny sky

        GLfloat lightAmb[] = { 0.35f, 0.40f, 0.45f, 1.0f };
        GLfloat lightDif[] = { 0.85f, 0.90f, 1.0f,  1.0f };
        GLfloat lightSpec[] = { 1.0f,  1.0f,  1.0f,  1.0f };

        glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmb);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDif);
        glLightfv(GL_LIGHT0, GL_SPECULAR, lightSpec);
    }
}


//Set base lighting
void setupBaseLighting() {
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_COLOR_MATERIAL);

    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

    // Main cyber directional light, controlled by arrow keys
    float yawRad = cyberLightYawDeg * 3.1415926f / 180.0f;
    float pitchRad = cyberLightPitchDeg * 3.1415926f / 180.0f;

    float dx = std::cos(pitchRad) * std::cos(yawRad);
    float dy = std::sin(pitchRad);
    float dz = std::cos(pitchRad) * std::sin(yawRad);

    GLfloat lightPos[] = { dx, dy, dz, 0.0f }; // directional
    GLfloat lightAmb[] = { 0.05f, 0.10f, 0.20f, 1.0f }; // cyber ambient
    GLfloat lightDif[] = { 0.3f,  0.7f,  1.0f, 1.0f };  // neon cyan-blue
    GLfloat lightSpec[] = { 0.8f,  0.9f,  1.0f, 1.0f };  // strong specular

    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDif);
    glLightfv(GL_LIGHT0, GL_SPECULAR, lightSpec);

    // Soft fill light
    GLfloat light_pos1[] = { -0.3f, 0.8f, -0.2f, 0.0f };
    GLfloat light_amb1[] = { 0.20f, 0.20f, 0.22f, 1.0f };
    GLfloat light_dif1[] = { 0.5f,  0.5f,  0.55f, 1.0f };
    GLfloat light_spec1[] = { 0.2f,  0.2f,  0.25f, 1.0f };
    glLightfv(GL_LIGHT1, GL_POSITION, light_pos1);
    glLightfv(GL_LIGHT1, GL_AMBIENT, light_amb1);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, light_dif1);
    glLightfv(GL_LIGHT1, GL_SPECULAR, light_spec1);

    // Material specular
    GLfloat mat_specular[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    GLfloat mat_shininess[] = { 64.0f };
    glMaterialfv(GL_FRONT, GL_SPECULAR, mat_specular);
    glMaterialfv(GL_FRONT, GL_SHININESS, mat_shininess);
}


//Set camera helper for 4 scenes
void setCameraForScene1() {
    if (cameraMode == CAM_DRONE_VIEW) {
        // Drone-follow camera
        float yawRad = droneAngleDeg * 3.1415926f / 180.0f;
        float dirX = std::cos(yawRad);
        float dirZ = std::sin(yawRad);

        float eyeX = droneX - 6.0f * dirX;
        float eyeY = droneY + 2.0f;
        float eyeZ = droneZ - 6.0f * dirZ;

        float centerX = droneX + dirX * 10.0f;
        float centerY = droneY;
        float centerZ = droneZ + dirZ * 10.0f;

        gluLookAt(eyeX, eyeY, eyeZ,
            centerX, centerY, centerZ,
            0.0, 1.0, 0.0);
    }
    else {
        // Default bird-view
        gluLookAt(45.0, 50.0, 45.0,
            0.0, 0.0, 0.0,
            0.0, 1.0, 0.0);
    }
}

void setCameraForScene2() {
    gluLookAt(12.0, 10.0, 25.0,
        0.0, 8.0, 0.0,
        0.0, 1.0, 0.0);
}

void setCameraForScene3() {
    // Orbit camera around street center
    float yawRad = scene3YawDeg * 3.1415926f / 180.0f;
    float pitchRad = scene3PitchDeg * 3.1415926f / 180.0f;

    float cx = scene3Distance * std::cos(pitchRad) * std::cos(yawRad);
    float cy = scene3Distance * std::sin(pitchRad);
    float cz = scene3Distance * std::cos(pitchRad) * std::sin(yawRad);

    gluLookAt(cx, cy, cz,
        0.0, 5.0, 0.0,
        0.0, 1.0, 0.0);
}

void setCameraForScene4() {
    if (cameraMode == CAM_TRAIN_VIEW && trainVisible) {
        // Train front-view camera
        float eyeX = trainPos - 4.0f;
        float eyeY = TRAIN_Y + 4.0f;
        float eyeZ = TRACK_Z;
        float centerX = trainPos + 20.0f;
        float centerY = TRAIN_Y + 4.0f;
        float centerZ = TRACK_Z;

        gluLookAt(eyeX, eyeY, eyeZ,
            centerX, centerY, centerZ,
            0.0f, 1.0f, 0.0f);
    }
    else {
        // Default train scene camera
        gluLookAt(20.0, 12.0, 35.0,
            0.0, 5.0, 0.0,
            0.0, 1.0, 0.0);
    }
}

//Draw help texture box
void drawTexturedBox(GLfloat sx, GLfloat sy, GLfloat sz, GLuint texID) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texID);
    glPushMatrix();
    glScalef(sx, sy, sz);

    glBegin(GL_QUADS);
    // Front
    glNormal3f(0, 0, 1);
    glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, 0.5f);
    glTexCoord2f(1, 0); glVertex3f(0.5f, -0.5f, 0.5f);
    glTexCoord2f(1, 1); glVertex3f(0.5f, 0.5f, 0.5f);
    glTexCoord2f(0, 1); glVertex3f(-0.5f, 0.5f, 0.5f);

    // Back
    glNormal3f(0, 0, -1);
    glTexCoord2f(0, 0); glVertex3f(0.5f, -0.5f, -0.5f);
    glTexCoord2f(1, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
    glTexCoord2f(1, 1); glVertex3f(-0.5f, 0.5f, -0.5f);
    glTexCoord2f(0, 1); glVertex3f(0.5f, 0.5f, -0.5f);

    // Left
    glNormal3f(-1, 0, 0);
    glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
    glTexCoord2f(1, 0); glVertex3f(-0.5f, -0.5f, 0.5f);
    glTexCoord2f(1, 1); glVertex3f(-0.5f, 0.5f, 0.5f);
    glTexCoord2f(0, 1); glVertex3f(-0.5f, 0.5f, -0.5f);

    // Right
    glNormal3f(1, 0, 0);
    glTexCoord2f(0, 0); glVertex3f(0.5f, -0.5f, 0.5f);
    glTexCoord2f(1, 0); glVertex3f(0.5f, -0.5f, -0.5f);
    glTexCoord2f(1, 1); glVertex3f(0.5f, 0.5f, -0.5f);
    glTexCoord2f(0, 1); glVertex3f(0.5f, 0.5f, 0.5f);

    // Top
    glNormal3f(0, 1, 0);
    glTexCoord2f(0, 0); glVertex3f(-0.5f, 0.5f, 0.5f);
    glTexCoord2f(1, 0); glVertex3f(0.5f, 0.5f, 0.5f);
    glTexCoord2f(1, 1); glVertex3f(0.5f, 0.5f, -0.5f);
    glTexCoord2f(0, 1); glVertex3f(-0.5f, 0.5f, -0.5f);

    // Bottom
    glNormal3f(0, -1, 0);
    glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
    glTexCoord2f(1, 0); glVertex3f(0.5f, -0.5f, -0.5f);
    glTexCoord2f(1, 1); glVertex3f(0.5f, -0.5f, 0.5f);
    glTexCoord2f(0, 1); glVertex3f(-0.5f, -0.5f, 0.5f);
    glEnd();

    glPopMatrix();
}


//Draw ground and road
void drawGroundAndRoads() {
    glDisable(GL_TEXTURE_2D);

    // Ground
    glColor3f(0.90f, 0.92f, 0.95f);
    glBegin(GL_QUADS);
    glVertex3f(-70, 0, -70);
    glVertex3f(70, 0, -70);
    glVertex3f(70, 0, 70);
    glVertex3f(-70, 0, 70);
    glEnd();

    // Main road strip
    glColor3f(0.70f, 0.75f, 0.80f);
    glBegin(GL_QUADS);
    glVertex3f(-70, 0.01f, -7);
    glVertex3f(70, 0.01f, -7);
    glVertex3f(70, 0.01f, 7);
    glVertex3f(-70, 0.01f, 7);
    glEnd();

    // Neon lane markers
    glDisable(GL_LIGHTING);
    glColor3f(0.15f, 0.85f, 1.0f);
    glLineWidth(4.0f);
    for (float x = -75; x <= 75; x += 10) {
        glBegin(GL_LINES);
        glVertex3f(x, 0.05f, -1);
        glVertex3f(x, 0.05f, 1);
        glEnd();
    }
    glEnable(GL_LIGHTING);
}


//Draw the buildings
void drawRoofWithChimney(float w, float d, float hRoof) {
    float hw = w * 0.5f;
    float hd = d * 0.5f;

    // Flat sci-fi roof
    glColor3f(0.88f, 0.92f, 0.97f);
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);
    glVertex3f(-hw, 0.0f, -hd);
    glVertex3f(hw, 0.0f, -hd);
    glVertex3f(hw, 0.0f, hd);
    glVertex3f(-hw, 0.0f, hd);
    glEnd();

    // Glow edge (cyan)
    glDisable(GL_LIGHTING);
    glLineWidth(3.0f);
    glColor3f(0.1f, 0.9f, 1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex3f(-hw, 0.01f, -hd);
    glVertex3f(hw, 0.01f, -hd);
    glVertex3f(hw, 0.01f, hd);
    glVertex3f(-hw, 0.01f, hd);
    glEnd();
    glEnable(GL_LIGHTING);
}


void drawBuildingBody(float w, float d, float h, GLuint wallTex) {
    float hw = w * 0.5f;
    float hd = d * 0.5f;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, wallTex);
    glColor3f(0.85f, 0.90f, 0.95f); // pale chrome silver-blue

    glBegin(GL_QUADS);
    // Front
    glNormal3f(0, 0, 1);
    glTexCoord2f(0, 0); glVertex3f(-hw, 0.0f, hd);
    glTexCoord2f(1, 0); glVertex3f(hw, 0.0f, hd);
    glTexCoord2f(1, 1); glVertex3f(hw, h, hd);
    glTexCoord2f(0, 1); glVertex3f(-hw, h, hd);

    // Back
    glNormal3f(0, 0, -1);
    glTexCoord2f(0, 0); glVertex3f(hw, 0.0f, -hd);
    glTexCoord2f(1, 0); glVertex3f(-hw, 0.0f, -hd);
    glTexCoord2f(1, 1); glVertex3f(-hw, h, -hd);
    glTexCoord2f(0, 1); glVertex3f(hw, h, -hd);

    // Left
    glNormal3f(-1, 0, 0);
    glTexCoord2f(0, 0); glVertex3f(-hw, 0.0f, -hd);
    glTexCoord2f(1, 0); glVertex3f(-hw, 0.0f, hd);
    glTexCoord2f(1, 1); glVertex3f(-hw, h, hd);
    glTexCoord2f(0, 1); glVertex3f(-hw, h, -hd);

    // Right
    glNormal3f(1, 0, 0);
    glTexCoord2f(0, 0); glVertex3f(hw, 0.0f, hd);
    glTexCoord2f(1, 0); glVertex3f(hw, 0.0f, -hd);
    glTexCoord2f(1, 1); glVertex3f(hw, h, -hd);
    glTexCoord2f(0, 1); glVertex3f(hw, h, hd);

    glEnd();

    glDisable(GL_TEXTURE_2D);
}


void drawOneBuilding(float x, float z, float w, float d, float h,
    GLuint wallTex, bool glowingPanel) {
    glPushMatrix();
    glTranslatef(x, 0.0f, z);

    drawBuildingBody(w, d, h, wallTex);

    // Neon panel on side
    if (glowingPanel) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, gTextures[TEX_BUILDING_LIGHT]);
        glColor3f(1.0f, 1.0f, 1.0f);

        float hw = w * 0.5f;
        glBegin(GL_QUADS);
        glNormal3f(1, 0, 0);
        glTexCoord2f(0, 0); glVertex3f(hw + 0.01f, 0.3f * h, -0.3f * d);
        glTexCoord2f(1, 0); glVertex3f(hw + 0.01f, 0.3f * h, 0.3f * d);
        glTexCoord2f(1, 1); glVertex3f(hw + 0.01f, 0.7f * h, 0.3f * d);
        glTexCoord2f(0, 1); glVertex3f(hw + 0.01f, 0.7f * h, -0.3f * d);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }

    // Roof
    glPushMatrix();
    glTranslatef(0.0f, h, 0.0f);
    drawRoofWithChimney(w, d, h * 0.35f);
    glPopMatrix();

    glPopMatrix();
}


void drawAllBuildings() {
    drawOneBuilding(-30, -30, 15, 15, 25, gTextures[TEX_BUILDING1], false);
    drawOneBuilding(-10, -40, 10, 16, 18, gTextures[TEX_BUILDING2], true);
    drawOneBuilding(20, -35, 20, 20, 30, gTextures[TEX_BUILDING1], true);
    drawOneBuilding(40, 15, 16, 18, 22, gTextures[TEX_BUILDING2], false);
    drawOneBuilding(-40, 20, 20, 16, 26, gTextures[TEX_BUILDING1], true);
    drawOneBuilding(10, 35, 14, 14, 18, gTextures[TEX_BUILDING2], false);
}


//Draw small house
void drawSmallHouse(float x, float z, float h) {
    float w = 3.5f;
    float d = 3.5f;

    glDisable(GL_TEXTURE_2D);
    glPushMatrix();
    glTranslatef(x, 0.0f, z);

    // body
    glColor3f(0.72f, 0.76f, 0.80f);
    glBegin(GL_QUADS);

    // Front
    glNormal3f(0, 0, 1);
    glVertex3f(-w / 2, 0, d / 2);
    glVertex3f(w / 2, 0, d / 2);
    glVertex3f(w / 2, h, d / 2);
    glVertex3f(-w / 2, h, d / 2);

    // Back
    glNormal3f(0, 0, -1);
    glVertex3f(w / 2, 0, -d / 2);
    glVertex3f(-w / 2, 0, -d / 2);
    glVertex3f(-w / 2, h, -d / 2);
    glVertex3f(w / 2, h, -d / 2);

    // Left
    glNormal3f(-1, 0, 0);
    glVertex3f(-w / 2, 0, -d / 2);
    glVertex3f(-w / 2, 0, d / 2);
    glVertex3f(-w / 2, h, d / 2);
    glVertex3f(-w / 2, h, -d / 2);

    // Right
    glNormal3f(1, 0, 0);
    glVertex3f(w / 2, 0, d / 2);
    glVertex3f(w / 2, 0, -d / 2);
    glVertex3f(w / 2, h, -d / 2);
    glVertex3f(w / 2, h, d / 2);

    glEnd();

    // cyan tech lines
    glDisable(GL_LIGHTING);
    glColor3f(0.35f, 0.95f, 1.0f);
    glLineWidth(2.0f);

    // horizontal bands
    for (float hy = 0.6f; hy < h; hy += 0.7f) {
        glBegin(GL_LINES);
        glVertex3f(-w / 2, hy, d / 2);
        glVertex3f(w / 2, hy, d / 2);

        glVertex3f(-w / 2, hy, -d / 2);
        glVertex3f(w / 2, hy, -d / 2);
        glEnd();
    }

    // front diagonal cross-lines
    glBegin(GL_LINES);
    glVertex3f(-w / 2, 0, d / 2); glVertex3f(w / 2, h, d / 2);
    glVertex3f(w / 2, 0, d / 2); glVertex3f(-w / 2, h, d / 2);
    glEnd();

    glEnable(GL_LIGHTING);
    glPopMatrix();
    glEnable(GL_TEXTURE_2D);
}


void drawSmallHouseField() {
    for (auto& sh : citySmallHouses) {
        drawSmallHouse(sh.x, sh.z, sh.h);
    }
}


//Draw terminal house for train
void drawTerminalHouse(float centerX) {
    float h = 5.0f;
    float w = 4.0f;
    float d = 4.0f;

    glDisable(GL_TEXTURE_2D);
    glPushMatrix();
    glTranslatef(centerX, TRACK_TOP_Y, TRACK_Z);

    // body
    glColor3f(0.82f, 0.86f, 0.90f);
    glPushMatrix();
    glScalef(w, h, d);
    glutSolidCube(1.0f);
    glPopMatrix();

    // tech line details
    glDisable(GL_LIGHTING);
    glLineWidth(2.0f);
    glColor3f(0.4f, 0.95f, 1.0f);

    float fw = w * 0.5f, fh = h, fd = d * 0.5f;
    for (float hy = 0.5f; hy < fh; hy += 0.7f) {
        glBegin(GL_LINES);
        glVertex3f(-fw, hy, fd); glVertex3f(fw, hy, fd);
        glVertex3f(-fw, hy, -fd); glVertex3f(fw, hy, -fd);
        glEnd();
    }

    glEnable(GL_LIGHTING);

    // roof
    glPushMatrix();
    glTranslatef(0, h, 0);
    glColor3f(0.92f, 0.95f, 0.98f);
    drawRoofWithChimney(w, d, 2.0f);
    glPopMatrix();

    glPopMatrix();
    glEnable(GL_TEXTURE_2D);
}


//Draw rail track
void drawRailTrack() {
    // Elevated beam
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.80f, 0.83f, 0.88f);
    glPushMatrix();
    glTranslatef(0.0f, TRACK_TOP_Y, TRACK_Z);
    glScalef(80.0f, 0.6f, 4.0f);
    glutSolidCube(1.0f);
    glPopMatrix();

    // Track top with texture
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gTextures[TEX_RAIL]);
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);
    glTexCoord2f(0, 0); glVertex3f(-40, TRACK_TOP_Y + 0.02f, TRACK_Z - 0.8f);
    glTexCoord2f(8, 0); glVertex3f(40, TRACK_TOP_Y + 0.02f, TRACK_Z - 0.8f);
    glTexCoord2f(8, 1); glVertex3f(40, TRACK_TOP_Y + 0.02f, TRACK_Z + 0.8f);
    glTexCoord2f(0, 1); glVertex3f(-40, TRACK_TOP_Y + 0.02f, TRACK_Z + 0.8f);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    // supports
    glColor3f(0.75f, 0.78f, 0.82f);
    for (int i = -40; i <= 40; i += 10) {
        glPushMatrix();
        glTranslatef(i, TRACK_TOP_Y - 2.0f, TRACK_Z);
        glScalef(1.5f, 4.0f, 1.5f);
        glutSolidCube(1.0);
        glPopMatrix();
    }

    // left & right terminal houses
    drawTerminalHouse(-40.0f);
    drawTerminalHouse(40.0f);
}


//Draw train
void drawTrainBody() {
    glColor3f(0.95f, 0.97f, 1.0f);
    drawTexturedBox(12.0f, 4.0f, 3.0f, gTextures[TEX_TRAIN_SIDE]);

    // Nose
    glDisable(GL_TEXTURE_2D);
    glPushMatrix();
    glTranslatef(6.0f, 0.0f, 0.0f);
    glScalef(2.0f, 2.0f, 2.0f);
    glColor3f(0.96f, 0.98f, 1.0f);
    glutSolidSphere(1.0f, 16, 16);
    glPopMatrix();

    // Windows
    glColor3f(0.25f, 0.55f, 0.95f);
    glBegin(GL_QUADS);
    // right
    glNormal3f(0, 0, 1);
    glVertex3f(-4.0f, 1.0f, 1.6f);
    glVertex3f(4.0f, 1.0f, 1.6f);
    glVertex3f(4.0f, 2.0f, 1.6f);
    glVertex3f(-4.0f, 2.0f, 1.6f);
    // left
    glNormal3f(0, 0, -1);
    glVertex3f(-4.0f, 1.0f, -1.6f);
    glVertex3f(4.0f, 1.0f, -1.6f);
    glVertex3f(4.0f, 2.0f, -1.6f);
    glVertex3f(-4.0f, 2.0f, -1.6f);
    glEnd();

    // Headlights
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 0.9f, 0.7f);
    glBegin(GL_QUADS);
    glVertex3f(6.5f, 0.5f, 0.5f);
    glVertex3f(6.5f, 0.5f, -0.5f);
    glVertex3f(6.5f, 1.0f, -0.5f);
    glVertex3f(6.5f, 1.0f, 0.5f);
    glEnd();
    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
}

void drawTrain() {
    if (!trainVisible) return;
    glPushMatrix();
    glTranslatef(trainPos, TRAIN_Y, TRACK_Z);
    drawTrainBody();
    glPopMatrix();
}


//Draw robot
void drawRobot() {
    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 10.0f);

    // Body
    glPushMatrix();
    glTranslatef(0.0f, 5.0f, 0.0f);
    glColor3f(0.90f, 0.92f, 0.95f);
    drawTexturedBox(3.0f, 4.0f, 1.8f, gTextures[TEX_ROBOT_METAL]);
    glPopMatrix();

    // Head (yaw)
    glPushMatrix();
    glTranslatef(0.0f, 7.5f, 0.0f);
    glRotatef(robotHeadYaw, 0, 1, 0);
    glColor3f(0.96f, 0.97f, 1.0f);
    drawTexturedBox(2.0f, 2.0f, 2.0f, gTextures[TEX_ROBOT_METAL]);

    // Face screen
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gTextures[TEX_ROBOT_FACE]);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    glTexCoord2f(0, 0); glVertex3f(-0.8f, -0.4f, 1.1f);
    glTexCoord2f(1, 0); glVertex3f(0.8f, -0.4f, 1.1f);
    glTexCoord2f(1, 1); glVertex3f(0.8f, 0.6f, 1.1f);
    glTexCoord2f(0, 1); glVertex3f(-0.8f, 0.6f, 1.1f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();

    // Left upper arm
    glPushMatrix();
    glTranslatef(-2.0f, 5.5f, 0.0f);
    glRotatef(leftUpperArmRotX, 1, 0, 0);
    drawTexturedBox(1.0f, 2.0f, 1.0f, gTextures[TEX_ROBOT_METAL]);

    // Left lower arm
    glTranslatef(0.0f, -1.5f, 0.0f);
    glRotatef(leftLowerArmRotX, 1, 0, 0);
    drawTexturedBox(1.0f, 2.0f, 1.0f, gTextures[TEX_ROBOT_METAL]);
    glPopMatrix();

    // Right upper arm
    glPushMatrix();
    glTranslatef(2.0f, 5.5f, 0.0f);
    glRotatef(rightUpperArmRotX, 1, 0, 0);
    drawTexturedBox(1.0f, 2.0f, 1.0f, gTextures[TEX_ROBOT_METAL]);

    // Right lower arm
    glTranslatef(0.0f, -1.5f, 0.0f);
    glRotatef(rightLowerArmRotX, 1, 0, 0);
    drawTexturedBox(1.0f, 2.0f, 1.0f, gTextures[TEX_ROBOT_METAL]);
    glPopMatrix();

    // Left leg (thigh + shin)
    glPushMatrix();
    glTranslatef(-1.0f, 3.0f, 0.0f);
    glRotatef(leftThighRotX, 1, 0, 0);
    drawTexturedBox(1.0f, 2.2f, 1.0f, gTextures[TEX_ROBOT_METAL]);

    glTranslatef(0.0f, -1.8f, 0.0f);
    glRotatef(leftShinRotX, 1, 0, 0);
    drawTexturedBox(1.0f, 2.0f, 1.0f, gTextures[TEX_ROBOT_METAL]);
    glPopMatrix();

    // Right leg
    glPushMatrix();
    glTranslatef(1.0f, 3.0f, 0.0f);
    glRotatef(rightThighRotX, 1, 0, 0);
    drawTexturedBox(1.0f, 2.2f, 1.0f, gTextures[TEX_ROBOT_METAL]);

    glTranslatef(0.0f, -1.8f, 0.0f);
    glRotatef(rightShinRotX, 1, 0, 0);
    drawTexturedBox(1.0f, 2.0f, 1.0f, gTextures[TEX_ROBOT_METAL]);
    glPopMatrix();

    glPopMatrix();

    // Surrounding simple buildings to integrate into environment
    drawSmallHouse(8.0f, 8.0f, robotSceneHouseHeights[0]);
    drawSmallHouse(-8.0f, 8.0f, robotSceneHouseHeights[1]);
    drawSmallHouse(8.0f, -8.0f, robotSceneHouseHeights[2]);
    drawSmallHouse(-8.0f, -8.0f, robotSceneHouseHeights[3]);
}


//Draw drone
void drawDrone() {
    glPushMatrix();
    glTranslatef(droneX, droneY, droneZ);
    glRotatef(droneAngleDeg, 0, 1, 0);

    // Body
    glColor3f(0.95f, 0.97f, 1.0f);
    drawTexturedBox(2.0f, 1.0f, 2.0f, gTextures[TEX_ROBOT_METAL]);

    // Arms
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.75f, 0.80f, 0.85f);
    for (int i = 0; i < 4; ++i) {
        float a = i * 90.0f;
        glPushMatrix();
        glRotatef(a, 0, 1, 0);
        glTranslatef(1.8f, 0.0f, 0.0f);
        glScalef(2.0f, 0.2f, 0.2f);
        glutSolidCube(1.0f);
        glPopMatrix();
    }

    // Propellers
    glDisable(GL_LIGHTING);
    glColor3f(0.3f, 0.3f, 0.3f);
    for (int i = 0; i < 4; ++i) {
        float a = i * 90.0f;
        glPushMatrix();
        glRotatef(a, 0, 1, 0);
        glTranslatef(3.5f, 0.4f, 0.0f);
        glRotatef(dronePropeller, 0, 1, 0);
        glBegin(GL_TRIANGLES);
        glVertex3f(0.0f, 0.0f, 0.0f);
        glVertex3f(0.9f, 0.0f, 0.3f);
        glVertex3f(-0.9f, 0.0f, 0.3f);
        glEnd();
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);

    glPopMatrix();
}


//Draw galaxy background(at night)
void drawGalaxyBackground() {
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);

    glPointSize(2.0f);
    glBegin(GL_POINTS);
    glColor3f(0.8f, 0.9f, 1.0f); // star color

    for (int i = 0; i < 600; i++) {
        float x = ((rand() % 200) - 100) * 2.0f;
        float y = (rand() % 200);
        float z = -150.0f - (rand() % 200);
        glVertex3f(x, y, z);
    }
    glEnd();

    // Planet / sun / moon
    glPushMatrix();
    glTranslatef(-60.0f, 50.0f, -200.0f);

    if (!isNightMode) {
        // Day: sun
        glColor3f(1.0f, 0.65f, 0.0f);
    }
    else {
        // Night: moon
        glColor3f(0.95f, 0.95f, 0.75f);
    }

    glutSolidSphere(20.0f, 32, 32);
    glPopMatrix();

    glEnable(GL_LIGHTING);
}


//Draw direction light
void drawLightDirectionUI() {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, gWindowWidth, 0, gWindowHeight);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);

    float cx = 80;                      // UI center
    float cy = gWindowHeight - 80;

    float dx = std::cos(cyberLightYawDeg * 3.14159f / 180.0f);
    float dy = std::sin(cyberLightPitchDeg * 3.14159f / 180.0f);

    glColor3f(0.1f, 0.9f, 1.0f);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    glVertex2f(cx, cy);
    glVertex2f(cx + dx * 40, cy + dy * 40);
    glEnd();

    // light head
    glPointSize(8.0f);
    glBegin(GL_POINTS);
    glVertex2f(cx + dx * 40, cy + dy * 40);
    glEnd();

    glEnable(GL_LIGHTING);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}


//Draw help hint
void drawHelpHint() {
    if (!showHelpHint) return;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, gWindowWidth, 0, gWindowHeight);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);

    // semi-transparent rectangle
    glColor4f(0.05f, 0.1f, 0.15f, 0.7f);
    glBegin(GL_QUADS);
    glVertex2f(gWindowWidth - 200, 20);
    glVertex2f(gWindowWidth - 20, 20);
    glVertex2f(gWindowWidth - 20, 65);
    glVertex2f(gWindowWidth - 200, 65);
    glEnd();

    // border
    glColor3f(0.2f, 0.8f, 1.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(gWindowWidth - 200, 20);
    glVertex2f(gWindowWidth - 20, 20);
    glVertex2f(gWindowWidth - 20, 65);
    glVertex2f(gWindowWidth - 200, 65);
    glEnd();

    // text
    glColor3f(0.85f, 0.95f, 1.0f);
    glRasterPos2f(gWindowWidth - 190, 40);
    const char* hint = "Press 'H' for Help";
    while (*hint) {
        glutBitmapCharacter(GLUT_BITMAP_9_BY_15, *hint);
        hint++;
    }

    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}


//Simple helper for drawing bitmap strings
void drawString(const char* s) {
    while (*s) {
        glutBitmapCharacter(GLUT_BITMAP_9_BY_15, *s);
        ++s;
    }
}


//Draw help panel 
void drawHelpUI() {
    if (!showHelp) return;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, gWindowWidth, 0, gWindowHeight);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);

    // background
    glColor4f(0.05f, 0.1f, 0.15f, 0.8f);
    glBegin(GL_QUADS);
    glVertex2f(gWindowWidth - 330, 20);
    glVertex2f(gWindowWidth - 20, 20);
    glVertex2f(gWindowWidth - 20, 220);
    glVertex2f(gWindowWidth - 330, 220);
    glEnd();

    // border
    glColor3f(0.2f, 0.8f, 1.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(gWindowWidth - 330, 20);
    glVertex2f(gWindowWidth - 20, 20);
    glVertex2f(gWindowWidth - 20, 220);
    glVertex2f(gWindowWidth - 330, 220);
    glEnd();

    // text lines
    glColor3f(0.9f, 0.95f, 1.0f);

    const char* text1 = "";
    const char* text2 = "";
    const char* text3 = "";
    const char* text4 = "";
    const char* text5 = "";
    const char* text6 = "";

    if (currentScene == 1) {
        text1 = "Scene 1 - Bird View";
        text2 = "Left click: Toggle drone view";
        text3 = "Arrow Keys: Adjust lighting";
        text4 = "direction";
        text5 = "W/S: Adjust Train speed";
        text6 = "SPACE: pause train";
    }
    else if (currentScene == 2) {
        text1 = "Scene 2 - Robot Close-up";
        text2 = "Robot joint control:";
        text3 = "Q/E: Head  ";
        text4 = "A/D/J/L: UpperArm";
        text5 = "Z/X/N/M: LowerArm";
        text6 = "T/G/U/K: Thigh | Y/R/I/O: Shin";
    }
    else if (currentScene == 3) {
        text1 = "Scene 3 - Street Level";
        text2 = "Right-drag mouse: orbit camera";
        text3 = "Arrow Keys: Adjust lighting";
        text4 = "W/S Train speed | SPACE pause";
    }
    else if (currentScene == 4) {
        text1 = "Scene 4 - Train View";
        text2 = "V: Switch to train first-person";
        text3 = "view";
        text4 = "W/S: Train Speed up/down";
        text5 = "SPACE: Pause";
        text6 = "Arrow Keys: Adjust global light";
    }

    glRasterPos2f(gWindowWidth - 320, 190); drawString(text1);
    glRasterPos2f(gWindowWidth - 320, 165); drawString(text2);
    glRasterPos2f(gWindowWidth - 320, 140); drawString(text3);
    glRasterPos2f(gWindowWidth - 320, 115); drawString(text4);
    glRasterPos2f(gWindowWidth - 320, 90); drawString(text5);
    glRasterPos2f(gWindowWidth - 320, 65); drawString(text6);

    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}


//Draw shadow (for robot)
void startShadow(float floorY) {
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.1f, 0.1f, 0.1f, 0.5f); // semi-transparent

    glPushMatrix();
    glTranslatef(0, floorY + 0.1f, 0);
    glScalef(1.0f, 0.0f, 1.0f);  // squash Y
}

void endShadow() {
    glPopMatrix();
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
}


//Draw billboards(on the street)
void drawBillboards() {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gTextures[TEX_AD_SCREEN1]);
    glColor3f(1.0f, 1.0f, 1.0f);

    glPushMatrix();
    glTranslatef(8.0f, 5.0f, 0.0f);
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    glTexCoord2f(0, 0); glVertex3f(-3.0f, 0.0f, 0.0f);
    glTexCoord2f(1, 0); glVertex3f(3.0f, 0.0f, 0.0f);
    glTexCoord2f(1, 1); glVertex3f(3.0f, 4.0f, 0.0f);
    glTexCoord2f(0, 1); glVertex3f(-3.0f, 4.0f, 0.0f);
    glEnd();
    glPopMatrix();
}


//Render each scenes
void drawScene1_BirdView() {
    setCameraForScene1();
    drawGroundAndRoads();
    drawAllBuildings();
    drawSmallHouseField();
    drawRailTrack();
    drawTrain();
    drawRobot();    // tiny robot in distance
    drawDrone();
    drawBillboards();

    // robot shadow on ground
    startShadow(0.0f);
    drawRobot();
    endShadow();
}

void drawScene2_RobotView() {
    setCameraForScene2();
    drawGroundAndRoads();
    drawAllBuildings();
    drawBillboards();
    drawRailTrack();
    drawSmallHouseField();
    drawDrone();         // still visible in distance
    drawRobot();         // target object

    startShadow(0.0f);
    drawRobot();
    endShadow();
}

void drawScene3_BuildingLightView() {
    setCameraForScene3();
    drawGroundAndRoads();
    drawAllBuildings();
    drawSmallHouseField();
    drawBillboards();
    drawRailTrack();
    drawTrain();
    drawRobot();
    drawDrone();

    startShadow(0.0f);
    drawRobot();
    endShadow();
}

void drawScene4_TrainView() {
    setCameraForScene4();
    drawGroundAndRoads();
    drawRailTrack();
    drawTrain();
    drawSmallHouseField();
   
    drawDrone();
    drawRobot();

    startShadow(0.0f);
    drawRobot();
    endShadow();
}


//Display function
void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    setupBaseLighting();
    applyDayNightMode();

    if (isNightMode) {
        drawGalaxyBackground();  // galaxy only at night
    }

    switch (currentScene) {
    case 1: drawScene1_BirdView();          break;
    case 2: drawScene2_RobotView();         break;
    case 3: drawScene3_BuildingLightView(); break;
    case 4: drawScene4_TrainView();         break;
    default: drawScene1_BirdView();         break;
    }

    //UI
    drawLightDirectionUI();
    drawHelpUI();
    drawHelpHint();

    glutSwapBuffers();
}


//Define keyboard interaction
void keyboard(unsigned char key, int, int) {
    switch (key) {

        // ---- Scene switching ----
    case '1': case '2': case '3': case '4':
        currentScene = key - '0';
        cameraMode = CAM_NORMAL;
        break;

        // ---- Robot joints (Scene 2) ----
        // Head yaw
    case 'q': case 'Q': robotHeadYaw += 5.0f; break;
    case 'e': case 'E': robotHeadYaw -= 5.0f; break;

        // Left arm
    case 'a': case 'A': leftUpperArmRotX += 5.0f; break;
    case 'd': case 'D': leftUpperArmRotX -= 5.0f; break;
    case 'z': case 'Z': leftLowerArmRotX += 5.0f; break;
    case 'x': case 'X': leftLowerArmRotX -= 5.0f; break;

        // Right arm
    case 'j': case 'J': rightUpperArmRotX += 5.0f; break;
    case 'l': case 'L': rightUpperArmRotX -= 5.0f; break;
    case 'n': case 'N': rightLowerArmRotX += 5.0f; break;
    case 'm': case 'M': rightLowerArmRotX -= 5.0f; break;

        // Left leg
    case 't': case 'T': leftThighRotX += 5.0f; break;
    case 'g': case 'G': leftThighRotX -= 5.0f; break;
    case 'y': case 'Y': leftShinRotX += 5.0f; break;
    case 'r': case 'R': leftShinRotX -= 5.0f; break;

        // Right leg
    case 'u': case 'U': rightThighRotX += 5.0f; break;
    case 'k': case 'K': rightThighRotX -= 5.0f; break;
    case 'i': case 'I': rightShinRotX += 5.0f; break;
    case 'o': case 'O': rightShinRotX -= 5.0f; break;

        // ---- Train control ----
    case 'w': case 'W':
        trainSpeed += 0.05f;
        break;

    case 's': case 'S':
        trainSpeed -= 0.05f;
        if (trainSpeed < 0.0f) trainSpeed = 0.0f;
        break;

    case ' ':
        trainRunning = !trainRunning;
        break;

    case 'v': case 'V':
        if (currentScene == 4) {
            cameraMode = (cameraMode == CAM_TRAIN_VIEW)
                ? CAM_NORMAL : CAM_TRAIN_VIEW;
        }
        break;

        // ---- Help UI toggle ----
    case 'h': case 'H':
        showHelp = !showHelp;
        if (showHelp) {
            showHelpHint = false;
        }
        else {
            showHelpHint = true;
        }
        break;

        // ---- Day / Night toggle ----
    case 'b': case 'B':
        isNightMode = false;
        printf("Switched to DAY mode\n");
        break;

    case 'c': case 'C':
        isNightMode = true;
        printf("Switched to NIGHT mode\n");
        break;

        // ---- Exit ----
    case 27: // ESC
        std::exit(0);
        break;

    default:
        break;
    }

    glutPostRedisplay();
}


//Define arrow keys interaction
void specialKeys(int key, int, int) {
    const float step = 5.0f;

    switch (key) {
    case GLUT_KEY_LEFT:
        cyberLightYawDeg -= step;
        break;
    case GLUT_KEY_RIGHT:
        cyberLightYawDeg += step;
        break;
    case GLUT_KEY_UP:
        cyberLightPitchDeg += step;
        if (cyberLightPitchDeg > 80) cyberLightPitchDeg = 80;
        break;
    case GLUT_KEY_DOWN:
        cyberLightPitchDeg -= step;
        if (cyberLightPitchDeg < -10) cyberLightPitchDeg = -10;
        break;
    default:
        break;
    }

    glutPostRedisplay();
}


void reshape(int w, int h) {
    gWindowWidth = w;
    gWindowHeight = h;
    glViewport(0, 0, w, h);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, (float)w / (float)h, 1.0, 500.0);
    glMatrixMode(GL_MODELVIEW);
}


//Mouse interaction
void mouse(int button, int state, int x, int y) {
    // Right button: Scene 3 orbit camera
    if (button == GLUT_RIGHT_BUTTON) {
        if (currentScene == 3) {
            if (state == GLUT_DOWN) {
                scene3OrbitActive = true;
                lastMouseX = x;
                lastMouseY = y;
            }
            else {
                scene3OrbitActive = false;
            }
        }
    }

    // Left button: Scene 1 drone-view toggle
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        if (currentScene == 1) {
            GLdouble model[16], proj[16];
            GLint    viewport[4];
            glGetDoublev(GL_MODELVIEW_MATRIX, model);
            glGetDoublev(GL_PROJECTION_MATRIX, proj);
            glGetIntegerv(GL_VIEWPORT, viewport);

            GLdouble winx, winy, winz;

            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            glLoadIdentity();
            setCameraForScene1();
            glGetDoublev(GL_MODELVIEW_MATRIX, model);
            glPopMatrix();

            gluProject(droneX, droneY, droneZ,
                model, proj, viewport,
                &winx, &winy, &winz);

            float dx = (float)winx - (float)x;
            float dy = (float)winy - (float)(viewport[3] - y);
            if (dx * dx + dy * dy <= droneRadiusPixels * droneRadiusPixels) {
                cameraMode = (cameraMode == CAM_DRONE_VIEW)
                    ? CAM_NORMAL : CAM_DRONE_VIEW;
            }
        }
    }

    glutPostRedisplay();
}


void motion(int x, int y) {
    if (scene3OrbitActive && currentScene == 3) {
        int dx = x - lastMouseX;
        int dy = y - lastMouseY;
        scene3YawDeg += dx * 0.4f;
        scene3PitchDeg += dy * 0.4f;
        if (scene3PitchDeg > 80.0f)  scene3PitchDeg = 80.0f;
        if (scene3PitchDeg < -10.0f) scene3PitchDeg = -10.0f;
        lastMouseX = x;
        lastMouseY = y;
        glutPostRedisplay();
    }
}


//Idle(update animation)
void idle() {
    // ---- Train animation between two terminal houses ----
    if (trainRunning) {
        if (trainVisible) {
            trainPos += trainSpeed * 0.3f;
            if (trainPos >= 40.0f) {
                // enter right house and disappear
                trainVisible = false;
                trainHideCounter = TRAIN_HIDE_FRAMES;
                trainPos = 40.0f;
            }
        }
        else {
            if (trainHideCounter > 0) {
                --trainHideCounter;
            }
            else {
                // reappear from left house
                trainVisible = true;
                trainPos = -40.0f;
            }
        }
    }

    // ---- Drone animation (orbit + propellers) ----
    droneAngleDeg += 0.05f;
    if (droneAngleDeg > 360.0f) droneAngleDeg -= 360.0f;
    float yawRad = droneAngleDeg * 3.1415926f / 180.0f;
    float radius = 18.0f;
    droneX = radius * std::cos(yawRad);
    droneZ = radius * std::sin(yawRad);
    droneY = 16.0f;

    dronePropeller += 10.0f;
    if (dronePropeller > 360.0f) dronePropeller -= 360.0f;

    glutPostRedisplay();
}


void initGL() {
    // initial clear color (night style, will be overridden by day/night switch)
    glClearColor(0.02f, 0.04f, 0.10f, 1.0f);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);

    initTextures();
    setupBaseLighting();
    generateSmallHouses();
    trainPos = -40.0f;
}


int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(gWindowWidth, gWindowHeight);
    glutInitWindowPosition(100, 60);
    glutCreateWindow("CPT205CW2 - Hi, Future!");

    initGL();

    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specialKeys);
    glutMouseFunc(mouse);
    glutMotionFunc(motion);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);

    glutMainLoop();
    return 0;
}
