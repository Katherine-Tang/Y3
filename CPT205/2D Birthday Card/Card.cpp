// greeting_card.cpp
// 2D Greeting Card for XJTLU 20th Anniversary
// Author: Zhiying Tang
// Student ID: 2360010

#define FREEGLUT_STATIC
#include <GL/freeglut.h>
#include <vector>
#include <cmath>
#include <string>
#include <ctime>

// Define windows size and control time
int winW = 1000, winH = 600;
float elapsed = 0.0f;
int lastTime = 0;

// Define each object structure
struct Balloon {
    float x, y, vx, vy, radius, hue;
    bool alive;
};
std::vector<Balloon> balloons;

struct Cake {
    bool visible = false;
    float riseProgress = 0.0f; 
} cake;

struct Cloud {
    float x, y, scale, speed, phase, offset;
};
std::vector<Cloud> clouds;

// Set the initial state of the animation 
bool introMode = true;
bool introOpening = false;
float introProgress = 0.0f;
float cloudSpeedMultiplier = 1.0f;
bool cloudReverse = false;
float sunRotation = 0.0f;
bool sunRotating = false;

// Call all the function
void display();
void reshape(int w, int h);
void timerFunc(int val);
void keyboard(unsigned char key, int x, int y);
void mouse(int button, int state, int x, int y);
void drawText(float x, float y, const std::string& s, void* font = GLUT_BITMAP_HELVETICA_18);
void drawScene();
void drawIntro();
void drawBalloon(const Balloon& b);
void spawnBalloon(float x, float y);
void resetScene();
void initClouds();
void drawCloud(float cx, float cy, float scale);
void drawTree(float baseX, float baseY, float scale);
void drawSun(float cx, float cy, float radius, float rotation);
void drawCB(float translationX, float translationY, float w);
void drawFlower(float x, float y, float size);

// Let generated balloon colorful
void hsv2rgb(float h, float s, float v, float& r, float& g, float& b) {
    float c = v * s;
    float hp = h / 60.0f;
    float x = c * (1 - fabs(fmodf(hp, 2.0f) - 1));
    float m = v - c;
    float rr = 0, gg = 0, bb = 0;

    if (0 <= hp && hp < 1) { rr = c; gg = x; }
    else if (1 <= hp && hp < 2) { rr = x; gg = c; }
    else if (2 <= hp && hp < 3) { gg = c; bb = x; }
    else if (3 <= hp && hp < 4) { gg = x; bb = c; }
    else if (4 <= hp && hp < 5) { rr = x; bb = c; }
    else { rr = c; bb = x; }

    r = rr + m; g = gg + m; b = bb + m;
}

// Main function
int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(winW, winH);
    glutCreateWindow("XJTLU 20th Anniversary Greeting Card");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutMouseFunc(mouse);
    glutTimerFunc(16, timerFunc, 0);

    initClouds();
    lastTime = glutGet(GLUT_ELAPSED_TIME);
    glutMainLoop();
    return 0;
}

// Create clouds and set their states
void initClouds() {
    clouds.clear();
    int num = 4;
    for (int i = 0; i < num; ++i) {
        Cloud c;
        c.scale = 0.7f + (rand() % 1000) / 1000.0f;
        c.x = (float)(rand() % winW);
        c.y = winH * (0.65f + (rand() % 25) / 100.0f);
        float dir = (rand() % 2 == 0) ? 1.0f : -1.0f;
        c.speed = dir * (15.0f + (rand() % 40));
        c.phase = (float)(rand() % 1000) / 1000.0f * 6.2831f;
        c.offset = 0.0f;
        clouds.push_back(c);
    }
}

// Set the status for reset
void resetScene() {
    balloons.clear();
    cake.visible = false;
    cake.riseProgress = 0.0f;
    initClouds();
}

// Create balloon (randomly floating balloons of various sizes and colors)
void spawnBalloon(float x, float y) {
    Balloon b;
    b.x = x; b.y = y;
    b.vx = (float)((rand() % 21 - 10) / 100.0f);
    b.vy = (float)(0.6f + (rand() % 20) / 100.0f);
    b.radius = 12.0f + rand() % 10;
    b.hue = (float)(rand() % 360); //balloon colorful(set random colour)
    b.alive = true;
    balloons.push_back(b);
}

