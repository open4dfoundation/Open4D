using KdTree;
using KdTree.Math;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Threading.Tasks;
using TVMEditor.Editing.AffinityCalculation;
using TVMEditor.Structures;

namespace TVMEditor.Editing.SurfaceDeformation
{
    public class CustomSurfaceDeformation : ISurfaceDeformation
    {
        public int MaxSplitIterations { get; set; } = 3;

        public int Neighbors { get; set; } = 5;
        public float LimitEpsilon { get; set; } = 1e-6f;
        public float Shape { get; set; } = 2f;
        public bool ResampleMesh { get; set; } = false;
        public IAffinityCalculation AffinityCalculation { get; set; }

        private Dictionary<int, List<int[]>> Centers = new Dictionary<int, List<int[]>>();
        private Dictionary<int, List<float[]>> Weights = new Dictionary<int, List<float[]>>();

        public CustomSurfaceDeformation(IAffinityCalculation affinityCalculation)
        {
            AffinityCalculation = affinityCalculation;
        }

        public TriangleMesh DeformSurface(Vector3[] vertices, Face[] faces, Vector3[] oldCenters, Vector3[] newCenters, int frameIndex, DualQuaternion[] transformations)
        {
            if (!Centers.ContainsKey(frameIndex))  // Also on change
                ComputeWeights(vertices, oldCenters, frameIndex);
           // else
           //     Console.WriteLine($"Weights precomputed: {frameIndex}");

            var centersArray = Centers[frameIndex];
            var weightsArray = Weights[frameIndex];

            // Queue with edges to split
            var verticesList = new List<Vector3>();

            // Original vertices deformations
            var vertexTransformations = new Dictionary<int, DualQuaternion>();
            for (var v = 0; v < vertices.Length; v++)
            {
                var weightedTransformation = DualQuaternion.Zero();

                for (var c = 0; c < centersArray[v].Length; c++)
                {
                    var centerIndex = centersArray[v][c];
                    var weight = weightsArray[v][c];

                    weightedTransformation += weight * transformations[centerIndex];
                }

                verticesList.Add(weightedTransformation.Normalize().Transform(vertices[v]));
                vertexTransformations[v] = weightedTransformation.Normalize();
            }

            if (!ResampleMesh)
            {
                return new TriangleMesh
                {
                    Vertices = verticesList.ToArray(),
                    Faces = faces
                };
            }

            // Pripravim si KD-strom s centry pro vypocet novych vah
            var kdTree = new KdTree<float, int>(3 , new FloatMath());
            for (var c = 0; c < oldCenters.Length; c++)
            {
                var centerPosition = oldCenters[c];
                kdTree.Add(new float[] { centerPosition.X, centerPosition.Y, centerPosition.Z }, c);
            }

            var newFaces = new HashSet<Face>(faces);
            var edgesMidPoints = new Dictionary<Edge, int>();

            var resampleIteration = 0;
            while (resampleIteration++ < MaxSplitIterations)
            {
                // Find opposite vertices to edges
                var oppositeVertices = new Dictionary<Edge, int>();
                foreach (var face in newFaces)
                {
                    oppositeVertices[new Edge(face.V1, face.V2)] = face.V3;
                    oppositeVertices[new Edge(face.V2, face.V3)] = face.V1;
                    oppositeVertices[new Edge(face.V3, face.V1)] = face.V2;
                }

                // Find edges to resample
                var edgesToSplit = new List<Edge>();
                var facesToSplit = new List<Face>();

                foreach (var face in newFaces)
                {
                    if (CheckSplitEdge(new Edge(face.V1, face.V2), vertexTransformations, verticesList))
                    {
                        edgesToSplit.Add(new Edge(face.V1, face.V2));
                        facesToSplit.Add(face);
                    }
                    if (CheckSplitEdge(new Edge(face.V2, face.V3), vertexTransformations, verticesList))
                    {
                        edgesToSplit.Add(new Edge(face.V2, face.V3));
                        facesToSplit.Add(face);
                    }
                    if (CheckSplitEdge(new Edge(face.V3, face.V1), vertexTransformations, verticesList))
                    {
                        edgesToSplit.Add(new Edge(face.V3, face.V1));
                        facesToSplit.Add(face);
                    }
                }

                if (edgesToSplit.Count == 0)
                    break;

                // Split edges
                for (var i = 0; i < edgesToSplit.Count; i++)
                {
                    var edgeToSplit = edgesToSplit[i];

                    var midPointIndex = verticesList.Count;
                    if (edgesMidPoints.ContainsKey(edgeToSplit.Unoriented()))
                    {
                        midPointIndex = edgesMidPoints[edgeToSplit.Unoriented()];
                    }
                    else
                    {
                        var oldMid = 0.5f * (vertexTransformations[edgeToSplit.V1].Conjugate().Transform(verticesList[edgeToSplit.V1]) + vertexTransformations[edgeToSplit.V2].Conjugate().Transform(verticesList[edgeToSplit.V2]));
                        var affinity = AffinityCalculation.GetCentersAffinity();
                        var affinityThreshold = 0.1;
                        var (vertexIndices, vertexWeights) = ComputeCustomWeightsForVertex(kdTree, vertices, oldMid, affinity, affinityThreshold, oldCenters);
                        Centers[frameIndex].Add(vertexIndices);
                        Weights[frameIndex].Add(vertexWeights);
                        var newMid = TransformPoint(oldMid, midPointIndex, frameIndex, transformations, out var transform);
                        //newMid = 0.5f * (newVerticesList[edge.V1] + -newVerticesList[edge.V2]);
                        vertexTransformations.Add(midPointIndex, transform);
                        edgesMidPoints[edgeToSplit.Unoriented()] = midPointIndex;
                        verticesList.Add(newMid);
                    }

                    /*if (!oppositeVertices.ContainsKey(edgeToSplit))
                    {
                        Debug.LogError($"Missing opposite vertex to ({edgeToSplit.V1}, {edgeToSplit.V2})");
                        Debug.LogError(oppositeVertices.ContainsKey(edgeToSplit.Opposite()));
                    }*/

                    var faceToSplit = facesToSplit[i];
                    if (!newFaces.Contains(faceToSplit)) // Replace with newer face{
                        faceToSplit = newFaces.Single(f => f.Edges.Contains(edgeToSplit));

                    newFaces.Remove(faceToSplit);
                    newFaces.Add(new Face
                    {
                        V1 = oppositeVertices[edgeToSplit],
                        V2 = edgeToSplit.V1,
                        V3 = midPointIndex
                    });
                    newFaces.Add(new Face
                    {
                        V1 = oppositeVertices[edgeToSplit],
                        V2 = midPointIndex,
                        V3 = edgeToSplit.V2
                    });

                    oppositeVertices[new Edge(edgeToSplit.V1, midPointIndex)] = oppositeVertices[edgeToSplit];
                    oppositeVertices[new Edge(midPointIndex, edgeToSplit.V2)] = oppositeVertices[edgeToSplit];

                    oppositeVertices[new Edge(oppositeVertices[edgeToSplit], midPointIndex)] = edgeToSplit.V2;
                    oppositeVertices[new Edge(midPointIndex, oppositeVertices[edgeToSplit])] = edgeToSplit.V1;

                    oppositeVertices[new Edge(oppositeVertices[edgeToSplit], edgeToSplit.V1)] = midPointIndex;
                    oppositeVertices[new Edge(edgeToSplit.V2, oppositeVertices[edgeToSplit])] = midPointIndex;
                    oppositeVertices.Remove(edgeToSplit);
                }
            }

            return new TriangleMesh
            {
                Vertices = verticesList.ToArray(),
                Faces = newFaces.ToArray()
            };
        }

