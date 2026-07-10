//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System.Numerics;
using System.Threading.Tasks;

namespace Framework
{
    public class MaxDistSmoothWeights : SmoothWeights
    {
        private readonly float k0;

        public MaxDistSmoothWeights(Vector4[][] pc, float k0) : base(pc[0].Length)
        {
            this.k0 = k0;
        }

        public override void Update(Vector4[][] pc, int frame, VolumeGrid vg = null)
        {
            //do nothing for now
        }

        public override void UpdateFull(Vector4[][] pc, VolumeGrid[] vg = null)
        {
            float[,] max = new float[n, n];

            for (int frame = 0; frame < pc.Length; frame++)
            {
                float[,] dist = Distances.SpatialDistance(pc[frame]);

                for (int i = 0; i < n; i++)
                {
                    for (int j = i + 1; j < n; j++)
                    {
                        if (dist[i, j] > max[i, j])
                        {
                            max[i, j] = dist[i, j];
                            max[j, i] = dist[i, j];
                        }
                    }
                }
            }

            UpdateFromDistances(max, k0);
        }
    }
}