// Set mouse interaction(let mouse click at different positions has different effects)
void mouse(int button, int state, int x, int y) {
    if (button != GLUT_LEFT_BUTTON || state != GLUT_DOWN) return;
    float fx = (float)x, fy = (float)(winH - y);

    if (introMode) {
        float envW = 420.0f, envH = 260.0f;
        float cx = winW * 0.5f, cy = winH * 0.5f;
        float left = cx - envW * 0.5f, right = cx + envW * 0.5f;
        float bottom = cy - envH * 0.5f, top = cy + envH * 0.5f;
        if (fx >= left && fx <= right && fy >= bottom && fy <= top)
            introOpening = true;
    }
    else {
        const float baseX_tree = 80.0f;
        const float trunkW = 18.0f, trunkH = 70.0f;
        const float crownTopY = winH * 0.15f + trunkH + 42.0f + 26.0f;
        const float boxW = 95.0f, boxH = 32.0f;
        const float margin = 8.0f;
        const float treeCenterX = baseX_tree + trunkW * 0.5f;

        float boxLeft = treeCenterX - boxW * 0.5f;
        float boxBottom = crownTopY + margin;
        float boxRight = boxLeft + boxW;
        float boxTop = boxBottom + boxH;

        if (fx >= boxLeft && fx <= boxRight && fy >= boxBottom && fy <= boxTop)
            cake.visible = true;
        else
            spawnBalloon((float)x, (float)(winH - y));
    }
}

// Define keyboard input
void keyboard(unsigned char key, int, int) {
    if (key == 'r' || key == 'R') resetScene();
    else if (key == 'q' || key == 'Q' || key == 27) exit(0);
    else if (key == 'd' || key == 'D') cloudSpeedMultiplier = std::min(cloudSpeedMultiplier * 1.5f, 5.0f);
    else if (key == 'a' || key == 'A') cloudSpeedMultiplier = std::max(cloudSpeedMultiplier * 0.7f, 0.1f);
    else if (key == 's' || key == 'S') cloudReverse = !cloudReverse;
    else if (key == 'k' || key == 'K') sunRotating = !sunRotating;
}

// Set animation effects
void timerFunc(int) {
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = (now - lastTime) / 1000.0f;
    if (dt > 0.05f) dt = 0.05f;
    elapsed += dt;
    lastTime = now;

    // Balloons move
    for (size_t i = 0; i < balloons.size(); ++i) {
        Balloon& b = balloons[i];
        if (!b.alive) continue;
        b.x += b.vx * 60.0f * dt;
        b.y += b.vy * 60.0f * dt;

        b.x += 10.0f * sinf(elapsed * 1.5f + (float)i) * dt;

        if (b.y - b.radius > winH + 50) b.alive = false;
    }

    // Envelope open
    if (introMode && introOpening) {
        introProgress += dt * 1.5f;
        if (introProgress >= 1.0f) {
            introProgress = 1.0f;
            introMode = false;
        }
    }

    // Clouds move
    for (auto& c : clouds) {
        float speed = c.speed * cloudSpeedMultiplier;
        if (cloudReverse) speed = -speed;
        c.x += speed * dt;
        c.offset = 4.0f * sinf(elapsed * (0.6f + 0.2f * c.scale) + c.phase);
        if (c.x - 120.0f * c.scale > winW + 20) c.x = -120.0f * c.scale;
        if (c.x + 120.0f * c.scale < -20) c.x = winW + 120.0f * c.scale;
    }
    //Sun rotate
    if (sunRotating) {
        sunRotation += dt * 60.0f;
        if (sunRotation >= 360.0f) sunRotation -= 360.0f;
    }

    glutPostRedisplay();
    glutTimerFunc(16, timerFunc, 0);
}


// Reshape coordinate when adjust the window
void reshape(int w, int h) {
    winW = w; winH = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluOrtho2D(0.0, (GLdouble)winW, 0.0, (GLdouble)winH);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}