        private bool[] CheckSplitFace(IList<Face> faces, int f, Dictionary<int, DualQuaternion> vertexTransformations, IList<Vector3> newVerticesList, out bool splitFace)
        {
            var faceEdges = new Edge[] { new Edge(faces[f].V1, faces[f].V2), new Edge(faces[f].V2, faces[f].V3), new Edge(faces[f].V3, faces[f].V1) };
            var splitEdges = new bool[3];
            //var splitVertices = new int[3];
            var e = 0;
            foreach (var edge in faceEdges)
            {
                var originalSize = (vertexTransformations[edge.V1].Conjugate().Transform(newVerticesList[edge.V1]) - vertexTransformations[edge.V2].Conjugate().Transform(newVerticesList[edge.V2])).Length();
                var newSize = (newVerticesList[edge.V1] - newVerticesList[edge.V2]).Length();

                if (newSize / originalSize >= 2)
                {
                    splitEdges[e] = true;
                }

                e++;
            }

            splitFace = SplitFace(splitEdges);

            return splitEdges;
        }

        private bool CheckSplitEdge(Edge edge, Dictionary<int, DualQuaternion> vertexTransformations, IList<Vector3> newVerticesList)
        {
            var a = vertexTransformations[edge.V1];
            var b = vertexTransformations[edge.V2];
            var a2b = a * b.Conjugate();
            var cos = a2b.Real.W;
            //var originalSize = (vertexTransformations[edge.V1].Conjugate().Transform(newVerticesList[edge.V1]) - vertexTransformations[edge.V2].Conjugate().Transform(newVerticesList[edge.V2])).magnitude;
            //var newSize = (newVerticesList[edge.V1] - newVerticesList[edge.V2]).magnitude;

            if (cos < System.Math.Cos(System.Math.PI * 0.01))
            {
                return true;
            }

            return false;
        }

