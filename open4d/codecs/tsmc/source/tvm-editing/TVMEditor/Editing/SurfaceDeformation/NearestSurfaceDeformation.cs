using System.Numerics;
using System.Threading.Tasks;
using TVMEditor.Structures;

namespace TVMEditor.Editing.SurfaceDeformation
{
    public class NearestSurfaceDeformation : ISurfaceDeformation
    {
        public TriangleMesh DeformSurface(Vector3[] vertices, Face[] faces, Vector3[] oldCenters, Vector3[] newCenters, int frameIndex, DualQuaternion[] transformations)
        {
            // TODO Use KD-Tree
            var newVertices = new Vector3[vertices.Length];

            Parallel.For(0, vertices.Length, i =>
            // for (var i = 0; i < vertices.Length; i++)
            {
                var minDist = float.PositiveInfinity;
                var minDistIndex = -1;

                for (var j = 0; j < oldCenters.Length; j++)
                {
                    var dist = (oldCenters[j] - vertices[i]).LengthSquared();
                    if (dist < minDist)
                    {
                        minDist = dist;
                        minDistIndex = j;
                    }
                }

                // newVertices[i] = (newCenters[minDistIndex] - oldCenters[minDistIndex]) + vertices[i];
                newVertices[i] = transformations[minDistIndex].Transform(vertices[i]);
            });

            return new TriangleMesh
            {
                Vertices = newVertices,
                Faces = faces
            };
        }

        public DualQuaternion[] ComputeDeformations(Vector3[] vertices, Face[] faces, Vector3[] oldCenters, Vector3[] newCenters, int frameIndex, DualQuaternion[] transformations)
        {
            var deformations = new DualQuaternion[vertices.Length];

            Parallel.For(0, vertices.Length, i =>
            // for (var i = 0; i < vertices.Length; i++)
            {
                var minDist = float.PositiveInfinity;
                var minDistIndex = -1;

                for (var j = 0; j < oldCenters.Length; j++)
                {
                    var dist = (oldCenters[j] - vertices[i]).LengthSquared();
                    if (dist < minDist)
                    {
                        minDist = dist;
                        minDistIndex = j;
                    }
                }

                // newVertices[i] = (newCenters[minDistIndex] - oldCenters[minDistIndex]) + vertices[i];
                deformations[i] = transformations[minDistIndex];
            });

            return deformations;
        }
    }
}
