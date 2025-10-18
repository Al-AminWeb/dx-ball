/* dxball_aurora_deluxe_enhanced_no_overlay.c
   DX-Ball — Aurora Deluxe Enhanced (no gray/black overlays)
   Build:
     Windows (MinGW + FreeGLUT):
       gcc dxball_aurora_deluxe_enhanced_no_overlay.c -o dxball_aurora -lfreeglut -lopengl32 -lwinmm
     Linux/macOS:
       gcc dxball_aurora_deluxe_enhanced_no_overlay.c -o dxball_aurora -lglut -lGL -lm
*/

#include <GL/glut.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
  #include <mmsystem.h>
  #define SOUND_SUPPORTED 1
#else
  static int PlaySound(const char *pszSound, void *hmod, unsigned long fdwSound) {
      (void)pszSound; (void)hmod; (void)fdwSound; return 1;
  }
  #define TEXT(x) (x)
  #define SOUND_SUPPORTED 0
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ======= CONFIG =======
#define WINDOW_W  960
#define WINDOW_H  680
#define PADDLE_H  18
#define BALL_R     9
#define MAX_BRICKS 260
#define MAX_PARTICLES 800
#define MAX_POWERUPS 40
#define MAX_BULLETS 10
#define TRAIL_LEN 16
#define GLOW_LAYERS 8
#define BOKEH_COUNT 24

// ======= ENUMS / TYPES =======
enum GameState { MENU, PLAYING, PAUSED, GAMEOVER, HIGHSCORE, WIN, HELP };
enum PowerType { EXTRA_LIFE=0, FASTER_BALL=1, WIDER_PADDLE=2, SHRINK_PADDLE=3,
                 FIREBALL=4, THROUGH_BRICK=5, IMMEDIATE_DEATH=6, SHOOTING_PADDLE=7 };

typedef struct { float x,y,w,h; int alive,colorIdx,hp; } Brick;
typedef struct { float x,y,vx,vy,r; int active; int fireball; int throughBrick; } Ball;
typedef struct { float x,y,vx,vy,r; float life; int alive; float rot; float r_,g_,b_; } Particle;
typedef struct { float x,y,vy; int type,alive; } PowerUp;
typedef struct { float x,y,vy; int alive; } Bullet;

// ======= GLOBALS =======
Brick bricks[MAX_BRICKS]; int brickCount;
Ball ball; int ballLaunched = 0;
float paddleX, paddleW = 118, paddleY = 56;
int leftDown=0,rightDown=0;

int score=0,lives=5,highscore=0,level=1;
enum GameState state=MENU;

Particle particles[MAX_PARTICLES]; int particleCount=0;
PowerUp powers[MAX_POWERUPS]; int powerCount=0;
Bullet bullets[MAX_BULLETS]; int bulletCount = 0;

// Ball trail
float trailX[TRAIL_LEN], trailY[TRAIL_LEN]; int trailHead=0;

#define MENU_ITEMS 5
int menuSelection=0;
float dx_init = 3.8f, dy_init = 3.8f;

char powerMsg[128] = ""; int powerMsgTimer = 0; int paddleCanShoot = 0; int bgPlaying = 0;

// Screen shake
float shakeTime=0.0f, shakeMag=0.0f;

// Global time for animations
float tGlobal=0.0f;

// ======= HELPERS =======
static inline float clampf(float v,float a,float b){ if(v<a) return a; if(v>b) return b; return v; }
static inline float lerp(float a,float b,float t){ return a + (b-a)*t; }

static void drawString(float x,float y,const char *s){
    glRasterPos2f(x,y);
    for(const char* p=s;*p;p++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18,*p);
}
static void drawStringSmall(float x,float y,const char *s){
    glRasterPos2f(x,y);
    for(const char* p=s;*p;p++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12,*p);
}
static void drawStringLarge(float x,float y,const char *s){
    glRasterPos2f(x,y);
    for(const char* p=s;*p;p++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18,*p);
}

static void saveHighscore(){ FILE*f=fopen("highscore.dat","w"); if(f){fprintf(f,"%d",highscore); fclose(f);} }
static void loadHighscore(){ FILE*f=fopen("highscore.dat","r"); if(f){fscanf(f,"%d",&highscore); fclose(f);} }

static void playSound(const char *name, unsigned long flags){
#ifdef _WIN32
    PlaySound(TEXT(name), NULL, flags);
#else
    (void)name; (void)flags;
#endif
}
static void startBackground(){ if(!bgPlaying){ playSound("background.wav", SND_ASYNC | SND_LOOP); bgPlaying=1; } }
static void stopBackground(){ if(bgPlaying){ PlaySound(NULL,NULL,SND_PURGE); bgPlaying=0; } }

// ======= DRAW PRIMS =======
static void drawCircle(float cx,float cy,float r,int seg){
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx,cy);
    for(int i=0;i<=seg;i++){ float a=i*2.0f*M_PI/seg; glVertex2f(cx+cosf(a)*r, cy+sinf(a)*r); }
    glEnd();
}

