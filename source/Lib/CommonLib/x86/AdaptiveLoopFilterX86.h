/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "CommonDefX86.h"
#include "../AdaptiveLoopFilter.h"

#ifdef TARGET_SIMD_X86
#if defined _MSC_VER
#include <tmmintrin.h>
#else
#include <x86intrin.h>
#endif

template<X86_VEXT vext>
static void simdDeriveClassificationBlk(AlfClassifier **classifier, int **laplacian[NUM_DIRECTIONS],
                                        const CPelBuf &srcLuma, const Area &blkDst, const Area &blk, const int shift,
#if JVET_O0625_ALF_PADDING
                                        const int vbCTUHeight, int vbPos, const int alfBryList[4] )
#else
                                        const int vbCTUHeight, int vbPos )
#endif
{
  CHECK((blk.height & 7) != 0, "Block height must be a multiple of 8");
  CHECK((blk.width & 7) != 0, "Block width must be a multiple of 8");
  CHECK((vbCTUHeight & (vbCTUHeight - 1)) != 0, "vbCTUHeight must be a power of 2");

  const size_t imgStride = srcLuma.stride;
  const Pel *  srcExt    = srcLuma.buf;

  const int imgHExtended = blk.height + 4;
  const int imgWExtended = blk.width + 4;

  const int posX = blk.pos().x;
  const int posY = blk.pos().y;
#if JVET_O0625_ALF_PADDING
  const __m128i mmZero = _mm_setzero_si128();
  const __m128i mmLut  = _mm_setr_epi8(0, 0, 0, 127, 63, 47, 31, 0, -1, 0, 0, 0, 0, 0, 0, 0);
#endif

  // 18x40 array
  uint16_t colSums[(AdaptiveLoopFilter::m_CLASSIFICATION_BLK_SIZE + 4) >> 1]
                  [AdaptiveLoopFilter::m_CLASSIFICATION_BLK_SIZE + 8];

  for (int i = 0; i < imgHExtended; i += 2)
  {
    const size_t offset = (i + posY - 3) * imgStride + posX - 3;

    const Pel *imgY0 = &srcExt[offset];
    const Pel *imgY1 = &srcExt[offset + imgStride];
    const Pel *imgY2 = &srcExt[offset + imgStride * 2];
    const Pel *imgY3 = &srcExt[offset + imgStride * 3];

    // pixel padding for gradient calculation
    int pos      = blkDst.pos().y - 2 + i;
    int posInCTU = pos & (vbCTUHeight - 1);
    if (pos > 0 && posInCTU == vbPos - 2)
    {
      imgY3 = imgY2;
    }
    else if (pos > 0 && posInCTU == vbPos)
    {
      imgY0 = imgY1;
    }

    __m128i prev = _mm_setzero_si128();

    for (int j = 0; j < imgWExtended; j += 8)
    {
      const __m128i x0 = _mm_loadu_si128((const __m128i *) (imgY0 + j));
      const __m128i x1 = _mm_loadu_si128((const __m128i *) (imgY1 + j));
      const __m128i x2 = _mm_loadu_si128((const __m128i *) (imgY2 + j));
      const __m128i x3 = _mm_loadu_si128((const __m128i *) (imgY3 + j));

      const __m128i x4 = _mm_loadu_si128((const __m128i *) (imgY0 + j + 2));
      const __m128i x5 = _mm_loadu_si128((const __m128i *) (imgY1 + j + 2));
      const __m128i x6 = _mm_loadu_si128((const __m128i *) (imgY2 + j + 2));
      const __m128i x7 = _mm_loadu_si128((const __m128i *) (imgY3 + j + 2));

      const __m128i nw = _mm_blend_epi16(x0, x1, 0xaa);
      const __m128i n  = _mm_blend_epi16(x0, x5, 0x55);
      const __m128i ne = _mm_blend_epi16(x4, x5, 0xaa);
      const __m128i w  = _mm_blend_epi16(x1, x2, 0xaa);
      const __m128i e  = _mm_blend_epi16(x5, x6, 0xaa);
      const __m128i sw = _mm_blend_epi16(x2, x3, 0xaa);
      const __m128i s  = _mm_blend_epi16(x2, x7, 0x55);
      const __m128i se = _mm_blend_epi16(x6, x7, 0xaa);

      __m128i c = _mm_blend_epi16(x1, x6, 0x55);
      c         = _mm_add_epi16(c, c);
      __m128i d = _mm_shuffle_epi8(c, _mm_setr_epi8(2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13));

      const __m128i ver = _mm_abs_epi16(_mm_sub_epi16(c, _mm_add_epi16(n, s)));
      const __m128i hor = _mm_abs_epi16(_mm_sub_epi16(d, _mm_add_epi16(w, e)));
      const __m128i di0 = _mm_abs_epi16(_mm_sub_epi16(d, _mm_add_epi16(nw, se)));
      const __m128i di1 = _mm_abs_epi16(_mm_sub_epi16(d, _mm_add_epi16(ne, sw)));

#if JVET_O0625_ALF_PADDING
      __m128i hv  = _mm_hadd_epi16(ver, hor);
      __m128i di  = _mm_hadd_epi16(di0, di1);
      int leftPos = blkDst.pos().x + j;
      if ( ( leftPos > 0 ) && ( leftPos == alfBryList[2] ) ) //left: cut the left one value 
      {
        hv = _mm_blend_epi16(hv, mmZero, 0x11);
        di = _mm_blend_epi16(di, mmZero, 0x11);
      }
      else if ( ( leftPos > 0 ) && ( leftPos == alfBryList[3] ) ) //right: cut the right values 
      {
        hv = _mm_blend_epi16(hv, mmZero, 0xEE);
        di = _mm_blend_epi16(di, mmZero, 0xEE);
      }
#else
      const __m128i hv  = _mm_hadd_epi16(ver, hor);
      const __m128i di  = _mm_hadd_epi16(di0, di1);
#endif
      const __m128i all = _mm_hadd_epi16(hv, di);

      const __m128i t = _mm_blend_epi16(all, prev, 0xaa);
      _mm_storeu_si128((__m128i *) &colSums[i >> 1][j], _mm_hadd_epi16(t, all));
      prev = all;
    }
  }

  for (int i = 0; i < (blk.height >> 1); i += 4)
  {
    for (int j = 0; j < blk.width; j += 8)
    {
      __m128i x0, x1, x2, x3, x4, x5, x6, x7;

#if JVET_O0625_ALF_PADDING
      uint32_t horBlkStride = 8, horBlkStride2 = 8, verBlkStride = 8, verBlkStride2 = 8;
      
      //left
      if ((alfBryList[2] != ALF_NONE_BOUNDARY) && (j + blkDst.pos().x == alfBryList[2]))
      {
        horBlkStride = 6;
      }

      //right
      if ((alfBryList[3] != ALF_NONE_BOUNDARY) && (j + blkDst.pos().x + 8 == alfBryList[3]))
      {
        horBlkStride2 = 6;
      }

      //top 4 lines
      const uint32_t topPos = 2 * i + blkDst.pos().y;
      x0 = _mm_loadu_si128((__m128i *) &colSums[i + 0][j + 4]);
      x1 = _mm_loadu_si128((__m128i *) &colSums[i + 1][j + 4]);
      x2 = _mm_loadu_si128((__m128i *) &colSums[i + 2][j + 4]);
      x3 = _mm_loadu_si128((__m128i *) &colSums[i + 3][j + 4]);

      if (alfBryList[0] != ALF_NONE_BOUNDARY && (topPos == alfBryList[0]))
      {
        x0 = _mm_setzero_si128();
        verBlkStride = 6;
      }
      else if ((topPos & (vbCTUHeight - 1)) == (vbPos - 4))
      {
        x3 = _mm_setzero_si128();
        verBlkStride = 6;
      }

      //bottom 4 lines
      const uint32_t topPos2 = topPos + 4;
      x4 = _mm_loadu_si128((__m128i *) &colSums[i + 2][j + 4]);
      x5 = _mm_loadu_si128((__m128i *) &colSums[i + 3][j + 4]);
      x6 = _mm_loadu_si128((__m128i *) &colSums[i + 4][j + 4]);
      x7 = _mm_loadu_si128((__m128i *) &colSums[i + 5][j + 4]);

      if (alfBryList[1] != ALF_NONE_BOUNDARY && (topPos2 == alfBryList[1] - 4))
      {
        x7 = _mm_setzero_si128();
        if (((alfBryList[1] - 4) & (vbCTUHeight - 1)) == vbPos)  //between vb and bottom boundary
        {
          x4 = _mm_setzero_si128();          
          verBlkStride2 = 4;
        }
        else
        {
          verBlkStride2 = 6;
        }
      }
      else if ((topPos2 & (vbCTUHeight - 1)) == vbPos)
      {
        x4 = _mm_setzero_si128();
        verBlkStride2 = 6;
      }
#else
      const uint32_t z = (2 * i + blkDst.pos().y) & (vbCTUHeight - 1);
      const uint32_t z2 = (2 * i + 4 + blkDst.pos().y) & (vbCTUHeight - 1);

      x0 = (z == vbPos) ? _mm_setzero_si128() : _mm_loadu_si128((__m128i *) &colSums[i + 0][j + 4]);
      x1 = _mm_loadu_si128((__m128i *) &colSums[i + 1][j + 4]);
      x2 = _mm_loadu_si128((__m128i *) &colSums[i + 2][j + 4]);
      x3 = (z == vbPos - 4) ? _mm_setzero_si128() : _mm_loadu_si128((__m128i *) &colSums[i + 3][j + 4]);

      x4 = (z2 == vbPos) ? _mm_setzero_si128() : _mm_loadu_si128((__m128i *) &colSums[i + 2][j + 4]);
      x5 = _mm_loadu_si128((__m128i *) &colSums[i + 3][j + 4]);
      x6 = _mm_loadu_si128((__m128i *) &colSums[i + 4][j + 4]);
      x7 = (z2 == vbPos - 4) ? _mm_setzero_si128() : _mm_loadu_si128((__m128i *) &colSums[i + 5][j + 4]);
#endif

      x0 = _mm_add_epi16(x0, x1);
      x2 = _mm_add_epi16(x2, x3);
      x4 = _mm_add_epi16(x4, x5);
      x6 = _mm_add_epi16(x6, x7);

      __m128i x0l = _mm_cvtepu16_epi32(x0);
      __m128i x0h = _mm_unpackhi_epi16(x0, _mm_setzero_si128());
      __m128i x2l = _mm_cvtepu16_epi32(x2);
      __m128i x2h = _mm_unpackhi_epi16(x2, _mm_setzero_si128());
      __m128i x4l = _mm_cvtepu16_epi32(x4);
      __m128i x4h = _mm_unpackhi_epi16(x4, _mm_setzero_si128());
      __m128i x6l = _mm_cvtepu16_epi32(x6);
      __m128i x6h = _mm_unpackhi_epi16(x6, _mm_setzero_si128());

      x0l = _mm_add_epi32(x0l, x2l);
      x4l = _mm_add_epi32(x4l, x6l);
      x0h = _mm_add_epi32(x0h, x2h);
      x4h = _mm_add_epi32(x4h, x6h);

      x2l = _mm_unpacklo_epi32(x0l, x4l);
      x2h = _mm_unpackhi_epi32(x0l, x4l);
      x6l = _mm_unpacklo_epi32(x0h, x4h);
      x6h = _mm_unpackhi_epi32(x0h, x4h);

      __m128i sumV  = _mm_unpacklo_epi32(x2l, x6l);
      __m128i sumH  = _mm_unpackhi_epi32(x2l, x6l);
      __m128i sumD0 = _mm_unpacklo_epi32(x2h, x6h);
      __m128i sumD1 = _mm_unpackhi_epi32(x2h, x6h);

      //      uint32_t tempAct = sumV + sumH;
      __m128i tempAct = _mm_add_epi32(sumV, sumH);

      //      const uint32_t activity = std::min<uint32_t>(15, tempAct * scale >> shift);
      //      static const uint8_t th[16] = { 0, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4 };
      //      uint8_t classIdx = th[activity];
#if JVET_O0625_ALF_PADDING
      __m128i verStrides = _mm_setr_epi32(verBlkStride, verBlkStride,  verBlkStride2, verBlkStride2);
      __m128i horStrides = _mm_setr_epi32(horBlkStride, horBlkStride2, horBlkStride,  horBlkStride2);
      __m128i alfArea    = _mm_mullo_epi16(verStrides, horStrides);
      const __m128i scaleValue = _mm_add_epi8(_mm_shuffle_epi8(mmLut, _mm_srli_epi32(_mm_add_epi32(alfArea, _mm_set1_epi32(4)), 3)), _mm_set1_epi32(65));
      __m128i activity = _mm_mullo_epi32(tempAct, scaleValue);
#else
      const uint32_t scale  = (z == vbPos - 4 || z == vbPos) ? 96 : 64;
      const uint32_t scale2 = (z2 == vbPos - 4 || z2 == vbPos) ? 96 : 64;
      __m128i activity = _mm_mullo_epi32(tempAct, _mm_unpacklo_epi64(_mm_set1_epi32(scale), _mm_set1_epi32(scale2)));
#endif
      activity         = _mm_srl_epi32(activity, _mm_cvtsi32_si128(shift));
      activity         = _mm_min_epi32(activity, _mm_set1_epi32(15));
      __m128i classIdx = _mm_shuffle_epi8(_mm_setr_epi8(0, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4), activity);

      //      if (sumV > sumH)
      //      {
      //        hv1       = sumV;
      //        hv0       = sumH;
      //        dirTempHV = 0;
      //      }
      //      else
      //      {
      //        hv1       = sumH;
      //        hv0       = sumV;
      //        dirTempHV = 1;
      //      }
      __m128i dirTempHVMinus1 = _mm_cmpgt_epi32(sumV, sumH);
      __m128i hv1             = _mm_max_epi32(sumV, sumH);
      __m128i hv0             = _mm_min_epi32(sumV, sumH);

      //      if (sumD0 > sumD1)
      //      {
      //        d1       = sumD0;
      //        d0       = sumD1;
      //        dirTempD = 0;
      //      }
      //      else
      //      {
      //        d1       = sumD1;
      //        d0       = sumD0;
      //        dirTempD = 1;
      //      }
      __m128i dirTempDMinus1 = _mm_cmpgt_epi32(sumD0, sumD1);
      __m128i d1             = _mm_max_epi32(sumD0, sumD1);
      __m128i d0             = _mm_min_epi32(sumD0, sumD1);

      //      int dirIdx;
      //      if (d1 * hv0 > hv1 * d0)
      //      {
      //        hvd1   = d1;
      //        hvd0   = d0;
      //        dirIdx = 0;
      //      }
      //      else
      //      {
      //        hvd1   = hv1;
      //        hvd0   = hv0;
      //        dirIdx = 2;
      //      }
      __m128i a      = _mm_xor_si128(_mm_mullo_epi32(d1, hv0), _mm_set1_epi32(0x80000000));
      __m128i b      = _mm_xor_si128(_mm_mullo_epi32(hv1, d0), _mm_set1_epi32(0x80000000));
      __m128i dirIdx = _mm_cmpgt_epi32(a, b);
      __m128i hvd1   = _mm_blendv_epi8(hv1, d1, dirIdx);
      __m128i hvd0   = _mm_blendv_epi8(hv0, d0, dirIdx);

      //      if (hvd1 * 2 > 9 * hvd0)
      //      {
      //        classIdx += (dirIdx + 2) * 5;
      //      }
      //      else if (hvd1 > 2 * hvd0)
      //      {
      //        classIdx += (dirIdx + 1) * 5;
      //      }
      __m128i strength1 = _mm_cmpgt_epi32(hvd1, _mm_add_epi32(hvd0, hvd0));
      __m128i strength2 = _mm_cmpgt_epi32(_mm_add_epi32(hvd1, hvd1), _mm_add_epi32(hvd0, _mm_slli_epi32(hvd0, 3)));
      __m128i offset    = _mm_and_si128(strength1, _mm_set1_epi32(5));
      classIdx          = _mm_add_epi32(classIdx, offset);
      classIdx          = _mm_add_epi32(classIdx, _mm_and_si128(strength2, _mm_set1_epi32(5)));
      offset            = _mm_andnot_si128(dirIdx, offset);
      offset            = _mm_add_epi32(offset, offset);
      classIdx          = _mm_add_epi32(classIdx, offset);

      //      uint8_t transposeIdx = 2 * dirTempD + dirTempHV;
      __m128i transposeIdx = _mm_set1_epi32(3);
      transposeIdx         = _mm_add_epi32(transposeIdx, dirTempHVMinus1);
      transposeIdx         = _mm_add_epi32(transposeIdx, dirTempDMinus1);
      transposeIdx         = _mm_add_epi32(transposeIdx, dirTempDMinus1);

      int yOffset = 2 * i + blkDst.pos().y;
      int xOffset = j + blkDst.pos().x;

      static_assert(sizeof(AlfClassifier) == 2, "ALFClassifier type must be 16 bits wide");
      __m128i v;
      v = _mm_unpacklo_epi8(classIdx, transposeIdx);
      v = _mm_shuffle_epi8(v, _mm_setr_epi8(0, 1, 0, 1, 0, 1, 0, 1, 8, 9, 8, 9, 8, 9, 8, 9));
      _mm_storeu_si128((__m128i *) (classifier[yOffset] + xOffset), v);
      _mm_storeu_si128((__m128i *) (classifier[yOffset + 1] + xOffset), v);
      _mm_storeu_si128((__m128i *) (classifier[yOffset + 2] + xOffset), v);
      _mm_storeu_si128((__m128i *) (classifier[yOffset + 3] + xOffset), v);
      v = _mm_unpackhi_epi8(classIdx, transposeIdx);
      v = _mm_shuffle_epi8(v, _mm_setr_epi8(0, 1, 0, 1, 0, 1, 0, 1, 8, 9, 8, 9, 8, 9, 8, 9));
      _mm_storeu_si128((__m128i *) (classifier[yOffset + 4] + xOffset), v);
      _mm_storeu_si128((__m128i *) (classifier[yOffset + 5] + xOffset), v);
      _mm_storeu_si128((__m128i *) (classifier[yOffset + 6] + xOffset), v);
      _mm_storeu_si128((__m128i *) (classifier[yOffset + 7] + xOffset), v);
    }
  }
}

