#include <MCUFRIEND_kbv.h>
#include <Adafruit_GFX.h>
#include <TouchScreen.h>
#include <SPI.h>
#include <SD.h>

MCUFRIEND_kbv tft;

// -------- Touch wiring + PORTRAIT calibration --------
#define XP 7
#define XM A1
#define YP A2
#define YM 6
const int TS_LEFT = 925;
const int TS_RT   = 168;
const int TS_TOP  = 964;
const int TS_BOT  = 190;
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
// -----------------------------------------------------

// ---- Set this to the CS pin that worked in your test ----
#define SD_CS_PIN 10   // change to 7 if your ZEN test worked on D7

// ---- Theme ----
#define COL_BG      0x2104
#define COL_CARD    0xFFFF
#define COL_BORDER  0x8410
#define COL_TEXT    0x0000
#define COL_ACCENT  0xAD55

int16_t TFT_W, TFT_H;
bool sdOK = false;

// ---- Buttons ----
struct Btn {
  int x,y,w,h;
  const char* label;
  const char* cmd;       // VOL_UP, VOL_DOWN, BRIGHT_UP, ...
  const char* arg;       // for OPEN_URL or KEY_COMBO
  const char* iconFile;  // 48x48, 24-bit BMP in SD root
};

// Page 1
Btn page1[8] = {
  {0,0,0,0, "Vol +",    "VOL_UP",      "",                        "VOLUP.BMP"},
  {0,0,0,0, "Vol -",    "VOL_DOWN",    "",                        "VOLDN.BMP"},
  {0,0,0,0, "Bright +", "BRIGHT_UP",   "",                        "BRUP.BMP"},
  {0,0,0,0, "Bright -", "BRIGHT_DOWN", "",                        "BRDN.BMP"},
  {0,0,0,0, "Zen",      "ZEN",         "",                        "ZEN.BMP"},
  {0,0,0,0, "Copy",     "KEY_COMBO",   "ctrl+c",                  "COPY.BMP"},
  {0,0,0,0, "Paste",    "KEY_COMBO",   "ctrl+v",                  "PASTE.BMP"},
  {0,0,0,0, "Next",     "PAGE_NEXT",   "",                        ""} // text only
};

// Page 2
Btn page2[8] = {
  {0,0,0,0, "Notion", "OPEN_URL", "https://mail.notion.so/",    "NOTION.BMP"},
  {0,0,0,0, "Music",  "OPEN_URL", "https://music.youtube.com/", "MUSIC.BMP"},
  {0,0,0,0, "Canva",  "OPEN_URL", "https://canva.com/",         "CANVA.BMP"},
  {0,0,0,0, "ChatGPT","OPEN_URL", "https://chat.openai.com/",   "CHAT.BMP"},
  {0,0,0,0, "Prev",   "PAGE_PREV","",                           ""}, // text only
  {0,0,0,0, "", "NONE","", ""}, {0,0,0,0, "", "NONE","", ""}, {0,0,0,0, "", "NONE","", ""}
};

Btn* currentPage = page1;
int currentPageNum = 1;
const int totalPages = 2;

// ---------- Touch ----------
inline bool pointIn(const Btn& b, int x, int y) {
  return x>=b.x && x<=b.x+b.w && y>=b.y && y<=b.y+b.h;
}
TSPoint readTouch() {
  TSPoint p = ts.getPoint();
  pinMode(YP, OUTPUT); pinMode(XM, OUTPUT);
  digitalWrite(YP, HIGH); digitalWrite(XM, HIGH);
  if (p.z < 200 || p.z > 1000) return TSPoint(0,0,0);
  int16_t x = map(p.x, TS_LEFT, TS_RT, 0, TFT_W);
  int16_t y = map(p.y, TS_TOP,  TS_BOT, 0, TFT_H);
  return TSPoint(x,y,p.z);
}