        private bool SplitFace(bool[] splitEdges)
        {
            return splitEdges[0] || splitEdges[1] || splitEdges[2];
        }

        private Vector3 TransformPoint(Vector3 point, int pointIndex, int frameIndex, DualQuaternion[] transformations, out DualQuaternion transform)
        {
            var weightedTransformation = DualQuaternion.Zero();

            for (var c = 0; c < Centers[frameIndex][pointIndex].Length; c++)
            {
                var centerIndex = Centers[frameIndex][pointIndex][c];
                var weight = Weights[frameIndex][pointIndex][c];

                weightedTransformation += weight * transformations[centerIndex];
            }

            transform = weightedTransformation.Normalize();
            return transform.Transform(point);
        }

        private void ComputeWeights(Vector3[] vertices, Vector3[] oldCenters, int frameIndex)
        {
            var indices = new int[vertices.Length][];
            var weights = new float[vertices.Length][];

            // Prepare centers KD-tree
            var kdTree = new KdTree<float, int>(3, new FloatMath());
            for (var c = 0; c < oldCenters.Length; c++)
            {
                var centerPosition = oldCenters[c];
                kdTree.Add(new float[] { centerPosition.X, centerPosition.Y, centerPosition.Z }, c);
            }

            // Get affinity matrix
            var affinity = AffinityCalculation.GetCentersAffinity();
            var affinityThreshold = 0.1;

            // For each vertex
            Parallel.For(0, vertices.Length, v =>
            {
                // ComputeCustomWeightsForVertex(kdTree, vertices, v, affinity, affinityThreshold, oldCenters, indices, weights);
                var (vertexIndices, vertexWeights) = ComputeCustomWeightsForVertex(kdTree, vertices, vertices[v], affinity, affinityThreshold, oldCenters);
                indices[v] = vertexIndices;
                weights[v] = vertexWeights;
            });

            lock (this)
            {
                Centers[frameIndex] = indices.ToList();
                Weights[frameIndex] = weights.ToList();
            }
        }

