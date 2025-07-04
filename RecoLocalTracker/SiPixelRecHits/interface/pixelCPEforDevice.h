#ifndef RecoLocalTracker_SiPixelRecHits_interface_pixelCPEforDevice_h
#define RecoLocalTracker_SiPixelRecHits_interface_pixelCPEforDevice_h

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iterator>

#include "DataFormats/SiPixelClusterSoA/interface/ClusteringConstants.h"
#include "Geometry/CommonTopologies/interface/SimplePixelTopology.h"
#include "DataFormats/TrackingRecHitSoA/interface/SiPixelHitStatus.h"
#include "DataFormats/SiPixelClusterSoA/interface/ClusteringConstants.h"
#include "DataFormats/GeometrySurface/interface/SOARotation.h"
#include "Geometry/CommonTopologies/interface/SimplePixelTopology.h"

// #define ONLY_TRIPLETS_IN_HOLE

namespace pixelCPEforDevice {

  // From https://cmssdt.cern.ch/dxr/CMSSW/source/CondFormats/SiPixelTransient/src/SiPixelGenError.cc#485-486
  // qbin: int (0-4) describing the charge of the cluster
  // [0: 1.5<Q/Qavg, 1: 1<Q/Qavg<1.5, 2: 0.85<Q/Qavg<1, 3: 0.95Qmin<Q<0.85Qavg, 4: Q<0.95Qmin]
  constexpr int kGenErrorQBins = 5;
  // arbitrary number of bins for sampling errors
  constexpr int kNumErrorBins = 16;

  constexpr float micronsToCm = 1.0e-4f;

  using Status = SiPixelHitStatus;
  using Frame = SOAFrame<float>;
  using Rotation = SOARotation<float>;

  // SOA (on device)

  template <uint32_t N>
  struct ClusParamsT {
    uint32_t minRow[N];
    uint32_t maxRow[N];
    uint32_t minCol[N];
    uint32_t maxCol[N];

    int32_t q_f_X[N];
    int32_t q_l_X[N];
    int32_t q_f_Y[N];
    int32_t q_l_Y[N];

    int32_t charge[N];

    float xpos[N];
    float ypos[N];

    float xerr[N];
    float yerr[N];

    int16_t xsize[N];  // (*8) clipped at 127 if negative is edge....
    int16_t ysize[N];

    Status status[N];
  };

  // all modules are identical!
  struct CommonParams {
    float theThicknessB;
    float theThicknessE;
  };

  struct DetParams {
    bool isBarrel;
    bool isPosZ;
    uint16_t layer;
    uint16_t index;
    uint32_t rawId;

    float shiftX;
    float shiftY;
    float thePitchX;
    float thePitchY;
    float chargeWidthX;
    float chargeWidthY;
    uint16_t pixmx;  // max pix charge

    uint16_t nRowsRoc;  //we don't need 2^16 columns, is worth to use 15 + 1 for sign
    uint16_t nColsRoc;
    uint16_t nRows;
    uint16_t nCols;

    uint32_t numPixsInModule;

    float x0, y0, z0;  // the vertex in the local coord of the detector

    float apeXX, apeYY;  // ape^2
    uint8_t sx2, sy1, sy2;
    uint8_t sigmax[kNumErrorBins], sigmax1[kNumErrorBins],
        sigmay[kNumErrorBins];  // in micron
    float xfact[kGenErrorQBins], yfact[kGenErrorQBins];
    int minCh[kGenErrorQBins];

    Frame frame;
  };

  constexpr int32_t MaxHitsInIter = pixelClustering::maxHitsInIter();
  using ClusParams = ClusParamsT<MaxHitsInIter>;