static void drawRoundedRect(float x,float y,float w,float h,float r){
    int seg=12; float x2=x+w,y2=y+h;
    glBegin(GL_TRIANGLE_FAN); glVertex2f(x+r,y+r);
    for(int s=0;s<4;s++){
        float cx = (s==0||s==3)? x2-r : x+r;
        float cy = (s==0||s==1)? y2-r : y+r;
        float start = s*(M_PI/2.0f);
        for(int i=0;i<=seg;i++){ float a=start + (i/(float)seg)*(M_PI/2.0f); glVertex2f(cx+cosf(a)*r, cy+sinf(a)*r); }
    }
    glEnd();
}

static void drawGradientRect(float x,float y,float w,float h, float r1,float g1,float b1,float r2,float g2,float b2){
    glBegin(GL_QUADS);
      glColor3f(r1,g1,b1); glVertex2f(x,y+h);
      glColor3f(r1,g1,b1); glVertex2f(x+w,y+h);
      glColor3f(r2,g2,b2); glVertex2f(x+w,y);
      glColor3f(r2,g2,b2); glVertex2f(x,y);
    glEnd();
    glColor3f(1,1,1);
}

// Vignette DISABLED (no overlay)
static void drawVignette(void){ /* no-op */ }

// ======= BACKGROUND =======
static void drawBackground(){
    // Radial aurora glows
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    float cx = WINDOW_W * 0.5f, cy = WINDOW_H * 0.55f;

    for(int l = 12; l >= 1; l--){
        float progress = l / 12.0f;
        float a = 0.04f * progress;
        glColor4f(0.08f, 0.12f, 0.28f, a * 0.7f);
        drawCircle(cx, cy, lerp(120, 650, progress), 64);
    }

    float auroraColors[][3] = {
        {0.15f, 0.22f, 0.45f}, {0.12f, 0.18f, 0.38f},
        {0.08f, 0.12f, 0.28f}, {0.05f, 0.08f, 0.18f}
    };
    for(int i = 0; i < 4; i++){
        float wave = sinf(tGlobal * 0.8f + i * 0.7f) * 0.5f + 0.5f;
        float radius = 300 + i * 80 + wave * 30;
        glColor4f(auroraColors[i][0], auroraColors[i][1], auroraColors[i][2], 0.15f);
        drawCircle(cx + wave * 20 - 10, cy, radius, 72);
    }
    glDisable(GL_BLEND);

    // Deep space gradient (opaque, not a dim overlay)
    drawGradientRect(0, 0, WINDOW_W, WINDOW_H,
                     0.04f, 0.05f, 0.10f,
                     0.01f, 0.01f, 0.04f);

    // Bokeh lights
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for(int i = 0; i < BOKEH_COUNT; i++){
        float phase = (float)i / BOKEH_COUNT * 2.0f * M_PI;
        float bx = fmodf((i * 87.3f + tGlobal * (15.0f + sinf(phase) * 5.0f)),
                         (float)(WINDOW_W + 300)) - 150.0f;
        float by = fmodf((i * 113.7f + tGlobal * (10.0f + cosf(phase) * 3.0f)),
                         (float)(WINDOW_H + 300)) - 150.0f;
        float pulse = 0.7f + 0.3f * sinf(tGlobal * 2.0f + phase);
        float r = (12.0f + (i % 9) * 2.0f) * pulse;
        float brightness = 0.7f + 0.3f * sinf(tGlobal * 1.5f + i);
        glColor4f(0.85f * brightness, 0.92f * brightness, 1.0f, 0.08f * pulse);
        drawCircle(bx, by, r, 36);
    }
    glDisable(GL_BLEND);
}

// ======= COLORS =======
static void brickColorByIdx(int idx, float *r,float *g,float *b){
    switch(idx%8){
        case 0: *r=0.17f; *g=0.82f; *b=0.52f; break;
        case 1: *r=0.98f; *g=0.46f; *b=0.46f; break;
        case 2: *r=0.38f; *g=0.58f; *b=1.00f; break;
        case 3: *r=0.62f; *g=0.38f; *b=0.92f; break;
        case 4: *r=1.00f; *g=0.68f; *b=0.26f; break;
        case 5: *r=1.00f; *g=0.96f; *b=0.26f; break;
        case 6: *r=0.12f; *g=0.12f; *b=0.12f; break;
        case 7: *r=0.00f; *g=0.92f; *b=0.92f; break;
        default:*r=0.8f;  *g=0.6f;  *b=0.9f;
    }
}

// ======= INIT / LEVELS =======
static void clearArrays(){
    for(int i=0;i<MAX_PARTICLES;i++) particles[i].alive=0;
    for(int i=0;i<MAX_POWERUPS;i++) powers[i].alive=0;
    for(int i=0;i<MAX_BULLETS;i++) bullets[i].alive=0;
    particleCount=0; powerCount=0; bulletCount=0; paddleCanShoot=0; powerMsg[0]=0; powerMsgTimer=0;
    for(int i=0;i<TRAIL_LEN;i++){ trailX[i]=trailY[i]=0; } trailHead=0;
}

static void initBricks(){
    brickCount=0; float bw=70, bh=24; float startX=50, startY=WINDOW_H-120; int rows=6+(level/2), cols=11;
    for(int r=0;r<rows && brickCount<MAX_BRICKS;r++){
        for(int c=0;c<cols && brickCount<MAX_BRICKS;c++){
            if(level>3 && (r*3+c)%7==0) continue;
            Brick b; float jitter=(rand()%11-5)/50.0f;
            b.x=startX + c*(bw+8) + jitter*12.0f; b.y=startY - r*(bh+8);
            b.w=bw; b.h=bh; b.alive=1; b.colorIdx=(r+c)%8; b.hp=1 + (rand()%2);
            bricks[brickCount++]=b;
        }
    }
}

