//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Numerics;

namespace Framework
{
    public class BilateralSmoothWeights : SmoothWeightsWithHistory
    {
        private readonly float k0;
        private readonly float kMax;
        private readonly float falloff;

        public BilateralSmoothWeights(int n, float k0, float kMax, float falloff) : base(n)
        {
            this.k0 = k0;
            this.kMax = kMax;
            this.falloff = falloff;
        }

        public override void Update(Vector4[][] pc, int frame, VolumeGrid vg = null)
        {
            Vector4[] vectors = new Vector4[n];
            Vector4[] framePc = pc[frame];

            for (int i = 0; i < n; i++)
            {
                vectors[i] = framePc[i] - pc[frame - 1][i];
            };

            for (int i = 0; i < n; i++)
            {
                Vector4 point = framePc[i];
                Vector4 vec = vectors[i];

                for (int j = i; j < n; j++)
                {
                    var r = (point - framePc[j]).LengthSquared();
                    var w1 = MathF.Exp(-k0 * r);

                    var s = (vec - vectors[j]).LengthSquared();
                    var w2 = MathF.Exp(-kMax * s);

                    if (frame > 1)
                    {
                        w2 = (1f - falloff) * history[i][j] + w2 * falloff;
                    }

                    var weight = w1 * w2;
                    weights[i][j] = weight;
                    weights[j][i] = weight;
                    historyNew[i][j] = w2;
                    historyNew[j][i] = w2;
                }
            };

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
