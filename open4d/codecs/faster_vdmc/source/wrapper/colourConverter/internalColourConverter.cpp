/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ISO/IEC
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
 *  * Neither the name of the ISO/IEC nor the names of its contributors may
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
#include "internalColourConverter.hpp"
#include "util/image.hpp"
#include <sstream>

namespace vmesh {

static const std::vector<Filter444to420> g_filter444to420 = {
  {// 0 DF_F0
   {{+64.0, +384.0, +64.0}, +256.0, 9.0},
   {{+256.0, +256.0}, +256.0, 9.0}},
  {// 1 DF_F1
   {{+128.0, +256.0, +128.0}, +256.0, 9.0},
   {{+256.0, +256.0}, +256.0, 9.0}},
  {// 2 DF_TM5
   {{+21.0,
     +0.0,
     -52.0,
     +0.0,
     +159.0,
     +256.0,
     +159.0,
     +0.0,
     -52.0,
     0.0,
     +21.0},
    +256.0,
    9.0},
   {{+5.0,
     +11.0,
     -21.0,
     -37.0,
     +70.0,
     +228.0,
     +228.0,
     +70.0,
     -37.0,
     -21.0,
     +11.0,
     +5.0},
    +256.0,
    9.0}},
  {// 3 DF_FV
   {{+8.0, 0.0, -64.0, +128.0, +368.0, +128.0, -64.0, 0.0, +8.0}, +256.0, 9.0},
   {{+8.0, 0.0, -24.0, +48.0, +224.0, +224.0, +48.0, -24.0, +0.0, +8.0},
    +256.0,
    9.0}},
  {// 4 DF_GS
   {{(float)(-0.01716352771649 * 512),
     (float)(0.0),
     (float)(+0.04066666714886 * 512),
     (float)(0.0),
     (float)(-0.09154810319329 * 512),
     (float)(0.0),
     (float)(0.31577823859943 * 512),
     (float)(0.50453345032298 * 512),
     (float)(0.31577823859943 * 512),
     (float)(0.0),
     (float)(-0.09154810319329 * 512),
     (float)(0.0),
     (float)(0.04066666714886 * 512),
     (float)(0.0),
     (float)(-0.01716352771649 * 512)},
    +256.0,
    9.0},
   {{(float)(-0.00945406160902 * 512),
     (float)(-0.01539537217249 * 512),
     (float)(0.02360533018213 * 512),
     (float)(0.03519540819902 * 512),
     (float)(-0.05254456550808 * 512),
     (float)(-0.08189331229717 * 512),
     (float)(0.14630826357715 * 512),
     (float)(0.45417830962846 * 512),
     (float)(0.45417830962846 * 512),
     (float)(0.14630826357715 * 512),
     (float)(-0.08189331229717 * 512),
     (float)(-0.05254456550808 * 512),
     (float)(0.03519540819902 * 512),
     (float)(0.02360533018213 * 512),
     (float)(-0.01539537217249 * 512),
     (float)(-0.00945406160902 * 512)},
    +256.0,
    9.0}},
  {// 5 DF_WCS
   {{2.0, -3.0, -9.0, 6.0, 39.0, 58.0, 39.0, 6.0, -9.0, -3.0, 2.0},
    +64.0,
    7.0},
   {{1.0, 0.0, -7.0, -5.0, 22.0, 53.0, 53.0, 22.0, -5.0, -7.0, 0.0, 1.0},
    +64.0,
    7.0}},
  {// 6 DF_SVC
   {{(float)(0.016512788 * 256),
     (float)(-9.77064E-18 * 256),
     (float)(-0.075189625 * 256),
     (float)(1.69232E-17 * 256),
     (float)(0.308132812 * 256),
     (float)(0.50108805 * 256),
     (float)(0.308132812 * 256),
     (float)(1.69232E-17 * 256),
     (float)(-0.075189625 * 256),
     (float)(-9.77064E-18 * 256),
     (float)(0.016512788 * 256)},
    128.0,
    8.0},
   {{(float)(0.005353954 * 256),
     (float)(0.019185222 * 256),
     (float)(-0.039239076 * 256),
     (float)(-0.071592303 * 256),
     (float)(0.138951671 * 256),
     (float)(0.447340531 * 256),
     (float)(0.447340531 * 256),
     (float)(0.138951671 * 256),
     (float)(-0.071592303 * 256),
     (float)(-0.039239076 * 256),
     (float)(0.019185222 * 256),
     (float)(0.005353954 * 256)},
    128.0,
    8.0}},
  {// 7 DF_LZW
   {{(float)(0.00190089262242 * 2048),
     (float)(0.01183528116391 * 2048),
     (float)(0.00000000000000 * 2048),
     (float)(-0.04505642829799 * 2048),
     (float)(-0.04734112465563 * 2048),
     (float)(0.08323159585050 * 2048),
     (float)(0.29483273086709 * 2048),
     (float)(0.40119410489940 * 2048),
     (float)(0.29483273086709 * 2048),
     (float)(0.08323159585050 * 2048),
     (float)(-0.04734112465563 * 2048),
     (float)(-0.04505642829799 * 2048),
     (float)(0.00000000000000 * 2048),
     (float)(0.01183528116391 * 2048),
     (float)(0.00190089262242 * 2048)},
    1024,
    11},
   {{(float)(0.00697781694123 * 2048),
     (float)(0.01100508557770 * 2048),
     (float)(-0.02103907505369 * 2048),
     (float)(-0.05884522739626 * 2048),
     (float)(0.00000000000000 * 2048),
     (float)(0.18935167548323 * 2048),
     (float)(0.37254972444780 * 2048),
     (float)(0.37254972444780 * 2048),
     (float)(0.18935167548323 * 2048),
     (float)(0.00000000000000 * 2048),
     (float)(-0.05884522739626 * 2048),
     (float)(-0.02103907505369 * 2048),
     (float)(0.01100508557770 * 2048),
     (float)(0.00697781694123 * 2048)},
    1024.0,
    11.0}},
  {// 8 DF_SNW
   {{(float)(0.00571729514027 * 128.0),
     (float)(0.01791720928626 * 128.0),
     (float)(-0.02500536578022 * 128.0),
     (float)(-0.07308632957148 * 128.0),
     (float)(0.03993394132849 * 128.0),
     (float)(0.30679811688568 * 128.0),
     (float)(0.45545026542200 * 128.0),
     (float)(0.30679811688568 * 128.0),
     (float)(0.03993394132849 * 128.0),
     (float)(-0.07308632957148 * 128.0),
     (float)(-0.02500536578022 * 128.0),
     (float)(0.01791720928626 * 128.0),
     (float)(0.00571729514027 * 128.0)},
    64.0,
    7.0},
   {{(float)(0.00016629082500 * 256.0),
     (float)(0.01501862112403 * 256.0),
     (float)(0.00483721502879 * 256.0),
     (float)(-0.05885266302048 * 256.0),
     (float)(-0.04391848267708 * 256.0),
     (float)(0.16770594201608 * 256.0),
     (float)(0.41504307670367 * 256.0),
     (float)(0.41504307670367 * 256.0),
     (float)(0.16770594201608 * 256.0),
     (float)(-0.04391848267708 * 256.0),
     (float)(-0.05885266302048 * 256.0),
     (float)(0.00483721502879 * 256.0),
     (float)(0.01501862112403 * 256.0),
     (float)(0.00016629082500 * 256.0)},
    128.0,
    8.0}},
  {// 9 DF_LZ2
   {{(float)(-0.00632274800093 * 256),
     (float)(0.00000000000000 * 256),
     (float)(0.02991834939159 * 256),
     (float)(0.00000000000000 * 256),
     (float)(-0.08310652608775 * 256),
     (float)(0.00000000000000 * 256),
     (float)(0.30981465204533 * 256),
     (float)(0.49939254530351 * 256),
     (float)(0.30981465204533 * 256),
     (float)(0.00000000000000 * 256),
     (float)(-0.08310652608775 * 256),
     (float)(0.00000000000000 * 256),
     (float)(0.02991834939159 * 256),
     (float)(0.00000000000000 * 256),
     (float)(-0.00632274800093 * 256)},
    128.0,
    8.0},
   {{(float)(-0.00198530798042 * 512.0),
     (float)(-0.00752708715723 * 512.0),
     (float)(0.01573387488128 * 512.0),
     (float)(0.02772449226106 * 512.0),
     (float)(-0.04583028312543 * 512.0),
     (float)(-0.07615195442538 * 512.0),
     (float)(0.14134196995234 * 512.0),
     (float)(0.44669429559377 * 512.0),
     (float)(0.44669429559377 * 512.0),
     (float)(0.14134196995234 * 512.0),
     (float)(-0.07615195442538 * 512.0),
     (float)(-0.04583028312543 * 512.0),
     (float)(0.02772449226106 * 512.0),
     (float)(0.01573387488128 * 512.0),
     (float)(-0.00752708715723 * 512.0),
     (float)(-0.00198530798042 * 512.0)},
    256.0,
    9.0}},
  {// 10 DF_SN2
   {{(float)(-0.00886299106559 * 512.0),
     (float)(0.00000000000000 * 512.0),
     (float)(0.03533552841036 * 512.0),
     (float)(0.00000000000000 * 512.0),
     (float)(-0.08813892574297 * 512.0),
     (float)(0.00000000000000 * 512.0),
     (float)(0.31190085501694 * 512.0),
     (float)(0.49953106676252 * 512.0),
     (float)(0.31190085501694 * 512.0),
     (float)(0.00000000000000 * 512.0),
     (float)(0.08813892574297 * 512.0),
     (float)(0.00000000000000 * 512.0),
     (float)(0.03533552841036 * 512.0),
     (float)(0.00000000000000 * 512.0),
     (float)(-0.00886299106559 * 512.0)},
    256.0,
    9.0},
   {{(float)(-0.00293861597778 * 256.0),
     (float)(-0.01004182911844 * 256.0),
     (float)(0.01927196301938 * 256.0),
     (float)(0.03169918776063 * 256.0),
     (float)(-0.04966144977164 * 256.0),
     (float)(-0.07932167506148 * 256.0),
     (float)(0.14344838825693 * 256.0),
     (float)(0.44754403089239 * 256.0),
     (float)(0.44754403089239 * 256.0),
     (float)(0.14344838825693 * 256.0),
     (float)(-0.07932167506148 * 256.0),
     (float)(-0.04966144977164 * 256.0),
     (float)(0.03169918776063 * 256.0),
     (float)(0.01927196301938 * 256.0),
     (float)(-0.01004182911844 * 256.0),
     (float)(-0.00293861597778 * 256.0)},
    128.0,
    8.0}},
  {// 11 DF_SN3
   {{(float)(-0.00067732252889 * 1024.0),
     (float)(-0.01187238377794 * 1024.0),
     (float)(0.01886537347052 * 1024.0),
     (float)(0.03282746274537 * 1024.0),
     (float)(-0.07278440675580 * 1024.0),
     (float)(-0.04925606519713 * 1024.0),
     (float)(0.30555914564961 * 1024.0),
     (float)(0.55467639278853 * 1024.0),
     (float)(0.30555914564961 * 1024.0),
     (float)(-0.04925606519713 * 1024.0),
     (float)(-0.07278440675580 * 1024.0),
     (float)(0.03282746274537 * 1024.0),
     (float)(0.01886537347052 * 1024.0),
     (float)(-0.01187238377794 * 1024.0),
     (float)(-0.00067732252889 * 1024.0)},
    512.0,
    10.0},
   {{(float)(-0.00699733029589 * 256.0),
     (float)(-0.00364087058553 * 256.0),
     (float)(0.03928171002786 * 256.0),
     (float)(-0.01140308043259 * 256.0),
     (float)(-0.10224241186895 * 256.0),
     (float)(0.10042934057489 * 256.0),
     (float)(0.48457264258020 * 256.0),
     (float)(0.48457264258020 * 256.0),
     (float)(0.10042934057490 * 256.0),
     (float)(-0.10224241186895 * 256.0),
     (float)(-0.01140308043259 * 256.0),
     (float)(0.03928171002786 * 256.0),
     (float)(-0.00364087058553 * 256.0),
     (float)(-0.00699733029589 * 256.0)},
    128.0,
    8.0}},
  {// 12 DF_LZ3
   {{(float)(0.00480212008232 * 256.0),
     (float)(-0.00409385963083 * 256.0),
     (float)(-0.01900940873498 * 256.0),
     (float)(0.02310408604406 * 256.0),
     (float)(0.03610013444384 * 256.0),
     (float)(-0.07603763493990 * 256.0),
     (float)(-0.05014978047763 * 256.0),
     (float)(0.30733568526821 * 256.0),
     (float)(0.55589731588983 * 256.0),
     (float)(0.30733568526821 * 256.0),
     (float)(-0.05014978047763 * 256.0),
     (float)(-0.07603763493990 * 256.0),
     (float)(0.03610013444384 * 256.0),
     (float)(0.02310408604406 * 256.0),
     (float)(-0.01900940873498 * 256.0),
     (float)(-0.00409385963083 * 256.0),
     (float)(0.00480212008232 * 256.0)},
    128.0,
    8.0},
   {{(float)(0.00168005324555 * 256.0),
     (float)(0.00405559129955 * 256.0),
     (float)(-0.01554711355088 * 256.0),
     (float)(-0.00492229194265 * 256.0),
     (float)(0.04506212555057 * 256.0),
     (float)(-0.01215504745022 * 256.0),
     (float)(-0.10509848760398 * 256.0),
     (float)(0.10138978248878 * 256.0),
     (float)(0.48553538796329 * 256.0),
     (float)(0.48553538796329 * 256.0),
     (float)(0.10138978248878 * 256.0),
     (float)(-0.10509848760398 * 256.0),
     (float)(-0.01215504745022 * 256.0),
     (float)(0.04506212555057 * 256.0),
     (float)(-0.00492229194265 * 256.0),
     (float)(-0.01554711355088 * 256.0),
     (float)(0.00405559129955 * 256.0),
     (float)(0.00168005324555 * 256.0)},
    128.0,
    8.0}},
  {// 13 DF_LZ4
   {{(float)(-0.00832391833024 * 1024.0),
     (float)(0.00961238047504 * 1024.0),
     (float)(0.03374816073530 * 1024.0),
     (float)(-0.05999673019609 * 1024.0),
     (float)(-0.06007737796902 * 1024.0),
     (float)(0.29966105988877 * 1024.0),
     (float)(0.57075285079248 * 1024.0),
     (float)(0.29966105988877 * 1024.0),
     (float)(-0.06007737796902 * 1024.0),
     (float)(-0.05999673019609 * 1024.0),
     (float)(0.03374816073530 * 1024.0),
     (float)(0.00961238047504 * 1024.0),
     (float)(-0.00832391833024 * 1024.0)},
    512.0,
    10.0},
   {{(float)(-0.00291663662971 * 1024.0),
     (float)(-0.00633435442913 * 1024.0),
     (float)(0.03072431945341 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(-0.09954679502905 * 1024.0),
     (float)(0.08516187621385 * 1024.0),
     (float)(0.49291159042062 * 1024.0),
     (float)(0.49291159042062 * 1024.0),
     (float)(0.08516187621385 * 1024.0),
     (float)(-0.09954679502905 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(0.03072431945341 * 1024.0),
     (float)(-0.00633435442913 * 1024.0),
     (float)(-0.00291663662971 * 1024.0)},
    512.0,
    10.0}},
  {
    // 14 DF_SNW3
    {{(float)(0.26087818243737 * 1024.0),
      (float)(0.47824363512526 * 1024.0),
      (float)(0.26087818243737 * 1024.0)},
     (float)(512.0),
     (float)(10.0)},
    {{(float)(-0.02250866038106 * 1024.0),
      (float)(0.10249133961894 * 1024.0),
      (float)(0.42001732076212 * 1024.0),
      (float)(0.42001732076212 * 1024.0),
      (float)(0.10249133961894 * 1024.0),
      (float)(-0.02250866038106 * 1024.0)},
     512.0,
     10.0},
  },
  {// 15 DF_SNW7
   {{(float)(-0.06359139153628 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(0.30867909869282 * 1024.0),
     (float)(0.50982458568693 * 1024.0),
     (float)(0.30867909869282 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(-0.06359139153628 * 1024.0)},
    512.0,
    10.0},
   {{(float)(0.00793104893426 * 1024.0),
     (float)(-0.02959302918114 * 1024.0),
     (float)(-0.06452911298933 * 1024.0),
     (float)(0.13551904324148 * 1024.0),
     (float)(0.45067204999473 * 1024.0),
     (float)(0.45067204999473 * 1024.0),
     (float)(0.13551904324148 * 1024.0),
     (float)(0.06452911298933 * 1024.0),
     (float)(-0.02959302918114 * 1024.0),
     (float)(0.00793104893426 * 1024.0)},
    512.0,
    10.0}},
  {// 16 DF_SNW11
   {{(float)(0.02247524825725 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(-0.07906905922190 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(0.30769680829831 * 1024.0),
     (float)(0.49779400533267 * 1024.0),
     (float)(0.30769680829831 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(-0.07906905922190 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(0.02247524825725 * 1024.0)},
    512.0,
    10.0},
   {{(float)(0.00535395438219 * 1024.0),
     (float)(0.01918522222618 * 1024.0),
     (float)(-0.03923907588008 * 1024.0),
     (float)(-0.07159230301126 * 1024.0),
     (float)(0.13895167108678 * 1024.0),
     (float)(0.44734053119618 * 1024.0),
     (float)(0.44734053119618 * 1024.0),
     (float)(0.13895167108678 * 1024.0),
     (float)(-0.07159230301126 * 1024.0),
     (float)(-0.03923907588008 * 1024.0),
     (float)(0.01918522222618 * 1024.0),
     (float)(0.00535395438219 * 1024.0)},
    512.0,
    10.0}},
  {// 17 DF_SNW15
   {{(float)(-0.00886299106559 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(0.03533552841036 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(-0.08813892574297 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(0.31190085501694 * 1024.0),
     (float)(0.49953106676252 * 1024.0),
     (float)(0.31190085501694 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(-0.08813892574297 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(0.03533552841036 * 1024.0),
     (float)(0.00000000000000 * 1024.0),
     (float)(-0.00886299106559 * 1024.0)},
    512.0,
    10.0},
   {{(float)(-0.00385006522758 * 1024.0),
     (float)(0.01342207139682 * 1024.0),
     (float)(0.02642569966785 * 1024.0),
     (float)(-0.04515627918959 * 1024.0),
     (float)(-0.07570126817606 * 1024.0),
     (float)(0.14064627966562 * 1024.0),
     (float)(0.44421356186295 * 1024.0),
     (float)(0.44421356186295 * 1024.0),
     (float)(0.14064627966562 * 1024.0),
     (float)(-0.07570126817606 * 1024.0),
     (float)(-0.04515627918959 * 1024.0),
     (float)(0.02642569966785 * 1024.0),
     (float)(0.01342207139682 * 1024.0),
     (float)(-0.00385006522758 * 1024.0)},
    512.0,
    10.0}},
  {// 18 DF_SSW3
   {{(float)(0.22473425589622 * 1024.0),
     (float)(0.55053148820757 * 1024.0),
     (float)(0.22473425589622 * 1024.0)},
    (float)(512.0),
    (float)(10.0)},
   {{(float)(0.06930869439185 * 1024.0),
     (float)(0.43069130560815 * 1024.0),
     (float)(0.43069130560815 * 1024.0),
     (float)(.06930869439185 * 1024.0)},
    512.0,
    10.0}},
  {// 19 DF_SSW5
   {{(float)(-0.00963149700658 * 1024.0),
     (float)(0.25498154223039 * 1024.0),
     (float)(0.50929990955238 * 1024.0),
     (float)(0.25498154223039 * 1024.0),
     (float)(-0.00963149700658 * 1024.0)},
    512.0,
    10.0},
   {{(float)(-0.01970384769087 * 1024.0),
     (float)(0.08511964870014 * 1024.0),
     (float)(0.43458419899072 * 1024.0),
     (float)(0.43458419899072 * 1024.0),
     (float)(0.08511964870014 * 1024.0),
     (float)(-0.01970384769087 * 1024.0)},
    512.0,
    10.0}},
  {// 20 DF_SSW7
   {{(float)(-0.03322983277383 * 1024.0),
     (float)(-0.01765304050413 * 1024.0),
     (float)(0.28904589924126 * 1024.0),
     (float)(0.52367394807341 * 1024.0),
     (float)(0.28904589924126 * 1024.0),
     (float)(-0.01765304050413 * 1024.0),
     (float)(-0.03322983277383 * 1024.0)},
    512.0,
    10.0},
   {{(float)(-0.01229101508504 * 1024.0),
     (float)(-0.06539024269701 * 1024.0),
     (float)(0.11223360796918 * 1024.0),
     (float)(0.46544764981287 * 1024.0),
     (float)(0.46544764981287 * 1024.0),
     (float)(0.11223360796918 * 1024.0),
     (float)(-0.06539024269701 * 1024.0),
     (float)(-0.01229101508504 * 1024.0)},
    512.0,
    10.0}},
  {// 21 DF_SSW11
   {{(float)(0.01119294477411 * 1024.0),
     (float)(0.01168400151526 * 1024.0),
     (float)(-0.06976399068352 * 1024.0),
     (float)(-0.02234931676070 * 1024.0),
     (float)(0.30569641505283 * 1024.0),
     (float)(0.52707989220403 * 1024.0),
     (float)(0.30569641505283 * 1024.0),
     (float)(-0.02234931676070 * 1024.0),
     (float)(-0.06976399068352 * 1024.0),
     (float)(0.01168400151526 * 1024.0),
     (float)(0.01119294477411 * 1024.0)},
    512.0,
    10.0},
   {{(float)(0.00103847390257 * 1024.0),
     (float)(0.02109981446330 * 1024.0),
     (float)(-0.02474235739652 * 1024.0),
     (float)(-0.08253987601651 * 1024.0),
     (float)(0.11973940989643 * 1024.0),
     (float)(0.46540453515074 * 1024.0),
     (float)(0.46540453515074 * 1024.0),
     (float)(0.11973940989643 * 1024.0),
     (float)(-0.08253987601651 * 1024.0),
     (float)(-0.02474235739652 * 1024.0),
     (float)(0.02109981446330 * 1024.0),
     (float)(0.00103847390257 * 1024.0)},
    512.0,
    10.0}},
  {// 22 DF_SSW15
   {{(float)(-0.00181153610954 * 1024.0),
     (float)(-0.00658767969886 * 1024.0),
     (float)(0.02692953876060 * 1024.0),
     (float)(0.01656650338770 * 1024.0),
     (float)(-0.08127280734884 * 1024.0),
     (float)(-0.02362649736930 * 1024.0),
     (float)(0.30806859050699 * 1024.0),
     (float)(0.52346777574252 * 1024.0),
     (float)(0.30806859050699 * 1024.0),
     (float)(-0.02362649736930 * 1024.0),
     (float)(-0.08127280734884 * 1024.0),
     (float)(0.01656650338770 * 1024.0),
     (float)(0.02692953876060 * 1024.0),
     (float)(-0.00658767969886 * 1024.0),
     (float)(-0.00181153610954 * 1024.0)},
    512.0,
    10.0},
   {{(float)(-0.00014417970451 * 1024.0),
     (float)(-0.01068600519132 * 1024.0),
     (float)(0.00789216874784 * 1024.0),
     (float)(0.03867103862476 * 1024.0),
     (float)(-0.03240217401926 * 1024.0),
     (float)(-0.09255303893640 * 1024.0),
     (float)(0.12396170720282 * 1024.0),
     (float)(0.46526048327607 * 1024.0),
     (float)(0.46526048327607 * 1024.0),
     (float)(0.12396170720282 * 1024.0),
     (float)(-0.09255303893640 * 1024.0),
     (float)(-0.03240217401926 * 1024.0),
     (float)(0.03867103862476 * 1024.0),
     (float)(0.00789216874784 * 1024.0),
     (float)(-0.01068600519132 * 1024.0),
     (float)(-0.00014417970451 * 1024.0)},
    512.0,
    10.0}}};

static std::vector<Filter420to444> g_filter420to444 = {
  {// 0 UF_F0
   {{0.0, +256.0}, +128.0, 8.0},
   {{-8.0, +64.0, +216.0, -16.0}, +128.0, 8.0},
   {{-16.0, +144.0, +144.0, -16.0}, +128.0, 8.0},
   {{-16.0, +216.0, +64.0, -8.0}, +128.0, 8.0}},
  {// 1 UF_FV
   {{0.0, +256.0}, +128.0, 8.0},
   {{+0.0, -16.0, +56.0, +240.0, -32.0, +8.0}, +128.0, 8.0},
   {{-16.0, +144.0, +144.0, -16.0}, +128.0, 8.0},
   {{+8.0, -32.0, +240.0, +56.0, -16.0, +0.0}, +128.0, 8.0}},
  {// 2 UF_GS
   {{0.0, +256.0}, +128.0, 8.0},
   {{-6.0, +58.0, +222.0, -18.0}, +128.0, 8.0},
   {{-16.0, +144.0, +144.0, -16.0}, +128.0, 8.0},
   {{-18.0, +222.0, +58.0, -6.0}, +128.0, 8.0}},
  {// 3 UF_LS3
   {{0.0, +256.0}, +128.0, 8.0},
   {{+2.0, -18.0, +70.0, +228.0, -34.0, +8.0}, +128.0, 8.0},
   {{+6.0, -34.0, +156.0, +156.0, -34.0, +6.0}, +128.0, 8.0},
   {{+8.0, -34.0, +228.0, +70.0, -18.0, +2.0}, +128.0, 8.0}},
  {// 4 UF_LS4
   {{0.0, +256.0}, +128.0, 8.0},
   {{-1.0, +8.0, -23.0, +72.0, +229.0, -39.0, +14.0, -4.0}, +128.0, 8.0},
   {{-3.0, +15.0, -43.0, +159.0, +159.0, -43.0, +15.0, -3.0}, +128.0, 8.0},
   {{-4.0, +14.0, -39.0, +229.0, +72.0, -23.0, +8.0, -1.0}, +128.0, 8.0}},
  {// 5 UF_TM
   {{0.0, +256.0}, +128.0, 8.0},
   {{+3.0, -16.0, +67.0, +227.0, -32.0, +7.0}, +128.0, 8.0},
   {{+21.0, -52.0, +159.0, +159.0, -52.0, +21.0}, +128.0, 8.0},
   {{+7.0, -32.0, +227.0, +67.0, -16.0, +3.0}, +128.0, 8.0}},
  {// 6 UF_LS5
   {{0.0, +256.0}, +128.0, 8.0},
   {{+1.0, -5.0, +12.0, -27.0, +74.0, +230.0, -41.0, +18.0, -8.0, +2.0},
    +128.0,
    8.0},
   {{+2.0, -8.0, +21.0, -47.0, +160.0, +160.0, -47.0, +21.0, -8.0, +2.0},
    +128.0,
    8.0},
   {{+2.0, -8.0, +18.0, -41.0, +230.0, +74.0, -27.0, +12.0, -5.0, +1.0},
    +128.0,
    8.0}},
  {// 7 UF_LS6
   {{0.0, +256.0}, +128.0, 8.0},
   {{0.0,
     +3.0,
     -7.0,
     +14.0,
     -29.0,
     +75.0,
     +230.0,
     -43.0,
     +20.0,
     -10.0,
     +5.0,
     -2.0},
    +128.0,
    8.0},
   {{-1.0,
     +5.0,
     -12.0,
     +24.0,
     -49.0,
     +161.0,
     +161.0,
     -49.0,
     +24.0,
     -12.0,
     +5.0,
     -1.0},
    +128.0,
    8.0},
   {{-2.0,
     +5.0,
     -10.0,
     +20.0,
     -43.0,
     +230.0,
     +75.0,
     -29.0,
     +14.0,
     -7.0,
     +3.0,
     0.0},
    +128.0,
    8.0}}};

template<typename T>
InternalColourConverter<T>::InternalColourConverter() = default;

template<typename T>
InternalColourConverter<T>::~InternalColourConverter() = default;

template<typename T>
void
InternalColourConverter<T>::initialize(std::string configuration) {
  if ((!extractParameters(configuration))
      && (srcColourSpace == ColourSpace::UNKNOW
          || dstColourSpace == ColourSpace::UNKNOW || srcBitdepth == -1
          || dstBitdepth == -1 || filter == -1 || range == -1)) {
    std::cout << "ColourConverter not correct: " << configuration
              << " => format " << srcColourSpace << " to " << dstColourSpace
              << " bits = " << srcBitdepth << " " << dstBitdepth
              << " filter = " << filter << " range = " << range << std::endl;
    exit(-1);
  }
}

template<typename T>
void
InternalColourConverter<T>::convert(FrameSequence<T>& src) {
  for (int i = 0; i < src.frameCount(); i++) { convert(src[i]); }
  src.colourSpace() = dstColourSpace;
}

template<typename T>
void
InternalColourConverter<T>::convert(Frame<T>& src) {
  Frame<T> dst;
  dst.resize(src.width(), src.height(), dstColourSpace);
  convert(src, dst);
  src.swap(dst);
}

template<typename T>
void
InternalColourConverter<T>::convert(FrameSequence<T>& src,
                                    FrameSequence<T>& dst) {
  dst.clear();
  dst.resize(src.width(), src.height(), dstColourSpace, src.frameCount());
  std::cout << "ColourConverter format " << srcColourSpace << " to "
            << dstColourSpace << " bits = " << srcBitdepth << " "
            << dstBitdepth << " filter = " << filter << " range = " << range
            << " frame = " << src.frameCount() << std::endl;
  for (int i = 0; i < src.frameCount(); i++) { convert(src[i], dst[i]); }
}

template<typename T>
void
InternalColourConverter<T>::convert(Frame<T>& src, Frame<T>& dst) {
  const int srcNB = srcBitdepth == 8 ? 1 : 2;
  const int dstNB = dstBitdepth == 8 ? 1 : 2;
  if (srcColourSpace == ColourSpace::YUV420p
      && dstColourSpace == ColourSpace::YUV444p) {
    convertYUV420ToYUV444(src, dst, srcNB, dstNB, filter, range);
  } else if (srcColourSpace == ColourSpace::YUV420p
             && dstColourSpace == ColourSpace::RGB444p) {
    convertYUV420ToRGB444(src, dst, srcNB, dstNB, filter, false, range);
  } else if (srcColourSpace == ColourSpace::YUV420p
             && dstColourSpace == ColourSpace::BGR444p) {
    convertYUV420ToRGB444(src, dst, srcNB, dstNB, filter, true, range);
  } else if (srcColourSpace == ColourSpace::RGB444p
             && dstColourSpace == ColourSpace::YUV420p) {
    convertRGB44ToYUV420(src, dst, srcNB, dstNB, filter, false, range);
  } else if (srcColourSpace == ColourSpace::BGR444p
             && dstColourSpace == ColourSpace::YUV420p) {
    convertRGB44ToYUV420(src, dst, srcNB, dstNB, filter, true, range);
  } else if (srcColourSpace == ColourSpace::RGB444p
             && dstColourSpace == ColourSpace::YUV444p) {
    convertRGB44ToYUV444(src, dst, srcNB, dstNB, filter, range);
  } else if (srcColourSpace == ColourSpace::YUV444p
             && dstColourSpace == ColourSpace::RGB444p) {
    convertYUV444ToRGB444(src, dst, srcNB, dstNB, filter, range);
  } else {
    std::cout << "ColourConverter format not supported: " << srcColourSpace
              << " to " << dstColourSpace << std::endl;
    exit(-1);
  }
}

template<typename T>
bool
InternalColourConverter<T>::extractParameters(std::string& configuration) {
  srcColourSpace = ColourSpace::UNKNOW;
  dstColourSpace = ColourSpace::UNKNOW;
  srcBitdepth    = -1;
  dstBitdepth    = -1;
  filter         = -1;
  range          = -1;
  std::istringstream ss(configuration);
  int                index = 0;
  std::string        token;
  while (std::getline(ss, token, '_')) {
    std::istringstream scp(token);
    switch (index) {
    case 0: scp >> srcColourSpace; break;
    case 1: scp >> dstColourSpace; break;
    case 2: srcBitdepth = stoi(token); break;
    case 3: dstBitdepth = stoi(token); break;
    case 4: filter = stoi(token); break;
    case 5: range = stoi(token); break;
    default: return false; break;
    }
    index++;
  }
  return true;
}

template<typename T>
void
InternalColourConverter<T>::convertRGB44ToYUV420(Frame<T>& src,
                                                 Frame<T>& dst,
                                                 size_t    srcNumByte,
                                                 size_t    dstNumByte,
                                                 size_t    filter,
                                                 bool      BGR,
                                                 bool      range) {
  const auto width  = src.width();
  const auto height = src.height();
  dst.resize(width, height, dstColourSpace);
  Plane<float> rgb[3];
  Plane<float> yuv[5];
  RGBtoFloatRGB(src[BGR ? 2 : 0], rgb[0], srcNumByte);
  RGBtoFloatRGB(src[1], rgb[1], srcNumByte);
  RGBtoFloatRGB(src[BGR ? 0 : 2], rgb[2], srcNumByte);
  convertRGBToYUV(rgb[0], rgb[1], rgb[2], yuv[0], yuv[1], yuv[2]);
  downsampling(yuv[1], yuv[3], srcNumByte == 1 ? 255 : 1023, filter);
  downsampling(yuv[2], yuv[4], srcNumByte == 1 ? 255 : 1023, filter);
  floatYUVToYUV(yuv[0], dst[0], 0, dstNumByte, range);
  floatYUVToYUV(yuv[3], dst[1], 1, dstNumByte, range);
  floatYUVToYUV(yuv[4], dst[2], 1, dstNumByte, range);
  // printf("convertRGB44ToYUV420  srcNumByte = %zu srcNumByte = %zu srcNumByte "
  //        "%zu range = %d \n",
  //        srcNumByte,
  //        dstNumByte,
  //        filter,
  //        range);
  // const int u0 = 1415, v0 = 495, n = 6;
  // src[0].log("src  B", u0, v0, n );
  // src[1].log("src  G", u0, v0, n );
  // src[2].log("src  R", u0, v0, n );
  // rgb[0].logF("floa B", u0, v0, n );
  // rgb[1].logF("floa G", u0, v0, n );
  // rgb[2].logF("floa R", u0, v0, n );
  // yuv[0].logF("444[0]", u0, v0, n );
  // yuv[1].logF("444[1]", u0, v0, n );
  // yuv[2].logF("444[2]", u0, v0, n );
  // yuv[0].logF("420[0]", u0, v0, n);
  // yuv[3].logF("420[1]", u0 >> 1, v0 >> 1, n >> 1);
  // yuv[4].logF("420[2]", u0 >> 1, v0 >> 1, n >> 1);
  // dst[0].log("dst  Y", u0, v0, n);
  // dst[1].log("dst  U", u0 >> 1, v0 >> 1, n >> 1);
  // dst[2].log("dst  V", u0 >> 1, v0 >> 1, n >> 1);
}

template<typename T>
void
InternalColourConverter<T>::convertRGB44ToYUV444(Frame<T>& src,
                                                 Frame<T>& dst,
                                                 size_t    srcNumByte,
                                                 size_t    dstNumByte,
                                                 size_t /*filter*/,
                                                 bool range) {
  dst.resize(src.width(), src.height(), dstColourSpace);
  Plane<float> rgb[3];
  Plane<float> yuv[3];
  RGBtoFloatRGB(src[0], rgb[0], srcNumByte);
  RGBtoFloatRGB(src[1], rgb[1], srcNumByte);
  RGBtoFloatRGB(src[2], rgb[2], srcNumByte);
  convertRGBToYUV(rgb[0], rgb[1], rgb[2], yuv[0], yuv[1], yuv[2]);
  floatYUVToYUV(yuv[0], dst[0], 0, dstNumByte, range);
  floatYUVToYUV(yuv[1], dst[1], 1, dstNumByte, range);
  floatYUVToYUV(yuv[2], dst[2], 1, dstNumByte, range);
}

template<typename T>
void
InternalColourConverter<T>::convertYUV420ToYUV444(Frame<T>& src,
                                                  Frame<T>& dst,
                                                  size_t    srcNumByte,
                                                  size_t    dstNumByte,
                                                  size_t    filter,
                                                  bool      range) {
  const auto width  = src.width();
  const auto height = src.height();
  dst.resize(width, height, dstColourSpace);
  Plane<float> YUV444[3];
  Plane<float> YUV420[3];
  YUVtoFloatYUV(src[0], YUV420[0], 0, srcNumByte, range);
  YUVtoFloatYUV(src[1], YUV420[1], 1, srcNumByte, range);
  YUVtoFloatYUV(src[2], YUV420[2], 1, srcNumByte, range);
  upsampling(YUV420[1], YUV444[1], srcNumByte == 1 ? 255 : 1023, filter);
  upsampling(YUV420[2], YUV444[2], srcNumByte == 1 ? 255 : 1023, filter);
  dst.resize(width, height, ColourSpace::YUV444p);
  floatYUVToYUV(YUV420[0], dst[0], 0, dstNumByte, range);
  floatYUVToYUV(YUV444[1], dst[1], 1, dstNumByte, range);
  floatYUVToYUV(YUV444[2], dst[2], 1, dstNumByte, range);
}

template<typename T>
void
InternalColourConverter<T>::convertYUV420ToRGB444(Frame<T>& src,
                                                  Frame<T>& dst,
                                                  size_t    srcNumByte,
                                                  size_t    dstNumByte,
                                                  size_t    filter,
                                                  bool      BGR,
                                                  bool      range) {
  const auto width  = src.width();
  const auto height = src.height();
  dst.resize(width, height, dstColourSpace);
  Plane<float> YUV444[3];
  Plane<float> YUV420[3];
  Plane<float> RGB444[3];

  YUVtoFloatYUV(src[0], YUV420[0], 0, srcNumByte, range);
  YUVtoFloatYUV(src[1], YUV420[1], 1, srcNumByte, range);
  YUVtoFloatYUV(src[2], YUV420[2], 1, srcNumByte, range);
  upsampling(YUV420[1], YUV444[1], srcNumByte == 1 ? 255 : 1023, filter);
  upsampling(YUV420[2], YUV444[2], srcNumByte == 1 ? 255 : 1023, filter);
  convertYUVToRGB(
    YUV420[0], YUV444[1], YUV444[2], RGB444[0], RGB444[1], RGB444[2]);
  floatRGBToRGB(RGB444[0], dst[BGR ? 2 : 0], dstNumByte);
  floatRGBToRGB(RGB444[1], dst[1], dstNumByte);
  floatRGBToRGB(RGB444[2], dst[BGR ? 0 : 2], dstNumByte);

  // const int u0= 704, v0=3;
  // src[0].log("src  Y", u0, v0);
  // src[1].log("src  U", u0, v0);
  // src[2].log("src  V", u0, v0);
  // YUV420[0].logF("floa Y", u0, v0);
  // YUV420[1].logF("floa U", u0 / 2, v0 / 2);
  // YUV420[2].logF("floa V", u0 / 2, v0 / 2);
  // YUV420[0].logF("444[0]", u0, v0);
  // YUV444[1].logF("444[1]", u0, v0);
  // YUV444[2].logF("444[2]", u0, v0);
  // RGB444[0].logF("RGB[0]", u0, v0);
  // RGB444[1].logF("RGB[1]", u0, v0);
  // RGB444[2].logF("RGB[2]", u0, v0);
  // dst[0].log("dst  R",u0, v0);
  // dst[1].log("dst  G",u0, v0);
  // dst[2].log("dst  B",u0, v0);
}

template<typename T>
void
InternalColourConverter<T>::convertYUV444ToRGB444(Frame<T>& src,
                                                  Frame<T>& dst,
                                                  size_t    srcNumByte,
                                                  size_t    dstNumByte,
                                                  size_t /*filter*/,
                                                  bool range) {
  const auto width  = src.width();
  const auto height = src.height();
  dst.resize(width, height, dstColourSpace);
  Plane<float> YUV444[3];
  Plane<float> RGB444[3];
  YUVtoFloatYUV(src[0], YUV444[0], 0, srcNumByte, range);
  YUVtoFloatYUV(src[1], YUV444[1], 1, srcNumByte, range);
  YUVtoFloatYUV(src[2], YUV444[2], 1, srcNumByte, range);
  convertYUVToRGB(
    YUV444[0], YUV444[1], YUV444[2], RGB444[0], RGB444[1], RGB444[2]);
  floatRGBToRGB(RGB444[0], dst[0], dstNumByte);
  floatRGBToRGB(RGB444[1], dst[1], dstNumByte);
  floatRGBToRGB(RGB444[2], dst[2], dstNumByte);
}

template<typename T>
void
InternalColourConverter<T>::RGBtoFloatRGB(const Plane<T>& src,
                                          Plane<float>&   dst,
                                          const size_t    nbyte) const {
  float offset = nbyte == 1 ? 255.F : 1023.F;
  dst.resize(src.width(), src.height());
  for (int i = 0; i < src.height(); i++) {
    for (int j = 0; j < src.width(); j++) {
      dst.get(i, j) = (float)src.get(i, j) / offset;
    }
  }
}

template<typename T>
void
InternalColourConverter<T>::convertRGBToYUV(const Plane<float>& R,
                                            const Plane<float>& G,
                                            const Plane<float>& B,
                                            Plane<float>&       Y,
                                            Plane<float>&       U,
                                            Plane<float>&       V) const {
  Y.resize(R.width(), R.height());
  U.resize(R.width(), R.height());
  V.resize(R.width(), R.height());
  for (int i = 0; i < R.height(); i++) {
    for (int j = 0; j < R.width(); j++) {
      Y.get(i, j) =
        (float)((double)clamp(0.212600 * R.get(i, j) + 0.715200 * G.get(i, j)
                                + 0.072200 * B.get(i, j),
                              0.0,
                              1.0));
      U.get(i, j) =
        (float)((double)clamp(-0.114572 * R.get(i, j) - 0.385428 * G.get(i, j)
                                + 0.500000 * B.get(i, j),
                              -0.5,
                              0.5));
      V.get(i, j) =
        (float)((double)clamp(0.500000 * R.get(i, j) - 0.454153 * G.get(i, j)
                                - 0.045847 * B.get(i, j),
                              -0.5,
                              0.5));
    }
  }
}

template<typename T>
void
InternalColourConverter<T>::floatYUVToYUV(const Plane<float>& src,
                                          Plane<T>&           dst,
                                          const bool          chroma,
                                          const size_t        nbyte,
                                          bool                range) const {
  dst.resize(src.width(), src.height());

  int numBits  = nbyte == 1 ? 8 : 10;
  int maxValue = (1 << numBits) - 1;
  // Full
  // weight = (1 << numBits) - 1.0; offset = 0.0;
  // weight = (1 << numBits) - 1.0; offset = 1 << (numBits-1);
  // weight = (1 << numBits) - 1.0; offset = 1 << (numBits-1);

  // limited
  // weight = (1 << (numBits-8)) * 219.0; offset = (1 << (numBits-8)) * 16.0;
  // weight = (1 << (numBits-8)) * 224.0; offset = (1 << (numBits-8)) * 128.0;
  // weight = (1 << (numBits-8)) * 224.0; offset = (1 << (numBits-8)) * 128.0;
  double offset = 0;
  double weight = 0;
  if (range == 0) {
    if (!chroma) {
      weight = (1 << (numBits - 8)) * 219.0;
      offset = (1 << (numBits - 8)) * 16.0;
    } else {
      weight = (1 << (numBits - 8)) * 224.0;
      offset = (1 << (numBits - 8)) * 128.0;
    }
  } else {
    if (!chroma) {
      weight = (1 << numBits) - 1.0;
      offset = 0;
    } else {
      weight = (1 << numBits) - 1.0;
      offset = 1 << (numBits - 1);
    }
  }
  for (int i = 0; i < src.height(); i++) {
    for (int j = 0; j < src.width(); j++) {
      dst.get(i, j) = static_cast<T>(
        fClip(std::round((float)(weight * (double)src.get(i, j) + offset)),
              0.F,
              (float)maxValue));
    }
  }
}

template<typename T>
void
InternalColourConverter<T>::YUVtoFloatYUV(const Plane<T>& src,
                                          Plane<float>&   dst,
                                          const bool      chroma,
                                          const size_t    nbBytes,
                                          const bool      range) const {
  dst.resize(src.width(), src.height());
  float    minV    = chroma ? -0.5F : 0.F;
  float    maxV    = chroma ? 0.5F : 1.F;
  uint8_t  numBits = nbBytes == 1 ? 8 : 10;
  uint16_t offset  = 0;
  double   weight  = 0.0;
  // 0: Limite range
  // weight = 1.0 / ((1 << (numBits-8)) * 219.0),  offset = 1 << (numBits-4);
  // weight = 1.0 / ((1 << (numBits-8)) * 224.0),  offset = 1 << (numBits-1);
  // weight = 1.0 / ((1 << (numBits-8)) * 224.0),  offset = 1 << (numBits-1);

  // 1: Full range
  // weight = 1.0 / ((1 << numBits) - 1.0); offset = 0;
  // weight = 1.0 / ((1 << numBits) - 1.0); offset = ((1 << (numBits-1)));
  // weight = 1.0 / ((1 << numBits) - 1.0); offset = ((1 << (numBits-1)));
  if (range == 0) {
    if (!chroma) {
      weight = 1.0 / ((1 << (numBits - 8)) * 219.0);
      offset = 1 << (numBits - 4);
    } else {
      weight = 1.0 / ((1 << (numBits - 8)) * 224.0);
      offset = 1 << (numBits - 1);
    }
  } else {
    if (!chroma) {
      weight = 1.0 / ((1 << numBits) - 1.0);
      offset = 0;
    } else {
      weight = 1.0 / ((1 << numBits) - 1.0);
      offset = 1 << (numBits - 1);
    }
  }

  for (int i = 0; i < src.height(); i++) {
    for (int j = 0; j < src.width(); j++) {
      dst.get(i, j) =
        clamp((float)(weight * (double)(src.get(i, j) - offset)), minV, maxV);
    }
  }
}

template<typename T>
void
InternalColourConverter<T>::convertYUVToRGB(const Plane<float>& Y,
                                            const Plane<float>& U,
                                            const Plane<float>& V,
                                            Plane<float>&       R,
                                            Plane<float>&       G,
                                            Plane<float>&       B) const {
  R.resize(Y.width(), Y.height());
  G.resize(Y.width(), Y.height());
  B.resize(Y.width(), Y.height());
  for (int i = 0; i < Y.height(); i++) {
    for (int j = 0; j < Y.width(); j++) {
      R.get(i, j) =
        (float)((double)clamp(Y.get(i, j) + 1.57480 * V.get(i, j), 0.0, 1.0));
      G.get(i, j) = (float)((double)clamp(Y.get(i, j) - 0.18733 * U.get(i, j)
                                            - 0.46813 * V.get(i, j),
                                          0.0,
                                          1.0));
      B.get(i, j) =
        (float)((double)clamp(Y.get(i, j) + 1.85563 * U.get(i, j), 0.0, 1.0));
    }
  }
}

template<typename T>
void
InternalColourConverter<T>::floatRGBToRGB(const Plane<float>& src,
                                          Plane<T>&           dst,
                                          const size_t        nbyte) const {
  dst.resize(src.width(), src.height());
  float scale = nbyte == 1 ? 255.F : 1023.F;
  for (int i = 0; i < src.height(); i++) {
    for (int j = 0; j < src.width(); j++) {
      dst.get(i, j) = static_cast<T>(
        clamp((T)std::round(scale * src.get(i, j)), (T)0, (T)scale));
    }
  }
}

template<typename T>
void
InternalColourConverter<T>::downsampling(const Plane<float>& src,
                                         Plane<float>&       dst,
                                         const int /*maxValue*/,
                                         const size_t filter) const {
  const auto   widthOut  = src.width() / 2;
  const auto   heightOut = src.height() / 2;
  Plane<float> temp;
  temp.resize(widthOut, src.height());
  dst.resize(widthOut, heightOut);
  for (int i = 0; i < src.height(); i++) {
    for (int j = 0; j < widthOut; j++) {
      temp(i, j) =
        downsamplingHorizontal(g_filter444to420[filter], src, i, j * 2);
    }
  }
  for (int i = 0; i < heightOut; i++) {
    for (int j = 0; j < widthOut; j++) {
      dst(i, j) =
        downsamplingVertical(g_filter444to420[filter], temp, 2 * i, j);
    }
  }
}

template<typename T>
void
InternalColourConverter<T>::upsampling(const Plane<float>& src,
                                       Plane<float>&       dst,
                                       const int /*maxValue*/,
                                       const size_t filter) const {
  const auto widthIn   = src.width();
  const auto heightIn  = src.height();
  const auto widthOut  = widthIn * 2;
  const auto heightOut = heightIn * 2;

  Plane<float> temp;
  dst.resize(widthOut, heightOut);
  temp.resize(widthIn, heightOut);
  for (int i = 0; i < heightIn; i++) {
    for (int j = 0; j < widthIn; j++) {
      temp.get(2 * i + 0, j) =
        upsamplingVertical0(g_filter420to444[filter], src, i + 0, j);
      temp.get(2 * i + 1, j) =
        upsamplingVertical1(g_filter420to444[filter], src, i + 1, j);
    }
  }
  for (int i = 0; i < heightOut; i++) {
    for (int j = 0; j < widthIn; j++) {
      dst.get(i, j * 2 + 0) =
        upsamplingHorizontal0(g_filter420to444[filter], temp, i, j + 0);
      dst.get(i, j * 2 + 1) =
        upsamplingHorizontal1(g_filter420to444[filter], temp, i, j + 1);
    }
  }
}

template<typename T>
void
InternalColourConverter<T>::upsample(FrameSequence<T>& video,
                                     size_t            rate,
                                     size_t            srcNumByte,
                                     size_t            dstNumByte,
                                     size_t            filter,
                                     bool              range) {
  for (auto& image : video) {
    upsample(image, rate, srcNumByte, dstNumByte, filter, range);
  }
}

template<typename T>
void
InternalColourConverter<T>::upsample(Frame<T>& image,
                                     size_t    rate,
                                     size_t    srcNumByte,
                                     size_t    dstNumByte,
                                     size_t    filter,
                                     bool      range) {
  for (size_t i = rate; i > 1; i /= 2) {
    int          width  = (int)image.width();
    int          height = (int)image.height();
    Plane<float> src[3];
    Plane<float> up[3];
    YUVtoFloatYUV(image[0], src[0], 0, srcNumByte, range);
    YUVtoFloatYUV(image[1], src[1], 1, srcNumByte, range);
    YUVtoFloatYUV(image[2], src[2], 1, srcNumByte, range);
    upsampling(src[0], up[0], srcNumByte == 1 ? 255 : 1023, filter);
    upsampling(src[1], up[1], srcNumByte == 1 ? 255 : 1023, filter);
    upsampling(src[2], up[2], srcNumByte == 1 ? 255 : 1023, filter);
    image.resize(width * 2, height * 2, ColourSpace::YUV444p);
    floatYUVToYUV(up[0], image[0], 0, dstNumByte, range);
    floatYUVToYUV(up[1], image[1], 1, dstNumByte, range);
    floatYUVToYUV(up[2], image[2], 1, dstNumByte, range);
  }
}

template class InternalColourConverter<uint8_t>;
template class InternalColourConverter<uint16_t>;

}  // namespace vmesh