#include <M5Unified.h>
#include <FS.h>
#include <SD_MMC.h>

// --- DEKLARACJE ---
const int STEPS = 16, TRACKS = 4, NOTES = 25; 
bool drumGrid[TRACKS][STEPS] = {false};
int bassGrid[STEPS] = {0}, melGrid[STEPS] = {0}, currentMode = 0; 
int masterVol = 180, bassVol = 180, melVol = 140, kbdVol = 160, bpm = 125;
float scrollY = 0;
volatile int currentStep = 0;
int lastDrawnStep = -1;
bool isPlaying = false, showFileMenu = false, fullRedraw = true, toolbarRedraw = true;
int uiFileSlot = 1;

uint8_t* wavData[TRACKS]; size_t wavLen[TRACKS]; float freqs[NOTES]; 
const uint8_t square_wav[] = { 255, 255, 255, 255, 0, 0, 0, 0, 128 }; 

void handleFile(bool save);
void initFreqs() { freqs[0] = 0; for(int i=1; i<NOTES; i++) freqs[i] = 65.41 * pow(2.0, (i-1)/12.0); }
bool isBlackKey(int n) { int m = (n - 1) % 12; return (m == 1 || m == 3 || m == 6 || m == 8 || m == 10); }
const int whiteKeyNotes[] = {1, 3, 5, 6, 8, 10, 12, 13, 15, 17, 18, 20, 22, 24};

void handleFile(bool save) {
  char path[24]; sprintf(path, "/slot%d.tsq", uiFileSlot);
  if (save) {
    File f = SD_MMC.open(path, FILE_WRITE);
    if (f) { f.write((uint8_t*)drumGrid, sizeof(drumGrid)); f.write((uint8_t*)bassGrid, sizeof(bassGrid)); f.write((uint8_t*)melGrid, sizeof(melGrid)); f.write((uint8_t*)&bpm, sizeof(bpm)); f.close(); }
  } else if (SD_MMC.exists(path)) {
    File f = SD_MMC.open(path, FILE_READ);
    if (f) { f.read((uint8_t*)drumGrid, sizeof(drumGrid)); f.read((uint8_t*)bassGrid, sizeof(bassGrid)); f.read((uint8_t*)melGrid, sizeof(melGrid)); f.read((uint8_t*)&bpm, sizeof(bpm)); f.close(); }
  }
  showFileMenu = false; fullRedraw = true; toolbarRedraw = true;
}

bool loadWav(const char* p, int t) {
  if (!SD_MMC.exists(p)) return false;
  File f = SD_MMC.open(p); if (!f) return false;
  wavLen[t] = f.size(); wavData[t] = (uint8_t*)malloc(wavLen[t]);
  if (wavData[t]) f.read(wavData[t], wavLen[t]); f.close(); return true;
}

void audioTask(void *pvParameters) {
  unsigned long lastTick = micros();
  while (true) {
    if (!M5.Speaker.isPlaying(0)) M5.Speaker.tone(1, 100, 0, false); 
    if (isPlaying) {
      unsigned long interval = 60000000 / (bpm * 4);
      if (micros() - lastTick >= interval) {
        lastTick += interval;
        currentStep = (currentStep + 1) % STEPS;
        int nextStep = (currentStep + 1) % STEPS;
        for (int i = 0; i < TRACKS; i++) if (drumGrid[i][currentStep] && wavData[i]) M5.Speaker.playWav(wavData[i], wavLen[i], 1, 0, true);
        auto handleLegato = [&](int channel, int* grid, int vol, float multi) {
          int curNote = grid[currentStep];
          int nxtNote = grid[nextStep];
          if (curNote > 0) {
            M5.Speaker.setChannelVolume(channel, vol);
            int duration = (curNote == nxtNote) ? (interval/1000) + 10 : (interval/1000) * 0.85;
            M5.Speaker.tone(freqs[curNote] * multi, duration, channel, true, square_wav, sizeof(square_wav));
          } else { M5.Speaker.setChannelVolume(channel, 0); }
        };
        handleLegato(1, bassGrid, bassVol, 1.0);
        handleLegato(2, melGrid, melVol, 4.0);
      }
    }
    vTaskDelay(1);
  }
}

