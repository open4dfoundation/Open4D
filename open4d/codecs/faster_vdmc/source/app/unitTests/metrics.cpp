/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2022, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of the ISO/IEC nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <chrono>
#include <iostream>

#include "util/misc.hpp"
#include "util/verbose.hpp"
#include "version.hpp"

#include "metrics.hpp"

#include <gtest/gtest.h>
#include "common.hpp"

TEST(metrics, compare) {
  disableSubProcessLog.disable();
  // Set parameters
  vmesh::VMCMetricsParameters params;
  std::string                 srcObjPath = "data/levi_fr%04d_qp12_qt13.obj";
  std::string                 srcTexPath = "data/levi_fr%04d.png";
  std::string                 decObjPath = "data/levi_fr%04d_decoded.obj";
  std::string                 decTexPath = "data/levi_fr%04d_decoded.png";
  const int                   startFrame = 0;
  const int                   frameCount = 1;
  params.computePcc                      = true;
  params.computeIbsm                     = false;
  params.computePcqm                     = false;
  params.gridSize                        = 1024;
  params.qp                              = 12;
  params.qt                              = 13;
  params.minPosition[0]                  = -0.780686975;
  params.minPosition[1]                  = -0.0424938016;
  params.minPosition[2]                  = -0.594317973;
  params.maxPosition[0]                  = 0.857237995;
  params.maxPosition[1]                  = 1.90897;
  params.maxPosition[2]                  = 0.687259018;
  params.pcqmRadiusCurvature             = 0.001;
  params.pcqmThresholdKnnSearch          = 20;
  params.pcqmRadiusFactor                = 2.0;
  params.dequantizeUV                    = false;
  params.verbose                         = true;

  // Compute metric with VMCMetrics:
  vmesh::Sequence source, reconstruct;
  if (!source.load(srcObjPath, srcTexPath, startFrame, frameCount)) {
    std::cerr << "Error: can't load source sequence\n";
    FAIL() << "Error: can't load source sequence\n";
    return;
  }
  if (!reconstruct.load(decObjPath, decTexPath, startFrame, frameCount)) {
    std::cerr << "Error: can't load reconstruct sequence\n";
    FAIL() << "Error: can't load reconstruct sequence\n";
    return;
  }

  vmesh::VMCMetrics metrics;
  metrics.compute(source, reconstruct, params);

  // Compute metric with mm command:
  // ISBM metrics
  std::stringstream cmd;
  // printf("START METRICS mm \n"); fflush(stdout);
  // cmd << g_mmMetricsPath << " "
  //     << "sequence "
  //     << "  --firstFrame 0 "
  //     << "  --lastFrame 0  "
  //     << "END "
  //     << "dequantize "
  //     << "  --inputModel " << srcObjPath << " "
  //     << "  --outputModel ID:deqRef "
  //     << "  --useFixedPoint "
  //     << "  --qp 12 "
  //     << "  --minPos \"-0.780686975 -0.0424938016 -0.594317973\" "
  //     << "  --maxPos \"0.857237995 1.90897 0.687259018\" "
  //     << "  --qt 13 "
  //     << "  --minUv \"0 0\" "
  //     << "  --maxUv \"1.0 1.0\" "
  //     << "END "
  //     << "dequantize "
  //     << "  --inputModel " << decObjPath << " "
  //     << "  --outputModel ID:deqDis "
  //     << "  --useFixedPoint "
  //     << "  --qp 12 "
  //     << "  --minPos \"-0.780686975 -0.0424938016 -0.594317973\" "
  //     << "  --maxPos \"0.857237995 1.90897 0.687259018\" "
  //     << "  --qt 13 "
  //     << "  --minUv \"0 0\" "
  //     << "  --maxUv \"1.0 1.0\" "
  //     << "END "
  //     << "compare "
  //     << "  --mode ibsm "
  //     << "  --inputModelA ID:deqRef "
  //     << "  --inputMapA " << srcTexPath << " "
  //     << "  --inputModelB ID:deqDis "
  //     << "  --inputMapB " << decTexPath << " "
  //     << "  --outputCsv mmetric_ibsm.csv";
  // if (disableSubProcessLog.disableLog())
  //   cmd << " 2>&1 > /dev/null";
  // printf("cmd = %s \n", cmd.str().c_str());
  // system(cmd.str().c_str());

  // PCC metrics
  cmd.str("");
  cmd << g_mmMetricsPath << " "
      << "sequence "
      << "  --firstFrame 0 "
      << "  --lastFrame 0  "
      << "END "
      << "dequantize "
      << "  --inputModel " << srcObjPath << " "
      << "  --outputModel ID:deqRef "
      << "  --useFixedPoint "
      << "  --qp 12 "
      << "  --minPos \"-0.780686975 -0.0424938016 -0.594317973\" "
      << "  --maxPos \"0.857237995 1.90897 0.687259018\" "
      << "  --qt 13 "
      << "  --minUv \"0 0\" "
      << "  --maxUv \"1.0 1.0\" "
      << "END "
      << "reindex "
      << "  --sort oriented "
      << "  --inputModel ID:deqRef "
      << "  --outputModel ID:ref_reordered "
      << "END sample "
      << "  --mode grid "
      << "  --gridSize 1024 "
      << "  --hideProgress "
      << "  --useNormal "
      << "  --useFixedPoint "
      << "  --minPos \"-0.780686975 -0.0424938016 -0.594317973\" "
      << "  --maxPos \"0.857237995 1.90897 0.687259018\" "
      << "  --bilinear "
      << "  --inputModel ID:ref_reordered "
      << "  --inputMap " << srcTexPath << " "
      << "  --outputModel ID:pcRef "
      << "END "
      << "dequantize "
      << "  --inputModel " << decObjPath << " "
      << "  --outputModel ID:deqDis "
      << "  --useFixedPoint "
      << "  --qp 12 "
      << "  --minPos \"-0.780686975 -0.0424938016 -0.594317973\" "
      << "  --maxPos \"0.857237995 1.90897 0.687259018\" "
      << "  --qt 13 "
      << "  --minUv \"0 0\" "
      << "  --maxUv \"1.0 1.0\" "
      << "END "
      << "reindex "
      << "  --sort oriented "
      << "  --inputModel ID:deqDis "
      << "  --outputModel ID:ref_reordered "
      << "END "
      << "sample "
      << "  --mode grid "
      << "  --gridSize 1024 "
      << "  --hideProgress "
      << "  --useNormal "
      << "  --useFixedPoint "
      << "  --minPos \"-0.780686975 -0.0424938016 -0.594317973\" "
      << "  --maxPos \"0.857237995 1.90897 0.687259018\" "
      << "  --bilinear "
      << "  --inputModel ID:ref_reordered "
      << "  --inputMap " << decTexPath << " "
      << "  --outputModel ID:pcDis "
      << "END "
      << "compare "
      << "  --mode pcc "
      << "  --resolution 1.9514638016 "
      << "  --inputModelA ID:pcRef "
      << "  --inputModelB ID:pcDis "
      << " > metric_pcc.log ";
  printf("cmd = %s \n", cmd.str().c_str());
  system(cmd.str().c_str());

  std::string d1a = grep("metric_pcc.log", "   mseF,PSNR (p2point): ");
  std::string d2a = grep("metric_pcc.log", "   mseF,PSNR (p2plane): ");
  std::string c0a = grep("metric_pcc.log", "   c[0],PSNRF         : ");
  std::string c1a = grep("metric_pcc.log", "   c[1],PSNRF         : ");
  std::string c2a = grep("metric_pcc.log", "   c[2],PSNRF         : ");

  auto               pcc = metrics.getPccResults();
  std::ostringstream d1b;
  std::ostringstream d2b;
  std::ostringstream c0b;
  std::ostringstream c1b;
  std::ostringstream c2b;
  d1b << std::setprecision(std::numeric_limits<float>::max_digits10) << pcc[0];
  d2b << std::setprecision(std::numeric_limits<float>::max_digits10) << pcc[1];
  c0b << std::setprecision(std::numeric_limits<float>::max_digits10) << pcc[2];
  c1b << std::setprecision(std::numeric_limits<float>::max_digits10) << pcc[3];
  c2b << std::setprecision(std::numeric_limits<float>::max_digits10) << pcc[4];
  std::cout << "mm soft/lib metrics:\n";
  std::cout << "d1 " << d1a << " / " << d1b.str() << "\n";
  std::cout << "d2 " << d2a << " / " << d2b.str() << "\n";
  std::cout << "c0 " << c0a << " / " << c0b.str() << "\n";
  std::cout << "c1 " << c1a << " / " << c1b.str() << "\n";
  std::cout << "c2 " << c2a << " / " << c2b.str() << "\n";

  disableSubProcessLog.enable();

  ASSERT_EQ(d1a, d1b.str());
  ASSERT_EQ(d2a, d2b.str());
  ASSERT_EQ(c0a, c0b.str());
  ASSERT_EQ(c1a, c1b.str());
  ASSERT_EQ(c2a, c2b.str());

  // Remove tmp files
  remove("metric_pcc.log");
}