  constexpr inline void computeAnglesFromDet(
      DetParams const& __restrict__ detParams, float const x, float const y, float& cotalpha, float& cotbeta) {
    // x,y local position on det
    auto gvx = x - detParams.x0;
    auto gvy = y - detParams.y0;
    auto gvz = -1.f / detParams.z0;
    // normalization not required as only ratio used...
    // calculate angles
    cotalpha = gvx * gvz;
    cotbeta = gvy * gvz;
  }

  constexpr inline float correction(int sizeM1,
                                    int q_f,                        //!< Charge in the first pixel.
                                    int q_l,                        //!< Charge in the last pixel.
                                    uint16_t upper_edge_first_pix,  //!< As the name says.
                                    uint16_t lower_edge_last_pix,   //!< As the name says.
                                    float lorentz_shift,            //!< L-shift at half thickness
                                    float theThickness,             //detector thickness
                                    float cot_angle,                //!< cot of alpha_ or beta_
                                    float pitch,                    //!< thePitchX or thePitchY
                                    bool first_is_big,              //!< true if the first is big
                                    bool last_is_big)               //!< true if the last is big
  {
    if (0 == sizeM1)  // size 1
      return 0;

    float w_eff = 0;
    bool simple = true;
    if (1 == sizeM1) {  // size 2
      //--- Width of the clusters minus the edge (first and last) pixels.
      //--- In the note, they are denoted x_F and x_L (and y_F and y_L)
      // assert(lower_edge_last_pix >= upper_edge_first_pix);
      auto w_inner = pitch * float(lower_edge_last_pix - upper_edge_first_pix);  // in cm

      //--- Predicted charge width from geometry
      auto w_pred = theThickness * cot_angle  // geometric correction (in cm)
                    - lorentz_shift;          // (in cm) &&& check fpix!

      w_eff = std::abs(w_pred) - w_inner;

      //--- If the observed charge width is inconsistent with the expectations
      //--- based on the track, do *not* use w_pred-w_inner.  Instead, replace
      //--- it with an *average* effective charge width, which is the average
      //--- length of the edge pixels.

      // this can produce "large" regressions for very small numeric differences
      simple = (w_eff < 0.0f) | (w_eff > pitch);
    }

    if (simple) {
      //--- Total length of the two edge pixels (first+last)
      float sum_of_edge = 2.0f;
      if (first_is_big)
        sum_of_edge += 1.0f;
      if (last_is_big)
        sum_of_edge += 1.0f;
      w_eff = pitch * 0.5f * sum_of_edge;  // ave. length of edge pixels (first+last) (cm)
    }

    //--- Finally, compute the position in this projection
    float qdiff = q_l - q_f;
    float qsum = q_l + q_f;

    //--- Temporary fix for clusters with both first and last pixel with charge = 0
    if (qsum == 0)
      qsum = 1.0f;

    return 0.5f * (qdiff / qsum) * w_eff;
  }