        private (int[], float[]) ComputeCustomWeightsForVertex(KdTree<float, int> centersKdTree, Vector3[] vertices, Vector3 vertex, float[,] affinity, double affinityThreshold, Vector3[] oldCenters)
        {
            //return ComputeEmbeddedDeformationsWeightsForVertex(centersKdTree, vertices, vertex, affinity, affinityThreshold, oldCenters);
            // Find k-nearest center
            var searchCount = Neighbors;
            var found = 0;
            KdTreeNode<float, int>[] nearestCenters = null;
            int nearestCenter;
            do
            {
                nearestCenters = centersKdTree.GetNearestNeighbours(new float[] { vertex.X, vertex.Y, vertex.Z }, searchCount);
                searchCount *= 2;
                nearestCenter = nearestCenters[0].Value;
                found = 0;
                for (var i = 0; i < nearestCenters.Length; i++)
                {
                    if (nearestCenters[i] == null)
                        continue;

                    var centerIndex = nearestCenters[i].Value;
                    if (affinity[nearestCenter, centerIndex] > affinityThreshold)
                    {
                        found++;
                    }
                }

                if (searchCount > 200)
                    affinityThreshold = 0;

            } while (found < Neighbors);

            var mostAffineCenters = nearestCenters.Select(n => n.Value).Take(Neighbors).ToArray();

            // var nearestCenter = kdTree.Nearest(new double[] { vertices[v].X, vertices[v].Y, vertices[v].Z }).Value;

            // Find top N most affine centers with N
            /*var centerAffinities = new List<float>();
            for (var c = 0; c < oldCenters.Length; c++)
            {
                centerAffinities.Add(affinity[nearestCenter, c]);
            }
            var mostAffineCenters = centerAffinities.Select((a, i) => (a, i)).OrderByDescending(x => x.a).Take(Neighbors.Value).Select(x => x.i).ToArray();*/

            // Compute distances
            var distances = new float[Neighbors];
            var distMin = float.PositiveInfinity;
            for (var i = 0; i < distances.Length; i++)
            {
                distances[i] = (oldCenters[mostAffineCenters[i]] - vertex).Length();
                if (distances[i] < distMin)
                    distMin = distances[i];
            }

            // Compute soft minimums vector
            var softmin = new float[Neighbors];
            var softminSum = 0f;
            var softminMin = float.PositiveInfinity;
            for (var i = 0; i < softmin.Length; i++)
            {
                softminSum += (float)System.Math.Exp(-distances[i] / (Shape * distMin + LimitEpsilon));
            }
            for (var i = 0; i < softmin.Length; i++)
            {
                softmin[i] = (float)System.Math.Exp(-distances[i] / (Shape * distMin + LimitEpsilon)) / softminSum;

                if (softmin[i] < softminMin)
                {
                    softminMin = softmin[i];
                }
            }

            var weights1 = new float[Neighbors];
            var weightsSum = 0f;
            for (var i = 0; i < weights1.Length; i++)
            {
                //weights1[i] = 1 - (softmin[i] / (softminMin - LimitEpsilon.Value));// TODO Square
                weights1[i] = softmin[i] - softminMin + 1e-6f;
                //weights1[i] = (softmin[i] - softminMin) * (softmin[i] - softminMin);
                weightsSum += weights1[i];
            }

            for (var i = 0; i < weights1.Length; i++)
            {
                weights1[i] /= weightsSum;
            }

            return (mostAffineCenters, weights1);
        }