static void resetBall(){
    ball.x=paddleX + paddleW/2.0f; ball.y=paddleY + PADDLE_H + BALL_R + 2;
    ball.r=BALL_R; ball.vx=0; ball.vy=0; ball.active=1; ball.fireball=0; ball.throughBrick=0; ballLaunched=0;
}

static void startNewGame(){ level=1; score=0; lives=5; paddleW=118; clearArrays(); initBricks(); resetBall(); state=PLAYING; startBackground(); }
static void resumeGame(){ state=PLAYING; startBackground(); }

// ======= PARTICLES =======
static void spawnParticles(float x,float y,int n, float r_,float g_,float b_){
    for(int i=0;i<n && particleCount<MAX_PARTICLES;i++){
        Particle p; p.x=x; p.y=y; float ang=((float)(rand()%360))*M_PI/180.0f; float spd=((float)(rand()%70))/14.0f + 1.1f;
        p.vx=cosf(ang)*spd; p.vy=sinf(ang)*spd; p.r=2.0f + (rand()%6)/2.0f; p.life=0.9f + (rand()%60)/100.0f; p.alive=1; p.rot=(rand()%360);
        p.r_=r_; p.g_=g_; p.b_=b_;
        particles[particleCount++]=p;
    }
}
static void spawnConfetti(float x,float y,int n){
    for(int i=0;i<n && particleCount<MAX_PARTICLES;i++){
        Particle p; p.x=x + (rand()%41-20)*0.6f; p.y=y + (rand()%41-20)*0.6f; float ang=((float)(rand()%360))*M_PI/180.0f;
        float spd=((float)(rand()%60))/12.0f + 1.4f; p.vx=cosf(ang)*spd; p.vy=sinf(ang)*spd; p.r=3.0f+(rand()%6)/2.0f; p.life=1.5f+(rand()%80)/100.0f; p.alive=1; p.rot=(rand()%360);
        float cs[6][3]={{1.0f,0.7f,0.3f},{0.6f,0.9f,1.0f},{0.9f,0.6f,1.0f},{0.7f,1.0f,0.7f},{1.0f,0.9f,0.5f},{1.0f,0.8f,0.9f}};
        int k=rand()%6; p.r_=cs[k][0]; p.g_=cs[k][1]; p.b_=cs[k][2];
        particles[particleCount++]=p;
    }
}
static void updateParticles(){
    for(int i=0;i<particleCount;i++){
        if(!particles[i].alive) continue;
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].vy -= 0.05f;
        particles[i].life -= 0.02f;
        if(particles[i].life<=0) particles[i].alive=0;
    }
}

// ======= POWERUPS =======
static void spawnPower(float x,float y,int type){
    if(powerCount>=MAX_POWERUPS) return;
    if(rand()%2!=0) return;
    for(int i=0;i<MAX_POWERUPS;i++){
        if(!powers[i].alive){
            powers[i].x=x; powers[i].y=y; powers[i].vy=-2.3f; powers[i].type=type; powers[i].alive=1; powerCount++; break;
        }
    }
}
static int rectOverlap(float ax,float ay,float aw,float ah,float bx,float by,float bw,float bh){
    if(ax+aw<bx) return 0; if(bx+bw<ax) return 0; if(ay+ah<by) return 0; if(by+bh<ay) return 0; return 1;
}
static void setPowerMessage(const char *s){
    strncpy(powerMsg,s,sizeof(powerMsg)-1);
    powerMsg[sizeof(powerMsg)-1]='\0';
    powerMsgTimer=150;
}
static void applyPower(int type){
    char msgbuf[64];
    switch(type){
        case EXTRA_LIFE: lives++; sprintf(msgbuf,"Extra Life!"); playSound("powerup.wav", SND_ASYNC); break;
        case FASTER_BALL: { if(ball.vx==0 && ball.vy==0){ ball.vx=dx_init; ball.vy=dy_init; } ball.vx*=1.22f; ball.vy*=1.22f; sprintf(msgbuf,"Faster Ball!"); playSound("powerup.wav", SND_ASYNC); } break;
        case WIDER_PADDLE: paddleW += 30; sprintf(msgbuf,"Wider Paddle!"); playSound("powerup.wav", SND_ASYNC); break;
        case SHRINK_PADDLE: paddleW=fmaxf(52.0f,paddleW-30.0f); sprintf(msgbuf,"Shrink Paddle!"); playSound("hit.wav", SND_ASYNC); break;
        case FIREBALL: ball.fireball=1; sprintf(msgbuf,"Fireball!"); playSound("powerup.wav", SND_ASYNC); break;
        case THROUGH_BRICK: ball.throughBrick=1; sprintf(msgbuf,"Through Brick!"); playSound("powerup.wav", SND_ASYNC); break;
        case IMMEDIATE_DEATH: lives--; sprintf(msgbuf,"Poison! -1 Life"); playSound("hit.wav", SND_ASYNC); break;
        case SHOOTING_PADDLE: paddleCanShoot=1; sprintf(msgbuf,"Shooting Enabled! (Space)"); playSound("powerup.wav", SND_ASYNC); break;
        default: sprintf(msgbuf,"?");
    }
    setPowerMessage(msgbuf);
}
static void updatePowers(){
    for(int i=0;i<MAX_POWERUPS;i++){
        if(!powers[i].alive) continue;
        powers[i].y += powers[i].vy;
        if(powers[i].y < -20){ powers[i].alive=0; powerCount--; continue; }
        float px=powers[i].x-10, py=powers[i].y-10;
        if(rectOverlap(px,py,20,20,paddleX,paddleY,paddleW,PADDLE_H)){
            applyPower(powers[i].type);
            powers[i].alive=0; powerCount--;
        }
    }
}

