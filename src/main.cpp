#include <GL/freeglut.h>
#include <windows.h>        // MessageBeep for the alarm buzzer
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

using namespace std;

//====================================================================
//  WINDOW / GLOBAL SETTINGS
//====================================================================

const int   WIN_W = 1320;          // initial window width
const int   WIN_H = 800;           // initial window height

// UI layout (pixels). The house renders LEFT of the dashboard strip,
// so the dashboard can never cover the 3D view.
const float DASH_W    = 350.0f;    // dashboard strip width (right side)
const float TOP_BAR_H = 36.0f;     // top bar height
const float BOT_BAR_H = 26.0f;     // bottom bar height
const float PI    = 3.14159265f;
const float DT    = 0.016f;        // timer step in seconds (~60 FPS)

// House geometry (world units, y is up)
const float WALL_H = 5.0f;         // full wall height
const float WALL_T = 0.2f;         // wall thickness
const float CUT_H  = 1.1f;         // cutaway (front) wall height

// Roof geometry
const float ROOF_RIDGE_Y = 7.3f;
const float ROOF_EAVE_Y  = 5.0f;
const float ROOF_EAVE_Z  = 6.0f;
const float ROOF_X_EXT   = 9.7f;

// Simulation constants
const float DAY_LENGTH    = 150.0f;  // real seconds for one 24h cycle
const float ENERGY_SCALE  = 60.0f;   // 1 real second = 1 simulated minute
const float TARIFF_TK_KWH = 8.0f;    // approx. residential tariff (Taka/kWh)
const int   HIST_N        = 60;      // power history samples (1 / second)

// Room x-ranges:  Living [-9,-3] | Bedroom [-3,3] | Kitchen [3,9]
// All rooms span z in [-5, +5].  Front (z=+5) wall is cut away.

//====================================================================
//  SMART HOME STATE VARIABLES
//====================================================================

// --- Lights ---
bool livingLight  = false;
bool bedroomLight = false;
bool kitchenLight = false;

// --- Fans ---
bool  livingFan  = false;
bool  bedroomFan = false;
int   fanSpeed   = 1;              // 1..3
float livingFanAngle  = 0.0f;
float bedroomFanAngle = 0.0f;

// --- Air conditioner (bedroom) & TV (living room) ---
bool acOn         = false;
bool compressorOn = false;
int  acTarget     = 24;            // Celsius
bool tvOn         = false;

// --- Door & window ---
bool  doorOpen    = false;
float doorAngle   = 0.0f;
bool  windowOpen  = false;
float windowOffset = 0.0f;

// --- Security / safety ---
bool  securityEnabled = false;
bool  alarmActive     = false;
bool  prevThreat      = false;
bool  gasLeak         = false;
int   alarmReason     = 0;         // 0 = door, 1 = gas leak, 2 = window
float alarmPhase      = 0.0f;
bool  soundOn         = true;      // alarm buzzer on/off (U key)
float beepTimer       = 0.0f;      // countdown to the next alarm beep

// --- Automation / time ---
bool  autoMode   = false;          // AUTO: lights by daylight, fans by temp
bool  dayCycle   = true;           // day-night cycle running
float timeOfDay  = 10.0f;          // hours 0..24
float dayLight   = 1.0f;           // 0 = night, 1 = full day (NOT 'daylight' - collides with time.h)
float outsideTemp = 30.0f;

// --- Simulation ---
float temperature      = 28.0f;    // indoor, Celsius
float totalPower       = 0.0f;     // Watt
float energyConsumption = 0.0f;    // kWh
float simTime          = 0.0f;     // real seconds since start
float powerHist[HIST_N];
float histTimer        = 0.0f;

// --- Event log ---
string logLines[5];
int    logCount = 0;

// --- UI ---
bool showHelp = false;
bool roofVisible = true;
float smokePhase = 0.0f;

// --- 3D scene viewport size (refreshed every frame) ---
float sceneW = 0.0f, sceneH = 0.0f;

// --- Orbit camera ---
float camYaw   = 0.50f;
float camPitch = 0.52f;
float camDist  = 22.0f;
const float CAM_YAW0   = 0.50f;
const float CAM_PITCH0 = 0.52f;
const float CAM_DIST0  = 22.0f;
float camTarget[3] = { 0.0f, 2.0f, 0.0f };

float goalYaw = 0.5f, goalPitch = 0.52f, goalDist = 22.0f;
bool  camAnim   = false;           // smooth move to a preset
bool  autoOrbit = false;

bool mouseDragging = false;
int  lastMouseX = 0, lastMouseY = 0;

// --- Stars ---
const int NUM_STARS = 160;
float starPos[NUM_STARS][3];

// --- GLU quadric object (for cylinders / spheres) ---
GLUquadric* gQuad = NULL;

// --- Current window size (refreshed every frame) ---
float winW = (float)WIN_W;
float winH = (float)WIN_H;

//====================================================================
//  SMALL MATH / STRING HELPERS
//====================================================================

float mixf(float a, float b, float t) { return a + (b - a) * t; }

float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// Format seconds as mm:ss
string mmss(float t)
{
    int s = (int)t;
    stringstream ss;
    ss << setfill('0') << setw(2) << (s / 60) << ":"
       << setw(2) << (s % 60);
    return ss.str();
}

// Format hour-of-day as 12-hour clock
string clockStr(float t)
{
    int h = (int)t;
    int m = (int)((t - h) * 60.0f);
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    stringstream ss;
    ss << setfill('0') << setw(2) << h12 << ":" << setw(2) << m
       << (h < 12 ? " AM" : " PM");
    return ss.str();
}

// Add a line to the dashboard event log (newest first)
void logEvent(const string& msg)
{
    for (int i = 4; i > 0; i--) logLines[i] = logLines[i - 1];
    logLines[0] = "[" + mmss(simTime) + "] " + msg;
    if (logCount < 5) logCount++;
    printf("%s\n", logLines[0].c_str());   // mirror to console (out.txt)
    fflush(stdout);
}

float computeDaylight(float t)
{
    float s = sinf(2.0f * PI * (t - 6.0f) / 24.0f);   // noon = +1
    return clamp01(0.5f + 2.5f * s);
}

//====================================================================
//  SMALL 3D DRAWING HELPERS
//====================================================================

void drawBox(float cx, float cy, float cz,
             float w, float h, float d,
             float r, float g, float b)
{
    glColor3f(r, g, b);
    glPushMatrix();
    glTranslatef(cx, cy, cz);
    glScalef(w, h, d);
    glutSolidCube(1.0f);
    glPopMatrix();
}

void drawCylinderY(float x, float yBottom, float z,
                   float rBase, float rTop, float h,
                   float r, float g, float b)
{
    glColor3f(r, g, b);
    glPushMatrix();
    glTranslatef(x, yBottom, z);
    glRotatef(-90.0f, 1, 0, 0);
    gluCylinder(gQuad, rBase, rTop, h, 20, 1);
    glPopMatrix();
}

void drawSphere(float x, float y, float z, float radius,
                float r, float g, float b)
{
    glColor3f(r, g, b);
    glPushMatrix();
    glTranslatef(x, y, z);
    gluSphere(gQuad, radius, 20, 12);
    glPopMatrix();
}

void setEmission(float r, float g, float b)
{
    float e[4] = { r, g, b, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, e);
}

