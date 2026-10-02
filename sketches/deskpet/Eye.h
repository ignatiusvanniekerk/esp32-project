#pragma once

struct Eye {
  float w, h, targetW, targetH;
  float pupilX, pupilY, targetPupilX, targetPupilY;
  float velW, velH, pVelX, pVelY;
  float k = 0.14f;
  float d = 0.62f;
  float pk = 0.10f;
  float pd = 0.52f;

  void init() {
    w = targetW = 44.0f;
    h = targetH = 30.0f;
    pupilX = pupilY = targetPupilX = targetPupilY = 0;
    velW = velH = pVelX = pVelY = 0;
  }

  void update() {
    velW = (velW + (targetW - w) * k) * d;
    velH = (velH + (targetH - h) * k) * d;
    w += velW;
    h += velH;

    pVelX = (pVelX + (targetPupilX - pupilX) * pk) * pd;
    pVelY = (pVelY + (targetPupilY - pupilY) * pk) * pd;
    pupilX += pVelX;
    pupilY += pVelY;
  }
};