// ======= BULLETS =======
static void spawnBullet(float x,float y){
    if(!paddleCanShoot) return;
    for(int i=0;i<MAX_BULLETS;i++){
        if(!bullets[i].alive){
            bullets[i].x=x; bullets[i].y=y+10; bullets[i].vy=6.8f; bullets[i].alive=1; bulletCount++;
            playSound("shoot.wav", SND_ASYNC);
            break;
        }
    }
}
static void updateBullets(){
    for(int i=0;i<MAX_BULLETS;i++){
        if(!bullets[i].alive) continue;
        bullets[i].y += bullets[i].vy;
        if(bullets[i].y>WINDOW_H+30){ bullets[i].alive=0; bulletCount--; continue; }
        for(int j=0;j<brickCount;j++){
            if(!bricks[j].alive) continue;
            if(bullets[i].x>bricks[j].x && bullets[i].x<bricks[j].x+bricks[j].w && bullets[i].y>bricks[j].y && bullets[i].y<bricks[j].y+bricks[j].h){
                bricks[j].hp--;
                if(bricks[j].hp<=0){
                    int btype=bricks[j].colorIdx; bricks[j].alive=0; score+=14;
                    float r,g,b; brickColorByIdx(btype,&r,&g,&b);
                    spawnParticles(bullets[i].x,bullets[i].y,10,r,g,b);
                    spawnPower(bullets[i].x,bullets[i].y,btype);
                    shakeTime=0.12f; shakeMag=4.0f;
                }
                bullets[i].alive=0; bulletCount--;
                playSound("hit.wav", SND_ASYNC);
                break;
            }
        }
    }
}
static void renderBullets(){
    for(int i=0;i<MAX_BULLETS;i++){
        if(!bullets[i].alive) continue;
        glEnable(GL_BLEND);
        glColor4f(1.0f, 0.9f, 0.3f, 0.8f);
        drawRoundedRect(bullets[i].x-3, bullets[i].y-10, 6,12, 2.0f);
        glColor4f(1.0f, 1.0f, 0.7f, 0.4f);
        drawRoundedRect(bullets[i].x-4, bullets[i].y-11, 8,14, 2.5f);
        glDisable(GL_BLEND);
    }
}

// ======= COLLISIONS =======
static void checkCollisions(){
    // Paddle
    if(ball.y - ball.r < paddleY + PADDLE_H && ball.x>paddleX && ball.x<paddleX+paddleW && ball.vy<0){
        ball.vy=fabsf(ball.vy);
        float hitPos=(ball.x - (paddleX+paddleW/2.0f))/(paddleW/2.0f);
        ball.vx = hitPos*5.2f;
        ball.vy += 0.28f*fabsf(hitPos);
        playSound("hit.wav", SND_ASYNC);
    }
    // Bricks
    for(int i=0;i<brickCount;i++){
        if(!bricks[i].alive) continue;
        if(ball.x>bricks[i].x && ball.x<bricks[i].x+bricks[i].w && ball.y>bricks[i].y && ball.y<bricks[i].y+bricks[i].h){
            bricks[i].hp--;
            float r,g,b; brickColorByIdx(bricks[i].colorIdx,&r,&g,&b);
            spawnParticles(ball.x,ball.y,12,r,g,b);
            if(bricks[i].hp<=0){
                int btype=bricks[i].colorIdx; bricks[i].alive=0; score+=16; spawnPower(ball.x,ball.y,btype); shakeTime=0.15f; shakeMag=5.0f;
            }
            if(!ball.throughBrick) ball.vy*=-1.0f;
            playSound("hit.wav", SND_ASYNC);
        }
    }
}
static int allBricksDestroyed(){ for(int i=0;i<brickCount;i++) if(bricks[i].alive) return 0; return 1; }

// ======= PHYSICS =======
static void pushTrail(float x,float y){ trailHead=(trailHead+1)%TRAIL_LEN; trailX[trailHead]=x; trailY[trailHead]=y; }