        private (int[], float[]) ComputeEmbeddedDeformationsWeightsForVertex(KdTree<float, int> centersKdTree, Vector3[] vertices, Vector3 vertex, float[,] affinity, double affinityThreshold, Vector3[] oldCenters)
        {
            // Find k-nearest center
            var searchCount = Neighbors;
            var found = 0;
            KdTreeNode<float, int>[] nearestCenters = null;
            int nearestCenter;
            do
            {
                nearestCenters = centersKdTree.GetNearestNeighbours(new float[] { vertex.X, vertex.Y, vertex.Z }, searchCount);
                searchCount *= 2;
                nearestCenter = nearestCenters[0].Value;
                found = 0;
                for (var i = 0; i < nearestCenters.Length; i++)
                {
                    if (nearestCenters[i] == null)
                        continue;

                    var centerIndex = nearestCenters[i].Value;
                    if (affinity[nearestCenter, centerIndex] > affinityThreshold)
                    {
                        found++;
                    }
                }

                if (searchCount > 200)
                    affinityThreshold = 0;

            } while (found < Neighbors);

            var mostAffineCenters = nearestCenters.Select(n => n.Value).Take(Neighbors).ToArray();

            // Compute distances
            var distances = new float[Neighbors];
            var distMax = float.PositiveInfinity;
            for (var i = 0; i < distances.Length; i++)
            {
                distances[i] = (oldCenters[mostAffineCenters[i]] - vertex).Length();
                if (distances[i] > distMax)
                    distMax = distances[i];
            }

            var weights1 = new float[Neighbors];
            var weightsSum = 0f;
            for (var i = 0; i < weights1.Length; i++)
            {
                weights1[i] = (float) System.Math.Pow(1 - distances[i] / distMax, 2);
                weightsSum += weights1[i];
            }

            for (var i = 0; i < weights1.Length; i++)
            {
                weights1[i] /= weightsSum;
            }

            return (mostAffineCenters, weights1);
        }

        private void ComputeNearestWeightsForVertex(KdTree<float, int> centersKdTree, Vector3[] vertices, int v, float[,] affinity, double affinityThreshold, Vector3[] oldCenters, int[,] indices, float[,] weights)
        {
            // Find k-nearest center
            var searchCount = 1;
            var found = 0;
            KdTreeNode<float, int>[] nearestCenters = null;
            int nearestCenter;
            nearestCenters = centersKdTree.GetNearestNeighbours(new float[] { vertices[v].X, vertices[v].Y, vertices[v].Z }, searchCount);

            var nearest2 = -1;
            var minDist = float.PositiveInfinity;

            for (var c = 0; c < oldCenters.Length; c++)
            {

                var dist = (oldCenters[c] - vertices[v]).Length();
                if (dist < minDist)
                {
                    minDist = dist;
                    nearest2 = c;
                }
            }

            var mostAffineCenter = nearestCenters.Select(n => n.Value).Take(searchCount).ToArray()[0];

            indices[v, 0] = nearest2;
            weights[v, 0] = 1;
        }

        public DualQuaternion[] ComputeDeformations(Vector3[] vertices, Face[] faces, Vector3[] oldCenters, Vector3[] newCenters, int frameIndex, DualQuaternion[] transformations)
        {
            if (!Centers.ContainsKey(frameIndex))  // Also on change
                ComputeWeights(vertices, oldCenters, frameIndex);
            /*else
                ConsoleView.Log($"Weights precomputed: {frameIndex}");*/

            //faces = faces.Where(f => f.V1 == 1922 || f.V2 == 1922 || f.V3 == 1922 || f.V1 == 37 || f.V2 == 37 || f.V3 == 37).ToArray();

            var centersArray = Centers[frameIndex];
            var weightsArray = Weights[frameIndex];

            // Queue with edges to split
            var verticesList = new List<Vector3>();

            // Original vertices deformations
            var vertexTransformations = new Dictionary<int, DualQuaternion>();
            for (var v = 0; v < vertices.Length; v++)
            {
                var weightedTransformation = DualQuaternion.Zero();

                for (var c = 0; c < centersArray[v].Length; c++)
                {
                    var centerIndex = centersArray[v][c];
                    var weight = weightsArray[v][c];

                    weightedTransformation += weight * transformations[centerIndex];
                }

                verticesList.Add(weightedTransformation.Normalize().Transform(vertices[v]));
                vertexTransformations[v] = weightedTransformation.Normalize();
            }

            var deformations = new DualQuaternion[vertices.Length];

            for (var i = 0; i < deformations.Length; i++)
            {
                deformations[i] = vertexTransformations[i];
            }

            return deformations;
        }
    }
}
