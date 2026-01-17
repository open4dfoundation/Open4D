//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System.Numerics;
using System.Threading.Tasks;

namespace Framework
{
    public class AvgDistSmoothWeights : SmoothWeights
    {
        private readonly float k0;

        public AvgDistSmoothWeights(Vector4[][] pc, float k0) : base(pc[0].Length)
        {
            this.k0 = k0;

            UpdateFromDistances(Distances.SpatialDistance(pc[0]), k0);
        }

        public override void Update(Vector4[][] pc, int frame, VolumeGrid vg = null)
        {
            //do nothing for now
        }

        public override void UpdateFull(Vector4[][] pc, VolumeGrid[] vg = null)
        {
            float[,] avg = new float[n, n];

            for (int frame = 0; frame < pc.Length; frame++)
            {
                float[,] dist = Distances.SpatialDistance(pc[frame]);

                for (int i = 0; i < n; i++)
                {
                    for (int j = i + 1; j < n; j++)
                    {
                        avg[i, j] += dist[i, j];
                        avg[j, i] += dist[i, j];
                    }
                }
            }

            Parallel.For(0, n, i =>
            {
                for (int j = 0; j < n; j++)
                {
                    avg[i, j] /= n;
                }
            });

            UpdateFromDistances(avg, k0);
        }
    }
}