  template <typename TrackerTraits>
  constexpr inline void position(CommonParams const& __restrict__ comParams,
                                 DetParams const& __restrict__ detParams,
                                 ClusParams& cp,
                                 uint32_t ic) {
    constexpr int maxSize = TrackerTraits::maxSizeCluster;
    //--- Upper Right corner of Lower Left pixel -- in measurement frame
    uint16_t llx = cp.minRow[ic] + 1;
    uint16_t lly = cp.minCol[ic] + 1;

    //--- Lower Left corner of Upper Right pixel -- in measurement frame
    uint16_t urx = cp.maxRow[ic];
    uint16_t ury = cp.maxCol[ic];

    uint16_t llxl = llx, llyl = lly, urxl = urx, uryl = ury;

    llxl = TrackerTraits::localX(llx);
    llyl = TrackerTraits::localY(lly);
    urxl = TrackerTraits::localX(urx);
    uryl = TrackerTraits::localY(ury);

    auto mx = llxl + urxl;
    auto my = llyl + uryl;

    int xsize = int(urxl) + 2 - int(llxl);
    int ysize = int(uryl) + 2 - int(llyl);
    assert(xsize >= 0);  // 0 if bixpix...
    assert(ysize >= 0);

    if (TrackerTraits::isBigPixX(cp.minRow[ic]))
      ++xsize;
    if (TrackerTraits::isBigPixX(cp.maxRow[ic]))
      ++xsize;
    if (TrackerTraits::isBigPixY(cp.minCol[ic]))
      ++ysize;
    if (TrackerTraits::isBigPixY(cp.maxCol[ic]))
      ++ysize;

    int unbalanceX = 8.f * std::abs(float(cp.q_f_X[ic] - cp.q_l_X[ic])) / float(cp.q_f_X[ic] + cp.q_l_X[ic]);
    int unbalanceY = 8.f * std::abs(float(cp.q_f_Y[ic] - cp.q_l_Y[ic])) / float(cp.q_f_Y[ic] + cp.q_l_Y[ic]);

    xsize = 8 * xsize - unbalanceX;
    ysize = 8 * ysize - unbalanceY;

    cp.xsize[ic] = std::min(xsize, maxSize);
    cp.ysize[ic] = std::min(ysize, maxSize);

    if (cp.minRow[ic] == 0 || cp.maxRow[ic] == uint32_t(detParams.nRows - 1))
      cp.xsize[ic] = -cp.xsize[ic];

    if (cp.minCol[ic] == 0 || cp.maxCol[ic] == uint32_t(detParams.nCols - 1))
      cp.ysize[ic] = -cp.ysize[ic];

    // Compute the position relative to the center of the module, taking into account the corrections for
    // the Phase 1 big pixels, the module pitch, and the Lorentz shift.
    // Use an explicit FMA instruction instead of simply (position * pitch + shift) to make sure that
    // different compiler optimizations do not produce different code on different architectures.
    float xPos = std::fmaf(0.5f * ((float)mx - (float)detParams.nRows) - TrackerTraits::bigPixXCorrection,
                           detParams.thePitchX,
                           detParams.shiftX);
    float yPos = std::fmaf(0.5f * ((float)my - (float)detParams.nCols) - TrackerTraits::bigPixYCorrection,
                           detParams.thePitchY,
                           detParams.shiftY);

    float cotalpha = 0, cotbeta = 0;

    computeAnglesFromDet(detParams, xPos, yPos, cotalpha, cotbeta);

    auto thickness = detParams.isBarrel ? comParams.theThicknessB : comParams.theThicknessE;

    auto xCorr = correction(cp.maxRow[ic] - cp.minRow[ic],
                            cp.q_f_X[ic],
                            cp.q_l_X[ic],
                            llxl,
                            urxl,
                            detParams.chargeWidthX,  // lorentz shift in cm
                            thickness,
                            cotalpha,
                            detParams.thePitchX,
                            TrackerTraits::isBigPixX(cp.minRow[ic]),
                            TrackerTraits::isBigPixX(cp.maxRow[ic]));

    auto yCorr = correction(cp.maxCol[ic] - cp.minCol[ic],
                            cp.q_f_Y[ic],
                            cp.q_l_Y[ic],
                            llyl,
                            uryl,
                            detParams.chargeWidthY,  // lorentz shift in cm
                            thickness,
                            cotbeta,
                            detParams.thePitchY,
                            TrackerTraits::isBigPixY(cp.minCol[ic]),
                            TrackerTraits::isBigPixY(cp.maxCol[ic]));

    cp.xpos[ic] = xPos + xCorr;
    cp.ypos[ic] = yPos + yCorr;
  }