// Define Text drawing
void drawText(float x, float y, const std::string& s, void* font) {
    glRasterPos2f(x, y);
    for (char c : s) glutBitmapCharacter(font, c);
}

// Drawing balloon
void drawBalloon(const Balloon& b) {
    float r, g, bb;
    hsv2rgb(b.hue, 0.6f, 0.95f, r, g, bb);

    glColor3f(r, g, bb);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(b.x, b.y);
    int segments = 24;
    for (int i = 0; i <= segments; ++i) {
        float ang = (float)i / segments * 2.0f * 3.1415926f;
        glVertex2f(b.x + cosf(ang) * b.radius, b.y + sinf(ang) * b.radius);
    }
    glEnd();

    glColor3f(0.3f, 0.15f, 0.05f);
    glBegin(GL_LINE_STRIP);
    glVertex2f(b.x, b.y - b.radius);
    glVertex2f(b.x + b.vx * 25.0f, b.y - b.radius - 20.0f);
    glVertex2f(b.x - b.vx * 10.0f, b.y - b.radius - 40.0f);
    glEnd();
}

// Main Scene
void drawScene() {
    // Background(gradual change)
    glBegin(GL_QUADS);
    glColor3f(0.98f, 0.9f, 0.97f);
    glVertex2f(0, winH);
    glVertex2f(winW, winH);
    glColor3f(0.82f, 0.92f, 1.0f);
    glVertex2f(winW, 0);
    glVertex2f(0, 0);
    glEnd();

    // Card border
    glColor3f(1.0f, 0.95f, 0.98f);
    glBegin(GL_QUADS);
    glVertex2f(20, 20); glVertex2f(winW - 20, 20);
    glVertex2f(winW - 20, winH - 20); glVertex2f(20, winH - 20);
    glEnd();

    glColor3f(0.95f, 0.85f, 0.9f);
    glLineWidth(3.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(20, 20); glVertex2f(winW - 20, 20);
    glVertex2f(winW - 20, winH - 20); glVertex2f(20, winH - 20);
    glEnd();

    // Clouds and sun
    for (auto& c : clouds) drawCloud(c.x, c.y + c.offset, c.scale);
    drawSun(winW - 100.0f, winH - 100.0f, 50.0f, sunRotation);

    // Ground
    glColor3f(0.12f, 0.5f, 0.12f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0); glVertex2f(winW, 0);
    glVertex2f(winW, winH * 0.15f); glVertex2f(0, winH * 0.15f);
    glEnd();

    float baseY = winH * 0.15f;
    drawTree(80.0f, baseY, 1.0f);

    // Tree and small box(right above the tree)
    const float baseX_tree = 80.0f;
    const float trunkW = 18.0f, trunkH = 70.0f;
    const float crownTopY = baseY + trunkH + 42.0f + 26.0f;
    const float boxW = 95.0f, boxH = 32.0f;
    const float margin = 8.0f;
    const float treeCenterX = baseX_tree + trunkW * 0.5f;

    float floatY = sinf(elapsed * 2.0f) * 3.5f;
    float boxX = treeCenterX - boxW * 0.5f;
    float boxY = crownTopY + margin + floatY;

    glColor3f(1.0f, 0.9f, 0.95f);
    glBegin(GL_QUADS);
    glVertex2f(boxX, boxY);
    glVertex2f(boxX + boxW, boxY);
    glVertex2f(boxX + boxW, boxY + boxH);
    glVertex2f(boxX, boxY + boxH);
    glEnd();

    glColor3f(0.95f, 0.6f, 0.8f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(boxX, boxY);
    glVertex2f(boxX + boxW, boxY);
    glVertex2f(boxX + boxW, boxY + boxH);
    glVertex2f(boxX, boxY + boxH);
    glEnd();

    glColor3f(0.95f, 0.3f, 0.6f);
    drawText(boxX + 18.0f, boxY + 12.0f, "Click me!", GLUT_BITMAP_HELVETICA_12);

    //CB
    drawCB(winW * 0.5f - 300.0f, baseY - 160.0f, 1.0f);

    // Flowers
    for (int i = 0; i < 8; ++i) {
        float fx = 60.0f + i * 100.0f;
        if (fx > 350.0f && fx < 600.0f) continue;
        drawFlower(fx, baseY + 12.0f, 8.0f);
    }

    // Balloons
    for (const auto& b : balloons) if (b.alive) drawBalloon(b);

    // Title
    glColor3f(0.85f, 0.25f, 0.45f);
    drawText(winW * 0.33f, winH * 0.9f, "Happy 20th Anniversary of XJTLU", GLUT_BITMAP_TIMES_ROMAN_24);

    // Instructions
    glColor3f(0.85f, 0.85f, 0.9f);
    drawText(10.0f, 10.0f, "Left Click: Balloon | Click Box: Cake | R: Reset | Q: Quit", GLUT_BITMAP_8_BY_13);
    drawText(10.0f, 25.0f, "A/D: Cloud Speed | S: Reverse Clouds | K: Rotate Sun", GLUT_BITMAP_8_BY_13);

    // Cake drawing
    if (cake.visible) {
        float groundY = winH * 0.15f;
        float targetY = groundY + 50.0f;
        if (cake.riseProgress < 1.0f) cake.riseProgress += 0.01f;

        float cakeCenterX = winW / 2 - 160.0f;
        float cakeY = 80.0f + (targetY - 70.0f) * cake.riseProgress;

        // Bottom layer
        glColor3f(1.0f, 0.75f, 0.85f);
        glBegin(GL_QUADS);
        glVertex2f(cakeCenterX - 120, cakeY);
        glVertex2f(cakeCenterX + 120, cakeY);
        glVertex2f(cakeCenterX + 120, cakeY + 65);
        glVertex2f(cakeCenterX - 120, cakeY + 65);
        glEnd();

        // White rim
        glColor3f(1.0f, 0.97f, 0.99f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cakeCenterX, cakeY + 65);
        for (int i = 0; i <= 36; ++i) {
            float a = i / 36.0f * 2 * 3.1415926f;
            glVertex2f(cakeCenterX + cosf(a) * 120, cakeY + 65 + sinf(a) * 10);
        }
        glEnd();

        // Top layer
        glColor3f(1.0f, 0.78f, 0.88f);
        glBegin(GL_QUADS);
        glVertex2f(cakeCenterX - 80, cakeY + 65);
        glVertex2f(cakeCenterX + 80, cakeY + 65);
        glVertex2f(cakeCenterX + 80, cakeY + 115);
        glVertex2f(cakeCenterX - 80, cakeY + 115);
        glEnd();

        // Candles
        glColor3f(1.0f, 0.85f, 0.5f);
        for (int i = -25; i <= 25; i += 50) {
            glBegin(GL_QUADS);
            glVertex2f(cakeCenterX + i - 3, cakeY + 115);
            glVertex2f(cakeCenterX + i + 3, cakeY + 115);
            glVertex2f(cakeCenterX + i + 3, cakeY + 135);
            glVertex2f(cakeCenterX + i - 3, cakeY + 135);
            glEnd();

            glColor3f(1.0f, 0.6f, 0.1f);
            glBegin(GL_TRIANGLE_FAN);
            glVertex2f(cakeCenterX + i, cakeY + 141);
            for (int k = 0; k <= 12; ++k) {
                float a = k / 12.0f * 2 * 3.1415926f;
                glVertex2f(cakeCenterX + i + cos(a) * 4, cakeY + 141 + sin(a) * 6);
            }
            glEnd();
            glColor3f(1.0f, 0.85f, 0.5f);
        }

        // Banner
        float flagTopY = cakeY;
        float flagWidth = 200.0f;
        float flagHeight = 60.0f;

        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glVertex2f(cakeCenterX - flagWidth * 0.5f, flagTopY);
        glVertex2f(cakeCenterX + flagWidth * 0.5f, flagTopY);
        glVertex2f(cakeCenterX + flagWidth * 0.5f, flagTopY - flagHeight);
        glVertex2f(cakeCenterX - flagWidth * 0.5f, flagTopY - flagHeight);
        glEnd();

        glColor3f(1.0f, 0.7f, 0.8f);
        glLineWidth(2.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(cakeCenterX - flagWidth * 0.5f, flagTopY);
        glVertex2f(cakeCenterX + flagWidth * 0.5f, flagTopY);
        glVertex2f(cakeCenterX + flagWidth * 0.5f, flagTopY - flagHeight);
        glVertex2f(cakeCenterX - flagWidth * 0.5f, flagTopY - flagHeight);
        glEnd();

        glColor3f(0.8f, 0.2f, 0.4f);
        drawText(cakeCenterX - 95, flagTopY - 38, "Happy 20th anniversary of XJTLU", GLUT_BITMAP_HELVETICA_12);
    }
}

// Display and initialize(choose to draw two scenes based on the current mode.)
void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    if (introMode) drawIntro();
    else drawScene();
    glutSwapBuffers();
}

// Draw the cover scene 
void drawIntro() {
    // Background
    glBegin(GL_QUADS);
    glColor3f(1.0f, 0.88f, 0.92f);
    glVertex2f(0, winH);
    glVertex2f(winW, winH);
    glColor3f(1.0f, 0.95f, 0.98f);
    glVertex2f(winW, 0);
    glVertex2f(0, 0);
    glEnd();

    float cx = winW * 0.5f, cy = winH * 0.5f;
    float envW = winW, envH = winH;

    // Envelope background
    glColor3f(1.0f, 0.9f, 0.93f);
    glBegin(GL_QUADS);
    glVertex2f(cx - envW * 0.5f, cy - envH * 0.5f);
    glVertex2f(cx + envW * 0.5f, cy - envH * 0.5f);
    glVertex2f(cx + envW * 0.5f, cy + envH * 0.5f);
    glVertex2f(cx - envW * 0.5f, cy + envH * 0.5f);
    glEnd();

    // Envelope Triangles
    glColor3f(0.99f, 0.83f, 0.88f);
    glBegin(GL_TRIANGLES);
    glVertex2f(cx - envW * 0.5f, cy - envH * 0.5f);
    glVertex2f(cx + envW * 0.5f, cy - envH * 0.5f);
    glVertex2f(cx, cy);
    glEnd();

    glColor3f(0.99f, 0.83f, 0.88f);
    glBegin(GL_TRIANGLES);
    glVertex2f(cx - envW * 0.5f, cy - envH * 0.5f);
    glVertex2f(cx, cy);
    glVertex2f(cx - envW * 0.5f, cy + envH * 0.5f);
    glEnd();

    glColor3f(0.99f, 0.83f, 0.88f);
    glBegin(GL_TRIANGLES);
    glVertex2f(cx + envW * 0.5f, cy - envH * 0.5f);
    glVertex2f(cx + envW * 0.5f, cy + envH * 0.5f);
    glVertex2f(cx, cy);
    glEnd();

    // Top flap
    glColor3f(0.92f, 0.55f, 0.70f);
    glBegin(GL_TRIANGLES);
    glVertex2f(cx - envW * 0.5f, cy + envH * 0.5f);
    glVertex2f(cx + envW * 0.5f, cy + envH * 0.5f);
    glVertex2f(cx, cy);
    glEnd();

    glColor3f(0.45f, 0.05f, 0.18f);
    drawText(cx - 55, cy + envH * 0.15f, "To XJTLU", GLUT_BITMAP_TIMES_ROMAN_24);

    glColor3f(0.45f, 0.2f, 0.3f);
    drawText(cx - 120, cy - 100, "[Click] to open the greeting card", GLUT_BITMAP_HELVETICA_18);

    // Opening animation
    float openScale = 1.0f + introProgress * 4.0f;
    if (introProgress < 1.0f && introOpening) {
        glPushMatrix();
        glTranslatef(cx, cy, 0);
        glScalef(openScale, openScale, 1);
        glTranslatef(-cx, -cy, 0);
        glColor4f(1.0f, 0.95f, 0.98f, 0.25f);
        glBegin(GL_QUADS);
        glVertex2f(cx - envW * 0.5f, cy - envH * 0.5f);
        glVertex2f(cx + envW * 0.5f, cy - envH * 0.5f);
        glVertex2f(cx + envW * 0.5f, cy + envH * 0.5f);
        glVertex2f(cx - envW * 0.5f, cy + envH * 0.5f);
        glEnd();
        glPopMatrix();
    }
}

//Draw clouds
void drawCloud(float cx, float cy, float scale) {
    int segments = 24;
    struct Puff { float ox, oy, rad; };
    Puff puffs[5] = { {-45,0,26},{-20,12,30},{10,8,28},{35,0,24},{-5,-8,22} };

    glColor3f(1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 5; ++i) {
        float px = cx + puffs[i].ox * scale;
        float py = cy + puffs[i].oy * scale;
        float rad = puffs[i].rad * scale;
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(px, py);
        for (int s = 0; s <= segments; ++s) {
            float ang = s / (float)segments * 2 * 3.1415926f;
            glVertex2f(px + cosf(ang) * rad, py + sinf(ang) * rad);
        }
        glEnd();
    }

    glColor3f(0.92f, 0.95f, 1.0f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx + 10.0f * scale, cy - 8.0f * scale);
    for (int s = 0; s <= segments; ++s) {
        float ang = s / (float)segments * 2 * 3.1415926f;
        glVertex2f(cx + 10.0f * scale + cosf(ang) * (26.0f * scale),
            cy - 8.0f * scale + sinf(ang) * (14.0f * scale));
    }
    glEnd();
}

//Draw Trees
void drawTree(float baseX, float baseY, float scale) {
    glColor3f(0.45f, 0.25f, 0.10f);
    float trunkW = 18.0f * scale, trunkH = 70.0f * scale;
    glBegin(GL_QUADS);
    glVertex2f(baseX, baseY);
    glVertex2f(baseX + trunkW, baseY);
    glVertex2f(baseX + trunkW, baseY + trunkH);
    glVertex2f(baseX, baseY + trunkH);
    glEnd();

    glColor3f(0.10f, 0.55f, 0.20f);
    int segments = 24;
    float cx = baseX + trunkW * 0.5f;
    struct Blob { float ox, oy, r; } blobs[4] = {
        {-20, trunkH + 30, 28}, {10, trunkH + 42, 26},
        {28, trunkH + 20, 24}, {0, trunkH + 10, 30}
    };

    for (int i = 0; i < 4; ++i) {
        float px = cx + blobs[i].ox * scale;
        float py = baseY + blobs[i].oy * scale;
        float rad = blobs[i].r * scale;
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(px, py);
        for (int s = 0; s <= segments; ++s) {
            float ang = s / (float)segments * 2 * 3.1415926f;
            glVertex2f(px + cosf(ang) * rad, py + sinf(ang) * rad);
        }
        glEnd();
    }

    glColor3f(0.18f, 0.75f, 0.28f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx + 8.0f * scale, baseY + (trunkH + 40.0f) * scale);
    for (int s = 0; s <= segments; ++s) {
        float ang = s / (float)segments * 2 * 3.1415926f;
        glVertex2f(cx + 8.0f * scale + cosf(ang) * (18.0f * scale),
            baseY + (trunkH + 40.0f) * scale + sinf(ang) * (12.0f * scale));
    }
    glEnd();
}

//Draw Sun
void drawSun(float cx, float cy, float radius, float rotation) {
    int segments = 24;
    glColor3f(1.0f, 0.9f, 0.2f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segments; ++i) {
        float angle = i / (float)segments * 2 * 3.1415926f;
        glVertex2f(cx + cosf(angle) * radius, cy + sinf(angle) * radius);
    }
    glEnd();

    glColor3f(1.0f, 0.8f, 0.1f);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    for (int i = 0; i < 8; ++i) {
        float angle = i / 8.0f * 2 * 3.1415926f + rotation * 3.1415926f / 180.0f;
        float rayLength = radius * 1.5f;
        glVertex2f(cx + cosf(angle) * radius, cy + sinf(angle) * radius);
        glVertex2f(cx + cosf(angle) * rayLength, cy + sinf(angle) * rayLength);
    }
    glEnd();
}

//Draw CB
void drawCB(float translationX, float translationY, float w) {
    glPushMatrix();
    glTranslatef(translationX, translationY - 40.0f, 0.0f);
    glScalef(1.2f, 1.2f, 1.0f);

    glLineWidth(w);
    glColor3f(0.6f, 0.5f, 0.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(300, 350);
    glVertex2f(330, 350);
    glVertex2f(340, 330);
    glVertex2f(360, 330);
    glVertex2f(360, 350);
    glVertex2f(500, 350);
    glVertex2f(500, 240);
    glVertex2f(460, 240);
    glVertex2f(460, 300);
    glVertex2f(420, 300);
    glVertex2f(380, 240);
    glVertex2f(300, 240);
    glEnd();

    float glow = 0.5f + 0.5f * sinf(elapsed * 1.5f);
    glColor3f(0.6f + 0.3f * glow, 0.6f + 0.3f * glow, 0.8f);

    glBegin(GL_LINE_STRIP);
    glVertex2f(324, 350);
    glVertex2f(336, 325);
    glVertex2f(365, 325);
    glVertex2f(365, 350);
    glEnd();

    glBegin(GL_LINE_STRIP);
    glVertex2f(300, 240);
    glVertex2f(383, 235);
    glVertex2f(423, 295);
    glVertex2f(455, 295);
    glVertex2f(455, 235);
    glVertex2f(500, 235);
    glEnd();

    glColor3f(0.5f, 0.45f, 0.45f);
    glBegin(GL_LINES);
    glVertex2f(310, 240); glVertex2f(310, 200);
    glVertex2f(490, 240); glVertex2f(490, 200);
    glEnd();

    glColor3f(0.45f, 0.4f, 0.4f);
    glBegin(GL_POLYGON);
    glVertex2f(300, 200); glVertex2f(300, 180);
    glVertex2f(500, 180); glVertex2f(500, 200);
    glEnd();

    glColor3f(0.28f, 0.26f, 0.26f);
    glBegin(GL_POLYGON);
    glVertex2f(300, 180); glVertex2f(300, 165);
    glVertex2f(500, 165); glVertex2f(500, 180);
    glEnd();

    glColor4f(0.0f, 0.0f, 0.0f, 0.15f);
    glBegin(GL_QUADS);
    glVertex2f(290, 165); glVertex2f(510, 165);
    glVertex2f(510, 155); glVertex2f(290, 155);
    glEnd();

    glColor3f(0.3f, 0.3f, 0.3f);
    drawText(415.0f, 335.0f, "Lights And Wings", GLUT_BITMAP_HELVETICA_12);

    glPopMatrix();
}

//Draw Flowers
void drawFlower(float x, float y, float size) {
    glColor3f(0.2f, 0.6f, 0.2f);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    glVertex2f(x, y);
    glVertex2f(x, y - size * 1.5f);
    glEnd();

    glColor3f(0.3f, 0.7f, 0.3f);
    glBegin(GL_TRIANGLES);
    glVertex2f(x - size * 0.3f, y - size * 0.8f);
    glVertex2f(x - size * 0.8f, y - size * 1.2f);
    glVertex2f(x - size * 0.1f, y - size * 1.0f);
    glVertex2f(x + size * 0.3f, y - size * 0.8f);
    glVertex2f(x + size * 0.8f, y - size * 1.2f);
    glVertex2f(x + size * 0.1f, y - size * 1.0f);
    glEnd();

    for (int p = 0; p < 6; ++p) {
        float ang = p * (2.0f * 3.1415926f / 6.0f);
        float px = x + cosf(ang) * size * 0.8f;
        float py = y + sinf(ang) * size * 0.8f;
        glColor3f(0.95f, 0.4f + 0.05f * (p % 2), 0.6f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(px, py);
        for (int s = 0; s <= 10; ++s) {
            float a = s / 10.0f * 2 * 3.1415926f;
            glVertex2f(px + cosf(a) * size * 0.4f, py + sinf(a) * size * 0.4f);
        }
        glEnd();
    }

    glColor3f(0.9f, 0.7f, 0.2f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int s = 0; s <= 12; ++s) {
        float a = s / 12.0f * 2 * 3.1415926f;
        glVertex2f(x + cosf(a) * size * 0.3f, y + sinf(a) * size * 0.3f);
    }
    glEnd();
}

