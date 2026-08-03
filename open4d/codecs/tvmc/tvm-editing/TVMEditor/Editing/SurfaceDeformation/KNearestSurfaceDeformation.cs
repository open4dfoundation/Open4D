using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;
using TVMEditor.Structures;

namespace TVMEditor.Editing.SurfaceDeformation
{
    public class KNearestSurfaceDeformation : ISurfaceDeformation
    {
        private int Neighbors { get; set; } = 5;

        public DualQuaternion[] ComputeDeformations(Vector3[] vertices, Face[] faces, Vector3[] oldCenters, Vector3[] newCenters, int frameIndex, DualQuaternion[] transformations)
        {
            var newTransformations = new DualQuaternion[vertices.Length];

            Parallel.For(0, vertices.Length, i =>
            // for (var i = 0; i < vertices.Length; i++)
            {
                var distances = new float[oldCenters.Length];

                for (var j = 0; j < oldCenters.Length; j++)
                {
                    distances[j] = (oldCenters[j] - vertices[i]).LengthSquared();
                }

                var distancesWithIndices = distances.Select((d, id) => new Tuple<float, int>(d, id)).OrderBy(x => x.Item1).Take(Neighbors).ToArray();

                var transform = Vector3.Zero;
                var weightsSum = 0f;

                for (var n = 0; n < Neighbors; n++)
                {
                    var dist = distancesWithIndices[n].Item1;
                    var centerIndex = distancesWithIndices[n].Item2;
                    var weight = (float)System.Math.Exp(-0.1 * dist);
                    transform += weight * (newCenters[centerIndex] - oldCenters[centerIndex]);
                    weightsSum += weight;
                }

                transform /= weightsSum;

                newTransformations[i] = DualQuaternion.Translation(transform);
            });

            return newTransformations;
        }

        public TriangleMesh DeformSurface(Vector3[] vertices, Face[] faces, Vector3[] oldCenters, Vector3[] newCenters, int frameIndex, DualQuaternion[] transformations)
        {
            var newVertices = new Vector3[vertices.Length];

            Parallel.For(0, vertices.Length, i =>
            // for (var i = 0; i < vertices.Length; i++)
            {
                var distances = new float[oldCenters.Length];

                for (var j = 0; j < oldCenters.Length; j++)
                {
                    distances[j] = (oldCenters[j] - vertices[i]).LengthSquared();
                }

                var distancesWithIndices = distances.Select((d, id) => new Tuple<float, int>(d, id)).OrderBy(x => x.Item1).Take(Neighbors).ToArray();

                var transform = Vector3.Zero;
                var weightsSum = 0f;

                for (var n = 0; n < Neighbors; n++)
                {
                    var dist = distancesWithIndices[n].Item1;
                    var centerIndex = distancesWithIndices[n].Item2;
                    var weight = (float)System.Math.Exp(-0.1 * dist);
                    transform += weight * (newCenters[centerIndex] - oldCenters[centerIndex]);
                    weightsSum += weight;
                }

                transform /= weightsSum;

                newVertices[i] = transform + vertices[i];
            });

            return new TriangleMesh
            {
                Vertices = newVertices,
                Faces = faces
            };
        }
    }
}