static void updatePhysics(){
    if(state!=PLAYING) return;

    if(leftDown) paddleX -= 5.2f; if(rightDown) paddleX += 5.2f; paddleX = clampf(paddleX,0.0f,(float)WINDOW_W - paddleW);

    if(!ballLaunched){ ball.x = paddleX + paddleW/2.0f; ball.y = paddleY + PADDLE_H + BALL_R + 2; return; }

    ball.x += ball.vx; ball.y += ball.vy; pushTrail(ball.x,ball.y);

    if(ball.x - ball.r < 0){ ball.x=ball.r; ball.vx*=-1; }
    if(ball.x + ball.r > WINDOW_W){ ball.x=WINDOW_W-ball.r; ball.vx*=-1; }
    if(ball.y + ball.r > WINDOW_H){ ball.y=WINDOW_H-ball.r; ball.vy*=-1; }

    checkCollisions(); updateParticles(); updatePowers(); updateBullets();

    if(allBricksDestroyed()){
        level++; clearArrays(); initBricks(); resetBall(); state=PLAYING;
        spawnConfetti(WINDOW_W/2.0f, WINDOW_H/2.0f, 140); playSound("powerup.wav", SND_ASYNC); return;
    }

    if(ball.y - ball.r < 0){
        lives--;
        if(lives>0){ spawnParticles(ball.x,ball.y,34,1.0f,0.8f,0.5f); resetBall(); }
        else { state=GAMEOVER; if(score>highscore){ highscore=score; saveHighscore(); } spawnConfetti(WINDOW_W/2.0f, WINDOW_H/2.0f, 220); playSound("hit.wav", SND_ASYNC); stopBackground(); }
    }

    if(powerMsgTimer>0) powerMsgTimer--; else powerMsg[0]='\0';

    if(shakeTime>0.0f) shakeTime -= 0.016f; if(shakeTime<0.0f) shakeTime=0.0f;
}

// ======= RENDER HELPERS =======
static void drawEnhancedBall(){
    // Trail
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for(int i = 0; i < TRAIL_LEN; i++){
        int idx = (trailHead - i + TRAIL_LEN) % TRAIL_LEN;
        float t = i / (float)TRAIL_LEN;
        float a = (1.0f - t) * 0.15f;
        float trailR = 1.0f, trailG = lerp(0.9f, 0.5f, t), trailB = lerp(0.4f, 0.1f, t);
        glColor4f(trailR, trailG, trailB, a * 0.8f);
        drawCircle(trailX[idx], trailY[idx], ball.r * (0.7f + t * 0.5f), 20);
    }
    // Glow
    float pulse = 0.8f + 0.2f * sinf(tGlobal * 8.0f);
    for(int layer = GLOW_LAYERS; layer >= 1; layer--){
        float progress = layer / (float)GLOW_LAYERS;
        float a = 0.08f * progress * pulse;
        float glowR = 1.0f, glowG = ball.fireball ? 0.4f : 0.7f, glowB = ball.fireball ? 0.1f : 0.3f;
        glColor4f(glowR, glowG, glowB, a);
        drawCircle(ball.x, ball.y, ball.r + layer * 3.0f, 32);
    }
    // Core
    glBegin(GL_TRIANGLE_FAN);
      glColor3f(1.0f, 0.95f, 0.75f);
      glVertex2f(ball.x, ball.y);
      glColor3f(1.0f, 0.8f, 0.4f);
      for(int i = 0; i <= 32; i++){
          float angle = i * 2.0f * M_PI / 32;
          glVertex2f(ball.x + cosf(angle) * ball.r, ball.y + sinf(angle) * ball.r);
      }
    glEnd();
    // Highlight
    glColor4f(1.0f, 1.0f, 1.0f, 0.3f);
    drawCircle(ball.x - ball.r * 0.3f, ball.y + ball.r * 0.3f, ball.r * 0.4f, 16);
    glDisable(GL_BLEND);
}

static void drawEnhancedHearts(int lives_, float x, float y){
    for(int i = 0; i < lives_; i++){
        float ox = x + i * 20.0f, oy = y;
        glEnable(GL_BLEND);
        glColor4f(1.0f, 0.3f, 0.4f, 0.3f);
        drawCircle(ox + 3, oy, 4.5f, 12);
        drawCircle(ox + 9, oy, 4.5f, 12);
        glColor3f(1.0f, 0.4f, 0.5f);
        glBegin(GL_TRIANGLES);
          glVertex2f(ox, oy);
          glVertex2f(ox + 6, oy - 8);
          glVertex2f(ox + 12, oy);
        glEnd();
        drawCircle(ox + 3, oy, 3.5f, 12);
        drawCircle(ox + 9, oy, 3.5f, 12);
        glColor4f(1.0f, 1.0f, 1.0f, 0.4f);
        drawCircle(ox + 4, oy + 1, 1.5f, 8);
        glDisable(GL_BLEND);
    }
}

// Transparent HUD: just text + hearts (no panel overlay)
static void drawEnhancedHUD(){
    glColor3f(0.95f, 0.96f, 1.0f);
    char hud[128]; sprintf(hud, "SCORE: %d  •  LEVEL: %d", score, level);
    drawString(22, WINDOW_H - 22, hud);

    drawString(WINDOW_W - 220, WINDOW_H - 22, "🏆 HIGH:");
    char hs[32]; sprintf(hs, "%d", highscore);
    drawString(WINDOW_W - 150, WINDOW_H - 22, hs);

    drawEnhancedHearts(lives, WINDOW_W - 320, WINDOW_H - 22);
}