  template <typename TrackerTraits>
  constexpr inline void errorFromSize(CommonParams const& __restrict__ comParams,
                                      DetParams const& __restrict__ detParams,
                                      ClusParams& cp,
                                      uint32_t ic) {
    // Edge cluster errors
    cp.xerr[ic] = 0.0050;
    cp.yerr[ic] = 0.0085;

    // FIXME these are errors form Run1
    float xerr_barrel_l1_def = TrackerTraits::xerr_barrel_l1_def;
    float yerr_barrel_l1_def = TrackerTraits::yerr_barrel_l1_def;
    float xerr_barrel_ln_def = TrackerTraits::xerr_barrel_ln_def;
    float yerr_barrel_ln_def = TrackerTraits::yerr_barrel_ln_def;
    float xerr_endcap_def = TrackerTraits::xerr_endcap_def;
    float yerr_endcap_def = TrackerTraits::yerr_endcap_def;

    constexpr float xerr_barrel_l1[] = {0.00115, 0.00120, 0.00088};  //TODO MOVE THESE SOMEWHERE ELSE
    constexpr float yerr_barrel_l1[] = {
        0.00375, 0.00230, 0.00250, 0.00250, 0.00230, 0.00230, 0.00210, 0.00210, 0.00240};
    constexpr float xerr_barrel_ln[] = {0.00115, 0.00120, 0.00088};
    constexpr float yerr_barrel_ln[] = {
        0.00375, 0.00230, 0.00250, 0.00250, 0.00230, 0.00230, 0.00210, 0.00210, 0.00240};
    constexpr float xerr_endcap[] = {0.0020, 0.0020};
    constexpr float yerr_endcap[] = {0.00210};

    auto sx = cp.maxRow[ic] - cp.minRow[ic];
    auto sy = cp.maxCol[ic] - cp.minCol[ic];

    // is edgy ?
    bool isEdgeX = cp.xsize[ic] < 1;
    bool isEdgeY = cp.ysize[ic] < 1;

    // is one and big?
    bool isBig1X = ((0 == sx) && TrackerTraits::isBigPixX(cp.minRow[ic]));
    bool isBig1Y = ((0 == sy) && TrackerTraits::isBigPixY(cp.minCol[ic]));

    if (!isEdgeX && !isBig1X) {
      if (not detParams.isBarrel) {
        cp.xerr[ic] = sx < std::size(xerr_endcap) ? xerr_endcap[sx] : xerr_endcap_def;
      } else if (detParams.layer == 1) {
        cp.xerr[ic] = sx < std::size(xerr_barrel_l1) ? xerr_barrel_l1[sx] : xerr_barrel_l1_def;
      } else {
        cp.xerr[ic] = sx < std::size(xerr_barrel_ln) ? xerr_barrel_ln[sx] : xerr_barrel_ln_def;
      }
    }

    if (!isEdgeY && !isBig1Y) {
      if (not detParams.isBarrel) {
        cp.yerr[ic] = sy < std::size(yerr_endcap) ? yerr_endcap[sy] : yerr_endcap_def;
      } else if (detParams.layer == 1) {
        cp.yerr[ic] = sy < std::size(yerr_barrel_l1) ? yerr_barrel_l1[sy] : yerr_barrel_l1_def;
      } else {
        cp.yerr[ic] = sy < std::size(yerr_barrel_ln) ? yerr_barrel_ln[sy] : yerr_barrel_ln_def;
      }
    }
  }