void drawGridColumn(int c, int w, int gh, int h, int offset) {
  M5.Display.fillRect(c * w, 0, w, gh, BLACK);
  uint16_t lc = (c % 4 == 0) ? 0x528A : 0x2104; 
  M5.Display.drawFastVLine(c * w, 0, gh, lc);
  if (currentMode == 0) {
    for (int r = 0; r < TRACKS; r++) {
      M5.Display.drawFastHLine(c * w, r * h, w, 0x2104);
      if (drumGrid[r][c]) {
        uint16_t color = (r==0?RED:r==1?BLUE:r==2?YELLOW:MAGENTA);
        M5.Display.fillRect(c * w + 2, r * h + 2, w - 4, h - 4, color);
      }
      if (isPlaying && c == currentStep) M5.Display.drawRect(c * w + 1, r * h + 1, w - 2, h - 2, WHITE);
    }
  } else if (currentMode < 3) {
    int* g = (currentMode == 1) ? bassGrid : melGrid;
    uint16_t ac = (currentMode == 1) ? GREEN : MAGENTA;
    for (int r = 0; r < 10; r++) {
      int nIdx = 24 - (r + offset);
      if (nIdx >= 1 && isBlackKey(nIdx)) M5.Display.fillRect(c * w, r * h, w, h, 0x18C3); 
      M5.Display.drawFastHLine(c * w, r * h, w, 0x2104);
      if (nIdx >= 1 && g[c] == nIdx) {
        int rectX = c * w + 2, rectW = w - 4;
        if (c < STEPS - 1 && g[c+1] == nIdx) rectW += 4;
        if (c > 0 && g[c-1] == nIdx) { rectX -= 2; rectW += 2; }
        M5.Display.fillRect(rectX, r * h + 2, rectW, h - 4, ac);
      }
      if (isPlaying && c == currentStep) M5.Display.drawRect(c * w + 1, r * h + 1, w - 2, h - 2, WHITE);
    }
  }
}

void drawKeyboard(int sw, int gh) {
  int keyW = sw / 14; 
  for (int i = 0; i < 14; i++) {
    M5.Display.fillRect(i * keyW, 0, keyW - 1, gh, WHITE);
    M5.Display.drawRect(i * keyW, 0, keyW, gh, 0x2104);
  }
  int blackKeyW = keyW * 0.7, blackKeyH = gh * 0.6;
  for (int i = 0; i < 13; i++) {
    if (i == 2 || i == 6 || i == 9 || i == 13) continue; 
    M5.Display.fillRect(i * keyW + (keyW - blackKeyW / 2), 0, blackKeyW, blackKeyH, BLACK);
  }
}

void drawBattery() {
  int bat = M5.Power.getBatteryLevel();
  uint16_t color = (bat > 50) ? GREEN : (bat > 20) ? YELLOW : RED;
  int bx = M5.Display.width() - 55, by = M5.Display.height() - 35;
  M5.Display.drawRect(bx, by, 30, 15, WHITE);
  M5.Display.fillRect(bx + 30, by + 4, 3, 7, WHITE);
  M5.Display.fillRect(bx + 2, by + 2, map(bat, 0, 100, 0, 26), 11, color);
}