// ---------- Minimal 24-bit BMP draw ----------
bool drawBMP(const char* filename, int x, int y) {
  if (!sdOK || !filename || !filename[0]) return false;
  File bmp = SD.open(filename, FILE_READ);
  if (!bmp) return false;

  if (bmp.read()!='B' || bmp.read()!='M') { bmp.close(); return false; }

  uint32_t fileSize;     bmp.read(&fileSize, 4);   // unused
  bmp.seek(10);
  uint32_t dataOffset;   bmp.read(&dataOffset, 4);
  bmp.seek(14);
  uint32_t headerSize;   bmp.read(&headerSize, 4);
  if (headerSize < 40) { bmp.close(); return false; }

  int32_t w, h;          bmp.read(&w, 4); bmp.read(&h, 4);
  uint16_t planes, bpp;  bmp.read(&planes, 2); bmp.read(&bpp, 2);
  uint32_t comp;         bmp.read(&comp, 4);
  if (planes != 1 || bpp != 24 || comp != 0 || w <= 0 || h == 0) { bmp.close(); return false; }

  bool flip = true; if (h < 0) { h = -h; flip = false; }
  uint32_t rowSize = ((w * 3 + 3) & ~3);
  if (x >= TFT_W || y >= TFT_H) { bmp.close(); return false; }

  bmp.seek(dataOffset);

  uint8_t sdbuf[3*10]; // tiny buffer to fit Uno
  for (int row = 0; row < h; row++) {
    uint32_t pos = dataOffset + (flip ? (h-1-row) : row) * rowSize;
    if (bmp.position() != pos) bmp.seek(pos);

    int col = 0; int cx = x;
    while (col < w) {
      int toRead = min((int)sizeof(sdbuf), (w - col) * 3);
      if (bmp.read(sdbuf, toRead) != toRead) { bmp.close(); return false; }
      for (int i = 0; i < toRead; i += 3) {
        if ((cx >= 0) && (cx < TFT_W) && (y+row >= 0) && (y+row < TFT_H)) {
          uint8_t b = sdbuf[i+0], g = sdbuf[i+1], r = sdbuf[i+2];
          uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
          tft.drawPixel(cx, y+row, color);
        }
        cx++;
      }
      col += toRead / 3;
    }
  }
  bmp.close();
  return true;
}

inline bool drawBMPCentered48(const Btn& b) {
  if (!b.iconFile || !b.iconFile[0]) return false;
  int ix = b.x + (b.w - 48)/2;
  int iy = b.y + (b.h - 48)/2 - 6; // leave room for label
  return drawBMP(b.iconFile, ix, iy);
}

// ---------- UI ----------
void layoutGrid(Btn* page){
  const int margin=10, cols=2, rows=4;
  int bw=(TFT_W - margin*(cols+1))/cols;
  int bh=(TFT_H - margin*(rows+1))/rows;
  int k=0;
  for(int r=0;r<rows;r++){
    for(int c=0;c<cols;c++,k++){
      page[k].x = margin + c*(bw+margin);
      page[k].y = margin + r*(bh+margin);
      page[k].w = bw; page[k].h = bh;
    }
  }
}
inline void card(const Btn& b){
  tft.fillRoundRect(b.x,b.y,b.w,b.h,10,COL_CARD);
  tft.drawRoundRect(b.x,b.y,b.w,b.h,10,COL_BORDER);
}
void drawButton(const Btn& b){
  card(b);
  if (sdOK) drawBMPCentered48(b);

  tft.setTextSize(1); tft.setTextColor(COL_TEXT);
  int cx=b.x+b.w/2, ly=b.y+b.h-10;
  int lx=cx - (strlen(b.label)*6)/2;   // 5x7 font ≈ 6 px per char
  tft.setCursor(lx, ly);
  tft.print(b.label);
}
void drawPage(){
  tft.fillScreen(COL_BG);
  layoutGrid(currentPage);
  for(int i=0;i<8;i++) drawButton(currentPage[i]);
}

// ---------- Page switch ----------
inline void toPage(int n){ currentPageNum=n; currentPage=(n==1)?page1:page2; drawPage(); }
inline void nextPage(){ toPage( (currentPageNum%2)+1 ); }
inline void prevPage(){ toPage( (currentPageNum+0)%2 + 1 ); }

inline void pressFeedback(const Btn& b, bool down){
  tft.drawRoundRect(b.x,b.y,b.w,b.h,10, down?COL_ACCENT:COL_BORDER);
}

// ---------- Setup / Loop ----------
void setup(){
  Serial.begin(115200);

  uint16_t id=tft.readID(); if(id==0xD3D3) id=0x9486;
  tft.begin(id);
  tft.setRotation(0);
  TFT_W=tft.width(); TFT_H=tft.height();

  pinMode(10, OUTPUT); digitalWrite(10, HIGH); // keep SPI master
  sdOK = SD.begin(SD_CS_PIN);

  toPage(1);
}

void loop(){
  TSPoint p = readTouch();
  if (p.z>0){
    delay(120);
    for(int i=0;i<8;i++){
      if(pointIn(currentPage[i], p.x, p.y)){
        pressFeedback(currentPage[i], true);

        // Local page nav
        if(!strcmp(currentPage[i].cmd,"PAGE_NEXT")){
          delay(120); pressFeedback(currentPage[i], false);
          while(readTouch().z>0){} nextPage(); return;
        }
        if(!strcmp(currentPage[i].cmd,"PAGE_PREV")){
          delay(120); pressFeedback(currentPage[i], false);
          while(readTouch().z>0){} prevPage(); return;
        }

        // BTN:<CMD>[|<ARG>]
        Serial.print("BTN:"); Serial.print(currentPage[i].cmd);
        if(currentPage[i].arg && currentPage[i].arg[0]){
          Serial.print('|'); Serial.print(currentPage[i].arg);
        }
        Serial.println();

        delay(120); pressFeedback(currentPage[i], false);
        break;
      }
    }
    while(readTouch().z>0) delay(10);
  }
}
