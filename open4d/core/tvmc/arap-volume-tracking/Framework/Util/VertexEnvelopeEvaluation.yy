using System;
using System.Collections.Generic;
using System.Numerics;

namespace Framework
{
    public class VertexEnvelopeEvaluation
    {

        public static Vector4[][] Evaluate(Vector4[][] vertices, Vector4[][] pc, Vector4[] mds, int n)
        {
            Vector4[][] output = new Vector4[vertices.Length][];
            Vector4[] toMDS = new Vector4[mds.Length];
            List<float> dist = new List<float>();
            List<int> indices = new List<int>();

            float maxdist = 0.1f;

            for (int i = 0; i < output.Length; i++)
            {
                output[i] = new Vector4[vertices[0].Length];

                for (int j = 0; j < mds.Length; j++)
                {
                    toMDS[j] = mds[j] - pc[i][j];
                }

                KDTree kd = new KDTree(pc[i]);

                for (int j = 0; j < vertices[0].Length; j++)
                {
                    dist.Clear();
                    indices.Clear();

                    Vector4 v = vertices[i][j];
                    kd.findNearest(v, dist, indices, maxdist);
                    while (indices.Count < n)
                    {
                        dist.Clear();
                        indices.Clear();
                        maxdist *= 2;
                        kd.findNearest(v, dist, indices, maxdist);

                        if (indices.Count >= 2 * n)
                        {
                            maxdist *= 0.5f;
                        }
                    }

                    var distArr = dist.ToArray();
                    var indicesArr = indices.ToArray();

                    Array.Sort(distArr, indicesArr);

                    Vector4 vs = new Vector4();

                    float[] weights = new float[n];
                    float ws = 0;

                    for (int k = 0; k < n; k++)
                    {
                        weights[k] = 1 - distArr[k] / distArr[n - 1];
                        ws += weights[k];
                    }

                    for (int k = 0; k < n; k++)
                    {
                        vs += (weights[k] / ws) * toMDS[indicesArr[k]];
                    }

                    output[i][j] = v + vs;
                }
            }

            return output;
        }

    }
}