void drawUI() {
  int sw = M5.Display.width(), sh = M5.Display.height(), th = 130, gh = sh - th, gridW = sw - 50, w = gridW / STEPS;
  int h = (currentMode == 0) ? (gh / TRACKS) : (gh / 10), offset = (int)scrollY;
  M5.Display.startWrite();
  if (fullRedraw) { 
    M5.Display.fillRect(0, 0, sw, gh, BLACK); 
    if (currentMode == 3) drawKeyboard(sw, gh);
    else {
      for(int i=0; i<STEPS; i++) drawGridColumn(i, w, gh, h, offset);
      if (currentMode != 0) { M5.Display.fillRect(gridW + 2, 0, 48, gh, 0x1084); M5.Display.fillRect(gridW + 5, map(offset, 0, 14, 0, gh - 40), 40, 40, WHITE); }
    }
    fullRedraw = false; 
  }
  if (!showFileMenu) {
    if (currentMode < 3 && isPlaying && lastDrawnStep != currentStep) {
      if (lastDrawnStep != -1) drawGridColumn(lastDrawnStep, w, gh, h, offset);
      drawGridColumn(currentStep, w, gh, h, offset);
      lastDrawnStep = currentStep;
    } else if (!isPlaying && lastDrawnStep != -1) { drawGridColumn(lastDrawnStep, w, gh, h, offset); lastDrawnStep = -1; }
    if (toolbarRedraw) {
      int bW = sw / 6; M5.Display.fillRect(0, gh, sw, th, BLACK); M5.Display.setTextSize(2.5);
      M5.Display.fillRoundRect(5, gh+5, bW-10, 50, 6, isPlaying?0x8000:0x0400);
      M5.Display.setTextColor(WHITE); M5.Display.drawCenterString(isPlaying?"STOP":"PLAY", bW/2, gh+20);
      M5.Display.fillRoundRect(bW+5, gh+5, bW-10, 50, 6, 0xC618);
      M5.Display.setTextColor(BLACK); M5.Display.drawCenterString("CLR", bW + bW/2, gh+20);
      uint16_t mC = (currentMode==0)?CYAN:(currentMode==1)?GREEN:(currentMode==2)?MAGENTA:YELLOW;
      M5.Display.fillRoundRect(2*bW+5, gh+5, bW*1.2-10, 50, 6, mC);
      const char* mNs[] = {"DRM","BS","MLD","KBD"}; M5.Display.drawCenterString(mNs[currentMode], 2*bW+(bW*1.2)/2, gh+20);
      M5.Display.setTextColor(WHITE);
      M5.Display.fillRoundRect(3.3*bW+5, gh+5, 35, 50, 4, 0x1084); M5.Display.drawCenterString("-", 3.3*bW+22, gh+20);
      M5.Display.setCursor(3.9*bW+10, gh+20); M5.Display.printf("%d", bpm);
      M5.Display.fillRoundRect(4.6*bW+10, gh+5, 35, 50, 4, 0x1084); M5.Display.drawCenterString("+", 4.6*bW+27, gh+20);
      M5.Display.fillRoundRect(5, gh+65, bW*1.5, 45, 6, 0x4208); M5.Display.drawCenterString("FILE", bW*0.75+5, gh+75);
      int cv = (currentMode==0)?masterVol:(currentMode==1)?bassVol:(currentMode==2)?melVol:kbdVol;
      int sx = bW*1.8+60, swl = sw-sx-100;
      M5.Display.setTextSize(2); M5.Display.setCursor(bW*1.8, gh+75); M5.Display.printf("VOL:");
      M5.Display.drawRect(sx, gh+70, swl, 30, WHITE); M5.Display.fillRect(sx+2, gh+72, map(cv, 0, 255, 0, swl-4), 26, mC);
      drawBattery(); toolbarRedraw = false;
    }
  } else if (toolbarRedraw) {
    M5.Display.fillRoundRect(20, 10, sw-40, sh-110, 10, 0x1084); M5.Display.drawRoundRect(20, 10, sw-40, sh-110, 10, WHITE);
    M5.Display.setTextSize(3); M5.Display.drawCenterString("SLOT", sw/2, 25);
    for(int i=0; i<6; i++) {
      int px = 40+(i*45); uint16_t sc = (uiFileSlot==i+1)?YELLOW:WHITE;
      M5.Display.drawRect(px, 60, 40, 40, sc); M5.Display.drawCenterString(String(i+1), px+20, 70);
    }
    M5.Display.fillRoundRect(40, 120, 110, 50, 6, BLUE); M5.Display.drawCenterString("LOAD", 95, 135);
    M5.Display.fillRoundRect(sw-150, 120, 110, 50, 6, RED); M5.Display.drawCenterString("SAVE", sw-95, 135);
    toolbarRedraw = false;
  }
  M5.Display.endWrite();
}

void setup() {
  auto cfg = M5.config(); M5.begin(cfg);
  M5.Speaker.begin(); M5.Speaker.setVolume(masterVol);
  M5.Display.setRotation(3); initFreqs();
  M5.Display.fillScreen(BLACK);
  SD_MMC.begin("/sdcard", false);
  loadWav("/kick.wav", 0); loadWav("/snare.wav", 1); loadWav("/hihat.wav", 2); loadWav("/clap.wav", 3);
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 8192, NULL, 15, NULL, 1);
  fullRedraw = true; toolbarRedraw = true;
}

