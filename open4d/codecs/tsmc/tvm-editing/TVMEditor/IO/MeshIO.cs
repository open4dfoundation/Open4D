using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;
using TVMEditor.Structures;

namespace TVMEditor.IO
{
    public static class MeshIO
    {
        private static bool flipNormals = false;
        private static object lockObject = new object();

        public static TriangleMesh LoadMeshFromObj(string filePath)
        {
            string fileContent;

            fileContent = File.ReadAllText(filePath);     

            // first pass determines the number of vertices and the number of triangles
            var sr = new StringReader(fileContent);

            int triangleCount = 0;
            int vertexCount = 0;
            int lineCount = 0;
            string line = sr.ReadLine();


            while (line != null)
            {
                line = line.Trim();

                if (line.StartsWith("v ")) vertexCount++;
                if ((line.StartsWith("f ")) || (line.StartsWith("fo ")) || line.StartsWith("f\t"))
                {
                    triangleCount++;
                    if (line.Split(new char[] { ' ', '\t' }).Length == 5)
                        triangleCount++;
                }
                line = sr.ReadLine();
                lineCount++;
            }

            sr.Close();

            // second pass performs the actual parsing
            sr = new StringReader(fileContent);
            Vector3[] vertices = new Vector3[vertexCount];
            Face[] triangles = new Face[triangleCount];

            int vi = 0;
            int ti = 0;
            //int ni = 0;
            //int tci = 0;
            Vector3 point;
            Face triangle/*, textureTriangle*/;
            NumberFormatInfo nfi = new NumberFormatInfo();
            nfi.NumberDecimalSeparator = ".";
            nfi.NumberGroupSeparator = ",";

            int linePos = 0;
            line = sr.ReadLine();
            while (line != null)
            {
                line = line.Trim();

                //parsing of a vertex
                if (line.StartsWith("v "))
                {
                    string[] coords = line.Split(new char[] { ' ' }, 4, StringSplitOptions.RemoveEmptyEntries);

                    var c1 = float.Parse(coords[1], nfi);
                    var c2 = float.Parse(coords[2], nfi);
                    var c3 = float.Parse(coords[3], nfi);

                    point = new Vector3(c1, c2, c3);
                    vertices[vi] = point;
                    vi++;
                }
                // parsing of a triangle
                else if ((line.StartsWith("f ")) || (line.StartsWith("fo ")) || (line.StartsWith("f\t")))
                {
                    string[] indices = line.Split(new char[] { ' ', '\t' }, 5, StringSplitOptions.RemoveEmptyEntries);

                    string[] parts = indices[1].Split(new char[] { '/' }, StringSplitOptions.RemoveEmptyEntries);
                    int v1 = int.Parse(parts[0]) - 1;

                    parts = indices[2].Split(new char[] { '/' }, StringSplitOptions.RemoveEmptyEntries);
                    int v2 = int.Parse(parts[0]) - 1;

                    parts = indices[3].Split(new char[] { '/' }, StringSplitOptions.RemoveEmptyEntries);
                    int v3 = int.Parse(parts[0]) - 1;

                    if (flipNormals)
                    {
                        triangle = new Face { V1 = v1, V2 = v3, V3 = v2 };
                    }
                    else
                    {
                        triangle = new Face { V1 = v1, V2 = v2, V3 = v3 };
                    }

                    if (indices.Length == 4)
                    {
                        triangles[ti] = triangle;
                        ti++;
                    }
                    else if (indices.Length == 5)
                    {
                        parts = indices[4].Split(new char[] { '/' }, StringSplitOptions.RemoveEmptyEntries);
                        v2 = int.Parse(parts[0]) - 1;

                        if (flipNormals)
                        {
                            triangle = new Face { V1 = v1, V2 = v2, V3 = v3 };
                        }
                        else
                        {
                            triangle = new Face { V1 = v1, V2 = v3, V3 = v2 };
                        }
                        triangles[ti] = triangle;
                        ti++;
                    }
                }
                line = sr.ReadLine();
                linePos++;
            }

            sr.Close();

            // creating an output Mesh instance
            return new TriangleMesh
            {
                Vertices = vertices.ToArray(),
                Faces = triangles.ToArray()
            };
        }

        public static TriangleMeshSequence LoadSequenceFromObj(string[] filesPaths)
        {
            var sequence = new TriangleMeshSequence
            {
                Meshes = new TriangleMesh[filesPaths.Length]
            };

            for (var i = 0; i < sequence.Meshes.Length; i++)
            {
                sequence.Meshes[i] = LoadMeshFromObj(filesPaths[i]);
            }

            return sequence;
        }

        public static TriangleMeshSequence LoadSequenceFromObj(string directoryPath)
        {
            var files = new DirectoryInfo(directoryPath).GetFiles("*.obj").OrderBy(f => f.FullName).Select(f => f.FullName).ToArray();
            return LoadSequenceFromObj(files);
        }

        public static void WriteSequenceToObj(string directoryPath, TriangleMeshSequence sequence)
        {
            if (!Directory.Exists(directoryPath))
                Directory.CreateDirectory(directoryPath);

            for (var i = 0; i < sequence.Meshes.Length; i++)
            {
                WriteMeshToObj($"{directoryPath}\\{i:000000}.obj", sequence.Meshes[i]);
            }
        }

        public static void WriteMeshToObj(string filePath, TriangleMesh mesh)
        {
            using (TextWriter tw = new StreamWriter(filePath))
            {
                foreach (var vertex in mesh.Vertices)
                {
                    tw.WriteLine(
                        $"v {vertex.X.ToString(CultureInfo.InvariantCulture)} {vertex.Y.ToString(CultureInfo.InvariantCulture)} {vertex.Z.ToString(CultureInfo.InvariantCulture)}");
                }
                foreach (var face in mesh.Faces)
                    tw.WriteLine($"f {face.V1 + 1} {face.V2 + 1} {face.V3 + 1}");
            }
        }
    }

}
