//
// Copyright (c) 2022,2023 Jan Dvoøák, Zuzana Káèereková, Petr Vanìèek, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Numerics;

namespace Framework
{
    /// <summary>
    /// Obj triangle mesh file loader module.
    /// </summary>
    public class ObjLoader
    {
        bool flipNormals = true;

        public bool FlipNormals
        {
            get { return flipNormals; }
            set { flipNormals = value; }
        }

        private string fileName = "mesh.obj";

        private TriangleMesh output;

        /// <summary>
        /// File to load data from. Should be in the .obj format. The property has no effect when there's a connection at the Filename port
        /// </summary>
        [Description("File to load data from. Should be in the .obj format. The property has no effect when there's a connection at the Filename port.")]
        public string FileName
        {
            get { return (fileName); }
            set
            {
                if (value != "")
                {
                    fileName = value;
                }
                else
                {
                    throw new Exception("FileName property cannot be set to an empty string!");
                }
            }
        }

        /// <summary>
        /// Loads the input file.
        /// I property values remain unchanged since the last run, then the file is not reloaded, unless the Realod property is set to true.
        /// </summary>
        public TriangleMesh Execute(string fn)
        {

            // first pass determines the number of vertices and the number of triangles
            StreamReader sr;
            try
            {
                sr = new StreamReader(fn);
            }
            catch (FileNotFoundException fnfe)
            {
                throw new Exception(fnfe.Message);
            }

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
            sr = new StreamReader(fn);
            Vector4[] vertices = new Vector4[vertexCount];
            Triangle[] triangles = new Triangle[triangleCount];

            int vi = 0;
            int ti = 0;
            Vector4 point;
            Triangle triangle;
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

                    float c1 = float.Parse(coords[1], nfi);
                    float c2 = float.Parse(coords[2], nfi);
                    float c3 = float.Parse(coords[3], nfi);

                    point = new Vector4(c1, c2, c3, 0);
                    vertices[vi] = point;
                    vi++;
                }

                // parsing of a triangle
                if ((line.StartsWith("f ")) || (line.StartsWith("fo ")) || (line.StartsWith("f\t")))
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
                        triangle = new Triangle(v1, v3, v2);
                    }
                    else
                    {
                        triangle = new Triangle(v1, v2, v3);
                    }

                    if (indices.Length == 4)
                    {
                        triangles[ti] = triangle;
                        ti++;
                    }

                    if (indices.Length == 5)
                    {
                        parts = indices[4].Split(new char[] { '/' }, StringSplitOptions.RemoveEmptyEntries);
                        v2 = int.Parse(parts[0]) - 1;

                        if (flipNormals)
                        {
                            triangle = new Triangle(v1, v2, v3);
                        }
                        else
                        {
                            triangle = new Triangle(v1, v3, v2);
                        }
                        triangles[ti] = triangle;
                        ti++;
                    }
                }
                line = sr.ReadLine();
                linePos++;
            }

            if (vertices.Length == 0)
                return null;
            // creating an output Mesh instance
            output = new TriangleMesh();
            output.Points = vertices;
            output.Triangles = triangles;
            return (output);
        }// Execute

    }//ObjLoader
}