void loop() {
  M5.update();
  if (M5.Touch.getCount() > 0) {
    auto det = M5.Touch.getDetail(); int sw = M5.Display.width(), sh = M5.Display.height(), gh = sh-130, bW = sw/6;
    if (showFileMenu) {
      if (det.wasPressed()) {
        if (det.y > 60 && det.y < 100 && det.x > 40 && det.x < 300) { uiFileSlot = constrain(((det.x-40)/45)+1, 1, 6); toolbarRedraw = true; }
        else if (det.y > 120 && det.y < 170) { if (det.x < 155) handleFile(false); else if (det.x > sw-155) handleFile(true); }
        else { showFileMenu = false; fullRedraw = true; toolbarRedraw = true; }
      }
    } else {
      if (det.y < gh) {
        if (currentMode == 3) {
           int keyW = sw / 14, blackKeyW = keyW * 0.7, blackKeyH = gh * 0.6; int note = -1;
           for (int i = 0; i < 13; i++) {
             if (i == 2 || i == 6 || i == 9 || i == 13) continue;
             int bx = i * keyW + (keyW - blackKeyW / 2);
             if (det.x > bx && det.x < bx + blackKeyW && det.y < blackKeyH) {
               const int bNotes[] = {2, 4, -1, 7, 9, 11, -1, 14, 16, -1, 19, 21, 23};
               note = bNotes[i]; break;
             }
           }
           if (note == -1) note = whiteKeyNotes[constrain(det.x / keyW, 0, 13)];
           if (note > 0) {
             M5.Speaker.setChannelVolume(3, kbdVol);
             M5.Speaker.tone(freqs[note] * 2.0, 60, 3, true, square_wav, sizeof(square_wav));
           }
        } else if (det.x > sw-50) { scrollY = constrain(map(det.y, 20, gh-20, 0, 14), 0, 14); fullRedraw = true; }
        else if (det.wasPressed()) {
          int c = det.x / ((sw-50)/STEPS);
          if (currentMode==0) { 
            int r = det.y/(gh/4); 
            if(c<STEPS&&r<4) {
              drumGrid[r][c]=!drumGrid[r][c];
              if(drumGrid[r][c] && wavData[r]) M5.Speaker.playWav(wavData[r], wavLen[r], 1, 0, true); // PREVIEW DRM FR
            }
          } else { 
            int r = det.y/(gh/10), n = 24-(r+(int)scrollY); 
            if(c<STEPS&&n>=1) { 
              int* g=(currentMode==1)?bassGrid:melGrid; 
              g[c]=(g[c]==n)?0:n; 
              if(g[c]>0) { // PREVIEW BS/MLD FR
                M5.Speaker.setChannelVolume(currentMode, (currentMode==1?bassVol:melVol));
                M5.Speaker.tone(freqs[g[c]] * (currentMode==2?4.0:1.0), 100, currentMode, true, square_wav, sizeof(square_wav));
              }
            } 
          }
          fullRedraw = true;
        }
      } else if (det.y > gh+60 && det.x > bW*1.8 && det.x < sw-60) {
        int sx = bW*1.8+60, swl = sw-sx-100;
        int v = constrain(map(det.x, sx, sx+swl, 0, 255), 0, 255);
        if (currentMode==0) { masterVol=v; M5.Speaker.setVolume(v); } 
        else if(currentMode==1) bassVol=v; 
        else if(currentMode==2) melVol=v;
        else kbdVol=v;
        toolbarRedraw = true;
      } else if (det.wasPressed()) {
        if (det.y > gh+60 && det.x < bW*1.8) { isPlaying = false; showFileMenu = true; toolbarRedraw = true; }
        else if (det.y < gh+60 && det.y > gh) {
          if (det.x < bW) { isPlaying = !isPlaying; toolbarRedraw = true; }
          else if (det.x < bW*2) { if(currentMode==0) memset(drumGrid,0,64); else if(currentMode==1) memset(bassGrid,0,64); else if(currentMode==2) memset(melGrid,0,64); fullRedraw = true; }
          else if (det.x < bW*3.2) { currentMode = (currentMode+1)%4; fullRedraw = true; toolbarRedraw = true; }
          else if (det.x < bW*3.8) { bpm = max(40, bpm-5); toolbarRedraw = true; }
          else if (det.x > bW*4.4) { bpm = min(250, bpm+5); toolbarRedraw = true; }
        }
      }
    }
  }
  static unsigned long lb = 0; if (millis() - lb > 5000) { lb = millis(); toolbarRedraw = true; }
  drawUI();
}