// ======= SCREENS =======
static void renderEnhancedMenu(void){
    glClear(GL_COLOR_BUFFER_BIT);
    drawBackground();

    glEnable(GL_BLEND);
    for(int i = 3; i >= 1; i--){
        float a = 0.1f * i;
        glColor4f(1.0f, 0.94f, 0.72f, a);
        drawString(WINDOW_W/2 - 240 - i*2, WINDOW_H - 120 + i*2, "★ DX-BALL — AURORA DELUXE ★");
    }
    glDisable(GL_BLEND);

    glColor3f(1.0f, 0.94f, 0.72f);
    drawString(WINDOW_W/2 - 240, WINDOW_H - 120, "★ DX-BALL — AURORA DELUXE ★");

    glColor3f(0.83f, 0.86f, 1.0f);
    drawString(WINDOW_W/2 - 260, WINDOW_H - 150, "Softer glow. Cleaner HUD. Satisfying hits.");

    const char* items[MENU_ITEMS] = { "▶ Start New Game", "⏯ Resume Last Game", "🏆 High Scores", "❓ Help & Instructions", "❌ Exit Game" };
    for(int i = 0; i < MENU_ITEMS; i++){
        float pul = (i == menuSelection) ? (0.85f + 0.15f * sinf(tGlobal * 4.0f)) : 1.0f;
        if(i == menuSelection){
            glColor3f(1.0f, 0.88f, 0.55f * pul);
            glEnable(GL_BLEND);
            glColor4f(1.0f, 0.88f, 0.55f, 0.3f);
            drawRoundedRect(WINDOW_W/2 - 170, WINDOW_H/2 + 45 - i*50, 340, 30, 8.0f);
            glDisable(GL_BLEND);
        } else {
            glColor3f(0.9f, 0.92f, 0.97f);
        }
        drawString(WINDOW_W/2 - 160, WINDOW_H/2 + 60 - i*50, items[i]);
    }

    drawVignette(); // no-op now
    glutSwapBuffers();
}

static void renderEnhancedHelp(void){
    glClear(GL_COLOR_BUFFER_BIT);
    drawBackground();
    drawVignette();

    glColor3f(0.95f, 0.96f, 1.0f);
    drawString(60, WINDOW_H - 80, "📝 HELP & CONTROLS");
    drawString(60, WINDOW_H - 120, "Arrow Keys or Mouse - Move Paddle");
    drawString(60, WINDOW_H - 150, "Left Mouse Click - Launch Ball");
    drawString(60, WINDOW_H - 180, "Space Bar - Shoot Bullets (after power-up)");
    drawString(60, WINDOW_H - 210, "P - Pause / Resume Game");
    drawString(60, WINDOW_H - 240, "ESC - Return to Menu");

    drawString(60, WINDOW_H - 300, "🎨 POWER-UP COLORS:");
    drawString(60, WINDOW_H - 330, "Green   = Extra Life");
    drawString(60, WINDOW_H - 360, "Red     = Faster Ball");
    drawString(60, WINDOW_H - 390, "Blue    = Wider Paddle");
    drawString(60, WINDOW_H - 420, "Purple  = Shrink Paddle");
    drawString(60, WINDOW_H - 450, "Orange  = Fireball");
    drawString(60, WINDOW_H - 480, "Yellow  = Through Brick");
    drawString(60, WINDOW_H - 510, "Black   = Poison (Lose Life)");
    drawString(60, WINDOW_H - 540, "Cyan    = Shooting Paddle");

    drawStringSmall(60, 40, "Press ESC to return to Menu");
    glutSwapBuffers();
}

static void renderEnhancedHighscore(void){
    glClear(GL_COLOR_BUFFER_BIT);
    drawBackground();
    drawVignette();

    glColor3f(0.96f, 0.96f, 1.0f);
    drawString(WINDOW_W/2 - 80, WINDOW_H - 120, "🏆 HIGH SCORES");

    char buf[64]; sprintf(buf, "%d", highscore);
    glColor3f(1.0f, 0.9f, 0.4f);
    drawStringLarge(WINDOW_W/2 - 20, WINDOW_H/2 + 10, buf);

    drawStringSmall(WINDOW_W/2 - 160, 60, "Press ESC to go back to Main Menu");
    glutSwapBuffers();
}

static void renderEnhancedWin(void){
    glClear(GL_COLOR_BUFFER_BIT);
    drawBackground();
    drawVignette();

    glEnable(GL_BLEND);
    for(int i = 3; i >= 1; i--){
        float a = 0.1f * i;
        glColor4f(1.0f, 0.96f, 0.86f, a);
        drawString(WINDOW_W/2 - 120 - i*2, WINDOW_H/2 + 84 + i*2, "🎉 LEVEL CLEARED! 🎉");
    }
    glDisable(GL_BLEND);

    glColor3f(1.0f, 0.96f, 0.86f);
    drawString(WINDOW_W/2 - 120, WINDOW_H/2 + 84, "🎉 LEVEL CLEARED! 🎉");

    char buf[64]; sprintf(buf, "Score: %d   Level: %d", score, level);
    drawString(WINDOW_W/2 - 100, WINDOW_H/2 + 46, buf);

    drawString(WINDOW_W/2 - 260, WINDOW_H/2 + 6, "Press ENTER to continue to next level, ESC to return to menu");
    glutSwapBuffers();
}

