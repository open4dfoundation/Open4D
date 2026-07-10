using MathNet.Numerics.LinearAlgebra.Single;
using System;
using System.Linq;
using System.Numerics;
using TVMEditor.Editing.AffinityCalculation;
using TVMEditor.Extensions;
using TVMEditor.Math;
using TVMEditor.Structures;

namespace TVMEditor.Editing.Looping
{
    public class KabschLooping : ILooping
    {
        private int Neighbors { get; set; } = 7;
        private bool MultiplyByAffinity { get; set; } = true;

        private IAffinityCalculation AffinityCalculation { get; set; }

        public KabschLooping(IAffinityCalculation affinityCalculation)
        {
            AffinityCalculation = affinityCalculation;
        }

        public DualQuaternion[] ComputeFirstFrameCentersTransformations(Vector3[][] centers)
        {
            return ComputeTransformations(centers[0], centers[centers.Length - 1]);
        }

        public DualQuaternion[] ComputeLastFrameCentersTransformations(Vector3[][] centers)
        {
            return ComputeTransformations(centers[centers.Length - 1], centers[0]);
        }

        private DualQuaternion[] ComputeTransformations(Vector3[] sourceCenters, Vector3[] destCenters)
        {
            var transformations = new DualQuaternion[sourceCenters.Length];

            var neighbors = PrepareNeighborIndices();
            var (weights, weightsSums) = PrepareWeights(sourceCenters, neighbors);

            for (var c = 0; c < sourceCenters.Length; c++)
            {
                // Kabsch
                var oldCentroid = new DenseVector(3);
                var frameCentroid = new DenseVector(3);

                for (var d = 0; d < Neighbors; d++)
                {
                    var weight = weights[c, d];

                    oldCentroid[0] += weight * sourceCenters[neighbors[c, d]].X;
                    oldCentroid[1] += weight * sourceCenters[neighbors[c, d]].Y;
                    oldCentroid[2] += weight * sourceCenters[neighbors[c, d]].Z;

                    frameCentroid[0] += weight * destCenters[neighbors[c, d]].X;
                    frameCentroid[1] += weight * destCenters[neighbors[c, d]].Y;
                    frameCentroid[2] += weight * destCenters[neighbors[c, d]].Z;
                }

                /*oldCentroid[0] = sourceCenters[c].x;
                oldCentroid[1] = sourceCenters[c].y;
                oldCentroid[2] = sourceCenters[c].z;

                frameCentroid[0] = destCenters[c].x;
                frameCentroid[1] = destCenters[c].y;
                frameCentroid[2] = destCenters[c].z;*/

                // oldCentroid /= weightsSums[c];
                // frameCentroid /= weightsSums[c];

                // Construct matrices P and Q
                var matrixP = new DenseMatrix(Neighbors, 3);
                var matrixQ = new DenseMatrix(Neighbors, 3);
                for (var d = 0; d < Neighbors; d++)
                {
                    matrixP[d, 0] = sourceCenters[neighbors[c, d]].X - oldCentroid[0];
                    matrixP[d, 1] = sourceCenters[neighbors[c, d]].Y - oldCentroid[1];
                    matrixP[d, 2] = sourceCenters[neighbors[c, d]].Z - oldCentroid[2];

                    matrixQ[d, 0] = weights[c, d] * (destCenters[neighbors[c, d]].X - frameCentroid[0]);
                    matrixQ[d, 1] = weights[c, d] * (destCenters[neighbors[c, d]].Y - frameCentroid[1]);
                    matrixQ[d, 2] = weights[c, d] * (destCenters[neighbors[c, d]].Z - frameCentroid[2]);
                }

                // Compute covariance matrix H
                var matrixH = matrixP.TransposeThisAndMultiply(matrixQ);

                // Computation of the optimal rotation matrix
                var hSvd = matrixH.Svd();
                var det = System.Math.Sign((hSvd.VT.Transpose() * hSvd.U.Transpose()).Determinant());
                var matrixI = new DenseMatrix(3, 3);
                matrixI[0, 0] = 1;
                matrixI[1, 1] = 1;
                matrixI[2, 2] = det;

                var rotationMatrix = hSvd.VT.Transpose() * matrixI * hSvd.U.Transpose();
                var rotationQuaternion = Geometry.QuaternionFromMatrix(rotationMatrix);

                transformations[c] =
                    DualQuaternion.Translation(frameCentroid.ToVector3()) *
                    DualQuaternion.Rotation(rotationQuaternion) *
                    DualQuaternion.Translation(-oldCentroid.ToVector3());
            }

            return transformations;
        }

        private int[,] PrepareNeighborIndices()
        {
            float[,] affinity = AffinityCalculation.GetCentersAffinity();
            var centersNum = affinity.GetLength(0);
            var neighbors = new int[centersNum, Neighbors];

            // Pocitam vahy pro kazde centrum
            for (var c = 0; c < centersNum; c++)
            {
                // Dvojice index centra a afinita
                var centerAffinities = new Tuple<int, float>[centersNum];
                for (var d = 0; d < centersNum; d++)
                {
                    centerAffinities[d] = new Tuple<int, float>(d, affinity[c, d]);
                }

                // Seradim podle afinit
                var centerNeighbors = centerAffinities.OrderByDescending(x => x.Item2).Select(x => x.Item1).Take(Neighbors).ToArray();

                for (var d = 0; d < Neighbors; d++)
                {
                    neighbors[c, d] = centerNeighbors[d];
                }
            }

            return neighbors;
        }

        private (float[,], float[]) PrepareWeights(Vector3[] centers, int[,] neighbors)
        {
            float[,] affinity = null;
            if (AffinityCalculation != null)
            {
                affinity = AffinityCalculation.GetCentersAffinity();
            }
            else
            {
                affinity = new float[centers.Length, centers.Length];
                for (var i = 0; i < centers.Length; i++)
                {
                    for (var j = i + 1; j < centers.Length; j++)
                    {
                        affinity[i, j] = affinity[j, i] = -(centers[i] - centers[j]).Length();
                    }
                }
                MultiplyByAffinity = false;
            }

            var weights = new float[centers.Length, Neighbors];
            var weightsSums = new float[centers.Length];

            // Pocitam vahy pro kazde centrum
            for (var c = 0; c < centers.Length; c++)
            {
                // Spoctu vahy
                var weightsSum = 0f;
                for (var d = 0; d < Neighbors; d++) // Pres vsechny sousedy
                {
                    var weight = 1f; // Weight(centers[c], centers[NeighborIndices[c ,d]]);

                    if (MultiplyByAffinity)
                        weight *= AffinityCalculation.GetCentersAffinity()[c, neighbors[c, d]];

                    weights[c, d] = weight;
                    weightsSum += weight;
                }

                weightsSums[c] = weightsSum;

                // Znormalizuju vahy
                for (var d = 0; d < Neighbors; d++)
                {
                    weights[c, d] /= weightsSum;
                }
            }

            return (weights, weightsSums);
        }
    }
}
