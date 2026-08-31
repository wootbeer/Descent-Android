//
//  Motion.h
//  Descent
//
//  Created by Devin Tuchsen on 10/22/15.
//  Copyright © 2015 Devin Tuchsen. All rights reserved.
//

#ifndef Motion_h
#define Motion_h

extern void startMotion();
extern void stopMotion();
extern void getRotationRate(double *x, double *y, double *z);
extern int haveGyroscope();
extern void setRenderScaleIndex(int index);
extern void setAspectRatio43(int enabled);
extern void restartAppForSettingsChange();

#endif /* Motion_h */
