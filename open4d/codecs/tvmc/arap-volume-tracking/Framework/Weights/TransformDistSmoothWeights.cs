//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.Numerics;
using System.Threading.Tasks;

namespace Framework
{
    public class TransformDistSmoothWeights : SmoothWeightsWithHistory
    {
        private readonly float k0;
        private readonly float kMax;
        private readonly float falloff;

        public TransformDistSmoothWeights(Vector4[][] pc, float k0, float kMax, float falloff) : base(pc[0].Length)
        {
            this.k0 = k0;
            this.kMax = kMax;
            this.falloff = falloff;

            UpdateFromDistances(Distances.SpatialDistance(pc[0]), k0);
        }

        public override void Update(Vector4[][] pc, int frame, VolumeGrid vg = null)
        {
            Vector4[] framePc = pc[frame];

            List<Vector4>[] gridPoints = vg.GetPoints(framePc);

            Transform[] transforms = new Transform[n];
            TransformDistance[] dst = new TransformDistance[n];
            Parallel.For(0, n, i =>
            {
                transforms[i] = ARAP.GetTransform(pc[frame - 1], framePc, i, this);
                dst[i] = new TransformDistance(gridPoints[i]);
            });

            //Precalculate transforms

            //Precalculate grid points


            Parallel.For(0, n, i =>
            {
                Vector4 point = framePc[i];
                TransformDistance currentDst = dst[i];
                Transform A = transforms[i];

                float r, s, w1, w2;

                for (int j = 0; j < n; j++)
                {
                    //if (i == j)
                    //{
                    //    weights[i][j] = 1f;
                    //    continue;
                    //}

                    Transform B = transforms[j];

                    r = (point - framePc[j]).LengthSquared();
                    w1 = MathF.Exp(-k0 * r);

                    ////calculate transform distance
                    //float dist = currentDst.GetTransformDistance(A.R, B.R, A.t, B.t);

                    //s = history[i][j] + ((dist - history[i][j]) / (frame - 1));

                    //w2 = squared ? MathF.Exp(-kMax * s * s) : MathF.Exp(-kMax * s);

                    //var weight = w1 * w2;
                    //weights[i][j] = weight;
                    //historyNew[i][j] = s;

                    //calculate transform distance
                    s = currentDst.GetTransformDistance(A.R, B.R, A.t, B.t);
                    //s = currentDst.GetTransformDistance(A, B);
                    w2 = MathF.Exp(-kMax * s);

                    if (frame > 1)
                    {
                        w2 = (1f - falloff) * history[i][j] + w2 * falloff;
                    }

                    var weight = w1 * w2;
                    weights[i][j] = weight;
                    historyNew[i][j] = w2;
                }
            });

            float[][] tmp = history;
            history = historyNew;
            historyNew = tmp;
        }

        public override void UpdateFull(Vector4[][] pc, VolumeGrid[] vg = null)
        {
            //do nothing
        }
    }
}