// Soft translucent glow sphere (no lighting, no depth write)
void drawGlow(float x, float y, float z, float radius,
              float r, float g, float b, float a)
{
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glColor4f(r, g, b, a);
    glPushMatrix();
    glTranslatef(x, y, z);
    gluSphere(gQuad, radius, 16, 10);
    glPopMatrix();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void label3D(float x, float y, float z, const string& s,
             float r, float g, float b,
             void* font = GLUT_BITMAP_HELVETICA_12)
{
    glDisable(GL_LIGHTING);
    glColor3f(r, g, b);
    glRasterPos3f(x, y, z);
    for (char c : s)
        glutBitmapCharacter(font, c);
    glEnable(GL_LIGHTING);
}

//====================================================================
//  2D OVERLAY HELPERS  (dashboard / help / alarm)
//====================================================================

void begin2D()
{
    glViewport(0, 0, (int)winW, (int)winH);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, winW, 0, winH);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void end2D()
{
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

void text2D(float x, float y, const string& s,
            float r, float g, float b,
            void* font = GLUT_BITMAP_HELVETICA_12)
{
    glColor3f(r, g, b);
    glRasterPos2f(x, y);
    for (char c : s)
        glutBitmapCharacter(font, c);
}

float textWidth(const string& s, void* font)
{
    return (float)glutBitmapLength(font, (const unsigned char*)s.c_str());
}

void text2DCentered(float cx, float y, const string& s,
                    float r, float g, float b,
                    void* font = GLUT_BITMAP_HELVETICA_12)
{
    text2D(cx - textWidth(s, font) / 2.0f, y, s, r, g, b, font);
}

void panel2D(float x, float y, float w, float h,
             float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

void statusColor(bool on, float& r, float& g, float& b)
{
    if (on) { r = 0.30f; g = 0.95f; b = 0.45f; }
    else    { r = 0.95f; g = 0.35f; b = 0.35f; }
}

//====================================================================
//  SKY COLOUR + LIGHTING SETUP
//====================================================================

void skyColor(float d, float c[3])
{
    float day[3]   = { 0.55f, 0.74f, 0.92f };
    float night[3] = { 0.02f, 0.03f, 0.10f };
    float dusk = 1.0f - fabsf(2.0f * d - 1.0f);        // peaks at sunrise/sunset

    for (int i = 0; i < 3; i++) c[i] = mixf(night[i], day[i], d);
    c[0] += 0.35f * dusk;
    c[1] += 0.12f * dusk;
    c[2] -= 0.10f * dusk;
    for (int i = 0; i < 3; i++) c[i] = clamp01(c[i]);
}

void setupLights()
{
    float d = dayLight;

    // ---- GL_LIGHT0: sun / moon light (changes with time of day) ----
    float amb0[4] = { mixf(0.10f, 0.26f, d), mixf(0.12f, 0.29f, d),
                      mixf(0.20f, 0.37f, d), 1.0f };
    float dif0[4] = { mixf(0.05f, 0.32f, d), mixf(0.07f, 0.34f, d),
                      mixf(0.14f, 0.40f, d), 1.0f };
    float a = 2.0f * PI * (timeOfDay - 6.0f) / 24.0f;
    float pos0[4] = { cosf(a), 0.3f + 0.9f * fmaxf(sinf(a), 0.0f), 0.45f, 0.0f };
    glLightfv(GL_LIGHT0, GL_AMBIENT,  amb0);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  dif0);
    glLightfv(GL_LIGHT0, GL_POSITION, pos0);

    // ---- GL_LIGHT1..3: warm room lamps (point lights) ----
    float warm[4]    = { 1.00f, 0.88f, 0.62f, 1.0f };
    float lampAmb[4] = { 0.30f, 0.26f, 0.18f, 1.0f };
    float lampPos[3][4] =
    {
        { -6.0f, 4.4f, -2.8f, 1.0f },
        {  0.0f, 4.4f, -2.8f, 1.0f },
        {  6.0f, 4.4f, -2.8f, 1.0f }
    };

    for (int i = 0; i < 3; i++)
    {
        GLenum L = GL_LIGHT1 + i;
        glLightfv(L, GL_AMBIENT,  lampAmb);
        glLightfv(L, GL_DIFFUSE,  warm);
        glLightf(L, GL_CONSTANT_ATTENUATION,  0.40f);
        glLightf(L, GL_LINEAR_ATTENUATION,    0.05f);
        glLightf(L, GL_QUADRATIC_ATTENUATION, 0.03f);
        glLightfv(L, GL_POSITION, lampPos[i]);
    }

    if (livingLight)  glEnable(GL_LIGHT1); else glDisable(GL_LIGHT1);
    if (bedroomLight) glEnable(GL_LIGHT2); else glDisable(GL_LIGHT2);
    if (kitchenLight) glEnable(GL_LIGHT3); else glDisable(GL_LIGHT3);

    // ---- GL_LIGHT4: flashing red alarm light ----
    float flash = alarmActive ? (0.5f + 0.5f * sinf(alarmPhase)) : 0.0f;
    float red[4] = { 1.0f * flash, 0.08f * flash, 0.08f * flash, 1.0f };
    float alarmPos[4] = { -6.8f, 2.6f, -3.6f, 1.0f };
    glLightfv(GL_LIGHT4, GL_DIFFUSE,  red);
    glLightfv(GL_LIGHT4, GL_POSITION, alarmPos);
    glLightf(GL_LIGHT4, GL_CONSTANT_ATTENUATION,  0.5f);
    glLightf(GL_LIGHT4, GL_LINEAR_ATTENUATION,    0.1f);
    glLightf(GL_LIGHT4, GL_QUADRATIC_ATTENUATION, 0.05f);
    if (alarmActive) glEnable(GL_LIGHT4); else glDisable(GL_LIGHT4);
}

//====================================================================
//  SKY  (sun, moon, stars, clouds)
//====================================================================

void drawSky()
{
    float a = 2.0f * PI * (timeOfDay - 6.0f) / 24.0f;
    float s = sinf(a);

    glDisable(GL_LIGHTING);

    // ---- Gradient sky dome (horizon pale -> zenith deep) ----
    {
        float hor[3], zen[3];
        skyColor(dayLight, hor);
        for (int i = 0; i < 3; i++) zen[i] = hor[i] * 0.55f;

        const float R = 140.0f;
        const int BANDS = 14, SEGS = 28;
        for (int b = 0; b < BANDS; b++)
        {
            float e0 = -0.40f + 2.00f * b / BANDS;
            float e1 = -0.40f + 2.00f * (b + 1) / BANDS;
            float t0 = clamp01(sinf(e0)), t1 = clamp01(sinf(e1));

            glBegin(GL_QUAD_STRIP);
            for (int k = 0; k <= SEGS; k++)
            {
                float th = 2.0f * PI * k / SEGS;
                glColor3f(mixf(hor[0], zen[0], t0),
                          mixf(hor[1], zen[1], t0),
                          mixf(hor[2], zen[2], t0));
                glVertex3f(R * cosf(e0) * cosf(th), R * sinf(e0),
                           R * cosf(e0) * sinf(th));
                glColor3f(mixf(hor[0], zen[0], t1),
                          mixf(hor[1], zen[1], t1),
                          mixf(hor[2], zen[2], t1));
                glVertex3f(R * cosf(e1) * cosf(th), R * sinf(e1),
                           R * cosf(e1) * sinf(th));
            }
            glEnd();
        }
    }

    // ---- Stars (fade in at night) ----
    float starA = clamp01(1.0f - dayLight * 2.0f);
    if (starA > 0.01f)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPointSize(2.0f);
        glColor4f(1.0f, 1.0f, 0.95f, starA);
        glBegin(GL_POINTS);
        for (int i = 0; i < NUM_STARS; i++)
            glVertex3f(starPos[i][0], starPos[i][1], starPos[i][2]);
        glEnd();
        glDisable(GL_BLEND);
    }

    // ---- Sun ----
    if (s > -0.08f)
    {
        float k = clamp01(s * 2.0f);
        float sx = cosf(a) * 60.0f, sy = s * 55.0f, sz = -35.0f;
        glColor3f(1.0f, 0.75f + 0.20f * k, 0.35f + 0.40f * k);
        glPushMatrix();
        glTranslatef(sx, sy, sz);
        gluSphere(gQuad, 3.5f, 20, 12);
        glPopMatrix();
        drawGlow(sx, sy, sz, 6.0f, 1.0f, 0.85f, 0.45f, 0.15f);
        glDisable(GL_LIGHTING);
    }

    // ---- Moon (opposite side of the sky) ----
    if (-s > -0.08f)
    {
        float mx = -cosf(a) * 60.0f, my = -s * 55.0f, mz = -35.0f;
        glColor3f(0.92f, 0.93f, 0.97f);
        glPushMatrix();
        glTranslatef(mx, my, mz);
        gluSphere(gQuad, 2.6f, 20, 12);
        glPopMatrix();
        drawGlow(mx, my, mz, 4.5f, 0.7f, 0.8f, 1.0f, 0.10f);
        glDisable(GL_LIGHTING);
    }

    // ---- Clouds (slowly drifting) ----
    float br = 0.30f + 0.70f * dayLight;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glColor4f(br, br, br + 0.03f, 0.85f);
    for (int i = 0; i < 5; i++)
    {
        float bx = fmodf(i * 35.0f + smokePhase * 0.8f, 140.0f) - 70.0f;
        float by = 18.0f + (i % 3) * 3.0f;
        float bz = -30.0f - (i % 2) * 20.0f;
        float off[3][4] = { {0, 0, 0, 3.0f}, {2.8f, -0.3f, 0.5f, 2.4f}, {-2.6f, -0.4f, -0.3f, 2.2f} };
        for (int j = 0; j < 3; j++)
        {
            glPushMatrix();
            glTranslatef(bx + off[j][0], by + off[j][1], bz + off[j][2]);
            glScalef(1.0f, 0.45f, 0.8f);
            gluSphere(gQuad, off[j][3], 14, 8);
            glPopMatrix();
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glEnable(GL_LIGHTING);
}

//====================================================================
//  GROUND, TREES, PATH, GARDEN LAMPS, CAR
//====================================================================

// Soft contact shadow on the lawn; call with lighting off + blending on.
// The offset follows the sun/moon azimuth so shadows swing over the day.
void groundShadow(float x, float z, float rx, float rz, float alpha,
                  float y = -0.288f)
{
    float sa   = 2.0f * PI * (timeOfDay - 6.0f) / 24.0f;
    float elev = 0.35f + 0.9f * fmaxf(sinf(sa), 0.0f);
    float len  = fminf(1.5f, 0.8f / elev);      // low sun = longer shadow
    float ox   = cosf(sa) * len * 0.5f;
    float oz   = 0.45f * len * 0.5f;

    glColor4f(0.0f, 0.0f, 0.0f, alpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x + ox, y, z + oz);
    for (int i = 0; i <= 24; i++)
    {
        float t = 2.0f * PI * i / 24.0f;
        glVertex3f(x + ox + rx * cosf(t), y, z + oz + rz * sinf(t));
    }
    glEnd();
}

void drawTree(float x, float z, float s)
{
    drawCylinderY(x, -0.3f, z, 0.18f * s, 0.14f * s, 1.7f * s,
                  0.45f, 0.30f, 0.18f);
    drawSphere(x, 2.2f * s, z, 1.05f * s, 0.22f, 0.55f, 0.25f);
    drawSphere(x + 0.55f * s, 1.75f * s, z + 0.30f * s,
               0.70f * s, 0.25f, 0.60f, 0.28f);
    drawSphere(x - 0.45f * s, 1.85f * s, z - 0.25f * s,
               0.60f * s, 0.20f, 0.50f, 0.23f);
}

void drawGround()
{
    drawBox(0.0f, -0.40f, 0.0f, 150.0f, 0.20f, 150.0f,
            0.36f, 0.55f, 0.32f);

    drawBox(0.0f, -0.27f, 8.4f, 5.0f, 0.06f, 5.4f,
            0.66f, 0.64f, 0.60f);

    drawBox(0.0f, -0.36f, -14.0f, 60.0f, 0.04f, 14.0f,
            0.30f, 0.48f, 0.27f);

    for (int i = -4; i <= 4; i++)
    {
        drawBox(i * 4.0f, -0.34f, -22.0f, 1.8f, 0.03f, 16.0f,
                0.42f, 0.62f, 0.36f);
    }

    drawBox(14.0f, -0.25f, 1.0f, 3.2f, 0.08f, 18.0f, 0.58f, 0.56f, 0.52f);

    for (int i = 0; i < 4; i++)
    {
        drawBox(0.0f, -0.24f, 11.0f + i * 1.3f, 1.6f, 0.05f, 0.8f,
                0.72f, 0.70f, 0.66f);
    }

    for (int i = -9; i <= 9; i++)
    {
        drawBox(i * 1.4f, 0.35f, 13.0f, 0.10f, 0.70f, 0.10f,
                0.95f, 0.95f, 0.93f);
    }
    drawBox(0.0f, 0.62f, 13.0f, 26.0f, 0.08f, 0.08f, 0.95f, 0.95f, 0.93f);
    drawBox(0.0f, 0.25f, 13.0f, 26.0f, 0.08f, 0.08f, 0.95f, 0.95f, 0.93f);

    drawBox(-7.0f, -0.20f, 6.9f, 3.0f, 0.10f, 0.9f, 0.38f, 0.26f, 0.16f);
    drawBox( 7.0f, -0.20f, 6.9f, 3.0f, 0.10f, 0.9f, 0.38f, 0.26f, 0.16f);
    drawSphere(-7.6f, 0.12f, 6.9f, 0.30f, 0.90f, 0.35f, 0.45f);
    drawSphere(-6.4f, 0.12f, 6.9f, 0.28f, 0.95f, 0.40f, 0.60f);
    drawSphere( 6.5f, 0.12f, 6.9f, 0.30f, 0.95f, 0.85f, 0.35f);
    drawSphere( 7.4f, 0.12f, 6.9f, 0.28f, 0.80f, 0.35f, 0.85f);

    drawTree(-13.0f,  2.0f, 1.00f);
    drawTree( 12.5f, -3.0f, 1.20f);
    drawTree(-12.0f, -4.5f, 0.80f);
    drawTree( 13.5f,  3.5f, 0.90f);
    drawTree( -15.0f, -8.0f, 1.10f);
    drawTree(  15.0f, -9.0f, 0.95f);
}

// Garden lamp post: switches on automatically at dusk
void drawStreetLamp(float x, float z)
{
    bool on = dayLight < 0.45f;

    drawCylinderY(x, -0.25f, z, 0.07f, 0.05f, 1.5f, 0.20f, 0.20f, 0.22f);

    if (on) setEmission(1.0f, 0.85f, 0.45f);
    drawSphere(x, 1.42f, z, 0.20f,
               on ? 1.0f : 0.55f, on ? 0.90f : 0.55f, on ? 0.55f : 0.55f);
    if (on) setEmission(0.0f, 0.0f, 0.0f);

    if (on) drawGlow(x, 1.42f, z, 0.75f, 1.0f, 0.85f, 0.40f, 0.14f);
}

void drawWheel(float x, float y, float z)
{
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(90.0f, 0, 1, 0);                 // axle along x
    glTranslatef(0.0f, 0.0f, -0.13f);

    glColor3f(0.08f, 0.08f, 0.09f);
    gluCylinder(gQuad, 0.35f, 0.35f, 0.26f, 18, 1);
    gluDisk(gQuad, 0.0, 0.35, 18, 1);
    glColor3f(0.72f, 0.72f, 0.75f);
    glTranslatef(0.0f, 0.0f, -0.004f);
    gluDisk(gQuad, 0.0, 0.17, 14, 1);
    glTranslatef(0.0f, 0.0f, 0.004f + 0.26f);
    glColor3f(0.08f, 0.08f, 0.09f);
    gluDisk(gQuad, 0.0, 0.35, 18, 1);
    glColor3f(0.72f, 0.72f, 0.75f);
    glTranslatef(0.0f, 0.0f, 0.004f);
    gluDisk(gQuad, 0.0, 0.17, 14, 1);
    glPopMatrix();
}

// Red car parked on the driveway, headlights switch on at night
void drawCar()
{
    float cx = 14.0f, cz = 3.0f;
    bool night = dayLight < 0.45f;

    drawBox(cx, 0.50f, cz, 1.90f, 0.60f, 4.20f, 0.75f, 0.12f, 0.12f);   // body
    drawBox(cx, 1.10f, cz - 0.3f, 1.60f, 0.55f, 2.10f, 0.70f, 0.10f, 0.10f); // cabin
    drawBox(cx, 1.12f, cz - 0.3f, 1.64f, 0.38f, 1.90f, 0.50f, 0.65f, 0.75f); // glass
    drawBox(cx, 0.22f, cz, 1.96f, 0.10f, 4.26f, 0.15f, 0.15f, 0.16f);  // bumper line

    drawWheel(cx - 0.95f, 0.15f, cz - 1.3f);
    drawWheel(cx + 0.95f, 0.15f, cz - 1.3f);
    drawWheel(cx - 0.95f, 0.15f, cz + 1.3f);
    drawWheel(cx + 0.95f, 0.15f, cz + 1.3f);

    if (night) setEmission(1.0f, 0.95f, 0.65f);
    drawSphere(cx - 0.62f, 0.58f, cz + 2.1f, 0.13f,
               night ? 1.0f : 0.85f, night ? 0.95f : 0.85f, night ? 0.70f : 0.80f);
    drawSphere(cx + 0.62f, 0.58f, cz + 2.1f, 0.13f,
               night ? 1.0f : 0.85f, night ? 0.95f : 0.85f, night ? 0.70f : 0.80f);
    if (night) setEmission(0.0f, 0.0f, 0.0f);

    setEmission(night ? 0.5f : 0.0f, 0.0f, 0.0f);
    drawSphere(cx - 0.65f, 0.58f, cz - 2.1f, 0.10f, 0.7f, 0.05f, 0.05f);
    drawSphere(cx + 0.65f, 0.58f, cz - 2.1f, 0.10f, 0.7f, 0.05f, 0.05f);
    setEmission(0.0f, 0.0f, 0.0f);

    if (night)
    {
        drawGlow(cx - 0.62f, 0.58f, cz + 2.4f, 0.55f, 1.0f, 0.95f, 0.65f, 0.15f);
        drawGlow(cx + 0.62f, 0.58f, cz + 2.4f, 0.55f, 1.0f, 0.95f, 0.65f, 0.15f);
    }
}

//====================================================================
//  HOUSE SHELL  (foundation, floors, walls)
//====================================================================

void drawHouseShell()
{
    drawBox(0.0f, -0.15f, 0.0f, 19.2f, 0.30f, 11.0f,
            0.42f, 0.33f, 0.27f);

    drawBox(0.0f, 0.02f, 5.25f, 19.4f, 0.08f, 0.10f, 0.50f, 0.48f, 0.44f);
    drawBox(0.0f, 0.02f, -5.25f, 19.4f, 0.08f, 0.10f, 0.50f, 0.48f, 0.44f);

    drawBox(-6.0f, 0.02f, 0.0f, 5.8f, 0.04f, 9.8f,
            0.62f, 0.45f, 0.30f);          // living room: wood
    drawBox( 0.0f, 0.02f, 0.0f, 5.8f, 0.04f, 9.8f,
            0.70f, 0.62f, 0.58f);          // bedroom: carpet
    drawBox( 6.0f, 0.02f, 0.0f, 5.8f, 0.04f, 9.8f,
            0.80f, 0.80f, 0.82f);          // kitchen: tile

    float wall[3] = { 0.87f, 0.84f, 0.78f };
    float inner[3] = { 0.80f, 0.78f, 0.72f };

    // ---- BACK WALL (z = -5) ----
    drawBox(-8.25f, WALL_H / 2, -5.1f, 1.50f, WALL_H, WALL_T, wall[0], wall[1], wall[2]);
    drawBox(-4.55f, WALL_H / 2, -5.1f, 3.10f, WALL_H, WALL_T, wall[0], wall[1], wall[2]);
    drawBox(-6.80f, 3.80f,      -5.1f, 1.40f, 2.40f,  WALL_T, wall[0], wall[1], wall[2]);

    drawBox(-2.45f, WALL_H / 2, -5.1f, 1.10f, WALL_H, WALL_T, wall[0], wall[1], wall[2]);
    drawBox( 2.45f, WALL_H / 2, -5.1f, 1.10f, WALL_H, WALL_T, wall[0], wall[1], wall[2]);
    drawBox( 0.00f, 0.60f,      -5.1f, 3.80f, 1.20f,  WALL_T, wall[0], wall[1], wall[2]);
    drawBox( 0.00f, 4.20f,      -5.1f, 3.80f, 1.60f,  WALL_T, wall[0], wall[1], wall[2]);

    drawBox(6.0f, WALL_H / 2, -5.1f, 6.0f, WALL_H, WALL_T, wall[0], wall[1], wall[2]);

    // ---- SIDE WALLS ----
    drawBox(-9.1f, WALL_H / 2, 0.0f, WALL_T, WALL_H, 10.4f, wall[0], wall[1], wall[2]);
    drawBox( 9.1f, WALL_H / 2, 0.0f, WALL_T, WALL_H, 10.4f, wall[0], wall[1], wall[2]);

    // ---- INTERIOR WALLS ----
    drawBox(-3.0f, WALL_H / 2, 0.0f, WALL_T, WALL_H, 10.0f, inner[0], inner[1], inner[2]);
    drawBox( 3.0f, WALL_H / 2, 0.0f, WALL_T, WALL_H, 10.0f, inner[0], inner[1], inner[2]);

    // ---- FRONT WALL: CUTAWAY ----
    drawBox(0.0f, CUT_H / 2, 5.1f, 18.4f, CUT_H, WALL_T, wall[0], wall[1], wall[2]);
    drawBox(0.0f, CUT_H + 0.06f, 5.1f, 18.6f, 0.12f, 0.26f,
            0.92f, 0.90f, 0.86f);

    drawBox(0.0f, 0.10f, -4.9f, 18.0f, 0.20f, 0.06f, 0.95f, 0.93f, 0.88f);
    drawBox(-8.9f, 0.10f, 0.0f, 0.06f, 0.20f, 10.0f, 0.95f, 0.93f, 0.88f);
    drawBox( 8.9f, 0.10f, 0.0f, 0.06f, 0.20f, 10.0f, 0.95f, 0.93f, 0.88f);

    drawBox(0.0f, -0.10f, 5.9f, 4.0f, 0.20f, 0.60f, 0.72f, 0.70f, 0.66f);

    drawBox(0.0f, 1.10f, -5.0f, 4.3f, 0.06f, 0.18f, 0.40f, 0.38f, 0.34f);

    drawBox(0.0f, 0.35f, -5.15f, 19.0f, 0.20f, 0.04f, 0.55f, 0.45f, 0.36f);
    drawBox(-9.15f, 0.35f, 0.0f, 0.04f, 0.20f, 10.6f, 0.55f, 0.45f, 0.36f);
    drawBox( 9.15f, 0.35f, 0.0f, 0.04f, 0.20f, 10.6f, 0.55f, 0.45f, 0.36f);
}

//====================================================================
//  GABLE ROOF
//====================================================================

void drawRoof()
{
    float roof[3] = { 0.55f, 0.20f, 0.16f };
    float trim[3] = { 0.92f, 0.90f, 0.86f };

    glColor3f(roof[0], roof[1], roof[2]);
    glBegin(GL_QUADS);
    glVertex3f(-ROOF_X_EXT, ROOF_RIDGE_Y,  0.0f);
    glVertex3f( ROOF_X_EXT, ROOF_RIDGE_Y,  0.0f);
    glVertex3f( ROOF_X_EXT, ROOF_EAVE_Y,  -ROOF_EAVE_Z);
    glVertex3f(-ROOF_X_EXT, ROOF_EAVE_Y,  -ROOF_EAVE_Z);
    glEnd();

    for (int side = 0; side < 2; side++)
    {
        float xc = (side == 0) ? -9.1f : 9.1f;
        float x0 = xc - 0.12f, x1 = xc + 0.12f;
        float zb = -5.1f, zf = 5.1f;
        float yb = ROOF_EAVE_Y, yr = ROOF_RIDGE_Y;

        glColor3f(0.87f, 0.84f, 0.78f);
        glBegin(GL_TRIANGLES);
        glVertex3f(x0, yb, zb); glVertex3f(x0, yb, zf); glVertex3f(x0, yr, 0);
        glVertex3f(x1, yb, zb); glVertex3f(x1, yb, zf); glVertex3f(x1, yr, 0);
        glEnd();

        glColor3f(trim[0], trim[1], trim[2]);
        glBegin(GL_QUADS);
        glVertex3f(x0, yb, zf); glVertex3f(x1, yb, zf);
        glVertex3f(x1, yr, 0.0f); glVertex3f(x0, yr, 0.0f);
        glEnd();
    }

    drawBox(0.0f, ROOF_RIDGE_Y + 0.05f, 0.0f, 2 * ROOF_X_EXT + 0.2f,
            0.14f, 0.55f, 0.40f, 0.14f, 0.12f);

    drawBox(0.0f, ROOF_EAVE_Y - 0.07f, -ROOF_EAVE_Z - 0.02f,
            2 * ROOF_X_EXT + 0.2f, 0.20f, 0.16f, trim[0], trim[1], trim[2]);

    drawBox(5.5f, 6.95f, -2.6f, 0.75f, 2.10f, 0.75f, 0.60f, 0.30f, 0.25f);
    drawBox(5.5f, 8.08f, -2.6f, 0.95f, 0.16f, 0.95f, 0.30f, 0.28f, 0.27f);
    drawBox(5.5f, 8.17f, -2.6f, 0.50f, 0.02f, 0.50f, 0.10f, 0.10f, 0.10f);
}

//====================================================================
//  CHIMNEY SMOKE
//====================================================================

void drawSmoke()
{
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (int i = 0; i < 4; i++)
    {
        float t = fmodf(smokePhase * 0.22f + i * 0.25f, 1.0f);
        float y = 8.25f + t * 2.0f;
        float x = 5.5f + t * 0.8f;
        float r = 0.16f + t * 0.30f;
        float a = 0.30f * (1.0f - t);

        glColor4f(0.85f, 0.85f, 0.87f, a);
        glPushMatrix();
        glTranslatef(x, y, -2.6f);
        gluSphere(gQuad, r, 12, 8);
        glPopMatrix();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

//====================================================================
//  MAIN DOOR
//====================================================================

void drawMainDoor()
{
    drawBox(-7.55f, 1.35f, -4.95f, 0.12f, 2.70f, 0.30f, 0.35f, 0.22f, 0.12f);
    drawBox(-6.05f, 1.35f, -4.95f, 0.12f, 2.70f, 0.30f, 0.35f, 0.22f, 0.12f);
    drawBox(-6.80f, 2.66f, -4.95f, 1.64f, 0.12f, 0.30f, 0.35f, 0.22f, 0.12f);

    glPushMatrix();
    glTranslatef(-7.5f, 0.0f, -4.95f);
    glRotatef(doorAngle, 0, 1, 0);

    drawBox(0.70f, 1.28f, 0.0f, 1.40f, 2.55f, 0.08f,
            0.55f, 0.32f, 0.16f);
    drawBox(0.70f, 1.78f, 0.05f, 1.00f, 0.70f, 0.02f,
            0.48f, 0.27f, 0.13f);
    drawBox(0.70f, 0.72f, 0.05f, 1.00f, 0.70f, 0.02f,
            0.48f, 0.27f, 0.13f);
    drawSphere(1.22f, 1.25f, 0.10f, 0.05f,
               0.90f, 0.75f, 0.20f);
    glPopMatrix();
}

//====================================================================
//  WINDOW
//====================================================================

void drawWindowPanel(float cx)
{
    drawBox(cx, 2.30f, -4.85f, 1.95f, 2.20f, 0.08f,
            0.55f, 0.68f, 0.78f);
    drawBox(cx,        3.36f, -4.85f, 1.95f, 0.10f, 0.10f,
            0.92f, 0.92f, 0.90f);
    drawBox(cx,        1.24f, -4.85f, 1.95f, 0.10f, 0.10f,
            0.92f, 0.92f, 0.90f);
    drawBox(cx - 0.925f, 2.30f, -4.85f, 0.10f, 2.20f, 0.10f,
            0.92f, 0.92f, 0.90f);
    drawBox(cx + 0.925f, 2.30f, -4.85f, 0.10f, 2.20f, 0.10f,
            0.92f, 0.92f, 0.90f);
}

void drawBedroomWindow()
{
    drawBox(-1.95f, 2.30f, -4.95f, 0.12f, 2.20f, 0.30f, 0.90f, 0.90f, 0.88f);
    drawBox( 1.95f, 2.30f, -4.95f, 0.12f, 2.20f, 0.30f, 0.90f, 0.90f, 0.88f);
    drawBox( 0.00f, 1.14f, -4.93f, 4.00f, 0.12f, 0.36f, 0.85f, 0.85f, 0.83f);
    drawBox( 0.00f, 3.46f, -4.95f, 4.00f, 0.12f, 0.30f, 0.90f, 0.90f, 0.88f);

    drawWindowPanel(-0.925f - windowOffset);
    drawWindowPanel( 0.925f + windowOffset);
}

//====================================================================
//  CEILING LAMPS
//====================================================================

void drawLamp(float x, float z, bool on)
{
    // Ceiling rose
    drawCylinderY(x, WALL_H - 0.08f, z, 0.16f, 0.13f, 0.08f,
                  0.92f, 0.90f, 0.86f);
    // Suspension cord
    drawCylinderY(x, WALL_H - 0.46f, z, 0.025f, 0.025f, 0.38f,
                  0.28f, 0.28f, 0.30f);
    // Conical shade, open at the bottom
    drawCylinderY(x, WALL_H - 0.78f, z, 0.36f, 0.075f, 0.32f,
                  0.93f, 0.90f, 0.84f);
    // Close the narrow top of the shade
    glColor3f(0.93f, 0.90f, 0.84f);
    glPushMatrix();
    glTranslatef(x, WALL_H - 0.46f, z);
    glRotatef(-90.0f, 1, 0, 0);
    gluDisk(gQuad, 0.0, 0.075, 20, 1);
    glPopMatrix();

    // Bulb peeking out under the shade
    float by = WALL_H - 0.84f;
    if (on) setEmission(1.0f, 0.85f, 0.45f);
    drawSphere(x, by, z, 0.11f,
               on ? 1.00f : 0.74f,
               on ? 0.92f : 0.74f,
               on ? 0.62f : 0.76f);
    if (on) setEmission(0.0f, 0.0f, 0.0f);

    if (on) drawGlow(x, by, z, 0.32f, 1.0f, 0.85f, 0.40f, 0.20f);
}

//====================================================================
//  CEILING FANS
//====================================================================

void drawCeilingFan(float x, float z, float angle)
{
    // Ceiling canopy (flares up to the ceiling)
    drawCylinderY(x, WALL_H - 0.11f, z, 0.10f, 0.18f, 0.11f,
                  0.78f, 0.79f, 0.82f);
    // Downrod
    drawCylinderY(x, WALL_H - 0.52f, z, 0.04f, 0.04f, 0.41f,
                  0.70f, 0.71f, 0.74f);

    // Motor housing: wide flat drum instead of a dark ball
    float mBot = WALL_H - 0.74f;
    drawCylinderY(x, mBot, z, 0.30f, 0.30f, 0.22f,
                  0.73f, 0.74f, 0.77f);
    glColor3f(0.73f, 0.74f, 0.77f);
    glPushMatrix();                          // bottom cap
    glTranslatef(x, mBot, z);
    glRotatef(90.0f, 1, 0, 0);
    gluDisk(gQuad, 0.0, 0.30, 20, 1);
    glPopMatrix();
    glPushMatrix();                          // top cap
    glTranslatef(x, mBot + 0.22f, z);
    glRotatef(-90.0f, 1, 0, 0);
    gluDisk(gQuad, 0.0, 0.30, 20, 1);
    glPopMatrix();

    // Flared pedestal + ornamental knob under the motor
    drawCylinderY(x, WALL_H - 0.81f, z, 0.14f, 0.30f, 0.07f,
                  0.68f, 0.69f, 0.72f);
    drawSphere(x, WALL_H - 0.86f, z, 0.085f, 0.85f, 0.85f, 0.88f);

    // Four wooden blades with a slight aerodynamic pitch
    glPushMatrix();
    glTranslatef(x, WALL_H - 0.63f, z);
    glRotatef(angle, 0, 1, 0);

    for (int i = 0; i < 4; i++)
    {
        glPushMatrix();
        glRotatef(i * 90.0f, 0, 1, 0);
        drawBox(0.40f, 0.0f, 0.0f, 0.36f, 0.045f, 0.11f,
                0.55f, 0.56f, 0.59f);           // blade iron
        glPushMatrix();
        glTranslatef(1.14f, 0.0f, 0.0f);
        glRotatef(6.0f, 1, 0, 0);              // blade pitch
        drawBox(0.0f, 0.0f, 0.0f, 1.56f, 0.04f, 0.42f,
                0.58f, 0.41f, 0.23f);           // wooden blade
        glPopMatrix();
        glPopMatrix();
    }
    glPopMatrix();
}

//====================================================================
//  LIGHT CONES
//====================================================================

void drawLightCones()
{
    if (!livingLight && !bedroomLight && !kitchenLight)
        return;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    float xs[3]  = { -6.0f, 0.0f, 6.0f };
    bool  ons[3] = { livingLight, bedroomLight, kitchenLight };

    for (int i = 0; i < 3; i++)
    {
        if (!ons[i]) continue;

        glColor4f(1.0f, 0.88f, 0.55f, 0.07f);
        glPushMatrix();
        glTranslatef(xs[i], WALL_H - 0.70f, -2.8f);
        glRotatef(90.0f, 1, 0, 0);
        gluCylinder(gQuad, 0.35f, 2.60f, 4.20f, 24, 1);
        glPopMatrix();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

//====================================================================
//  FURNITURE  -  LIVING ROOM
//====================================================================

void drawSofa()
{
    float X = -8.35f, Z = 0.0f;
    drawBox(X,        0.28f, Z,        0.95f, 0.55f, 3.20f,
            0.22f, 0.40f, 0.62f);
    drawBox(X - 0.38f, 0.85f, Z,       0.22f, 1.05f, 3.20f,
            0.16f, 0.32f, 0.52f);
    drawBox(X,        0.72f, Z - 1.55f, 0.95f, 0.62f, 0.24f,
            0.18f, 0.35f, 0.56f);
    drawBox(X,        0.72f, Z + 1.55f, 0.95f, 0.62f, 0.24f,
            0.18f, 0.35f, 0.56f);
    drawBox(X + 0.05f, 0.60f, Z - 0.75f, 0.75f, 0.14f, 1.35f,
            0.30f, 0.52f, 0.74f);
    drawBox(X + 0.05f, 0.60f, Z + 0.75f, 0.75f, 0.14f, 1.35f,
            0.30f, 0.52f, 0.74f);
}

void drawCoffeeTable()
{
    float X = -6.70f, Z = 0.0f;
    drawBox(X, 0.42f, Z, 1.10f, 0.07f, 1.70f,
            0.42f, 0.26f, 0.15f);
    drawBox(X - 0.45f, 0.19f, Z - 0.75f, 0.08f, 0.38f, 0.08f,
            0.35f, 0.22f, 0.13f);
    drawBox(X + 0.45f, 0.19f, Z - 0.75f, 0.08f, 0.38f, 0.08f,
            0.35f, 0.22f, 0.13f);
    drawBox(X - 0.45f, 0.19f, Z + 0.75f, 0.08f, 0.38f, 0.08f,
            0.35f, 0.22f, 0.13f);
    drawBox(X + 0.45f, 0.19f, Z + 0.75f, 0.08f, 0.38f, 0.08f,
            0.35f, 0.22f, 0.13f);
}

void drawTV()
{
    float X = -3.45f, Z = 0.0f;
    drawBox(X, 0.25f, Z, 0.50f, 0.50f, 1.80f,
            0.25f, 0.25f, 0.28f);
    drawBox(X, 1.05f, Z, 0.12f, 0.90f, 0.50f,
            0.20f, 0.20f, 0.22f);
    drawBox(X, 1.90f, Z, 0.10f, 1.15f, 2.20f,
            0.12f, 0.12f, 0.14f);

    if (tvOn)   // animated glowing screen
    {
        float f = 0.5f + 0.5f * sinf(simTime * 1.7f);
        float r = 0.15f + 0.25f * f;
        float g = 0.30f + 0.25f * (1.0f - f);
        float b = 0.55f + 0.15f * sinf(simTime * 0.9f);
        setEmission(r, g, b);
        drawBox(X - 0.06f, 1.90f, Z, 0.02f, 1.00f, 2.05f, r, g, b);
        setEmission(0.0f, 0.0f, 0.0f);
    }
    else        // dark screen
    {
        drawBox(X - 0.06f, 1.90f, Z, 0.02f, 1.00f, 2.05f,
                0.04f, 0.04f, 0.05f);
    }
}

void drawLivingRoom()
{
    drawBox(-6.70f, 0.03f, 0.0f, 2.80f, 0.04f, 3.80f,
            0.70f, 0.30f, 0.30f);
    drawSofa();
    drawCoffeeTable();
    drawTV();
}

//====================================================================
//  FURNITURE  -  BEDROOM  (+ AIR CONDITIONER)
//====================================================================

void drawBed()
{
    float CX = -0.2f;
    drawBox(CX,        0.20f, -3.05f, 1.95f, 0.40f, 2.90f,
            0.35f, 0.24f, 0.16f);
    drawBox(CX,        0.50f, -3.05f, 1.85f, 0.22f, 2.80f,
            0.92f, 0.93f, 0.95f);
    drawBox(CX,        0.62f, -2.45f, 1.87f, 0.10f, 1.60f,
            0.20f, 0.55f, 0.60f);
    drawBox(CX - 0.45f, 0.68f, -4.15f, 0.75f, 0.16f, 0.45f,
            0.98f, 0.98f, 0.95f);
    drawBox(CX + 0.45f, 0.68f, -4.15f, 0.75f, 0.16f, 0.45f,
            0.98f, 0.98f, 0.95f);
    drawBox(CX,        0.62f, -4.62f, 1.95f, 1.15f, 0.14f,
            0.40f, 0.28f, 0.18f);
}

void drawBedroom()
{
    drawBed();

    drawBox(2.62f, 1.10f, -2.60f, 0.62f, 2.20f, 1.70f,
            0.48f, 0.33f, 0.20f);
    drawSphere(2.28f, 1.15f, -2.25f, 0.04f, 0.90f, 0.80f, 0.30f);
    drawSphere(2.28f, 1.15f, -2.95f, 0.04f, 0.90f, 0.80f, 0.30f);

    drawBox(1.05f, 0.26f, -4.35f, 0.55f, 0.52f, 0.55f,
            0.42f, 0.28f, 0.16f);
    drawCylinderY(1.05f, 0.52f, -4.35f, 0.03f, 0.03f, 0.25f,
                  0.20f, 0.20f, 0.20f);
    if (bedroomLight) setEmission(1.0f, 0.80f, 0.40f);
    drawSphere(1.05f, 0.85f, -4.35f, 0.12f,
               bedroomLight ? 1.0f : 0.6f,
               bedroomLight ? 0.9f : 0.6f,
               bedroomLight ? 0.6f : 0.6f);
    if (bedroomLight) setEmission(0.0f, 0.0f, 0.0f);
}

// Wall-mounted split AC on the bedroom's left wall
void drawAC()
{
    drawBox(-2.72f, 3.90f, 1.50f, 0.34f, 0.50f, 1.70f, 0.93f, 0.94f, 0.96f);
    drawBox(-2.54f, 3.74f, 1.50f, 0.04f, 0.08f, 1.45f, 0.70f, 0.72f, 0.76f);

    if (acOn)
    {
        if (compressorOn)
        {
            setEmission(0.2f, 0.6f, 1.0f);
            drawSphere(-2.53f, 4.05f, 0.85f, 0.045f, 0.3f, 0.7f, 1.0f);
        }
        else
        {
            setEmission(0.2f, 0.9f, 0.3f);
            drawSphere(-2.53f, 4.05f, 0.85f, 0.045f, 0.3f, 0.95f, 0.4f);
        }
        setEmission(0.0f, 0.0f, 0.0f);
    }
    else
    {
        drawSphere(-2.53f, 4.05f, 0.85f, 0.045f, 0.4f, 0.4f, 0.4f);
    }
}

// Cool air blowing out of the AC while the compressor runs
void drawCoolAir()
{
    if (!(acOn && compressorOn)) return;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (int i = 0; i < 9; i++)
    {
        float t = fmodf(smokePhase * 0.55f + i * 0.111f, 1.0f);
        float x = -2.45f + t * 1.7f;
        float y = 3.65f - t * 1.5f;
        float z = 1.5f + ((i % 3) - 1) * 0.55f;
        float r = 0.07f + 0.12f * t;

        glColor4f(0.55f, 0.82f, 1.0f, 0.40f * (1.0f - t));
        glPushMatrix();
        glTranslatef(x, y, z);
        gluSphere(gQuad, r, 10, 6);
        glPopMatrix();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

//====================================================================
//  FURNITURE  -  KITCHEN  (+ GAS DETECTOR)
//====================================================================

void drawKitchen()
{
    drawBox(6.00f, 0.45f, -4.35f, 5.40f, 0.90f, 0.70f,
            0.50f, 0.34f, 0.22f);
    drawBox(6.00f, 0.94f, -4.33f, 5.50f, 0.07f, 0.76f,
            0.75f, 0.75f, 0.78f);
    for (int i = 0; i < 4; i++)
        drawBox(3.70f + i * 1.35f, 0.45f, -3.99f,
                0.03f, 0.75f, 0.02f, 0.30f, 0.20f, 0.12f);
    drawBox(4.90f, 2.55f, -4.72f, 3.20f, 0.85f, 0.40f,
            0.55f, 0.38f, 0.25f);

    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            drawCylinderY(4.55f + i * 0.50f, 0.975f, -4.45f + j * 0.40f,
                          0.14f, 0.14f, 0.02f, 0.12f, 0.12f, 0.13f);

    drawBox(7.60f, 0.965f, -4.35f, 0.70f, 0.05f, 0.50f,
            0.25f, 0.27f, 0.30f);
    drawCylinderY(7.60f, 0.99f, -4.55f, 0.03f, 0.03f, 0.30f,
                  0.70f, 0.70f, 0.75f);

    drawBox(8.50f, 1.05f, -1.60f, 0.85f, 2.10f, 0.95f,
            0.82f, 0.85f, 0.88f);
    drawBox(8.50f, 1.42f, -1.60f, 0.87f, 0.03f, 0.97f,
            0.45f, 0.47f, 0.50f);
    drawBox(8.04f, 1.70f, -1.35f, 0.04f, 0.50f, 0.05f,
            0.40f, 0.42f, 0.45f);
    drawBox(8.04f, 0.90f, -1.35f, 0.04f, 0.50f, 0.05f,
            0.40f, 0.42f, 0.45f);

    drawBox(5.60f, 0.76f, 1.60f, 1.40f, 0.08f, 1.00f,
            0.50f, 0.34f, 0.20f);
    drawBox(5.00f, 0.36f, 1.25f, 0.08f, 0.72f, 0.08f,
            0.42f, 0.28f, 0.16f);
    drawBox(6.20f, 0.36f, 1.25f, 0.08f, 0.72f, 0.08f,
            0.42f, 0.28f, 0.16f);
    drawBox(5.00f, 0.36f, 1.95f, 0.08f, 0.72f, 0.08f,
            0.42f, 0.28f, 0.16f);
    drawBox(6.20f, 0.36f, 1.95f, 0.08f, 0.72f, 0.08f,
            0.42f, 0.28f, 0.16f);
    drawCylinderY(4.60f, 0.0f, 1.60f, 0.22f, 0.22f, 0.55f,
                  0.40f, 0.28f, 0.18f);
    drawCylinderY(6.60f, 0.0f, 1.60f, 0.22f, 0.22f, 0.55f,
                  0.40f, 0.28f, 0.18f);
}

// Gas / smoke detector on the kitchen back wall
void drawDetector()
{
    drawBox(7.90f, 3.90f, -4.85f, 0.35f, 0.10f, 0.35f, 0.92f, 0.92f, 0.94f);

    bool blink = gasLeak && (sinf(alarmPhase * 1.5f) > 0.0f);
    if (gasLeak)
    {
        if (blink) setEmission(1.0f, 0.1f, 0.1f);
        drawSphere(7.90f, 3.90f, -4.65f, 0.05f, blink ? 1.0f : 0.4f, 0.1f, 0.1f);
        if (blink) setEmission(0.0f, 0.0f, 0.0f);
    }
    else
    {
        setEmission(0.1f, 0.6f, 0.2f);
        drawSphere(7.90f, 3.90f, -4.65f, 0.05f, 0.2f, 0.9f, 0.3f);
        setEmission(0.0f, 0.0f, 0.0f);
    }
}

// Greenish gas cloud rising from the cooktop
void drawGas()
{
    if (!gasLeak) return;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (int i = 0; i < 8; i++)
    {
        float t = fmodf(smokePhase * 0.35f + i * 0.125f, 1.0f);
        float x = 4.8f + 0.7f * sinf(i * 1.7f + t * 4.0f);
        float y = 1.1f + t * 3.0f;
        float z = -4.2f + 0.5f * cosf(i * 1.3f + t * 3.0f) + t * 1.2f;
        float r = 0.18f + 0.38f * t;

        glColor4f(0.75f, 0.92f, 0.20f, 0.30f * (1.0f - t));
        glPushMatrix();
        glTranslatef(x, y, z);
        gluSphere(gQuad, r, 12, 8);
        glPopMatrix();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

//====================================================================
//  FLOATING ROOM LABELS
//====================================================================

void drawLabels()
{
    label3D(-6.0f, 4.35f, -4.85f, "LIVING ROOM",
            0.10f, 0.15f, 0.25f, GLUT_BITMAP_HELVETICA_18);
    label3D( 0.0f, 4.35f, -4.85f, "BEDROOM",
            0.10f, 0.15f, 0.25f, GLUT_BITMAP_HELVETICA_18);
    label3D( 6.0f, 4.35f, -4.85f, "KITCHEN",
            0.10f, 0.15f, 0.25f, GLUT_BITMAP_HELVETICA_18);

    label3D(-6.8f, 3.00f, -4.60f, "MAIN DOOR", 0.15f, 0.15f, 0.20f);
    label3D( 0.0f, 3.80f, -4.60f, "WINDOW",    0.15f, 0.15f, 0.20f);

    // AC status tag
    stringstream ac;
    if (acOn) ac << "AC " << acTarget << " C";
    else      ac << "AC OFF";
    label3D(-2.45f, 4.40f, 1.0f, ac.str(),
            acOn ? 0.10f : 0.40f, acOn ? 0.35f : 0.40f, acOn ? 0.70f : 0.40f);

    label3D(7.3f, 4.20f, -4.60f, "GAS SENSOR", 0.15f, 0.15f, 0.20f);
}

//====================================================================
//  DASHBOARD  (2D overlay, right side)
//====================================================================

const float ROW_H = 17.0f;
const float SEC_H = 22.0f;

void dashSection(float x, float& y, const string& title)
{
    text2D(x, y, title, 1.0f, 0.78f, 0.25f, GLUT_BITMAP_HELVETICA_18);
    y -= SEC_H;
}

void dashRow(float x, float xv, float& y, const string& k,
             const string& v, float r, float g, float b)
{
    text2D(x,  y, k, 0.85f, 0.88f, 0.92f, GLUT_BITMAP_HELVETICA_12);
    text2D(xv, y, v, r, g, b, GLUT_BITMAP_HELVETICA_12);
    y -= ROW_H;
}

void drawDashboard()
{
    float px = winW - DASH_W;
    float pw = DASH_W;
    float py = BOT_BAR_H;
    float ph = winH - TOP_BAR_H - BOT_BAR_H;

    panel2D(px, py, pw, ph, 0.05f, 0.08f, 0.14f, 0.88f);

    glColor4f(0.30f, 0.55f, 0.80f, 0.9f);
    glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(px, py);
    glVertex2f(px + pw, py);
    glVertex2f(px + pw, py + ph);
    glVertex2f(px, py + ph);
    glEnd();

    float x  = px + 18.0f;
    float xv = px + 165.0f;
    float y  = py + ph - 30.0f;
    float r, g, b;

    text2D(x, y, "CONTROL DASHBOARD",
           0.30f, 0.85f, 1.0f, GLUT_BITMAP_HELVETICA_18);
    y -= 12.0f;
    glColor4f(0.30f, 0.50f, 0.70f, 0.8f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    glVertex2f(x, y);
    glVertex2f(px + pw - 18.0f, y);
    glEnd();
    y -= 22.0f;

    // ---- LIGHTING ----
    dashSection(x, y, "LIGHTING");
    statusColor(livingLight,  r, g, b);
    dashRow(x, xv, y, "Living Room", livingLight ? "ON" : "OFF", r, g, b);
    statusColor(bedroomLight, r, g, b);
    dashRow(x, xv, y, "Bedroom", bedroomLight ? "ON" : "OFF", r, g, b);
    statusColor(kitchenLight, r, g, b);
    dashRow(x, xv, y, "Kitchen", kitchenLight ? "ON" : "OFF", r, g, b);
    y -= 4.0f;

    // ---- APPLIANCES ----
    dashSection(x, y, "APPLIANCES");
    statusColor(livingFan, r, g, b);
    dashRow(x, xv, y, "Living Fan", livingFan ? "ON" : "OFF", r, g, b);
    statusColor(bedroomFan, r, g, b);
    dashRow(x, xv, y, "Bedroom Fan", bedroomFan ? "ON" : "OFF", r, g, b);
    {
        stringstream sp;
        sp << fanSpeed << " / 3";
        dashRow(x, xv, y, "Fan Speed", sp.str(), 1, 1, 1);
    }
    if (!acOn)            dashRow(x, xv, y, "Air Conditioner", "OFF", 0.95f, 0.35f, 0.35f);
    else if (compressorOn) dashRow(x, xv, y, "Air Conditioner", "COOLING", 0.45f, 0.80f, 1.0f);
    else                  dashRow(x, xv, y, "Air Conditioner", "STANDBY", 0.30f, 0.95f, 0.45f);
    statusColor(tvOn, r, g, b);
    dashRow(x, xv, y, "Television", tvOn ? "ON" : "OFF", r, g, b);
    y -= 4.0f;

    // ---- ACCESS & SECURITY ----
    dashSection(x, y, "ACCESS & SECURITY");
    dashRow(x, xv, y, "Main Door", doorOpen ? "OPEN" : "CLOSED",
            doorOpen ? 1.0f : 0.30f,
            doorOpen ? 0.70f : 0.90f,
            doorOpen ? 0.20f : 0.40f);
    dashRow(x, xv, y, "Window", windowOpen ? "OPEN" : "CLOSED",
            windowOpen ? 1.0f : 0.30f,
            windowOpen ? 0.70f : 0.90f,
            windowOpen ? 0.20f : 0.40f);
    dashRow(x, xv, y, "Security",
            securityEnabled ? "ARMED" : "DISABLED",
            securityEnabled ? 0.30f : 0.75f,
            securityEnabled ? 0.95f : 0.75f,
            securityEnabled ? 0.45f : 0.75f);
    if (alarmActive)
    {
        float f = 0.5f + 0.5f * sinf(alarmPhase);
        dashRow(x, xv, y, "Alarm", gasLeak ? "GAS LEAK!" : "ACTIVE!",
                1.0f, 0.2f + 0.6f * f, 0.2f);
    }
    else
    {
        dashRow(x, xv, y, "Alarm", "NORMAL", 0.30f, 0.90f, 0.45f);
    }
    statusColor(soundOn, r, g, b);
    dashRow(x, xv, y, "Buzzer", soundOn ? "ON" : "OFF", r, g, b);
    y -= 4.0f;

    // ---- CLIMATE ----
    dashSection(x, y, "CLIMATE");
    {
        stringstream tt;
        tt << fixed << setprecision(1) << temperature << " C";
        float tr = 1, tg = 1, tb = 1;
        if (temperature > 32.0f)      { tr = 1.0f; tg = 0.50f; tb = 0.20f; }
        else if (temperature < 25.0f) { tr = 0.40f; tg = 0.80f; tb = 1.00f; }
        dashRow(x, xv, y, "Indoor Temp", tt.str(), tr, tg, tb);

        stringstream ot;
        ot << fixed << setprecision(1) << outsideTemp << " C";
        dashRow(x, xv, y, "Outdoor Temp", ot.str(), 0.70f, 0.80f, 0.90f);

        stringstream at;
        at << acTarget << " C";
        dashRow(x, xv, y, "AC Target", at.str(),
                acOn ? 0.45f : 0.55f, acOn ? 0.80f : 0.55f, acOn ? 1.0f : 0.55f);
    }
    y -= 4.0f;

    // ---- ENERGY ----
    dashSection(x, y, "ENERGY");
    {
        stringstream pw2;
        pw2 << fixed << setprecision(0) << totalPower << " W";
        dashRow(x, xv, y, "Total Power", pw2.str(), 1, 1, 1);

        stringstream ee;
        ee << fixed << setprecision(3) << energyConsumption << " kWh";
        dashRow(x, xv, y, "Consumption", ee.str(), 1, 1, 1);

        stringstream cc;
        cc << fixed << setprecision(2) << energyConsumption * TARIFF_TK_KWH << " Tk";
        dashRow(x, xv, y, "Est. Cost", cc.str(), 1.0f, 0.85f, 0.40f);

        dashRow(x, xv, y, "Uptime", mmss(simTime), 0.70f, 0.80f, 0.90f);
    }
    y -= 4.0f;

    // ---- POWER HISTORY GRAPH ----
    dashSection(x, y, "POWER (last 60 s)");
    {
        float gw = pw - 36.0f, gh = 38.0f;
        float gy = y - gh + 8.0f;
        panel2D(x, gy, gw, gh, 0.10f, 0.15f, 0.22f, 0.9f);

        float maxv = 200.0f;
        for (int i = 0; i < HIST_N; i++)
            if (powerHist[i] > maxv) maxv = powerHist[i];

        glColor3f(0.30f, 0.90f, 0.50f);
        glLineWidth(1.5f);
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i < HIST_N; i++)
        {
            float xx = x + i * (gw / (HIST_N - 1));
            float yy = gy + 2.0f + (powerHist[i] / maxv) * (gh - 4.0f);
            glVertex2f(xx, yy);
        }
        glEnd();
        glLineWidth(1.0f);
        y = gy - 14.0f;
    }

    // ---- EVENT LOG ----
    dashSection(x, y, "EVENT LOG");
    for (int i = 0; i < logCount; i++)
    {
        float fade = 1.0f - 0.12f * i;
        text2D(x, y, logLines[i], 0.70f * fade, 0.85f * fade, 1.0f * fade);
        y -= 15.0f;
    }
}

//====================================================================
//  TOP BAR / BOTTOM BAR / ALARM / HELP OVERLAYS
//====================================================================

void drawTopBar()
{
    panel2D(0.0f, winH - TOP_BAR_H, winW, TOP_BAR_H, 0.05f, 0.08f, 0.14f, 0.90f);
    text2D(16.0f, winH - 27.0f, "SMART HOME 3D  -  CGM PROJECT",
           0.30f, 0.85f, 1.0f, GLUT_BITMAP_HELVETICA_18);

    // Clock, day/night, mode (centered over the 3D view)
    string phase = (dayLight > 0.6f) ? "DAY" : (dayLight < 0.25f ? "NIGHT" : "DAWN/DUSK");
    string mid = clockStr(timeOfDay) + "  " + phase + (dayCycle ? "" : " (paused)")
               + "   |   MODE: " + (autoMode ? "AUTO" : "MANUAL");
    text2DCentered(sceneW / 2.0f + 40.0f, winH - 25.0f, mid,
                   1.0f, 0.85f, 0.40f, GLUT_BITMAP_HELVETICA_18);

    string hint = "drag = rotate, wheel = zoom";
    text2D(winW - textWidth(hint, GLUT_BITMAP_HELVETICA_12) - 16.0f,
           winH - 25.0f, hint, 0.65f, 0.75f, 0.90f);
}

void drawBottomBar()
{
    panel2D(0.0f, 0.0f, winW, BOT_BAR_H, 0.05f, 0.08f, 0.14f, 0.85f);
    text2D(16.0f, 8.0f,
           "1/2/3 lights  F/B fans  E AC  V TV  D door  W window  S security  "
           "G gas test  M auto  N time  7/8/9/0 view  O orbit  U sound  H help  R reset",
           0.75f, 0.80f, 0.90f);
}

void drawAlarmOverlay()
{
    float f = 0.5f + 0.5f * sinf(alarmPhase);

    panel2D(0.0f, 0.0f, winW, winH, 0.90f, 0.05f, 0.05f, 0.08f + 0.10f * f);

    string msg;
    if (alarmReason == 1)
        msg = ">>>  GAS LEAK DETECTED - VENTILATING  <<<";
    else if (alarmReason == 2)
        msg = ">>>  ALARM!  INTRUSION - WINDOW OPENED  <<<";
    else
        msg = ">>>  ALARM!  INTRUSION - MAIN DOOR OPEN  <<<";

    text2DCentered(sceneW / 2.0f, winH - 78.0f, msg,
                   1.0f, 0.25f + 0.75f * f, 0.25f,
                   GLUT_BITMAP_HELVETICA_18);
}

void drawHelpOverlay()
{
    // {key, description}. Empty desc + key => heading, both empty => spacer
    const char* help[][2] =
    {
        { "1 / 2 / 3",   "Living / Bedroom / Kitchen light" },
        { "F / B",       "Living / Bedroom fan ON-OFF" },
        { "+ / -",       "Fan speed (1-3)" },
        { "E",           "Air conditioner ON / OFF" },
        { "[ / ]",       "AC target temperature" },
        { "V",           "Television ON / OFF" },
        { "D",           "Main door open / close" },
        { "W",           "Window open / close" },
        { "S",           "Security arm / disarm" },
        { "A",           "Reset alarm / clear gas leak" },
        { "U",           "Alarm buzzer on / off" },
        { "G",           "Simulate gas leak (safety test)" },
        { "M",           "AUTO mode (night lights, fans)" },
        { "N",           "Pause / resume day-night cycle" },
        { ", / .",       "Time -1 hour / +1 hour" },
        { "T / Y",       "Room temperature up / down" },
        { "C",           "Roof show / hide" },
        { "R",           "Reset system" },
        { "H",           "Show / hide this help" },
        { "ESC",         "Exit" },
        { "",            "" },
        { "CAMERA",      "" },
        { "ARROWS/DRAG", "Rotate camera" },
        { "WHEEL / PgUp/PgDn", "Zoom in / out" },
        { "7 / 8 / 9 / 0", "Front / Top / Side / Default view" },
        { "O",           "Auto-orbit ON / OFF" }
    };
    int n = (int)(sizeof(help) / sizeof(help[0]));

    float w = 560.0f, h = 100.0f + n * 22.0f;
    float x = (sceneW - w) / 2.0f + 10.0f;
    float y = (winH - h) / 2.0f;

    panel2D(x, y, w, h, 0.05f, 0.08f, 0.14f, 0.93f);
    glColor4f(0.30f, 0.55f, 0.80f, 0.9f);
    glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();

    float tx = x + 30.0f;
    float ty = y + h - 40.0f;

    text2D(tx, ty, "KEYBOARD CONTROLS",
           0.30f, 0.85f, 1.0f, GLUT_BITMAP_HELVETICA_18);
    ty -= 34.0f;

    for (int i = 0; i < n; i++)
    {
        if (help[i][0][0] != '\0' && help[i][1][0] == '\0')
            text2D(tx, ty, help[i][0], 0.30f, 0.85f, 1.0f);
        else if (help[i][0][0] != '\0')
        {
            text2D(tx, ty, help[i][0], 1.0f, 0.78f, 0.25f);
            text2D(tx + 150.0f, ty, help[i][1], 0.85f, 0.88f, 0.92f);
        }
        ty -= 22.0f;
    }

    text2D(tx, y + 14.0f, "Press H to return", 0.65f, 0.75f, 0.90f);
}

//====================================================================
//  SIMULATION  -  POWER / ENERGY
//====================================================================

void calculatePower()
{
    totalPower = 80.0f;                         // refrigerator (always on)

    if (livingLight)  totalPower += 20.0f;
    if (bedroomLight) totalPower += 15.0f;
    if (kitchenLight) totalPower += 15.0f;

    if (livingFan)  totalPower += 60.0f * fanSpeed;
    if (bedroomFan) totalPower += 60.0f * fanSpeed;

    if (acOn) totalPower += compressorOn ? 900.0f : 40.0f;
    if (tvOn) totalPower += 100.0f;
}

//====================================================================
//  CAMERA HELPERS
//====================================================================

void clampCamera()
{
    if (camPitch < 0.15f) camPitch = 0.15f;
    if (camPitch > 1.45f) camPitch = 1.45f;
    if (camDist  <  9.0f) camDist  =  9.0f;
    if (camDist  > 45.0f) camDist  = 45.0f;
}

// Smoothly move the camera to a preset view
void goCam(float yaw, float pitch, float dist)
{
    while (camYaw >  PI) camYaw -= 2.0f * PI;
    while (camYaw < -PI) camYaw += 2.0f * PI;
    goalYaw = yaw; goalPitch = pitch; goalDist = dist;
    camAnim = true;
    autoOrbit = false;
}

//====================================================================
//  SYSTEM RESET
//====================================================================

void resetSystem()
{
    livingLight = bedroomLight = kitchenLight = false;
    livingFan = bedroomFan = false;
    fanSpeed = 1;
    livingFanAngle = bedroomFanAngle = 0.0f;

    acOn = compressorOn = false;
    acTarget = 24;
    tvOn = false;

    doorOpen = windowOpen = false;
    securityEnabled = alarmActive = false;
    prevThreat = false;
    gasLeak = false;
    alarmReason = 0;

    autoMode = false;
    dayCycle = true;
    timeOfDay = 10.0f;

    temperature = 28.0f;
    totalPower = 0.0f;
    energyConsumption = 0.0f;
    simTime = 0.0f;
    for (int i = 0; i < HIST_N; i++) powerHist[i] = 0.0f;
    histTimer = 0.0f;

    for (int i = 0; i < 5; i++) logLines[i] = "";
    logCount = 0;

    showHelp = false;
    roofVisible = true;
    goCam(CAM_YAW0, CAM_PITCH0, CAM_DIST0);
}

//====================================================================
//  DISPLAY
//====================================================================

void display()
{
    // Sky colour follows the time of day
    float sky[3];
    skyColor(dayLight, sky);
    glClearColor(sky[0], sky[1], sky[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    winW = (float)glutGet(GLUT_WINDOW_WIDTH);
    winH = (float)glutGet(GLUT_WINDOW_HEIGHT);

    sceneW = winW - DASH_W;
    sceneH = winH - TOP_BAR_H - BOT_BAR_H;
    glViewport(0, (int)BOT_BAR_H, (int)sceneW, (int)sceneH);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, (double)sceneW / (double)sceneH, 0.1, 500.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float cx = camTarget[0] + camDist * cosf(camPitch) * sinf(camYaw);
    float cy = camTarget[1] + camDist * sinf(camPitch);
    float cz = camTarget[2] + camDist * cosf(camPitch) * cosf(camYaw);
    gluLookAt(cx, cy, cz,
              camTarget[0], camTarget[1], camTarget[2],
              0.0f, 1.0f, 0.0f);

    setupLights();

    // ---- 3D scene ----
    drawSky();
    drawGround();

    // soft contact shadows under everything (blended, no depth write)
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    groundShadow(0.0f,    0.0f, 10.6f, 6.6f, 0.22f);        // house
    groundShadow(14.0f,   3.0f,  1.7f, 2.7f, 0.26f, -0.20f); // car (on driveway)
    groundShadow(-13.0f,  2.0f,  1.6f, 1.6f, 0.24f);        // trees
    groundShadow( 12.5f, -3.0f,  1.9f, 1.9f, 0.24f);
    groundShadow(-12.0f, -4.5f,  1.3f, 1.3f, 0.24f);
    groundShadow( 13.5f,  3.5f,  1.4f, 1.4f, 0.24f);
    groundShadow(-15.0f, -8.0f,  1.7f, 1.7f, 0.24f);
    groundShadow( 15.0f, -9.0f,  1.5f, 1.5f, 0.24f);
    groundShadow(-3.2f,   9.5f,  0.6f, 0.6f, 0.22f);        // lamp posts
    groundShadow( 3.2f,   9.5f,  0.6f, 0.6f, 0.22f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);

    drawStreetLamp(-3.2f, 9.5f);
    drawStreetLamp( 3.2f, 9.5f);
    drawCar();
    drawHouseShell();
    drawLivingRoom();
    drawBedroom();
    drawAC();
    drawKitchen();
    drawDetector();
    drawMainDoor();
    drawBedroomWindow();
    drawLamp(-6.0f, -2.8f, livingLight);
    drawLamp( 0.0f, -2.8f, bedroomLight);
    drawLamp( 6.0f, -2.8f, kitchenLight);
    drawCeilingFan(-6.0f, 2.2f, livingFanAngle);
    drawCeilingFan( 0.0f, 2.2f, bedroomFanAngle);

    if (roofVisible)
    {
        drawRoof();
        drawSmoke();
    }

    // translucent effects last
    drawLightCones();
    drawCoolAir();
    drawGas();

    drawLabels();

    // ---- 2D overlay ----
    begin2D();
    drawDashboard();
    drawTopBar();
    drawBottomBar();
    if (alarmActive) drawAlarmOverlay();
    if (showHelp)    drawHelpOverlay();
    end2D();

    glutSwapBuffers();
}

//====================================================================
//  RESHAPE
//====================================================================

void reshape(int w, int h)
{
    (void)w;
    (void)h;
}

//====================================================================
//  KEYBOARD  (normal keys)
//====================================================================

// Manual control of a device switches AUTO mode off
void leaveAuto()
{
    if (autoMode)
    {
        autoMode = false;
        logEvent("Auto mode OFF (manual)");
    }
}

void keyboard(unsigned char key, int, int)
{
    switch (key)
    {
        case '1': leaveAuto(); livingLight  = !livingLight;
                  logEvent(string("Living light ") + (livingLight ? "ON" : "OFF")); break;
        case '2': leaveAuto(); bedroomLight = !bedroomLight;
                  logEvent(string("Bedroom light ") + (bedroomLight ? "ON" : "OFF")); break;
        case '3': leaveAuto(); kitchenLight = !kitchenLight;
                  logEvent(string("Kitchen light ") + (kitchenLight ? "ON" : "OFF")); break;

        case 'f': case 'F': leaveAuto(); livingFan  = !livingFan;
                  logEvent(string("Living fan ") + (livingFan ? "ON" : "OFF")); break;
        case 'b': case 'B': leaveAuto(); bedroomFan = !bedroomFan;
                  logEvent(string("Bedroom fan ") + (bedroomFan ? "ON" : "OFF")); break;

        case '+': case '=': if (fanSpeed < 3) fanSpeed++; break;
        case '-': case '_': if (fanSpeed > 1) fanSpeed--; break;

        case 'e': case 'E': acOn = !acOn;
                  logEvent(string("Air conditioner ") + (acOn ? "ON" : "OFF")); break;
        case '[': if (acTarget > 16) acTarget--; break;
        case ']': if (acTarget < 30) acTarget++; break;

        case 'v': case 'V': tvOn = !tvOn;
                  logEvent(string("Television ") + (tvOn ? "ON" : "OFF")); break;

        case 'd': case 'D': doorOpen   = !doorOpen;
                  logEvent(string("Main door ") + (doorOpen ? "OPENED" : "CLOSED")); break;
        case 'w': case 'W': windowOpen = !windowOpen;
                  logEvent(string("Window ") + (windowOpen ? "OPENED" : "CLOSED")); break;

        case 's': case 'S':
            securityEnabled = !securityEnabled;
            if (!securityEnabled && !gasLeak) alarmActive = false;
            if (soundOn) MessageBeep(MB_OK);      // arm/disarm chirp
            logEvent(string("Security ") + (securityEnabled ? "ARMED" : "DISARMED"));
            break;

        case 'u': case 'U':
            soundOn = !soundOn;
            logEvent(string("Alarm sound ") + (soundOn ? "ON" : "OFF"));
            break;

        case 'a': case 'A':
            alarmActive = false;
            if (gasLeak) { gasLeak = false; logEvent("Gas leak cleared"); }
            else logEvent("Alarm reset");
            break;

        case 'g': case 'G':
            gasLeak = !gasLeak;
            if (gasLeak)
            {
                alarmActive = true;
                alarmReason = 1;
                windowOpen = true;                 // auto-ventilate
                livingFan = bedroomFan = true;
                logEvent("GAS LEAK! Ventilating");
            }
            else
            {
                alarmActive = false;
                logEvent("Gas leak cleared");
            }
            break;

        case 'm': case 'M': autoMode = !autoMode;
                  logEvent(string("Auto mode ") + (autoMode ? "ON" : "OFF")); break;
        case 'n': case 'N': dayCycle = !dayCycle;
                  logEvent(string("Day cycle ") + (dayCycle ? "running" : "paused")); break;
        case ',': timeOfDay = fmodf(timeOfDay + 23.0f, 24.0f); break;
        case '.': timeOfDay = fmodf(timeOfDay + 1.0f, 24.0f);  break;

        case 't': case 'T': if (temperature < 45.0f) temperature += 1.0f; break;
        case 'y': case 'Y': if (temperature > 16.0f) temperature -= 1.0f; break;

        case 'h': case 'H': showHelp = !showHelp; break;
        case 'c': case 'C': roofVisible = !roofVisible; break;
        case 'r': case 'R': resetSystem();        break;

        // camera presets
        case '7': goCam(0.0f, 0.35f, 21.0f); break;
        case '8': goCam(0.0f, 1.45f, 24.0f); roofVisible = false; break;
        case '9': goCam(1.45f, 0.30f, 22.0f); break;
        case '0': goCam(CAM_YAW0, CAM_PITCH0, CAM_DIST0); roofVisible = true; break;
        case 'o': case 'O': autoOrbit = !autoOrbit; camAnim = false; break;

        case 27: exit(0);   // ESC
    }

    glutPostRedisplay();
}

//====================================================================
//  KEYBOARD  (arrow keys / page keys for the camera)
//====================================================================

void specialKeys(int key, int, int)
{
    camAnim = false;
    switch (key)
    {
        case GLUT_KEY_LEFT:      camYaw   -= 0.06f; break;
        case GLUT_KEY_RIGHT:     camYaw   += 0.06f; break;
        case GLUT_KEY_UP:        camPitch += 0.05f; break;
        case GLUT_KEY_DOWN:      camPitch -= 0.05f; break;
        case GLUT_KEY_PAGE_UP:   camDist  *= 0.92f; break;
        case GLUT_KEY_PAGE_DOWN: camDist  *= 1.08f; break;
    }

    clampCamera();
    glutPostRedisplay();
}

//====================================================================
//  MOUSE  (drag to rotate, wheel to zoom)
//====================================================================

void mouseButton(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON)
    {
        if (state == GLUT_DOWN)
        {
            mouseDragging = true;
            camAnim = false;
            lastMouseX = x;
            lastMouseY = y;
        }
        else
        {
            mouseDragging = false;
        }
    }
}

void mouseMotion(int x, int y)
{
    if (!mouseDragging) return;

    camYaw   += (x - lastMouseX) * 0.005f;
    camPitch += (y - lastMouseY) * 0.004f;
    lastMouseX = x;
    lastMouseY = y;

    clampCamera();
    glutPostRedisplay();
}

void mouseWheel(int, int direction, int, int)
{
    camDist *= (direction > 0) ? 0.90f : 1.10f;
    clampCamera();
    glutPostRedisplay();
}

//====================================================================
//  ANIMATION / SIMULATION TIMER  (every 16 ms)
//====================================================================

// Set a device state from AUTO mode and log only real changes
void autoSet(bool& var, bool want, const string& name)
{
    if (var != want)
    {
        var = want;
        logEvent(name + (want ? " ON (auto)" : " OFF (auto)"));
    }
}

void update(int)
{
    // ---- Time of day / outdoor conditions ----
    if (dayCycle)
    {
        timeOfDay += DT * (24.0f / DAY_LENGTH);
        if (timeOfDay >= 24.0f) timeOfDay -= 24.0f;
    }
    dayLight    = computeDaylight(timeOfDay);
    outsideTemp = 27.0f + 6.0f * sinf(2.0f * PI * (timeOfDay - 9.0f) / 24.0f);

    // ---- AUTO mode: lights follow daylight, fans follow temperature ----
    if (autoMode)
    {
        bool night = dayLight < 0.45f;
        autoSet(livingLight,  night, "Living light");
        autoSet(bedroomLight, night, "Bedroom light");
        autoSet(kitchenLight, night, "Kitchen light");

        if (temperature > 30.0f)
        {
            autoSet(livingFan,  true, "Living fan");
            autoSet(bedroomFan, true, "Bedroom fan");
        }
        else if (temperature < 27.5f && !gasLeak)
        {
            autoSet(livingFan,  false, "Living fan");
            autoSet(bedroomFan, false, "Bedroom fan");
        }
    }

    // ---- Fan rotation ----
    if (livingFan)  livingFanAngle  += 4.0f * fanSpeed;
    if (bedroomFan) bedroomFanAngle += 4.0f * fanSpeed;
    if (livingFanAngle  > 360.0f) livingFanAngle  -= 360.0f;
    if (bedroomFanAngle > 360.0f) bedroomFanAngle -= 360.0f;

    // ---- Smooth door / window animation ----
    float doorTarget   = doorOpen   ? -78.0f : 0.0f;
    float windowTarget = windowOpen ?   1.65f : 0.0f;
    doorAngle    += (doorTarget   - doorAngle)    * 0.10f;
    windowOffset += (windowTarget - windowOffset) * 0.10f;

    // ---- Alarm / detector blink phase ----
    if (alarmActive || gasLeak) alarmPhase += DT * 9.0f;

    // ---- Alarm buzzer: non-blocking system beep, repeats while active ----
    if (alarmActive && soundOn)
    {
        beepTimer -= DT;
        if (beepTimer <= 0.0f)
        {
            beepTimer = 0.55f;     // about two beeps a second
            MessageBeep(alarmReason == 1 ? MB_ICONHAND
                                         : MB_ICONEXCLAMATION);
        }
    }
    else beepTimer = 0.0f;         // fresh beep the moment the alarm returns

    // ---- Chimney smoke / clouds / effects ----
    smokePhase += DT;

    // ---- Security: alarm while a door/window is open (armed) ----
    bool threat = securityEnabled && (doorOpen || windowOpen);
    if (threat && !prevThreat && !gasLeak)
    {
        alarmActive = true;
        alarmReason = doorOpen ? 0 : 2;
        logEvent(doorOpen ? "INTRUSION: main door" : "INTRUSION: window");
    }
    else if (!threat && !gasLeak && alarmActive)
    {
        alarmActive = false;   // door/window closed again -> stop alarm
        logEvent("Intrusion cleared - alarm off");
    }
    prevThreat = threat;

    // ---- Thermostat ----
    if (acOn)
    {
        if (temperature > acTarget + 0.5f)       compressorOn = true;
        else if (temperature <= acTarget - 0.5f) compressorOn = false;
    }
    else compressorOn = false;

    // ---- Temperature simulation ----
    float drift = (outsideTemp - temperature) * (windowOpen ? 0.0030f : 0.0006f);
    temperature += drift;
    if (livingFan || bedroomFan)
    {
        if (temperature > 22.0f) temperature -= 0.0025f * fanSpeed;
    }
    if (acOn && compressorOn) temperature -= 0.012f;
    if (temperature < 16.0f) temperature = 16.0f;
    if (temperature > 45.0f) temperature = 45.0f;

    // ---- Energy monitoring ----
    calculatePower();
    energyConsumption += totalPower * DT * ENERGY_SCALE / 3600000.0f;
    simTime += DT;

    // ---- Power history (1 sample / second) ----
    histTimer += DT;
    if (histTimer >= 1.0f)
    {
        histTimer -= 1.0f;
        memmove(&powerHist[0], &powerHist[1], sizeof(float) * (HIST_N - 1));
        powerHist[HIST_N - 1] = totalPower;
    }

    // ---- Camera animation / auto-orbit ----
    if (camAnim)
    {
        camYaw   += (goalYaw   - camYaw)   * 0.08f;
        camPitch += (goalPitch - camPitch) * 0.08f;
        camDist  += (goalDist  - camDist)  * 0.08f;
        if (fabsf(goalYaw - camYaw) < 0.003f &&
            fabsf(goalPitch - camPitch) < 0.003f &&
            fabsf(goalDist - camDist) < 0.03f)
            camAnim = false;
    }
    else if (autoOrbit && !mouseDragging)
    {
        camYaw += 0.004f;
    }

    glutPostRedisplay();
    glutTimerFunc(16, update, 0);
}

//====================================================================
//  OPENGL INITIALIZATION
//====================================================================

void init()
{
    glClearColor(0.55f, 0.74f, 0.92f, 1.0f);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);

    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    // Subtle specular sheen: floors, tiles, car body and glass pick up
    // a soft highlight while matte surfaces stay mostly flat.
    float spec[4] = { 0.18f, 0.18f, 0.18f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 24.0f);

    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);

    glEnable(GL_LIGHT0);   // sun / moon
    glEnable(GL_LIGHT1);   // living room lamp
    glEnable(GL_LIGHT2);   // bedroom lamp
    glEnable(GL_LIGHT3);   // kitchen lamp
    glEnable(GL_LIGHT4);   // alarm light

    // Random stars on a sky dome
    srand(7);
    for (int i = 0; i < NUM_STARS; i++)
    {
        float th = (rand() % 6283) / 1000.0f;
        float el = 0.15f + (rand() % 1000) / 1000.0f * 1.2f;
        starPos[i][0] = 95.0f * cosf(el) * cosf(th);
        starPos[i][1] = 95.0f * sinf(el);
        starPos[i][2] = 95.0f * cosf(el) * sinf(th) - 10.0f;
    }

    for (int i = 0; i < HIST_N; i++) powerHist[i] = 0.0f;
    dayLight = computeDaylight(timeOfDay);
    logEvent("System started");
}

//====================================================================
//  MAIN
//====================================================================

int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(WIN_W, WIN_H);
    glutInitWindowPosition(80, 40);
    glutCreateWindow("Smart Home Automation System 3D v2 - CGM Project");

    gQuad = gluNewQuadric();

    init();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specialKeys);
    glutMouseFunc(mouseButton);
    glutMotionFunc(mouseMotion);
    glutMouseWheelFunc(mouseWheel);
    glutTimerFunc(16, update, 0);

    glutMainLoop();
    return 0;
}