static void renderEnhancedGameOver(void){
    glClear(GL_COLOR_BUFFER_BIT);
    drawBackground();
    drawVignette();

    glEnable(GL_BLEND);
    for(int i = 3; i >= 1; i--){
        float a = 0.1f * i;
        glColor4f(1.0f, 0.78f, 0.78f, a);
        drawString(WINDOW_W/2 - 120 - i*2, WINDOW_H/2 + 80 + i*2, "💀 GAME OVER 💀");
    }
    glDisable(GL_BLEND);

    glColor3f(1.0f, 0.78f, 0.78f);
    drawString(WINDOW_W/2 - 120, WINDOW_H/2 + 80, "💀 GAME OVER 💀");

    char b[128]; sprintf(b, "Final Score: %d   High: %d", score, highscore);
    drawString(WINDOW_W/2 - 120, WINDOW_H/2 + 42, b);

    drawString(WINDOW_W/2 - 260, WINDOW_H/2 + 2, "Press ENTER to return to Main Menu and restart, ESC to exit");
    glutSwapBuffers();
}

static void renderEnhancedScene(void){
    glClear(GL_COLOR_BUFFER_BIT);

    if(state == MENU){ renderEnhancedMenu(); return; }
    else if(state == HIGHSCORE){ renderEnhancedHighscore(); return; }
    else if(state == HELP){ renderEnhancedHelp(); return; }
    else if(state == WIN){ renderEnhancedWin(); return; }
    else if(state == GAMEOVER){ renderEnhancedGameOver(); return; }
    else if(state == PLAYING || state == PAUSED){
        drawBackground();

        glPushMatrix();
        if(shakeTime > 0.0f){
            float sx = ((rand()%100)/100.0f*2.0f-1.0f)*shakeMag;
            float sy = ((rand()%100)/100.0f*2.0f-1.0f)*shakeMag;
            glTranslatef(sx,sy,0);
        }

        // Bricks
        for(int i = 0; i < brickCount; i++){
            if(!bricks[i].alive) continue;
            float r,g,b; brickColorByIdx(bricks[i].colorIdx,&r,&g,&b);

            glColor4f(0,0,0,0.22f);
            drawRoundedRect(bricks[i].x+3, bricks[i].y-3, bricks[i].w, bricks[i].h, 6.0f);

            drawGradientRect(bricks[i].x, bricks[i].y, bricks[i].w, bricks[i].h,
                            r*0.95f, g*0.95f, b*0.95f,
                            r*0.58f, g*0.58f, b*0.58f);

            glColor3f(1,1,1);
            glBegin(GL_LINES);
                glVertex2f(bricks[i].x+4, bricks[i].y+bricks[i].h-4);
                glVertex2f(bricks[i].x+bricks[i].w-4, bricks[i].y+bricks[i].h-4);
            glEnd();

            if(bricks[i].hp > 1){
                glColor3f(1,1,1);
                char hp[4]; sprintf(hp, "%d", bricks[i].hp);
                glRasterPos2f(bricks[i].x + bricks[i].w/2 - 4, bricks[i].y + bricks[i].h/2 - 6);
                for(const char* p = hp; *p; p++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *p);
            }
        }

        // Paddle
        glColor4f(0,0,0,0.25f);
        drawRoundedRect(paddleX+4, paddleY-4, paddleW, PADDLE_H, 9.0f);
        drawGradientRect(paddleX, paddleY, paddleW, PADDLE_H,
                         0.98f, 0.98f, 1.0f,
                         0.76f, 0.76f, 0.9f);
        glColor3f(0.95f, 0.95f, 0.98f);
        drawRoundedRect(paddleX, paddleY, paddleW, PADDLE_H, 9.0f);

        if(paddleCanShoot){
            glEnable(GL_BLEND);
            glColor4f(1.0f, 0.9f, 0.3f, 0.6f + 0.4f * sinf(tGlobal * 8.0f));
            drawRoundedRect(paddleX + paddleW/2 - 8, paddleY - 6, 16, 4, 2.0f);
            glDisable(GL_BLEND);
        }

        // Ball, bullets, powerups
        drawEnhancedBall();
        renderBullets();

        for(int i = 0; i < MAX_POWERUPS; i++){
            if(powers[i].alive){
                float r,g,b; brickColorByIdx(powers[i].type,&r,&g,&b);
                glEnable(GL_BLEND);
                glColor4f(r, g, b, 0.4f);
                drawRoundedRect(powers[i].x-11, powers[i].y-11, 22, 22, 4.0f);
                glDisable(GL_BLEND);
                glColor3f(1,1,1);
                drawRoundedRect(powers[i].x-9, powers[i].y-9, 18, 18, 3.0f);
                glColor3f(r,g,b);
                drawRoundedRect(powers[i].x-6, powers[i].y-6, 12, 12, 2.5f);
            }
        }

        for(int i = 0; i < particleCount; i++){
            if(!particles[i].alive) continue;
            float t = clampf(particles[i].life, 0, 1);
            glColor4f(particles[i].r_, particles[i].g_, particles[i].b_, clampf(t, 0, 1));
            drawRoundedRect(particles[i].x, particles[i].y, particles[i].r, particles[i].r, 1.2f);
        }

        glPopMatrix();

        drawEnhancedHUD();

        if(powerMsg[0] != '\0'){
            float pulse = 0.9f + 0.1f * sinf(tGlobal * 10.0f);
            glColor3f(1.0f, 0.92f * pulse, 0.66f * pulse);
            drawString(WINDOW_W/2 - 90, WINDOW_H - 56, powerMsg);
        }

        // NOTE: Pause dark overlay removed; keep glow text only
        if(state == PAUSED){
            glEnable(GL_BLEND);
            for(int i = 2; i >= 1; i--){
                float a = 0.2f * i;
                glColor4f(1,1,1,a);
                drawString(WINDOW_W/2 - 36 - i, WINDOW_H/2 + i, "PAUSED");
            }
            glDisable(GL_BLEND);
            glColor3f(1,1,1);
            drawString(WINDOW_W/2 - 36, WINDOW_H/2, "PAUSED");
        }

        if(!ballLaunched && state == PLAYING){
            glColor3f(0.9f, 0.93f, 1.0f);
            drawString(WINDOW_W/2 - 170, WINDOW_H/2 - 22, "Click Mouse to Launch Ball (or press SPACE)");
        }

        drawVignette(); // no-op
        glutSwapBuffers();
        return;
    }

    glutSwapBuffers();
}

