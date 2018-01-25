/**

Copyright (c) 2016, UChicago Argonne, LLC. All rights reserved.

Copyright 2016. UChicago Argonne, LLC. This software was produced 
under U.S. Government contract DE-AC02-06CH11357 for Argonne National 
Laboratory (ANL), which is operated by UChicago Argonne, LLC for the 
U.S. Department of Energy. The U.S. Government has rights to use, 
reproduce, and distribute this software.  NEITHER THE GOVERNMENT NOR 
UChicago Argonne, LLC MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR a
ASSUMES ANY LIABILITY FOR THE USE OF THIS SOFTWARE.  If software is 
modified to produce derivative works, such modified software should 
be clearly marked, so as not to confuse it with the version available 
from ANL.

Additionally, redistribution and use in source and binary forms, with 
or without modification, are permitted provided that the following 
conditions are met:

    * Redistributions of source code must retain the above copyright 
      notice, this list of conditions and the following disclaimer. 

    * Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in 
      the documentation and/or other materials provided with the 
      distribution. 

    * Neither the name of UChicago Argonne, LLC, Argonne National 
      Laboratory, ANL, the U.S. Government, nor the names of its 
      contributors may be used to endorse or promote products derived 
      from this software without specific prior written permission. 

THIS SOFTWARE IS PROVIDED BY UChicago Argonne, LLC AND CONTRIBUTORS 
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL UChicago 
Argonne, LLC OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
POSSIBILITY OF SUCH DAMAGE.

**/

#include "dark_image.h"

#include <math.h>

#include "configuration.h"

namespace xpcs {

DarkImage::DarkImage(short** data, int frames, int pixelPerFrame, double* flatfield)
{
  darkAvg = new double[pixelPerFrame];
  darkStd = new double[pixelPerFrame];

  computeDarkStats(data, frames, pixelPerFrame, flatfield);
}

DarkImage::~DarkImage()
{
  //TODO
}

double* DarkImage::getDarkAvg() 
{
  return darkAvg;
}

double* DarkImage::getDarkStd() 
{
  return darkStd;
}

void DarkImage::computeDarkStats(short** data, int frames, int pixels, double* flatfield)
{

  for (int i = 0 ; i < pixels; i++)
  {
      darkAvg[i] = 0.0;
      darkStd[i] = 0.0;
  }

  for (int i = 0; i < frames; i++)
  {
      for (int j = 0; j < pixels; j++)
      {
          double tmp = darkAvg[j];
          double pix = (double)data[i][j] * flatfield[j];
          
          darkAvg[j] += ((pix - darkAvg[j]) / (double)(i+1));
          darkStd[j] += ((pix - tmp) * (pix - darkAvg[j]));

      }
  }

  for (int j = 0; j < pixels; j++)
      darkStd[j] = sqrt(darkStd[j] / frames);


}

}