  template <typename TrackerTraits>
  constexpr inline void errorFromDB(CommonParams const& __restrict__ comParams,
                                    DetParams const& __restrict__ detParams,
                                    ClusParams& cp,
                                    uint32_t ic) {
    // Edge cluster errors
    cp.xerr[ic] = 0.0050f;
    cp.yerr[ic] = 0.0085f;

    auto sx = cp.maxRow[ic] - cp.minRow[ic];
    auto sy = cp.maxCol[ic] - cp.minCol[ic];

    // is edgy ?  (size is set negative: see above)
    bool isEdgeX = cp.xsize[ic] < 1;
    bool isEdgeY = cp.ysize[ic] < 1;
    // is one and big?
    bool isOneX = (0 == sx);
    bool isOneY = (0 == sy);
    bool isBigX = TrackerTraits::isBigPixX(cp.minRow[ic]);
    bool isBigY = TrackerTraits::isBigPixY(cp.minCol[ic]);

    auto ch = cp.charge[ic];
    auto bin = 0;
    for (; bin < kGenErrorQBins - 1; ++bin)
      // find first bin which minimum charge exceeds cluster charge
      if (ch < detParams.minCh[bin + 1])
        break;

    // in detParams qBins are reversed bin0 -> smallest charge, bin4-> largest charge
    // whereas in CondFormats/SiPixelTransient/src/SiPixelGenError.cc it is the opposite
    // so we reverse the bin here -> kGenErrorQBins - 1 - bin
    cp.status[ic].qBin = kGenErrorQBins - 1 - bin;
    cp.status[ic].isOneX = isOneX;
    cp.status[ic].isBigX = (isOneX & isBigX) | isEdgeX;
    cp.status[ic].isOneY = isOneY;
    cp.status[ic].isBigY = (isOneY & isBigY) | isEdgeY;

    auto xoff = -float(TrackerTraits::xOffset) * detParams.thePitchX;
    int low_value = 0;
    int high_value = kNumErrorBins - 1;
    int bin_value = float(kNumErrorBins) * (cp.xpos[ic] + xoff) / (2 * xoff);
    // return estimated bin value truncated to [0, 15]
    // Equivalent of jx = std::clamp(bin_value, low_value, high_value)
    // which doesn't compile with gcc14 due to reference to __glibcxx_assert
    // See https://github.com/llvm/llvm-project/issues/95183
    int tmp_max = std::max<int>(bin_value, low_value);
    int jx = std::min<int>(tmp_max, high_value);

    auto toCM = [](uint8_t x) { return float(x) * 1.e-4f; };

    if (not isEdgeX) {
      cp.xerr[ic] = isOneX ? toCM(isBigX ? detParams.sx2 : detParams.sigmax1[jx])
                           : detParams.xfact[bin] * toCM(detParams.sigmax[jx]);
    }

    auto ey = cp.ysize[ic] > 8 ? detParams.sigmay[std::min(cp.ysize[ic] - 9, 15)] : detParams.sy1;
    if (not isEdgeY) {
      cp.yerr[ic] = isOneY ? toCM(isBigY ? detParams.sy2 : detParams.sy1) : detParams.yfact[bin] * toCM(ey);
    }
  }

  //for Phase2 -> fallback to error from size
  template <>
  constexpr inline void errorFromDB<pixelTopology::Phase2>(CommonParams const& __restrict__ comParams,
                                                           DetParams const& __restrict__ detParams,
                                                           ClusParams& cp,
                                                           uint32_t ic) {
    errorFromSize<pixelTopology::Phase2>(comParams, detParams, cp, ic);
  }

  template <typename TrackerTopology>
  struct ParamsOnDeviceT {
    CommonParams m_commonParams;
    // Will contain an array of DetParams instances
    DetParams m_detParams[TrackerTopology::numberOfModules];

    constexpr CommonParams const& __restrict__ commonParams() const { return m_commonParams; }
    constexpr DetParams const& __restrict__ detParams(int i) const { return m_detParams[i]; }

    CommonParams& commonParams() { return m_commonParams; }
    DetParams& detParams(int i) { return m_detParams[i]; }

#ifdef ONLY_TRIPLETS_IN_HOLE
    using AverageGeometry = pixelTopology::AverageGeometryT<TrackerTopology>;

    AverageGeometry m_averageGeometry;
    constexpr AverageGeometry const& __restrict__ averageGeometry() const { return m_averageGeometry; }
    AverageGeometry& averageGeometry() { return m_averageGeometry; }
#endif  // ONLY_TRIPLETS_IN_HOLE
  };

}  // namespace pixelCPEforDevice

#endif  // RecoLocalTracker_SiPixelRecHits_interface_pixelCPEforDevice_h