// ======= TIMER =======
static void updateTimer(int value){
    if(state == PLAYING) updatePhysics();
    tGlobal += 0.016f;
    glutPostRedisplay();
    glutTimerFunc(16, updateTimer, 0);
}

// ======= INPUT =======
static void keyDown(unsigned char key,int x,int y){
    if(state == MENU){
        if(key == 13){
            if(menuSelection == 0) startNewGame();
            else if(menuSelection == 1) { state = PLAYING; startBackground(); }
            else if(menuSelection == 2) state = HIGHSCORE;
            else if(menuSelection == 3) state = HELP;
            else if(menuSelection == 4) exit(0);
        }
        else if(key == 27) exit(0);
    } else if(state == HIGHSCORE || state == HELP){
        if(key == 27){ state = MENU; menuSelection = 0; }
    }
    else if(state == PLAYING){
        if(key == 'p' || key == 'P'){ state = PAUSED; stopBackground(); }
        if(key == ' '){
            if(!ballLaunched){
                ball.vx = dx_init; ball.vy = dy_init; ballLaunched = 1;
                playSound("launch.wav", SND_ASYNC);
            } else {
                spawnBullet(paddleX + paddleW/2, paddleY + PADDLE_H);
            }
        }
        if(key == 27){ state = MENU; menuSelection = 0; stopBackground(); }
    } else if(state == PAUSED){
        if(key == 'p' || key == 'P'){ state = PLAYING; startBackground(); }
        if(key == 27){ state = MENU; menuSelection = 0; stopBackground(); }
    }
    else if(state == GAMEOVER){
        if(key == 13){
            state = MENU; menuSelection = 0; level = 1; score = 0; lives = 5; paddleW = 118;
            clearArrays(); initBricks(); resetBall();
        }
        if(key == 27) exit(0);
    }
    else if(state == WIN){
        if(key == 13){
            level++; clearArrays(); initBricks(); resetBall(); state = PLAYING; startBackground();
        }
        if(key == 27){ state = MENU; menuSelection = 0; }
    }
}
static void specialKeyDown(int key,int x,int y){
    if(key == GLUT_KEY_LEFT) leftDown = 1;
    if(key == GLUT_KEY_RIGHT) rightDown = 1;
    if(state == MENU){
        if(key == GLUT_KEY_LEFT){ menuSelection--; if(menuSelection < 0) menuSelection = MENU_ITEMS-1; }
        if(key == GLUT_KEY_RIGHT){ menuSelection++; if(menuSelection >= MENU_ITEMS) menuSelection = 0; }
    }
}
static void specialKeyUp(int key,int x,int y){ if(key == GLUT_KEY_LEFT) leftDown = 0; if(key == GLUT_KEY_RIGHT) rightDown = 0; }

static void mouseMotion(int x,int y){
    paddleX = (float)x - paddleW/2.0f;
    paddleX = clampf(paddleX, 0.0f, (float)WINDOW_W - paddleW);
}
static void mouseClick(int button,int buttonState,int x,int y){
    if(state == PLAYING && !ballLaunched && button == GLUT_LEFT_BUTTON && buttonState == GLUT_DOWN){
        ball.vx = dx_init; ball.vy = dy_init; ballLaunched = 1;
        playSound("launch.wav", SND_ASYNC);
    }
}

// ======= MAIN / GL =======
static void reshape(int w,int h){
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0,w,0,h,-1,1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

int main(int argc,char**argv){
    srand((unsigned int)time(NULL));
    loadHighscore();
    glutInit(&argc,argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(WINDOW_W, WINDOW_H);
    glutCreateWindow("DX-Ball — Aurora Deluxe (No Overlay)");

    glClearColor(0.03f, 0.03f, 0.07f, 1.0f);
    paddleX = WINDOW_W/2 - paddleW/2;
    clearArrays();
    initBricks();
    resetBall();

    glutDisplayFunc(renderEnhancedScene);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyDown);
    glutSpecialFunc(specialKeyDown);
    glutSpecialUpFunc(specialKeyUp);
    glutMotionFunc(mouseMotion);
    glutPassiveMotionFunc(mouseMotion);
    glutMouseFunc(mouseClick);

    state = MENU; menuSelection = 0;
    glutTimerFunc(16, updateTimer, 0);
    glutMainLoop();
    return 0;
}