template<X86_VEXT vext>
#if JVET_O0625_ALF_PADDING
static void simdFilter5x5Blk(AlfClassifier **classifier, const PelUnitBuf &recDst, const CPelUnitBuf &recSrc,
  const Area &blkDst, const Area &blk, const ComponentID compId, const short *filterSet,
  const short *fClipSet, const ClpRng &clpRng, CodingStructure &cs, const int vbCTUHeight,
  int vbPos, const int alfBryList[4])
#else
static void simdFilter5x5Blk(AlfClassifier **classifier, const PelUnitBuf &recDst, const CPelUnitBuf &recSrc,
  const Area &blkDst, const Area &blk, const ComponentID compId, const short *filterSet,
  const short *fClipSet, const ClpRng &clpRng, CodingStructure &cs, const int vbCTUHeight,
  int vbPos)
#endif

{
  CHECK((vbCTUHeight & (vbCTUHeight - 1)) != 0, "vbCTUHeight must be a power of 2");
  CHECK(!isChroma(compId), "ALF 5x5 filter is for chroma only");

#if !JVET_O0525_REMOVE_PCM
  const SPS*     sps = cs.slice->getSPS();
  bool isDualTree = CS::isDualITree(cs);
  bool isPCMFilterDisabled = sps->getPCMFilterDisableFlag();
  ChromaFormat nChromaFormat = sps->getChromaFormatIdc();
#endif

  const CPelBuf srcBuffer = recSrc.get(compId);
  PelBuf        dstBuffer = recDst.get(compId);

  const size_t srcStride = srcBuffer.stride;
  const size_t dstStride = dstBuffer.stride;

  constexpr int SHIFT = AdaptiveLoopFilter::m_NUM_BITS - 1;
  constexpr int ROUND = 1 << (SHIFT - 1);

  const size_t width  = blk.width;
  const size_t height = blk.height;

  constexpr size_t STEP_X = 8;
  constexpr size_t STEP_Y = 4;

  CHECK(blk.y % STEP_Y, "Wrong startHeight in filtering");
  CHECK(blk.x % STEP_X, "Wrong startWidth in filtering");
  CHECK(height % STEP_Y, "Wrong endHeight in filtering");
  CHECK(width % 4, "Wrong endWidth in filtering");

  const Pel *src = srcBuffer.buf + blk.y * srcStride + blk.x;
  Pel *      dst = dstBuffer.buf + blkDst.y * dstStride + blkDst.x;

#if JVET_O0625_ALF_PADDING
  const SPS*     sps = cs.slice->getSPS();
  const bool bChroma = true;
  ChromaFormat nChromaFormat = sps->getChromaFormatIdc();
  const int chromaScaleY = getComponentScaleY(bChroma ? COMPONENT_Cb : COMPONENT_Y, nChromaFormat);
  const int chromaScaleX = getComponentScaleX(bChroma ? COMPONENT_Cb : COMPONENT_Y, nChromaFormat);
  const int alfTopBryPos = (alfBryList[0] != ALF_NONE_BOUNDARY) ? (alfBryList[0] >> chromaScaleY) : ALF_NONE_BOUNDARY;
  const int alfBotBryPos = (alfBryList[1] != ALF_NONE_BOUNDARY) ? (alfBryList[1] >> chromaScaleY) : ALF_NONE_BOUNDARY;
  const int alfLeftBryPos = (alfBryList[2] != ALF_NONE_BOUNDARY) ? (alfBryList[2] >> chromaScaleX) : ALF_NONE_BOUNDARY;
  const int alfRightBryPos = (alfBryList[3] != ALF_NONE_BOUNDARY) ? (alfBryList[3] >> chromaScaleX) : ALF_NONE_BOUNDARY;
  const char maskArray[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 6, 7, 10, 11, 14, 15, };
  const __m128i mmMask = _mm_lddqu_si128((__m128i*) maskArray);
  int botBryLines = ((((alfBotBryPos - (4 >> chromaScaleY)) & (vbCTUHeight - 1)) == vbPos) && alfBotBryPos != ALF_NONE_BOUNDARY) ? 1 : 2;
#endif

#if !JVET_O0525_REMOVE_PCM
  bool pcmFlags2x2[8] = {0,0,0,0,0,0,0,0};
  Pel  pcmRec2x2[32];
#endif

  const __m128i mmOffset = _mm_set1_epi32(ROUND);
  const __m128i mmMin = _mm_set1_epi16( clpRng.min );
  const __m128i mmMax = _mm_set1_epi16( clpRng.max );

  __m128i params[2][3];
  __m128i fs   = _mm_loadu_si128((__m128i *) filterSet);
  params[0][0] = _mm_shuffle_epi32(fs, 0x00);
  params[0][1] = _mm_shuffle_epi32(fs, 0x55);
  params[0][2] = _mm_shuffle_epi32(fs, 0xaa);
  __m128i fc   = _mm_loadu_si128((__m128i *) fClipSet);
  params[1][0] = _mm_shuffle_epi32(fc, 0x00);
  params[1][1] = _mm_shuffle_epi32(fc, 0x55);
  params[1][2] = _mm_shuffle_epi32(fc, 0xaa);

  for (size_t i = 0; i < height; i += STEP_Y)
  {
    for (size_t j = 0; j < width; j += STEP_X)
    {
#if !JVET_O0525_REMOVE_PCM
      if (isPCMFilterDisabled)
      {
        size_t blkX, blkY;
        bool *flags  = pcmFlags2x2;
        Pel  *pcmRec = pcmRec2x2;

        // check which chroma 2x2 blocks use PCM
        // chroma PCM may not be aligned with 4x4 ALF processing grid
        for (blkY = 0; blkY < STEP_Y; blkY += 2)
        {
          for (blkX = 0; blkX < STEP_X; blkX += 2)
          {
            Position pos((PosType)(j + blkDst.x + blkX), (PosType)(i + blkDst.y + blkY));
#if JVET_O0090_ALF_CHROMA_FILTER_ALTERNATIVES_CTB && !JVET_O0050_LOCAL_DUAL_TREE
            const CodingUnit* cu = isDualTree ? cs.getCU(pos, CH_C) : cs.getCU(recalcPosition(nChromaFormat, CH_C, CH_L, pos), CH_L);
#else
            CodingUnit* cu = isDualTree ? cs.getCU(pos, CH_C) : cs.getCU(recalcPosition(nChromaFormat, CH_C, CH_L, pos), CH_L);
#endif
#if JVET_O0050_LOCAL_DUAL_TREE
            cu = cu != NULL && cu->isSepTree() ? cs.getCU( pos, CH_C ) : cu;
#endif
            if(cu != NULL)
            {
              *flags++ = cu->ipcm ? 1 : 0;
            }
            else
            {
              *flags++ = 0;
            }

            // save original samples from 2x2 PCM blocks
            if( cu != NULL && cu->ipcm )
            {
              *pcmRec++ = dst[(blkY + 0) * dstStride + (blkX + 0) + j];
              *pcmRec++ = dst[(blkY + 0) * dstStride + (blkX + 1) + j];
              *pcmRec++ = dst[(blkY + 1) * dstStride + (blkX + 0) + j];
              *pcmRec++ = dst[(blkY + 1) * dstStride + (blkX + 1) + j];
            }
          }
        }
      }
#endif

      for (size_t ii = 0; ii < STEP_Y; ii++)
      {
        const Pel *pImg0, *pImg1, *pImg2, *pImg3, *pImg4;

        pImg0 = src + j + ii * srcStride;
        pImg1 = pImg0 + srcStride;
        pImg2 = pImg0 - srcStride;
        pImg3 = pImg1 + srcStride;
        pImg4 = pImg2 - srcStride;

#if JVET_O0625_ALF_PADDING
        const int yV = (int)(blkDst.y + i + ii);
#endif
        const int yVb = (blkDst.y + i + ii) & (vbCTUHeight - 1);
        if (yVb < vbPos && (yVb >= vbPos - 2))   // above
        {
          pImg1 = (yVb == vbPos - 1) ? pImg0 : pImg1;
          pImg3 = (yVb >= vbPos - 2) ? pImg1 : pImg3;

          pImg2 = (yVb == vbPos - 1) ? pImg0 : pImg2;
          pImg4 = (yVb >= vbPos - 2) ? pImg2 : pImg4;
        }
#if JVET_O0625_ALF_PADDING
        else if (alfBotBryPos != ALF_NONE_BOUNDARY && yV < alfBotBryPos && yV >= alfBotBryPos - botBryLines) //above
        {
          pImg1 = (yV == alfBotBryPos - 1) ? pImg0 : pImg1;
          pImg3 = (yV >= alfBotBryPos - 2) ? pImg1 : pImg3;

          pImg2 = (yV == alfBotBryPos - 1) ? pImg0 : pImg2;
          pImg4 = (yV >= alfBotBryPos - 2) ? pImg2 : pImg4;
        }
        else if (alfTopBryPos != ALF_NONE_BOUNDARY && yV >= alfTopBryPos && yV <= alfTopBryPos + 1) //bottom
        {
          pImg1 = (yV == alfTopBryPos) ? pImg0 : pImg1;
          pImg3 = (yV <= alfTopBryPos + 1) ? pImg1 : pImg3;

          pImg2 = (yV == alfTopBryPos) ? pImg0 : pImg2;
          pImg4 = (yV <= alfTopBryPos + 1) ? pImg2 : pImg4;
        }
#endif
        else if (yVb >= vbPos && (yVb <= vbPos + 1))   // bottom
        {
          pImg2 = (yVb == vbPos) ? pImg0 : pImg2;
          pImg4 = (yVb <= vbPos + 1) ? pImg2 : pImg4;

          pImg1 = (yVb == vbPos) ? pImg0 : pImg1;
          pImg3 = (yVb <= vbPos + 1) ? pImg1 : pImg3;
        }
        __m128i cur = _mm_loadu_si128((const __m128i *) pImg0);
#if JVET_O0625_ALF_PADDING
        // vertical 2-side padding
        bool leftBry = alfLeftBryPos != ALF_NONE_BOUNDARY && (blkDst.x + j) >= alfLeftBryPos && (blkDst.x + j) <= alfLeftBryPos + 1;
        bool rightBry = alfRightBryPos != ALF_NONE_BOUNDARY && (blkDst.x + j) < alfRightBryPos && (blkDst.x + j + 8) >= alfRightBryPos;
        __m128i xmm00 = _mm_lddqu_si128((__m128i*) (pImg3 + 0));
        __m128i xmm01 = _mm_lddqu_si128((__m128i*) (pImg4 + 0));
        __m128i xmm10 = _mm_lddqu_si128((__m128i*) (pImg1 + 1));
        __m128i xmm11 = _mm_lddqu_si128((__m128i*) (pImg2 - 1));
        __m128i xmm20 = _mm_lddqu_si128((__m128i*) (pImg1 + 0));
        __m128i xmm21 = _mm_lddqu_si128((__m128i*) (pImg2 + 0));
        __m128i xmm30 = _mm_lddqu_si128((__m128i*) (pImg1 - 1));
        __m128i xmm31 = _mm_lddqu_si128((__m128i*) (pImg2 + 1));
        __m128i xmm40 = _mm_lddqu_si128((__m128i*) (pImg0 + 2));
        __m128i xmm41 = _mm_lddqu_si128((__m128i*) (pImg0 - 2));
        __m128i xmm50 = _mm_lddqu_si128((__m128i*) (pImg0 + 1));
        __m128i xmm51 = _mm_lddqu_si128((__m128i*) (pImg0 - 1));
        if (leftBry)
        {
          xmm10 = _mm_blend_epi16(xmm10, xmm20, 0x01);
          xmm31 = _mm_blend_epi16(xmm31, xmm21, 0x01);
          __m128i xmm40Tmp = _mm_shufflelo_epi16(cur, 0xe8);
          xmm40 = _mm_blend_epi16(xmm40, xmm40Tmp, 0x03);
          xmm50 = _mm_blend_epi16(xmm50, cur, 0x01);
        }
        if (rightBry)
        {
          xmm11 = _mm_blend_epi16(xmm11, xmm21, 0x80);
          xmm30 = _mm_blend_epi16(xmm30, xmm20, 0x80);
          __m128i xmm41Tmp = _mm_shuffle_epi8(cur, mmMask);
          xmm41 = _mm_blend_epi16(xmm41, xmm41Tmp, 0xC0);
          xmm51 = _mm_blend_epi16(xmm51, cur, 0x80);
        }        
#endif
        __m128i accumA = mmOffset;
        __m128i accumB = mmOffset;

#if JVET_O0625_ALF_PADDING
        auto process2coeffs = [&](const int i, __m128i ptr0, __m128i ptr1, __m128i ptr2, __m128i ptr3) {
          const __m128i val00 = _mm_sub_epi16(ptr0, cur);
          const __m128i val10 = _mm_sub_epi16(ptr2, cur);
          const __m128i val01 = _mm_sub_epi16(ptr1, cur);
          const __m128i val11 = _mm_sub_epi16(ptr3, cur);
#else
        auto process2coeffs = [&](const int i, const Pel *ptr0, const Pel *ptr1, const Pel *ptr2, const Pel *ptr3) {
          const __m128i val00 = _mm_sub_epi16(_mm_loadu_si128((const __m128i *) ptr0), cur);
          const __m128i val10 = _mm_sub_epi16(_mm_loadu_si128((const __m128i *) ptr2), cur);
          const __m128i val01 = _mm_sub_epi16(_mm_loadu_si128((const __m128i *) ptr1), cur);
          const __m128i val11 = _mm_sub_epi16(_mm_loadu_si128((const __m128i *) ptr3), cur);
#endif
          __m128i val01A = _mm_unpacklo_epi16(val00, val10);
          __m128i val01B = _mm_unpackhi_epi16(val00, val10);
          __m128i val01C = _mm_unpacklo_epi16(val01, val11);
          __m128i val01D = _mm_unpackhi_epi16(val01, val11);

          __m128i limit01A = params[1][i];

          val01A = _mm_min_epi16(val01A, limit01A);
          val01B = _mm_min_epi16(val01B, limit01A);
          val01C = _mm_min_epi16(val01C, limit01A);
          val01D = _mm_min_epi16(val01D, limit01A);

          limit01A = _mm_sub_epi16(_mm_setzero_si128(), limit01A);

          val01A = _mm_max_epi16(val01A, limit01A);
          val01B = _mm_max_epi16(val01B, limit01A);
          val01C = _mm_max_epi16(val01C, limit01A);
          val01D = _mm_max_epi16(val01D, limit01A);

          val01A = _mm_add_epi16(val01A, val01C);
          val01B = _mm_add_epi16(val01B, val01D);

          __m128i coeff01A = params[0][i];

          accumA = _mm_add_epi32(accumA, _mm_madd_epi16(val01A, coeff01A));
          accumB = _mm_add_epi32(accumB, _mm_madd_epi16(val01B, coeff01A));
        };

#if JVET_O0625_ALF_PADDING
        process2coeffs(0, xmm00, xmm01, xmm10, xmm11);
        process2coeffs(1, xmm20, xmm21, xmm30, xmm31);
        process2coeffs(2, xmm40, xmm41, xmm50, xmm51);
#else
        process2coeffs(0, pImg3 + 0, pImg4 + 0, pImg1 + 1, pImg2 - 1);
        process2coeffs(1, pImg1 + 0, pImg2 + 0, pImg1 - 1, pImg2 + 1);
        process2coeffs(2, pImg0 + 2, pImg0 - 2, pImg0 + 1, pImg0 - 1);
#endif
        accumA = _mm_srai_epi32(accumA, SHIFT);
        accumB = _mm_srai_epi32(accumB, SHIFT);
        accumA = _mm_packs_epi32(accumA, accumB);
        accumA = _mm_add_epi16(accumA, cur);
        accumA = _mm_min_epi16(mmMax, _mm_max_epi16(accumA, mmMin));

        if (j + STEP_X <= width)
        {
          _mm_storeu_si128((__m128i *) (dst + ii * dstStride + j), accumA);
        }
        else
        {
          _mm_storel_epi64((__m128i *) (dst + ii * dstStride + j), accumA);
        }
      }
#if !JVET_O0525_REMOVE_PCM
      // restore 2x2 PCM chroma blocks
      if (isPCMFilterDisabled)
      {
        size_t blkX, blkY;
        bool *flags  = pcmFlags2x2;
        Pel  *pcmRec = pcmRec2x2;
        for (blkY = 0; blkY < STEP_Y; blkY += 2)
        {
          for (blkX = 0; blkX < STEP_X; blkX += 2)
          {
            if( *flags++ )
            {
              dst[(blkY + 0) * dstStride + (blkX + 0) + j] = *pcmRec++;
              dst[(blkY + 0) * dstStride + (blkX + 1) + j] = *pcmRec++;
              dst[(blkY + 1) * dstStride + (blkX + 0) + j] = *pcmRec++;
              dst[(blkY + 1) * dstStride + (blkX + 1) + j] = *pcmRec++;
            }
          }
        }
      }
#endif

    }

    src += srcStride * STEP_Y;
    dst += dstStride * STEP_Y;
  }
}

constexpr uint16_t sh(int x)
{
  return 0x0202 * (x & 7) + 0x0100 + 0x1010 * (x & 8);
}

static const uint16_t shuffleTab[4][2][8] = {
  {
    { sh(0), sh(1), sh(2), sh(3), sh(4), sh(5), sh(6), sh(7) },
    { sh(8), sh(9), sh(10), sh(11), sh(12), sh(13), sh(14), sh(15) },
  },
  {
    { sh(9), sh(4), sh(10), sh(8), sh(1), sh(5), sh(11), sh(7) },
    { sh(3), sh(0), sh(2), sh(6), sh(12), sh(13), sh(14), sh(15) },
  },
  {
    { sh(0), sh(3), sh(2), sh(1), sh(8), sh(7), sh(6), sh(5) },
    { sh(4), sh(9), sh(10), sh(11), sh(12), sh(13), sh(14), sh(15) },
  },
  {
    { sh(9), sh(8), sh(10), sh(4), sh(3), sh(7), sh(11), sh(5) },
    { sh(1), sh(0), sh(2), sh(6), sh(12), sh(13), sh(14), sh(15) },
  },
};

template<X86_VEXT vext>
#if JVET_O0625_ALF_PADDING
static void simdFilter7x7Blk(AlfClassifier **classifier, const PelUnitBuf &recDst, const CPelUnitBuf &recSrc,
  const Area &blkDst, const Area &blk, const ComponentID compId, const short *filterSet,
  const short *fClipSet, const ClpRng &clpRng, CodingStructure &cs, const int vbCTUHeight,
  int vbPos, const int alfBryList[4])
#else
static void simdFilter7x7Blk(AlfClassifier **classifier, const PelUnitBuf &recDst, const CPelUnitBuf &recSrc,
  const Area &blkDst, const Area &blk, const ComponentID compId, const short *filterSet,
  const short *fClipSet, const ClpRng &clpRng, CodingStructure &cs, const int vbCTUHeight,
  int vbPos)
#endif
{
  CHECK((vbCTUHeight & (vbCTUHeight - 1)) != 0, "vbCTUHeight must be a power of 2");
  CHECK(isChroma(compId), "7x7 ALF filter is meant for luma only");

#if !JVET_O0525_REMOVE_PCM
  const SPS *sps = cs.slice->getSPS();

  bool isPCMFilterDisabled = sps->getPCMFilterDisableFlag();
#endif

  const CPelBuf srcBuffer = recSrc.get(compId);
  PelBuf        dstBuffer = recDst.get(compId);

  const size_t srcStride = srcBuffer.stride;
  const size_t dstStride = dstBuffer.stride;

  constexpr int SHIFT = AdaptiveLoopFilter::m_NUM_BITS - 1;
  constexpr int ROUND = 1 << (SHIFT - 1);

  const size_t width  = blk.width;
  const size_t height = blk.height;

  constexpr size_t STEP_X = 8;
  constexpr size_t STEP_Y = 4;

  CHECK(blk.y % STEP_Y, "Wrong startHeight in filtering");
  CHECK(blk.x % STEP_X, "Wrong startWidth in filtering");
  CHECK(height % STEP_Y, "Wrong endHeight in filtering");
  CHECK(width % STEP_X, "Wrong endWidth in filtering");

  const Pel *src = srcBuffer.buf + blk.y * srcStride + blk.x;
  Pel *      dst = dstBuffer.buf + blkDst.y * dstStride + blkDst.x;

  const __m128i mmOffset = _mm_set1_epi32(ROUND);
  const __m128i mmMin = _mm_set1_epi16( clpRng.min );
  const __m128i mmMax = _mm_set1_epi16( clpRng.max );

#if JVET_O0625_ALF_PADDING
  const int alfTopBryPos = (alfBryList[0] != ALF_NONE_BOUNDARY) ? alfBryList[0] : ALF_NONE_BOUNDARY;
  const int alfBotBryPos = (alfBryList[1] != ALF_NONE_BOUNDARY) ? alfBryList[1] : ALF_NONE_BOUNDARY;
  const int alfLeftBryPos = (alfBryList[2] != ALF_NONE_BOUNDARY) ? alfBryList[2] : ALF_NONE_BOUNDARY;
  const int alfRightBryPos = (alfBryList[3] != ALF_NONE_BOUNDARY) ? alfBryList[3] : ALF_NONE_BOUNDARY;
  const char mask1Array[16] = { 0, 1, 4, 5, 8, 9, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, };
  const char mask2Array[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 6, 7, 10, 11, 14, 15, };
  const char mask3Array[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 6, 7, 10, 11, 14, 15, };
  const __m128i mmMask1 = _mm_lddqu_si128((__m128i*) mask1Array);
  const __m128i mmMask2 = _mm_lddqu_si128((__m128i*) mask2Array);
  const __m128i mmMask3 = _mm_lddqu_si128((__m128i*) mask3Array);

  int botBryLines = ((((alfBotBryPos - 4) & (vbCTUHeight - 1)) == vbPos) && alfBotBryPos != ALF_NONE_BOUNDARY) ? 2 : 4;
#endif

  for (size_t i = 0; i < height; i += STEP_Y)
  {
    const AlfClassifier *pClass = classifier[blkDst.y + i] + blkDst.x;

    for (size_t j = 0; j < width; j += STEP_X)
    {
      __m128i params[2][2][6];

      for (int k = 0; k < 2; ++k)
      {
        const AlfClassifier &cl = pClass[j + 4 * k];

        const int transposeIdx = cl.transposeIdx;
        const int classIdx     = cl.classIdx;

        static_assert(sizeof(*filterSet) == 2, "ALF coeffs must be 16-bit wide");
        static_assert(sizeof(*fClipSet) == 2, "ALF clip values must be 16-bit wide");

        __m128i rawCoeff0, rawCoeff1;
        __m128i rawClip0, rawClip1;

#if !JVET_O0525_REMOVE_PCM
        if (isPCMFilterDisabled && classIdx == AdaptiveLoopFilter::m_ALF_UNUSED_CLASSIDX
            && transposeIdx == AdaptiveLoopFilter::m_ALF_UNUSED_TRANSPOSIDX)
        {
          rawCoeff0 = _mm_setzero_si128();
          rawCoeff1 = _mm_setzero_si128();
          rawClip0  = _mm_setzero_si128();
          rawClip1  = _mm_setzero_si128();
        }
        else
        {
#endif
          rawCoeff0 = _mm_loadu_si128((const __m128i *) (filterSet + classIdx * MAX_NUM_ALF_LUMA_COEFF));
          rawCoeff1 = _mm_loadl_epi64((const __m128i *) (filterSet + classIdx * MAX_NUM_ALF_LUMA_COEFF + 8));

          rawClip0 = _mm_loadu_si128((const __m128i *) (fClipSet + classIdx * MAX_NUM_ALF_LUMA_COEFF));
          rawClip1 = _mm_loadl_epi64((const __m128i *) (fClipSet + classIdx * MAX_NUM_ALF_LUMA_COEFF + 8));
#if !JVET_O0525_REMOVE_PCM
        }
#endif

        const __m128i s0 = _mm_loadu_si128((const __m128i *) shuffleTab[transposeIdx][0]);
        const __m128i s1 = _mm_xor_si128(s0, _mm_set1_epi8((char) 0x80));
        const __m128i s2 = _mm_loadu_si128((const __m128i *) shuffleTab[transposeIdx][1]);
        const __m128i s3 = _mm_xor_si128(s2, _mm_set1_epi8((char) 0x80));

        const __m128i rawCoeffLo = _mm_or_si128(_mm_shuffle_epi8(rawCoeff0, s0), _mm_shuffle_epi8(rawCoeff1, s1));
        const __m128i rawCoeffHi = _mm_or_si128(_mm_shuffle_epi8(rawCoeff0, s2), _mm_shuffle_epi8(rawCoeff1, s3));
        const __m128i rawClipLo  = _mm_or_si128(_mm_shuffle_epi8(rawClip0, s0), _mm_shuffle_epi8(rawClip1, s1));
        const __m128i rawClipHi  = _mm_or_si128(_mm_shuffle_epi8(rawClip0, s2), _mm_shuffle_epi8(rawClip1, s3));

        params[k][0][0] = _mm_shuffle_epi32(rawCoeffLo, 0x00);
        params[k][0][1] = _mm_shuffle_epi32(rawCoeffLo, 0x55);
        params[k][0][2] = _mm_shuffle_epi32(rawCoeffLo, 0xaa);
        params[k][0][3] = _mm_shuffle_epi32(rawCoeffLo, 0xff);
        params[k][0][4] = _mm_shuffle_epi32(rawCoeffHi, 0x00);
        params[k][0][5] = _mm_shuffle_epi32(rawCoeffHi, 0x55);
        params[k][1][0] = _mm_shuffle_epi32(rawClipLo, 0x00);
        params[k][1][1] = _mm_shuffle_epi32(rawClipLo, 0x55);
        params[k][1][2] = _mm_shuffle_epi32(rawClipLo, 0xaa);
        params[k][1][3] = _mm_shuffle_epi32(rawClipLo, 0xff);
        params[k][1][4] = _mm_shuffle_epi32(rawClipHi, 0x00);
        params[k][1][5] = _mm_shuffle_epi32(rawClipHi, 0x55);
      }

      for (size_t ii = 0; ii < STEP_Y; ii++)
      {
        const Pel *pImg0, *pImg1, *pImg2, *pImg3, *pImg4, *pImg5, *pImg6;

        pImg0 = src + j + ii * srcStride;
        pImg1 = pImg0 + srcStride;
        pImg2 = pImg0 - srcStride;
        pImg3 = pImg1 + srcStride;
        pImg4 = pImg2 - srcStride;
        pImg5 = pImg3 + srcStride;
        pImg6 = pImg4 - srcStride;

        const int yVb = (blkDst.y + i + ii) & (vbCTUHeight - 1);
        if (yVb < vbPos && (yVb >= vbPos - 4))   // above
        {
          pImg1 = (yVb == vbPos - 1) ? pImg0 : pImg1;
          pImg3 = (yVb >= vbPos - 2) ? pImg1 : pImg3;
          pImg5 = (yVb >= vbPos - 3) ? pImg3 : pImg5;

          pImg2 = (yVb == vbPos - 1) ? pImg0 : pImg2;
          pImg4 = (yVb >= vbPos - 2) ? pImg2 : pImg4;
          pImg6 = (yVb >= vbPos - 3) ? pImg4 : pImg6;
        }
#if JVET_O0625_ALF_PADDING
        else if (alfBotBryPos != ALF_NONE_BOUNDARY && (blkDst.y + i + ii) < alfBotBryPos && (blkDst.y + i + ii) >= alfBotBryPos - botBryLines) //above
        {
          pImg1 = ((blkDst.y + i + ii) == alfBotBryPos - 1) ? pImg0 : pImg1;
          pImg3 = ((blkDst.y + i + ii) >= alfBotBryPos - 2) ? pImg1 : pImg3;
          pImg5 = ((blkDst.y + i + ii) >= alfBotBryPos - 3) ? pImg3 : pImg5;

          pImg2 = ((blkDst.y + i + ii) == alfBotBryPos - 1) ? pImg0 : pImg2;
          pImg4 = ((blkDst.y + i + ii) >= alfBotBryPos - 2) ? pImg2 : pImg4;
          pImg6 = ((blkDst.y + i + ii) >= alfBotBryPos - 3) ? pImg4 : pImg6;
        }
        else if (alfTopBryPos != ALF_NONE_BOUNDARY && (blkDst.y + i + ii) >= alfTopBryPos && (blkDst.y + i + ii) <= alfTopBryPos + 2) //bottom
        {
          pImg1 = ((blkDst.y + i + ii) == alfTopBryPos) ? pImg0 : pImg1;
          pImg3 = ((blkDst.y + i + ii) <= alfTopBryPos + 1) ? pImg1 : pImg3;
          pImg5 = ((blkDst.y + i + ii) <= alfTopBryPos + 2) ? pImg3 : pImg5;

          pImg2 = ((blkDst.y + i + ii) == alfTopBryPos) ? pImg0 : pImg2;
          pImg4 = ((blkDst.y + i + ii) <= alfTopBryPos + 1) ? pImg2 : pImg4;
          pImg6 = ((blkDst.y + i + ii) <= alfTopBryPos + 2) ? pImg4 : pImg6;
        }
#endif
        else if (yVb >= vbPos && (yVb <= vbPos + 3))   // bottom
        {
          pImg2 = (yVb == vbPos) ? pImg0 : pImg2;
          pImg4 = (yVb <= vbPos + 1) ? pImg2 : pImg4;
          pImg6 = (yVb <= vbPos + 2) ? pImg4 : pImg6;

          pImg1 = (yVb == vbPos) ? pImg0 : pImg1;
          pImg3 = (yVb <= vbPos + 1) ? pImg1 : pImg3;
          pImg5 = (yVb <= vbPos + 2) ? pImg3 : pImg5;
        }
        __m128i cur = _mm_loadu_si128((const __m128i *) pImg0);

#if JVET_O0625_ALF_PADDING
        // vertical 2-side padding
        bool leftBry = alfLeftBryPos != ALF_NONE_BOUNDARY && (blkDst.x + j) >= alfLeftBryPos && (blkDst.x + j) <= alfLeftBryPos + 2;
        bool rightBry = alfRightBryPos != ALF_NONE_BOUNDARY && (blkDst.x + j) < alfRightBryPos && (blkDst.x + j + 8) >= alfRightBryPos;
        __m128i xmm00 = _mm_lddqu_si128((__m128i*) (pImg5 + 0));
        __m128i xmm01 = _mm_lddqu_si128((__m128i*) (pImg6 + 0));
        __m128i xmm10 = _mm_lddqu_si128((__m128i*) (pImg3 + 1));
        __m128i xmm11 = _mm_lddqu_si128((__m128i*) (pImg4 - 1));
        __m128i xmm20 = _mm_lddqu_si128((__m128i*) (pImg3 + 0));
        __m128i xmm21 = _mm_lddqu_si128((__m128i*) (pImg4 + 0));
        __m128i xmm30 = _mm_lddqu_si128((__m128i*) (pImg3 - 1));
        __m128i xmm31 = _mm_lddqu_si128((__m128i*) (pImg4 + 1));
        __m128i xmm40 = _mm_lddqu_si128((__m128i*) (pImg1 + 2));
        __m128i xmm41 = _mm_lddqu_si128((__m128i*) (pImg2 - 2));
        __m128i xmm50 = _mm_lddqu_si128((__m128i*) (pImg1 + 1));
        __m128i xmm51 = _mm_lddqu_si128((__m128i*) (pImg2 - 1));
        __m128i xmm60 = _mm_lddqu_si128((__m128i*) (pImg1 + 0));
        __m128i xmm61 = _mm_lddqu_si128((__m128i*) (pImg2 + 0));
        __m128i xmm70 = _mm_lddqu_si128((__m128i*) (pImg1 - 1));
        __m128i xmm71 = _mm_lddqu_si128((__m128i*) (pImg2 + 1));
        __m128i xmm80 = _mm_lddqu_si128((__m128i*) (pImg1 - 2));
        __m128i xmm81 = _mm_lddqu_si128((__m128i*) (pImg2 + 2));
        __m128i xmm90 = _mm_lddqu_si128((__m128i*) (pImg0 + 3));
        __m128i xmm91 = _mm_lddqu_si128((__m128i*) (pImg0 - 3));
        __m128i xmm100 = _mm_lddqu_si128((__m128i*) (pImg0 + 2));
        __m128i xmm101 = _mm_lddqu_si128((__m128i*) (pImg0 - 2));
        __m128i xmm110 = _mm_lddqu_si128((__m128i*) (pImg0 + 1));
        __m128i xmm111 = _mm_lddqu_si128((__m128i*) (pImg0 - 1));
        if (leftBry)
        {
          xmm10 = _mm_blend_epi16(xmm10, xmm20, 0x01);
          xmm31 = _mm_blend_epi16(xmm31, xmm21, 0x01);
          __m128i xmm40Tmp = _mm_shufflelo_epi16(xmm60, 0xe8);
          xmm40 = _mm_blend_epi16(xmm40, xmm40Tmp, 0x03);
          xmm50 = _mm_blend_epi16(xmm50, xmm60, 0x01);
          xmm71 = _mm_blend_epi16(xmm71, xmm61, 0x01);
          __m128i xmm81Tmp = _mm_shufflelo_epi16(xmm61, 0xe8);
          xmm81 = _mm_blend_epi16(xmm81, xmm81Tmp, 0x03);
          __m128i xmm90Tmp = _mm_shuffle_epi8(cur, mmMask1);
          xmm90 = _mm_blend_epi16(xmm90, xmm90Tmp, 0x07);
          __m128i xmm100Tmp = _mm_shufflelo_epi16(cur, 0xe8);
          xmm100 = _mm_blend_epi16(xmm100, xmm100Tmp, 0x03);
          xmm110 = _mm_blend_epi16(xmm110, cur, 0x01);
        }
        if (rightBry)
        {
          xmm11 = _mm_blend_epi16(xmm11, xmm21, 0x80);
          xmm30 = _mm_blend_epi16(xmm30, xmm20, 0x80);
          __m128i xmm41Tmp = _mm_shuffle_epi8(xmm61, mmMask2);
          xmm41 = _mm_blend_epi16(xmm41, xmm41Tmp, 0xC0);
          xmm51 = _mm_blend_epi16(xmm51, xmm61, 0x80);
          xmm70 = _mm_blend_epi16(xmm70, xmm60, 0x80);
          __m128i xmm80Tmp = _mm_shuffle_epi8(xmm60, mmMask2);
          xmm80 = _mm_blend_epi16(xmm80, xmm80Tmp, 0xC0);
          __m128i xmm91Tmp = _mm_shuffle_epi8(cur, mmMask3);
          xmm91 = _mm_blend_epi16(xmm91, xmm91Tmp, 0xE0);
          __m128i xmm101Tmp = _mm_shuffle_epi8(cur, mmMask2);
          xmm101 = _mm_blend_epi16(xmm101, xmm101Tmp, 0xC0);
          xmm111 = _mm_blend_epi16(xmm111, cur, 0x80);
        }

#endif
        __m128i accumA = mmOffset;
        __m128i accumB = mmOffset;

#if JVET_O0625_ALF_PADDING
        auto process2coeffs = [&](const int i, __m128i ptr0, __m128i ptr1, __m128i ptr2, __m128i ptr3) {
          const __m128i val00 = _mm_sub_epi16(ptr0, cur);
          const __m128i val10 = _mm_sub_epi16(ptr2, cur);
          const __m128i val01 = _mm_sub_epi16(ptr1, cur);
          const __m128i val11 = _mm_sub_epi16(ptr3, cur);
#else
        auto process2coeffs = [&](const int i, const Pel *ptr0, const Pel *ptr1, const Pel *ptr2, const Pel *ptr3) {
          const __m128i val00 = _mm_sub_epi16(_mm_loadu_si128((const __m128i *) ptr0), cur);
          const __m128i val10 = _mm_sub_epi16(_mm_loadu_si128((const __m128i *) ptr2), cur);
          const __m128i val01 = _mm_sub_epi16(_mm_loadu_si128((const __m128i *) ptr1), cur);
          const __m128i val11 = _mm_sub_epi16(_mm_loadu_si128((const __m128i *) ptr3), cur);
#endif

          __m128i val01A = _mm_unpacklo_epi16(val00, val10);
          __m128i val01B = _mm_unpackhi_epi16(val00, val10);
          __m128i val01C = _mm_unpacklo_epi16(val01, val11);
          __m128i val01D = _mm_unpackhi_epi16(val01, val11);

          __m128i limit01A = params[0][1][i];
          __m128i limit01B = params[1][1][i];

          val01A = _mm_min_epi16(val01A, limit01A);
          val01B = _mm_min_epi16(val01B, limit01B);
          val01C = _mm_min_epi16(val01C, limit01A);
          val01D = _mm_min_epi16(val01D, limit01B);

          limit01A = _mm_sub_epi16(_mm_setzero_si128(), limit01A);
          limit01B = _mm_sub_epi16(_mm_setzero_si128(), limit01B);

          val01A = _mm_max_epi16(val01A, limit01A);
          val01B = _mm_max_epi16(val01B, limit01B);
          val01C = _mm_max_epi16(val01C, limit01A);
          val01D = _mm_max_epi16(val01D, limit01B);

          val01A = _mm_add_epi16(val01A, val01C);
          val01B = _mm_add_epi16(val01B, val01D);

          const __m128i coeff01A = params[0][0][i];
          const __m128i coeff01B = params[1][0][i];

          accumA = _mm_add_epi32(accumA, _mm_madd_epi16(val01A, coeff01A));
          accumB = _mm_add_epi32(accumB, _mm_madd_epi16(val01B, coeff01B));
        };


#if JVET_O0625_ALF_PADDING
        process2coeffs(0, xmm00, xmm01, xmm10, xmm11);
        process2coeffs(1, xmm20, xmm21, xmm30, xmm31);
        process2coeffs(2, xmm40, xmm41, xmm50, xmm51);
        process2coeffs(3, xmm60, xmm61, xmm70, xmm71);
        process2coeffs(4, xmm80, xmm81, xmm90, xmm91);
        process2coeffs(5, xmm100, xmm101, xmm110, xmm111);
#else
        process2coeffs(0, pImg5 + 0, pImg6 + 0, pImg3 + 1, pImg4 - 1);
        process2coeffs(1, pImg3 + 0, pImg4 + 0, pImg3 - 1, pImg4 + 1);
        process2coeffs(2, pImg1 + 2, pImg2 - 2, pImg1 + 1, pImg2 - 1);
        process2coeffs(3, pImg1 + 0, pImg2 + 0, pImg1 - 1, pImg2 + 1);
        process2coeffs(4, pImg1 - 2, pImg2 + 2, pImg0 + 3, pImg0 - 3);
        process2coeffs(5, pImg0 + 2, pImg0 - 2, pImg0 + 1, pImg0 - 1);
#endif


        accumA = _mm_srai_epi32(accumA, SHIFT);
        accumB = _mm_srai_epi32(accumB, SHIFT);
        accumA = _mm_packs_epi32(accumA, accumB);
        accumA = _mm_add_epi16(accumA, cur);
        accumA = _mm_min_epi16(mmMax, _mm_max_epi16(accumA, mmMin));

        _mm_storeu_si128((__m128i *) (dst + ii * dstStride + j), accumA);
      }
    }

    src += srcStride * STEP_Y;
    dst += dstStride * STEP_Y;
  }
}

template <X86_VEXT vext>
void AdaptiveLoopFilter::_initAdaptiveLoopFilterX86()
{
  m_deriveClassificationBlk = simdDeriveClassificationBlk<vext>;
  m_filter5x5Blk = simdFilter5x5Blk<vext>;
  m_filter7x7Blk = simdFilter7x7Blk<vext>;
}

template void AdaptiveLoopFilter::_initAdaptiveLoopFilterX86<SIMDX86>();
#endif   // TARGET_SIMD_X86
