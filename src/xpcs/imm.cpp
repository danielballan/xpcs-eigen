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
#include "imm.h"
#include "configuration.h"
#include "benchmark.h"
#include "dark_image.h"

#include <stdio.h>
#include <iostream>

namespace xpcs {


IMM::IMM(const char* filename, int frameFrom, int frameTo, int pixelsPerFrame)
{
    m_data = NULL;
    m_filename = filename;
    m_frameStartTodo = frameFrom;
    m_frameEndTodo = frameTo;
    m_frames = frameTo - frameFrom + 1;
    m_pixelsPerFrame = pixelsPerFrame;
    
    Benchmark ben("Reading IMM file");

    init();

    if (m_ptrHeader == NULL)
    {
        printf("Failed to read IMM file : %s", m_filename);
        return;
    }

    if (m_pixelsPerFrame < 1)
        m_pixelsPerFrame = m_ptrHeader->rows * m_ptrHeader->cols;


    if (m_ptrHeader->compression) {
        load_sparse2();
        m_isSparse = true;
    } 
    else {
        load_nonsparse2();
        m_isSparse = false;
    }

}

IMM::~IMM()
{
    if (m_data)
        free(m_data);
}

void IMM::init()
{
    m_ptrFile = fopen(m_filename, "rb");
    if (m_ptrFile == NULL) return ; //TODO handle error

    m_ptrHeader = new IMMHeader();
    fread(m_ptrHeader, 1024, 1, m_ptrFile);

}

void IMM::load_nonsparse()
{
    short* buffer = new short[m_pixelsPerFrame];
    m_data = new float[m_frames * m_pixelsPerFrame];

    long pixelsInFrame = m_ptrHeader->rows * m_ptrHeader->cols;
    long bytesInFrame = pixelsInFrame * m_ptrHeader->bytes;
    long bytesPerFrame = m_pixelsPerFrame * m_ptrHeader->bytes;

    // If we are reading only a limited number of pixels.
    long skip = bytesInFrame - bytesPerFrame;

    long index = 0;
    int fcount = 0;

    rewind(m_ptrFile);

    while (fcount < m_frames)
    {
        fseek(m_ptrFile, 1024, SEEK_CUR);
        fread(buffer, 1, bytesPerFrame, m_ptrFile);

        if (skip != 0)
            fseek(m_ptrFile, skip, SEEK_CUR);

        std::copy(buffer, buffer+m_pixelsPerFrame, m_data+index);
        index += m_pixelsPerFrame;
        fcount++;
    }

    m_pixelData = Eigen::Map<Eigen::MatrixXf>(m_data, m_pixelsPerFrame, m_frames);

    delete(buffer);
}


void IMM::load_nonsparse2()
{
    Configuration *conf = Configuration::instance();

    short *pixelmask = conf->getPixelMask();
    int* sbinmask = conf->getSbinMask();
    double *flatfield = conf->getFlatField();
    float eff = conf->getDetEfficiency();
    float detAdhu = conf->getDetAdhuPhot();
    float preset =  conf->getDetPreset();
    int x = conf->getFrameWidth();
    int y = conf->getFrameHeight();

    float normFactor = conf->getNormFactor();

    int framesTodo = conf->getFrameTodoCount();
    int swindow = conf->getStaticWindowSize();
    int totalStaticPartns = conf->getTotalStaticPartitions();
    int partitions = (int) ceil((double)framesTodo/swindow);
    int darkStart = conf->getDarkFrameStart();
    int darkEnd = conf->getDarkFrameEnd();
    int darkFrames = conf->getDarkFrames();

    float threshold = conf->getDarkThreshold();
    float sigma = conf->getDarkSigma();

    int totalPixels = x * y;
    m_sdata = new ds::SparseData(totalPixels);

    m_timestampClock = new float[2*m_frames];
    m_timestampTick = new float[2*m_frames];

    rewind(m_ptrFile);

     // Row/Cols large enough to hold the index buffer
    // TODO: We can further reduce the memory requirement here 
    // through a better allocation scheme. 
    short* values = new short[m_pixelsPerFrame];
    m_frameSums = new float[2*m_frames];
    m_pixelSums = new float[totalPixels];
    m_partialPartitionMean = new float[totalStaticPartns * partitions];
    m_totalPartitionMean = new float[totalStaticPartns];
    float *pixcount = new float[totalStaticPartns];

    for (int i = 0; i < totalStaticPartns; i++)
    {
        pixcount[i] = 0.0;
        m_totalPartitionMean[i] = 0.0;
    }

    for (int i = 0; i < totalStaticPartns * partitions; i++)
        m_partialPartitionMean[i] = 0.0;

    for (int i = 0; i < totalPixels; i++)
    {
        pixcount[sbinmask[i] - 1]++;
    }

    for (int i = 0 ; i < totalPixels; i++)
        m_pixelSums[i] = 0.0f;

    int fcount = 0;
    int count = 0;

    // Skip frames below start frame. 
    while (fcount < darkStart)
    {
        fread(m_ptrHeader, 1024, 1, m_ptrFile);
        uint skip = m_ptrHeader->dlen;        
        fseek(m_ptrFile, skip * 2, SEEK_CUR);
        fcount++;
    }

    short** darkPixels = 0;
    if (darkFrames > 0)
        darkPixels = new short*[darkFrames];

    int darkIndex = 0;

    while (fcount < darkFrames)
    {
        fread(m_ptrHeader, 1024, 1, m_ptrFile);
        uint pixels = m_ptrHeader->dlen;        
        darkPixels[darkIndex] = new short[m_pixelsPerFrame];
        fread(darkPixels[darkIndex], pixels * 2, 1, m_ptrFile);
        darkIndex++;
        fcount++;
    }

    double* darkAvg =  NULL;
    double * darkStd = NULL;

    if (darkFrames > 0) 
    {
        DarkImage darkImage(darkPixels, darkFrames, m_pixelsPerFrame, flatfield);
        darkAvg = darkImage.getDarkAvg();
        darkStd = darkImage.getDarkStd();    
    }

    // Skip frames below start frame. 
    while (fcount < m_frameStartTodo)
    {
        fread(m_ptrHeader, 1024, 1, m_ptrFile);
        uint skip = m_ptrHeader->dlen;        
        fseek(m_ptrFile, skip * 2, SEEK_CUR);
        fcount++;
    }

    int partno = 0;
    int totalNonZeroPixles = 0;
    int framesInWindow = 0;
    while ((fcount - m_frameStartTodo) < m_frames)
    {
        fread(m_ptrHeader, 1024, 1, m_ptrFile);

        uint pixels = m_ptrHeader->dlen;        

        m_timestampClock[count] = count + 1;
        m_timestampClock[count+m_frames] = m_ptrHeader->elapsed;
        m_timestampTick[count] = count + 1;
        m_timestampTick[count+m_frames] = m_ptrHeader->corecotick;
        count++;

        fread(values, pixels * 2, 1, m_ptrFile);

        // TODO insert assert statements
        // /// - read pixels == requested pixels to read etc. 
        int fnumber = fcount - m_frameStartTodo;
        if (fnumber > 0 && (fnumber % swindow) == 0) {
            partno++;
            framesInWindow = 0;
        }

        framesInWindow++;
        float fsum = 0.0;
        totalNonZeroPixles = 0;
        for (int i = 0; i < pixels; i++)
        {
            // We want the sparse pixel index to be less the number of pixels requested.
            // if (index[i] >= m_pixelsPerFrame)
            //     break;

            if (pixelmask[i] != 0) {                
                // tripletList.push_back(Triplet(index[i], fnumber, values[i] *flatfield[index[i]]));       
                int pix = i;

                float val = values[i];
                float thresh = 0.0;

                if (darkAvg != NULL) {
                    val = values[i] - darkAvg[i];
                    val = std::max(val, 0.0f);
                    thresh = threshold + sigma * darkStd[i];
                }
                
                if (val <= thresh) continue; 

                val = val * (float) flatfield[i];    

                totalNonZeroPixles++;

                ds::Row* ptr = m_sdata->get(pix);
                ptr->indxPtr.push_back(fnumber);
                ptr->valPtr.push_back(val);
                fsum += val;
                m_pixelSums[pix] += val;

                int sbin = sbinmask[pix] - 1;
                m_totalPartitionMean[sbin] += val; 
                m_partialPartitionMean[partno * totalStaticPartns + sbin] += val;
            }
        }

        m_frameSums[fnumber] = fnumber + 1.0;
        m_frameSums[fnumber+m_frames] = fsum / totalNonZeroPixles;
        fcount++;
    }

    float denom = 1.0;
    for (int i = 0; i < totalStaticPartns; i++)
    {
        for (int j = 0; j < partitions; j++)
        {   
            denom = pixcount[i] * framesInWindow * normFactor;
            m_partialPartitionMean[j*totalStaticPartns + i] /= denom;
        }
    }
    for (int  i = 0; i < totalStaticPartns; i++) 
    {
        denom = pixcount[i] * framesTodo * normFactor;
        m_totalPartitionMean[i] /= denom;
    }

}

void IMM::load_sparse()
{
    //TODO:: Remove the dependence of IMM reader on configuration object. 
    Configuration *conf = Configuration::instance();
    short *pixelmask = conf->getPixelMask();
    double *flatfield = conf->getFlatField();
    m_timestampClock = new float[2*m_frames];
    m_timestampTick = new float[2*m_frames];

    rewind(m_ptrFile);

    int fcount = 0;
    int count = 0;
    typedef Eigen::Triplet<float> Triplet;

    std::vector<Triplet> tripletList;

    // Estimated reserve space. 
    //TODO: Make sparse factor configurable. 
    tripletList.reserve(m_frames * int(m_pixelsPerFrame*0.10));

    // Row/Cols large enough to hold the index buffer
    // TODO: We can further reduce the memory requirement here 
    // through a better allocation scheme. 
    int* index = new int[m_pixelsPerFrame];
    short* values = new short[m_pixelsPerFrame];

    // Skip frames below start frame. 
    while (fcount < m_frameStartTodo)
    {
        fread(m_ptrHeader, 1024, 1, m_ptrFile);
        uint skip = m_ptrHeader->dlen;        
        fseek(m_ptrFile, skip * 6, SEEK_CUR);
        fcount++;
    }

    while ((fcount - m_frameStartTodo) < m_frames)
    {
        fread(m_ptrHeader, 1024, 1, m_ptrFile);

        uint pixels = m_ptrHeader->dlen;        

        m_timestampClock[count] = count + 1;
        m_timestampClock[count+m_frames] = m_ptrHeader->elapsed;
        m_timestampTick[count] = count + 1;
        m_timestampTick[count+m_frames] = m_ptrHeader->corecotick;
        count++;

        uint skip = 0;
        
        if (pixels > m_pixelsPerFrame)
            skip = pixels - m_pixelsPerFrame;

        fread(index, pixels  * 4, 1, m_ptrFile);
        if (skip > 0) fseek(m_ptrFile, skip * 4, SEEK_CUR);
        fread(values, pixels * 2, 1, m_ptrFile);
        if (skip > 0) fseek(m_ptrFile, skip * 2, SEEK_CUR);

        // TODO insert assert statements
        /// - read pixels == requested pixels to read etc. 
        int fnumber = fcount - m_frameStartTodo;
        for (int i = 0; i < pixels; i++)
        {
            // We want the sparse pixel index to be less the number of pixels requested.
            // if (index[i] >= m_pixelsPerFrame)
            //     break;

            if (pixelmask[index[i]] != 0) {                
                tripletList.push_back(Triplet(index[i], fnumber, values[i] *flatfield[index[i]]));       
            }
        }

        fcount++;
    }


    m_sparsePixelData = SparseMatF(m_pixelsPerFrame, m_frames);
    m_sparsePixelData.setFromTriplets(tripletList.begin(), tripletList.end());
    m_sparsePixelData.makeCompressed();
}

void IMM::load_sparse2()
{
    //TODO:: Remove the dependence of IMM reader on configuration object. 
    Configuration *conf = Configuration::instance();
    short *pixelmask = conf->getPixelMask();
    int* sbinmask = conf->getSbinMask();
    double *flatfield = conf->getFlatField();
    float eff = conf->getDetEfficiency();
    float detAdhu = conf->getDetAdhuPhot();
    float preset =  conf->getDetPreset();
    int x = conf->getFrameWidth();
    int y = conf->getFrameHeight();

    float normFactor = conf->getNormFactor();

    int framesTodo = conf->getFrameTodoCount();
    int swindow = conf->getStaticWindowSize();
    int totalStaticPartns = conf->getTotalStaticPartitions();
    int partitions = (int) ceil((double)framesTodo/swindow);

    int totalPixels = x * y;
    m_sdata = new ds::SparseData(totalPixels);

    m_timestampClock = new float[2*m_frames];
    m_timestampTick = new float[2*m_frames];

    rewind(m_ptrFile);

    int fcount = 0;
    int count = 0;

    if (flatfield == NULL)
    {
        for (int i = 0; i < m_pixelsPerFrame; i++)
            flatfield[i] = 1.0;
    }


    // Row/Cols large enough to hold the index buffer
    // TODO: We can further reduce the memory requirement here 
    // through a better allocation scheme. 
    int* index = new int[m_pixelsPerFrame];
    short* values = new short[m_pixelsPerFrame];
    m_frameSums = new float[2*m_frames];
    m_pixelSums = new float[totalPixels];
    m_partialPartitionMean = new float[totalStaticPartns * partitions];
    m_totalPartitionMean = new float[totalStaticPartns];
    float *pixcount = new float[totalStaticPartns];

    for (int i = 0; i < totalStaticPartns; i++)
    {
        pixcount[i] = 0.0;
        m_totalPartitionMean[i] = 0.0;
    }

    for (int i = 0; i < totalStaticPartns * partitions; i++)
        m_partialPartitionMean[i] = 0.0;

    for (int i = 0; i < totalPixels; i++)
    {
        pixcount[sbinmask[i] - 1]++;
    }


    for (int i = 0 ; i < totalPixels; i++)
        m_pixelSums[i] = 0.0f;

    // Skip frames below start frame. 
    while (fcount < m_frameStartTodo)
    {
        fread(m_ptrHeader, 1024, 1, m_ptrFile);
        uint skip = m_ptrHeader->dlen;        
        fseek(m_ptrFile, skip * 6, SEEK_CUR);
        fcount++;
    }

    int partno = 0;
    while ((fcount - m_frameStartTodo) < m_frames)
    {
        fread(m_ptrHeader, 1024, 1, m_ptrFile);

        uint pixels = m_ptrHeader->dlen;        

        m_timestampClock[count] = count + 1;
        m_timestampClock[count+m_frames] = m_ptrHeader->elapsed;
        m_timestampTick[count] = count + 1;
        m_timestampTick[count+m_frames] = m_ptrHeader->corecotick;
        count++;

        uint skip = 0;
        
        if (pixels > m_pixelsPerFrame)
            skip = pixels - m_pixelsPerFrame;

        fread(index, pixels  * 4, 1, m_ptrFile);
        if (skip > 0) fseek(m_ptrFile, skip * 4, SEEK_CUR);
        fread(values, pixels * 2, 1, m_ptrFile);
        if (skip > 0) fseek(m_ptrFile, skip * 2, SEEK_CUR);

        // TODO insert assert statements
        // /// - read pixels == requested pixels to read etc. 
        int fnumber = fcount - m_frameStartTodo;
        if (fnumber > 0 && (fnumber % swindow) == 0)
            partno++;

        float fsum = 0.0;
        for (int i = 0; i < pixels; i++)
        {
            // We want the sparse pixel index to be less the number of pixels requested.
            // if (index[i] >= m_pixelsPerFrame)
            //     break;

            if (pixelmask[index[i]] != 0) {                
                // tripletList.push_back(Triplet(index[i], fnumber, values[i] *flatfield[index[i]]));       
                int pix = index[i];
                float val = values[i] * flatfield[index[i]];

                ds::Row* ptr = m_sdata->get(pix);
                ptr->indxPtr.push_back(fnumber);
                ptr->valPtr.push_back(val);
                fsum += val;
                m_pixelSums[pix] += val;

                int sbin = sbinmask[pix] - 1;
                m_totalPartitionMean[sbin] += val; 
                m_partialPartitionMean[partno * totalStaticPartns + sbin] += val;
            }
        }

        m_frameSums[fnumber] = fnumber + 1.0;
        m_frameSums[fnumber+m_frames] = fsum / totalPixels;
        fcount++;
    }

    float denom = 1.0;
    for (int i = 0; i < totalStaticPartns; i++)
    {
        for (int j = 0; j < partitions; j++)
        {   
            denom = pixcount[i] * swindow * normFactor;
            m_partialPartitionMean[j*totalStaticPartns + i] /= denom;
        }
    }
    for (int  i = 0; i < totalStaticPartns; i++) 
    {
        denom = pixcount[i] * framesTodo * normFactor;
        m_totalPartitionMean[i] /= denom;
    }

}

Eigen::MatrixXf IMM::getPixelData()
{
    return m_pixelData;
}

SparseRMatF IMM::getSparsePixelData()
{
    return m_sparsePixelData;
}

bool IMM::getIsSparse()
{
    return m_isSparse;
}

float* IMM::getTimestampClock()
{
    return m_timestampClock;
}

float* IMM::getTimestampTick()
{
    return m_timestampTick;
}

float* IMM::getFrameSums()
{
    return m_frameSums;
}

float* IMM::getPixelSums()
{
    return m_pixelSums;
}

ds::SparseData* IMM::getSparseData()
{
    return m_sdata;
}

float* IMM::getTotalPartitionMean()
{
    return m_totalPartitionMean;
}

float* IMM::getPartialPartitionMean()
{
    return m_partialPartitionMean;
}

}