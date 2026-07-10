using g3;
using System.Numerics;

namespace TVMEditor.Structures
{
    public class TriangleMesh
    {
        public Vector3[] Vertices { get; set; }
        public Face[] Faces { get; set; }

        public Vector3[] Normals { get; set; }
        public bool HasNormals => Normals != null;

        public void ComputeNormals()
        {
            Normals = new Vector3[Vertices.Length];

            for (var t = 0; t < Faces.Length; t++)
            {
                var faceNormal = FaceNormal(t);
                Normals[Faces[t].V1] += faceNormal;
                Normals[Faces[t].V2] += faceNormal;
                Normals[Faces[t].V3] += faceNormal;
            }

            for (var v = 0; v < Vertices.Length; v++)
            {
                Normals[v] /= Normals[v].Length();
            }
        }

        private Vector3 FaceNormal(int t)
        {
            var normal = Vector3.Cross(Vertices[Faces[t].V3] - Vertices[Faces[t].V1], Vertices[Faces[t].V2] - Vertices[Faces[t].V1]);
            return normal / normal.Length();
        }

        public int[] GetUnityFaces()
        {
            var unityFaces = new int[Faces.Length * 3];

            for (var i = 0; i < Faces.Length; i++)
            {
                unityFaces[3 * i] = Faces[i].V1;
                unityFaces[3 * i + 1] = Faces[i].V2;
                unityFaces[3 * i + 2] = Faces[i].V3;
            }

            return unityFaces;
        }

        public TriangleMesh Clone()
        {
            return new TriangleMesh
            {
                Vertices = (Vector3[])Vertices.Clone(),
                Faces = (Face[])Faces.Clone(),
            };
        }

        public Bounds GetBoundingBox()
        {
            var minX = float.PositiveInfinity;
            var minY = float.PositiveInfinity;
            var minZ = float.PositiveInfinity;
            var maxX = float.NegativeInfinity;
            var maxY = float.NegativeInfinity;
            var maxZ = float.NegativeInfinity;

            for (var v = 0; v < Vertices.Length; v++)
            {
                if (Vertices[v].X < minX)
                    minX = Vertices[v].X;

                if (Vertices[v].Y < minY)
                    minY = Vertices[v].Y;

                if (Vertices[v].Z < minZ)
                    minZ = Vertices[v].Z;

                if (Vertices[v].X > maxX)
                    maxX = Vertices[v].X;

                if (Vertices[v].Y > maxY)
                    maxY = Vertices[v].Y;

                if (Vertices[v].Z > maxZ)
                    maxZ = Vertices[v].Z;
            }

            var min = new Vector3(minX, minY, minZ);
            var max = new Vector3(maxX, maxY, maxZ);

            return new Bounds(0.5f * (min + max), (max - min));
        }

        public DMesh3 ToDMesh3()
        {
            var g3Mesh = new DMesh3();
            for (var v = 0; v < Vertices.Length; v++)
            {
                g3Mesh.AppendVertex(new Vector3d(Vertices[v].X, Vertices[v].Y, Vertices[v].Z));
            }

            for (var t = 0; t < Faces.Length; t++)
            {
                g3Mesh.AppendTriangle(new Index3i(Faces[t].V1, Faces[t].V2, Faces[t].V3));
            }

            return g3Mesh;
        }
    }
}
